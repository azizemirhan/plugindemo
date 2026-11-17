#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "DSP/ConfigResult.h"
#include "DSP/Denormal.h"
#include "DSP/AllpassPhaseBank.h"
#include "DSP/DampingTuning.h"
#include "DSP/FdnDelayLine.h"
#include "DSP/FdnTuning.h"
#include "DSP/LineDamping.h"
#include "DSP/ModulatedFeedbackMatrix.h"
#include "DSP/ModulationTuning.h"
#include "DSP/SmoothedParameter.h"

/**
    PHASE 12 - STAGE 3. The research core.

    RESEARCH ONLY. NOT PRODUCTION. Source/ IS NOT CHANGED BY THIS FILE.

    Stage 2 could reach only what FdnCore already exposes, and found that
    almost everything it exposes is something a user control already does.
    Stage 3 is allowed new topology in the harness, and topology means
    reaching inside the feedback loop, which FdnCore has no seam for - by
    design, and that design is not being reopened for a research question.

    So ResearchCore is FdnCore, COPIED MECHANICALLY (a script, not a retyping;
    every statement outside the four seams below is production's own), with
    four seams added:

      1. hooks.processLoop   a per-line stage straight after the delay read:
                             static in-loop allpasses and in-loop dispersion.
      2. hooks.applyMatrix   an arbitrary static N x N matrix, applied before
                             the production modulated matrix. The caller sets
                             the production base type to identity, so the
                             production modulation rotations still follow.
      3. hooks.written       what each line was fed this sample, for readout
                             taps taken inside a line.
      4. hooks.extraLoopDelay  delay a hook added to a loop, so the decay gain
                             and the damping shelves are computed from the
                             delay the signal really experiences - the rule
                             FdnCore already follows for its own allpass.

    With the hooks at their defaults every seam is a branch not taken and an
    addition of zero, and the core must equal FdnCore BIT FOR BIT. That is
    tested before anything is measured with it.
*/
namespace avtest::stage3
{

using namespace av::dsp;

template <int N>
struct ResearchHooks
{
    // --- 2. static matrix ---------------------------------------------------
    bool   customMatrix = false;
    double matrix[N * N] {};          // row-major

    void applyMatrix (double* v) const noexcept
    {
        double out[N];

        for (int r = 0; r < N; ++r)
        {
            double sum = 0.0;

            for (int c = 0; c < N; ++c)
                sum += matrix[r * N + c] * v[c];

            out[r] = sum;
        }

        for (int i = 0; i < N; ++i)
            v[i] = out[i];
    }

    // --- 1. in-loop stages --------------------------------------------------
    static constexpr int maxLoopAllpasses = 3;
    static constexpr int maxDispersion    = 16;

    struct LoopAllpass
    {
        std::vector<double> buffer;
        size_t position = 0;
        double g = 0.0;

        /** Schroeder allpass, delay = buffer.size(). Flat magnitude. */
        [[nodiscard]] double process (double x) noexcept
        {
            const double delayed = buffer[position];
            const double w = x - g * delayed;

            buffer[position] = w;
            position = position + 1 < buffer.size() ? position + 1 : 0;

            return g * w + delayed;
        }
    };

    int loopAllpassCount = 0;

    /** STAGE 6. When set, each line has its own number of allpasses. */
    bool perLine = false;
    int  lineCount[N] {};
    LoopAllpass loopAllpass[N][maxLoopAllpasses];

    /** First-order allpasses, y = a x + s, s = x - a y. Flat magnitude; group
        delay (1-a^2)/(1+a^2+2a cos w): for a < 0, long at low frequency. */
    int    dispersionSections = 0;
    double dispersionCoeff    = 0.0;
    double dispersionState[N][maxDispersion] {};

    [[nodiscard]] double processLoop (double x, int line) noexcept
    {
        for (int k = 0; k < dispersionSections; ++k)
        {
            const double y = dispersionCoeff * x + dispersionState[line][k];
            dispersionState[line][k] = x - dispersionCoeff * y;
            x = y;
        }

        const int count = perLine ? lineCount[line] : loopAllpassCount;

        for (int k = 0; k < count; ++k)
            x = loopAllpass[line][k].process (x);

        return x;
    }

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            for (auto& s : dispersionState[i])
                s = 0.0;

