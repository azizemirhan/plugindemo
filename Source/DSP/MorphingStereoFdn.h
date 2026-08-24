#pragma once

#include <algorithm>
#include <cmath>

#include "ConfigResult.h"
#include "FdnTuning.h"
#include "SizeCompensation.h"
#include "StereoFdn.h"

namespace av::dsp
{

/**
    Two independent reverb tanks and a crossfade between them, so that Size can
    change without the tank being rebuilt underneath the signal.

    WHY TWO TANKS
    -------------
    An FDN's delays are whole samples and sit inside a feedback loop. Shortening
    one while it is ringing does not shorten the reverb, it deletes a piece of
    the circulating signal and re-reads memory that belongs to a different
    moment — a click at best, a burst of energy at worst. Smoothing the delay
    length does not help either: a delay that moves is a pitch shift, so a Size
    sweep would sound like tape flutter across the entire tail.

    So Size changes are handled the way a hardware reverb handles a program
    change. The inactive bank is reconfigured and reset while silent, both banks
    are then fed the same input, and only their *outputs* are crossfaded. The
    feedback loops never touch. Each bank keeps its own delay memory, its own
    gains and its own diffuser, and neither is ever asked to be two sizes at
    once.

    The crossfade is equal power (cos/sin), because the two tails are
    uncorrelated: a linear fade between uncorrelated signals dips about 3 dB in
    the middle, which is audible as a hole. Equal power keeps the sum's energy
    constant.

    PRIMING
    -------
    A transition has two stages, not one. A tank that has just been reset is
    empty: silent until its shortest delay elapses and quiet for several
    circulations after that. Fading straight into it fades from a full reverb
    into a nearly silent one — measured at 9.8 dB of level drop in the middle of
    a 120 ms fade, which is a dropout rather than a Size change.

    So the incoming bank is fed the input first while contributing nothing to
    the output, and only once it has filled does the crossfade begin. Priming
    costs nothing extra: both banks were going to run during the transition
    anyway.

    WHAT HAPPENS DURING A MORPH
    ---------------------------
    Both banks run, so the CPU cost doubles for the duration and no longer.
    That is the price of the approach and it is bounded: there are exactly two
    banks, never three, no matter how fast Size is automated.

    A Size change arriving mid-morph does not start a second morph. It is stored
    as the pending target, replacing any previous pending value, and applied
    once the current crossfade finishes. Automating Size continuously therefore
    produces a sequence of complete crossfades rather than an unbounded pile of
    half-finished ones, and changes smaller than a threshold are ignored
    entirely so a slow ramp does not retrigger on every block.

    Nothing here allocates, locks or blocks. Reconfiguring the inactive bank is
    a set of integer delay assignments into memory that prepare() already
    reserved.
*/
template <int N = fdn::order>
class MorphingStereoFdn
{
public:
    static constexpr int size = N;

    using Bank   = StereoFdn<N>;
    using Matrix = typename Bank::Matrix;

    /**
        Where the Size level compensation sits relative to the bank crossfade.

        During a morph the two banks are different sizes, so they need different
        compensation gains. There are two defensible orderings and they are not
        equivalent, so the choice is measured rather than assumed:

          perBankThenFade  each bank is corrected to its own Size, then the two
                           corrected outputs are crossfaded. Both ends of the
                           fade are at the right level throughout.

          fadeThenTarget   the raw outputs are crossfaded and the incoming Size's
                           correction is applied to the sum. Cheaper by one
                           multiply, but for the first half of the fade it
                           applies the new Size's correction to mostly the old
                           bank's signal.

        See "Size / crossfade ordering" in the test suite.
    */
    enum class CompensationOrder
    {
        perBankThenFade,
        fadeThenTarget
    };

