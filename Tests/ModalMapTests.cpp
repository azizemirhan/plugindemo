#include "DspAnalysis.h"
#include "ModalMap.h"
#include "TestFramework.h"

#include "DSP/FdnTuning.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    [[nodiscard]] std::string fixed (double v, int places)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision (places) << v;
        return out.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : s + std::string (w - s.size(), ' ');
    }

    [[nodiscard]] std::string padLeft (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : std::string (w - s.size(), ' ') + s;
    }

    /** A sum of exponentially decaying sinusoids: known frequencies, known decays. */
    struct SyntheticMode { double hz; double amplitude; double decaySeconds; };

    [[nodiscard]] std::vector<double> synthesise (const std::vector<SyntheticMode>& modes,
                                                  double sampleRate, double seconds)
    {
        const auto length = static_cast<size_t> (sampleRate * seconds);
        std::vector<double> x (length, 0.0);

        for (const auto& m : modes)
        {
            // -60 dB over decaySeconds, the same convention as RT60.
            const double tau = m.decaySeconds / 6.907755;

            for (size_t n = 0; n < length; ++n)
            {
                const double t = static_cast<double> (n) / sampleRate;
                x[n] += m.amplitude * std::exp (-t / tau) * std::sin (2.0 * pi * m.hz * t);
            }
        }

        return x;
    }

    /** Nearest detected peak to a target, or nullptr. */
    [[nodiscard]] const ModalPeak* nearest (const std::vector<ModalPeak>& peaks, double hz)
    {
        const ModalPeak* best = nullptr;
        double bestDistance = std::numeric_limits<double>::infinity();

        for (const auto& p : peaks)
        {
            const double d = std::abs (p.frequencyHz - hz);

            if (d < bestDistance) { bestDistance = d; best = &p; }
        }

        return best;
    }
}