            for (auto& a : loopAllpass[i])
            {
                std::fill (a.buffer.begin(), a.buffer.end(), 0.0);
                a.position = 0;
            }

            written[i] = 0.0;
        }
    }

    // --- 3, 4 ---------------------------------------------------------------
    double written[N] {};
    double extraLoopDelay[N] {};      // samples
};

template <int N = fdn::order>
class ResearchCore
{
public:
    static constexpr int size = N;

    using Matrix = ModulatedFeedbackMatrix<N>;

    ResearchCore()
    {
        for (int i = 0; i < N; ++i)
            requestedMs[static_cast<size_t> (i)] =
                fdn::delaysMs[static_cast<size_t> (std::min (i, fdn::order - 1))];
    }

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    /**
        Reserves storage for delays of up to `maxDelayMs` on every line.

        Capacity is stated explicitly rather than inferred from the current
        delay set, so a later Size increase is either possible or reported as
        impossible — never silently truncated.
    */
    ConfigResult prepare (double sampleRate, double maxDelayMs,
                          double gainSmoothingSeconds = 0.02)
    {
        if (sampleRate <= 0.0 || maxDelayMs <= 0.0)
            return ConfigResult::invalidArgument;

        currentSampleRate = sampleRate;
        capacitySamples   = static_cast<int> (std::ceil (maxDelayMs * 0.001 * sampleRate)) + 2;

        for (auto& line : lines)
            line.prepare (capacitySamples);

        for (auto& g : gains)
            g.prepare (sampleRate, gainSmoothingSeconds);

        for (auto& guard : guards)
            guard.reset();

        matrix.prepare (sampleRate);
        allpass.prepare (sampleRate);

        for (auto& d : dampers)
            d.prepare (sampleRate, gainSmoothingSeconds);

        // Snapped to whatever was already requested, the same way the delay
        // set is re-applied below: a corner asked for before prepare() must
        // not be glided to from a placeholder.
        lowCornerSm .prepare (sampleRate, damping::frequencyGlideSeconds, damping::minCutoffHz);
        highCornerSm.prepare (sampleRate, damping::frequencyGlideSeconds, damping::minCutoffHz);
        lowCornerSm .snapTo (requestedLowCornerHz);
        highCornerSm.snapTo (requestedHighCornerHz);

        dampingCounter = 0;

        prepared = true;

        // Re-apply whatever tuning was already requested, now that the lines
        // exist and know their capacity.
        const auto result = setDelaysMilliseconds (requestedMs.data(), requestedScale);

        if (! succeeded (result))
            return result;

        setRt60Seconds (rt60Seconds, true);
        return ConfigResult::ok;
    }

    // -----------------------------------------------------------------------
    //  Configuration — real-time safe, but changes the loop topology.
    // -----------------------------------------------------------------------

    /**
        Sets all N delays from millisecond values times a scale.

        Rounding is deterministic: nearest whole sample, computed from the
        millisecond value and the prepared sample rate, so the same request
        always produces the same tank at the same rate. Refused as a whole if
        any line would not fit; the previous delays stay in effect.
    */
    [[nodiscard]] ConfigResult setDelaysMilliseconds (const double* millisecondsPerLine,
                                                      double scale) noexcept
    {
        if (millisecondsPerLine == nullptr || scale <= 0.0)
            return ConfigResult::invalidArgument;

        std::array<int, N> candidate {};

        for (int i = 0; i < N; ++i)
        {
            const double samples = millisecondsPerLine[i] * scale * 0.001 * currentSampleRate;
            candidate[static_cast<size_t> (i)] = static_cast<int> (std::lround (samples));

            if (prepared && (candidate[static_cast<size_t> (i)] < 1
                             || candidate[static_cast<size_t> (i)] > capacitySamples))
                return ConfigResult::exceedsCapacity;
        }

        for (int i = 0; i < N; ++i)
            requestedMs[static_cast<size_t> (i)] = millisecondsPerLine[i];

        requestedScale = scale;

        if (! prepared)
            return ConfigResult::ok;

        for (int i = 0; i < N; ++i)
            if (! lines[static_cast<size_t> (i)].setDelaySamples (candidate[static_cast<size_t> (i)]))
                return ConfigResult::exceedsCapacity;

        // The decay gains depend on the delay lengths, so they have to follow.
        applyGains (true);
        return ConfigResult::ok;
    }

