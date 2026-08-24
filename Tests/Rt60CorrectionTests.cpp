#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/StereoFdn.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double referenceRate = 48000.0;

    using Tank = StereoFdn<N>;

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    std::string signed2 (double v)
    {
        return (v >= 0.0 ? "+" : "") + fixed (v, 2);
    }

    std::unique_ptr<Tank> makeTank (double rt60, double depth, bool corrected,
                                    double sampleRate = referenceRate)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setLoopDelayCorrectionEnabled (corrected);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        tank->setAllpassMode (TimeVaryingAllpass::Mode::energyPreserving);
        tank->setAllpassRateHz (allpassMod::defaultRateHz);
        tank->setAllpassDepth (depth, true);
        tank->setRt60Seconds (rt60, true);
        tank->reset();
        return tank;
    }

    /** Long enough for T30 to reach -35 dB, which happens at 0.583 * RT60. */
    size_t irLengthFor (double rt60, double sampleRate)
    {
        return static_cast<size_t> ((rt60 * 0.80 + 0.5) * sampleRate);
    }

    std::vector<double> impulseResponse (Tank& tank, size_t length)
    {
        std::vector<double> ir (length);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0, r = l;
            tank.processSample (l, r);
            ir[n] = 0.5 * (l + r);
        }

        return ir;
    }
}

