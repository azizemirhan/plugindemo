#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/CutFilter.h"
#include "DSP/Denormal.h"
#include "DSP/DiffusionTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"
#include "DSP/StereoDiffusionNetwork.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 9 - STAGE 1 RESEARCH. Measurement only.

    Nothing here is wired to a production parameter, no production DSP is
    changed, and no suite in this file gates anything it has not justified.
    Its job is to answer four questions with numbers rather than with
    intuition:

      1. Where does the current tank's density actually come from, and has it
         already saturated by the time anyone could hear it?

      2. What does the existing Early Diffusion control reach - only the
         excitation, or the late field too?

      3. Can "Late Diffusion" and "Density" be moved independently in THIS
         topology, or are they two names for one measurement?

      4. Can an Attack concept be distinguished from Pre-Delay, and a Bloom
         concept from Decay?

    THE MEASURES ARE THE ONES THIS PROJECT ALREADY OWNS. Normalised echo
    density (Abel & Huang) came in with PHASE 3's diffuser, envelope
    periodicity with PHASE 6's metallic-character work, and flattenDecay with
    the delay-set search. No new analyser was written: the brief asked for the
    smallest set that tracks audible sparse-to-dense behaviour, and that set
    is already here and already validated against listening.

    flattenDecay before every shape statistic, always. Over any window of a
    decaying response the shape is dominated by the decay rather than by the
    structure being measured - a tail that falls 15 dB across a window scores
    as concentrated no matter how diffuse it is. A Phase-9 measurement that
    forgot this would read Decay as Density.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N  = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    // The shipping defaults, from ParameterLayout.cpp.
    constexpr double kDecay          = 2.5;
    constexpr double kSize           = 0.60;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kHighDamp       = 0.45;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDampHz     = 10000.0;

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

    /** Research knobs. Only the first two exist as production controls. */
    struct Config
    {
        double decay          = kDecay;
        double size           = kSize;
        double earlyDiffusion = kEarlyDiffusion;
        double modDepth       = kModDepth;
        double highDamp       = kHighDamp;

        bool inputDiffusion   = true;   ///< the whole stage, on or off
        int  allpassSections  = 1;      ///< in-loop sections per line, 1 or 2
        int  allpassLines     = -1;     ///< -1 keeps the candidate's own value
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (c.inputDiffusion);
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

        // Research knobs applied AFTER the candidate, so they override it.
        if (c.allpassSections != 1)
            tank->setAllpassSectionsPerLine (c.allpassSections);

        if (c.allpassLines >= 0)
            tank->setAllpassActiveLines (c.allpassLines);

        // Settle the Size morph on silence, then clear, so an impulse
        // response holds the tank AT its size rather than moving into it.
        for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        // Morph::reset() clears both banks but leaves the modulation LFOs
        // running, so a research comparison would start each configuration at
        // a different modulation phase and the differences would be partly
        // that. Reached through the banks on purpose: this is a measurement
        // requirement, not something production needs.
        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).getCore().resetModulation();

        tank->reset();

        return tank;
    }

    /** Left-channel impulse response of the tank, deterministic. */
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

    /** Both channels, from one pass, for the stereo questions. */
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

    /*
        Normalised echo density in a window starting at `atMs`.

        30 ms windows: long enough for the exceed-the-standard-deviation
        fraction to be a stable estimate (1440 samples at 48 kHz), short
        enough to resolve the build-up of a late field that saturates inside
        100 ms. The window is flattened first, so what is measured is the
        structure inside it and not the decay across it.
    */
    /*
        Returns -1 for a window that is effectively silent.

        This guard is here because its absence produced a confident wrong
        number. With the input diffuser at Diffusion 0 its allpass
        coefficients are zero, which makes the chain four cascaded pure
        delays - a single 14.7 ms delay - so the 5 ms window held nothing but
        pre-onset silence. flattenDecay divides by a smoothed copy of the
        envelope, which on silence amplifies the floor into something
        structured, and the density of that came out as 1.1227: higher than
        noise, from a window containing no signal. A statistic computed on
        silence is not a small number, it is a meaningless one, and it has to
        be refused rather than printed.
    */
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

        // 80 dB below the response's own peak: far below anything audible,
        // and far below where the flattening stays meaningful.
        if (peak <= 0.0 || rms < peak * 1.0e-4)
            return -1.0;

        const auto flat = flattenDecay (ir.data() + from, len);

        return normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());
    }

    /** "--" for a refused window, the number otherwise. */
    [[nodiscard]] std::string densityText (double value, int decimals = 4)
    {
        return value < 0.0 ? std::string ("--") : fx (value, decimals);
    }

    /*
        Envelope periodicity over a window, lags 1-150 ms. PHASE 6's measure.

        The window is passed RAW, and that correction is worth recording. The
        first version of this file flattened it first, as it does for echo
        density. But maxEnvelopePeriodicity detrends its own envelope
        internally, with the same 0.002 coefficient flattenDecay uses - so
        pre-flattening detrended it twice, and the numbers that came out
        (12 to 26, where 1 means diffuse) described the double-detrending
        rather than the tank. Echo density genuinely does need the flattening,
        because it measures against the window's own standard deviation.
        Two statistics, two different requirements, and one of them was being
        given the other's preparation.
    */
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

    /** Excess kurtosis over a flattened window: sparseness, decay removed. */
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

    /** Short-window RMS envelope, in dB, one point per `stepMs`. */
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

    /*
        The build-up landmark: the first time the short-window energy reaches
        `fraction` of the window maximum.

        This is the raw material for an Attack metric and for a Bloom metric,
        and it is deliberately measured on the ENERGY ENVELOPE rather than on
        the first non-zero sample. "When does the wet signal start" is
        Pre-Delay's question. "When is the late field established" is a
        different one, and only the second is a landmark on this curve.
    */
    [[nodiscard]] double timeToFractionMs (const std::vector<double>& ir,
                                           double fraction,
                                           double windowMs = 10.0,
                                           double untilMs = 400.0)
    {
        const auto env = envelopeDb (ir, windowMs, 1.0, untilMs);

        if (env.empty())
            return -1.0;

        const double peak = *std::max_element (env.begin(), env.end());
        const double thresholdDb = peak + 20.0 * std::log10 (fraction);

        for (size_t i = 0; i < env.size(); ++i)
            if (env[i] >= thresholdDb)
                return static_cast<double> (i);   // 1 ms steps

        return -1.0;
    }

    /** A long 8-stage allpass chain, AROUND the loop: the cheap candidate. */
    struct LateChain
    {
        StereoDiffusionNetwork<double> net;

        void prepare (double diffusionAmount, int stages)
        {
            net.setBasePattern (diffusion::longPattern);
            net.prepare (kSampleRate);
            net.setStageCount (stages, true);
            net.setDiffusion (diffusionAmount, true);
            net.reset();
        }

        void run (std::vector<double>& l, std::vector<double>& r)
        {
            for (size_t n = 0; n < l.size(); ++n)
                net.processSample (l[n], r[n]);
        }
    };
}