    /** Target broadband decay time. Never rebuilds the tank. */
    void setRt60Seconds (double seconds, bool immediate = false) noexcept
    {
        rt60Seconds = std::clamp (seconds, fdn::minRt60Seconds, fdn::maxRt60Seconds);
        applyGains (immediate);
    }

    // --- Frequency-dependent decay (PHASE 7) -------------------------------

    /**
        Sets the two band decay ratios and their corner frequencies.

        Ratios, not percentages and not filter coefficients: `lowRatio` is
        RT60_low / RT60_mid and `highRatio` is RT60_high / RT60_mid. 1.0 means
        that band decays with the mid band, and is a bit-exact bypass rather
        than a filter that happens to be flat. Values above 1 are clamped away:
        damping only ever shortens a band, which is what makes the stability
        argument survive untouched.

        The corners glide in the log domain at control rate; the shelf gains
        ramp at audio rate inside each LineDamping.
    */
    void setDamping (double lowRatio, double highRatio,
                     double lowCornerHz, double highCornerHz,
                     bool immediate = false) noexcept
    {
        lowRatio  = std::clamp (lowRatio,  damping::minShelfGain, 1.0);
        highRatio = std::clamp (highRatio, damping::minShelfGain, 1.0);

        requestedLowCornerHz  = lowCornerHz;
        requestedHighCornerHz = highCornerHz;

        dampingLowRatio  = lowRatio;
        dampingHighRatio = highRatio;

        lowCornerSm .setTarget (lowCornerHz);
        highCornerSm.setTarget (highCornerHz);

        if (immediate)
        {
            lowCornerSm .snapToTarget();
            highCornerSm.snapToTarget();
        }

        applyGains (immediate);
    }

    [[nodiscard]] double getDampingLowRatio()  const noexcept { return dampingLowRatio; }
    [[nodiscard]] double getDampingHighRatio() const noexcept { return dampingHighRatio; }

    /** The shelf pair currently targeted on one line. For tests and analysis. */
    [[nodiscard]] const damping::ShelfSpec& getDampingSpec (int i) const noexcept
    {
        return dampingSpec[static_cast<size_t> (i)];
    }

    /** 1 / max|H| on one line — the gain that makes its response peak at 1. */
    [[nodiscard]] double getDampingNormalisation (int i) const noexcept
    {
        return dampingNorm[static_cast<size_t> (i)];
    }

    [[nodiscard]] bool isDampingActive() const noexcept { return dampingActive; }

    void setMatrixType (typename Matrix::Type type) noexcept { matrix.setType (type); }
    [[nodiscard]] typename Matrix::Type getMatrixType() const noexcept { return matrix.getType(); }

    // --- Matrix modulation (PHASE 5A) -------------------------------------

    void setModulationDepth (double zeroToOne, bool immediate = false) noexcept
    {
        matrix.setDepth (zeroToOne, immediate);
    }

    void setModulationRateHz (double hz) noexcept          { matrix.setRateHz (hz); }
    void setModulationShape (LfoShape shape) noexcept      { matrix.setShape (shape); }
    void setModulationSchedule (const modulation::Schedule& s) noexcept { matrix.setSchedule (s); }
    void setModulationUpdateMode (typename Matrix::UpdateMode m) noexcept { matrix.setUpdateMode (m); }

    // --- Allpass phase modulation (PHASE 5B) ------------------------------