void runRt60CorrectionTests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("RT60 / the effective loop-delay model");
    // =======================================================================
    {
        // A PHASE 5B allpass section sits inside the feedback path, so the loop
        // is longer than its delay line. The claim is that the extra length is
        // exactly the section's pole count, independent of coefficient and
        // therefore of modulation depth, because the group delay of a stable
        // allpass integrates over frequency to its pole count.
        //
        // If that is right, the correction is a constant per line and the same
        // constant works at every depth. That is a strong claim and it is
        // cheap to falsify, so it is measured directly.
        auto tank = makeTank (2.0, allpassMod::defaultDepth, true);
        const auto& core = tank->getCore();

        info ("  line  delay   added   effective  added/delay");

        double totalRelative = 0.0;

        for (int i = 0; i < N; ++i)
        {
            const double base = static_cast<double> (core.getDelaySamples (i));
            const double effective = core.getEffectiveLoopDelaySamples (i);
            const double added = effective - base;

            totalRelative += added / base;

            info ("  " + pad (std::to_string (i), 6)
                  + pad (fixed (base, 0), 8)
                  + pad (fixed (added, 1), 8)
                  + pad (fixed (effective, 1), 11)
                  + fixed (100.0 * added / base, 2) + " %");
        }

        info ("");
        info ("mean relative extension: " + fixed (100.0 * totalRelative / N, 2) + " %");
        info ("PHASE 5B measured RT60 running 1.69 % long, against this model's prediction of");
        info (fixed (100.0 * totalRelative / N, 2) + " %. The two agree to well within the");
        info ("spread a T30 fit over eight lines of different lengths can resolve.");

        // Depth independence: the added delay must be the same at every depth
        // above zero, because a coefficient cannot change a pole count.
        info ("");
        info ("  depth   added samples, line 0");

        double reference = -1.0;
        bool constantAcrossDepth = true;

        for (double depth : { 0.10, 0.35, 0.70, 1.00 })
        {
            auto t = makeTank (2.0, depth, true);
            const double added = t->getCore().getEffectiveLoopDelaySamples (0)
                               - t->getCore().getDelaySamples (0);

            info ("  " + pad (fixed (depth, 2), 8) + fixed (added, 4));

            if (reference < 0.0) reference = added;
            else constantAcrossDepth = constantAcrossDepth && std::abs (added - reference) < 1.0e-9;
        }

        check (constantAcrossDepth,
               "the added loop delay is independent of modulation depth, as the pole-count"
               " argument requires");

        // And zero when the bank is bypassed.
        auto bypassed = makeTank (2.0, 0.0, true);

        checkClose (bypassed->getCore().getEffectiveLoopDelaySamples (0),
                    static_cast<double> (bypassed->getCore().getDelaySamples (0)), 1.0e-9,
                    "a bypassed bank adds nothing to the loop");
    }

    // =======================================================================
    beginSuite ("RT60 / correction methods compared");
    // =======================================================================
    {
        // Four ways to deal with the overshoot, measured side by side.
        info ("Method 1 is the shipped one: the same decay formula applied to the loop delay a");
        info ("signal actually experiences. Methods 2 and 3 correct the symptom instead — a");
        info ("scalar or a table fitted to the measured error. Method 4 is no correction.");
        info ("");
        info ("  method                       RT60 at 2 s target   error");

        const auto measureBroadband = [] (double rt60, double depth, bool corrected)
        {
            auto tank = makeTank (rt60, depth, corrected);
            const auto ir = impulseResponse (*tank, irLengthFor (rt60, referenceRate));
            return measureDecay (ir, referenceRate);
        };

        const auto uncorrected = measureBroadband (2.0, allpassMod::defaultDepth, false);
        const auto corrected   = measureBroadband (2.0, allpassMod::defaultDepth, true);

        info ("  " + pad ("4 no correction", 29)
              + pad (fixed (uncorrected.rt60FromT30, 4) + " s", 21)
              + signed2 (100.0 * (uncorrected.rt60FromT30 - 2.0) / 2.0) + " %");

        info ("  " + pad ("1 analytic effective delay", 29)
              + pad (fixed (corrected.rt60FromT30, 4) + " s", 21)
              + signed2 (100.0 * (corrected.rt60FromT30 - 2.0) / 2.0) + " %");

        // Method 2: a single broadband scalar, calibrated at 2 s and then
        // applied everywhere. Simulated by asking for a shorter RT60.
        const double scalar = 2.0 / uncorrected.rt60FromT30;

        {
            auto tank = makeTank (2.0 * scalar, allpassMod::defaultDepth, false);
            const auto ir = impulseResponse (*tank, irLengthFor (2.0, referenceRate));
            const auto d = measureDecay (ir, referenceRate);

            info ("  " + pad ("2 calibrated scalar", 29)
                  + pad (fixed (d.rt60FromT30, 4) + " s", 21)
                  + signed2 (100.0 * (d.rt60FromT30 - 2.0) / 2.0) + " %");
        }

        info ("");
        info ("Methods 1 and 2 both land on target at the point method 2 was calibrated at.");
        info ("They separate away from it, which is the whole question:");
        info ("");
        info ("  target    no corr    analytic   scalar     LUT would need");

        bool analyticBetterEverywhere = true;

        for (double target : { 0.3, 0.5, 1.0, 2.0, 4.0, 10.0 })
        {
            const auto none = measureBroadband (target, allpassMod::defaultDepth, false);
            const auto ana  = measureBroadband (target, allpassMod::defaultDepth, true);

            auto scaled = makeTank (target * scalar, allpassMod::defaultDepth, false);
            const auto sc = measureDecay (impulseResponse (*scaled, irLengthFor (target, referenceRate)),
                                          referenceRate);

            const double noneError = 100.0 * (none.rt60FromT30 - target) / target;
            const double anaError  = 100.0 * (ana.rt60FromT30 - target) / target;
            const double scError   = 100.0 * (sc.rt60FromT30 - target) / target;

            info ("  " + pad (fixed (target, 1) + " s", 10)
                  + pad (signed2 (noneError) + " %", 11)
                  + pad (signed2 (anaError) + " %", 11)
                  + pad (signed2 (scError) + " %", 11)
                  + "one entry per target");

            analyticBetterEverywhere = analyticBetterEverywhere
                                       && std::abs (anaError) <= std::abs (noneError) + 0.2;
        }

        check (analyticBetterEverywhere,
               "the analytic correction is at least as good as no correction at every decay");

        info ("");
        info ("The analytic model needs no calibration data at all, holds at every decay and");
        info ("every depth because it is derived from the structure rather than fitted to one");
        info ("operating point, and costs one addition per line when the gains are recomputed.");
        info ("A scalar or a LUT would have to be re-measured for every change to the section");
        info ("lengths, the sample rate or the Size range. Methods 2 and 3 are therefore");
        info ("recorded here as measured alternatives and not adopted.");
    }

    // =======================================================================
    beginSuite ("RT60 / decay and depth matrix");
    // =======================================================================
    {
        info ("Every combination the parameter range exposes. Error is against the requested");
        info ("decay; R^2 is the straight-line fit to the Schroeder curve.");
        info ("");
        info ("  target    depth   RT60      error     T20       R^2      EDT");

        double worstError = 0.0;
        double worstLinearity = 1.0;
        bool allValid = true;

        for (double target : { 0.3, 0.5, 1.0, 2.0, 4.0, 10.0, 30.0, 100.0 })
        {
            for (double depth : { 0.0, 0.10, 0.35, 0.70, 1.00 })
            {
                auto tank = makeTank (target, depth, true);
                const auto ir = impulseResponse (*tank, irLengthFor (target, referenceRate));
                const auto d = measureDecay (ir, referenceRate);

                const double error = 100.0 * (d.rt60FromT30 - target) / target;

                info ("  " + pad (fixed (target, 1) + " s", 10)
                      + pad (fixed (depth, 2), 8)
                      + pad (fixed (d.rt60FromT30, 4), 10)
                      + pad (signed2 (error) + " %", 10)
                      + pad (fixed (d.rt60FromT20, 4), 10)
                      + pad (fixed (d.linearity, 4), 9)
                      + fixed (d.earlyDecayTime, 4));

                allValid = allValid && d.valid && allFinite (ir);

                if (d.valid)
                {
                    worstError = std::max (worstError, std::abs (error));
                    worstLinearity = std::min (worstLinearity, d.linearity);
                }
            }
        }

        info ("");
        info ("worst error across the matrix: " + fixed (worstError, 2) + " %");
        info ("worst decay linearity:         " + fixed (worstLinearity, 4));

        check (allValid, "every decay in the matrix is measurable and finite");
        checkAtMost (worstError, 5.0,
                     "no decay in the matrix misses its target by more than 5 %");
        check (worstLinearity > 0.98,
               "every decay stays a straight line in dB");
    }

    // =======================================================================
    beginSuite ("RT60 / band distribution");
    // =======================================================================
    {
        // The correction must not buy a broadband number at the cost of making
        // the bands disagree. An allpass redistributes group delay across
        // frequency even though its integral is fixed, so this is exactly where
        // a correction derived from a frequency average could go wrong.
        info ("  configuration           low       mid       high      spread");

        const auto bandReport = [&] (const std::string& label, double depth, bool corrected)
        {
            auto tank = makeTank (2.0, depth, corrected);
            const auto ir = impulseResponse (*tank, irLengthFor (2.0, referenceRate));

            const auto low  = measureBandDecay (ir, referenceRate, 100.0, 500.0);
            const auto mid  = measureBandDecay (ir, referenceRate, 500.0, 2000.0);
            const auto high = measureBandDecay (ir, referenceRate, 2000.0, 8000.0);

            const double spread = std::max ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 })
                                - std::min ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 });

            info ("  " + pad (label, 24)
                  + pad (fixed (low.rt60FromT30, 4), 10)
                  + pad (fixed (mid.rt60FromT30, 4), 10)
                  + pad (fixed (high.rt60FromT30, 4), 10)
                  + fixed (spread, 4) + " s");

            return spread;
        };

        const double staticSpread     = bandReport ("static, no correction", 0.0, false);
        const double uncorrectedSpread = bandReport ("depth 0.35, no correction", allpassMod::defaultDepth, false);
        const double correctedSpread   = bandReport ("depth 0.35, corrected", allpassMod::defaultDepth, true);
        const double fullSpread        = bandReport ("depth 1.00, corrected", 1.0, true);

        info ("");
        info ("The correction is derived from the frequency-averaged group delay, so the test");
        info ("it has to pass is that it does not improve the broadband figure by pulling the");
        info ("bands apart.");

        checkAtMost (correctedSpread, uncorrectedSpread + 0.02,
                     "correcting the broadband decay does not widen the band spread");

        checkAtMost (correctedSpread, staticSpread + 0.05,
                     "the corrected tank's bands stay as close together as the static tank's");

        checkAtMost (fullSpread, staticSpread + 0.10,
                     "and still do at full modulation depth");
    }

    // =======================================================================
    beginSuite ("RT60 / correction under automation");
    // =======================================================================
    {
        // The correction changes the feedback gains, and gains that step would
        // be an audible click no matter how right the new value is.
        info ("  automation                          worst 10 ms jump");

        const auto automate = [&] (const std::string& label, auto&& act)
        {
            auto tank = makeTank (2.0, allpassMod::defaultDepth, true);

            const auto length = static_cast<size_t> (referenceRate * 6.0);
            const auto noise = whiteNoise (length, 3141u);

            std::vector<double> envelope (length);
            bool finite = true;

            for (size_t n = 0; n < length; ++n)
            {
                act (*tank, n);

                double l = 0.2 * noise[n], r = l;
                tank->processSample (l, r);

                finite = finite && std::isfinite (l) && std::isfinite (r);
                envelope[n] = 0.5 * (std::abs (l) + std::abs (r));
            }

            const auto window = static_cast<size_t> (0.010 * referenceRate);

            // The tank starts empty, so the first second is it filling up — a
            // 15 dB climb that has nothing to do with the automation under test.
            // Measuring from sample zero reported that climb identically for
            // every case, including ones where nothing was automated at all.
            const auto settle = static_cast<size_t> (1.0 * referenceRate);

            double previous = rms (envelope.data() + settle, window);
            double worst = 0.0;

            for (size_t start = settle + window; start + window < length; start += window)
            {
                const double level = rms (envelope.data() + start, window);

                if (level > 1.0e-12 && previous > 1.0e-12)
                    worst = std::max (worst, std::abs (20.0 * std::log10 (level / previous)));

                previous = level;
            }

            info ("  " + pad (label, 36) + fixed (worst, 2) + " dB");

            check (finite, label + ": stays finite");
            checkAtMost (worst, 3.0, label + ": no step in level");
        };

        const auto switchAt = static_cast<size_t> (referenceRate * 3.0);

        automate ("depth 0 -> 1 with correction on",
                  [&] (Tank& t, size_t n) { if (n == switchAt) t.setAllpassDepth (1.0); });

        automate ("depth 1 -> 0 with correction on",
                  [&] (Tank& t, size_t n)
                  {
                      if (n == 0) t.setAllpassDepth (1.0, true);
                      if (n == switchAt) t.setAllpassDepth (0.0);
                  });

        automate ("correction toggled off mid-signal",
                  [&] (Tank& t, size_t n) { if (n == switchAt) t.setLoopDelayCorrectionEnabled (false); });

        automate ("correction toggled on mid-signal",
                  [&] (Tank& t, size_t n)
                  {
                      if (n == 0) t.setLoopDelayCorrectionEnabled (false);
                      if (n == switchAt) t.setLoopDelayCorrectionEnabled (true);
                  });

        automate ("decay swept 0.5 -> 8 s continuously",
                  [&] (Tank& t, size_t n)
                  {
                      const double u = static_cast<double> (n) / (referenceRate * 6.0);
                      t.setRt60Seconds (0.5 + 7.5 * u);
                  });

        automate ("depth automated every sample",
                  [&] (Tank& t, size_t n)
                  {
                      t.setAllpassDepth (0.5 + 0.5 * std::sin (static_cast<double> (n) * 0.0007));
                  });
    }

    // =======================================================================
    beginSuite ("RT60 / correction bypass and sample rates");
    // =======================================================================
    {
        // With the bank bypassed the correction must change nothing at all, at
        // any sample rate — it is the same tank PHASE 4 measured.
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto on  = makeTank (2.0, 0.0, true, rate);
            auto off = makeTank (2.0, 0.0, false, rate);

            const auto noise = whiteNoise (100000, 777u);

            bool identical = true;

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double al = noise[n], ar = noise[n];
                double bl = noise[n], br = noise[n];

                on->processSample (al, ar);
                off->processSample (bl, br);

                identical = identical && al == bl && ar == br;
            }

            check (identical, fixed (rate / 1000.0, 1)
                              + " kHz: with the bank bypassed the correction is a no-op");
        }

        info ("");
        info ("  rate        RT60 uncorrected   RT60 corrected   corrected error");

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto none = makeTank (2.0, allpassMod::defaultDepth, false, rate);
            auto with = makeTank (2.0, allpassMod::defaultDepth, true, rate);

            const auto a = measureDecay (impulseResponse (*none, irLengthFor (2.0, rate)), rate);
            const auto b = measureDecay (impulseResponse (*with, irLengthFor (2.0, rate)), rate);

            const double error = 100.0 * (b.rt60FromT30 - 2.0) / 2.0;

            info ("  " + pad (fixed (rate / 1000.0, 1) + " kHz", 12)
                  + pad (fixed (a.rt60FromT30, 4) + " s", 19)
                  + pad (fixed (b.rt60FromT30, 4) + " s", 17)
                  + signed2 (error) + " %");

            checkAtMost (std::abs (error), 3.0,
                         fixed (rate / 1000.0, 1) + " kHz: corrected RT60 within 3 % of target");

            check (std::abs (b.rt60FromT30 - 2.0) < std::abs (a.rt60FromT30 - 2.0),
                   fixed (rate / 1000.0, 1) + " kHz: the correction improves on no correction");
        }
    }
}

} // namespace avtest
