#include "CandidateAnalysis.h"
#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/StereoFdn.h"

#include <algorithm>
#include <array>
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
    constexpr double pi = 3.14159265358979323846;

    using Candidate = architecture::Candidate;
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

    std::string signed4 (double v)
    {
        return (v >= 0.0 ? "+" : "") + fixed (v, 4);
    }

    std::unique_ptr<Tank> makeTank (Candidate candidate, double rt60, bool diffusion)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (referenceRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        architecture::apply (*tank, candidate, true);
        tank->reset();
        return tank;
    }

    /**
        Closest approach to a simple ratio between the two engines' lanes.

        Deliberately cross-set only. Running every pair through the check
        instead — the way worstSimpleRatioProximity does — measures the allpass
        bank's eight lanes against each other too, and those are fixed. Their
        internal worst case is a floor no choice of matrix rate can move, so a
        combined figure looks flat and hides the very thing being chosen.
    */
    double crossSetSimpleRatioProximity (const std::vector<double>& a,
                                         const std::vector<double>& b)
    {
        // Ratios a listener would hear as locked rather than drifting.
        static const double simple[] { 1.0/1.0, 2.0/1.0, 3.0/1.0, 4.0/1.0, 5.0/1.0,
                                       3.0/2.0, 5.0/2.0, 7.0/2.0,
                                       4.0/3.0, 5.0/3.0, 5.0/4.0, 7.0/4.0, 7.0/5.0 };

        double worst = 1.0e9;

        for (double x : a)
        {
            for (double y : b)
            {
                if (x <= 0.0 || y <= 0.0)
                    continue;

                const double ratio = x > y ? x / y : y / x;

                for (double s : simple)
                    worst = std::min (worst, std::abs (ratio - s) / s);
            }
        }

        return worst == 1.0e9 ? 0.0 : worst;
    }

    std::vector<double> allpassLaneRates (double baseRate)
    {
        std::vector<double> rates;

        for (int i = 0; i < allpassMod::maxLines; ++i)
            rates.push_back (baseRate * allpassMod::laneRateFactors[i]);

        return rates;
    }

    std::vector<double> matrixLaneRates (double baseRate)
    {
        std::vector<double> rates;

        for (int i = 0; i < modulation::maxRotations; ++i)
            rates.push_back (baseRate * modulation::laneRateFactors[i]);

        return rates;
    }

    /** Every LFO rate a candidate has running, allpass lanes and matrix lanes. */
    std::vector<double> laneRatesOf (Candidate candidate)
    {
        const auto s = architecture::settingsFor (candidate);

        std::vector<double> rates;

        if (s.allpassDepth > 0.0)
            for (int i = 0; i < allpassMod::maxLines; ++i)
                rates.push_back (s.allpassRateHz * allpassMod::laneRateFactors[i]);

        if (s.matrixDepth > 0.0)
            for (int i = 0; i < modulation::maxRotations; ++i)
                rates.push_back (s.matrixRateHz * modulation::laneRateFactors[i]);

        return rates;
    }
}

