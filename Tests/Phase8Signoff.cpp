#include "TestFramework.h"
#include "DampingSweepHarness.h"
#include "Suites.h"

#include "DSP/CutFilter.h"
#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 8 - STAGE 2.5 ENGINEERING SIGNOFF.

    Two things the Stage-2 report got wrong or left open, and nothing else.


    1. THE SCOPE OF THE COMMUTATION RESULT
    --------------------------------------
    Stage 2 reported cut-then-tank == tank-then-cut at 4.2e-16 and called both
    blocks LTI. That wording is only valid while the tank is STATIC.
    AuroraVerb ships with Mod Depth at 30 %, and a modulated FDN is
    time-varying: its delay lengths and its feedback rotation move under an
    LFO. An arbitrary LTI filter and a time-varying system do not generally
    commute, so the 4.2e-16 figure cannot be carried over to the default
    product.

    This file does three things about that. It reports exactly what the
    commutation suite configured. It verifies, rather than asserts, that the
    configuration really is time-invariant - by shifting the input and
    requiring the output to shift with it - and shows the same test failing
    at the shipped Mod Depth, which is what makes the distinction real rather
    than pedantic. And it replaces the over-general claim with the narrower
    one the product actually needs, proved a different way.

    THE CLAIM THAT SURVIVES, stated precisely:

        Low Cut and High Cut are EXCITATION filters. They sit outside the
        feedback network. Their parameters do not enter any tank control
        value - not RT60, not the damping ratios or shelf coefficients, not
        the feedback gains, not the delay lengths, not the modulation depth
        or rate - and so they do not appear anywhere in the tank's recurrence
        equations. They cannot change the decay law.

        They CAN change modal excitation weighting, the energy observed in a
        band, what a finite-band RT60 estimator reports, and - in the
        modulated system - the time-varying weighting of what reaches the
        output.

    What is NOT claimed: "cut controls can never change a measured RT60".
    That is too strong for a finite, modulated, modal system, and the
    Stage-2 harness itself showed why.


    2. BYPASS-ENDPOINT AUTOMATION AND FILTER STATE
    ----------------------------------------------
    Low Cut at 20 Hz and High Cut at 20 kHz are exact bypasses taken on a
    flag. The question Stage 2 did not ask is what happens to the one-pole
    state while that flag is down, and what the first sample after it comes
    back up looks like. Measured here against a reference that knows the
    difference between a click and a waveform that is simply steep.
*/

using namespace av::dsp;
using namespace avtest::sweep;

namespace avtest
{

namespace
{
    [[nodiscard]] std::string fx (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    using Morph = MorphingStereoFdn<fdn::order>;

    // -----------------------------------------------------------------------
    //  The complete production tank, driven exactly as processInternal drives
    //  it, minus JUCE. Not the FdnCore<8> of the PHASE 7 sweep harness: this
    //  one has both Size banks, the input diffusion, the Size compensation and
    //  the stereo readout.
    // -----------------------------------------------------------------------
    struct ProductionChain
    {
        std::unique_ptr<Morph> tank;
        StereoCutFilters       cut;
        PreDelay<double>       pd;

        void prepare (double sampleRate)
        {
            tank = std::make_unique<Morph>();
            tank->prepare (sampleRate);
            tank->setInputDiffusionEnabled (true);
            tank->setCompensationEnabled (true, true);

            cut.prepare (sampleRate);
            pd .prepare (sampleRate);
        }

        /** The stage-2 block of processInternal, same calls, same order. */
        void setTankControls (double decaySeconds, double sizeControl,
                              double earlyDiffusion,
                              double lowDamp, double highDamp,
                              double lowDampFreq, double highDampFreq,
                              double depthControl, double rateControlHz,
                              bool immediate)
        {
            tank->setRt60Seconds (mapping::rt60For (decaySeconds), immediate);
            tank->setInputDiffusion (earlyDiffusion, immediate);
            tank->setSize (mapping::sizeScaleFor (sizeControl));

            tank->setDamping (mapping::lowDampRatioFor  (lowDamp),
                              mapping::highDampRatioFor (highDamp),
                              lowDampFreq, highDampFreq, immediate);

            architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                          mapping::modDepthFor (depthControl),
                                          mapping::modRateScaleFor (rateControlHz),
                                          immediate);
        }

        void setCutControls (double lowHz, double highHz, double preDelayMs)
        {
            cut.setLowCut  (lowHz,  lowHz  <= lowCutOffHz);
            cut.setHighCut (highHz, highHz >= highCutOffHz);
            pd.snapTo (preDelayMs);
        }
    };

