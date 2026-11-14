#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 9 - STAGE 2. Research prototypes and control-independence
    validation. No production parameter, no shipping DSP path change.

    Stage 1 reduced the roadmap from four controls to two, one of them
    conditional. Stage 2 decides whether Phase 9 ships Late Diffusion AND
    Attack, or Attack alone.

    THE STAGE-1 BASELINES ARE CARRIED FORWARD AND RE-MEASURED, not quoted.
    A number that does not exceed what the estimator scatters by is not a
    finding, and Stage 1's worst mistake was reporting 0.0066 against a floor
    of 0.077. Both corrections from Stage 1 are preserved: near-silent windows
    are refused rather than printed, and periodicity is passed its window RAW
    because maxEnvelopePeriodicity detrends internally.

    LATE DIFFUSION'S HARD GATE is that it must work at Mod Depth 0. The
    mechanism Stage 1 found - more in-loop allpass sections - is disabled
    there, because AllpassPhaseBank is a modulation engine and Phase 5B gave
    it a true bypass at Depth 0. The research path added for this stage claims
    the bank's already-allocated SPARE section and drives it statically with
    its own engagement weight, so modulation owns section 0 and diffusion owns
    section 1 and neither depends on the other.

    ATTACK'S HARD GATE is that it must work in a continuously running reverb.
    A mechanism that only looks right on the first impulse after reset is not
    an Attack control, it is a fade-in. That is tested with three impulses and
    no reset between them.
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
    constexpr double kSize           = 0.60;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kHighDamp       = 0.45;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDampHz     = 10000.0;

    // Carried from Stage 1, and re-measured in the first suite below.
    constexpr double kStage1NedFloor    = 0.077;
    constexpr double kStage1PeriodFloor = 3.03;

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

    struct Config
    {
        double decay          = kDecay;
        double size           = kSize;
        double earlyDiffusion = kEarlyDiffusion;
        double modDepth       = kModDepth;
        double highDamp       = kHighDamp;

        // PHASE 9 research: the static in-loop diffusion path.
        double staticWeight      = 0.0;
        double staticCoefficient = 0.0;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (c.earlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (c.highDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (c.modDepth),
                                      mapping::modRateScaleFor (kModRate),
                                      true);

        // The research path, after the candidate so it is not overwritten.
        tank->setStaticDiffusion (c.staticWeight, c.staticCoefficient, true);

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

    [[nodiscard]] std::vector<double> tankIr (Morph& tank, double seconds)
    {
        const auto length = static_cast<size_t> (kSampleRate * seconds);

        std::vector<double> ir (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;

            tank.processSample (l, r);

            ir[n] = l;
        }

        return ir;
    }

    void tankIrStereo (Morph& tank, double seconds,
                       std::vector<double>& left, std::vector<double>& right)
    {
        const auto length = static_cast<size_t> (kSampleRate * seconds);

        left .assign (length, 0.0);
        right.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;

            tank.processSample (l, r);

            left[n]  = l;
            right[n] = r;
        }
    }

    /** Runs an arbitrary drive signal through the tank, left channel out. */
    [[nodiscard]] std::vector<double> renderDrive (Morph& tank,
                                                   const std::vector<double>& drive,
                                                   size_t length)
    {
        std::vector<double> out (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = n < drive.size() ? drive[n] : 0.0;
            double r = l;

            tank.processSample (l, r);

            out[n] = l;
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  Measures, carried forward from Stage 1 with its corrections intact
    // -----------------------------------------------------------------------
    [[nodiscard]] double densityAt (const std::vector<double>& ir, double atMs,
                                    double windowMs = 30.0)
    {
        const auto from = static_cast<size_t> (atMs * kSampleRate / 1000.0);
        const auto len  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);

        if (from + len > ir.size())
            return -1.0;

        double peak = 0.0, sum = 0.0;

        for (double s : ir)
            peak = std::max (peak, std::abs (s));

        for (size_t n = from; n < from + len; ++n)
            sum += ir[n] * ir[n];

        const double rms = std::sqrt (sum / static_cast<double> (len));

        if (peak <= 0.0 || rms < peak * 1.0e-4)
            return -1.0;

        const auto flat = flattenDecay (ir.data() + from, len);

        return normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());
    }

    [[nodiscard]] std::string densityText (double value, int decimals = 4)
    {
        return value < 0.0 ? std::string ("--") : fx (value, decimals);
    }

    /** RAW window: maxEnvelopePeriodicity detrends internally. */
    [[nodiscard]] double periodicityIn (const std::vector<double>& ir,
                                        double fromMs, double toMs)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (ir.size(), static_cast<size_t> (toMs * kSampleRate / 1000.0));

        if (to <= from)
            return 0.0;

        return maxEnvelopePeriodicity (ir.data() + from, to - from,
                                       static_cast<size_t> (0.001 * kSampleRate),
                                       static_cast<size_t> (0.150 * kSampleRate));
    }

    [[nodiscard]] double kurtosisIn (const std::vector<double>& ir,
                                     double fromMs, double toMs)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (ir.size(), static_cast<size_t> (toMs * kSampleRate / 1000.0));

        if (to <= from)
            return 0.0;

        const auto flat = flattenDecay (ir.data() + from, to - from);

        return excessKurtosis (flat.data(), flat.size());
    }

    [[nodiscard]] std::vector<double> envelopeDb (const std::vector<double>& ir,
                                                  double windowMs, double stepMs,
                                                  double untilMs)
    {
        const auto win  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);
        const auto step = static_cast<size_t> (stepMs * kSampleRate / 1000.0);
        const auto last = static_cast<size_t> (untilMs * kSampleRate / 1000.0);

        std::vector<double> out;

        for (size_t from = 0; from + win <= std::min (last, ir.size()); from += step)
        {
            double sum = 0.0;

            for (size_t n = from; n < from + win; ++n)
                sum += ir[n] * ir[n];

            const double rms = std::sqrt (sum / static_cast<double> (win));

            out.push_back (rms > 0.0 ? 20.0 * std::log10 (rms) : -200.0);
        }

        return out;
    }

    /** RT60 proxy: envelope fall of 30 dB from the peak, scaled to 60. */
    [[nodiscard]] double rt60Proxy (const std::vector<double>& ir)
    {
        const auto env = envelopeDb (ir, 30.0, 10.0, 3500.0);

        if (env.empty())
            return 0.0;

        const auto peakAt = static_cast<size_t> (
            std::max_element (env.begin(), env.end()) - env.begin());

        const double peak = env[peakAt];

        for (size_t i = peakAt; i < env.size(); ++i)
            if (env[i] <= peak - 30.0)
                return 2.0 * static_cast<double> (i - peakAt) * 0.010;

        return 0.0;
    }

    /*
        The two Attack numbers the brief asks for, kept separate on purpose.

        ONSET is the first reliable wet arrival: the first 2 ms window that
        reaches 40 dB below the eventual peak. Pre-Delay moves this. An Attack
        mechanism must not.

        BUILDUP is the time from that onset to the first window within 3 dB of
        the local established level. Attack must move this. Pre-Delay must
        not.
    */
    struct Buildup
    {
        double onsetMs = -1.0;
        double buildupMs = -1.0;
    };

    [[nodiscard]] Buildup buildupOf (const std::vector<double>& ir,
                                     double fromMs = 0.0, double untilMs = 600.0)
    {
        const auto env = envelopeDb (ir, 2.0, 1.0, untilMs);

        if (env.empty())
            return {};

        const auto skip = static_cast<size_t> (fromMs);

        if (skip >= env.size())
            return {};

        double peak = -1.0e30;

        for (size_t i = skip; i < env.size(); ++i)
            peak = std::max (peak, env[i]);

        Buildup b;

        for (size_t i = skip; i < env.size(); ++i)
            if (env[i] >= peak - 40.0)
            {
                b.onsetMs = static_cast<double> (i - skip);
                break;
            }

        if (b.onsetMs < 0.0)
            return b;

        for (size_t i = skip + static_cast<size_t> (b.onsetMs); i < env.size(); ++i)
            if (env[i] >= peak - 3.0)
            {
                b.buildupMs = static_cast<double> (i - skip) - b.onsetMs;
                break;
            }

        return b;
    }

    // -----------------------------------------------------------------------
    //  Attack prototypes. Test-only; nothing here touches production.
    // -----------------------------------------------------------------------
    /**
        AT-A, the control architecture: a one-pole rise on the wet output,
        started once. Included because it is the cheapest possible Attack
        signature and because the brief requires it to be measured rather
        than dismissed.
    */
    [[nodiscard]] std::vector<double> applyWetEnvelope (std::vector<double> wet,
                                                        double attackMs)
    {
        if (attackMs <= 0.0)
            return wet;

        const double a = std::exp (-1.0 / (attackMs * kSampleRate / 1000.0));
        double state = 0.0;

        for (auto& s : wet)
        {
            state = a * state + (1.0 - a);
            s *= state;
        }

        return wet;
    }

    /**
        AT-B, feedforward staggered injection.

        A fixed FIR applied to the signal entering the tank: one immediate tap
        plus a rising spread of later taps over `attackMs`. Linear and time
        invariant, so it behaves identically on the first transient and the
        hundredth, needs no onset detector, knows nothing about transport, and
        sits entirely outside the feedback recurrence.

        Taps are L2-normalised. The tank is linear and the taps are far enough
        apart to add incoherently in the late field, so unit L2 keeps the
        established late energy approximately fixed while moving when the
        energy arrives - which is the thing being controlled. The immediate
        tap keeps a share of the energy so the wet onset stays present rather
        than being delayed, which is what separates this from Pre-Delay.
    */
    [[nodiscard]] std::vector<double> staggeredInjection (const std::vector<double>& drive,
                                                          double attackMs,
                                                          double immediateShare = 0.25)
    {
        if (attackMs <= 0.0)
            return drive;

        constexpr int taps = 24;

        const double spacing = attackMs * kSampleRate / 1000.0 / taps;

        std::vector<double> weight (taps, 0.0);
        std::vector<int> offset (taps, 0);

        // p[0] carries the immediate share; the rest rises linearly, so the
        // field grows rather than arriving all at once somewhere later.
        double rest = 0.0;

        for (int k = 1; k < taps; ++k)
            rest += static_cast<double> (k);

        for (int k = 0; k < taps; ++k)
        {
            const double power = (k == 0)
                               ? immediateShare
                               : (1.0 - immediateShare) * static_cast<double> (k) / rest;

            weight[static_cast<size_t> (k)] = std::sqrt (power);
            offset[static_cast<size_t> (k)] = static_cast<int> (std::lround (k * spacing));
        }

        std::vector<double> out (drive.size(), 0.0);

        for (size_t n = 0; n < drive.size(); ++n)
        {
            if (drive[n] == 0.0)
                continue;

            for (int k = 0; k < taps; ++k)
            {
                const size_t at = n + static_cast<size_t> (offset[static_cast<size_t> (k)]);

                if (at < out.size())
                    out[at] += drive[n] * weight[static_cast<size_t> (k)];
            }
        }

        return out;
    }

    /** Integrated energy, for the normalisation findings. */
    [[nodiscard]] double energyOf (const std::vector<double>& x)
    {
        double sum = 0.0;

        for (double s : x)
            sum += s * s;

        return sum;
    }

    [[nodiscard]] double peakOf (const std::vector<double>& x)
    {
        double p = 0.0;

        for (double s : x)
            p = std::max (p, std::abs (s));

        return p;
    }

    /** Three impulses, 500 ms apart, one continuous render. */
    [[nodiscard]] std::vector<double> threeImpulseDrive (size_t length)
    {
        std::vector<double> drive (length, 0.0);

        for (int k = 0; k < 3; ++k)
        {
            const auto at = static_cast<size_t> (k * 0.5 * kSampleRate);

            if (at < length)
                drive[at] = 1.0;
        }

        return drive;
    }
}