    /** Why the priming stage ended. Reported for tests and diagnostics. */
    enum class PrimeReason
    {
        levelSettled,  /**< The incoming bank stopped filling, the normal case. */
        silence,       /**< Nothing was playing, so there was nothing to fill. */
        timedOut       /**< The ceiling was hit; the fade started regardless. */
    };

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    ConfigResult prepare (double sampleRate, double smoothingSeconds = 0.02)
    {
        if (sampleRate <= 0.0)
            return ConfigResult::invalidArgument;

        currentSampleRate = sampleRate;

        for (auto& bank : banks)
        {
            const auto result = bank.prepare (sampleRate, smoothingSeconds);

            if (! succeeded (result))
                return result;
        }

        for (auto& c : compensation)
        {
            c.prepare (sampleRate);
            c.setEnabled (compensationEnabled, true);
        }

        compensation[0].setSize (banks[0].getSizeScale(), true);
        compensation[1].setSize (banks[1].getSizeScale(), true);

        setMorphSeconds (morphSeconds);

        envelopeCoefficient = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
        fastCoefficient     = 1.0 - std::exp (-1.0 / (fdn::morphFastEnvelopeSeconds * sampleRate));
        slowCoefficient     = 1.0 - std::exp (-1.0 / (fdn::morphSlowEnvelopeSeconds * sampleRate));
        settledHoldSamples  = std::max (1, static_cast<int> (fdn::morphSettledHoldSeconds * sampleRate));

        active     = 0;
        phase      = Phase::idle;
        position   = 0.0;
        hasPending = false;

        return ConfigResult::ok;
    }

    // -----------------------------------------------------------------------
    //  Configuration — real-time safe.
    // -----------------------------------------------------------------------

    /** Requests a new Size. Starts a crossfade, or queues one if busy. */
    void setSize (double scale) noexcept
    {
        const double clamped = std::clamp (scale, fdn::minSizeScale, fdn::maxSizeScale);

        if (std::abs (clamped - requestedSize) < fdn::sizeMorphThreshold)
            return;

        requestedSize = clamped;

        if (phase != Phase::idle)
        {
            // Coalesce: only the most recent request survives.
            pendingSize = clamped;
            hasPending  = true;
            return;
        }

        beginMorph (clamped);
    }

    /** Decay is a gain change, not a topology change, so it never morphs. */
    void setRt60Seconds (double seconds, bool immediate = false) noexcept
    {
        rt60Seconds = seconds;

        for (auto& bank : banks)
            bank.setRt60Seconds (seconds, immediate);

        // The Size correction was measured to depend on the decay as well, so
        // it has to be told about a decay change.
        for (auto& c : compensation)
            c.setRt60Seconds (seconds, immediate);
    }

    /**
        Frequency-dependent decay, applied to both banks.

        Both must carry it, not just the active one: during a Size morph the
        two banks are crossfaded, and a bank that was damped differently would
        make the transition audible as a tone change rather than a size change.
        Each bank derives its own shelf gains from its own delay set, which is
        exactly what keeps the two sounding like the same reverb at different
        sizes.
    */
    void setDamping (double lowRatio, double highRatio,
                     double lowCornerHz, double highCornerHz,
                     bool immediate = false) noexcept
    {
        for (auto& bank : banks)
            bank.setDamping (lowRatio, highRatio, lowCornerHz, highCornerHz, immediate);
    }

    [[nodiscard]] bool isDampingActive() const noexcept
    {
        return banks[static_cast<size_t> (active)].isDampingActive();
    }

    void setMatrixType (typename Matrix::Type type) noexcept
    {
        for (auto& bank : banks)
            bank.setMatrixType (type);
    }

    void setInputDiffusionEnabled (bool enabled) noexcept
    {
        for (auto& bank : banks)
            bank.setInputDiffusionEnabled (enabled);
    }

