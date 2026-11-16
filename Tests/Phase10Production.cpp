#include "AllocationDetector.h"
#include "DspAnalysis.h"
#include "SignalExport.h"
#include "WetGainStaging.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/EarlyReflections.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 10 - STAGE 3. The production early-reflection path.

    WHAT THIS SUITE IS AND IS NOT
    -----------------------------
    It characterises the SHIPPING components - dsp::EarlyReflections and
    dsp::EarlyLateBalance - driven in the order the processor drives them. It
    is not a replica of the plugin and it does not claim to be one: the
    processor's own topology claims (legacy bit-exactness, Pre-Delay moving
    both branches, Attack and Early Diffusion being unable to reach the early
    layer) are tested against the REAL assembled processor in
    Tests/StateCompatibility, because those are properties of the assembly
    and testing them against a hand-built chain here would be testing the
    hand-built chain.

    What belongs here is everything that is a property of the components:
    the tap schedule, the Size law and its crossfade, the balance law and its
    calibration, the handoff, the stereo behaviour, automation, CPU and
    memory.

    THE ENVELOPE DECAY IS A LISTENING CANDIDATE
    -------------------------------------------
    1.5 is measured here and it improves the handoff valley. It is NOT
    presented as a solved problem. The residual is structural and the
    remaining question is perceptual; this suite's job is to say how large
    the residual is, not to decide whether it matters.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    constexpr double kDecay          = 2.5;
    constexpr double kSizeDefault    = er::referenceSizeControl;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;

    /**
        The shipping schedule's fingerprint. See the suite that checks it:
        it exists so that changing the pattern cannot leave a stale derived
        figure standing in the report.
    */
    constexpr std::uint64_t kPatternFingerprint = 5065939454054309816ull;

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

    // =======================================================================
    //  The production chain, in the processor's order
    // =======================================================================
    /*
        Mirrors AuroraVerbProcessor::processInternal from the pre-delay to the
        branch combine, and stops there. Width at 100 % is the identity and
        Mix is a dry/wet blend; neither is part of what PHASE 10 changes, and
        including them would only put two more stages between a measurement
        and the thing it measures.
    */
    struct Config
    {
        double size      = kSizeDefault;
        double decay     = kDecay;
        double earlyDiff = kEarlyDiffusion;
        double modDepth  = kModDepth;
        double attack    = 0.0;
        double preDelayMs = 0.0;
        double balance   = 0.0;
        er::Pattern pattern = er::defaultPattern;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (c.earlyDiff, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (c.modDepth),
                                      mapping::modRateScaleFor (kModRate), true);

        // Settle, then put the modulators back to a known phase, so two runs
        // of the same configuration are the same run.
        for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).getCore().resetModulation();

        tank->reset();

        return tank;
    }

    [[nodiscard]] double sizeRatioFor (double size)
    {
        return mapping::sizeScaleFor (size) / mapping::sizeScaleFor (kSizeDefault);
    }

    struct Render
    {
        std::vector<double> lateL, lateR;   // trimmed late branch, gain 1
        std::vector<double> erL,   erR;     // early branch, gain 1
        std::vector<double> outL,  outR;    // composed at c.balance
    };

    /** One pass of the production chain. Both branches are kept separately so
        a single render can answer both "what does each layer do" and "what
        does the composition do", from identical history. */
    [[nodiscard]] Render render (const Config& c, const std::vector<double>& drive,
                                 size_t length)
    {
        auto tank = makeTank (c);

        PreDelay<double> pre;
        pre.prepare (kSampleRate);
        pre.snapTo (c.preDelayMs);

        EarlyReflections early;
        early.prepare (kSampleRate, c.pattern);
        early.snapToSizeRatio (sizeRatioFor (c.size));

        AttackInjection attack;
        attack.prepare (kSampleRate);
        attack.snapTo (c.attack);

        EarlyLateBalance balance;
        balance.prepare (kSampleRate);
        balance.snapTo (c.balance, er::balanceCalibration);

        Render out;
        out.lateL.assign (length, 0.0);
        out.lateR.assign (length, 0.0);
        out.erL  .assign (length, 0.0);
        out.erR  .assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            out.lateL[n] = n < drive.size() ? drive[n] : 0.0;
            out.lateR[n] = out.lateL[n];
        }

        const auto count = static_cast<int> (length);

        pre.process (out.lateL.data(), out.lateR.data(), count);

        // The split. The ER branch reads the pre-delayed signal before Attack
        // touches it, which is the whole of "Attack cannot reach the ER".
        early.process (out.lateL.data(), out.lateR.data(),
                       out.erL.data(), out.erR.data(), count);

        attack.process (out.lateL.data(), out.lateR.data(), count);

        for (size_t n = 0; n < length; ++n)
            tank->processSample (out.lateL[n], out.lateR[n]);

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        for (size_t n = 0; n < length; ++n)
        {
            out.lateL[n] *= trim;
            out.lateR[n] *= trim;
        }

        out.outL.assign (length, 0.0);
        out.outR.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            const double gl = balance.nextLate();
            const double ge = balance.nextEarly();

            out.outL[n] = gl * out.lateL[n] + ge * out.erL[n];
            out.outR[n] = gl * out.lateR[n] + ge * out.erR[n];
        }

        return out;
    }

    // =======================================================================
    //  Excitation
    // =======================================================================
    [[nodiscard]] std::vector<double> impulse()
    {
        std::vector<double> x (1, 1.0);
        return x;
    }

    /** A 10 ms noise burst. An impulse leaves a sparse tap train's analysis
        windows empty, which Stage 2 learned the hard way: the "notch" it
        reported was the gaps between single taps, not a handoff. */
    [[nodiscard]] std::vector<double> burst (double ms = 10.0)
    {
        const auto n = static_cast<size_t> (ms * kSampleRate / 1000.0);

        std::vector<double> x (n, 0.0);
        unsigned int s = 22222u;

        for (size_t i = 0; i < n; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;

            const double u = static_cast<double> (s & 0xffffffu) / 16777215.0;
            const double window = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * static_cast<double> (i) / static_cast<double> (n - 1));

            x[i] = (2.0 * u - 1.0) * window;
        }

        return x;
    }

    /** A pure sustained sine. The diagnostic extreme, not a product target:
        it is the source that correlates the two branches most strongly and so
        stresses the crossfade law hardest. */
    [[nodiscard]] std::vector<double> diagnosticSine()
    {
        const auto n = static_cast<size_t> (1.0 * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;
            const double env = std::min (1.0, t * 100.0)
                             * std::min (1.0, (1.0 - t) * 100.0);

            x[i] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * t)
                 * std::max (0.0, env);
        }

        return x;
    }

    // =======================================================================
    //  Analysis
    // =======================================================================
    [[nodiscard]] double energyOf (const std::vector<double>& x)
    {
        double sum = 0.0;

        for (double s : x)
            sum += s * s;

        return sum;
    }

    [[nodiscard]] std::vector<double> stereoMagnitude (const std::vector<double>& l,
                                                       const std::vector<double>& r)
    {
        std::vector<double> out (l.size(), 0.0);

        for (size_t n = 0; n < l.size(); ++n)
            out[n] = std::sqrt (l[n] * l[n] + r[n] * r[n]);

        return out;
    }

    [[nodiscard]] std::vector<double> envelopeDb (const std::vector<double>& x,
                                                  double windowMs, double stepMs,
                                                  double untilMs)
    {
        const auto win  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);
        const auto step = std::max<size_t> (1, static_cast<size_t> (stepMs * kSampleRate / 1000.0));
        const auto last = static_cast<size_t> (untilMs * kSampleRate / 1000.0);

        std::vector<double> out;

        for (size_t from = 0; from + win <= std::min (last, x.size()); from += step)
        {
            double sum = 0.0;

            for (size_t n = from; n < from + win; ++n)
                sum += x[n] * x[n];

            const double r = std::sqrt (sum / static_cast<double> (win));

            out.push_back (r > 0.0 ? 20.0 * std::log10 (r) : -200.0);
        }

        return out;
    }

    /**
        Deepest notch WITH A RECOVERY, inside a window.

        The recovery requirement is not decoration. Stage 2 first measured
        this as a running-maximum drop and reported a 10.87 dB "notch" at
        245 ms that was simply the reverb tail decaying. A decay is not an
        energy hole; a hole has to come back.
    */
    [[nodiscard]] double notchDb (const std::vector<double>& x,
                                  double fromMs, double toMs, double* atMs = nullptr)
    {
        const auto env = envelopeDb (x, 5.0, 5.0, toMs + 50.0);

        if (env.size() < 6)
            return 0.0;

        std::vector<double> smooth (env.size(), 0.0);

        for (size_t i = 0; i < env.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;

            for (int k = -2; k <= 2; ++k)
            {
                const auto j = static_cast<long long> (i) + k;

                if (j >= 0 && j < static_cast<long long> (env.size()))
                {
                    sum += env[static_cast<size_t> (j)];
                    ++count;
                }
            }

            smooth[i] = sum / count;
        }

        const auto from = static_cast<size_t> (fromMs / 5.0);
        const auto to   = std::min (smooth.size(), static_cast<size_t> (toMs / 5.0));

        double worst = 0.0;

        for (size_t i = from + 1; i + 1 < to; ++i)
        {
            double before = -1.0e30, after = -1.0e30;

            for (size_t k = from; k < i; ++k)
                before = std::max (before, smooth[k]);

            for (size_t k = i + 1; k < to; ++k)
                after = std::max (after, smooth[k]);

            const double depth = std::min (before, after) - smooth[i];

            if (depth > worst)
            {
                worst = depth;

                if (atMs != nullptr)
                    *atMs = static_cast<double> (i) * 5.0;
            }
        }

        return worst;
    }

    [[nodiscard]] double firstArrivalMs (const std::vector<double>& x)
    {
        double peak = 0.0;

        for (double s : x)
            peak = std::max (peak, std::abs (s));

        if (peak <= 0.0)
            return -1.0;

        for (size_t n = 0; n < x.size(); ++n)
            if (std::abs (x[n]) >= peak * 0.01)
                return 1000.0 * static_cast<double> (n) / kSampleRate;

        return -1.0;
    }

    struct Stereo { double correlation = 0.0, monoDb = 0.0; };

    [[nodiscard]] Stereo stereoIn (const std::vector<double>& l,
                                   const std::vector<double>& r,
                                   double fromMs, double toMs)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (l.size(),
                                    static_cast<size_t> (toMs * kSampleRate / 1000.0));

        Stereo s;

        if (to <= from)
            return s;

        s.correlation = correlation (l.data() + from, r.data() + from, to - from);

        double stereoEnergy = 0.0, monoEnergy = 0.0;

        for (size_t n = from; n < to; ++n)
        {
            stereoEnergy += l[n] * l[n] + r[n] * r[n];

            const double mono = 0.5 * (l[n] + r[n]);
            monoEnergy += 2.0 * mono * mono;
        }

        s.monoDb = stereoEnergy > 0.0
                 ? 10.0 * std::log10 (monoEnergy / stereoEnergy) : 0.0;

        return s;
    }

    /** Largest sample-to-sample step in a signal. */
    [[nodiscard]] double maxStep (const std::vector<double>& x)
    {
        double worst = 0.0;

        for (size_t n = 1; n < x.size(); ++n)
            worst = std::max (worst, std::abs (x[n] - x[n - 1]));

        return worst;
    }
}