void runModalMapTests()
{
    constexpr double rate = 48000.0;

    // =======================================================================
    beginSuite ("Phase 6 / resolution bookkeeping");
    // =======================================================================
    {
        // The guardrail that matters most: zero-padding must change the bin
        // width and leave the resolving power alone. Getting this backwards is
        // how an analysis reports modes it never separated.
        const auto plain  = resolutionFor (rate, 48000, 48000, 1.0);
        const auto padded = resolutionFor (rate, 48000, 262144, 1.0);

        info ("  1 s window, no padding : " + plain.describe());
        info ("  1 s window, padded x5.5: " + padded.describe());

        checkClose (plain.windowSeconds, 1.0, 1.0e-12, "the window is one second");
        checkClose (plain.resolutionHz, 2.0, 1.0e-12, "so the resolution is 2 Hz");
        checkClose (padded.resolutionHz, plain.resolutionHz, 1.0e-12,
                    "zero-padding does not improve the resolution");
        check (padded.binWidthHz < plain.binWidthHz * 0.25,
               "but it does shrink the bin width, which is display only");

        // The modal spacing figure is the delay set's, and it must scale with
        // Size the way the delays do.
        const auto small = resolutionFor (rate, 48000, 48000, 0.5);
        const auto large = resolutionFor (rate, 48000, 48000, 2.0);

        checkClose (small.meanModalSpacingHz, 2.0 * plain.meanModalSpacingHz, 1.0e-9,
                    "halving Size doubles the mean modal spacing estimate");
        checkClose (large.meanModalSpacingHz, 0.5 * plain.meanModalSpacingHz, 1.0e-9,
                    "and doubling it halves it");

        // Against the value FdnTuning documents for the unscaled set.
        checkClose (plain.meanModalSpacingHz, 2.574, 0.01,
                    "the unscaled estimate matches the documented 2.6 Hz");
    }

    // =======================================================================
    beginSuite ("Phase 6 / peak detector against known truth");
    // =======================================================================
    {
        // Frequencies chosen well apart relative to the resolution, so this
        // measures the detector rather than the window.
        const std::vector<SyntheticMode> truth
        {
            { 220.0,  1.00, 2.5 },
            { 512.0,  0.60, 2.5 },
            { 1330.0, 0.35, 2.5 },
            { 3225.0, 0.20, 2.5 },
        };

        const double seconds = 2.0;
        const auto signal = synthesise (truth, rate, seconds);
        const auto window = static_cast<size_t> (rate * seconds);
        const size_t fftSize = 1 << 18;

        const auto res = resolutionFor (rate, window, fftSize, 0.8706);
        const auto spectrum = magnitudeSpectrum (signal, fftSize, true);
        const auto peaks = detectPeaks (spectrum, res, 40.0, 12000.0, 200.0, 6.0);

        info ("");
        info ("  " + res.describe());
        info ("  detected " + std::to_string (peaks.size()) + " peaks above 6 dB prominence");
        info ("");
        info ("      truth Hz    found Hz      error Hz    prominence      Q   ambiguous");
        info ("    ----------------------------------------------------------------------");

        for (const auto& t : truth)
        {
            const auto* p = nearest (peaks, t.hz);

            check (p != nullptr, "a peak was found near " + fixed (t.hz, 0) + " Hz");

            if (p == nullptr)
                continue;

            const double error = p->frequencyHz - t.hz;

            info ("    " + padLeft (fixed (t.hz, 1), 12)
                  + padLeft (fixed (p->frequencyHz, 3), 12)
                  + padLeft (fixed (error, 4), 14)
                  + padLeft (fixed (p->prominenceDb, 2), 14)
                  + padLeft (fixed (p->q, 1), 7)
                  + padLeft (p->ambiguous ? "yes" : "no", 12));

            // Well inside one resolution width. Parabolic interpolation on a
            // Hann-windowed log spectrum is good to a small fraction of a bin
            // for an isolated peak; anything worse than half the resolution
            // would make the Size and Decay sweeps meaningless.
            checkAtMost (std::abs (error), 0.5 * res.resolutionHz,
                         "the centre at " + fixed (t.hz, 0) + " Hz is within half a resolution width");
        }
    }

    // =======================================================================
    beginSuite ("Phase 6 / the detector refuses what it cannot resolve");
    // =======================================================================
    {
        // Two tones closer together than the window can separate. The right
        // answer is not "two peaks" and not "one peak" - it is a flag.
        const double seconds = 0.5;
        const auto window = static_cast<size_t> (rate * seconds);
        const size_t fftSize = 1 << 18;

        const auto res = resolutionFor (rate, window, fftSize, 0.8706);

        info ("  " + res.describe());

        const std::vector<SyntheticMode> tooClose
        {
            { 1000.0, 1.0, 2.5 },
            { 1000.0 + 0.4 * res.resolutionHz, 1.0, 2.5 },
        };

        const auto signal = synthesise (tooClose, rate, seconds);
        const auto spectrum = magnitudeSpectrum (signal, fftSize, true);
        const auto peaks = detectPeaks (spectrum, res, 800.0, 1200.0, 200.0, 6.0);

        info ("  two tones " + fixed (0.4 * res.resolutionHz, 3)
              + " Hz apart, resolution " + fixed (res.resolutionHz, 3) + " Hz");
        info ("  peaks reported: " + std::to_string (peaks.size()));

        for (const auto& p : peaks)
            info ("    " + fixed (p.frequencyHz, 3) + " Hz, prominence "
                  + fixed (p.prominenceDb, 2) + " dB"
                  + (p.ambiguous ? "   AMBIGUOUS: " + p.ambiguityReason : ""));

        // Whatever it reports, it must not report two clean separate peaks.
        // Either one merged peak, or two both flagged.
        const bool honest = peaks.size() <= 1
                         || std::all_of (peaks.begin(), peaks.end(),
                                         [] (const ModalPeak& p) { return p.ambiguous; });

        check (honest, "unresolvable tones are not reported as two clean peaks");

        // And the same pair with a window long enough should separate.
        {
            const double wide = 4.0;
            const auto wideWindow = static_cast<size_t> (rate * wide);
            const auto wideRes = resolutionFor (rate, wideWindow, fftSize, 0.8706);
            const auto wideSignal = synthesise (tooClose, rate, wide);
            const auto wideSpectrum = magnitudeSpectrum (wideSignal, fftSize, true);
            const auto widePeaks = detectPeaks (wideSpectrum, wideRes, 800.0, 1200.0, 200.0, 6.0);

            info ("");
            info ("  the same pair with a " + fixed (wide, 1) + " s window (resolution "
                  + fixed (wideRes.resolutionHz, 3) + " Hz): "
                  + std::to_string (widePeaks.size()) + " peaks");

            check (widePeaks.size() >= 2,
                   "a long enough window does separate them");
        }
    }

    // =======================================================================
    beginSuite ("Phase 6 / the assignment solver is a real global optimum");
    // =======================================================================
    {
        // Checked against brute force, not against itself. For a small matrix
        // every permutation can be enumerated, so the true optimum is known and
        // the solver either finds it or does not.
        const auto bruteForce = [] (const std::vector<std::vector<double>>& c)
        {
            const size_t n = c.size(), m = c[0].size();

            std::vector<size_t> cols (m);
            std::iota (cols.begin(), cols.end(), size_t { 0 });

            double best = std::numeric_limits<double>::infinity();

            // Every injective map from rows to columns.
            std::vector<size_t> pick (n);
            std::vector<bool> used (m, false);

            std::function<void (size_t, double)> recurse = [&] (size_t row, double running)
            {
                if (running >= best)
                    return;

                if (row == n) { best = running; return; }

                for (size_t j = 0; j < m; ++j)
                {
                    if (used[j])
                        continue;

                    used[j] = true;
                    recurse (row + 1, running + c[row][j]);
                    used[j] = false;
                }
            };

            recurse (0, 0.0);

            return best;
        };

        // The same greedy rule the first version of this used, kept so the
        // difference can be measured rather than asserted.
        const auto greedy = [] (const std::vector<std::vector<double>>& c)
        {
            const size_t n = c.size(), m = c[0].size();

            std::vector<bool> tr (n, false), tc (m, false);
            double total = 0.0;

            for (size_t step = 0; step < std::min (n, m); ++step)
            {
                double best = std::numeric_limits<double>::infinity();
                size_t bi = 0, bj = 0;

                for (size_t i = 0; i < n; ++i)
                {
                    if (tr[i]) continue;

                    for (size_t j = 0; j < m; ++j)
                    {
                        if (tc[j]) continue;

                        if (c[i][j] < best) { best = c[i][j]; bi = i; bj = j; }
                    }
                }

                tr[bi] = true; tc[bj] = true; total += best;
            }

            return total;
        };

        unsigned int seed = 20260815u;
        const auto nextRandom = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 16777216.0;
        };

        int trials = 0, exact = 0, greedyWorse = 0;
        double worstGap = 0.0, worstGreedyExcess = 0.0;

        for (int t = 0; t < 400; ++t)
        {
            const size_t n = 2 + static_cast<size_t> (nextRandom() * 5.0);   // 2..6
            const size_t m = n + static_cast<size_t> (nextRandom() * 3.0);   // n..n+2

            std::vector<std::vector<double>> c (n, std::vector<double> (m));

            for (size_t i = 0; i < n; ++i)
                for (size_t j = 0; j < m; ++j)
                    c[i][j] = nextRandom() * 20.0;

            const double optimum = bruteForce (c);
            const double solved = solveMinCostAssignment (c).totalCost;
            const double greedyCost = greedy (c);

            ++trials;

            const double gap = std::abs (solved - optimum);

            worstGap = std::max (worstGap, gap);

            if (gap < 1.0e-9)
                ++exact;

            if (greedyCost > optimum + 1.0e-9)
            {
                ++greedyWorse;
                worstGreedyExcess = std::max (worstGreedyExcess, greedyCost - optimum);
            }
        }

        info ("");
        info ("  " + std::to_string (trials) + " random matrices, 2-6 rows, checked against brute force");
        info ("    solver reached the true optimum : " + std::to_string (exact) + " / "
              + std::to_string (trials));
        info ("    worst deviation from optimum    : " + fixed (worstGap, 12));
        info ("");
        info ("  the greedy rule this replaced, on the same matrices:");
        info ("    worse than optimum in           : " + std::to_string (greedyWorse) + " / "
              + std::to_string (trials) + " cases");
        info ("    worst excess over optimum       : " + fixed (worstGreedyExcess, 4));

        checkEqual (static_cast<double> (exact), static_cast<double> (trials),
                    "the solver finds the true optimum on every random matrix");
        checkAtMost (worstGap, 1.0e-9, "and does so exactly");

        // If greedy never lost, this test would prove nothing about the change.
        check (greedyWorse > 0,
               "greedy really does fail on some of these, so the comparison is meaningful");
    }

    // =======================================================================
    beginSuite ("Phase 6 / a case built so greedy must fail");
    // =======================================================================
    {
        // Two tracks, two peaks. Track 0 sits nearest peak 0, so greedy takes
        // that pair first and strands track 1 with a match that costs far more
        // than the swap would have. The structure is the classic one: the
        // globally cheapest edge belongs to the wrong solution.
        std::vector<std::vector<double>> c
        {
            { 1.0, 3.0 },
            { 3.0, 20.0 },
        };

        const auto solved = solveMinCostAssignment (c);

        info ("");
        info ("    cost matrix   P0     P1");
        info ("      T0         1.0    3.0");
        info ("      T1         3.0   20.0");
        info ("");
        info ("    greedy: T0->P0 (1.0) then T1->P1 (20.0)  = 21.0");
        info ("    optimum: T0->P1 (3.0) and T1->P0 (3.0)   =  6.0");
        info ("    solver chose: T0->P" + std::to_string (solved.assignment[0])
              + ", T1->P" + std::to_string (solved.assignment[1])
              + "  = " + fixed (solved.totalCost, 1));

        checkClose (solved.totalCost, 6.0, 1.0e-9,
                    "the solver takes the swap, which greedy cannot see");
        checkEqual (static_cast<double> (solved.assignment[0]), 1.0, "T0 goes to P1");
        checkEqual (static_cast<double> (solved.assignment[1]), 0.0, "T1 goes to P0");
    }

    // =======================================================================
    beginSuite ("Phase 6 / rejection competes inside the optimisation");
    // =======================================================================
    {
        // A track whose only reachable peak is a poor fit should be left
        // unmatched rather than forced, and that has to be the solver's
        // decision rather than a threshold applied to its output.
        ModalPeak t0, t1;
        t0.frequencyHz = 1000.0; t0.prominenceDb = 20.0;
        t1.frequencyHz = 2000.0; t1.prominenceDb = 20.0;

        ModalPeak p0;
        p0.frequencyHz = 1000.5; p0.prominenceDb = 20.0;

        MatchOptions options;
        options.localSpacingHz = 3.0;
        options.rejectionCost = 4.0;

        const auto match = matchPeaks ({ t0, t1 }, { p0 }, options);

        info ("");
        info ("  two tracks, one peak, the second track far from it");
        info ("    track 0 -> " + (match.assignment[0] >= 0
                                   ? "peak " + std::to_string (match.assignment[0])
                                   : std::string ("unmatched")));
        info ("    track 1 -> " + (match.assignment[1] >= 0
                                   ? "peak " + std::to_string (match.assignment[1])
                                   : std::string ("unmatched")));
        info ("    regret: track 0 " + fixed (match.regret[0], 3)
              + ", track 1 " + fixed (match.regret[1], 3));

        checkEqual (static_cast<double> (match.assignment[0]), 0.0,
                    "the near track takes the peak");
        check (match.assignment[1] < 0, "the far track is left unmatched, not forced onto it");
    }

    // =======================================================================
    beginSuite ("Phase 6 / the tracker does not swap identities at a crossing");
    // =======================================================================
    {
        // The failure the whole tracker exists to avoid. Two trajectories, one
        // rising and one falling, that cross. Nearest-neighbour matching hands
        // each to the other after the crossing and reports two modes that
        // reversed direction; a tracker that predicts from the trajectory does
        // not.
        constexpr int points = 11;

        std::vector<std::vector<ModalPeak>> frames;
        std::vector<double> risingTruth, fallingTruth;

        for (int i = 0; i < points; ++i)
        {
            const double t = static_cast<double> (i) / (points - 1);

            const double rising  = 900.0 + 200.0 * t;
            const double falling = 1100.0 - 200.0 * t;

            risingTruth.push_back (rising);
            fallingTruth.push_back (falling);

            ModalPeak a, b;
            a.frequencyHz = rising;  a.prominenceDb = 12.0;
            b.frequencyHz = falling; b.prominenceDb = 18.0;

            // Ordered by frequency, as a detector would return them, so the
            // tracker cannot cheat by relying on input order.
            if (a.frequencyHz <= b.frequencyHz) frames.push_back ({ a, b });
            else                                frames.push_back ({ b, a });
        }

        MatchOptions options;
        options.localSpacingHz = 20.0;

        const auto modes = trackAcross (frames, options);

        info ("");
        info ("  two trajectories crossing at the midpoint, 11 steps");
        info ("  slots tracked: " + std::to_string (modes.size()));
        info ("");
        info ("    step   slot 0      slot 1     truth rising   truth falling");
        info ("    -------------------------------------------------------------");

        for (int i = 0; i < points; ++i)
        {
            const auto at = [&] (size_t s)
            {
                if (s >= modes.size() || ! std::isfinite (modes[s].frequencyHz[static_cast<size_t> (i)]))
                    return std::string ("    --");

                return fixed (modes[s].frequencyHz[static_cast<size_t> (i)], 1);
            };

            info ("    " + padLeft (std::to_string (i), 4)
                  + padLeft (at (0), 10) + padLeft (at (1), 12)
                  + padLeft (fixed (risingTruth[static_cast<size_t> (i)], 1), 15)
                  + padLeft (fixed (fallingTruth[static_cast<size_t> (i)], 1), 16));
        }

        check (modes.size() == 2, "two slots, one per trajectory");

        if (modes.size() == 2)
        {
            // Each slot must stay on one monotonic trajectory throughout. A
            // swap shows up as a slot that rises and then falls.
            const auto monotone = [] (const std::vector<double>& v)
            {
                int up = 0, down = 0;

                for (size_t i = 1; i < v.size(); ++i)
                {
                    if (! std::isfinite (v[i]) || ! std::isfinite (v[i - 1]))
                        continue;

                    if (v[i] > v[i - 1] + 1.0e-9) ++up;
                    if (v[i] < v[i - 1] - 1.0e-9) ++down;
                }

                return up == 0 || down == 0;
            };

            check (monotone (modes[0].frequencyHz), "slot 0 never reverses direction");
            check (monotone (modes[1].frequencyHz), "slot 1 never reverses direction");

            const int totalMatched = modes[0].matchedPoints + modes[1].matchedPoints;

            info ("");
            info ("  matched points: " + std::to_string (totalMatched) + " of "
                  + std::to_string (2 * points));
            info ("  ambiguous refusals: slot 0 " + std::to_string (modes[0].ambiguousAt.size())
                  + ", slot 1 " + std::to_string (modes[1].ambiguousAt.size()));

            // Some refusal at the exact crossing is correct behaviour, not a
            // failure - the two peaks really are indistinguishable there.
            check (totalMatched >= points,
                   "most points are matched, and refusal at the crossing is allowed");
        }
    }

    // =======================================================================
    beginSuite ("Phase 6 / renders are deterministic");
    // =======================================================================
    {
        // The repeatability figure below is only a measurement of the analysis
        // if the renderer contributes nothing. That has to be shown, not
        // assumed: a free-running modulation LFO would make every sweep point
        // depend on how many points preceded it.
        SweepPoint point;
        point.label = "default";

        const auto a = renderImpulse (point, rate, 1.0);
        const auto b = renderImpulse (point, rate, 1.0);

        check (a.left.size() == b.left.size(), "two renders are the same length");

        double worst = 0.0;

        for (size_t n = 0; n < std::min (a.left.size(), b.left.size()); ++n)
            worst = std::max (worst, std::abs (a.left[n] - b.left[n]));

        info ("  worst sample difference between two identical renders: " + fixed (worst, 20));

        checkEqual (worst, 0.0, "identical settings produce bit-identical renders");

        info ("  broadband RT60 " + fixed (a.broadbandRt60, 4) + " s"
              + (a.rt60Measured ? "" : "  (not measurable)"));
    }

    // =======================================================================
    beginSuite ("Phase 6 / repeatability and noise floor");
    // =======================================================================
    {
        // What the tracker's own numbers do when nothing changes. Every
        // movement threshold used later has to sit well above this.
        SweepPoint point;
        point.label = "default";

        const double seconds = 2.0;
        const auto window = static_cast<size_t> (rate * seconds);
        const size_t fftSize = 1 << 18;

        const auto render = renderImpulse (point, rate, seconds + 1.0);
        const auto res = resolutionFor (rate, window, fftSize, render.sizeScale);

        std::vector<double> segment (render.left.begin(),
                                     render.left.begin() + static_cast<long> (window));

        const auto spectrum = magnitudeSpectrum (segment, fftSize, true);
        const auto first = detectPeaks (spectrum, res, 100.0, 6000.0, 200.0, 8.0);

        // Analyse the identical data a second time. Any difference here is the
        // analysis itself, since the input is the same object.
        const auto second = detectPeaks (spectrum, res, 100.0, 6000.0, 200.0, 8.0);

        info ("");
        info ("  " + res.describe());
        info ("  peaks found in the production tank's response: " + std::to_string (first.size()));

        check (first.size() == second.size(), "re-analysing the same data finds the same count");

        double worstFrequencyDelta = 0.0;

        for (size_t i = 0; i < std::min (first.size(), second.size()); ++i)
            worstFrequencyDelta = std::max (worstFrequencyDelta,
                                            std::abs (first[i].frequencyHz - second[i].frequencyHz));

        info ("  worst centre difference on re-analysis: " + fixed (worstFrequencyDelta, 12) + " Hz");
        checkEqual (worstFrequencyDelta, 0.0, "the analysis is deterministic given identical input");

        // The figure that actually matters: analysing two different segments of
        // the same decaying response. This is the closest thing to "the same
        // measurement twice" that a decaying signal allows, and it is where a
        // real noise floor comes from.
        const size_t offset = static_cast<size_t> (rate * 0.25);

        std::vector<double> shifted (render.left.begin() + static_cast<long> (offset),
                                     render.left.begin() + static_cast<long> (offset + window));

        const auto shiftedSpectrum = magnitudeSpectrum (shifted, fftSize, true);
        const auto shiftedPeaks = detectPeaks (shiftedSpectrum, res, 100.0, 6000.0, 200.0, 8.0);

        MatchOptions options;
        options.localSpacingHz = std::max (1.0, res.meanModalSpacingHz);

        const auto match = matchPeaks (first, shiftedPeaks, options);

        std::vector<double> deltas;

        for (size_t i = 0; i < first.size(); ++i)
        {
            const int j = match.assignment[i];

            if (j >= 0)
                deltas.push_back (std::abs (shiftedPeaks[static_cast<size_t> (j)].frequencyHz
                                            - first[i].frequencyHz));
        }

        std::sort (deltas.begin(), deltas.end());

        const double median = deltas.empty() ? 0.0 : deltas[deltas.size() / 2];
        const double p95 = deltas.empty() ? 0.0
                         : deltas[static_cast<size_t> (0.95 * (deltas.size() - 1))];
        const double worst = deltas.empty() ? 0.0 : deltas.back();

        info ("");
        info ("  same response, analysis window moved 0.25 s:");
        info ("    peaks matched      " + std::to_string (deltas.size()) + " of "
              + std::to_string (first.size()));
        info ("    refused as ambiguous " + std::to_string (match.refused));
        info ("    median |delta f|   " + fixed (median, 4) + " Hz");
        info ("    95th percentile    " + fixed (p95, 4) + " Hz");
        info ("    worst              " + fixed (worst, 4) + " Hz");
        info ("");
        info ("  NOISE FLOOR for frequency movement: use the 95th percentile above.");
        info ("  Any movement threshold in the Size and Decay sweeps must be set");
        info ("  well clear of it - the classifier already requires 3x.");

        check (! deltas.empty(), "the two windows have peaks in common to compare");
        checkAtMost (median, res.resolutionHz,
                     "the median repeat difference is within one resolution width");
    }

    // =======================================================================
    beginSuite ("Phase 6 / classification is structural only");
    // =======================================================================
    {
        // Class C must never be reachable by "not a mode" reasoning, and no
        // path may produce a perceptual label. The enum has no such value, so
        // this checks the mapping instead.
        checkEqual (static_cast<double> (classify (-1.0, true, 5.0, 0.5, false)),
                    static_cast<double> (ModeClass::sizeTrackingModulationMoving),
                    "size-tracking and moving is class A");

        checkEqual (static_cast<double> (classify (-1.0, true, 0.2, 0.5, false)),
                    static_cast<double> (ModeClass::sizeTrackingModulationResistant),
                    "size-tracking and still is class B");

        checkEqual (static_cast<double> (classify (-0.1, true, 5.0, 0.5, false)),
                    static_cast<double> (ModeClass::sizeInsensitiveFixedOrMixed),
                    "size-insensitive is class C, not 'not a mode'");

        checkEqual (static_cast<double> (classify (-1.0, false, 5.0, 0.5, false)),
                    static_cast<double> (ModeClass::ambiguousUnresolved),
                    "an unmeasured size exponent is class D");

        checkEqual (static_cast<double> (classify (-1.0, true, 5.0, 0.5, true)),
                    static_cast<double> (ModeClass::ambiguousUnresolved),
                    "an ambiguous peak is class D whatever else is known");

        // Movement must clear the floor by a factor, not merely exceed it.
        checkEqual (static_cast<double> (classify (-1.0, true, 1.4, 0.5, false)),
                    static_cast<double> (ModeClass::sizeTrackingModulationResistant),
                    "movement below 3x the noise floor does not count as moving");

        info ("");
        info ("  " + std::string (describe (ModeClass::sizeTrackingModulationMoving)));
        info ("  " + std::string (describe (ModeClass::sizeTrackingModulationResistant)));
        info ("  " + std::string (describe (ModeClass::sizeInsensitiveFixedOrMixed)));
        info ("  " + std::string (describe (ModeClass::ambiguousUnresolved)));
        info ("");
        info ("No class carries a judgement about whether a mode is wanted. That");
        info ("decision is made later, from these measurements together with blind");
        info ("listening, and it is not the harness's to make.");
    }

    // =======================================================================
    beginSuite ("Phase 6 / sweep cost");
    // =======================================================================
    {
        // What the real sweep will cost, measured rather than guessed, so the
        // decision to run it is informed.
        SweepPoint point;

        const auto start = std::chrono::steady_clock::now();
        const auto render = renderImpulse (point, rate, 4.0);
        const auto renderMs = std::chrono::duration<double, std::milli> (
                                  std::chrono::steady_clock::now() - start).count();

        const size_t fftSize = 1 << 18;
        const auto window = static_cast<size_t> (rate * 2.0);

        std::vector<double> segment (render.left.begin(),
                                     render.left.begin() + static_cast<long> (window));

        const auto analysisStart = std::chrono::steady_clock::now();
        const auto res = resolutionFor (rate, window, fftSize, render.sizeScale);
        const auto spectrum = magnitudeSpectrum (segment, fftSize, true);
        const auto peaks = detectPeaks (spectrum, res, 40.0, 12000.0, 200.0, 8.0);
        const auto analysisMs = std::chrono::duration<double, std::milli> (
                                    std::chrono::steady_clock::now() - analysisStart).count();

        info ("");
        info ("  one 4 s impulse render        " + fixed (renderMs, 1) + " ms");
        info ("    (includes a 3 s silent settle before the impulse)");
        info ("  one spectrum + peak detection " + fixed (analysisMs, 1) + " ms");
        info ("  peaks found                   " + std::to_string (peaks.size()));

        check (renderMs > 0.0 && analysisMs >= 0.0, "the cost was measured");
    }
}

} // namespace avtest