    // --- Matrix modulation -------------------------------------------------
    /*
        Both banks share the same modulation settings, and each keeps its own
        LFO phase. Three policies were available:

          - one global phase shared by both banks;
          - independent but deterministic phase per bank;
          - starting the incoming bank from the outgoing bank's phase.

        The second is used. The banks hold different tanks with different delay
        sets, so a shared phase would not make their outputs correlate anyway,
        and copying phase across a swap would mean the modulation state depended
        on the Size history — the same session would render differently
        depending on what the user had done earlier. Each bank's lanes start
        from the same fixed seeds and offsets, so the whole thing stays
        reproducible.
    */

    void setModulationDepth (double d, bool immediate = false) noexcept
    {
        for (auto& bank : banks)
            bank.setModulationDepth (d, immediate);
    }

    void setModulationRateHz (double hz) noexcept
    {
        for (auto& bank : banks)
            bank.setModulationRateHz (hz);
    }

    void setModulationShape (LfoShape shape) noexcept
    {
        for (auto& bank : banks)
            bank.setModulationShape (shape);
    }

    void setModulationSchedule (const modulation::Schedule& s) noexcept
    {
        for (auto& bank : banks)
            bank.setModulationSchedule (s);
    }

    void setModulationUpdateMode (typename Matrix::UpdateMode m) noexcept
    {
        for (auto& bank : banks)
            bank.setModulationUpdateMode (m);
    }

    // --- Allpass phase modulation ------------------------------------------

    void setAllpassDepth (double d, bool immediate = false) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassDepth (d, immediate);

        // Engaging the bank lengthens every loop by a fixed number of samples,
        // which is a larger proportion at small Sizes and shows up as a level
        // change of up to 0.58 dB. Depth beyond zero does not matter — a
        // coefficient cannot change a pole count — so this is a switch.
        for (auto& c : compensation)
            c.setAllpassEngaged (d > 0.0, immediate);
    }