// ===========================================================================

void runPhase10ProductionTests()
{
    beginSuite ("PHASE 10 Stage 3 - the tap schedule");
    {
        EarlyReflections er;
        er.prepare (kSampleRate);

        const auto& p = er.getPattern();

        info ("  The accepted candidate: ER-B, deterministic irregular timing,");
        info ("  decorrelated between channels, 8 events per channel.");
        info ("");
        info ("    " + pad ("event", 8) + pad ("L offset", 11) + pad ("L time", 11)
              + pad ("L weight", 12) + pad ("R offset", 11) + pad ("R time", 11)
              + "R weight");

        double sumSqL = 0.0, sumSqR = 0.0;
        int minOffset = 1 << 30;
        bool duplicate = false;

        std::vector<int> offL, offR;

        for (int k = 0; k < er.getEventCount(); ++k)
        {
            const int dl = er.getOffsetL (k), dr = er.getOffsetR (k);
            const double wl = er.getWeightL (k), wr = er.getWeightR (k);

            for (int prev : offL) if (prev == dl) duplicate = true;
            for (int prev : offR) if (prev == dr) duplicate = true;

            offL.push_back (dl);
            offR.push_back (dr);

            sumSqL += wl * wl;
            sumSqR += wr * wr;
            minOffset = std::min ({ minOffset, dl, dr });

            info ("    " + pad (std::to_string (k), 8)
                  + pad (std::to_string (dl), 11)
                  + pad (fx (dl * 1000.0 / kSampleRate, 2) + " ms", 11)
                  + pad (fx (wl, 4), 12)
                  + pad (std::to_string (dr), 11)
                  + pad (fx (dr * 1000.0 / kSampleRate, 2) + " ms", 11)
                  + fx (wr, 4));
        }

        info ("");
        info ("    span " + fx (er.getSpanMs(), 2) + " ms, "
              + std::to_string (er.getEventCount()) + " events per channel, "
              + std::to_string (er.getEventCount() * 2) + " taps total");
        info ("    sum of squares: L " + fx (sumSqL, 9) + ", R " + fx (sumSqR, 9));

        check (er.getEventCount() == 8, "8 reflection events per channel");
        check (p.envelopeDecay == 1.5, "envelope decay is the accepted 1.5 candidate");
        check (p.sizeAlpha == 0.25, "Size alpha is the accepted 0.25");
        check (std::abs (sumSqL - 1.0) < 1.0e-12 && std::abs (sumSqR - 1.0) < 1.0e-12,
               "each channel's weights are L2-normalised to exactly 1");
        check (minOffset >= 1,
               "no tap sits at offset 0, so none of them is the direct signal re-added");
        check (! duplicate, "no two events in a channel collide on the same sample");

        /*
            IRREGULARITY, TESTED RATHER THAN ASSERTED - AND THIS TEST EARNED
            ITS PLACE.

            The first implementation used PHASE 9's golden-ratio placement
            inside equal cells, and this check failed it: the merged stereo
            gap sequence came out as

                177 119 178 297 302 297 | 177 119 178 297 302 297 | 177 ...

            repeating exactly every seven arrivals, out of only five distinct
            values. frac(phi*k) advances by a constant mod 1, so one event per
            equal cell can only ever produce two distinct gaps - a property of
            the construction, not of the constant. The placement is now a
            fixed-seed draw inside each cell.

            So the criterion is PERIODICITY, not exact equality. Integer
            sample offsets will occasionally produce two equal gaps by
            coincidence and that means nothing; a gap sequence that repeats
            means a comb with a rhythm in it.
        */
        std::vector<int> merged = offL;
        merged.insert (merged.end(), offR.begin(), offR.end());
        std::sort (merged.begin(), merged.end());

        std::vector<int> gaps;

        for (size_t i = 1; i < merged.size(); ++i)
            gaps.push_back (merged[i] - merged[i - 1]);

        auto distinct = gaps;
        std::sort (distinct.begin(), distinct.end());
        distinct.erase (std::unique (distinct.begin(), distinct.end()), distinct.end());

        const auto smallest = *std::min_element (gaps.begin(), gaps.end());
        const auto largest  = *std::max_element (gaps.begin(), gaps.end());

        /*
            The periodicity test: for every period p that fits, how much of
            the sequence repeats itself at that lag. A schedule built on a
            constant mod-1 advance scores 1.0 at its own period; an aperiodic
            one scores low at every lag.
        */
        double worstPeriodicity = 0.0;
        size_t worstPeriod = 0;

        for (size_t lag = 1; lag * 2 <= gaps.size(); ++lag)
        {
            size_t matches = 0, compared = 0;

            for (size_t i = lag; i < gaps.size(); ++i)
            {
                ++compared;

                if (gaps[i] == gaps[i - lag])
                    ++matches;
            }

            const double score = static_cast<double> (matches) / compared;

            if (score > worstPeriodicity)
            {
                worstPeriodicity = score;
                worstPeriod = lag;
            }
        }

        std::string gapRow = "    merged gaps (samples):";

        for (int g : gaps)
            gapRow += " " + std::to_string (g);

        info ("");
        info (gapRow);
        info ("    smallest " + fx (smallest * 1000.0 / kSampleRate, 2)
              + " ms, largest " + fx (largest * 1000.0 / kSampleRate, 2)
              + " ms, " + std::to_string (distinct.size()) + " distinct values out of "
              + std::to_string (gaps.size()));
        info ("    strongest repetition: " + fx (worstPeriodicity * 100.0, 1)
              + " % of the sequence at period " + std::to_string (worstPeriod));

        check (worstPeriodicity < 0.35,
               "the merged arrival sequence does not repeat itself at any period");
        check (distinct.size() >= gaps.size() * 3 / 4,
               "most arrival gaps are distinct rather than drawn from a handful of values");

        /*
            Lopsidedness is a PER-CHANNEL property, and the merged sequence is
            the wrong place to measure it.

            The merged gaps include a zero: at 28.04 ms a left and a right
            arrival land on the same sample. That is not a defect and not a
            doublet - it is one centre-panned reflection, which is a thing
            rooms do. What would be a defect is two arrivals in the SAME
            channel almost on top of each other, and the cell guard is what
            prevents that, so that is what gets checked.
        */
        int worstChannelRatio = 0;

        for (const auto* offsets : { &offL, &offR })
        {
            auto sorted = *offsets;
            std::sort (sorted.begin(), sorted.end());

            int lo = 1 << 30, hi = 0;

            for (size_t i = 1; i < sorted.size(); ++i)
            {
                const int g = sorted[i] - sorted[i - 1];
                lo = std::min (lo, g);
                hi = std::max (hi, g);
            }

            info ("    per channel: smallest " + fx (lo * 1000.0 / kSampleRate, 2)
                  + " ms, largest " + fx (hi * 1000.0 / kSampleRate, 2)
                  + " ms, ratio " + fx (static_cast<double> (hi) / lo, 2) + "x");

            worstChannelRatio = std::max (worstChannelRatio, hi / std::max (1, lo));
        }

        check (worstChannelRatio <= 6,
               "within a channel the cell guard keeps the gaps from becoming lopsided");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - the pattern fingerprint, against stale figures");
    // =======================================================================
    {
        /*
            A GUARD AGAINST THE MISTAKE THIS STAGE ACTUALLY MADE.

            Stage 3 changed the placement law and carried a handoff figure of
            6.02 dB into its report that had been measured with the previous
            one. The real value was 16.47 dB. Nothing failed, because nothing
            connected the quoted number to the pattern it came from.

            This connects them. The fingerprint is computed from the shipping
            schedule - every offset and every weight - and compared with a
            recorded value. Changing the pattern in any way changes it, and
            this check then fails with the list of figures that have to be
            re-measured. It cannot be satisfied by editing the constant alone
            without also looking at what the constant is guarding.

            IF YOU ARE HERE BECAUSE THIS FAILED: re-run the suite, replace
            every number in the list below with what it now reports, update
            Docs/Phase10-Stage3-Production.md to match, and only then update
            the fingerprint.
        */
        EarlyReflections e;
        e.prepare (kSampleRate);

        std::uint64_t fingerprint = 1469598103934665603ull;

        auto mix = [&fingerprint] (double v)
        {
            std::uint64_t bits = 0;
            std::memcpy (&bits, &v, sizeof bits);

            for (int byte = 0; byte < 8; ++byte)
            {
                fingerprint ^= (bits >> (byte * 8)) & 0xffu;
                fingerprint *= 1099511628211ull;
            }
        };

        for (int k = 0; k < e.getEventCount(); ++k)
        {
            mix (static_cast<double> (e.getOffsetL (k)));
            mix (static_cast<double> (e.getOffsetR (k)));
            mix (e.getWeightL (k));
            mix (e.getWeightR (k));
        }

        info ("  The shipping schedule's fingerprint vouches for these figures,");
        info ("  all of which are quoted in the Stage 3 report:");
        info ("");
        info ("    span at the default Size        80.00 ms");
        info ("    worst handoff valley, Size 100  16.47 dB");
        info ("    worst handoff valley, Size 60    3.56 dB");
        info ("    ideal K at the reference         0.7187");
        info ("    merged gap periodicity           0.0 %");
        info ("");
        info ("    fingerprint " + std::to_string (fingerprint));

        check (fingerprint == kPatternFingerprint,
               "the shipping pattern is unchanged, so the derived figures "
               "quoted in the report are still the ones it produces");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - determinism and sample rate");
    // =======================================================================
    {
        /*
            A pattern is specified in milliseconds, so the same Size must give
            the same reflection TIMES at every rate. Comparing sample offsets
            would pass trivially and mean nothing; this compares times.
        */
        EarlyReflections a, b;
        a.prepare (kSampleRate);
        b.prepare (kSampleRate);

        bool identical = true;

        for (int k = 0; k < a.getEventCount(); ++k)
            identical = identical
                     && a.getOffsetL (k) == b.getOffsetL (k)
                     && a.getOffsetR (k) == b.getOffsetR (k)
                     && a.getWeightL (k) == b.getWeightL (k)
                     && a.getWeightR (k) == b.getWeightR (k);

        check (identical, "two prepares produce bit-identical schedules");

        info ("");
        info ("    " + pad ("rate", 12) + pad ("span", 11) + pad ("first", 11)
              + pad ("last", 11) + "worst time error");

        double worstAcrossRates = 0.0;

        std::vector<double> referenceTimes;

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            EarlyReflections e;
            e.prepare (rate);

            std::vector<double> times;

            for (int k = 0; k < e.getEventCount(); ++k)
            {
                times.push_back (e.getOffsetL (k) * 1000.0 / rate);
                times.push_back (e.getOffsetR (k) * 1000.0 / rate);
            }

            std::sort (times.begin(), times.end());

            if (referenceTimes.empty())
                referenceTimes = times;

            double worst = 0.0;

            for (size_t i = 0; i < times.size(); ++i)
                worst = std::max (worst, std::abs (times[i] - referenceTimes[i]));

            worstAcrossRates = std::max (worstAcrossRates, worst);

            info ("    " + pad (fx (rate / 1000.0, 1) + " kHz", 12)
                  + pad (fx (e.getSpanMs(), 2) + " ms", 11)
                  + pad (fx (times.front(), 3) + " ms", 11)
                  + pad (fx (times.back(), 3) + " ms", 11)
                  + fx (worst * 1000.0, 2) + " us");
        }

        info ("");
        info ("    Measured ACROSS rates rather than against each rate's own");
        info ("    nominal span - PHASE 9 made that mistake and reported a");
        info ("    6.79 ms error that was really 0.0116 ms.");

        check (worstAcrossRates < 0.05,
               "reflection times agree to better than 50 us from 44.1 to 192 kHz");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - the Size law");
    // =======================================================================
    {
        info ("  span = windowMs * sizeScaleRatio^0.25, capped at "
              + fx (er::maxSpanMs, 0) + " ms.");
        info ("");
        info ("    " + pad ("Size", 9) + pad ("tank scale", 13) + pad ("ER span", 11)
              + pad ("first ER", 11) + "late first arrival");

        double minSpan = 1.0e30, maxSpan = 0.0;
        bool coversLate = true;

        for (double size : { 0.0, 0.20, 0.50, 0.80, 1.0 })
        {
            EarlyReflections e;
            e.prepare (kSampleRate);
            e.snapToSizeRatio (sizeRatioFor (size));

            Config c;
            c.size = size;

            const auto r = render (c, burst(), static_cast<size_t> (kSampleRate * 0.6));

            const double lateOnset = firstArrivalMs (stereoMagnitude (r.lateL, r.lateR));

            double firstEr = 1.0e30;

            for (int k = 0; k < e.getEventCount(); ++k)
                firstEr = std::min ({ firstEr,
                                      e.getOffsetL (k) * 1000.0 / kSampleRate,
                                      e.getOffsetR (k) * 1000.0 / kSampleRate });

            minSpan = std::min (minSpan, e.getSpanMs());
            maxSpan = std::max (maxSpan, e.getSpanMs());

            // The layer has to reach past where the late field begins, or it
            // is not a handoff at all - it is two sounds with a gap.
            coversLate = coversLate && e.getSpanMs() > lateOnset;

            info ("    " + pad (fx (size * 100.0, 0) + " %", 9)
                  + pad (fx (mapping::sizeScaleFor (size), 3), 13)
                  + pad (fx (e.getSpanMs(), 2) + " ms", 11)
                  + pad (fx (firstEr, 2) + " ms", 11)
                  + fx (lateOnset, 2) + " ms");
        }

        info ("");
        info ("    ER travel " + fx (maxSpan / minSpan, 2)
              + "x against the tank's "
              + fx (mapping::sizeScaleFor (1.0) / mapping::sizeScaleFor (0.0), 2) + "x.");

        check (coversLate, "the ER span reaches past the late field's first arrival at every Size");
        check (maxSpan < 120.0,
               "the span stays under 120 ms, so the layer never becomes a second late reverb");
        check (maxSpan / minSpan > 1.2 && maxSpan / minSpan < 2.5,
               "Size moves the ER span noticeably but far less than it moves the tank");

        /*
            A CEILING THE TEST CAN ACTUALLY FAIL.

            Stage 2 first wrote a coverage check that nothing could fail,
            because every alpha satisfied it. The 120 ms ceiling is what makes
            this pair of checks select alpha rather than merely describe it:
            alpha 0.5 produces 139 ms at Size 100 % and fails.
        */
        er::Pattern halfAlpha;
        halfAlpha.sizeAlpha = 0.5;

        const double atHalf = er::spanFor (halfAlpha, sizeRatioFor (1.0));

        info ("    Control: alpha 0.5 would give " + fx (atHalf, 1)
              + " ms at Size 100 %, which the ceiling rejects.");

        check (atHalf > 120.0, "the rejected alpha 0.5 really does breach the ceiling");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - Size changes crossfade, taps never move");
    // =======================================================================
    {
        /*
            The hazard: moving a delay tap continuously is a delay-time
            derivative, which is a pitch shift applied to whatever is passing
            through it. PHASE 8's pre-delay and PHASE 9's Attack both solve it
            by crossfading between fixed schedules, and this does the same.

            What is measured is the discontinuity a Size move introduces,
            against the discontinuity the signal already has. A reference that
            compares against the destination's own steepness would flatter the
            result; this compares against the steepest step in the input.
        */
        const auto drive = burst (400.0);
        const auto length = static_cast<size_t> (kSampleRate * 1.0);

        EarlyReflections e;
        e.prepare (kSampleRate);
        e.snapToSizeRatio (sizeRatioFor (0.20));

        std::vector<double> inL (length, 0.0), inR (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            inL[n] = n < drive.size() ? drive[n] : 0.0;
            inR[n] = inL[n];
        }

        std::vector<double> outL (length, 0.0), outR (length, 0.0);

        // A Size sweep applied per sample, which is harsher than the
        // processor's per-chunk update.
        for (size_t n = 0; n < length; ++n)
        {
            const double size = 0.20 + 0.80 * static_cast<double> (n) / length;

            e.setSizeScaleRatio (sizeRatioFor (size));
            e.process (inL.data() + n, inR.data() + n,
                       outL.data() + n, outR.data() + n, 1);
        }

        const double inputStep = maxStep (inL);
        const double sweptStep = maxStep (outL);

        // Static reference: the same input at a fixed Size.
        EarlyReflections s;
        s.prepare (kSampleRate);
        s.snapToSizeRatio (sizeRatioFor (0.60));

        std::vector<double> staticL (length, 0.0), staticR (length, 0.0);
        s.process (inL.data(), inR.data(), staticL.data(), staticR.data(),
                   static_cast<int> (length));

        const double staticStep = maxStep (staticL);

        info ("  A full Size sweep, with the span rebuilt every sample.");
        info ("");
        info ("    steepest step in the input        " + fx (inputStep, 6));
        info ("    steepest step, Size held          " + fx (staticStep, 6));
        info ("    steepest step, Size swept         " + fx (sweptStep, 6));
        info ("    fade length                       "
              + fx (e.getFadeLengthSamples() * 1000.0 / kSampleRate, 1) + " ms");

        check (sweptStep <= staticStep * 1.5 + 1.0e-12,
               "sweeping Size adds no step the same input does not already produce");

        // Two spans that round to the same integer sample count must not
        // restart the fade, or a creeping Size smoother would fade forever.
        EarlyReflections q;
        q.prepare (kSampleRate);
        q.snapToSizeRatio (sizeRatioFor (0.60));

        q.setSizeScaleRatio (sizeRatioFor (0.60) * (1.0 + 1.0e-9));

        check (! q.isFading(),
               "a Size nudge too small to move a tap does not start a crossfade");

        q.setSizeScaleRatio (sizeRatioFor (1.0));
        check (q.isFading(), "a Size change that does move the taps does start one");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - the balance calibration constant");
    // =======================================================================
    {
        /*
            K EQUALISES THE TWO BRANCHES ONCE, AT THE REFERENCE CONFIGURATION.

            It cannot be computed from the running signal: that would be
            dynamic normalisation, which PHASE 10's brief forbids by name and
            which would make the control's meaning depend on the programme
            material.

            Stage 2's figure was 0.7811, measured against an UNTRIMMED late
            branch. Production trims the late branch - the decay-aware
            compensation that keeps Decay from doubling as a volume control -
            so that figure does not transfer and the constant has to be
            re-measured here against what actually ships.
        */
        Config c;

        const auto r = render (c, burst(), static_cast<size_t> (kSampleRate * 4.0));

        const double lateEnergy = energyOf (r.lateL) + energyOf (r.lateR);
        const double erEnergy   = energyOf (r.erL)   + energyOf (r.erR);
        const double ideal      = std::sqrt (lateEnergy / erEnergy);

        info ("  Reference: Size 60 %, Decay 2.5 s, Early Diffusion 70 %,");
        info ("  Mod Depth 30 %, Attack 0, Pre-Delay 0, 10 ms noise burst.");
        info ("");
        info ("    trimmed late branch energy   " + fx (lateEnergy, 6));
        info ("    early branch energy          " + fx (erEnergy, 6));
        info ("    ideal K                      " + fx (ideal, 4));
        info ("    shipping constant            " + fx (er::balanceCalibration, 4));
        info ("    error                        "
              + fx (20.0 * std::log10 (er::balanceCalibration / ideal), 3) + " dB");

        check (std::abs (20.0 * std::log10 (er::balanceCalibration / ideal)) < 0.25,
               "the shipping constant is within 0.25 dB of the measured ideal");

        /*
            THE COST OF A FIXED CONSTANT, MEASURED RATHER THAN WAVED AT.

            The late branch's energy moves with Decay and Size and K cannot
            follow it. That is the price of refusing dynamic normalisation,
            and the right thing to do with a price is to state it.
        */
        info ("");
        info ("    How far the calibration drifts away from the reference:");
        info ("");
        info ("    " + pad ("Decay", 10) + pad ("Size", 9) + pad ("ideal K", 11)
              + "error of the fixed constant");

        double worstDrift = 0.0;

        for (double decay : { 0.5, 2.5, 10.0 })
        {
            for (double size : { 0.20, 0.60, 1.0 })
            {
                Config d;
                d.decay = decay;
                d.size  = size;

                const auto rr = render (d, burst(), static_cast<size_t> (kSampleRate * 4.0));

                const double le = energyOf (rr.lateL) + energyOf (rr.lateR);
                const double ee = energyOf (rr.erL)   + energyOf (rr.erR);
                const double k  = std::sqrt (le / ee);
                const double db = 20.0 * std::log10 (er::balanceCalibration / k);

                worstDrift = std::max (worstDrift, std::abs (db));

                info ("    " + pad (fx (decay, 1) + " s", 10)
                      + pad (fx (size * 100.0, 0) + " %", 9)
                      + pad (fx (k, 4), 11)
                      + fx (db, 2) + " dB");
            }
        }

        info ("");
        info ("    Worst drift across that grid: " + fx (worstDrift, 2) + " dB.");
        info ("    This is a REPORTED CHARACTERISTIC, not a defect to be fixed");
        info ("    with a detector. A balance whose calibration tracked Decay");
        info ("    in real time would be the gain normalisation the brief rules");
        info ("    out, and it would make Early/Late behave differently on a");
        info ("    sustained pad and on a snare.");
    }

    // =======================================================================
    beginSuite ("PHASE 10 final - the BAL-4 calibration table");
    // =======================================================================
    {
        /*
            THE TABLE IS A PROPERTY OF THE ER PATTERN, so it is re-measured
            here against the shipping pattern rather than trusted. It was
            re-derived when cellOccupancy became 0.40; carrying the 0.70
            figures forward is exactly the class of mistake the pattern
            fingerprint exists to prevent.

            Measured at each knot with a 10 ms noise burst over a 4 s window,
            every other control at its default. The knot axes are checked
            against the parameter mapping too, because they are stated as
            literals in a header that deliberately does not include it.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);
        const auto drive  = burst();

        info ("  Knot axes, checked against the parameter mapping:");
        info ("");

        const double decayKnots[er::calibrationDecayKnots] { 0.1, 0.5, 2.5, 10.0, 60.0 };
        const double sizeKnots[er::calibrationSizeKnots]   { 0.0, 0.5, 1.0 };

        bool axesAgree = true;

        for (int d = 0; d < er::calibrationDecayKnots; ++d)
            axesAgree = axesAgree
                     && std::abs (mapping::rt60For (decayKnots[d])
                                  - er::calibrationRt60[d]) < 1.0e-9;

        for (int s = 0; s < er::calibrationSizeKnots; ++s)
            axesAgree = axesAgree
                     && std::abs (mapping::sizeScaleFor (sizeKnots[s])
                                  - er::calibrationSizeScale[s]) < 1.0e-9;

        check (axesAgree,
               "the table's rt60 and sizeScale axes match the parameter mapping");

        info ("    " + pad ("Decay", 10) + pad ("Size", 9) + pad ("measured", 11)
              + pad ("in table", 11) + "error");

        double worstError = 0.0;

        for (int d = 0; d < er::calibrationDecayKnots; ++d)
        {
            for (int s = 0; s < er::calibrationSizeKnots; ++s)
            {
                Config c;
                c.decay = decayKnots[d];
                c.size  = sizeKnots[s];

                const auto r = render (c, drive, length);

                const double measured =
                    std::sqrt ((energyOf (r.lateL) + energyOf (r.lateR))
                             / (energyOf (r.erL) + energyOf (r.erR)));

                const double stored = er::calibrationTable[d][s];
                const double errorDb = 20.0 * std::log10 (stored / measured);

                worstError = std::max (worstError, std::abs (errorDb));

                info ("    " + pad (fx (decayKnots[d], 1) + " s", 10)
                      + pad (fx (sizeKnots[s] * 100.0, 0) + " %", 9)
                      + pad (fx (measured, 4), 11)
                      + pad (fx (stored, 4), 11)
                      + fx (errorDb, 2) + " dB");
            }
        }

        info ("");
        info ("    Worst knot error: " + fx (worstError, 3) + " dB.");
        info ("");
        info ("    IF THIS FAILED: the pattern changed and the table did not.");
        info ("    Copy the measured column into er::calibrationTable, then");
        info ("    re-run every derived figure the fingerprint test lists.");

        check (worstError < 0.1,
               "every table knot matches what the shipping pattern measures");

        // The interpolator must reproduce its own knots, or the axes and the
        // table disagree about which cell is which.
        bool knotsExact = true;

        for (int d = 0; d < er::calibrationDecayKnots; ++d)
            for (int s = 0; s < er::calibrationSizeKnots; ++s)
                knotsExact = knotsExact
                          && std::abs (er::calibrationFor (er::calibrationRt60[d],
                                                           er::calibrationSizeScale[s])
                                       / er::calibrationTable[d][s] - 1.0) < 1.0e-9;

        check (knotsExact, "the interpolator returns each knot exactly at that knot");

        /*
            And it must clamp rather than extrapolate past the ends.

            Compared with a tolerance, not with ==, and the reason is worth a
            line: the interpolator blends in the log domain and exponentiates
            once, so even at a knot it returns exp(log(k)) rather than k. That
            round trip is not bit-exact. Writing == here would have been a
            test that fails on arithmetic rather than on behaviour, which is
            the fourth time this stage has nearly shipped one.
        */
        const double lowCorner  = er::calibrationFor (0.001, 0.01);
        const double highCorner = er::calibrationFor (1000.0, 100.0);

        check (std::abs (lowCorner / er::calibrationTable[0][0] - 1.0) < 1.0e-9
                 && std::abs (highCorner
                              / er::calibrationTable[er::calibrationDecayKnots - 1]
                                                    [er::calibrationSizeKnots - 1]
                              - 1.0) < 1.0e-9,
               "the interpolator clamps outside its knots instead of extrapolating");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - what the placement fix cost the handoff");
    // =======================================================================
    {
        /*
            A CORRECTION TO A NUMBER THIS SUITE ITSELF REPORTED.

            The Stage-3 report quoted a 6.02 dB worst handoff valley at Size
            100 %. That figure was measured BEFORE the placement law changed
            from golden-ratio-in-cells to a fixed-seed draw, and it was never
            re-read afterwards. The current figure is 16.47 dB. The report was
            wrong; the measurement was stale, not the code.

            Two things changed in the same step - the placement law and the
            calibration constant K - so the cause has to be separated rather
            than assumed. K is ruled out immediately: the section below shows
            the Size-100 valley is 16.47 dB with the shipped K and 16.48 dB
            with that configuration's ideal K, a 0.01 dB difference. So the
            placement is what moved it.

            WHY IT WOULD. A handoff hole is a gap in arrival density, and the
            cell guard is exactly the knob that controls how wide a gap the
            draw may produce. Golden-ratio placement had two gap values and
            therefore an almost uniform arrival density - periodic, but
            evenly spread. A free draw inside 70 % of each cell can put two
            consecutive arrivals 0.3 cells apart and the next pair 1.7 cells
            apart, and a 14 ms hole in a sparse layer at the moment the late
            field has not yet started is an energy hole.

            So this sweeps the one parameter that trades the two against each
            other, to find out whether aperiodicity and a clean handoff are
            actually in conflict or merely were in the first attempt.

            NOTHING IS CHANGED HERE. `cellOccupancy` is Pattern data and the
            production default stays 0.70; this is a measurement of what the
            alternatives would do.
        */
        info ("  cellOccupancy sets how much of each cell an event may land in.");
        info ("  Small values approach one event per cell centre - even spacing,");
        info ("  which is what golden-ratio placement effectively produced.");
        info ("  Large values allow wide gaps.");
        info ("");
        info ("    " + pad ("occupancy", 12) + pad ("max gap", 11)
              + pad ("periodicity", 14) + pad ("distinct", 11)
              + "valley, Size 100 %, E/L 25 %");

        const auto length = static_cast<size_t> (kSampleRate * 1.0);

        for (double occupancy : { 0.05, 0.20, 0.40, 0.70, 0.95 })
        {
            er::Pattern p;
            p.cellOccupancy = occupancy;

            EarlyReflections e;
            e.prepare (kSampleRate, p);

            std::vector<int> merged;

            for (int k = 0; k < e.getEventCount(); ++k)
            {
                merged.push_back (e.getOffsetL (k));
                merged.push_back (e.getOffsetR (k));
            }

            std::sort (merged.begin(), merged.end());

            std::vector<int> gaps;

            for (size_t i = 1; i < merged.size(); ++i)
                gaps.push_back (merged[i] - merged[i - 1]);

            auto distinct = gaps;
            std::sort (distinct.begin(), distinct.end());
            distinct.erase (std::unique (distinct.begin(), distinct.end()), distinct.end());

            double periodicity = 0.0;

            for (size_t lag = 1; lag * 2 <= gaps.size(); ++lag)
            {
                size_t matches = 0, compared = 0;

                for (size_t i = lag; i < gaps.size(); ++i)
                {
                    ++compared;

                    if (gaps[i] == gaps[i - lag])
                        ++matches;
                }

                periodicity = std::max (periodicity,
                                        static_cast<double> (matches) / compared);
            }

            Config c;
            c.size    = 1.0;
            c.balance = 0.25;
            c.pattern = p;

            const auto r = render (c, burst(), length);
            const auto mag = stereoMagnitude (r.outL, r.outR);
            const double onset = firstArrivalMs (mag);
            const double valley = onset > 0.0 ? notchDb (mag, onset + 5.0, 220.0) : 0.0;

            const auto maxGap = *std::max_element (gaps.begin(), gaps.end());

            info ("    " + pad (fx (occupancy, 2), 12)
                  + pad (fx (maxGap * 1000.0 / kSampleRate, 2) + " ms", 11)
                  + pad (fx (periodicity * 100.0, 1) + " %", 14)
                  + pad (std::to_string (distinct.size()) + " / "
                         + std::to_string (gaps.size()), 11)
                  + fx (valley, 2) + " dB");
        }

        info ("");
        info ("    The shipped 0.70 is in that table, and it is not obviously");
        info ("    the best point on it. Choosing a different one is a");
        info ("    PRODUCTION DSP CHANGE and is not made here.");

        check (true, "the placement/handoff trade-off is characterised, not decided");
    }

    // =======================================================================
    beginSuite ("PHASE 10 final - the balance, measured three ways");
    // =======================================================================
    {
        /*
            THIS REPLACES THE INTENTIONALLY FAILING GATE, AND ONLY BECAUSE THE
            DESIGN CHANGED UNDER IT.

            The old gate said a single fixed K does not generalise across
            Decay and Size, and it was correct: the error reached 6.14 dB on
            the grid it used and 26.26 dB once the grid was extended to the
            parameter's real minimum. Production no longer has a single fixed
            K. It has BAL-4, a parameter-aware table, and the p = 0.50
            linear-in-power law.

            So the gate is replaced rather than relaxed, and the criterion is
            the one the brief set: 1.5 dB of traversal on normal musical
            material.

            THREE ERRORS ARE REPORTED SEPARATELY AND NOT MERGED, because they
            have different causes and different remedies:

              A  PARAMETER-SPACE error - how far the table is from ideal at a
                 given Decay and Size, on the broadband material it was
                 measured with. This is the table's own accuracy and it is
                 fixable by adding knots.

              B  SOURCE-DEPENDENT error - how far the ideal K for a given
                 source is from the ideal K for broadband material at the same
                 settings. No parameter-only law can touch this, because the
                 law does not know what is playing. Closing it would require
                 the signal analysis the brief forbids.

              C  CROSSFADE-LAW SAG - how far the middle of the control falls
                 below the straight line joining its ends. This is the law's
                 own contribution and is independent of both of the above.

            One combined number would hide all three. In particular it would
            let the source-dependent floor be read as a calibration bug, which
            is how a team ends up building an AGC.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        struct Source { const char* name; std::vector<double> drive; bool diagnostic; };

        const auto fu2 = makeFu2Sources (kSampleRate);

        auto fu2Mono = [&fu2] (const char* name)
        {
            std::vector<double> x;

            for (const auto& s : fu2)
                if (s.name == name)
                {
                    x.resize (s.frames.size());

                    for (size_t n = 0; n < s.frames.size(); ++n)
                        x[n] = s.frames[n].l;
                }

            return x;
        };

        auto vocalMono = [] ()
        {
            const auto v = makeVocalReference (kSampleRate, 6.0);

            std::vector<double> x (v.size(), 0.0);

            for (size_t n = 0; n < v.size(); ++n)
                x[n] = v[n].l;

            return x;
        };

        const std::vector<Source> sources
        {
            { "broadband noise", burst(),            false },
            { "snare",           fu2Mono ("snare"),  false },
            { "vocal",           vocalMono(),        false },
            { "piano",           fu2Mono ("piano"),  false },
            { "pad",             fu2Mono ("pad"),    false },
            { "sine diagnostic", diagnosticSine(),   true  },
        };

        struct Setting { const char* name; double decay, size; };

        const std::vector<Setting> settings
        {
            { "defaults",        2.5,  0.60 },
            { "small room",      0.8,  0.30 },
            { "large hall",      4.0,  0.90 },
            { "long + large",   12.0,  1.00 },
            { "short + small",   0.3,  0.10 },
            { "shortest decay",  0.1,  0.60 },
        };

        // ---------------------------------------------------------------
        info ("  A. PARAMETER-SPACE ERROR - the table against ideal, on the");
        info ("     broadband material it was measured with.");
        info ("");
        info ("     " + pad ("setting", 18) + pad ("table K", 11)
              + pad ("ideal K", 11) + "error");

        double worstParameterSpace = 0.0;

        for (const auto& st : settings)
        {
            Config c;
            c.decay = st.decay;
            c.size  = st.size;

            const auto r = render (c, burst(), length);

            const double ideal = std::sqrt ((energyOf (r.lateL) + energyOf (r.lateR))
                                          / (energyOf (r.erL) + energyOf (r.erR)));

            const double k = er::calibrationFor (mapping::rt60For (st.decay),
                                                 mapping::sizeScaleFor (st.size));

            const double db = 20.0 * std::log10 (k / ideal);

            worstParameterSpace = std::max (worstParameterSpace, std::abs (db));

            info ("     " + pad (st.name, 18) + pad (fx (k, 4), 11)
                  + pad (fx (ideal, 4), 11) + fx (db, 2) + " dB");
        }

        info ("");
        info ("     Worst parameter-space error: " + fx (worstParameterSpace, 2) + " dB.");
        info ("");

        // ---------------------------------------------------------------
        info ("  B. SOURCE-DEPENDENT ERROR - each source's own ideal K against");
        info ("     the broadband one, at the same settings. Unreachable by any");
        info ("     parameter-only law.");
        info ("");
        info ("     " + pad ("setting", 18) + pad ("broadband", 12)
              + pad ("worst source", 14) + pad ("which", 18) + "spread");

        double worstSourceSpread = 0.0;

        for (const auto& st : settings)
        {
            Config c;
            c.decay = st.decay;
            c.size  = st.size;

            double broadband = 0.0, worst = 0.0;
            std::string which;

            for (const auto& src : sources)
            {
                if (src.drive.empty())
                    continue;

                const auto r = render (c, src.drive, length);

                const double k = std::sqrt ((energyOf (r.lateL) + energyOf (r.lateR))
                                          / (energyOf (r.erL) + energyOf (r.erR)));

                if (std::string (src.name) == "broadband noise")
                    broadband = k;

                if (src.diagnostic)
                    continue;

                const double db = std::abs (20.0 * std::log10 (k / std::max (1.0e-9, broadband)));

                if (db > worst)
                {
                    worst = db;
                    which = src.name;
                }
            }

            worstSourceSpread = std::max (worstSourceSpread, worst);

            info ("     " + pad (st.name, 18) + pad (fx (broadband, 4), 12)
                  + pad (fx (worst, 2) + " dB", 14) + pad (which, 18)
                  + fx (worst, 2) + " dB");
        }

        info ("");
        info ("     Worst source-dependent error: " + fx (worstSourceSpread, 2) + " dB.");
        info ("");

        // ---------------------------------------------------------------
        info ("  C. CROSSFADE-LAW SAG - the middle against the chord joining");
        info ("     the ends, under the shipping p = 0.50 law.");
        info ("");
        info ("     " + pad ("source", 18) + pad ("traversal", 12)
              + pad ("sag", 10) + "at");

        double worstNormalTraversal = 0.0, worstNormalSag = 0.0;
        double worstDiagnosticTraversal = 0.0;
        std::string worstNormalWhere, worstSagWhere;

        for (const auto& src : sources)
        {
            if (src.drive.empty())
                continue;

            double worstT = 0.0, worstS = 0.0;
            std::string whereT;

            for (const auto& st : settings)
            {
                Config c;
                c.decay = st.decay;
                c.size  = st.size;

                const auto r = render (c, src.drive, length);

                const double k = er::calibrationFor (mapping::rt60For (st.decay),
                                                     mapping::sizeScaleFor (st.size));

                std::array<double, 5> db { 0.0, 0.0, 0.0, 0.0, 0.0 };
                const std::array<double, 5> at { 0.0, 0.25, 0.50, 0.75, 1.0 };

                double reference = 0.0;

                for (size_t i = 0; i < at.size(); ++i)
                {
                    const auto g = er::gainsForBalance (at[i], k);

                    double energy = 0.0;

                    for (size_t n = 0; n < length; ++n)
                    {
                        const double l = g.late * r.lateL[n] + g.early * r.erL[n];
                        const double rr = g.late * r.lateR[n] + g.early * r.erR[n];

                        energy += l * l + rr * rr;
                    }

                    if (i == 0)
                        reference = energy;

                    db[i] = 10.0 * std::log10 (energy / reference);
                }

                double lo = 0.0, hi = 0.0, sag = 0.0;

                for (double v : db) { lo = std::min (lo, v); hi = std::max (hi, v); }

                for (size_t i = 1; i <= 3; ++i)
                    sag = std::max (sag, db[0] + at[i] * (db[4] - db[0]) - db[i]);

                if (hi - lo > worstT)
                {
                    worstT = hi - lo;
                    whereT = st.name;
                }

                worstS = std::max (worstS, sag);
            }

            if (src.diagnostic)
            {
                worstDiagnosticTraversal = std::max (worstDiagnosticTraversal, worstT);
            }
            else
            {
                if (worstT > worstNormalTraversal)
                {
                    worstNormalTraversal = worstT;
                    worstNormalWhere = std::string (src.name) + " at " + whereT;
                }

                if (worstS > worstNormalSag)
                {
                    worstNormalSag = worstS;
                    worstSagWhere = src.name;
                }
            }

            info ("     " + pad (std::string (src.name) + (src.diagnostic ? " *" : ""), 18)
                  + pad (fx (worstT, 2) + " dB", 12)
                  + pad (fx (worstS, 2) + " dB", 10) + whereT);
        }

        info ("");
        info ("     Worst law sag on normal material: " + fx (worstNormalSag, 2)
              + " dB, on " + worstSagWhere + ".");
        info ("");
        info ("  SUMMARY, and the three stay separate on purpose:");
        info ("");
        info ("     A parameter-space      " + fx (worstParameterSpace, 2) + " dB");
        info ("     B source-dependent     " + fx (worstSourceSpread, 2) + " dB");
        info ("     C law sag              " + fx (worstNormalSag, 2) + " dB");
        info ("");
        info ("     worst traversal, normal musical material   "
              + fx (worstNormalTraversal, 2) + " dB");
        info ("       at " + worstNormalWhere);
        info ("     worst traversal, sine diagnostic           "
              + fx (worstDiagnosticTraversal, 2) + " dB");
        info ("");
        info ("     B dominates, and it is the one nothing here can fix. It is");
        info ("     not a defect to be chased: a calibration that followed the");
        info ("     programme material would make Early/Late behave one way on");
        info ("     a snare and another on a pad, which is worse than a known");
        info ("     fixed offset. Reported, carried, not hidden.");

        check (worstParameterSpace < 1.5,
               "A: the table is within 1.5 dB of ideal across the parameter space");

        check (worstNormalSag < 1.0,
               "C: the shipping law contributes under 1 dB of sag");

        check (worstNormalTraversal <= 1.5,
               "the traversal target is met on normal musical material");

        /*
            THE WORST CASE, OPENED UP RATHER THAN QUOTED.

            A 12.90 dB sag on a pad would be a serious product finding, so it
            does not get to stand as one number in a summary. The curve and
            the branch correlation are printed, because a sag has exactly one
            mechanism available to it under this law: the cross term.

            With branches that add in energy, this law makes total/Le equal
            (1-t) + t*r, a straight line, whose dB curve is log-concave and
            therefore sits ABOVE its chord - a bulge, never a sag. A sag
            requires the cross term 2*rho*late*early*sqrt(Le*Ee) to be
            NEGATIVE, which means the two branches are anti-correlated. So if
            rho is negative here, the sag is real and is a property of the
            source, not an artefact of the metric.
        */
        info ("");
        info ("  THE WORST CASE: pad, and what is actually happening to it");
        info ("");
        info ("    " + pad ("setting", 18) + pad ("corr", 10) + pad ("K", 10)
              + pad ("ideal", 10) + "0 / 25 / 50 / 75 / 100 %");

        for (auto setting : { std::pair<double, double> { 2.5, 0.60 },
                              std::pair<double, double> { 0.3, 0.10 } })
        {
            Config c;
            c.decay = setting.first;
            c.size  = setting.second;

            const auto r = render (c, fu2Mono ("pad"), length);

            const double corr = correlation (r.lateL.data(), r.erL.data(),
                                             static_cast<size_t> (kSampleRate * 1.0));

            const double k = er::calibrationFor (mapping::rt60For (setting.first),
                                                 mapping::sizeScaleFor (setting.second));

            const double ideal = std::sqrt ((energyOf (r.lateL) + energyOf (r.lateR))
                                          / (energyOf (r.erL) + energyOf (r.erR)));

            std::string curve;
            double reference = 0.0;

            for (double t : { 0.0, 0.25, 0.50, 0.75, 1.0 })
            {
                const auto g = er::gainsForBalance (t, k);

                double energy = 0.0;

                for (size_t n = 0; n < length; ++n)
                {
                    const double l  = g.late * r.lateL[n] + g.early * r.erL[n];
                    const double rr = g.late * r.lateR[n] + g.early * r.erR[n];

                    energy += l * l + rr * rr;
                }

                if (t == 0.0)
                    reference = energy;

                curve += fx (10.0 * std::log10 (energy / reference), 1) + "  ";
            }

            info ("    " + pad ("D " + fx (setting.first, 1) + " S "
                                + fx (setting.second * 100.0, 0), 18)
                  + pad (fx (corr, 4), 10) + pad (fx (k, 4), 10)
                  + pad (fx (ideal, 4), 10) + curve);
        }

        info ("");
        /*
            AND IS IT THE PATTERN'S FAULT? That decides whether the occupancy
            choice has to be reopened, so it is measured rather than argued.

            The branch correlation on narrowband material depends on where the
            comb's teeth fall relative to the source's partials, which is a
            property of the tap positions. So the same pad could behave very
            differently at a different occupancy, and if 0.40 were uniquely bad
            the decision would need revisiting.
        */
        info ("");
        info ("    Is it the pattern? The same case across the occupancy family:");
        info ("");
        info ("      " + pad ("occupancy", 12) + pad ("corr", 11)
              + "0 / 25 / 50 / 75 / 100 %");

        double worstAcrossOccupancy = 0.0;

        for (double occupancy : { 0.20, 0.40, 0.70 })
        {
            er::Pattern p;
            p.cellOccupancy = occupancy;

            Config c;
            c.decay   = 0.3;
            c.size    = 0.10;
            c.pattern = p;

            const auto r = render (c, fu2Mono ("pad"), length);

            const double corr = correlation (r.lateL.data(), r.erL.data(),
                                             static_cast<size_t> (kSampleRate * 1.0));

            const double k = er::calibrationFor (mapping::rt60For (0.3),
                                                 mapping::sizeScaleFor (0.10));

            std::string curve;
            double reference = 0.0, worst = 0.0;

            for (double t : { 0.0, 0.25, 0.50, 0.75, 1.0 })
            {
                const auto g = er::gainsForBalance (t, k);

                double energy = 0.0;

                for (size_t n = 0; n < length; ++n)
                {
                    const double l  = g.late * r.lateL[n] + g.early * r.erL[n];
                    const double rr = g.late * r.lateR[n] + g.early * r.erR[n];

                    energy += l * l + rr * rr;
                }

                if (t == 0.0)
                    reference = energy;

                const double db = 10.0 * std::log10 (energy / reference);
                worst = std::max (worst, std::abs (db));

                curve += fx (db, 1) + "  ";
            }

            worstAcrossOccupancy = std::max (worstAcrossOccupancy, worst);

            info ("      " + pad (fx (occupancy, 2), 12) + pad (fx (corr, 4), 11)
                  + curve);
        }

        /*
            EVERY OCCUPANCY SHOWS IT, so the effect is architectural. But 0.40
            is the worst of the three AT THIS SETTING, and that could mean
            either that 0.40 is systematically worse or that the correlation
            at one frequency and one setting is a coin toss per pattern.

            Choosing an occupancy on one source at one setting would be
            fitting noise, so the question gets a sweep. If 0.40 is
            systematically worst the decision reopens; if the ranking moves
            around, it does not.
        */
        info ("");
        info ("    Systematic, or a coin toss? The pad across settings:");
        info ("");
        info ("      " + pad ("setting", 18) + pad ("occ 0.20", 12)
              + pad ("occ 0.40", 12) + "occ 0.70");

        std::array<double, 3> familyWorst { 0.0, 0.0, 0.0 };
        std::array<int, 3> familyWins { 0, 0, 0 };

        /*
            TWO NARROWBAND SOURCES AND EIGHT SETTINGS, because the quantity
            being compared is a MAXIMUM and a maximum over five draws is
            mostly draw variance. Sixteen cells per occupancy is still not
            many, but it is enough to tell a systematic difference from a
            lucky one, and the mean is reported next to the worst for exactly
            that reason.
        */
        std::array<double, 3> familySum { 0.0, 0.0, 0.0 };
        int familyCells = 0;

        struct Narrow { const char* name; std::vector<double> drive; };

        const std::vector<Narrow> narrow
        {
            { "pad 220 Hz",  fu2Mono ("pad")   },
            { "sine 440 Hz", diagnosticSine()  },
        };

        for (const auto& src : narrow)
        for (auto setting : { std::pair<double, double> { 0.3, 0.10 },
                              std::pair<double, double> { 0.5, 0.20 },
                              std::pair<double, double> { 0.8, 0.30 },
                              std::pair<double, double> { 1.5, 0.45 },
                              std::pair<double, double> { 2.5, 0.60 },
                              std::pair<double, double> { 4.0, 0.90 },
                              std::pair<double, double> { 8.0, 0.95 },
                              std::pair<double, double> { 12.0, 1.00 } })
        {
            std::string row = "      " + pad (std::string (src.name).substr (0, 4)
                                              + " D " + fx (setting.first, 1) + " S "
                                              + fx (setting.second * 100.0, 0), 18);

            std::array<double, 3> here { 0.0, 0.0, 0.0 };
            int index = 0;

            for (double occupancy : { 0.20, 0.40, 0.70 })
            {
                er::Pattern p;
                p.cellOccupancy = occupancy;

                Config c;
                c.decay   = setting.first;
                c.size    = setting.second;
                c.pattern = p;

                const auto r = render (c, src.drive, length);

                const double k = er::calibrationFor (mapping::rt60For (setting.first),
                                                     mapping::sizeScaleFor (setting.second));

                double reference = 0.0, worst = 0.0;

                for (double t : { 0.0, 0.25, 0.50, 0.75, 1.0 })
                {
                    const auto g = er::gainsForBalance (t, k);

                    double energy = 0.0;

                    for (size_t n = 0; n < length; ++n)
                    {
                        const double l  = g.late * r.lateL[n] + g.early * r.erL[n];
                        const double rr = g.late * r.lateR[n] + g.early * r.erR[n];

                        energy += l * l + rr * rr;
                    }

                    if (t == 0.0)
                        reference = energy;

                    worst = std::max (worst, std::abs (10.0 * std::log10 (energy / reference)));
                }

                here[static_cast<size_t> (index)] = worst;
                familyWorst[static_cast<size_t> (index)] =
                    std::max (familyWorst[static_cast<size_t> (index)], worst);
                familySum[static_cast<size_t> (index)] += worst;

                row += pad (fx (worst, 1) + " dB", 12);
                ++index;
            }

            ++familyCells;

            const auto best = static_cast<size_t> (
                std::min_element (here.begin(), here.end()) - here.begin());

            ++familyWins[best];

            info (row);
        }

        info ("");
        info ("      " + pad ("worst of each", 18)
              + pad (fx (familyWorst[0], 1) + " dB", 12)
              + pad (fx (familyWorst[1], 1) + " dB", 12)
              + fx (familyWorst[2], 1) + " dB");
        info ("      " + pad ("mean of each", 18)
              + pad (fx (familySum[0] / familyCells, 1) + " dB", 12)
              + pad (fx (familySum[1] / familyCells, 1) + " dB", 12)
              + fx (familySum[2] / familyCells, 1) + " dB");
        info ("      " + pad ("cells won", 18)
              + pad (std::to_string (familyWins[0]), 12)
              + pad (std::to_string (familyWins[1]), 12)
              + std::to_string (familyWins[2]));
        info ("");

        /*
            THE DISCRIMINATING STATISTIC IS THE SPREAD OF THE MEANS, not any
            one occupancy's maximum.

            A maximum over sixteen near-random draws is dominated by the one
            unluckiest cell, and 0.40's 13.4 dB against 0.70's 11.3 dB is the
            kind of difference that gets reshuffled by adding a setting. The
            means are 3.8, 4.9 and 4.8 dB - a 1.1 dB spread against an effect
            reaching 13.4 dB - and the cells won are 5, 5 and 6, which is a
            tie.

            So the effect is a property of the ARCHITECTURE: two branches
            carrying the same narrow band, whose relative phase at that
            frequency is arbitrary but deterministic. Reopening the occupancy
            choice would be chasing it around the family rather than fixing
            it, and picking the occupancy that happened to win on one source
            at one setting would be fitting noise.
        */
        const double meanLo = *std::min_element (familySum.begin(), familySum.end())
                            / familyCells;
        const double meanHi = *std::max_element (familySum.begin(), familySum.end())
                            / familyCells;

        check (meanHi - meanLo < 2.0,
               "every occupancy suffers the narrowband effect about equally, so "
               "it is architectural and not the occupancy choice");

        info ("    If every occupancy shows it, the effect belongs to the");
        info ("    ARCHITECTURE - two branches carrying the same narrow band -");
        info ("    and reopening the occupancy choice would be chasing it");
        info ("    around the family rather than fixing it.");
        info ("");
        info ("    If the correlation column is NEGATIVE, the sag is real and");
        info ("    is the branches cancelling each other mid-travel on narrow-");
        info ("    band material - a property of the source meeting this");
        info ("    architecture, not of the calibration or the metric. It is a");
        info ("    PRODUCT CONCERN and is carried as one; the brief says a");
        info ("    repeated 5-8 dB movement on normal musical material cannot");
        info ("    be hidden by averaging, and this is larger than that.");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - BAL-B holds the wet energy");
    // =======================================================================
    {
        info ("  Early/Late is a COMPOSITION control. Moving it must change");
        info ("  what the reverb is made of, not how loud it is.");
        info ("");
        info ("    " + pad ("Early/Late", 13) + pad ("late gain", 12)
              + pad ("early gain", 12) + pad ("wet energy", 13) + "vs 0 %");

        const auto length = static_cast<size_t> (kSampleRate * 4.0);
        const auto drive  = burst();

        double reference = 0.0;
        double worst = 0.0;

        for (double balance : { 0.0, 0.25, 0.50, 0.75, 1.0 })
        {
            Config c;
            c.balance = balance;

            const auto r = render (c, drive, length);
            const auto g = er::gainsForBalance (balance, er::balanceCalibration);

            const double e = energyOf (r.outL) + energyOf (r.outR);

            if (balance == 0.0)
                reference = e;

            const double db = 10.0 * std::log10 (e / reference);
            worst = std::max (worst, std::abs (db));

            info ("    " + pad (fx (balance * 100.0, 0) + " %", 13)
                  + pad (fx (g.late, 4), 12)
                  + pad (fx (g.early, 4), 12)
                  + pad (fx (e, 4), 13)
                  + fx (db, 2) + " dB");
        }

        info ("");
        info ("    Worst deviation across the travel: " + fx (worst, 2) + " dB.");

        check (worst < 1.0,
               "total wet energy stays within 1 dB across the whole Early/Late travel");

        const auto zero = er::gainsForBalance (0.0, er::balanceCalibration);
        const auto one  = er::gainsForBalance (1.0, er::balanceCalibration);

        check (zero.late == 1.0 && zero.early == 0.0,
               "balance 0 is exactly late 1, early 0");
        check (one.late == 0.0,
               "balance 100 % is exactly zero late, so 'early only' is literal");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - the handoff, and what 1.5 did to it");
    // =======================================================================
    {
        /*
            MEASURED ON sqrt(l^2 + r^2), NOT ON ONE CHANNEL.

            Stage 2 measured one channel first and reported a 40 dB "notch"
            that was per-channel ER sparseness: the two channels have
            different arrival times and fill each other's gaps. The question
            is about the field a listener is in, which is both channels at
            once. Correcting this turned Stage 2's 6.93 dB into 9.03 dB, so
            the correction made the number worse rather than better.
        */
        info ("  Worst notch with a recovery, measured on the stereo magnitude,");
        info ("  from 5 ms after onset out to 220 ms.");
        info ("");
        info ("  THE DEFAULT SIZE IS IN THIS GRID BECAUSE IT HAS TO BE. The");
        info ("  first version of the loop ran 0, 20, 50, 80 and 100 % and then");
        info ("  asserted a bound on 'the default Size' - which is 60 and was");
        info ("  never rendered, so that variable stayed at zero and the check");
        info ("  could not fail. Stage 2 caught a test like this once already.");
        info ("");
        info ("    " + pad ("Size", 9) + pad ("E/L 25 %", 12) + pad ("E/L 50 %", 12)
              + pad ("E/L 75 %", 12) + "worst");

        const auto length = static_cast<size_t> (kSampleRate * 1.0);
        const auto drive  = burst();

        double worstOverall = 0.0;
        double worstAtDefaultSize = 0.0;

        /*
            THE SWEEP IS A NAMED LIST SO IT CAN BE CHECKED, not a brace
            initialiser inside the for. Stage 3 asserted a bound on "the
            default Size" against a loop that never rendered it, and the only
            thing that would have caught it is the loop's contents being
            something a test can look at.
        */
        const std::vector<double> sweepSizes { 0.0, 0.20, 0.50, kSizeDefault, 0.80, 1.0 };

        check (std::find (sweepSizes.begin(), sweepSizes.end(), kSizeDefault)
                 != sweepSizes.end(),
               "the handoff sweep includes the default Size");

        for (double size : sweepSizes)
        {
            std::string row = "    " + pad (fx (size * 100.0, 0) + " %", 9);
            double worstHere = 0.0;

            for (double balance : { 0.25, 0.50, 0.75 })
            {
                Config c;
                c.size    = size;
                c.balance = balance;

                const auto r = render (c, drive, length);
                const auto mag = stereoMagnitude (r.outL, r.outR);

                const double onset = firstArrivalMs (mag);
                const double notch = onset > 0.0 ? notchDb (mag, onset + 5.0, 220.0) : 0.0;

                worstHere = std::max (worstHere, notch);

                row += pad (fx (notch, 2) + " dB", 12);
            }

            if (size == kSizeDefault)
                worstAtDefaultSize = worstHere;

            worstOverall = std::max (worstOverall, worstHere);

            info (row + fx (worstHere, 2) + " dB");

            /*
                THE STRUCTURE BEHIND THE NUMBER, on a second line per Size.

                A valley figure on its own says nothing about whether the two
                layers overlap at all. The overlap is the ER span minus the
                late field's first arrival: positive means the early layer is
                still sounding when the late one starts, which is the
                condition for a handoff rather than a gap. Where the valley
                SITS matters too - one inside the ER span is a comb, one after
                it is a genuine handoff hole.
            */
            {
                EarlyReflections e;
                e.prepare (kSampleRate);
                e.snapToSizeRatio (sizeRatioFor (size));

                double firstEr = 1.0e30, lastEr = 0.0;

                for (int k = 0; k < e.getEventCount(); ++k)
                {
                    const double l = e.getOffsetL (k) * 1000.0 / kSampleRate;
                    const double r2 = e.getOffsetR (k) * 1000.0 / kSampleRate;

                    firstEr = std::min ({ firstEr, l, r2 });
                    lastEr  = std::max ({ lastEr, l, r2 });
                }

                Config c;
                c.size    = size;
                c.balance = 0.25;

                const auto r = render (c, drive, length);

                const double lateOnset =
                    firstArrivalMs (stereoMagnitude (r.lateL, r.lateR));

                const auto mag = stereoMagnitude (r.outL, r.outR);
                const double onset = firstArrivalMs (mag);

                double atMs = 0.0;
                const double notch = onset > 0.0
                                   ? notchDb (mag, onset + 5.0, 220.0, &atMs) : 0.0;

                info ("             ER " + fx (firstEr, 2) + " to "
                      + fx (lastEr, 2) + " ms, late from " + fx (lateOnset, 2)
                      + " ms, overlap " + fx (lastEr - lateOnset, 2)
                      + " ms, valley " + fx (notch, 2) + " dB at "
                      + fx (atMs, 0) + " ms");
            }
        }

        info ("");

        /*
            THE COMPARISON THAT MATTERS: the same grid at Stage 2's envelope.
            Without it, "1.5 is better" is a claim about a number with nothing
            to be better than.
        */
        er::Pattern old;
        old.envelopeDecay = 0.6;

        info ("    Control, the Stage-2 envelope (decay 0.6), at Size 100 %:");

        double worstOld = 0.0;

        {
            // Rendered through the same chain with the old pattern, so the
            // only thing that differs between the two numbers is the decay.
            for (double balance : { 0.25, 0.50, 0.75 })
            {
                auto tank = makeTank ({ 1.0, kDecay, kEarlyDiffusion, kModDepth, 0.0, 0.0, balance });

                EarlyReflections e;
                e.prepare (kSampleRate, old);
                e.snapToSizeRatio (sizeRatioFor (1.0));

                EarlyLateBalance b;
                b.prepare (kSampleRate);
                b.snapTo (balance, er::balanceCalibration);

                std::vector<double> l (length, 0.0), r (length, 0.0);
                std::vector<double> el (length, 0.0), er_ (length, 0.0);

                for (size_t n = 0; n < length; ++n)
                {
                    l[n] = n < drive.size() ? drive[n] : 0.0;
                    r[n] = l[n];
                }

                e.process (l.data(), r.data(), el.data(), er_.data(),
                           static_cast<int> (length));

                for (size_t n = 0; n < length; ++n)
                    tank->processSample (l[n], r[n]);

                const double trim = mapping::previewWetTrim (mapping::rt60For (kDecay),
                                                             mapping::sizeScaleFor (1.0));

                std::vector<double> ol (length, 0.0), orr (length, 0.0);

                for (size_t n = 0; n < length; ++n)
                {
                    const double gl = b.nextLate(), ge = b.nextEarly();

                    ol[n]  = gl * l[n] * trim + ge * el[n];
                    orr[n] = gl * r[n] * trim + ge * er_[n];
                }

                const auto mag = stereoMagnitude (ol, orr);
                const double onset = firstArrivalMs (mag);

                worstOld = std::max (worstOld,
                                     onset > 0.0 ? notchDb (mag, onset + 5.0, 220.0) : 0.0);
            }
        }

        // The ER tail: how quietly the layer ends relative to how loudly it
        // starts. That shape number is what `decay` actually changes.
        auto tailOf = [] (const er::Pattern& p)
        {
            EarlyReflections e;
            e.prepare (kSampleRate, p);
            e.snapToSizeRatio (sizeRatioFor (1.0));

            double firstG = 0.0, lastG = 0.0;
            int firstT = 1 << 30, lastT = 0;

            for (int k = 0; k < e.getEventCount(); ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int t = ch == 0 ? e.getOffsetL (k) : e.getOffsetR (k);
                    const double g = std::abs (ch == 0 ? e.getWeightL (k) : e.getWeightR (k));

                    if (t < firstT) { firstT = t; firstG = g; }
                    if (t > lastT)  { lastT  = t; lastG  = g; }
                }

            return 20.0 * std::log10 (lastG / firstG);
        };

        info ("");
        info ("    " + pad ("envelope", 14) + pad ("ER tail", 12) + "worst notch, Size 100 %");
        info ("    " + pad ("decay 0.6", 14) + pad (fx (tailOf (old), 1) + " dB", 12)
              + fx (worstOld, 2) + " dB");
        info ("    " + pad ("decay 1.5", 14) + pad (fx (tailOf (er::defaultPattern), 1) + " dB", 12)
              + fx (worstOverall, 2) + " dB");

        info ("");
        info ("    WHAT THIS DOES AND DOES NOT SHOW. 1.5 improves the measured");
        info ("    valley and the mechanism is understood: a flatter envelope");
        info ("    leaves the layer audible for longer, so it is still there");
        info ("    when a large-Size late field finally establishes.");
        info ("");
        info ("    IT DOES NOT SHOW THE PROBLEM IS SOLVED. The residual is");
        info ("    structural - at Size 100 % the late field's first arrival is");
        info ("    around 54 ms, and a quiet early layer cannot bridge that -");
        info ("    and no envelope constant removes it. Whether the remaining");
        info ("    valley reads as natural separation or as a hole is a");
        info ("    LISTENING question. 1.5 is a production listening candidate,");
        info ("    not an accepted shipping constant.");

        check (worstOverall <= worstOld,
               "the accepted envelope is no worse than Stage 2's on the worst case");

        check (worstAtDefaultSize < 6.0,
               "at the default Size the handoff valley stays under 6 dB");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - stereo behaviour of the composed wet");
    // =======================================================================
    {
        /*
            NOT "MONO-NEUTRAL". Stage 1 called a -3.01 dB downmix exactly that
            and the claim was withdrawn: -3.01 dB is what two approximately
            decorrelated equal-energy channels give when summed, and reading
            it as proof that no cancellation exists anywhere is a different
            and much stronger claim than the measurement supports.

            So this reports the numbers and says what they are consistent
            with. It does not certify an absence.
        */
        info ("    " + pad ("Early/Late", 13) + pad ("correlation", 14)
              + pad ("mono sum", 12) + "vs the decorrelated expectation");

        const auto length = static_cast<size_t> (kSampleRate * 2.0);
        const auto drive  = burst();

        double worstMono = 0.0;

        for (double balance : { 0.0, 0.25, 0.50, 0.75, 1.0 })
        {
            Config c;
            c.balance = balance;

            const auto r = render (c, drive, length);
            const auto s = stereoIn (r.outL, r.outR, 0.0, 1000.0);

            worstMono = std::max (worstMono, std::abs (s.monoDb + 3.01));

            info ("    " + pad (fx (balance * 100.0, 0) + " %", 13)
                  + pad (fx (s.correlation, 4), 14)
                  + pad (fx (s.monoDb, 2) + " dB", 12)
                  + fx (s.monoDb + 3.01, 2) + " dB");
        }

        info ("");
        info ("    Every figure sits close to -3.01 dB, which is CONSISTENT");
        info ("    WITH the downmix of two approximately decorrelated channels");
        info ("    of similar energy. It is not a proof that no cancellation");
        info ("    occurs anywhere in the spectrum, and it is not offered as");
        info ("    one - the ER layer gives each channel its own arrival times");
        info ("    and its own signs, which is exactly the structure that can");
        info ("    produce narrow-band cancellation in a sum.");

        check (worstMono < 1.5,
               "the mono downmix stays near the decorrelated expectation at every balance");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - balance automation moves gains, not taps");
    // =======================================================================
    {
        /*
            The brief rules out smoothing the balance by moving reflections in
            time, so the first thing to establish is that nothing does.
        */
        EarlyReflections e;
        e.prepare (kSampleRate);
        e.snapToSizeRatio (sizeRatioFor (kSizeDefault));

        std::vector<int> before;

        for (int k = 0; k < e.getEventCount(); ++k)
        {
            before.push_back (e.getOffsetL (k));
            before.push_back (e.getOffsetR (k));
        }

        EarlyLateBalance b;
        b.prepare (kSampleRate);

        bool moved = false;

        for (double balance : { 0.0, 0.13, 0.5, 0.87, 1.0 })
        {
            b.setBalance (balance, er::balanceCalibration);

            size_t i = 0;

            for (int k = 0; k < e.getEventCount(); ++k)
            {
                moved = moved || before[i++] != e.getOffsetL (k);
                moved = moved || before[i++] != e.getOffsetR (k);
            }
        }

        check (! moved, "no balance position changes a single reflection time");

        /*
            Then the automation itself. The reference is the steepest step the
            same material produces with the balance held, because a crossfade
            cannot be judged against a flat line.
        */
        const auto length = static_cast<size_t> (kSampleRate * 2.0);
        const auto drive  = burst (1500.0);

        Config c;
        c.balance = 0.5;

        const auto held = render (c, drive, length);
        const double heldStep = maxStep (held.outL);

        info ("");
        info ("    " + pad ("sequence", 34) + pad ("steepest step", 16) + "vs held");

        struct Seq { const char* name; std::vector<double> points; };

        const std::vector<Seq> sequences
        {
            { "0 -> 100",                     { 0.0, 1.0 } },
            { "100 -> 0",                     { 1.0, 0.0 } },
            { "0 -> 25",                      { 0.0, 0.25 } },
            { "25 -> 75",                     { 0.25, 0.75 } },
            { "75 -> 10",                     { 0.75, 0.10 } },
            { "0 -> 100 -> 0 -> 100",         { 0.0, 1.0, 0.0, 1.0 } },
            { "random, 40 steps",             {} },
        };

        double worstRatio = 0.0;

        for (const auto& seq : sequences)
        {
            auto points = seq.points;

            if (points.empty())
            {
                unsigned int s = 777u;

                for (int i = 0; i < 40; ++i)
                {
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    points.push_back (static_cast<double> (s & 0xffffu) / 65535.0);
                }
            }

            auto tank = makeTank (c);

            EarlyReflections er_;
            er_.prepare (kSampleRate);
            er_.snapToSizeRatio (sizeRatioFor (kSizeDefault));

            EarlyLateBalance bal;
            bal.prepare (kSampleRate);
            bal.snapTo (points.front(), er::balanceCalibration);

            std::vector<double> l (length, 0.0), r (length, 0.0);
            std::vector<double> el (length, 0.0), err (length, 0.0);

            for (size_t n = 0; n < length; ++n)
            {
                l[n] = n < drive.size() ? drive[n] : 0.0;
                r[n] = l[n];
            }

            er_.process (l.data(), r.data(), el.data(), err.data(),
                         static_cast<int> (length));

            for (size_t n = 0; n < length; ++n)
                tank->processSample (l[n], r[n]);

            const double trim = mapping::previewWetTrim (mapping::rt60For (kDecay),
                                                         mapping::sizeScaleFor (kSizeDefault));

            std::vector<double> out (length, 0.0);

            // Steps land on arbitrary sample indices, not on block
            // boundaries, and the sequence is walked across the whole render.
            for (size_t n = 0; n < length; ++n)
            {
                const auto step = points.size() * n / length;

                bal.setBalance (points[std::min (step, points.size() - 1)], er::balanceCalibration);

                out[n] = bal.nextLate() * l[n] * trim + bal.nextEarly() * el[n];
            }

            const double step = maxStep (out);
            const double ratio = step / heldStep;

            worstRatio = std::max (worstRatio, ratio);

            info ("    " + pad (seq.name, 34)
                  + pad (fx (step, 6), 16)
                  + fx (ratio, 3) + "x");
        }

        info ("");
        info ("    Held reference: " + fx (heldStep, 6)
              + ". A ratio near 1 means the automation adds nothing the");
        info ("    material does not already contain.");

        check (worstRatio < 1.25,
               "no balance automation sequence produces a step the held signal does not");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 3 - CPU and memory");
    // =======================================================================
    {
        /*
            Measured as min-of-N against a baseline taken in the SAME run,
            because a single timing on a machine with other work on it
            measures the other work.
        */
        constexpr int blockSize = 512;
        constexpr int blocks = 400;
        constexpr int repeats = 7;

        std::vector<double> inL (blockSize, 0.0), inR (blockSize, 0.0);
        std::vector<double> outL (blockSize, 0.0), outR (blockSize, 0.0);

        unsigned int s = 99u;

        for (int i = 0; i < blockSize; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            inL[i] = inR[i] = static_cast<double> (s & 0xffffu) / 32768.0 - 1.0;
        }

        volatile double sink = 0.0;

        auto timeIt = [&] (auto&& body)
        {
            double best = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                body();
                const auto t1 = std::chrono::steady_clock::now();

                best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            }

            return best;
        };

        auto tank = makeTank ({});

        const double tankTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                std::copy (inL.begin(), inL.end(), outL.begin());
                std::copy (inR.begin(), inR.end(), outR.begin());

                for (int i = 0; i < blockSize; ++i)
                    tank->processSample (outL[i], outR[i]);

                sink += outL[0];
            }
        });

        EarlyReflections e;
        e.prepare (kSampleRate);

        const double erTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                e.process (inL.data(), inR.data(), outL.data(), outR.data(), blockSize);
                sink += outL[0];
            }
        });

        const double bypassTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                e.writeOnly (inL.data(), inR.data(), blockSize);
                sink += inL[0];
            }
        });

        const double samples = static_cast<double> (blocks) * blockSize;
        const double realtime = samples / kSampleRate;

        info ("  " + std::to_string (blocks) + " blocks of " + std::to_string (blockSize)
              + " samples, best of " + std::to_string (repeats) + ".");
        info ("");
        info ("    " + pad ("stage", 26) + pad ("time", 13) + pad ("% of real time", 17)
              + "relative to the tank");
        info ("    " + pad ("the tank", 26) + pad (fx (tankTime * 1000.0, 2) + " ms", 13)
              + pad (fx (100.0 * tankTime / realtime, 2) + " %", 17) + "1.00x");
        info ("    " + pad ("early reflections", 26) + pad (fx (erTime * 1000.0, 2) + " ms", 13)
              + pad (fx (100.0 * erTime / realtime, 2) + " %", 17)
              + fx (erTime / tankTime, 3) + "x");
        info ("    " + pad ("ER at Early/Late 0", 26) + pad (fx (bypassTime * 1000.0, 2) + " ms", 13)
              + pad (fx (100.0 * bypassTime / realtime, 2) + " %", 17)
              + fx (bypassTime / tankTime, 3) + "x");

        /*
            THE TWO NEW PIECES OF THE FINAL DESIGN, timed separately because
            the brief asks for them separately and because lumping them into
            the ER figure would hide which one costs anything.

            Both run ONCE PER BLOCK in production - they produce smoother
            targets, not samples - so they are timed per call and the per-block
            cost is that figure divided by nothing at all.
        */
        constexpr int lawIterations = 2000000;

        volatile double lawSink = 0.0;

        auto timeCalls = [&] (auto&& body)
        {
            double best = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                body();
                const auto t1 = std::chrono::steady_clock::now();

                best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            }

            return best / lawIterations;
        };

        const double lawTime = timeCalls ([&]
        {
            double acc = 0.0;

            for (int i = 0; i < lawIterations; ++i)
            {
                const auto g = er::gainsForBalance (static_cast<double> (i % 1000) * 0.001,
                                                    er::balanceCalibration);
                acc += g.late + g.early;
            }

            lawSink += acc;
        });

        const double lookupTime = timeCalls ([&]
        {
            double acc = 0.0;

            for (int i = 0; i < lawIterations; ++i)
                acc += er::calibrationFor (0.1 + static_cast<double> (i % 600) * 0.1,
                                           0.25 + static_cast<double> (i % 70) * 0.025);

            lawSink += acc;
        });

        // A 512-sample block at 48 kHz is 10.667 ms of wall clock.
        constexpr double blockBudget = 512.0 / 48000.0;

        info ("");
        info ("    " + pad ("per call, once per block", 32) + pad ("time", 13)
              + "share of a 512-sample block");
        info ("    " + pad ("p = 0.50 gain law (sqrt)", 32)
              + pad (fx (lawTime * 1.0e9, 2) + " ns", 13)
              + fx (lawTime / blockBudget * 100.0, 5) + " %");
        info ("    " + pad ("BAL-4 lookup + interpolation", 32)
              + pad (fx (lookupTime * 1.0e9, 2) + " ns", 13)
              + fx (lookupTime / blockBudget * 100.0, 5) + " %");
        info ("");
        info ("    The lookup is the dearer of the two because it evaluates");
        info ("    eight logarithms - two for the inputs and six for the knots");
        info ("    and corners. Precomputing the log knots would remove most of");
        info ("    that and is NOT done, because it would trade a clear table");
        info ("    of measured values for a parallel array of their logarithms");
        info ("    that someone would eventually fail to keep in step. At once");
        info ("    per block the cost does not justify the hazard.");
        info ("");
        info ("    The law uses sqrt rather than pow, as required: pow measured");
        info ("    2.3x the trigonometric form and sqrt measures a third of it.");

        check (lawTime < 1.0e-7 && lookupTime < 1.0e-6,
               "the gain law and the calibration lookup are both negligible");

        (void) lawSink;

        const size_t ring = static_cast<size_t> (std::ceil (er::maxSpanMs * kSampleRate / 1000.0)) + 4;
        size_t rounded = 1;
        while (rounded < ring) rounded <<= 1;

        info ("");
        info ("    ring buffers: 2 x " + std::to_string (rounded) + " doubles = "
              + fx (2.0 * rounded * 8.0 / 1024.0, 1) + " KiB at 48 kHz,");
        info ("    sized for the " + fx (er::maxSpanMs, 0)
              + " ms ceiling rather than the current span, so a Size");
        info ("    change never allocates.");

        check (erTime < tankTime * 0.5,
               "the ER layer costs well under half what the tank costs");
        check (bypassTime < erTime * 0.5,
               "at Early/Late 0 the layer costs a fraction of its active cost");

        (void) sink;

        /*
            NOTHING ON THE AUDIO THREAD MAY ALLOCATE, and a Size change is the
            one place where that could plausibly fail: it rebuilds a tap
            schedule. The schedules are sized once in prepare and the rebuild
            writes into them in place, so a vector assignment between two
            equal-sized vectors cannot reallocate - but "cannot" is the kind
            of claim this project turns into an assertion rather than a
            comment.
        */
        info ("");

        if (allocationCountingAvailable())
        {
            EarlyReflections live;
            live.prepare (kSampleRate);

            EarlyLateBalance bal;
            bal.prepare (kSampleRate);

            ScopedAllocationCounter counter;

            for (int b = 0; b < 200; ++b)
            {
                // A Size sweep and a balance sweep together, both changing
                // every block, which is harsher than any host will be.
                live.setSizeScaleRatio (sizeRatioFor (static_cast<double> (b % 101) * 0.01));
                bal .setBalance (static_cast<double> (b % 97) / 96.0, er::balanceCalibration);

                live.process (inL.data(), inR.data(), outL.data(), outR.data(), blockSize);

                for (int i = 0; i < blockSize; ++i)
                    sink += bal.nextLate() * outL[i] + bal.nextEarly() * outR[i];
            }

            /*
                SNAPSHOT FIRST, REPORT SECOND.

                The first version of this read counter.count() twice: once
                inside the info() call and once inside the check(). It
                reported "0 allocations" and failed, which is a contradiction
                until you notice that building the message is itself a string
                concatenation - so info() allocated, and the second read saw
                its own report. The measurement was right and the test was
                wrong, which is the fourth time that has happened in this
                stage.
            */
            const auto allocations = counter.count();

            info ("    heap allocations during 200 blocks of Size and balance"
                  " automation: " + std::to_string (allocations));

            check (allocations == 0,
                   "neither a Size change nor a balance change allocates");
        }
        else
        {
            info ("    Allocation counting is not available in this build"
                  " (sanitizer), so this is NOT MEASURED rather than passed.");
        }
    }
}

} // namespace avtest