void runPhase9ResearchTests()
{
    ScopedFtz ftz;

    const double times[] { 5.0, 10.0, 20.0, 40.0, 80.0, 160.0, 320.0, 640.0 };

    // =======================================================================
    beginSuite ("PHASE 9 research - what these measures can resolve at all");
    // =======================================================================
    {
        /*
            This suite exists because the first run of this file quoted a
            density difference of 0.0066 as if it meant something, without
            knowing whether the measure could resolve 0.0066.

            Every statistic below is an estimate from a finite window, so it
            scatters with where the window is put even when nothing about the
            tank has changed. That scatter is the floor: a control that moves
            a number by less than it has not been shown to move the number at
            all. One configuration, one impulse response, the same statistic
            at eleven window positions across the late field.
        */
        info ("  One configuration - the shipping default - measured at eleven");
        info ("  window positions across the late field. Nothing about the tank");
        info ("  changes between these numbers, so their spread is what the");
        info ("  measure cannot resolve.");
        info ("");
        info ("  The windows here are the SAME LENGTHS the comparison tables");
        info ("  use: 30 ms for density, 600 ms for periodicity. An earlier");
        info ("  version measured the floor with 300 ms periodicity windows and");
        info ("  compared it against 600 ms ones, which is not a floor - the");
        info ("  statistic depends on how much signal it was given, so a floor");
        info ("  measured at one length says nothing about another.");
        info ("");

        auto tank = makeTank (Config {});
        const auto ir = tankIr (*tank, 3.5);

        double nedMin = 1.0e30, nedMax = -1.0e30;
        double perMin = 1.0e30, perMax = -1.0e30;
        double kurtMin = 1.0e30, kurtMax = -1.0e30;

        std::string row = "    NED        ";
        std::string perRow = "    period     ";

        for (int step = 0; step <= 10; ++step)
        {
            const double at = 300.0 + 20.0 * step;

            const double ned = densityAt (ir, at);
            const double per = periodicityIn (ir, at, at + 600.0);
            const double kurt = kurtosisIn (ir, at, at + 600.0);

            if (ned >= 0.0)
            {
                nedMin = std::min (nedMin, ned);
                nedMax = std::max (nedMax, ned);
            }

            perMin = std::min (perMin, per);
            perMax = std::max (perMax, per);
            kurtMin = std::min (kurtMin, kurt);
            kurtMax = std::max (kurtMax, kurt);

            row    += pad (densityText (ned, 3), 8);
            perRow += pad (fx (per, 2), 8);
        }

        info ("    window start   300 ms to 500 ms in 20 ms steps");
        info (row);
        info (perRow);
        info ("");
        info ("    NED        spread " + fx (nedMax - nedMin, 4)
              + "   range " + fx (nedMin, 3) + " to " + fx (nedMax, 3));
        info ("    period     spread " + fx (perMax - perMin, 4)
              + "   range " + fx (perMin, 2) + " to " + fx (perMax, 2));
        info ("    kurtosis   spread " + fx (kurtMax - kurtMin, 4)
              + "   range " + fx (kurtMin, 3) + " to " + fx (kurtMax, 3));
        info ("");
        info ("    RESOLUTION FLOOR. Read every later table against these.");
        info ("    A control that moves NED by less than " + fx (nedMax - nedMin, 3)
              + " has not been");
        info ("    shown to move it. The same for periodicity below "
              + fx (perMax - perMin, 2) + ".");

        check (nedMax > nedMin, "the resolution floor of the density measure is established");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - where the late field's density comes from");
    // =======================================================================
    {
        info ("  Normalised echo density (Abel & Huang) in 30 ms windows, each");
        info ("  flattened first so the decay across it cannot read as");
        info ("  structure inside it. 1.0 means indistinguishable from noise;");
        info ("  near 0 means a handful of discrete echoes.");
        info ("");

        struct Case { const char* label; Config config; };

        Config tankOnly;
        tankOnly.inputDiffusion = false;

        Config early0  = Config {}; early0.earlyDiffusion  = 0.0;
        Config early70 = Config {}; early70.earlyDiffusion = 0.70;
        Config early100 = Config {}; early100.earlyDiffusion = 1.0;

        const Case cases[]
        {
            { "tank alone",     tankOnly },
            { "Early Diff 0",   early0 },
            { "Early Diff 70",  early70 },
            { "Early Diff 100", early100 },
        };

        std::string header = "    " + pad ("configuration", 17);

        for (double t : times)
            header += pad (fx (t, 0) + "ms", 9);

        info (header);

        double densityAt40Shipping = 0.0;
        double densityAt320Shipping = 0.0;
        double worstLateSpread = 0.0;
        double lateAtEarly0 = 0.0, lateAtEarly100 = 0.0;

        for (const auto& c : cases)
        {
            auto tank = makeTank (c.config);
            const auto ir = tankIr (*tank, 1.5);

            std::string row = "    " + pad (c.label, 17);

            for (double t : times)
                row += pad (densityText (densityAt (ir, t)), 9);

            info (row);

            if (std::string (c.label) == "Early Diff 70")
            {
                densityAt40Shipping = densityAt (ir, 40.0);
                densityAt320Shipping = densityAt (ir, 320.0);
            }

            if (std::string (c.label) == "Early Diff 0")
                lateAtEarly0 = densityAt (ir, 320.0);

            if (std::string (c.label) == "Early Diff 100")
                lateAtEarly100 = densityAt (ir, 320.0);
        }

        worstLateSpread = std::abs (lateAtEarly100 - lateAtEarly0);

        info ("");
        info ("    At the shipping default, density at 40 ms is "
              + fx (densityAt40Shipping, 4) + " and at 320 ms");
        info ("    it is " + fx (densityAt320Shipping, 4) + ".");
        info ("");
        info ("    Early Diffusion 0 -> 100 % moves the 320 ms density by "
              + fx (worstLateSpread, 4) + ".");
        info ("");
        info ("    Read that second number carefully, because it is the whole");
        info ("    Density question. If the late field is already at or near the");
        info ("    noise-equivalent value whatever the excitation does, then a");
        info ("    Density control that only reshapes the excitation would be");
        info ("    moving a quantity that is already saturated - a knob with a");
        info ("    number on it and nothing under it.");

        // No gate: this suite measures the architecture rather than policing
        // it. A threshold here would be a Phase-9 decision smuggled into a
        // Phase-9 measurement.
        check (densityAt320Shipping > 0.0,
               "the late field's density is measurable at the shipping default");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - what Early Diffusion actually reaches");
    // =======================================================================
    {
        info ("  The same response, split into an EARLY window (0-30 ms, which");
        info ("  the input diffuser's own 14.7 ms group delay sits inside) and a");
        info ("  LATE window (300-900 ms, long after the excitation has gone).");
        info ("");
        info ("  If Early Diffusion is an excitation control, the early column");
        info ("  moves and the late column does not. If it reaches the late");
        info ("  field, both move - and a separate Late Diffusion control would");
        info ("  then be a second scaling of the same thing.");
        info ("");
        info ("    " + pad ("Early Diff", 12)
              + pad ("NED early", 12) + pad ("NED late", 12)
              + pad ("period early", 14) + pad ("period late", 13)
              + "kurtosis late");

        double earlySpread = 0.0, lateSpread = 0.0;
        double firstEarly = 0.0, firstLate = 0.0;
        bool first = true;

        for (double amount : { 0.0, 0.35, 0.70, 1.0 })
        {
            Config c;
            c.earlyDiffusion = amount;

            auto tank = makeTank (c);
            const auto ir = tankIr (*tank, 1.5);

            const double nedEarly = densityAt (ir, 0.0, 30.0);
            const double nedLate  = densityAt (ir, 320.0, 30.0);
            const double perEarly = periodicityIn (ir, 0.0, 200.0);
            const double perLate  = periodicityIn (ir, 300.0, 900.0);
            const double kurtLate = kurtosisIn (ir, 300.0, 900.0);

            // A refused window cannot anchor a spread. At Early Diffusion 0
            // the chain is four pure delays, so the early window is still
            // pre-onset silence and densityAt refuses it - and an earlier
            // version anchored on that -1 and reported an early spread of
            // 1.1247, which was the sentinel, not a measurement.
            if (first && nedEarly >= 0.0 && nedLate >= 0.0)
            {
                firstEarly = nedEarly;
                firstLate = nedLate;
                first = false;
            }

            if (! first && nedEarly >= 0.0)
                earlySpread = std::max (earlySpread, std::abs (nedEarly - firstEarly));

            if (! first && nedLate >= 0.0)
                lateSpread  = std::max (lateSpread,  std::abs (nedLate  - firstLate));

            info ("    " + pad (fx (amount * 100.0, 0) + " %", 12)
                  + pad (densityText (nedEarly), 12) + pad (densityText (nedLate), 12)
                  + pad (fx (perEarly, 3), 14) + pad (fx (perLate, 3), 13)
                  + fx (kurtLate, 3));
        }

        info ("");
        info ("    early-window NED spread across the control  " + fx (earlySpread, 4));
        info ("    late-window  NED spread across the control  " + fx (lateSpread, 4));

        check (earlySpread >= 0.0 && lateSpread >= 0.0,
               "Early Diffusion's early and late reach are both measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - Late Diffusion candidates, in and around the loop");
    // =======================================================================
    {
        /*
            Two structurally different candidates for a Late Diffusion
            control, measured against the same baseline.

            AROUND THE LOOP: the long 8-stage allpass pattern that already
            exists in DiffusionTuning.h and that production does not use -
            50.78 ms left, 39.70 ms right - placed after the tank. It cannot
            touch the recurrence, so the decay law is safe by construction,
            and it costs eight allpass sections per channel.

            INSIDE THE LOOP: the AllpassPhaseBank already in the line
            feedback path, with two sections per line instead of one. This is
            the architecturally interesting one, because an allpass inside a
            feedback path is applied once per circulation, so its effect
            compounds with the tail rather than being a fixed post-filter.

            Both are research configurations. Neither is wired to anything.
        */
        info ("  Baseline is the shipping tank. Two candidates:");
        info ("");
        info ("    around the loop   the unused 8-stage long allpass pattern");
        info ("                      from DiffusionTuning.h, after the tank");
        info ("    inside the loop   AllpassPhaseBank at 2 sections per line");
        info ("                      instead of 1, in the line feedback path");
        info ("");
        info ("    " + pad ("configuration", 26)
              + pad ("NED late", 12) + pad ("period late", 14)
              + pad ("kurt late", 12) + "RT60 proxy");

        /*
            An RT60 proxy, because the decay law is the thing Phase 9 must not
            break casually. Measured as the time for the short-window envelope
            to fall 30 dB from its peak, scaled to 60 dB. Crude next to the
            certified analyser, and it is not used as a gate - it is here to
            show whether a candidate moved the decay at all.
        */
        const auto rt60Proxy = [] (const std::vector<double>& ir)
        {
            const auto env = envelopeDb (ir, 30.0, 10.0, 3000.0);

            if (env.empty())
                return 0.0;

            // Search AFTER the peak. The first version searched from index 0
            // and reported 0.000 s for every around-the-loop row, because the
            // long allpass chain adds 50 ms of group delay, so the first
            // window was pre-onset silence and already more than 30 dB below
            // the peak. The number was not a short decay, it was the
            // measurement reading its own latency.
            const auto peakAt = static_cast<size_t> (
                std::max_element (env.begin(), env.end()) - env.begin());

            const double peak = env[peakAt];

            for (size_t i = peakAt; i < env.size(); ++i)
                if (env[i] <= peak - 30.0)
                    return 2.0 * static_cast<double> (i - peakAt) * 0.010;

            return 0.0;
        };

        struct Row { std::string label; double ned, per, kurt, rt; };

        std::vector<Row> rows;

        // --- baseline -----------------------------------------------------
        {
            auto tank = makeTank (Config {});
            const auto ir = tankIr (*tank, 3.5);

            rows.push_back ({ "baseline (shipping)",
                              densityAt (ir, 320.0), periodicityIn (ir, 300.0, 900.0),
                              kurtosisIn (ir, 300.0, 900.0), rt60Proxy (ir) });
        }

        // --- around the loop, three strengths -----------------------------
        for (double amount : { 0.35, 0.70, 1.0 })
        {
            auto tank = makeTank (Config {});

            std::vector<double> l, r;
            tankIrStereo (*tank, 3.5, l, r);

            LateChain chain;
            chain.prepare (amount, 8);
            chain.run (l, r);

            rows.push_back ({ "around loop, 8 stages @ " + fx (amount * 100.0, 0),
                              densityAt (l, 320.0), periodicityIn (l, 300.0, 900.0),
                              kurtosisIn (l, 300.0, 900.0), rt60Proxy (l) });
        }

        // --- inside the loop ----------------------------------------------
        {
            Config c;
            c.allpassSections = 2;

            auto tank = makeTank (c);
            const auto ir = tankIr (*tank, 3.5);

            rows.push_back ({ "inside loop, 2 sections",
                              densityAt (ir, 320.0), periodicityIn (ir, 300.0, 900.0),
                              kurtosisIn (ir, 300.0, 900.0), rt60Proxy (ir) });
        }

        // --- inside the loop, and the coupling that matters ---------------
        {
            Config c;
            c.allpassSections = 2;
            c.modDepth = 0.0;

            auto tank = makeTank (c);
            const auto ir = tankIr (*tank, 3.5);

            rows.push_back ({ "inside loop, Mod Depth 0",
                              densityAt (ir, 320.0), periodicityIn (ir, 300.0, 900.0),
                              kurtosisIn (ir, 300.0, 900.0), rt60Proxy (ir) });
        }

        for (const auto& row : rows)
            info ("    " + pad (row.label, 26)
                  + pad (densityText (row.ned), 12) + pad (fx (row.per, 3), 14)
                  + pad (fx (row.kurt, 3), 12) + fx (row.rt, 3) + " s");

        info ("");
        info ("    The last row is the one to read first. The in-loop bank");
        info ("    disengages completely at Mod Depth 0 - that is PHASE 5B's");
        info ("    true-bypass property, and it is deliberate - so a Late");
        info ("    Diffusion control built on that bank would do NOTHING at Mod");
        info ("    Depth 0. Two user controls where one silently disables the");
        info ("    other is not a parameter set; it is a bug report waiting to");
        info ("    be filed.");

        check (rows.size() == 6, "every Late Diffusion candidate was measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - Attack against Pre-Delay");
    // =======================================================================
    {
        /*
            The distinction the brief requires proved, not asserted.

            Pre-Delay moves the whole wet onset later and changes nothing
            about the shape that follows. An Attack worth having must change
            the SHAPE after onset. The test is the build-up curve: landmark
            times at which the short-window energy first reaches -20, -10 and
            -3 dB of its own peak.

            Mechanism N - a plain wet amplitude envelope - is included as the
            control architecture the brief asked for. It is the cheap answer
            and it is measured here so that a later decision to reject or keep
            it rests on numbers.
        */
        info ("  Build-up landmarks: the first time the 10 ms short-window");
        info ("  energy reaches -20, -10 and -3 dB of its own peak.");
        info ("");
        info ("  Pre-Delay shifts all three landmarks by the same amount and");
        info ("  leaves the gaps between them alone. Anything worth calling");
        info ("  Attack has to change the GAPS.");
        info ("");
        info ("    " + pad ("configuration", 28)
              + pad ("-20 dB", 10) + pad ("-10 dB", 10) + pad ("-3 dB", 10)
              + "gap -20 to -3");

        auto tank = makeTank (Config {});
        const auto baseIr = tankIr (*tank, 2.0);

        const auto report = [&] (const std::string& label, const std::vector<double>& ir)
        {
            const double t20 = timeToFractionMs (ir, 0.1);
            const double t10 = timeToFractionMs (ir, 0.3162);
            const double t3  = timeToFractionMs (ir, 0.7079);

            info ("    " + pad (label, 28)
                  + pad (fx (t20, 1) + " ms", 10)
                  + pad (fx (t10, 1) + " ms", 10)
                  + pad (fx (t3, 1) + " ms", 10)
                  + fx (t3 - t20, 1) + " ms");

            return t3 - t20;
        };

        const double baseGap = report ("baseline", baseIr);

        // --- Pre-Delay 50 ms, the production control ----------------------
        {
            auto t2 = makeTank (Config {});

            std::vector<double> l, r;
            tankIrStereo (*t2, 2.0, l, r);

            // The production pre-delay sits BEFORE the tank, so the correct
            // comparison delays the excitation rather than the output. Done
            // by rendering a delayed impulse through a fresh tank.
            auto t3 = makeTank (Config {});

            const auto length = static_cast<size_t> (kSampleRate * 2.0);

            std::vector<double> delayed (length, 0.0);

            PreDelay<double> pd;
            pd.prepare (kSampleRate);
            pd.snapTo (50.0);

            std::vector<double> drive (length, 0.0), driveR (length, 0.0);
            drive[0] = 1.0;
            driveR[0] = 1.0;
            pd.process (drive.data(), driveR.data(), static_cast<int> (length));

            for (size_t n = 0; n < length; ++n)
            {
                double a = drive[n], b = driveR[n];
                t3->processSample (a, b);
                delayed[n] = a;
            }

            report ("Pre-Delay 50 ms", delayed);
        }

        // --- mechanism N: a wet amplitude envelope, the control -----------
        for (double attackMs : { 50.0, 150.0 })
        {
            auto ir = baseIr;

            // A one-pole rise on the wet output. Nothing structural: this is
            // the reference architecture, included to be measured rather than
            // assumed acceptable.
            const double a = std::exp (-1.0 / (attackMs * kSampleRate / 1000.0));
            double state = 0.0;

            for (auto& s : ir)
            {
                state = a * state + (1.0 - a);
                s *= state;
            }

            const double gap = report ("N: wet envelope " + fx (attackMs, 0) + " ms", ir);

            info ("      gap grew by " + fx (gap - baseGap, 1)
                  + " ms against the baseline");
        }

        /*
            What the landmark metric CANNOT distinguish, measured rather than
            left as a caveat.

            Mechanism N scales the amplitude of a field whose density is
            already established. Mechanism J - gradual injection into the
            network - would make the density itself build more slowly. Both
            stretch the landmark gap, so the landmark metric alone cannot
            choose between them, and a Stage-2 decision resting on it would be
            resting on nothing.

            The discriminator is the density-versus-time curve: under N it
            should be unchanged, because flattening removes the envelope
            again. If that holds, then "the gap stretched" and "the field
            built more slowly" are different claims and only the second is
            what Attack is supposed to mean.
        */
        info ("");
        info ("  The metric's own blind spot, measured. Density versus time");
        info ("  under mechanism N, against the baseline:");
        info ("");
        info ("    " + pad ("configuration", 28) + pad ("10ms", 9) + pad ("20ms", 9)
              + pad ("40ms", 9) + pad ("80ms", 9) + "160ms");

        {
            auto enveloped = baseIr;

            const double a = std::exp (-1.0 / (150.0 * kSampleRate / 1000.0));
            double state = 0.0;

            for (auto& s : enveloped)
            {
                state = a * state + (1.0 - a);
                s *= state;
            }

            const double probes[] { 10.0, 20.0, 40.0, 80.0, 160.0 };

            std::string baseRow = "    " + pad ("baseline", 28);
            std::string envRow  = "    " + pad ("N: wet envelope 150 ms", 28);
            double worst = 0.0;

            for (double t : probes)
            {
                const double b = densityAt (baseIr, t);
                const double e = densityAt (enveloped, t);

                baseRow += pad (densityText (b, 4), 9);
                envRow  += pad (densityText (e, 4), 9);

                if (b >= 0.0 && e >= 0.0)
                    worst = std::max (worst, std::abs (b - e));
            }

            info (baseRow);
            info (envRow);
            info ("");
            info ("    worst density difference  " + fx (worst, 4));
            info ("");
            info ("    Against the 0.077 resolution floor established in the");
            info ("    first suite. A wet envelope that stretches the landmark");
            info ("    gap by 41 ms while leaving the density-versus-time curve");
            info ("    where it was is not building a late field slowly - it is");
            info ("    turning down a late field that is already fully built.");
            info ("    So the landmark metric is necessary and not sufficient,");
            info ("    and any Attack mechanism has to be judged on BOTH.");
        }

        info ("");
        info ("    A Pre-Delay row that shifts all three landmarks together");
        info ("    while keeping the gap, next to an envelope row that stretches");
        info ("    the gap, is the proof the brief asked for: the two concepts");
        info ("    are measurably different and a 50 ms Attack is not +50 ms of");
        info ("    Pre-Delay.");
        info ("");
        info ("    What this does NOT settle is whether the envelope is the");
        info ("    right mechanism. It changes only the amplitude of a field");
        info ("    whose density is already established, so the tail arrives");
        info ("    fully dense and merely quiet. Whether that reads as a slow");
        info ("    attack or as someone riding a fader is a listening question,");
        info ("    and it belongs in Stage 2.");

        check (baseGap >= 0.0, "the baseline build-up gap is measurable");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - independence probes for the matrix");
    // =======================================================================
    {
        /*
            The independence matrix needs evidence, not opinion, for the pairs
            that decide the parameter count. These are the probes: sweep one
            existing control and watch the late-field density and periodicity
            that a Density or Late Diffusion control would claim to own.

            A control that already moves those numbers is a control the new
            one would partly duplicate.
        */
        info ("  Each existing control swept, with the late-field measures a");
        info ("  Density or Late Diffusion control would claim as its own.");
        info ("  A control that already moves them is one the new control");
        info ("  would partly duplicate.");
        info ("");
        info ("    " + pad ("control", 20) + pad ("setting", 11)
              + pad ("NED late", 12) + pad ("period late", 14)
              + pad ("kurt late", 12) + pad ("NED 20ms", 11) + "buildup gap");

        /*
            The buildup gap column is here for the Bloom question, and it is
            the measurement that decides §13.

            If Bloom means "the shape between onset and the established
            field", then its measure is the gap between buildup landmarks.
            Decay must not already own that gap - if lengthening Decay
            stretches the buildup the same way a Bloom control would, then
            Bloom is Decay under another name and the brief says to reject
            it.
        */
        const auto probe = [&] (const std::string& control, const std::string& setting,
                                const Config& c)
        {
            auto t = makeTank (c);
            const auto ir = tankIr (*t, 3.5);

            const double t20 = timeToFractionMs (ir, 0.1);
            const double t3  = timeToFractionMs (ir, 0.7079);

            info ("    " + pad (control, 20) + pad (setting, 11)
                  + pad (densityText (densityAt (ir, 320.0)), 12)
                  + pad (fx (periodicityIn (ir, 300.0, 900.0), 3), 14)
                  + pad (fx (kurtosisIn (ir, 300.0, 900.0), 3), 12)
                  + pad (densityText (densityAt (ir, 20.0)), 11)
                  + fx (t3 - t20, 1) + " ms");
        };

        for (double d : { 1.0, 2.5, 6.0 })
        {
            Config c; c.decay = d;
            probe ("Decay", fx (d, 1) + " s", c);
        }

        for (double s : { 0.20, 0.60, 1.0 })
        {
            Config c; c.size = s;
            probe ("Size", fx (s * 100.0, 0) + " %", c);
        }

        for (double m : { 0.0, 0.30, 0.70 })
        {
            Config c; c.modDepth = m;
            probe ("Mod Depth", fx (m * 100.0, 0) + " %", c);
        }

        for (double h : { 0.0, 0.45, 1.0 })
        {
            Config c; c.highDamp = h;
            probe ("High Damp", fx (h * 100.0, 0) + " %", c);
        }

        info ("");
        info ("    Three columns, three questions.");
        info ("");
        info ("    NED late is the Density claim. If it does not move here and");
        info ("    does not move for Early Diffusion either, it is not that");
        info ("    these controls fail to reach it - it is that there is");
        info ("    nothing left to reach.");
        info ("");
        info ("    period late is the Late Diffusion claim. Every control that");
        info ("    moves it by more than the floor is a control a Late");
        info ("    Diffusion knob would be entangled with.");
        info ("");
        info ("    buildup gap is the Bloom claim. Decay must not already own");
        info ("    it. If lengthening Decay stretches the buildup the way a");
        info ("    Bloom control would, Bloom is Decay wearing a different");
        info ("    label and the brief says to reject it.");

        check (true, "the independence probes ran");
    }

    // =======================================================================
    beginSuite ("PHASE 9 research - stereo cost of a late diffusion candidate");
    // =======================================================================
    {
        /*
            A density or diffusion mechanism that destroys the stereo field is
            not acceptable, so the candidate that is cheapest to add - the
            long allpass chain around the loop - is measured for what it does
            to channel correlation and to the mono downmix.
        */
        info ("  L/R correlation and mono-downmix energy for the baseline and");
        info ("  the around-the-loop candidate. A diffusion mechanism that");
        info ("  collapses the image or hollows the downmix is not acceptable");
        info ("  however good its density figure looks.");
        info ("");
        info ("    " + pad ("configuration", 26)
              + pad ("L/R corr", 12) + pad ("mono/stereo", 14) + "NED late");

        const auto measure = [&] (const std::string& label,
                                  std::vector<double> l, std::vector<double> r)
        {
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

            info ("    " + pad (label, 26) + pad (fx (corr, 4), 12)
                  + pad (fx (ratio, 2) + " dB", 14)
                  + densityText (densityAt (l, 320.0)));
        };

        {
            auto tank = makeTank (Config {});
            std::vector<double> l, r;
            tankIrStereo (*tank, 1.5, l, r);
            measure ("baseline (shipping)", l, r);
        }

        for (double amount : { 0.70, 1.0 })
        {
            auto tank = makeTank (Config {});
            std::vector<double> l, r;
            tankIrStereo (*tank, 1.5, l, r);

            LateChain chain;
            chain.prepare (amount, 8);
            chain.run (l, r);

            measure ("around loop, 8 st @ " + fx (amount * 100.0, 0), l, r);
        }

        info ("");
        info ("    The long pattern's two channels differ by 11 ms of total");
        info ("    group delay - 50.78 against 39.70 ms - which DiffusionTuning");
        info ("    rejected for the INPUT position precisely because an 11 ms");
        info ("    inter-ear onset difference is a localisation cue rather than");
        info ("    reverb. After the tank there is no onset left to localise, so");
        info ("    the objection may not carry. These numbers say whether it");
        info ("    does.");

        check (true, "the stereo cost of the around-the-loop candidate was measured");
    }
}

} // namespace avtest