    void setAllpassRateHz (double hz) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassRateHz (hz);
    }

    void setAllpassShape (LfoShape shape) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassShape (shape);
    }

    void setAllpassMode (TimeVaryingAllpass::Mode m) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassMode (m);
    }

    void setAllpassActiveLines (int count) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassActiveLines (count);
    }

    void setAllpassSectionsPerLine (int count) noexcept
    {
        for (auto& bank : banks)
            bank.setAllpassSectionsPerLine (count);
    }

    void setLoopDelayCorrectionEnabled (bool enabled) noexcept
    {
        for (auto& bank : banks)
            bank.setLoopDelayCorrectionEnabled (enabled);
    }

    // --- Size level compensation -------------------------------------------

    void setCompensationEnabled (bool enabled, bool immediate = false) noexcept
    {
        compensationEnabled = enabled;

        for (auto& c : compensation)
            c.setEnabled (enabled, immediate);
    }

    [[nodiscard]] bool isCompensationEnabled() const noexcept { return compensationEnabled; }

    void setCompensationOrder (CompensationOrder order) noexcept { compensationOrder = order; }
    [[nodiscard]] CompensationOrder getCompensationOrder() const noexcept { return compensationOrder; }

    [[nodiscard]] double getCompensationGain (int bank) const noexcept
    {
        return compensation[static_cast<size_t> (bank)].getCurrentGain();
    }

    void setInputDiffusion (double amount, bool immediate = false) noexcept
    {
        for (auto& bank : banks)
            bank.setInputDiffusion (amount, immediate);
    }

    void setMorphSeconds (double seconds) noexcept
    {
        morphSeconds = std::clamp (seconds, 0.005, 2.0);
        increment    = 1.0 / std::max (1.0, morphSeconds * currentSampleRate);
    }

    /** Upper bound on the priming stage. The fade normally starts earlier,
        when the incoming bank's level has caught up. Setting this to zero
        disables priming entirely, which is what the measurement in
        FdnMorphTests.cpp uses as its control case. */
    void setPrimeCapSeconds (double seconds) noexcept
    {
        primeCapSeconds = std::clamp (seconds, 0.0, 4.0);
    }

    /** Lower bound on the priming stage, so the incoming bank always gets long
        enough to produce output from every one of its delay lines. */
    void setPrimeMinimumSeconds (double seconds) noexcept
    {
        primeMinimumSeconds = std::clamp (seconds, 0.0, 4.0);
    }

    /** Turns the "incoming bank has settled" trigger off, so the transition is
        purely time-based. Used by the measurements as a control case. */
    void setSettlingTriggerEnabled (bool enabled) noexcept { settlingEnabled = enabled; }

    /** Clears both banks and cancels any morph in progress. */
    void reset() noexcept
    {
        for (auto& bank : banks)
            bank.reset();

        for (auto& c : compensation)
            c.reset();

        phase      = Phase::idle;
        position   = 0.0;
        hasPending = false;
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    void processSample (double& l, double& r) noexcept
    {
        if (phase == Phase::idle)
        {
            banks[static_cast<size_t> (active)].processSample (l, r);

            // Compensation is outside the loop, on the way out. The inactive
            // bank's smoother is advanced too, so that when it becomes active
            // its gain is where it should be rather than where it was left.
            compensation[static_cast<size_t> (active)].process (l, r);
            (void) compensation[static_cast<size_t> (1 - active)].nextGain();

            // The outgoing envelope is tracked all the time, not only during a
            // transition. Starting it from zero when a morph begins meant it
            // was still climbing while the incoming bank was compared against
            // it, so the ratio crossed its threshold far too early and the
            // level trigger behaved like no trigger at all.
            trackOutgoing (l, r);
            return;
        }

        const int incoming = 1 - active;

        double outgoingL = l, outgoingR = r;
        double incomingL = l, incomingR = r;

        banks[static_cast<size_t> (active)].processSample (outgoingL, outgoingR);
        banks[static_cast<size_t> (incoming)].processSample (incomingL, incomingR);

        // Both gains are advanced every sample whichever ordering is in use, so
        // that switching ordering cannot leave a smoother stranded and so that
        // the incoming bank's gain has finished moving by the time it is heard.
        const double outgoingGain = compensation[static_cast<size_t> (active)].nextGain();
        const double incomingGain = compensation[static_cast<size_t> (incoming)].nextGain();

        if (compensationOrder == CompensationOrder::perBankThenFade)
        {
            outgoingL *= outgoingGain;  outgoingR *= outgoingGain;
            incomingL *= incomingGain;  incomingR *= incomingGain;
        }

        if (phase == Phase::priming)
        {
            // The incoming bank is filling. It is fed but not heard.
            l = outgoingL;
            r = outgoingR;

            // Nothing has crossfaded yet, so under either ordering what is heard
            // is the outgoing bank at the outgoing bank's own correction.
            if (compensationOrder == CompensationOrder::fadeThenTarget)
            {
                l *= outgoingGain;
                r *= outgoingGain;
            }

            // Track both banks' levels and start the fade once the incoming one
            // has caught up. Rectified one-poles, so this costs four
            // multiply-adds and no branching on the decay setting.
            trackOutgoing (outgoingL, outgoingR);

            const double incomingLevel = std::abs (incomingL) + std::abs (incomingR);

            incomingFast += fastCoefficient * (incomingLevel - incomingFast);
            incomingSlow += slowCoefficient * (incomingLevel - incomingSlow);

            ++primeElapsed;

            // Three independent ways out, so the transition always terminates.
            //
            //   warmedUp   the new tank has had time to produce output from
            //              every line, including its longest
            //   settled    its own level has stopped rising
            //   timedOut   the hard ceiling, for inputs whose level never
            //              settles: silence, guard-level noise, or a decaying
            //              single impulse
            const bool warmedUp = primeElapsed >= primeMinSamples;

            if (settlingEnabled
                && incomingFast > 1.0e-12
                && incomingFast <= incomingSlow * (1.0 + fdn::morphSettledTolerance))
                ++settledHeld;
            else
                settledHeld = 0;

            const bool settled = warmedUp && settledHeld >= settledHoldSamples;

            // Silence: nothing filling and nothing to hear. Still honours the
            // warm-up, so a transition takes the same time whether or not audio
            // is playing.
            const bool nothingToMatch = warmedUp
                                        && outgoingEnvelope < 1.0e-9
                                        && incomingSlow < 1.0e-12;

            const bool timedOut = --primeCountdown <= 0;

            if (settled || nothingToMatch || timedOut)
            {
                lastPrimeSamples = primeElapsed;
                lastPrimeReason  = nothingToMatch ? PrimeReason::silence
                                 : settled        ? PrimeReason::levelSettled
                                                  : PrimeReason::timedOut;

                phase    = Phase::fading;
                position = 0.0;
            }

            return;
        }

        constexpr double halfPi = 1.57079632679489661923;

        const double fadeOut = std::cos (position * halfPi);
        const double fadeIn  = std::sin (position * halfPi);

        l = fadeOut * outgoingL + fadeIn * incomingL;
        r = fadeOut * outgoingR + fadeIn * incomingR;

        if (compensationOrder == CompensationOrder::fadeThenTarget)
        {
            l *= incomingGain;
            r *= incomingGain;
        }

        position += increment;

        if (position >= 1.0)
            finishMorph();
    }

    void processBlock (double* leftData, double* rightData, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            processSample (leftData[n], rightData[n]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] bool   isMorphing()      const noexcept { return phase != Phase::idle; }
    [[nodiscard]] bool   isPriming()       const noexcept { return phase == Phase::priming; }
    [[nodiscard]] bool   isFading()        const noexcept { return phase == Phase::fading; }
    [[nodiscard]] bool   hasPendingSize()  const noexcept { return hasPending; }
    [[nodiscard]] double getPrimeCapSeconds() const noexcept { return primeCapSeconds; }
    [[nodiscard]] bool   isSettlingTriggerEnabled() const noexcept { return settlingEnabled; }

    /** How the last priming stage ended, and how long it lasted. */
    [[nodiscard]] PrimeReason getLastPrimeReason()  const noexcept { return lastPrimeReason; }
    [[nodiscard]] int         getLastPrimeSamples() const noexcept { return lastPrimeSamples; }

    /** Longest a transition can possibly take, in samples. */
    [[nodiscard]] int getWorstCaseTransitionSamples() const noexcept
    {
        return static_cast<int> ((primeCapSeconds + morphSeconds) * currentSampleRate) + 4;
    }

    [[nodiscard]] static constexpr const char* describe (PrimeReason reason) noexcept
    {
        switch (reason)
        {
            case PrimeReason::levelSettled: return "level settled";
            case PrimeReason::silence:      return "silence";
            case PrimeReason::timedOut:     return "timed out";
        }

        return "unknown";
    }
    [[nodiscard]] double getMorphPosition() const noexcept { return position; }
    [[nodiscard]] double getSize()         const noexcept { return requestedSize; }
    [[nodiscard]] double getMorphSeconds() const noexcept { return morphSeconds; }
    [[nodiscard]] double getRt60Seconds()  const noexcept { return rt60Seconds; }
    [[nodiscard]] int    getActiveBankIndex() const noexcept { return active; }

    /** The last configuration failure, if a Size request could not be honoured. */
    [[nodiscard]] ConfigResult getLastConfigResult() const noexcept { return lastResult; }

    [[nodiscard]] const Bank& getBank (int index) const noexcept
    {
        return banks[static_cast<size_t> (index)];
    }

    [[nodiscard]] Bank& getBank (int index) noexcept
    {
        return banks[static_cast<size_t> (index)];
    }

    [[nodiscard]] const Bank& getActiveBank() const noexcept { return getBank (active); }
    [[nodiscard]] Bank&       getActiveBank()       noexcept { return getBank (active); }

private:
    void trackOutgoing (double l, double r) noexcept
    {
        const double level = std::abs (l) + std::abs (r);
        outgoingEnvelope += envelopeCoefficient * (level - outgoingEnvelope);
    }

    void beginMorph (double scale) noexcept
    {
        const int incoming = 1 - active;
        auto& target = banks[static_cast<size_t> (incoming)];

        // Reset first: the bank may hold the tail of a tank two sizes ago, and
        // crossfading into that would be audible as an unrelated reverb.
        target.reset();
        target.resetModulation();   // deterministic phase, independent of Size history

        lastResult = target.setSizeScale (scale);

        if (! succeeded (lastResult))
        {
            // Nothing was applied to the incoming bank, so nothing is playing
            // that should not be. Stay on the current tank.
            requestedSize = banks[static_cast<size_t> (active)].getSizeScale();
            return;
        }

        target.setRt60Seconds (rt60Seconds, true);

        // The incoming bank's correction is snapped, not ramped: it is silent
        // right now, and it has to be at the right level the instant the
        // crossfade starts rather than still travelling towards it.
        compensation[static_cast<size_t> (incoming)].setSize (scale, true);

        // The ceiling follows the decay: a tank fills with the same time
        // constant it empties with, so there is no point waiting longer than
        // the fill takes, and no point waiting less.
        const double decayDerivedCap = std::clamp (fdn::morphPrimeDecayFactor * rt60Seconds,
                                                   primeMinimumSeconds, primeCapSeconds);

        position         = 0.0;
        primeCountdown   = std::max (1, static_cast<int> (decayDerivedCap * currentSampleRate));
        primeMinSamples  = std::min (primeCountdown,
                                     std::max (1, static_cast<int> (primeMinimumSeconds * currentSampleRate)));
        primeElapsed     = 0;
        settledHeld      = 0;
        incomingFast     = 0.0;   // the outgoing envelope is already warm
        incomingSlow     = 0.0;
        phase            = Phase::priming;
    }

    void finishMorph() noexcept
    {
        phase    = Phase::idle;
        position = 0.0;
        active   = 1 - active;

        // The bank that just faded out still holds a full tail. Clearing it now
        // means the next morph starts from silence and costs nothing later.
        banks[static_cast<size_t> (1 - active)].reset();

        if (hasPending)
        {
            hasPending = false;

            if (std::abs (pendingSize - banks[static_cast<size_t> (active)].getSizeScale())
                >= fdn::sizeMorphThreshold)
                beginMorph (pendingSize);
        }
    }

    enum class Phase { idle, priming, fading };

    Bank banks[2];

    int    active   = 0;
    Phase  phase    = Phase::idle;
    double position = 0.0;
    double increment = 1.0 / 5760.0;
    int    primeCountdown  = 0;
    int    primeMinSamples = 0;
    int    primeElapsed    = 0;
    int    lastPrimeSamples = 0;

    double primeCapSeconds     = fdn::morphPrimeMaxSeconds;
    double primeMinimumSeconds = fdn::morphPrimeMinSeconds;

    bool   settlingEnabled   = true;
    int    settledHeld       = 0;
    int    settledHoldSamples = 1;

    double incomingFast   = 0.0;
    double incomingSlow   = 0.0;
    double fastCoefficient = 1.0 / 7200.0;
    double slowCoefficient = 1.0 / 28800.0;

    PrimeReason lastPrimeReason = PrimeReason::levelSettled;

    double outgoingEnvelope    = 0.0;
    double envelopeCoefficient = 1.0 / 2400.0;

    SizeLevelCompensation compensation[2];
    CompensationOrder compensationOrder = CompensationOrder::perBankThenFade;
    bool   compensationEnabled = true;

    bool   hasPending    = false;
    double pendingSize   = 1.0;
    double requestedSize = 1.0;

    double rt60Seconds       = 2.0;
    double morphSeconds      = fdn::morphSeconds;
    double currentSampleRate = 48000.0;

    ConfigResult lastResult = ConfigResult::ok;
};

} // namespace av::dsp