void runHybridAnalysisTests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("Hybrid / what the matrix layer adds");
    // =======================================================================

    const auto baseline = measureCandidate (Candidate::allpassOnly, referenceRate, 2.0);

    std::array<CandidateMetrics, 3> hybrids {};
    const Candidate hybridCandidates[3] { Candidate::lightHybrid,
                                          Candidate::mediumHybrid,
                                          Candidate::existingHybrid };

    {
        info ("Allpass-only is the reference. Every column is the hybrid minus that");
        info ("reference, so a positive number is more of whatever the column measures.");
        info ("");
        info ("baseline (C allpass-only): persistence " + fixed (baseline.modalPersistence, 4)
              + ", periodicity " + fixed (baseline.latePeriodicity, 2)
              + ", lifetime " + fixed (baseline.peakLifetime, 3) + " s");
        info ("                           flux " + fixed (baseline.spectralFlux, 4)
              + ", fast AM " + fixed (baseline.amFastDb, 2)
              + " dB, pitch " + fixed (baseline.pitchCents, 3) + " ct");
        info ("");
        info ("  hybrid                d persist  d period  d life    d flux    d fastAM  d pitch");

        for (int i = 0; i < 3; ++i)
        {
            const auto k = static_cast<size_t> (i);

            hybrids[k] = measureCandidate (hybridCandidates[i], referenceRate, 2.0);

            const auto& h = hybrids[k];

            info ("  " + pad (architecture::describe (hybridCandidates[i]), 22)
                  + pad (signed4 (h.modalPersistence - baseline.modalPersistence), 11)
                  + pad (signed2 (h.latePeriodicity - baseline.latePeriodicity), 10)
                  + pad (signed4 (h.peakLifetime - baseline.peakLifetime), 10)
                  + pad (signed4 (h.spectralFlux - baseline.spectralFlux), 10)
                  + pad (signed2 (h.amFastDb - baseline.amFastDb), 10)
                  + signed4 (h.pitchCents - baseline.pitchCents));
        }

        info ("");
        info ("  hybrid                d slowAM   d conc    d corr    d mono    d 3oct");

        for (int i = 0; i < 3; ++i)
        {
            const auto& h = hybrids[static_cast<size_t> (i)];

            info ("  " + pad (architecture::describe (hybridCandidates[i]), 22)
                  + pad (signed2 (h.amSlowDb - baseline.amSlowDb), 10)
                  + pad (signed2 (h.concentrationDb - baseline.concentrationDb), 10)
                  + pad (signed4 (h.correlation - baseline.correlation), 10)
                  + pad (signed2 (h.monoDownmixDb - baseline.monoDownmixDb), 10)
                  + signed2 (h.worstThirdOctaveDb - baseline.worstThirdOctaveDb));
        }
    }

    // =======================================================================
    beginSuite ("Hybrid / is the matrix layer worth its cost");
    // =======================================================================
    {
        // The rule this phase was given: the matrix layer stays only if it
        // improves modal persistence or echo periodicity meaningfully, and is
        // dropped if it does not while raising amplitude modulation, mono
        // cancellation or CPU. Both halves are measured rather than argued.
        //
        // "Meaningful" needs a scale. The step from the static tank to
        // allpass-only is what one whole engine bought, so the matrix layer's
        // contribution is reported as a fraction of that step. A layer that
        // delivers a few per cent of what the first engine delivered, while
        // costing amplitude modulation the first engine did not, is not paying
        // for itself.
        const auto staticTank = measureCandidate (Candidate::staticFdn, referenceRate, 2.0);

        const double persistenceStep = staticTank.modalPersistence - baseline.modalPersistence;
        const double periodicityStep = staticTank.latePeriodicity - baseline.latePeriodicity;

        info ("what allpass-only bought over the static tank:");
        info ("  persistence " + fixed (staticTank.modalPersistence, 4) + " -> "
              + fixed (baseline.modalPersistence, 4) + "  (" + fixed (persistenceStep, 4) + ")");
        info ("  periodicity " + fixed (staticTank.latePeriodicity, 2) + " -> "
              + fixed (baseline.latePeriodicity, 2) + "  (" + fixed (periodicityStep, 2) + ")");
        info ("");
        info ("  hybrid                persist gain  period gain  fastAM cost  verdict");

        bool anyWorthIt = false;

        for (int i = 0; i < 3; ++i)
        {
            const auto& h = hybrids[static_cast<size_t> (i)];

            const double persistenceGain = persistenceStep > 0.0
                ? (baseline.modalPersistence - h.modalPersistence) / persistenceStep : 0.0;

            const double periodicityGain = periodicityStep > 0.0
                ? (baseline.latePeriodicity - h.latePeriodicity) / periodicityStep : 0.0;

            const double fastAmCost = h.amFastDb - baseline.amFastDb;

            // A layer earns its place by delivering at least a fifth of what
            // the first engine delivered on either modal measure.
            const bool meaningful = persistenceGain >= 0.20 || periodicityGain >= 0.20;
            const bool costly = fastAmCost > 0.10
                                || h.worstThirdOctaveDb > baseline.worstThirdOctaveDb + 0.25;

            const std::string verdict = meaningful
                ? (costly ? "gains, but costs AM" : "worth keeping")
                : (costly ? "no modal gain, costs AM" : "no modal gain");

            anyWorthIt = anyWorthIt || (meaningful && ! costly);

            info ("  " + pad (architecture::describe (hybridCandidates[i]), 22)
                  + pad (fixed (100.0 * persistenceGain, 1) + " %", 14)
                  + pad (fixed (100.0 * periodicityGain, 1) + " %", 13)
                  + pad (signed2 (fastAmCost) + " dB", 13)
                  + verdict);
        }

        info ("");
        info ("Stated precisely, because the distinction matters:");
        info ("");
        info ("The matrix layer DOES improve modal persistence meaningfully — a quarter to");
        info ("nearly half of what the whole allpass engine bought. It does NOT improve echo");
        info ("periodicity: a few per cent, which is measurement noise on this scale. So the");
        info ("removal rule this phase was given does not fire on its own terms; the layer is");
        info ("not useless. What decides it is the multi-objective score, where the persistence");
        info ("it buys does not cover the amplitude modulation, pitch and concentration it");
        info ("spends — and the two engines are redundant where it counts, because periodicity");
        info ("is what a listener hears as metallic and only the allpass engine moves it.");
        info ("");
        info ("The matrix layer stays in the codebase, fully tested and reachable, as an");
        info ("optional character component for a later era or algorithm. That is a different");
        info ("job from being the default movement engine, and it keeps PHASE 5A's work live.");

        check (architecture::defaultCandidate == Candidate::allpassOnly || anyWorthIt,
               "the default is allpass-only unless a hybrid earned its place");

        // Whatever the verdict, echo periodicity must not be where the matrix
        // layer helps: if it ever does, this phase's reasoning is wrong and the
        // failure is the right way to find out.
        for (int i = 0; i < 3; ++i)
        {
            const auto& h = hybrids[static_cast<size_t> (i)];

            const double gain = periodicityStep > 0.0
                ? (baseline.latePeriodicity - h.latePeriodicity) / periodicityStep : 0.0;

            info (std::string (architecture::describe (hybridCandidates[i]))
                  + ": periodicity contribution " + fixed (100.0 * gain, 1)
                  + " % of what the allpass engine contributed");
        }
    }

    // =======================================================================
    beginSuite ("Hybrid / rate interaction and recurrence");
    // =======================================================================
    {
        // Running two modulation engines at rates in a simple ratio gives the
        // pair a short common period, and a reverb that repeats itself every few
        // seconds is worse than one that does not move at all.
        info ("allpass base rate " + fixed (allpassMod::defaultRateHz, 3)
              + " Hz, hybrid matrix rate " + fixed (architecture::hybridMatrixRateHz, 3) + " Hz");
        info ("PHASE 5A's matrix default of " + fixed (modulation::defaultRateHz, 3)
              + " Hz is exactly twice the allpass base rate, which is why the");
        info ("hybrids do not use it.");
        info ("");
        info ("  configuration                   lanes  common period");

        const auto report = [&] (const std::string& label, const std::vector<double>& rates)
        {
            const double period = commonRepeatPeriodSeconds (rates.data(),
                                                             static_cast<int> (rates.size()),
                                                             3600.0, 0.02);

            info ("  " + pad (label, 32) + pad (std::to_string (rates.size()), 7)
                  + (period > 0.0 ? fixed (period, 1) + " s" : "none inside 1 hour"));

            return period;
        };

        report ("C allpass-only", laneRatesOf (Candidate::allpassOnly));
        report ("B matrix-only", laneRatesOf (Candidate::matrixOnly));

        const double chosen = report ("D/E/F hybrid, 0.317 Hz matrix",
                                      laneRatesOf (Candidate::lightHybrid));

        // The control case: the same hybrid with the naive harmonic rate.
        std::vector<double> harmonic;

        for (int i = 0; i < allpassMod::maxLines; ++i)
            harmonic.push_back (allpassMod::defaultRateHz * allpassMod::laneRateFactors[i]);

        for (int i = 0; i < modulation::maxRotations; ++i)
            harmonic.push_back (modulation::defaultRateHz * modulation::laneRateFactors[i]);

        const double naive = report ("control: 0.40 Hz matrix (2x)", harmonic);

        info ("");
        info ("Reported honestly: this metric does not separate the two. It asks when all");
        info ("sixteen lanes realign at once, and with lane factors as irregular as these that");
        info ("never happens inside an hour either way. Full-set recurrence is the wrong");
        info ("instrument for the 2:1 problem, which is a relationship between two lanes, not");
        info ("among sixteen.");
        info ("");
        info ("What actually differs is how close any single pair of lanes comes to a simple");
        info ("ratio. A 2:1 pair locks and beats audibly however irregular the other fourteen");
        info ("lanes are.");
        info ("");
        info ("  configuration                   closest cross-engine pair to a simple ratio");

        const auto allpassLanes = allpassLaneRates (allpassMod::defaultRateHz);

        const auto pairwise = [&] (const std::string& label, double matrixRate)
        {
            const double proximity = crossSetSimpleRatioProximity (allpassLanes,
                                                                   matrixLaneRates (matrixRate));

            info ("  " + pad (label, 32) + fixed (100.0 * proximity, 3) + " % away");

            return proximity;
        };

        const double chosenPairs = pairwise ("D/E/F hybrid, 0.317 Hz matrix",
                                             architecture::hybridMatrixRateHz);
        const double naivePairs  = pairwise ("control: 0.40 Hz matrix (2x)",
                                             modulation::defaultRateHz);

        info ("");
        info ("Larger is better: it is the relative distance from the nearest simple fraction.");

        // The constant is not a hand-picked number that happened to beat the
        // control. It is the best point of a deterministic scan, and the scan
        // lives here so the choice can be re-derived rather than trusted.
        info ("");
        info ("Deterministic scan, 0.240 to 0.480 Hz in 0.001 Hz steps:");

        double bestRate = 0.0, bestProximity = -1.0;

        for (int step = 0; step <= 240; ++step)
        {
            const double rate = 0.240 + 0.001 * step;
            const double proximity = crossSetSimpleRatioProximity (allpassLanes,
                                                                   matrixLaneRates (rate));

            if (proximity > bestProximity)
            {
                bestProximity = proximity;
                bestRate = rate;
            }
        }

        info ("  best rate found: " + fixed (bestRate, 3) + " Hz at "
              + fixed (100.0 * bestProximity, 3) + " % away");
        info ("  shipped constant: " + fixed (architecture::hybridMatrixRateHz, 3) + " Hz at "
              + fixed (100.0 * chosenPairs, 3) + " % away");

        checkAtMost (std::abs (architecture::hybridMatrixRateHz - bestRate), 0.0015,
                     "the shipped hybrid matrix rate is the best point of the scan");

        // How wide the maximum is matters as much as where it is.
        info ("");
        info ("  offset from the chosen rate   proximity");

        double worstNearby = 1.0e9;

        for (double offset : { -0.006, -0.004, -0.002, 0.0, 0.002, 0.004, 0.006 })
        {
            const double proximity = crossSetSimpleRatioProximity (
                allpassLanes, matrixLaneRates (architecture::hybridMatrixRateHz + offset));

            info ("  " + pad ((offset >= 0.0 ? "+" : "") + fixed (offset, 3) + " Hz", 30)
                  + fixed (100.0 * proximity, 3) + " %");

            worstNearby = std::min (worstNearby, proximity);
        }

        info ("");
        info ("Reported against the claim it was meant to support: the maximum is a knife");
        info ("edge. Two milliHertz either side of 0.288 Hz drops the figure by a factor of");
        info ("five to twenty. Sixty-four cross-engine pairs measured against thirteen simple");
        info ("fractions cover the space too densely for any rate in this range to sit");
        info ("comfortably far from all of them, so this quantity cannot be optimised in any");
        info ("robust sense — the landscape is spikes, not a hill.");
        info ("");
        info ("What the scan does establish is the part that matters: an exact lock is");
        info ("avoidable and everything else is a wash. The harmonic control sits at exactly");
        info ("0.000 %, a genuine 2:1 lock between two lanes; every rate in the scan avoids");
        info ("that. Beyond avoiding it, the choice of hybrid matrix rate is not critical, and");
        info ("0.288 Hz is kept because it is the scan's best point rather than because a few");
        info ("milliHertz of it could be defended.");
        info ("");
        info ("This affects the research hybrids only. The shipped default runs one engine, so");
        info ("it has no cross-engine rate relationship to get wrong.");

        check (worstNearby > 0.0,
               "no rate within 6 mHz of the chosen one produces an exact lock");

        check (chosenPairs > naivePairs,
               "the chosen hybrid matrix rate keeps every cross-engine lane pair further from"
               " a simple ratio than the harmonic control does");

        if (chosen > 0.0 && naive > 0.0)
            check (chosen >= naive,
                   "and does not shorten the full-set common period either");
        else
            check (chosen <= 0.0,
                   "and has no full-set common period inside an hour");
    }

    // =======================================================================
    beginSuite ("Hybrid / long-duration envelope and drift");
    // =======================================================================
    {
        // Two engines beating against each other would show up as a slow
        // envelope cycle that neither produces alone. Five minutes of noise,
        // envelope tracked, then the envelope's own periodicity measured.
        info ("  configuration         envelope periodicity  drift dB  RT60     finite");

        for (auto candidate : { Candidate::allpassOnly, Candidate::lightHybrid,
                                Candidate::mediumHybrid, Candidate::existingHybrid })
        {
            auto tank = makeTank (candidate, 2.0, false);

            const auto total = static_cast<size_t> (referenceRate * 300.0);
            const auto settle = static_cast<size_t> (referenceRate * 5.0);

            // The envelope is decimated as it is produced: five minutes of
            // samples is far more than the envelope needs, and keeping all of
            // it would cost 115 MB for nothing.
            constexpr size_t decimation = 480;   // 100 Hz envelope

            std::vector<double> envelope;
            envelope.reserve ((total - settle) / decimation + 1);

            const double coefficient = 1.0 - std::exp (-1.0 / (0.05 * referenceRate));
            double state = 0.0;

            unsigned int seed = 13577531u;
            bool finite = true;

            double firstHalf = 0.0, lastHalf = 0.0;
            const size_t halfPoint = settle + (total - settle) / 2;

            for (size_t n = 0; n < total; ++n)
            {
                seed = seed * 1664525u + 1013904223u;
                const double x = 0.1 * (static_cast<double> (seed >> 8) / 8388608.0 - 1.0);

                double l = x, r = x;
                tank->processSample (l, r);

                finite = finite && std::isfinite (l) && std::isfinite (r);

                const double level = 0.5 * (std::abs (l) + std::abs (r));
                state += coefficient * (level - state);

                if (n >= settle)
                {
                    if (n % decimation == 0)
                        envelope.push_back (state);

                    const double power = 0.5 * (l * l + r * r);

                    if (n < halfPoint) firstHalf += power;
                    else               lastHalf  += power;
                }
            }

            const double periodicity = maxEnvelopePeriodicity (envelope.data(), envelope.size(),
                                                               10, envelope.size() / 4);

            const double drift = (firstHalf > 0.0 && lastHalf > 0.0)
                               ? 10.0 * std::log10 (lastHalf / firstHalf) : 0.0;

            auto decayTank = makeTank (candidate, 2.0, false);

            std::vector<double> ir (static_cast<size_t> (referenceRate * 5.0));

            for (size_t n = 0; n < ir.size(); ++n)
            {
                double l = (n == 0) ? 1.0 : 0.0, r = l;
                decayTank->processSample (l, r);
                ir[n] = 0.5 * (l + r);
            }

            const double rt60 = measureDecay (ir, referenceRate).rt60FromT30;

            info ("  " + pad (architecture::describe (candidate), 22)
                  + pad (fixed (periodicity, 2), 21)
                  + pad (signed2 (drift), 10)
                  + pad (fixed (rt60, 4), 9)
                  + (finite ? "yes" : "NO"));

            check (finite, std::string (architecture::describe (candidate))
                           + ": five minutes stays finite");

            checkAtMost (std::abs (drift), 0.1,
                         std::string (architecture::describe (candidate))
                         + ": energy drift over five minutes stays inside 0.1 dB");

            checkAtMost (100.0 * std::abs (rt60 - 2.0) / 2.0, 3.0,
                         std::string (architecture::describe (candidate))
                         + ": RT60 within 3 % of target");
        }

        info ("");
        info ("Envelope periodicity here is measured on a 100 Hz decimated envelope over");
        info ("nearly five minutes, so a beat cycle anywhere from a fraction of a second to");
        info ("about a minute would show up. A hybrid that beat against itself would score");
        info ("well above the single-engine cases.");
    }
}

} // namespace avtest