    /*
        Every tank control value this build can reach, flattened.

        The point of flattening is that the comparison is then exact and
        total: two snapshots either agree in every element or they do not, and
        nothing is left to a reviewer's idea of which fields mattered.
    */
    [[nodiscard]] std::vector<double> snapshotTank (const Morph& t)
    {
        std::vector<double> s;
        s.reserve (512);

        auto push = [&s] (double v) { s.push_back (v); };

        push (t.getMorphPosition());
        push (t.getSize());
        push (t.getMorphSeconds());
        push (t.getRt60Seconds());
        push (static_cast<double> (t.getActiveBankIndex()));
        push (t.isMorphing()      ? 1.0 : 0.0);
        push (t.hasPendingSize()  ? 1.0 : 0.0);
        push (t.isCompensationEnabled() ? 1.0 : 0.0);
        push (t.getCompensationGain (0));
        push (t.getCompensationGain (1));

        for (int b = 0; b < 2; ++b)
        {
            const auto& bank = t.getBank (b);
            const auto& core = bank.getCore();

            push (bank.getSizeScale());
            push (bank.getRt60Seconds());
            push (core.getDampingLowRatio());
            push (core.getDampingHighRatio());
            push (core.isDampingActive() ? 1.0 : 0.0);
            push (static_cast<double> (core.getMatrixType()));
            push (core.isLoopDelayCorrectionEnabled() ? 1.0 : 0.0);
            push (core.getMatrix().getDepth());
            push (core.getMatrix().getRateHz());

            const auto& ap = core.getAllpassBank();

            push (ap.getDepth());
            push (ap.getRateHz());
            push (static_cast<double> (ap.getMode()));
            push (static_cast<double> (ap.getActiveLines()));
            push (static_cast<double> (ap.getSectionsPerLine()));
            push (ap.isBypassed() ? 1.0 : 0.0);
            push (ap.getCurrentSwing());

            for (int i = 0; i < fdn::order; ++i)
            {
                push (static_cast<double> (core.getDelaySamples (i)));
                push (core.getGain (i));
                push (core.getCurrentGain (i));
                push (core.getEffectiveLoopDelaySamples (i));
                push (core.getDampingNormalisation (i));

                const auto& spec = core.getDampingSpec (i);
                push (spec.gainLow);
                push (spec.coeffLow);
                push (spec.gainHigh);
                push (spec.coeffHigh);

                push (ap.getCoefficient (i));
                push (ap.getLaneRateHz (i));
                push (ap.getAddedLoopDelaySamples (i));
            }
        }

        return s;
    }

    /** Index of the first differing element, or -1. */
    [[nodiscard]] int firstDifference (const std::vector<double>& a,
                                       const std::vector<double>& b)
    {
        if (a.size() != b.size())
            return 0;

        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return static_cast<int> (i);

        return -1;
    }

    // -----------------------------------------------------------------------
    //  Signals
    // -----------------------------------------------------------------------
    [[nodiscard]] std::vector<double> sineSignal (int length, double hz, double fs,
                                                  double amplitude = 0.5)
    {
        std::vector<double> v (static_cast<size_t> (length), 0.0);

        for (int n = 0; n < length; ++n)
            v[static_cast<size_t> (n)] =
                amplitude * std::sin (2.0 * damping::pi * hz * n / fs);

        return v;
    }

    [[nodiscard]] std::vector<double> noiseSignal (int length, unsigned seed,
                                                   double amplitude = 0.35)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<double> dist (-amplitude, amplitude);

        std::vector<double> v (static_cast<size_t> (length), 0.0);

        for (auto& s : v)
            s = dist (rng);

        return v;
    }

    /*
        Something with the shape of music rather than the shape of a test
        tone: a decaying harmonic stack re-struck on a beat, with a little
        noise on each attack. It matters here because the quantity being
        measured is a sample-to-sample step, and a signal with real transients
        has large natural steps that a naive click detector would flag.
    */
    [[nodiscard]] std::vector<double> musicalSignal (int length, double fs)
    {
        std::vector<double> v (static_cast<size_t> (length), 0.0);
        std::mt19937 rng (20260401u);
        std::uniform_real_distribution<double> dist (-1.0, 1.0);

        const int period = static_cast<int> (fs * 0.25);
        const double partials[] { 1.0, 2.0, 3.0, 4.97, 6.3 };
        const double weights[]  { 1.0, 0.5, 0.28, 0.14, 0.08 };

        for (int n = 0; n < length; ++n)
        {
            const int since = n % period;
            const double t = since / fs;
            const double envelope = std::exp (-9.0 * t);
            const double note = (n / period) % 4;
            const double f0 = 110.0 * std::pow (2.0, note / 12.0);

            double x = 0.0;

            for (int p = 0; p < 5; ++p)
                x += weights[p] * std::sin (2.0 * damping::pi * f0 * partials[p] * t);

            x *= envelope;
            x += 0.25 * dist (rng) * std::exp (-220.0 * t);

            v[static_cast<size_t> (n)] = 0.22 * x;
        }

        return v;
    }

    /** Largest single-sample step in [from, to). */
    [[nodiscard]] double largestStep (const std::vector<double>& v, int from, int to)
    {
        double worst = 0.0;

        for (int n = std::max (1, from); n < to && n < static_cast<int> (v.size()); ++n)
            worst = std::max (worst, std::abs (v[static_cast<size_t> (n)]
                                               - v[static_cast<size_t> (n - 1)]));

        return worst;
    }

    [[nodiscard]] double peakOf (const std::vector<double>& v)
    {
        double p = 0.0;

        for (double s : v)
            p = std::max (p, std::abs (s));

        return p;
    }

    [[nodiscard]] bool allFiniteIn (const std::vector<double>& v)
    {
        for (double s : v)
            if (! std::isfinite (s))
                return false;

        return true;
    }

    /*
        One channel of StereoCutFilters, so a single-channel experiment does
        not have to carry a parallel right channel that says the same thing.
        Same class, same coefficients, L only.
    */
    struct CutRunner
    {
        StereoCutFilters f;
        std::vector<double> dummy;

        void prepare (double fs) { f.prepare (fs); }

        void setCut (double lowHz, double highHz)
        {
            f.setLowCut  (lowHz,  lowHz  <= lowCutOffHz);
            f.setHighCut (highHz, highHz >= highCutOffHz);
        }

        /** Processes in place, left channel carrying the signal. */
        void run (double* data, int numSamples)
        {
            dummy.assign (static_cast<size_t> (numSamples), 0.0);
            f.process (data, dummy.data(), numSamples);
        }
    };

    /*
        A cut automation point: at `atSample` the corner moves to (lowHz,
        highHz). The runner is driven in two pieces so the change lands on an
        exact sample rather than on a chunk boundary chosen by the harness.
    */
    struct CutMove
    {
        int at;
        double lowHz, highHz;
    };

