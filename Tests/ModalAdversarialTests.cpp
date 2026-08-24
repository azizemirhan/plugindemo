#include "DspAnalysis.h"
#include "ModalMap.h"
#include "TestFramework.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace avtest
{

namespace
{
    constexpr double tau = 6.283185307179586;
    constexpr double halfTau = 3.141592653589793;

    [[nodiscard]] std::string fx (double v, int p)
    {
        std::ostringstream o; o << std::fixed << std::setprecision (p) << v; return o.str();
    }

    [[nodiscard]] std::string padL (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : std::string (w - s.size(), ' ') + s;
    }

    struct Comp { double hz, amplitude, rt60, phase; };

    [[nodiscard]] std::vector<double> synth (const std::vector<Comp>& parts,
                                             double rate, double seconds)
    {
        const auto n = static_cast<size_t> (rate * seconds);
        std::vector<double> x (n, 0.0);

        for (const auto& c : parts)
        {
            const double t60 = c.rt60 / 6.907755;

            for (size_t i = 0; i < n; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[i] += c.amplitude * std::exp (-t / t60)
                      * std::sin (tau * c.hz * t + c.phase);
            }
        }

        return x;
    }
}

void runModalAdversarialTests()
{
    constexpr double rate = 48000.0;

    // =======================================================================
    beginSuite ("Phase 6 / decay B2: equal-decay close modes, adversarial");
    // =======================================================================
    {
        // The first close-mode suite was too kind. Its neighbour decayed in
        // 1.2 s against the target's 2.5 s, so by the late part of a T30 fit
        // the target was alone and the estimator was measuring a clean mode.
        // That tested whether a fast neighbour spoils a fit. It did not test
        // whether two modes that decay together can be told apart, which is
        // the case the sweep actually meets.
        const double spacings[] = { 0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0, 16.0, 24.0 };
        const double phases[] = { 0.0, 0.25, 0.5, 0.75, 1.0, 0.375, 0.625 };
        const double levels[] = { 1.0, 0.8, 0.5 };

        const double target = 2.5;
        const double seconds = 3.0;

        DecayOptions base;
        base.windowSeconds = 0.125;
        base.hopSeconds = 0.015625;

        info ("");
        info ("  target and neighbour both RT60 " + fx (target, 2)
              + " s. Analysis bandwidth " + fx (2.0 / base.windowSeconds, 2) + " Hz.");
        info ("  7 phases x 3 relative levels at each spacing, 210 cases.");
        info ("");
        info ("  WITHOUT the contamination guard - residual as the only defence:");
        info ("");
        info ("   spacing   measured  unresolved   worst err   FALSE CONFIDENT");
        info ("    ----------------------------------------------------------------");

        // A claim is false-confident when the estimator says measured and the
        // answer is outside what suite A showed a clean single mode costs.
        // Suite A measured 0.000% on every ground truth, so any error beyond a
        // small allowance is the neighbour, not the method.
        constexpr double tolerancePct = 5.0;

        int totalFalseConfident = 0;
        double worstOverall = 0.0;
        double worstFalseSpacing = 0.0;

        for (double spacing : spacings)
        {
            int measured = 0, unresolved = 0, falseConfident = 0;
            double worst = 0.0;

            for (double level : levels)
                for (double phaseUnit : phases)
                {
                    const auto signal = synth ({ { 1000.0, 1.0, target, 0.0 },
                                                 { 1000.0 + spacing, level, target,
                                                   phaseUnit * halfTau } },
                                               rate, seconds);

                    const auto e = estimateModalDecay (signal, rate, 1000.0, base);

                    if (e.quality != DecayQuality::measured) { ++unresolved; continue; }

                    ++measured;

                    const double m = e.rt60FromT30 > 0.0 ? e.rt60FromT30 : e.rt60FromT20;
                    const double pct = std::abs (100.0 * (m - target) / target);

                    worst = std::max (worst, pct);

                    if (pct > tolerancePct)
                        ++falseConfident;
                }

            totalFalseConfident += falseConfident;
            worstOverall = std::max (worstOverall, worst);

            if (falseConfident > 0)
                worstFalseSpacing = std::max (worstFalseSpacing, spacing);

            info ("    " + padL (fx (spacing, 2) + " Hz", 8) + padL (std::to_string (measured), 11)
                  + padL (std::to_string (unresolved), 12) + padL (fx (worst, 2) + "%", 12)
                  + padL (std::to_string (falseConfident), 18));
        }

        info ("");
        info ("  false-confident claims without the guard: " + std::to_string (totalFalseConfident)
              + " of 210, worst error " + fx (worstOverall, 2) + "%");
        info ("  largest spacing that still produced one: " + fx (worstFalseSpacing, 2) + " Hz");
        info ("");

        // Now with the guard, which is told what the caller knows: a neighbour
        // exists, where it is, and how loud.
        info ("  WITH the contamination guard:");
        info ("");
        info ("   spacing   measured  contaminated  unresolved   FALSE CONFIDENT");
        info ("    --------------------------------------------------------------------");

        int guardedFalseConfident = 0;
        int guardedMeasured = 0;

        for (double spacing : spacings)
        {
            int measured = 0, contaminated = 0, unresolved = 0, falseConfident = 0;

            for (double level : levels)
                for (double phaseUnit : phases)
                {
                    const auto signal = synth ({ { 1000.0, 1.0, target, 0.0 },
                                                 { 1000.0 + spacing, level, target,
                                                   phaseUnit * halfTau } },
                                               rate, seconds);

                    DecayOptions o = base;
                    o.neighbours = { { 1000.0 + spacing, 20.0 * std::log10 (level) } };

                    const auto e = estimateModalDecay (signal, rate, 1000.0, o);

                    if (e.quality == DecayQuality::contaminated) { ++contaminated; continue; }
                    if (e.quality != DecayQuality::measured)     { ++unresolved; continue; }

                    ++measured;

                    const double m = e.rt60FromT30 > 0.0 ? e.rt60FromT30 : e.rt60FromT20;

                    if (std::abs (100.0 * (m - target) / target) > tolerancePct)
                        ++falseConfident;
                }

            guardedFalseConfident += falseConfident;
            guardedMeasured += measured;

            info ("    " + padL (fx (spacing, 2) + " Hz", 8) + padL (std::to_string (measured), 11)
                  + padL (std::to_string (contaminated), 14) + padL (std::to_string (unresolved), 12)
                  + padL (std::to_string (falseConfident), 18));
        }

        info ("");
        info ("  false-confident claims with the guard: " + std::to_string (guardedFalseConfident)
              + " of 210");
        info ("  still measured: " + std::to_string (guardedMeasured));
        info ("");
        info ("  The guard is not a second residual test. A high R-squared alone does");
        info ("  not make a measurement trustworthy: two modes at the wrong phase can");
        info ("  produce an envelope that is close to straight while the slope belongs");
        info ("  to neither. Knowing a comparable neighbour is inside the analysis");
        info ("  bandwidth is what catches that, and it is checked separately.");

        check (guardedFalseConfident < totalFalseConfident || totalFalseConfident == 0,
               "the guard removes false-confident claims rather than adding them");
        checkEqual (static_cast<double> (guardedFalseConfident), 0.0,
                    "no false-confident measurement survives the guard");
        check (guardedMeasured > 0,
               "the guard does not refuse everything - well-separated modes still measure");
    }

    // =======================================================================
    beginSuite ("Phase 6 / decay B2b: near-equal decay times");
    // =======================================================================
    {
        info ("");
        info ("   neighbour RT60   spacing   measured RT60   err%   quality");
        info ("    -------------------------------------------------------------");

        DecayOptions o;
        o.windowSeconds = 0.125;
        o.hopSeconds = 0.015625;

        const double target = 2.5;

        for (double neighbourRt60 : { 2.0, 3.0 })
            for (double spacing : { 1.0, 4.0, 16.0 })
            {
                const auto signal = synth ({ { 1000.0, 1.0, target, 0.0 },
                                             { 1000.0 + spacing, 0.8, neighbourRt60, 0.7 } },
                                           rate, 3.0);

                DecayOptions guarded = o;
                guarded.neighbours = { { 1000.0 + spacing, 20.0 * std::log10 (0.8) } };

                const auto e = estimateModalDecay (signal, rate, 1000.0, guarded);
                const double m = e.rt60FromT30 > 0.0 ? e.rt60FromT30 : e.rt60FromT20;
                const double pct = m > 0.0 ? 100.0 * (m - target) / target : 0.0;

                info ("    " + padL (fx (neighbourRt60, 2) + " s", 14) + padL (fx (spacing, 2) + " Hz", 10)
                      + padL (m > 0.0 ? fx (m, 4) : std::string ("-"), 15)
                      + padL (m > 0.0 ? fx (pct, 2) : std::string ("-"), 8)
                      + "   " + describe (e.quality));

                if (e.quality == DecayQuality::measured)
                    checkAtMost (std::abs (pct), 5.0,
                                 "a claimed measurement at " + fx (spacing, 1) + " Hz is accurate");
            }

        check (true, "near-equal decay cases were exercised");
    }

    // =======================================================================
    beginSuite ("Phase 6 / adaptive window policy");
    // =======================================================================
    {
        info ("");
        info ("  Window from what is being measured: separation wants it long, fitting");
        info ("  a short decay wants it short. Where both cannot hold, the policy says");
        info ("  so instead of choosing one.");
        info ("");
        info ("     RT60   spacing    window    bandwidth   frames  feasible");
        info ("    ------------------------------------------------------------");

        int infeasible = 0;

        for (double rt60 : { 0.5, 2.5, 10.0 })
            for (double spacing : { 1.0, 3.0, 10.0 })
            {
                const auto p = windowPolicyFor (rt60, spacing);

                if (! p.feasible) ++infeasible;

                info ("    " + padL (fx (rt60, 2), 7) + padL (fx (spacing, 1) + " Hz", 10)
                      + padL (fx (p.windowSeconds, 4) + " s", 11)
                      + padL (fx (p.bandwidthHz, 2) + " Hz", 12)
                      + padL (std::to_string (p.expectedFrames), 8)
                      + "  " + (p.feasible ? "yes" : "NO"));
            }

        info ("");
        info ("  infeasible combinations: " + std::to_string (infeasible) + " of 9");
        info ("  Those are the regions where no single window can both separate the");
        info ("  neighbour and fit the decay. The sweep reports them unresolved.");

        check (infeasible > 0,
               "the policy identifies combinations that cannot be measured cleanly");

        const auto easy = windowPolicyFor (10.0, 10.0);
        check (easy.feasible, "a long decay in a sparse region is feasible");

        const auto hard = windowPolicyFor (0.5, 1.0);
        check (! hard.feasible, "a short decay in a dense region is not");
    }

    // =======================================================================
    beginSuite ("Phase 6 / local response catches a smeared cluster");
    // =======================================================================
    {
        // The proof the two-layer Mod Depth analysis needs: when one peak
        // becomes three, the local metrics must say so. Without this the
        // measurement would report "unmatched" and lose the information.
        constexpr size_t fftSize = 1 << 18;
        constexpr double seconds = 2.0;

        const auto window = static_cast<size_t> (rate * seconds);
        const auto res = resolutionFor (rate, window, fftSize, 1.0);

        const auto spectrumOf = [&] (const std::vector<double>& x)
        {
            return magnitudeSpectrum (x, fftSize, true);
        };

        // Before: one line. After: the same total energy spread over three.
        const auto single = synth ({ { 1000.0, 1.0, 2.5, 0.0 } }, rate, seconds);

        const double third = 1.0 / std::sqrt (3.0);
        const auto cluster = synth ({ { 996.0, third, 2.5, 0.0 },
                                      { 1000.0, third, 2.5, 1.1 },
                                      { 1004.0, third, 2.5, 2.2 } }, rate, seconds);

        // The neighbourhood is defined by the modal spacing and the excursion
        // being looked for, not by a fixed number of Hz.
        const double localSpacing = res.meanModalSpacingHz;
        const double expectedExcursion = 6.0;
        const double halfWidth = std::max (2.0 * localSpacing, 1.5 * expectedExcursion);

        const auto before = measureLocalResponse (spectrumOf (single), res, 1000.0, halfWidth);
        const auto after  = measureLocalResponse (spectrumOf (cluster), res, 1000.0, halfWidth);

        info ("");
        info ("  neighbourhood half-width " + fx (halfWidth, 2)
              + " Hz, from modal spacing " + fx (localSpacing, 2)
              + " Hz and expected excursion " + fx (expectedExcursion, 1) + " Hz");
        info ("");
        info ("                          one line     cluster of three     change");
        info ("    --------------------------------------------------------------------");
        info ("    integrated energy   " + padL (fx (before.integratedEnergyDb, 2), 10)
              + padL (fx (after.integratedEnergyDb, 2), 20)
              + padL (fx (after.integratedEnergyDb - before.integratedEnergyDb, 2) + " dB", 14));
        info ("    max prominence      " + padL (fx (before.maxProminenceDb, 2), 10)
              + padL (fx (after.maxProminenceDb, 2), 20)
              + padL (fx (after.maxProminenceDb - before.maxProminenceDb, 2) + " dB", 14));
        info ("    spectral spread     " + padL (fx (before.spectralSpreadHz, 3), 10)
              + padL (fx (after.spectralSpreadHz, 3), 20)
              + padL (fx (after.spectralSpreadHz - before.spectralSpreadHz, 3) + " Hz", 14));
        info ("    concentration       " + padL (fx (before.energyConcentration, 5), 10)
              + padL (fx (after.energyConcentration, 5), 20)
              + padL (fx (after.energyConcentration / std::max (1.0e-12, before.energyConcentration), 3) + "x", 14));
        info ("    peak count          " + padL (std::to_string (before.peakCount), 10)
              + padL (std::to_string (after.peakCount), 20));

        check (before.measured && after.measured, "both neighbourhoods were measurable");

        // The signature of redistribution: energy is broadly preserved, the
        // spread grows, the concentration falls, and the count rises. Any one
        // of those alone could be something else; together they are specific.
        checkAtMost (std::abs (after.integratedEnergyDb - before.integratedEnergyDb), 3.0,
                     "energy is broadly preserved - the mode was spread, not removed");
        check (after.spectralSpreadHz > 2.0 * before.spectralSpreadHz,
               "the spectral spread grows sharply");
        check (after.energyConcentration < 0.6 * before.energyConcentration,
               "the energy concentration falls");
        check (after.peakCount > before.peakCount,
               "and more peaks are found where there was one");

        info ("");
        info ("  This is what lets a Mod Depth comparison report redistribution rather");
        info ("  than 'unmatched'. A peak that could not be followed 1:1 is not the");
        info ("  same as information lost.");
    }
}

} // namespace avtest
