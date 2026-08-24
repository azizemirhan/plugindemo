#include "CandidateAnalysis.h"
#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"
#include "DSP/ModulationArchitecture.h"

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

    /**
        Normalises one objective to 0..1 across the candidates being ranked.

        Every objective is on its own scale — persistence is a fraction,
        periodicity is a ratio in the tens, movement is in Hz — so they cannot be
        summed directly. Min-max normalisation within the candidate set puts them
        on a common axis without inventing weights that pretend to know how many
        Hz of drift a point of persistence is worth.
    */
    std::vector<double> normalise (const std::vector<double>& values, bool higherIsBetter)
    {
        std::vector<double> out (values.size(), 0.5);

        const auto [lo, hi] = std::minmax_element (values.begin(), values.end());

        if (values.empty() || *hi - *lo < 1.0e-12)
            return out;

        for (size_t i = 0; i < values.size(); ++i)
        {
            const double t = (values[i] - *lo) / (*hi - *lo);
            out[i] = higherIsBetter ? t : 1.0 - t;
        }

        return out;
    }
}

void runModulationSelectionTests()
{
    // =======================================================================
    beginSuite ("Selection / candidate measurement");
    // =======================================================================

    std::array<CandidateMetrics, architecture::candidateCount> metrics {};
    std::array<ConstraintCheck, architecture::candidateCount> constraints {};

    {
        info ("Seven architectures, measured identically on a fresh tank each time.");
        info ("Movement metrics on an 8 s decay with 32768-sample frames; decay, AM, pitch");
        info ("and concentration on a 2 s tank; stereo with the input diffuser engaged.");
        info ("");
        info ("  candidate             persist  period  life    moveHz  flux    RT60    R^2");

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto candidate = architecture::allCandidates[i];

            metrics[static_cast<size_t> (i)] = measureCandidate (candidate, referenceRate, 2.0);

            const auto& m = metrics[static_cast<size_t> (i)];

            info ("  " + pad (architecture::describe (candidate), 22)
                  + pad (fixed (m.modalPersistence, 4), 9)
                  + pad (fixed (m.latePeriodicity, 2), 8)
                  + pad (fixed (m.peakLifetime, 3), 8)
                  + pad (fixed (m.modalMovementHz, 3), 8)
                  + pad (fixed (m.spectralFlux, 4), 8)
                  + pad (fixed (m.rt60, 4), 8)
                  + fixed (m.decayLinearity, 4));

            check (m.finite, std::string (architecture::describe (candidate)) + ": output stays finite");
        }

        info ("");
        info ("  candidate             AM dB    pitch   concDb  drift    corr     mono dB  3oct dB");

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto& m = metrics[static_cast<size_t> (i)];

            info ("  " + pad (architecture::describe (architecture::allCandidates[i]), 22)
                  + pad (fixed (m.amDepthDb, 2), 9)
                  + pad (fixed (m.pitchCents, 3), 8)
                  + pad (fixed (m.concentrationDb, 1), 8)
                  + pad (fixed (m.energyDriftDb, 3), 9)
                  + pad (fixed (m.correlation, 4), 9)
                  + pad (fixed (m.monoDownmixDb, 2), 9)
                  + fixed (m.worstThirdOctaveDb, 2));
        }

        // A single AM figure treats a slow swell and a fast tremolo as the same
        // defect. They are not, and ranking on the combined number penalises
        // whichever architecture happens to modulate more slowly.
        info ("");
        info ("  candidate             AM total  AM <1Hz  AM >1Hz  dominant");

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto& m = metrics[static_cast<size_t> (i)];

            info ("  " + pad (architecture::describe (architecture::allCandidates[i]), 22)
                  + pad (fixed (m.amDepthDb, 2) + " dB", 10)
                  + pad (fixed (m.amSlowDb, 2) + " dB", 9)
                  + pad (fixed (m.amFastDb, 2) + " dB", 9)
                  + fixed (m.amDominantHz, 3) + " Hz");
        }
    }

    // =======================================================================
    beginSuite ("Selection / hard constraints");
    // =======================================================================
    {
        info ("A candidate that fails any of these is out of the running whatever it scores");
        info ("on movement. Kept separate from the ranking on purpose.");
        info ("");
        // The chorus constraint is relative to the static tank measured in this
        // same run, not to a number written down in a previous one.
        const double staticConcentration = metrics[0].concentrationDb;

        info ("static tank spectral concentration: " + fixed (staticConcentration, 1)
              + " dB; a candidate may give away at most "
              + fixed (chorusConcentrationDropDb, 1) + " dB of it");
        info ("");
        info ("  candidate             drift  RT60   mono   pitch  finite  chorus  verdict");

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto& m = metrics[static_cast<size_t> (i)];
            const auto c = checkConstraints (m, staticConcentration);

            constraints[static_cast<size_t> (i)] = c;

            const auto mark = [] (bool ok) { return std::string (ok ? "ok" : "FAIL"); };

            info ("  " + pad (architecture::describe (architecture::allCandidates[i]), 22)
                  + pad (mark (c.driftOk), 7)
                  + pad (mark (c.rt60Ok), 7)
                  + pad (mark (c.monoOk), 7)
                  + pad (mark (c.pitchOk), 7)
                  + pad (mark (c.finiteOk), 8)
                  + pad (mark (c.chorusOk), 8)
                  + (c.passes() ? "eligible" : "rejected: " + c.firstFailure()));
        }

        const auto defaultIndex = static_cast<size_t> (
            std::distance (std::begin (architecture::allCandidates),
                           std::find (std::begin (architecture::allCandidates),
                                      std::end (architecture::allCandidates),
                                      architecture::defaultCandidate)));

        check (constraints[defaultIndex].passes(),
               "the selected default architecture satisfies every hard constraint");

        info ("");
        info ("The static tank is expected to fail nothing and win nothing: it is the");
        info ("reference the others have to beat, not a candidate for shipping.");
    }

    // =======================================================================
    beginSuite ("Selection / multi-objective ranking");
    // =======================================================================
    {
        // Objectives are grouped by the phenomenon they describe, not listed
        // one per metric. Modal persistence and peak lifetime are two views of
        // the same thing — whether peaks stand still — and summing them as
        // separate objectives would weight that phenomenon twice while echo
        // periodicity, a different failure mode entirely, counted once.
        std::vector<double> persistence, periodicity, lifetime, movement, flux;
        std::vector<double> amFast, amSlow, pitch, concentration;

        for (const auto& m : metrics)
        {
            persistence.push_back (m.modalPersistence);
            periodicity.push_back (m.latePeriodicity);
            lifetime.push_back (m.peakLifetime);
            movement.push_back (m.modalMovementHz);
            flux.push_back (m.spectralFlux);
            amFast.push_back (m.amFastDb);
            amSlow.push_back (m.amSlowDb);
            pitch.push_back (m.pitchCents);
            concentration.push_back (m.concentrationDb);
        }

        const auto nPersistence   = normalise (persistence, false);
        const auto nPeriodicity   = normalise (periodicity, false);
        const auto nLifetime      = normalise (lifetime, false);
        const auto nMovement      = normalise (movement, true);
        const auto nFlux          = normalise (flux, true);
        const auto nAmFast        = normalise (amFast, false);
        const auto nAmSlow        = normalise (amSlow, false);
        const auto nPitch         = normalise (pitch, false);
        const auto nConcentration = normalise (concentration, true);

        std::array<double, architecture::candidateCount> stasis {}, echoing {}, change {}, cost {};

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto k = static_cast<size_t> (i);

            stasis[k]  = 0.5 * (nPersistence[k] + nLifetime[k]);
            echoing[k] = nPeriodicity[k];
            change[k]  = 0.5 * (nMovement[k] + nFlux[k]);

            // Fast amplitude modulation is weighted above slow: a tremolo is a
            // defect, a swell over several seconds is not the same complaint.
            cost[k] = (0.30 * nAmFast[k] + 0.10 * nAmSlow[k]
                       + 0.30 * nPitch[k] + 0.30 * nConcentration[k]);
        }

        info ("Objectives are min-max normalised across the candidate set and grouped by");
        info ("phenomenon: modal stasis (persistence + lifetime), echo periodicity, and");
        info ("spectral change (movement + flux). Cost is fast AM, slow AM, pitch, concentration.");
        info ("");
        info ("  candidate             stasis  echo    change  cost    score   eligible");

        std::vector<std::pair<double, int>> ranked;

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto k = static_cast<size_t> (i);

            const double movementScore = (stasis[k] + echoing[k] + change[k]) / 3.0;
            const double score = 0.60 * movementScore + 0.40 * cost[k];

            info ("  " + pad (architecture::describe (architecture::allCandidates[i]), 22)
                  + pad (fixed (stasis[k], 4), 8)
                  + pad (fixed (echoing[k], 4), 8)
                  + pad (fixed (change[k], 4), 8)
                  + pad (fixed (cost[k], 4), 8)
                  + pad (fixed (score, 4), 8)
                  + (constraints[k].passes() ? "yes" : "no"));

            if (constraints[k].passes() && architecture::allCandidates[i] != Candidate::staticFdn)
                ranked.emplace_back (score, i);
        }

        std::sort (ranked.begin(), ranked.end(),
                   [] (const auto& a, const auto& b) { return a.first > b.first; });

        check (! ranked.empty(), "at least one modulated candidate is eligible");

        if (! ranked.empty())
        {
            info ("");
            info ("highest-scoring under this weighting: "
                  + std::string (architecture::describe (architecture::allCandidates[ranked.front().second])));
            info ("shipped default: " + std::string (architecture::describe (architecture::defaultCandidate)));
        }

        // -------------------------------------------------------------------
        //  Pareto front
        // -------------------------------------------------------------------
        // A weighted sum can only ever answer "which candidate wins under these
        // weights". Dominance answers something the weights cannot influence:
        // whether a candidate is beaten outright on every objective at once.
        info ("");
        info ("Dominance, over all eight raw objectives at once. A candidate is dominated");
        info ("only if another is at least as good on every one of them and better on one.");
        info ("");
        info ("  candidate             dominated by");

        for (int i = 0; i < architecture::candidateCount; ++i)
        {
            const auto k = static_cast<size_t> (i);

            std::string dominators;

            for (int j = 0; j < architecture::candidateCount; ++j)
            {
                if (i == j)
                    continue;

                const auto q = static_cast<size_t> (j);

                const bool atLeastAsGood =
                    nPersistence[q] >= nPersistence[k] && nPeriodicity[q] >= nPeriodicity[k]
                    && nLifetime[q] >= nLifetime[k] && nMovement[q] >= nMovement[k]
                    && nFlux[q] >= nFlux[k] && nAmFast[q] >= nAmFast[k]
                    && nPitch[q] >= nPitch[k] && nConcentration[q] >= nConcentration[k];

                const bool strictlyBetter =
                    nPersistence[q] > nPersistence[k] || nPeriodicity[q] > nPeriodicity[k]
                    || nLifetime[q] > nLifetime[k] || nMovement[q] > nMovement[k]
                    || nFlux[q] > nFlux[k] || nAmFast[q] > nAmFast[k]
                    || nPitch[q] > nPitch[k] || nConcentration[q] > nConcentration[k];

                if (atLeastAsGood && strictlyBetter)
                    dominators += std::string (dominators.empty() ? "" : ", ")
                                + architecture::describe (architecture::allCandidates[j]);
            }

            info ("  " + pad (architecture::describe (architecture::allCandidates[i]), 22)
                  + (dominators.empty() ? "nothing — on the Pareto front" : dominators));
        }

        const auto defaultIndex = static_cast<size_t> (
            std::distance (std::begin (architecture::allCandidates),
                           std::find (std::begin (architecture::allCandidates),
                                      std::end (architecture::allCandidates),
                                      architecture::defaultCandidate)));

        {
            bool dominated = false;

            for (int j = 0; j < architecture::candidateCount; ++j)
            {
                const auto q = static_cast<size_t> (j);

                if (q == defaultIndex)
                    continue;

                const bool atLeastAsGood =
                    nPersistence[q] >= nPersistence[defaultIndex]
                    && nPeriodicity[q] >= nPeriodicity[defaultIndex]
                    && nLifetime[q] >= nLifetime[defaultIndex]
                    && nMovement[q] >= nMovement[defaultIndex]
                    && nFlux[q] >= nFlux[defaultIndex]
                    && nAmFast[q] >= nAmFast[defaultIndex]
                    && nPitch[q] >= nPitch[defaultIndex]
                    && nConcentration[q] >= nConcentration[defaultIndex];

                dominated = dominated || atLeastAsGood;
            }

            check (! dominated, "the shipped default is not dominated by any other candidate");
        }

        // -------------------------------------------------------------------
        //  Weight sensitivity
        // -------------------------------------------------------------------
        // The weighted ranking above is one opinion. Reporting only its winner
        // would hide how much that winner depends on a number chosen by hand.
        info ("");
        info ("How the winner moves as echo periodicity is weighted against the other two");
        info ("movement phenomena. Everything else is held fixed.");
        info ("");
        info ("  echo weight   winner");

        for (double echoWeight : { 0.10, 0.20, 0.3333, 0.45, 0.60, 0.75, 0.90 })
        {
            const double other = (1.0 - echoWeight) * 0.5;

            double best = -1.0;
            int bestIndex = 0;

            for (int i = 0; i < architecture::candidateCount; ++i)
            {
                const auto k = static_cast<size_t> (i);

                if (! constraints[k].passes() || architecture::allCandidates[i] == Candidate::staticFdn)
                    continue;

                const double movementScore = other * stasis[k] + echoWeight * echoing[k]
                                           + other * change[k];
                const double score = 0.60 * movementScore + 0.40 * cost[k];

                if (score > best)
                {
                    best = score;
                    bestIndex = i;
                }
            }

            info ("  " + pad (fixed (echoWeight, 4), 14)
                  + architecture::describe (architecture::allCandidates[bestIndex]));
        }

        info ("");
        info ("This is the decision, stated rather than buried in a weight: late-tail");
        info ("periodicity is the metallic-ringing metric, and PHASE 3 established that it is");
        info ("invisible to every magnitude-domain measure because an allpass cascade is flat");
        info ("by construction. A tank whose modes drift but whose echo pattern still repeats");
        info ("on a fixed interval still sounds metallic, so periodicity is not one movement");
        info ("metric among three — it is the one the modulation exists to fix. Matrix-only");
        info ("leaves it at 42, essentially the static tank's 43. Allpass-only halves it.");
    }

    // =======================================================================
    beginSuite ("Selection / determinism and safety");
    // =======================================================================
    {
        // Two instances of the default architecture must render identically.
        auto a = std::make_unique<Tank>();
        auto b = std::make_unique<Tank>();

        for (auto* tank : { a.get(), b.get() })
        {
            tank->prepare (referenceRate);
            tank->setRt60Seconds (2.0, true);
            architecture::apply (*tank, architecture::defaultCandidate, true);
            tank->reset();
        }

        const auto noise = whiteNoise (300000, 991u);

        bool identical = true;

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double al = noise[n], ar = noise[n];
            double bl = noise[n], br = noise[n];

            a->processSample (al, ar);
            b->processSample (bl, br);

            identical &= (al == bl) && (ar == br);
        }

        check (identical, "two instances of the default architecture render bit-identically");
    }

    // =======================================================================
    beginSuite ("Selection / depth zero equivalence");
    // =======================================================================
    {
        // Whatever the architecture, Depth 0 has to be the PHASE 4 static tank.
        // Not "quiet modulation" — the same samples.
        for (auto candidate : architecture::allCandidates)
        {
            auto modulated = std::make_unique<Tank>();
            auto plain     = std::make_unique<Tank>();

            for (auto* tank : { modulated.get(), plain.get() })
            {
                tank->prepare (referenceRate);
                tank->setRt60Seconds (2.0, true);
                tank->setAllpassDepth (0.0, true);
                tank->setModulationDepth (0.0, true);
            }

            architecture::applyWithDepth (*modulated, candidate, 0.0, 1.0, true);

            modulated->reset();
            plain->reset();

            const auto noise = whiteNoise (150000, 5150u);

            bool identical = true;
            double worst = 0.0;

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double al = noise[n], ar = noise[n];
                double bl = noise[n], br = noise[n];

                modulated->processSample (al, ar);
                plain->processSample (bl, br);

                worst = std::max (worst, std::max (std::abs (al - bl), std::abs (ar - br)));
                identical &= (al == bl) && (ar == br);
            }

            check (identical, std::string (architecture::describe (candidate))
                              + ": Depth 0 is bit-identical to the static tank");

            if (! identical)
                info ("  worst difference: " + fixed (worst, 12));
        }
    }

    // =======================================================================
    beginSuite ("Selection / architecture transitions");
    // =======================================================================
    {
        // Switching architecture while audio is running must not step the level
        // or produce anything non-finite. The reverb does not expose this as a
        // user control, but the preview build and the tests both do it, and a
        // structure that cannot survive it is fragile for the wrong reasons.
        info ("  transition                          steady   worst jump");

        for (int i = 0; i + 1 < architecture::candidateCount; ++i)
        {
            const auto from = architecture::allCandidates[i];
            const auto to   = architecture::allCandidates[i + 1];

            auto tank = std::make_unique<Tank>();
            tank->prepare (referenceRate);
            tank->setRt60Seconds (2.0, true);
            architecture::apply (*tank, from, true);
            tank->reset();

            const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 6.0), 8181u);
            const auto switchAt = static_cast<size_t> (referenceRate * 3.0);

            std::vector<double> envelope;
            envelope.reserve (noise.size());

            bool finite = true;

            for (size_t n = 0; n < noise.size(); ++n)
            {
                if (n == switchAt)
                    architecture::apply (*tank, to, false);

                double l = 0.2 * noise[n], r = l;
                tank->processSample (l, r);

                finite &= std::isfinite (l) && std::isfinite (r);
                envelope.push_back (0.5 * (std::abs (l) + std::abs (r)));
            }

            // Level before the switch against level well after it, and the
            // worst short-window excursion anywhere across the boundary.
            const auto windowSamples = static_cast<size_t> (0.10 * referenceRate);

            const double before = rms (envelope.data() + switchAt - 4 * windowSamples, 3 * windowSamples);
            const double after  = rms (envelope.data() + switchAt + windowSamples, 3 * windowSamples);

            double worstJump = 0.0;

            for (size_t start = switchAt - windowSamples;
                 start + windowSamples < noise.size(); start += windowSamples / 4)
            {
                const double level = rms (envelope.data() + start, windowSamples);

                if (level > 0.0 && before > 0.0)
                    worstJump = std::max (worstJump, std::abs (20.0 * std::log10 (level / before)));
            }

            const double steady = (before > 0.0 && after > 0.0)
                                ? std::abs (20.0 * std::log10 (after / before)) : 0.0;

            const std::string label = std::string (architecture::describe (from)) + " -> "
                                    + architecture::describe (to);

            info ("  " + pad (label, 36) + pad (fixed (steady, 2) + " dB", 9)
                  + fixed (worstJump, 2) + " dB");

            check (finite, label + ": output stays finite");
            checkAtMost (worstJump, 4.0, label + ": no level step");
        }
    }
}

} // namespace avtest