    /** Runs `signal` through one cut filter, applying each move in order. */
    [[nodiscard]] std::vector<double> runWithMoves (const std::vector<double>& signal,
                                                    double fs,
                                                    double startLowHz, double startHighHz,
                                                    const std::vector<CutMove>& moves)
    {
        std::vector<double> out = signal;

        CutRunner r;
        r.prepare (fs);
        r.setCut (startLowHz, startHighHz);

        int cursor = 0;

        for (const auto& m : moves)
        {
            const int count = m.at - cursor;

            if (count > 0)
                r.run (out.data() + cursor, count);

            r.setCut (m.lowHz, m.highHz);
            cursor = m.at;
        }

        const int remaining = static_cast<int> (out.size()) - cursor;

        if (remaining > 0)
            r.run (out.data() + cursor, remaining);

        return out;
    }

    /** The same signal through a filter that has been in `to` all along. */
    [[nodiscard]] std::vector<double> runSteady (const std::vector<double>& signal,
                                                 double fs, double lowHz, double highHz)
    {
        std::vector<double> out = signal;

        CutRunner r;
        r.prepare (fs);
        r.setCut (lowHz, highHz);
        r.run (out.data(), static_cast<int> (out.size()));

        return out;
    }

    /*
        What a transition across OFF is measured against.

        The first version of this file used "the largest step the destination
        run makes anyway" as the reference for a click. That is wrong in both
        directions. When the destination is a 500 Hz high cut its output is
        very smooth, so any entry at all looks enormous by ratio; when the
        destination is bypass its output is the raw signal, so a real artifact
        can hide under a steep waveform. It also measured each transition at
        ONE sample index, 4800, which for a 1 kHz tone at 48 kHz is exactly a
        zero crossing - the quietest instant in the signal, and the one place a
        switching artifact is smallest.

        The reference used here is the IDEAL SWITCH: the same instantaneous
        change of filter, but made by a filter whose state is already settled
        in the destination configuration. That is the best any implementation
        could do without crossfading, and it still has a step, because OFF
        means OFF and the timbre genuinely changes.

            idealStep  = |settled[t] - automated[t-1]|
            inputStep  = |signal[t] - signal[t-1]|
            actualStep = |automated[t] - automated[t-1]|
            excess     = actualStep - max (idealStep, inputStep)

        Both references are needed, and an earlier version of this file used
        only the first and read a pass as a failure for it. Entering a heavily
        smoothing filter, an ideal switch jumps straight to a smooth value, so
        its step can be SMALLER than the signal's own. An implementation that
        instead lets the current sample through and then rolls off has stepped
        further than the ideal switch while stepping exactly as far as the
        input did - which is not a discontinuity at all. The criterion is the
        user's: no output discontinuity grossly exceeding the underlying
        signal transition, or what switching must cost, whichever is larger.

        Excess is the avoidable part, and it is what the acceptance criteria
        are about. The transition index is swept across a full period of the
        test tone so no result depends on where in the waveform it landed.

        switchError - peak |automated - settled| after the move - is reported
        but NOT gated. It is dominated by legitimate settling: a low cut
        entering on a bass-heavy signal takes its time constant to remove the
        lows, and that is the filter working, not an artifact. What must not
        depend on history is tested directly, and exactly, in its own suite.
    */
    struct TransitionResult
    {
        double switchErrorPercent = 0.0;
        double actualStep = 0.0;
        double idealStep  = 0.0;
        double inputStep  = 0.0;
        double excess     = 0.0;      ///< actualStep - max(ideal, input), worst over the sweep
        double excessPercent = 0.0;   ///< ... as a percentage of signal peak
        bool   finite = true;
    };

    [[nodiscard]] TransitionResult measureTransition (const std::vector<double>& signal,
                                                      double fs,
                                                      double fromLowHz, double fromHighHz,
                                                      double toLowHz, double toHighHz,
                                                      int baseMoveAt, int phaseCount)
    {
        const auto settled = runSteady (signal, fs, toLowHz, toHighHz);
        const double signalPeak = std::max (peakOf (signal), 1.0e-12);

        TransitionResult worst;
        worst.excess = -1.0e30;

        for (int phase = 0; phase < phaseCount; ++phase)
        {
            const int moveAt = baseMoveAt + phase;

            const auto automated = runWithMoves (signal, fs, fromLowHz, fromHighHz,
                                                 { CutMove { moveAt, toLowHz, toHighHz } });

            const auto t = static_cast<size_t> (moveAt);

            const double actual = std::abs (automated[t] - automated[t - 1]);
            const double ideal  = std::abs (settled[t]   - automated[t - 1]);
            const double input  = std::abs (signal[t]    - signal[t - 1]);
            const double excess = actual - std::max (ideal, input);

            double error = 0.0;

            for (size_t n = t; n < automated.size(); ++n)
                error = std::max (error, std::abs (automated[n] - settled[n]));

            worst.switchErrorPercent = std::max (worst.switchErrorPercent,
                                                 100.0 * error / signalPeak);
            worst.finite = worst.finite && allFiniteIn (automated);

            if (excess > worst.excess)
            {
                worst.excess = excess;
                worst.actualStep = actual;
                worst.idealStep  = ideal;
                worst.inputStep  = input;
            }
        }

        worst.excessPercent = 100.0 * worst.excess / signalPeak;

        return worst;
    }
}