    /*
        Anything that changes how much delay the allpass chain adds to a loop
        also changes the gain that loop needs, so each of these re-applies the
        decay gains. Depth is included because engaging or disengaging a line
        moves its section in or out of the path; the coefficient itself does not
        matter, since an allpass contributes its pole count to the mean group
        delay whatever the coefficient is.
    */

    void setAllpassDepth (double zeroToOne, bool immediate = false) noexcept
    {
        allpass.setDepth (zeroToOne, immediate);
        applyGains (immediate);
    }

    void setAllpassRateHz (double hz) noexcept              { allpass.setRateHz (hz); }
    void setAllpassShape (LfoShape shape) noexcept          { allpass.setShape (shape); }

    void setAllpassMode (TimeVaryingAllpass::Mode m) noexcept
    {
        allpass.setMode (m);
        applyGains (false);
    }

    void setAllpassActiveLines (int count) noexcept
    {
        allpass.setActiveLines (count);
        applyGains (false);
    }

    void setAllpassSectionsPerLine (int count) noexcept
    {
        allpass.setSectionsPerLine (count);
        applyGains (false);
    }

    /**
        Turns the loop-delay correction off, restoring the PHASE 5B gain model.

        A research control, not a user setting: the measurements need to compare
        corrected against uncorrected on the same tank.
    */
    void setLoopDelayCorrectionEnabled (bool enabled) noexcept
    {
        if (enabled == loopDelayCorrection)
            return;

        loopDelayCorrection = enabled;
        applyGains (false);
    }

    [[nodiscard]] bool isLoopDelayCorrectionEnabled() const noexcept { return loopDelayCorrection; }

    /** Effective loop delay of one line, in samples, including the allpass. */
    [[nodiscard]] double getEffectiveLoopDelaySamples (int i) const noexcept
    {
        const double base = static_cast<double> (getDelaySamples (i));

        return loopDelayCorrection ? base + allpass.getAddedLoopDelaySamples (i) : base;
    }

    [[nodiscard]] const AllpassPhaseBank<N>& getAllpassBank() const noexcept { return allpass; }
    [[nodiscard]] AllpassPhaseBank<N>&       getAllpassBank()       noexcept { return allpass; }

    /** Restarts the modulation LFOs without touching the tank's memory. */
    void resetModulation() noexcept
    {
        matrix.reset();
        allpass.reset();
    }