void runPhase9Stage2Tests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - the baselines, re-measured not quoted");
    // =======================================================================
    {
        info ("  Stage 1's floors are carried forward, but they are re-measured");
        info ("  here rather than trusted: this file renders with a different");
        info ("  helper and a different tank lifetime, and a floor is a property");
        info ("  of the measurement as it is actually performed.");
        info ("");

        auto tank = makeTank (Config {});
        const auto ir = tankIr (*tank, 3.5);

        double nedMin = 1.0e30, nedMax = -1.0e30;
        double perMin = 1.0e30, perMax = -1.0e30;
        double onsetMin = 1.0e30, onsetMax = -1.0e30;
        double buildMin = 1.0e30, buildMax = -1.0e30;

        for (int step = 0; step <= 10; ++step)
        {
            const double at = 300.0 + 20.0 * step;

            const double ned = densityAt (ir, at);
            const double per = periodicityIn (ir, at, at + 600.0);

            if (ned >= 0.0)
            {
                nedMin = std::min (nedMin, ned);
                nedMax = std::max (nedMax, ned);
            }

            perMin = std::min (perMin, per);
            perMax = std::max (perMax, per);
        }

        /*
            The Attack metrics need their own floor, and it cannot be found by
            sliding a window: onset and buildup are landmarks of one event, so
            moving the analysis window just moves the answer with it. The
            scatter that matters is across REALISATIONS - the same
            configuration driven at different moments in the tank's modulation
            cycle, which is what a real transient meets.
        */
        for (int k = 0; k < 8; ++k)
        {
            auto t = makeTank (Config {});

            const auto length = static_cast<size_t> (kSampleRate * 1.5);
            const auto offset = static_cast<size_t> (k * 0.037 * kSampleRate);

            std::vector<double> drive (length, 0.0);

            if (offset < length)
                drive[offset] = 1.0;

            const auto out = renderDrive (*t, drive, length);

            const auto b = buildupOf (out, static_cast<double> (offset) * 1000.0 / kSampleRate);

            if (b.onsetMs >= 0.0)
            {
                onsetMin = std::min (onsetMin, b.onsetMs);
                onsetMax = std::max (onsetMax, b.onsetMs);
            }

            if (b.buildupMs >= 0.0)
            {
                buildMin = std::min (buildMin, b.buildupMs);
                buildMax = std::max (buildMax, b.buildupMs);
            }
        }

        info ("    " + pad ("measure", 22) + pad ("spread", 12) + pad ("range", 22)
              + "Stage 1");
        info ("    " + pad ("echo density", 22) + pad (fx (nedMax - nedMin, 4), 12)
              + pad (fx (nedMin, 3) + " to " + fx (nedMax, 3), 22)
              + fx (kStage1NedFloor, 3));
        info ("    " + pad ("envelope periodicity", 22) + pad (fx (perMax - perMin, 4), 12)
              + pad (fx (perMin, 2) + " to " + fx (perMax, 2), 22)
              + fx (kStage1PeriodFloor, 2));
        info ("    " + pad ("Attack onset", 22) + pad (fx (onsetMax - onsetMin, 1) + " ms", 12)
              + pad (fx (onsetMin, 0) + " to " + fx (onsetMax, 0) + " ms", 22) + "new");
        info ("    " + pad ("Attack buildup", 22) + pad (fx (buildMax - buildMin, 1) + " ms", 12)
              + pad (fx (buildMin, 0) + " to " + fx (buildMax, 0) + " ms", 22) + "new");
        info ("");
        info ("    The onset and buildup floors come from eight realisations at");
        info ("    different moments in the modulation cycle, not from sliding a");
        info ("    window: onset and buildup are landmarks of one event, so");
        info ("    moving the window moves the answer with it and would report");
        info ("    a floor of zero.");

        check (nedMax > nedMin, "the density floor is re-established in this file");
        check (buildMax >= buildMin, "the Attack buildup floor is established");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - LD backward compatibility, the hard gate");
    // =======================================================================
    {
        /*
            The gate the brief calls hard, and it is the first thing to check,
            because a candidate that cannot reproduce PHASE 8 at its neutral
            value is finished regardless of how well it diffuses.

            Bit identity is the requirement, not a tolerance. The research
            path's weight is zero at the neutral position, which means the
            static section is skipped, no smoother reaches the signal, and the
            arithmetic is the same arithmetic. If that is true the two renders
            are identical sample for sample, and if it is not true then
            "approximately PHASE 8" is a different product.
        */
        info ("  At the research neutral - static weight 0 - the tank must be");
        info ("  bit-identical to PHASE 8, across Mod Depth, Size, Decay and");
        info ("  damping. Not close: identical. The static section is skipped");
        info ("  entirely at weight 0, so anything other than 0 difference");
        info ("  would mean the path leaks.");
        info ("");
        info ("    " + pad ("Mod Depth", 11) + pad ("Size", 8) + pad ("Decay", 9)
              + pad ("High Damp", 11) + pad ("samples differ", 16) + "worst diff");

        long long worstDiffering = 0;
        double worstDifference = 0.0;

        struct Case { double mod, size, decay, damp; };

        const Case cases[]
        {
            { 0.00, 0.60, 2.5, 0.45 },
            { 0.30, 0.60, 2.5, 0.45 },
            { 1.00, 0.60, 2.5, 0.45 },
            { 0.00, 0.20, 1.0, 0.00 },
            { 0.30, 1.00, 6.0, 1.00 },
            { 1.00, 0.20, 6.0, 0.45 },
        };

        for (const auto& c : cases)
        {
            Config legacy;
            legacy.modDepth = c.mod;
            legacy.size = c.size;
            legacy.decay = c.decay;
            legacy.highDamp = c.damp;

            // The legacy render never calls the research setter at all.
            auto a = std::make_unique<Morph>();
            {
                a->prepare (kSampleRate);
                a->setInputDiffusionEnabled (true);
                a->setCompensationEnabled (true, true);
                a->setRt60Seconds (mapping::rt60For (c.decay), true);
                a->setInputDiffusion (kEarlyDiffusion, true);
                a->setSize (mapping::sizeScaleFor (c.size));
                a->setDamping (mapping::lowDampRatioFor (kLowDamp),
                               mapping::highDampRatioFor (c.damp),
                               kLowDampHz, kHighDampHz, true);
                architecture::applyWithDepth (*a, architecture::defaultCandidate,
                                              mapping::modDepthFor (c.mod),
                                              mapping::modRateScaleFor (kModRate), true);

                for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
                {
                    double l = 0.0, r = 0.0;
                    a->processSample (l, r);
                }

                for (int bank = 0; bank < 2; ++bank)
                    a->getBank (bank).getCore().resetModulation();

                a->reset();
            }

            const auto legacyIr = tankIr (*a, 1.0);

            // The research render calls it, at zero.
            auto b = makeTank (legacy);
            const auto researchIr = tankIr (*b, 1.0);

            long long differing = 0;
            double worst = 0.0;

            for (size_t n = 0; n < legacyIr.size(); ++n)
            {
                const double d = std::abs (legacyIr[n] - researchIr[n]);

                worst = std::max (worst, d);

                if (d != 0.0)
                    ++differing;
            }

            worstDiffering = std::max (worstDiffering, differing);
            worstDifference = std::max (worstDifference, worst);

            info ("    " + pad (fx (c.mod * 100.0, 0) + " %", 11)
                  + pad (fx (c.size * 100.0, 0) + " %", 8)
                  + pad (fx (c.decay, 1) + " s", 9)
                  + pad (fx (c.damp * 100.0, 0) + " %", 11)
                  + pad (std::to_string (differing) + " of "
                         + std::to_string (legacyIr.size()), 16)
                  + fx (worst, 14));
        }

        info ("");
        info ("    worst difference  " + fx (worstDifference, 16));

        checkEqual (worstDifference, 0.0,
                    "at the research neutral the tank is bit-identical to PHASE 8");

        checkEqual (static_cast<double> (worstDiffering), 0.0,
                    "and not one sample differs, in any of the six configurations");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - LD-A and LD-C: does it work at Mod Depth 0");
    // =======================================================================
    {
        /*
            LD-A was "add a coefficient setter to the existing bank". LD-C was
            "an independent static element". In this architecture they turn
            out to be the same prototype, and that is a finding rather than a
            shortcut: the bank already owns a second, allocated, unused
            section per line, so an independent static element does not need
            new memory or a new delay line - it needs the spare section driven
            by its own weight instead of by modulation depth.

            LD-A as literally specified - a coefficient setter on the
            modulation sections - fails gate 1 by construction and without any
            measurement, because `bypassed` is true whenever depth is 0 and
            process() returns before a coefficient can matter. Measuring it
            would only confirm the code. So the prototype tested is the
            decoupled one, which is LD-B's requirement met by LD-C's
            mechanism.
        */
        info ("  Static in-loop diffusion against Mod Depth. The gate is that");
        info ("  the periodicity span must survive at Mod Depth 0, where the");
        info ("  modulation bank is fully bypassed.");
        info ("");
        info ("    " + pad ("Mod Depth", 11) + pad ("LD weight", 11) + pad ("coeff", 8)
              + pad ("periodicity", 13) + pad ("NED late", 11)
              + pad ("kurt late", 11) + "RT60 proxy");

        struct Setting { const char* label; double weight, coefficient; };

        // Minimum / low / middle / high / maximum, as the grid requires.
        const Setting settings[]
        {
            { "min",  0.0,  0.00 },
            { "low",  1.0,  0.20 },
            { "mid",  1.0,  0.35 },
            { "high", 1.0,  0.50 },
            { "max",  1.0,  0.60 },
        };

        struct Result { double period, ned, kurt, rt; };

        std::vector<std::vector<Result>> grid;

        for (double mod : { 0.0, 0.30, 1.00 })
        {
            std::vector<Result> row;

            for (const auto& s : settings)
            {
                Config c;
                c.modDepth = mod;
                c.staticWeight = s.weight;
                c.staticCoefficient = s.coefficient;

                auto tank = makeTank (c);
                const auto ir = tankIr (*tank, 3.5);

                const Result r { periodicityIn (ir, 300.0, 900.0),
                                 densityAt (ir, 320.0),
                                 kurtosisIn (ir, 300.0, 900.0),
                                 rt60Proxy (ir) };

                row.push_back (r);

                info ("    " + pad (fx (mod * 100.0, 0) + " %", 11)
                      + pad (s.label, 11) + pad (fx (s.coefficient, 2), 8)
                      + pad (fx (r.period, 3), 13)
                      + pad (densityText (r.ned), 11)
                      + pad (fx (r.kurt, 3), 11)
                      + fx (r.rt, 3) + " s");
            }

            grid.push_back (row);

            info ("");
        }

        // --- the matrix, as spans --------------------------------------------
        info ("    " + pad ("Mod Depth", 11) + pad ("periodicity span", 19)
              + pad ("vs floor", 11) + pad ("NED span", 11) + pad ("vs floor", 11)
              + "RT60 drift");

        double worstPeriodSpan = 1.0e30;
        double worstNedSpan = 0.0;
        double worstRtDrift = 0.0;

        const char* names[] { "0 %", "30 %", "100 %" };

        for (size_t m = 0; m < grid.size(); ++m)
        {
            double perMin = 1.0e30, perMax = -1.0e30;
            double nedMin = 1.0e30, nedMax = -1.0e30;
            double rtMin = 1.0e30, rtMax = -1.0e30;

            for (const auto& r : grid[m])
            {
                perMin = std::min (perMin, r.period);
                perMax = std::max (perMax, r.period);

                if (r.ned >= 0.0)
                {
                    nedMin = std::min (nedMin, r.ned);
                    nedMax = std::max (nedMax, r.ned);
                }

                rtMin = std::min (rtMin, r.rt);
                rtMax = std::max (rtMax, r.rt);
            }

            const double perSpan = perMax - perMin;
            const double nedSpan = nedMax - nedMin;
            const double rtDrift = rtMax > 0.0 ? 100.0 * (rtMax - rtMin) / rtMax : 0.0;

            worstPeriodSpan = std::min (worstPeriodSpan, perSpan);
            worstNedSpan = std::max (worstNedSpan, nedSpan);
            worstRtDrift = std::max (worstRtDrift, rtDrift);

            info ("    " + pad (names[m], 11) + pad (fx (perSpan, 3), 19)
                  + pad (fx (perSpan / kStage1PeriodFloor, 2) + "x", 11)
                  + pad (fx (nedSpan, 4), 11)
                  + pad (fx (nedSpan / kStage1NedFloor, 2) + "x", 11)
                  + fx (rtDrift, 2) + " %");
        }

        info ("");
        info ("    smallest periodicity span across the three Mod Depths  "
              + fx (worstPeriodSpan, 3));
        info ("    largest NED span                                       "
              + fx (worstNedSpan, 4));
        info ("    largest RT60 proxy drift                               "
              + fx (worstRtDrift, 2) + " %");
        info ("");
        info ("    THE DESIRED SIGNATURE is a periodicity span well above the");
        info ("    3.03 floor at every Mod Depth, with the NED span inside the");
        info ("    0.077 floor. That is what would make Late Diffusion a");
        info ("    different control from Density rather than a second name");
        info ("    for it.");

        /*
            The grid above shows something the coarse five-point sweep could
            report but not explain: at Mod Depth 0 the periodicity does not
            fall monotonically with the coefficient. It rises steeply first.

            That matters more than the span does. A control whose effect
            reverses direction partway through its own travel is not a
            control. So the coefficient is swept finely at Mod Depth 0, where
            the reversal is largest, to find out whether any sub-range is
            monotone and whether it stays below the neutral value.

            The mechanism for the rise is not mysterious: the static section
            is a fixed 1-2 ms delay with coefficient g, and at small g an
            allpass is very nearly a pure delay. A pure delay added to a
            feedback loop is an extra repeat, not a disperser - so it ADDS
            periodic structure. Only at large g does the section disperse
            enough to remove more than it adds.
        */
        info ("");
        info ("  The reversal, swept finely at Mod Depth 0 where it is largest.");
        info ("  Neutral - the static section disengaged - is the row marked");
        info ("  'off', and it is the value a user leaving the control alone");
        info ("  would hear.");
        info ("");
        info ("    " + pad ("coefficient", 14) + pad ("periodicity", 14)
              + pad ("vs neutral", 13) + "direction");

        double neutralPeriod = 0.0;

        {
            Config c;
            c.modDepth = 0.0;

            auto tank = makeTank (c);
            neutralPeriod = periodicityIn (tankIr (*tank, 3.5), 300.0, 900.0);

            info ("    " + pad ("off", 14) + pad (fx (neutralPeriod, 3), 14)
                  + pad ("-", 13) + "neutral");
        }

        double previous = -1.0;
        int reversals = 0;
        int lastSign = 0;
        double bestBelowNeutral = 1.0e30;
        double worstAboveNeutral = 0.0;

        for (double g : { 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80 })
        {
            Config c;
            c.modDepth = 0.0;
            c.staticWeight = 1.0;
            c.staticCoefficient = g;

            auto tank = makeTank (c);
            const double per = periodicityIn (tankIr (*tank, 3.5), 300.0, 900.0);

            int sign = 0;

            if (previous >= 0.0)
            {
                sign = per > previous ? 1 : -1;

                if (lastSign != 0 && sign != lastSign)
                    ++reversals;

                lastSign = sign;
            }

            if (per < neutralPeriod)
                bestBelowNeutral = std::min (bestBelowNeutral, per);
            else
                worstAboveNeutral = std::max (worstAboveNeutral, per - neutralPeriod);

            info ("    " + pad (fx (g, 2), 14) + pad (fx (per, 3), 14)
                  + pad (fx (per - neutralPeriod, 3), 13)
                  + (previous < 0.0 ? "-" : (sign > 0 ? "rising" : "falling")));

            previous = per;
        }

        info ("");
        info ("    direction reversals across the sweep       " + std::to_string (reversals));
        info ("    worst excursion ABOVE neutral              "
              + fx (worstAboveNeutral, 3));
        info ("    lowest value reached below neutral         "
              + (bestBelowNeutral < 1.0e29 ? fx (bestBelowNeutral, 3) : std::string ("none")));

        info ("");
        info ("    If the excursion above neutral exceeds the 3.03 floor, then");
        info ("    turning a Late Diffusion control up from zero makes the tail");
        info ("    MORE periodic before it makes it less - at Mod Depth 0, which");
        info ("    is precisely the case this mechanism exists to serve.");

        check (worstPeriodSpan > 0.0, "the Late Diffusion grid was measured at every Mod Depth");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - LD stereo, coloration and CPU");
    // =======================================================================
    {
        info ("  A diffusion mechanism that collapses the image, colours the");
        info ("  spectrum or costs more than it is worth fails on any one of");
        info ("  those alone.");
        info ("");
        info ("    " + pad ("LD setting", 12) + pad ("L/R corr", 11)
              + pad ("mono/stereo", 13) + pad ("peak", 10)
              + pad ("HF/LF tilt", 12) + "spectral drift");

        /*
            Coloration: an allpass is magnitude-flat, so in principle a
            diffusion control cannot colour anything. In a FEEDBACK path that
            reasoning does not hold - the loop applies the section once per
            circulation and the per-line decay gain is recomputed from the
            effective loop length - so it is measured rather than argued. The
            tilt is high-band over low-band energy across the whole response,
            split at 2 kHz, reported against the neutral setting.
        */
        const auto tiltDb = [] (const std::vector<double>& ir)
        {
            const double a = std::exp (-2.0 * 3.14159265358979323846 * 2000.0 / kSampleRate);

            double state = 0.0, low = 0.0, high = 0.0;

            for (double s : ir)
            {
                state = a * state + (1.0 - a) * s;

                const double hi = s - state;

                low  += state * state;
                high += hi * hi;
            }

            return (low > 0.0 && high > 0.0) ? 10.0 * std::log10 (high / low) : 0.0;
        };

        struct Setting { const char* label; double weight, coefficient; };

        const Setting settings[]
        {
            { "min",  0.0, 0.00 },
            { "mid",  1.0, 0.35 },
            { "max",  1.0, 0.60 },
        };

        double neutralTilt = 0.0;
        bool first = true;
        double worstTiltDrift = 0.0;
        double worstCorrelation = 0.0;

        for (const auto& s : settings)
        {
            Config c;
            c.staticWeight = s.weight;
            c.staticCoefficient = s.coefficient;

            auto tank = makeTank (c);

            std::vector<double> l, r;
            tankIrStereo (*tank, 2.0, l, r);

            const auto from = static_cast<size_t> (0.300 * kSampleRate);
            const auto to   = std::min (l.size(), static_cast<size_t> (0.900 * kSampleRate));

            const double corr = correlation (l.data() + from, r.data() + from, to - from);

            double stereoEnergy = 0.0, monoEnergy = 0.0;

            for (size_t n = from; n < to; ++n)
            {
                stereoEnergy += l[n] * l[n] + r[n] * r[n];

                const double mono = 0.5 * (l[n] + r[n]);
                monoEnergy += 2.0 * mono * mono;
            }

            const double ratio = stereoEnergy > 0.0
                               ? 10.0 * std::log10 (monoEnergy / stereoEnergy) : 0.0;

            const double tilt = tiltDb (l);

            if (first)
            {
                neutralTilt = tilt;
                first = false;
            }

            worstTiltDrift = std::max (worstTiltDrift, std::abs (tilt - neutralTilt));
            worstCorrelation = std::max (worstCorrelation, std::abs (corr));

            info ("    " + pad (s.label, 12) + pad (fx (corr, 4), 11)
                  + pad (fx (ratio, 2) + " dB", 13)
                  + pad (fx (peakOf (l), 4), 10)
                  + pad (fx (tilt, 2) + " dB", 12)
                  + fx (tilt - neutralTilt, 2) + " dB");
        }

        info ("");
        info ("    worst |L/R correlation|   " + fx (worstCorrelation, 4));
        info ("    worst spectral drift      " + fx (worstTiltDrift, 2) + " dB");

        // --- CPU, min of repeats against the same-run tank ------------------
        info ("");
        info ("  CPU, measured against the tank in the same run. Minimum of");
        info ("  five repeats: a single timing of a short render is dominated");
        info ("  by scheduling, and Stage 2 of PHASE 8 learned that the hard");
        info ("  way by reporting a bypassed stage as slower than an engaged");
        info ("  one.");
        info ("");

        constexpr int repeats = 5;
        const int length = 48000;

        std::vector<double> drive (static_cast<size_t> (length), 0.0);

        for (int i = 0; i < length; ++i)
            drive[static_cast<size_t> (i)] = 0.25 * std::sin (0.013 * i);

        const auto timeTank = [&] (const Config& c)
        {
            auto tank = makeTank (c);

            double bestNs = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                std::vector<double> l = drive, r = drive;

                const auto start = std::chrono::steady_clock::now();

                for (int n = 0; n < length; ++n)
                    tank->processSample (l[static_cast<size_t> (n)],
                                         r[static_cast<size_t> (n)]);

                const auto finish = std::chrono::steady_clock::now();

                const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>
                                    (finish - start).count();

                bestNs = std::min (bestNs, static_cast<double> (ns));
            }

            return bestNs / static_cast<double> (length);
        };

        Config neutral;
        Config engaged; engaged.staticWeight = 1.0; engaged.staticCoefficient = 0.35;

        const double neutralNs = timeTank (neutral);
        const double engagedNs = timeTank (engaged);

        info ("    " + pad ("configuration", 24) + pad ("ns/sample", 13) + "vs neutral");
        info ("    " + pad ("LD neutral (PHASE 8)", 24) + pad (fx (neutralNs, 3), 13) + "-");
        info ("    " + pad ("LD engaged, coeff 0.35", 24) + pad (fx (engagedNs, 3), 13)
              + "+" + fx (100.0 * (engagedNs - neutralNs) / neutralNs, 2) + " %");
        info ("");
        info ("    Added state: one LinearSmoother per line, 8 doubles, plus");
        info ("    two scalars. Added delay memory: NONE - section 1 is already");
        info ("    allocated and prepared by PHASE 5B and merely unused while");
        info ("    sectionsPerLine is 1. Added per-sample cost at neutral: one");
        info ("    smoother advance and one compare, inside a branch that was");
        info ("    already there.");

        check (engagedNs > 0.0 && neutralNs > 0.0, "the Late Diffusion CPU cost was measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - Attack in a continuously running reverb");
    // =======================================================================
    {
        /*
            The mandatory test, and the one that decides between the
            candidates. Three impulses 500 ms apart in ONE continuous render,
            no reset between them. A mechanism whose Attack signature appears
            only on the first event is a fade-in, not a control.
        */
        /*
            THE FIRST VERSION OF THIS SUITE DID NOT WORK, and the way it
            failed is worth keeping.

            It measured onset and buildup per event, in a window starting at
            each impulse. For events 2 and 3 it reported onset 0 ms - because
            the window opens in the middle of the previous event's tail, which
            is already far above the -40 dB onset threshold. The landmark
            metric has no way to tell a new event from the decay it arrives
            on top of, so per-event landmarks in a continuous render are not a
            measurement at all.

            What CAN be measured, and is the right question anyway:
            SUPERPOSITION. If a mechanism treats every event identically then
            the response to three impulses is exactly the sum of three shifted
            copies of the response to one. That is the definition of behaving
            the same on the hundredth transient as on the first, it needs no
            event detection, and it is a bit-level test rather than a
            landmark.

            A time-varying mechanism fails it. That is the whole distinction
            between the two candidates.
        */
        info ("  Three impulses, 500 ms apart, one continuous render, no reset.");
        info ("");
        info ("  Not measured by per-event landmarks: in a continuous render a");
        info ("  window opening at event 2 opens inside event 1's tail, which");
        info ("  is already above any onset threshold, so the landmark reports");
        info ("  0 ms and means nothing. The first version of this suite did");
        info ("  exactly that.");
        info ("");
        info ("  Measured by SUPERPOSITION instead. If a mechanism treats every");
        info ("  event alike then three impulses produce exactly the sum of");
        info ("  three shifted single-impulse responses. No event detection, no");
        info ("  landmark, and a time-varying mechanism cannot pass it.");
        info ("");
        info ("  RUN AT MOD DEPTH 0, and that is not a convenience. The second");
        info ("  version of this suite ran it at the shipping Mod Depth 30 % and");
        info ("  the BASELINE failed at 9.95 % - because a modulated tank is");
        info ("  time-varying by design, so three impulses meet it at three");
        info ("  different modulation phases and superposition does not hold for");
        info ("  the tank either. The test would have been measuring PHASE 5's");
        info ("  accepted character and blaming the Attack mechanism for it.");
        info ("  At Mod Depth 0 the tank is time invariant to 1.3e-25 (Stage 1),");
        info ("  so whatever residual remains belongs to the mechanism.");
        info ("");
        info ("    " + pad ("mechanism", 28) + pad ("residual", 14)
              + pad ("rel. to peak", 15) + "verdict");

        const auto length = static_cast<size_t> (kSampleRate * 1.6);
        const auto drive = threeImpulseDrive (length);
        const auto step  = static_cast<size_t> (0.5 * kSampleRate);

        std::vector<double> single (length, 0.0);
        single[0] = 1.0;

        const auto superposition = [&] (const std::string& label,
                                        const std::vector<double>& three,
                                        const std::vector<double>& one)
        {
            std::vector<double> predicted (length, 0.0);

            for (int k = 0; k < 3; ++k)
            {
                const size_t shift = static_cast<size_t> (k) * step;

                for (size_t n = 0; n + shift < length; ++n)
                    predicted[n + shift] += one[n];
            }

            double residual = 0.0, peak = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                residual = std::max (residual, std::abs (three[n] - predicted[n]));
                peak = std::max (peak, std::abs (predicted[n]));
            }

            const double relative = peak > 0.0 ? residual / peak : 0.0;

            info ("    " + pad (label, 28) + pad (fx (residual, 10), 14)
                  + pad (fx (100.0 * relative, 4) + " %", 15)
                  + (relative < 1.0e-9 ? "identical on every event"
                                       : "EVENT-DEPENDENT"));

            return relative;
        };

        Config lti;
        lti.modDepth = 0.0;

        // --- baseline, and the same baseline WITH modulation, so the
        //     difference between them is visible rather than asserted ------
        {
            auto t1 = makeTank (lti);
            auto t2 = makeTank (lti);

            superposition ("baseline, Mod Depth 0",
                           renderDrive (*t1, drive, length),
                           renderDrive (*t2, single, length));

            auto t3 = makeTank (Config {});
            auto t4 = makeTank (Config {});

            superposition ("baseline, Mod Depth 30 %",
                           renderDrive (*t3, drive, length),
                           renderDrive (*t4, single, length));
        }

        // --- AT-A: wet envelope -------------------------------------------
        double atARelative = 0.0;
        {
            auto t1 = makeTank (lti);
            auto t2 = makeTank (lti);

            atARelative = superposition ("AT-A: wet envelope 150 ms",
                                         applyWetEnvelope (renderDrive (*t1, drive, length), 150.0),
                                         applyWetEnvelope (renderDrive (*t2, single, length), 150.0));
        }

        // --- AT-B: staggered injection ------------------------------------
        double atBRelative = 0.0;
        {
            auto t1 = makeTank (lti);
            auto t2 = makeTank (lti);

            atBRelative = superposition ("AT-B: staggered 150 ms",
                                         renderDrive (*t1, staggeredInjection (drive, 150.0), length),
                                         renderDrive (*t2, staggeredInjection (single, 150.0), length));
        }

        info ("");

        // What AT-A's envelope is actually worth at each event, in one line.
        info ("  And the reason AT-A fails, stated as a number rather than as");
        info ("  an argument. Its envelope is one global ramp started once, so");
        info ("  its gain at each event is:");
        info ("");

        for (int k = 0; k < 3; ++k)
        {
            const double t = k * 500.0;
            const double gain = 1.0 - std::exp (-t / 150.0);

            info ("    event " + std::to_string (k + 1) + " at " + fx (t, 0)
                  + " ms   envelope gain " + fx (gain, 4));
        }

        info ("");
        info ("    By the second impulse the ramp is 96 % open, so there is no");
        info ("    attack left to shape. Making it work per event needs a");
        info ("    retrigger, which needs an onset detector, which the brief");
        info ("    asks us not to introduce. AT-B is a fixed FIR on the signal");
        info ("    entering the tank - linear and time invariant - so it is");
        info ("    identical on every event by construction rather than by luck.");

        check (atBRelative <= 1.0e-9,
               "AT-B is exactly superposable: every event gets the same Attack");

        check (atARelative > 1.0e-6,
               "AT-A is not: its Attack applies to whichever event comes first");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - Attack against Pre-Delay and Early Diffusion");
    // =======================================================================
    {
        info ("  ONSET must belong to Pre-Delay and BUILDUP to Attack. Early");
        info ("  Diffusion must keep owning early scattering without owning");
        info ("  the buildup, or Attack is a second Early Diffusion.");
        info ("");
        info ("    " + pad ("configuration", 28) + pad ("onset", 10)
              + pad ("buildup", 11) + pad ("NED 20ms", 11)
              + pad ("energy", 12) + "peak");

        const auto length = static_cast<size_t> (kSampleRate * 2.0);

        const auto report = [&] (const std::string& label,
                                 const std::vector<double>& out,
                                 double onsetOffsetMs = 0.0)
        {
            const auto b = buildupOf (out, onsetOffsetMs);

            info ("    " + pad (label, 28)
                  + pad (b.onsetMs >= 0.0 ? fx (b.onsetMs, 0) + " ms" : "--", 10)
                  + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                  + pad (densityText (densityAt (out, 20.0 + onsetOffsetMs)), 11)
                  + pad (fx (energyOf (out), 6), 12)
                  + fx (peakOf (out), 5));

            return b;
        };

        std::vector<double> impulse (length, 0.0);
        impulse[0] = 1.0;

        Buildup baseline, preDelayed, attacked, diffused;

        {
            auto tank = makeTank (Config {});
            baseline = report ("baseline", renderDrive (*tank, impulse, length));
        }

        // --- Pre-Delay 50 ms, the production control -----------------------
        {
            auto tank = makeTank (Config {});

            std::vector<double> l = impulse, r = impulse;

            PreDelay<double> pd;
            pd.prepare (kSampleRate);
            pd.snapTo (50.0);
            pd.process (l.data(), r.data(), static_cast<int> (length));

            preDelayed = report ("Pre-Delay 50 ms", renderDrive (*tank, l, length));
        }

        // --- AT-B at two strengths -----------------------------------------
        for (double attackMs : { 50.0, 150.0 })
        {
            auto tank = makeTank (Config {});
            const auto spread = staggeredInjection (impulse, attackMs);

            const auto b = report ("AT-B staggered " + fx (attackMs, 0) + " ms",
                                   renderDrive (*tank, spread, length));

            if (attackMs > 100.0)
                attacked = b;
        }

        // --- AT-A for comparison, first event only -------------------------
        {
            auto tank = makeTank (Config {});
            const auto wet = renderDrive (*tank, impulse, length);
            report ("AT-A envelope 150 ms", applyWetEnvelope (wet, 150.0));
        }

        // --- Early Diffusion, the control it must not duplicate ------------
        for (double amount : { 0.0, 1.0 })
        {
            Config c;
            c.earlyDiffusion = amount;

            auto tank = makeTank (c);

            const auto b = report ("Early Diffusion " + fx (amount * 100.0, 0) + " %",
                                   renderDrive (*tank, impulse, length));

            if (amount > 0.5)
                diffused = b;
        }

        info ("");
        info ("    " + pad ("comparison", 34) + pad ("onset change", 15) + "buildup change");

        const auto delta = [] (const Buildup& a, const Buildup& b)
        {
            return std::make_pair (b.onsetMs - a.onsetMs, b.buildupMs - a.buildupMs);
        };

        const auto pd = delta (baseline, preDelayed);
        const auto at = delta (baseline, attacked);
        const auto ed = delta (baseline, diffused);

        info ("    " + pad ("Pre-Delay 50 ms", 34)
              + pad (fx (pd.first, 0) + " ms", 15) + fx (pd.second, 0) + " ms");
        info ("    " + pad ("AT-B staggered 150 ms", 34)
              + pad (fx (at.first, 0) + " ms", 15) + fx (at.second, 0) + " ms");
        info ("    " + pad ("Early Diffusion 0 -> 100 %", 34)
              + pad (fx (ed.first, 0) + " ms", 15) + fx (ed.second, 0) + " ms");
        info ("");
        info ("    The signature that would justify Attack as its own control:");
        info ("    Pre-Delay moves onset and leaves buildup; AT-B moves buildup");
        info ("    and leaves onset; Early Diffusion does neither strongly");
        info ("    while still owning early density.");

        check (baseline.buildupMs >= 0.0, "the Attack independence comparison was measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 2 - Attack energy, and Decay safety");
    // =======================================================================
    {
        /*
            Two things that could make an Attack measurement a lie.

            The first is level. "Slower" and "quieter" produce similar
            short-window curves, so integrated energy and peak are reported
            next to the buildup, raw - and then an energy-matched diagnostic
            is reported separately so it is clear which is which.

            The second is the decay law. AT-B is a fixed FIR outside the
            recurrence and cannot touch it; that is a structural claim and it
            is checked anyway, because PHASE 8 Stage 2.5 showed that a
            structural claim stated too broadly is still wrong.
        */
        info ("  Raw against energy-matched, so that slower is not read as");
        info ("  quieter. And the RT60 proxy, because a mechanism outside the");
        info ("  recurrence should not move it at all.");
        info ("");
        info ("    " + pad ("configuration", 26) + pad ("buildup", 11)
              + pad ("energy", 12) + pad ("rel. energy", 13)
              + pad ("peak", 10) + "RT60 proxy");

        const auto length = static_cast<size_t> (kSampleRate * 3.5);

        std::vector<double> impulse (length, 0.0);
        impulse[0] = 1.0;

        double baselineEnergy = 0.0;
        double worstRt = 0.0, baselineRt = 0.0;
        bool first = true;

        for (double attackMs : { 0.0, 50.0, 150.0, 300.0 })
        {
            auto tank = makeTank (Config {});
            const auto spread = staggeredInjection (impulse, attackMs);
            const auto out = renderDrive (*tank, spread, length);

            const auto b = buildupOf (out);
            const double e = energyOf (out);
            const double rt = rt60Proxy (out);

            if (first)
            {
                baselineEnergy = e;
                baselineRt = rt;
                first = false;
            }

            worstRt = std::max (worstRt, std::abs (rt - baselineRt));

            info ("    " + pad (attackMs <= 0.0 ? std::string ("AT-B off")
                                                : "AT-B " + fx (attackMs, 0) + " ms", 26)
                  + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                  + pad (fx (e, 6), 12)
                  + pad (fx (10.0 * std::log10 (e / baselineEnergy), 2) + " dB", 13)
                  + pad (fx (peakOf (out), 5), 10)
                  + fx (rt, 3) + " s");
        }

        info ("");
        info ("    worst RT60 proxy change across the Attack range  "
              + fx (worstRt, 3) + " s");
        info ("");
        info ("    The relative-energy column is the honest one. AT-B's taps are");
        info ("    L2-normalised, so if the late field's energy is roughly");
        info ("    preserved then the buildup change is a change in WHEN energy");
        info ("    arrives, not in how much. A large negative number there would");
        info ("    mean the mechanism is partly a volume control and the buildup");
        info ("    measurement is partly reading that.");

        check (worstRt >= 0.0, "Attack's effect on the decay proxy was measured");
    }
}

} // namespace avtest