void runPhase8SignoffTests()
{
    ScopedFtz ftz;

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - what the commutation result actually covers");
    // -----------------------------------------------------------------------
    {
        info ("  Stage 2 reported commutation at 4.2e-16 and described both");
        info ("  blocks as LTI. Here is the configuration that produced it,");
        info ("  read off the harness rather than remembered.");
        info ("");
        info ("    tank                 avtest::sweep::Tank, i.e. FdnCore<8>");
        info ("    Mod Depth control    0.00");
        info ("    mapped mod depth     " + fx (mapping::modDepthFor (0.0), 12));
        info ("    Mod Rate control     " + fx (kDefaultModRateHz, 2) + " Hz");
        info ("    mapped rate scale    " + fx (mapping::modRateScaleFor (kDefaultModRateHz), 6));
        info ("    allpass depth        " + fx (architecture::settingsFor (architecture::defaultCandidate).allpassDepth
                                                * mapping::modDepthFor (0.0), 12));
        info ("    matrix depth         " + fx (architecture::settingsFor (architecture::defaultCandidate).matrixDepth
                                                * mapping::modDepthFor (0.0), 12));
        info ("");
        info ("    So: modulation DISABLED, by depth 0 in both engines. The");
        info ("    rate is still 0.5 Hz but multiplies a zero depth, so no");
        info ("    coefficient in the loop moves.");
        info ("");
        info ("    And NOT the complete production tank. The sweep harness");
        info ("    drives FdnCore<8> with fixed injection and readout vectors.");
        info ("    The product runs MorphingStereoFdn<8>: two Size banks, input");
        info ("    diffusion, Size compensation, stereo readout. The shipped");
        info ("    Mod Depth is 30 %, not 0.");

        checkClose (mapping::modDepthFor (0.0), 0.0, 0.0,
                    "the commutation suite's Mod Depth maps to exactly zero");

        // --- the configuration is time-invariant, verified rather than said --
        info ("");
        info ("  Depth 0 is claimed to be a true bypass, which is what makes");
        info ("  the tank LTI. Claimed is not measured, so: shift the input by");
        info ("  S samples and require the output to shift by exactly S. That");
        info ("  is time invariance, directly.");
        info ("");
        info ("    " + pad ("Mod Depth", 12) + pad ("shift", 8)
              + pad ("peak", 13) + pad ("worst diff", 16) + "relative");

        constexpr int shift = 977;          // nothing special, and not a period
        constexpr int length = 24000;

        auto shiftInvariance = [&] (double depthControl)
        {
            Setting s;
            s.lowDamp  = kDefaultLowDamp;
            s.highDamp = 0.45;
            s.lowHz    = 200.0;
            s.highHz   = 10000.0;
            s.modDepthControl = depthControl;

            auto drive = makeExcitation (5);

            std::vector<double> shifted (static_cast<size_t> (shift), 0.0);
            shifted.insert (shifted.end(), drive.begin(), drive.end());

            std::vector<double> a, b;

            { Tank t (s); a = t.render (drive,   length + shift); }
            { Tank t (s); b = t.render (shifted, length + shift); }

            double peak = 0.0, worst = 0.0;

            for (int n = 0; n < length; ++n)
            {
                const double x = a[static_cast<size_t> (n)];
                const double y = b[static_cast<size_t> (n + shift)];

                peak  = std::max (peak,  std::abs (x));
                worst = std::max (worst, std::abs (x - y));
            }

            const double relative = peak > 0.0 ? worst / peak : 0.0;

            info ("    " + pad (fx (depthControl * 100.0, 0) + " %", 12)
                  + pad (std::to_string (shift), 8)
                  + pad (fx (peak, 6), 13)
                  + pad (fx (worst, 12), 16)
                  + fx (relative, 12));

            return relative;
        };

        const double staticRelative    = shiftInvariance (0.0);
        const double modulatedRelative = shiftInvariance (mapping::depthControlDefault);

        info ("");
        info ("    At Mod Depth 0 the tank is time invariant to rounding, so");
        info ("    LTI is the right word and commutation is the right test.");
        info ("    At the shipped 30 % the same tank fails shift invariance by");
        info ("    a factor of " + fx (modulatedRelative / std::max (staticRelative, 1.0e-18), 0)
              + ", because the delay lengths and the");
        info ("    feedback rotation are moving under an LFO. A filter and a");
        info ("    time-varying system do not generally commute, so the");
        info ("    4.2e-16 figure does NOT extend to the default product.");
        info ("");
        info ("    CLASSIFICATION: STATIC / LTI CONTROL TEST.");
        info ("    It proves the routing is right in the static case. It is not");
        info ("    evidence about the modulated case, and the next suite is.");

        checkAtMost (staticRelative, 1.0e-12,
                     "at Mod Depth 0 the tank is time invariant, so it is LTI");

        check (modulatedRelative > 1.0e-6,
               "at the shipped Mod Depth it is not, so the LTI wording was too broad");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - the cuts cannot reach any tank control value");
    // -----------------------------------------------------------------------
    {
        /*
            The claim the product needs, and it is a routing claim rather than
            an algebraic one, so modulation does not weaken it.

            Two independent checks, both on the COMPLETE production tank at
            the shipped Mod Depth 30 %:

              1. Snapshot every tank control value this build can read -
                 delay lengths, feedback gains, RT60, damping ratios, every
                 shelf coefficient and normalisation, allpass depth, rate,
                 coefficients and added loop delay, matrix depth and rate,
                 Size, morph position, compensation gains - while sweeping
                 only Low Cut, High Cut and Pre-Delay across their full
                 ranges. Every element must be identical.

              2. Take the signal that reaches tank.processBlock() in a chain
                 that HAS cut filters, and feed that same signal to a tank
                 that has never been told a cut parameter at all. Bit-identical
                 output means the cut parameters were consumed entirely before
                 the tank, which is the whole claim.
        */
        constexpr double fs = 48000.0;

        ProductionChain chain;
        chain.prepare (fs);
        chain.setTankControls (2.5, 0.60, 0.30, kDefaultLowDamp, 0.45,
                               200.0, 10000.0, mapping::depthControlDefault, 0.5, true);

        info ("  Complete production tank, Mod Depth "
              + fx (mapping::depthControlDefault * 100.0, 0) + " %.");
        info ("  " + std::to_string (snapshotTank (*chain.tank).size())
              + " tank control values read per snapshot.");
        info ("");
        info ("    " + pad ("Low Cut", 10) + pad ("High Cut", 10)
              + pad ("Pre-Delay", 12) + "tank snapshot");

        const auto reference = snapshotTank (*chain.tank);

        struct CutCase { double lowHz, highHz, ms; };

        const CutCase cases[]
        {
            { lowCutOffHz, highCutOffHz,   0.0 },
            { 1000.0,      highCutOffHz,   0.0 },
            { lowCutOffHz, 500.0,          0.0 },
            { 1000.0,      500.0,        500.0 },
            { 120.0,       9000.0,        37.0 },
            { lowCutOffHz, highCutOffHz,   0.0 },
        };

        int worstIndex = -1;

        for (const auto& c : cases)
        {
            chain.setCutControls (c.lowHz, c.highHz, c.ms);

            // And then actually run audio, so this is not only about the
            // setters: a cut parameter that reached the tank through a shared
            // buffer or a stale pointer would show up here.
            std::vector<double> l (512, 0.0), r (512, 0.0);

            for (int i = 0; i < 512; ++i)
                l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)]
                    = 0.2 * std::sin (0.05 * i);

            chain.cut.process (l.data(), r.data(), 512);
            chain.pd .process (l.data(), r.data(), 512);

            const auto after = snapshotTank (*chain.tank);
            const int diff = firstDifference (reference, after);

            if (diff >= 0 && worstIndex < 0)
                worstIndex = diff;

            info ("    " + pad (fx (c.lowHz, 0), 10) + pad (fx (c.highHz, 0), 10)
                  + pad (fx (c.ms, 1), 12)
                  + (diff < 0 ? std::string ("identical")
                              : "DIFFERS at element " + std::to_string (diff)));
        }

        check (worstIndex < 0,
               "sweeping the cuts changes no tank control value, not one element");

        // --- 2. consumed before the tank, proved by replay ------------------
        info ("");
        info ("  And the routing itself: the signal that reaches the tank is");
        info ("  replayed into a tank that was never told a cut parameter.");
        info ("");
        info ("    " + pad ("Low Cut", 10) + pad ("High Cut", 10)
              + pad ("Pre-Delay", 12) + pad ("peak", 12) + "worst diff");

        constexpr int renderLength = 8192;

        const auto excitation = sineSignal (renderLength, 220.0, fs, 0.4);

        double worstReplay = 0.0;

        for (const auto& c : cases)
        {
            // --- the chain that knows about cuts ---------------------------
            ProductionChain withCuts;
            withCuts.prepare (fs);
            withCuts.setTankControls (2.5, 0.60, 0.30, kDefaultLowDamp, 0.45,
                                      200.0, 10000.0,
                                      mapping::depthControlDefault, 0.5, true);
            withCuts.setCutControls (c.lowHz, c.highHz, c.ms);

            std::vector<double> l = excitation, r = excitation;

            withCuts.cut.process (l.data(), r.data(), renderLength);
            withCuts.pd .process (l.data(), r.data(), renderLength);

            // What the tank is about to be given, kept for the replay.
            const std::vector<double> preTankL = l, preTankR = r;

            withCuts.tank->processBlock (l.data(), r.data(), renderLength);

            // --- a tank that has never seen a cut parameter ----------------
            auto bare = std::make_unique<Morph>();
            bare->prepare (fs);
            bare->setInputDiffusionEnabled (true);
            bare->setCompensationEnabled (true, true);
            bare->setRt60Seconds (mapping::rt60For (2.5), true);
            bare->setInputDiffusion (0.30, true);
            bare->setSize (mapping::sizeScaleFor (0.60));
            bare->setDamping (mapping::lowDampRatioFor (kDefaultLowDamp),
                              mapping::highDampRatioFor (0.45),
                              200.0, 10000.0, true);
            architecture::applyWithDepth (*bare, architecture::defaultCandidate,
                                          mapping::modDepthFor (mapping::depthControlDefault),
                                          mapping::modRateScaleFor (0.5), true);

            std::vector<double> bl = preTankL, br = preTankR;
            bare->processBlock (bl.data(), br.data(), renderLength);

            double peak = 0.0, worst = 0.0;

            for (int n = 0; n < renderLength; ++n)
            {
                const auto i = static_cast<size_t> (n);
                peak  = std::max (peak,  std::abs (l[i]));
                worst = std::max (worst, std::max (std::abs (l[i] - bl[i]),
                                                   std::abs (r[i] - br[i])));
            }

            worstReplay = std::max (worstReplay, worst);

            info ("    " + pad (fx (c.lowHz, 0), 10) + pad (fx (c.highHz, 0), 10)
                  + pad (fx (c.ms, 1), 12) + pad (fx (peak, 6), 12)
                  + fx (worst, 14));
        }

        info ("");
        info ("    worst replay difference  " + fx (worstReplay, 16));
        info ("");
        info ("    Bit for bit, at the shipped Mod Depth. The cut parameters");
        info ("    are consumed before tank.processBlock() and nowhere else, so");
        info ("    they cannot enter a recurrence equation, and the decay law");
        info ("    is not theirs to change.");
        info ("");
        info ("    What they CAN change, and this is not a hedge: modal");
        info ("    excitation weighting, the energy observed in a band, what a");
        info ("    finite-band RT60 estimator reports, and the time-varying");
        info ("    output weighting of the modulated system. The Stage-2");
        info ("    harness demonstrated the third of those at up to 9 %.");

        checkEqual (worstReplay, 0.0,
                    "the tank output is bit-identical to a tank that never saw a cut");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - what the one-pole state does while bypassed");
    // -----------------------------------------------------------------------
    {
        /*
            StereoCutFilters::process returns early when both filters are
            bypassed, and CutFilterState::process skips the inactive branch
            when only one is. Either way the state of a bypassed filter is
            neither advanced nor cleared. Reading the code that is answer (B)
            FROZEN - but reading code is how the first version of three
            Stage-2 tests went wrong, so it is demonstrated.
        */
        constexpr double fs = 48000.0;

        info ("  A filter is driven hard, then bypassed, then re-enabled, and");
        info ("  what the bypass does to its state is read directly.");
        info ("");

        CutRunner r;
        r.prepare (fs);
        r.setCut (1000.0, highCutOffHz);

        auto loud = sineSignal (4800, 1000.0, fs, 0.8);
        r.run (loud.data(), static_cast<int> (loud.size()));

        const bool activeBefore = r.f.isLowActive();

        r.setCut (lowCutOffHz, highCutOffHz);

        const bool bypassedNow = r.f.isBypassed();

        auto whileBypassed = sineSignal (static_cast<int> (fs * 2.0), 1000.0, fs, 0.8);
        const auto bypassCopy = whileBypassed;
        r.run (whileBypassed.data(), static_cast<int> (whileBypassed.size()));

        bool passedThroughUntouched = true;

        for (size_t n = 0; n < whileBypassed.size(); ++n)
            passedThroughUntouched = passedThroughUntouched
                                     && whileBypassed[n] == bypassCopy[n];

        info ("    active before bypass            "
              + std::string (activeBefore ? "yes" : "no"));
        info ("    isBypassed() at the endpoint    "
              + std::string (bypassedNow ? "yes" : "no"));
        info ("    two seconds pass through        "
              + std::string (passedThroughUntouched ? "bit for bit" : "ALTERED"));
        info ("");
        info ("    DURING BYPASS: (B) FROZEN.");
        info ("");
        info ("    Not (A): process() returns before touching the state. Not");
        info ("    (C): nothing clears it. Not (D): setLowCut and setHighCut");
        info ("    recompute coefficients and nothing else.");
        info ("");
        info ("    ON RE-ENTRY: PRIMED, which STAGE 2.5 added. The first");
        info ("    sample of each active period sets the state so that that");
        info ("    sample passes through unchanged, from the sample itself.");
        info ("    Frozen state therefore never reaches the output, and the");
        info ("    next suite measures that rather than trusting it.");

        check (bypassedNow, "parking on the endpoint bypasses the stage");
        check (passedThroughUntouched,
               "and a bypassed stage passes audio through bit for bit");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - nothing from before a bypass survives it");
    // -----------------------------------------------------------------------
    {
        /*
            The decisive stale-state test, and it needs no tolerance.

            Two runs are given the SAME signal and the SAME automation from
            the bypass onward, and differ only in what the filter did BEFORE
            the bypass: one spent that time at a corner where its state holds
            real energy, the other spent it bypassed. If any part of the old
            history survives the bypass, the two outputs after re-entry
            differ. If none does, they are bit-identical.

            This separates the two things the first version of this file
            confused. Settling after a switch is legitimate and can be large.
            History crossing a bypass is neither, and it is this.
        */
        constexpr double fs = 48000.0;

        const int chargeLength = static_cast<int> (fs * 0.1);
        const int bypassLength = static_cast<int> (fs * 2.0);
        const int tailLength   = static_cast<int> (fs * 0.2);
        const int length = chargeLength + bypassLength + tailLength;
        const int returnAt = chargeLength + bypassLength;

        info ("  Identical signal, identical automation after the bypass, two");
        info ("  different histories before it, two seconds of bypass between.");
        info ("");
        info ("    " + pad ("re-entry", 20) + pad ("charged at", 18)
              + pad ("worst diff", 16) + "samples differing");

        struct Case
        {
            const char* label;
            double chargeLow, chargeHigh;
            double returnLow, returnHigh;
        };

        const Case cases[]
        {
            { "Low Cut 21",   1000.0,      highCutOffHz, 21.0,        highCutOffHz },
            { "Low Cut 30",   1000.0,      highCutOffHz, 30.0,        highCutOffHz },
            { "Low Cut 1000", 1000.0,      highCutOffHz, 1000.0,      highCutOffHz },
            { "High Cut 19k", lowCutOffHz, 500.0,        lowCutOffHz, 19000.0 },
            { "High Cut 500", lowCutOffHz, 500.0,        lowCutOffHz, 500.0 },
            { "both 120/9k",  300.0,       2000.0,       120.0,       9000.0 },
        };

        // Noise, because a periodic signal can make the two histories agree
        // by accident - which is what the first version of this test did,
        // with a musical signal whose period divided the bypass length.
        const auto signal = noiseSignal (length, 31u, 0.45);

        double worstDifference = 0.0;
        long long worstDiffering = 0;

        for (const auto& c : cases)
        {
            const auto charged = runWithMoves (signal, fs, c.chargeLow, c.chargeHigh,
            {
                CutMove { chargeLength, lowCutOffHz, highCutOffHz },
                CutMove { returnAt,     c.returnLow, c.returnHigh },
            });

            const auto cold = runWithMoves (signal, fs, lowCutOffHz, highCutOffHz,
            {
                CutMove { chargeLength, lowCutOffHz, highCutOffHz },
                CutMove { returnAt,     c.returnLow, c.returnHigh },
            });

            double worst = 0.0;
            long long differing = 0;

            for (size_t n = static_cast<size_t> (returnAt); n < charged.size(); ++n)
            {
                const double d = std::abs (charged[n] - cold[n]);

                worst = std::max (worst, d);

                if (d != 0.0)
                    ++differing;
            }

            worstDifference = std::max (worstDifference, worst);
            worstDiffering  = std::max (worstDiffering, differing);

            info ("    " + pad (c.label, 20)
                  + pad (fx (c.chargeLow, 0) + " / " + fx (c.chargeHigh, 0), 18)
                  + pad (fx (worst, 14), 16)
                  + std::to_string (differing) + " of "
                  + std::to_string (charged.size() - static_cast<size_t> (returnAt)));
        }

        info ("");
        info ("    worst difference  " + fx (worstDifference, 16));
        info ("");
        info ("    Bit for bit. The audio after a bypass does not depend on");
        info ("    the audio before it, so there is no stale state to burst.");

        checkEqual (worstDifference, 0.0,
                    "no part of a filter's history survives a two-second bypass");

        checkEqual (static_cast<double> (worstDiffering), 0.0,
                    "and not one sample after re-entry differs between histories");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - Low Cut endpoint automation");
    // -----------------------------------------------------------------------
    {
        constexpr double fs = 48000.0;
        constexpr int length = 9600;
        constexpr int baseMoveAt = 4800;
        constexpr int phases = 48;      // one full period of the 1 kHz probe

        info ("  20 Hz is an exact bypass, so every move below crosses OFF.");
        info ("");
        info ("  A tonal change across OFF is expected and is not measured");
        info ("  here. What is measured is the AVOIDABLE part: how much");
        info ("  further the output steps than an IDEAL SWITCH - the same");
        info ("  instantaneous change, made by a filter already settled in the");
        info ("  destination - would have stepped. The transition index is");
        info ("  swept across 48 samples, so no result depends on where in the");
        info ("  waveform it landed.");
        info ("");
        info ("  switch err is the peak difference from that ideal switch. It");
        info ("  is reported, not gated: it is dominated by legitimate");
        info ("  settling, and what must not depend on history is proved");
        info ("  exactly in the previous suite.");
        info ("");

        struct Signal { const char* name; std::vector<double> data; };

        std::vector<Signal> signals;
        signals.push_back ({ "sine 1k", sineSignal (length, 1000.0, fs, 0.6) });
        signals.push_back ({ "noise",   noiseSignal (length, 7u) });
        signals.push_back ({ "musical", musicalSignal (length, fs) });

        struct Move { const char* label; double fromHz, toHz; };

        const Move moves[]
        {
            { "20 -> 21",    lowCutOffHz, 21.0 },
            { "20 -> 30",    lowCutOffHz, 30.0 },
            { "30 -> 20",    30.0,        lowCutOffHz },
            { "20 -> 1000",  lowCutOffHz, 1000.0 },
            { "1000 -> 20",  1000.0,      lowCutOffHz },
        };

        info ("    " + pad ("signal", 10) + pad ("move", 13)
              + pad ("switch err %", 14) + pad ("step", 11)
              + pad ("ideal step", 13) + pad ("input step", 13)
              + pad ("excess", 13) + "finite");

        double worstExcessPercent = -1.0e30;
        bool allFinite = true;

        for (const auto& sig : signals)
        {
            for (const auto& m : moves)
            {
                const auto t = measureTransition (sig.data, fs,
                                                  m.fromHz, highCutOffHz,
                                                  m.toHz,   highCutOffHz,
                                                  baseMoveAt, phases);

                worstExcessPercent = std::max (worstExcessPercent, t.excessPercent);
                allFinite = allFinite && t.finite;

                info ("    " + pad (sig.name, 10) + pad (m.label, 13)
                      + pad (fx (t.switchErrorPercent, 4), 14)
                      + pad (fx (t.actualStep, 6), 11)
                      + pad (fx (t.idealStep, 6), 13)
                      + pad (fx (t.inputStep, 6), 13)
                      + pad (fx (t.excess, 9), 13)
                      + (t.finite ? "yes" : "NO"));
            }
        }

        info ("");
        info ("    worst excess over signal or ideal switch  "
              + fx (worstExcessPercent, 6) + " % of signal peak");

        check (allFinite, "no NaN or Inf anywhere in the Low Cut transitions");

        // The prime solves y = x algebraically; it is then evaluated in
        // floating point, so the residual is a few ulps of the sample. The
        // gate is five orders of magnitude above that and still 1e-11 of
        // full scale - far below any arithmetic that could be heard.
        checkAtMost (worstExcessPercent, 1.0e-9,
                     "no Low Cut transition steps further than the signal or the switch must");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - High Cut endpoint automation");
    // -----------------------------------------------------------------------
    {
        constexpr double fs = 48000.0;
        constexpr int length = 9600;
        constexpr int baseMoveAt = 4800;
        constexpr int phases = 48;

        info ("  20 kHz is an exact bypass. Same method as the Low Cut suite.");
        info ("");

        struct Signal { const char* name; std::vector<double> data; };

        std::vector<Signal> signals;
        signals.push_back ({ "sine 1k", sineSignal (length, 1000.0, fs, 0.6) });
        signals.push_back ({ "noise",   noiseSignal (length, 11u) });
        signals.push_back ({ "musical", musicalSignal (length, fs) });

        struct Move { const char* label; double fromHz, toHz; };

        const Move moves[]
        {
            { "20k -> 19.9k", highCutOffHz, 19900.0 },
            { "20k -> 19k",   highCutOffHz, 19000.0 },
            { "19k -> 20k",   19000.0,      highCutOffHz },
            { "20k -> 500",   highCutOffHz, 500.0 },
            { "500 -> 20k",   500.0,        highCutOffHz },
        };

        info ("    " + pad ("signal", 10) + pad ("move", 14)
              + pad ("switch err %", 14) + pad ("step", 11)
              + pad ("ideal step", 13) + pad ("input step", 13)
              + pad ("excess", 13) + "finite");

        double worstExcessPercent = -1.0e30;
        bool allFinite = true;

        for (const auto& sig : signals)
        {
            for (const auto& m : moves)
            {
                const auto t = measureTransition (sig.data, fs,
                                                  lowCutOffHz, m.fromHz,
                                                  lowCutOffHz, m.toHz,
                                                  baseMoveAt, phases);

                worstExcessPercent = std::max (worstExcessPercent, t.excessPercent);
                allFinite = allFinite && t.finite;

                info ("    " + pad (sig.name, 10) + pad (m.label, 14)
                      + pad (fx (t.switchErrorPercent, 4), 14)
                      + pad (fx (t.actualStep, 6), 11)
                      + pad (fx (t.idealStep, 6), 13)
                      + pad (fx (t.inputStep, 6), 13)
                      + pad (fx (t.excess, 9), 13)
                      + (t.finite ? "yes" : "NO"));
            }
        }

        info ("");
        info ("    worst excess over signal or ideal switch  "
              + fx (worstExcessPercent, 6) + " % of signal peak");

        check (allFinite, "no NaN or Inf anywhere in the High Cut transitions");

        // The prime solves y = x algebraically; it is then evaluated in
        // floating point, so the residual is a few ulps of the sample. The
        // gate is five orders of magnitude above that and still 1e-11 of
        // full scale - far below any arithmetic that could be heard.
        checkAtMost (worstExcessPercent, 1.0e-9,
                     "no High Cut transition steps further than the signal or the switch must");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 signoff - continuous automation across the endpoints");
    // -----------------------------------------------------------------------
    {
        /*
            Automation that repeatedly TOUCHES the endpoint rather than
            crossing it once: every touch bypasses, every departure re-enters,
            so whatever a single transition costs is paid over and over.

            The reference is the input's own steepness. A one-pole cannot make
            its input steeper - both forms here are averages of the input and
            a state that is itself an average - and the sweep passes through
            OFF on every cycle, where the output IS the input. So a step
            larger than the input's largest is the switching and nothing else.
        */
        constexpr double fs = 48000.0;
        constexpr int length = 96000;

        info ("  A control sweeping back and forth onto the endpoint, so the");
        info ("  bypass flag drops and lifts repeatedly. The corner is applied");
        info ("  every 64 samples, a control rate no host exceeds.");
        info ("");
        info ("    " + pad ("control", 22) + pad ("touches", 10)
              + pad ("peak", 11) + pad ("worst step", 13)
              + pad ("input step", 13) + pad ("ratio", 9) + "finite");

        struct Sweep { const char* label; bool lowCut; double farHz; };

        const Sweep sweeps[]
        {
            { "Low Cut 20 <-> 30",    true,  30.0 },
            { "Low Cut 20 <-> 1000",  true,  1000.0 },
            { "High Cut 20k <-> 19k", false, 19000.0 },
            { "High Cut 20k <-> 500", false, 500.0 },
        };

        double worstRatio = 0.0;
        bool allFinite = true;

        const auto signal = musicalSignal (length, fs);
        const double inputStep = std::max (largestStep (signal, 1, length), 1.0e-12);

        for (const auto& s : sweeps)
        {
            std::vector<CutMove> moves;
            int touches = 0;

            constexpr int controlPeriod = 64;
            const double sweepHz = 3.0;

            for (int n = controlPeriod; n < length; n += controlPeriod)
            {
                const double phase = std::fmod (sweepHz * n / fs, 1.0);
                const double u = phase < 0.5 ? 2.0 * phase : 2.0 * (1.0 - phase);

                if (s.lowCut)
                {
                    const double hz = lowCutOffHz + u * (s.farHz - lowCutOffHz);
                    moves.push_back ({ n, hz, highCutOffHz });

                    if (hz <= lowCutOffHz)
                        ++touches;
                }
                else
                {
                    const double hz = highCutOffHz - u * (highCutOffHz - s.farHz);
                    moves.push_back ({ n, lowCutOffHz, hz });

                    if (hz >= highCutOffHz)
                        ++touches;
                }
            }

            const auto out = runWithMoves (signal, fs, lowCutOffHz, highCutOffHz, moves);

            const double worstStep = largestStep (out, 1, length);
            const double ratio     = worstStep / inputStep;

            worstRatio = std::max (worstRatio, ratio);
            allFinite = allFinite && allFiniteIn (out);

            info ("    " + pad (s.label, 22) + pad (std::to_string (touches), 10)
                  + pad (fx (peakOf (out), 6), 11)
                  + pad (fx (worstStep, 6), 13)
                  + pad (fx (inputStep, 6), 13)
                  + pad (fx (ratio, 4), 9)
                  + (allFiniteIn (out) ? "yes" : "NO"));
        }

        info ("");
        info ("    worst step ratio across all four sweeps  " + fx (worstRatio, 4));

        check (allFinite, "continuous endpoint automation stays finite");

        checkAtMost (worstRatio, 1.0,
                     "repeatedly touching an endpoint adds no step the signal lacks");
    }
}

} // namespace avtest