    /** Clears every delay line and snaps the gain smoothers. Real-time safe.

        The modulation state is deliberately NOT reset here: switching the
        engine on or off, or changing its settings, must not clear the tank, and
        a reverb that restarted its modulation phase every time the transport
        moved would be neither deterministic nor pleasant. resetModulation() is
        separate for the cases that genuinely want it. */
    void reset() noexcept
    {
        for (auto& line : lines)
            line.reset();

        for (auto& g : gains)
            g.snapToTarget();

        for (auto& guard : guards)
            guard.reset();

        for (auto& d : dampers)
            d.reset();

        // Free-running, and restarted only here. Nothing in processSample may
        // key it to a block boundary: the same automation must render
        // identically at any block size.
        dampingCounter = 0;
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /**
        Advances the network one sample.

        `injection` is the per-line input, already scaled by whatever injection
        vector the caller uses. `stateOut` receives the per-line delay outputs
        after the decay gain — the vector a readout matrix multiplies.
    */
    void processSample (const double* injection, double* stateOut) noexcept
    {
        double v[N];

        // One advance per sample for the whole bank, before any line is
        // processed, so every section sees the same modulation instant.
        allpass.advance();

        // Damping coefficients move on the same free-running control grid the
        // matrix modulation uses, counted on a sample index rather than on
        // block boundaries. Skipped entirely once the corners have settled, so
        // a static setting costs one comparison.
        if (dampingCounter == 0)
        {
            const bool moving = lowCornerSm.isSmoothing() || highCornerSm.isSmoothing();

            if (moving)
            {
                lowCornerSm .skip (modulation::controlRatePeriod);
                highCornerSm.skip (modulation::controlRatePeriod);

                applyGains (false);
            }
        }

        if (++dampingCounter >= modulation::controlRatePeriod)
            dampingCounter = 0;

        for (int i = 0; i < N; ++i)
        {
            const double delayed = hooks.processLoop (lines[static_cast<size_t> (i)].read(), i);

            // The order the line state passes through:
            //   integer delay read
            //   -> time-varying allpass phase section   (PHASE 5B)
            //   -> damping slot                          (PHASE 7, identity)
            //   -> per-line decay gain
            //   -> orthogonal feedback matrix
            //   -> delay write
            const double phased = allpass.process (delayed, i);
            const double damped = processLineState (phased, i);

            v[i] = damped * gains[static_cast<size_t> (i)].getNextValue();
            stateOut[i] = v[i];
        }

        if (hooks.customMatrix)
            hooks.applyMatrix (v);

        matrix.process (v);

        for (int i = 0; i < N; ++i)
        {
            hooks.written[i] = v[i] + injection[i];
            lines[static_cast<size_t> (i)].write (hooks.written[i]
                                                  + guards[static_cast<size_t> (i)].next());
        }
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] int    getDelaySamples (int i) const noexcept { return lines[static_cast<size_t> (i)].getDelaySamples(); }
    [[nodiscard]] double getDelaySeconds (int i) const noexcept { return getDelaySamples (i) / currentSampleRate; }
    [[nodiscard]] double getGain (int i)         const noexcept { return gains[static_cast<size_t> (i)].getTargetValue(); }
    [[nodiscard]] double getCurrentGain (int i)  const noexcept { return gains[static_cast<size_t> (i)].getCurrentValue(); }
    [[nodiscard]] double getRt60Seconds()        const noexcept { return rt60Seconds; }
    [[nodiscard]] double getSampleRate()         const noexcept { return currentSampleRate; }
    [[nodiscard]] int    getCapacitySamples()    const noexcept { return capacitySamples; }
    [[nodiscard]] bool   isPrepared()            const noexcept { return prepared; }

    /** The largest per-line gain. Stability requires this to stay below 1. */
    [[nodiscard]] double getMaxGain() const noexcept
    {
        double worst = 0.0;

        for (int i = 0; i < N; ++i)
            worst = std::max (worst, std::abs (gains[static_cast<size_t> (i)].getTargetValue()));

        return worst;
    }

    [[nodiscard]] const Matrix& getMatrix() const noexcept { return matrix; }

    /** RESEARCH. The structural seams. Default-constructed, every one is inert. */
    ResearchHooks<N> hooks;

    /** RESEARCH. Re-derives the gains after a hook changed the loop delay. */
    void refreshGains() noexcept { applyGains (true); }

private:
    /** PHASE 7 per-line damping. Non-static since PHASE 7: the seam was shaped
        for a stateless transform, and a shelf pair has memory. */
    [[nodiscard]] double processLineState (double x, int i) noexcept
    {
        return dampers[static_cast<size_t> (i)].process (x);
    }

    /**
        Loop delay one line actually experiences, in seconds.

        The same quantity the decay gain has always been computed from, lifted
        out because the damping shelves need it too and the two must never
        disagree about what a line's delay is.
    */
    [[nodiscard]] double effectiveDelaySeconds (int i) const noexcept
    {
        const auto idx = static_cast<size_t> (i);

        if (! prepared)
            return requestedMs[idx] * requestedScale * 0.001;

        double samples = static_cast<double> (lines[idx].getDelaySamples());

        if (loopDelayCorrection)
            samples += allpass.getAddedLoopDelaySamples (i);

        samples += hooks.extraLoopDelay[idx];

        return samples / currentSampleRate;
    }

    /**
        Re-derives the per-line shelf pair.

        Called only from applyGains(), because the shelves depend on exactly
        what the gains depend on — the loop delays and RT60 — and the two are
        never allowed to be computed from different state.
    */
    void updateDamping (bool immediate) noexcept
    {
        dampingActive = (dampingLowRatio < 1.0) || (dampingHighRatio < 1.0);

        const double fLow  = lowCornerSm .getCurrentValue();
        const double fHigh = highCornerSm.getCurrentValue();

        // Identical on every line: only the shelf gains are per line.
        const double aLow  = damping::onePoleCoefficient (fLow,  currentSampleRate);
        const double aHigh = damping::onePoleCoefficient (fHigh, currentSampleRate);

        for (int i = 0; i < N; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const double d = effectiveDelaySeconds (i);

            auto& spec = dampingSpec[idx];

            spec.coeffLow  = aLow;
            spec.coeffHigh = aHigh;
            spec.gainLow   = dampingActive
                             ? damping::shelfGainFor (d, rt60Seconds, dampingLowRatio)
                             : 1.0;
            spec.gainHigh  = dampingActive
                             ? damping::shelfGainFor (d, rt60Seconds, dampingHighRatio)
                             : 1.0;

            // Every line is normalised by its OWN maximum, because every line
            // has its own shelf depths. Normalising them all by one figure
            // would put the eight lines back on eight different decay rates.
            dampingNorm[idx] = damping::normalisationFor (spec);

            dampers[idx].setShelves (spec, dampingNorm[idx], immediate);
        }
    }

    AllpassPhaseBank<N> allpass;

    /*
        The decay gain is set from the loop delay a signal actually experiences,
        not from the delay line alone.

        A PHASE 5B allpass section sits inside the feedback path, so the loop is
        longer than its delay line by the section's mean group delay. Leaving the
        gain at 10^(-3 d_i / RT60) therefore overshoots the target: measured
        RT60 came out about 1.7 % long. Using d_i + added_i closes it. This is
        not an energy correction — nothing was gaining energy — it is the same
        formula applied to the right delay.
    */
    /*
        The decay gain is exactly what it was before PHASE 7.

        Damping normalises itself — each line's response is scaled so it peaks
        at unity — so the gain never has to compensate for anything, the
        feedback clamp is never reached for a damping reason, and g_i keeps
        meaning "the per-pass gain of the longest-decaying frequency in this
        line". That is what makes Decay the longest RT60 in the tank rather
        than the RT60 of one anchor frequency.
    */
    void applyGains (bool immediate) noexcept
    {
        // The shelves are derived from the same delays and the same RT60 as the
        // gains, so they are re-derived here rather than anywhere else.
        updateDamping (immediate);

        for (int i = 0; i < N; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const double gain = std::min (fdn::maxFeedbackGain,
                                          fdn::feedbackGainForRt60 (effectiveDelaySeconds (i),
                                                                    rt60Seconds));

            if (immediate) gains[idx].snapTo (gain);
            else           gains[idx].setTarget (gain);
        }
    }

    std::array<FdnDelayLine, static_cast<size_t> (N)> lines;
    std::array<LinearSmoother<double>, static_cast<size_t> (N)> gains;
    std::array<AlternatingGuard, static_cast<size_t> (N)> guards;

    Matrix matrix;

    std::array<double, static_cast<size_t> (N)> requestedMs {};

    // --- PHASE 7 damping ---------------------------------------------------

    std::array<LineDamping, static_cast<size_t> (N)>        dampers;
    std::array<damping::ShelfSpec, static_cast<size_t> (N)> dampingSpec {};
    std::array<double, static_cast<size_t> (N)>             dampingNorm {};


    LogSmoother<double> lowCornerSm;
    LogSmoother<double> highCornerSm;

    double requestedLowCornerHz  = 200.0;
    double requestedHighCornerHz = 6000.0;

    double dampingLowRatio  = 1.0;
    double dampingHighRatio = 1.0;

    bool dampingActive  = false;
    int  dampingCounter = 0;

    bool   loopDelayCorrection = true;
    double requestedScale    = 1.0;
    double rt60Seconds       = 2.0;
    double currentSampleRate = 48000.0;
    int    capacitySamples   = 1;
    bool   prepared          = false;
};

} // namespace avtest::stage3
