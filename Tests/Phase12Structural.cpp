#include "Phase12Stage3Metric.h"
#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/FdnCore.h"

#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

/*
    PHASE 12 - STAGE 3. Structural Mode dimension research.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    THE ORDER OF THIS FILE IS THE ORDER OF THE WORK, and the order matters:

      12.3.1  the research core equals production, bit for bit
      12.3.2  the same-family floor of the augmented metric
      12.3.3  NEGATIVE controls: ordinary changes must stay REPRODUCED
      12.3.4  POSITIVE controls: changed topology must come out DISTINCT

      ----- everything above was committed before anything below was run -----

      12.3.5 onward: structural levers, one at a time, then candidates.

    A check in this file that reports a research finding is allowed to FAIL.
    Such checks are prefixed "FINDING" and listed in the Stage 3 report.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;

namespace structural
{
    // =======================================================================
    //  Formatting and plumbing
    // =======================================================================
    [[nodiscard]] std::string fx (double v, int digits)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (digits) << v;
        return s.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    [[nodiscard]] bool identical (const Stereo& a, const Stereo& b)
    {
        return a.l.size() == b.l.size()
            && std::memcmp (a.l.data(), b.l.data(), a.l.size() * sizeof (double)) == 0
            && std::memcmp (a.r.data(), b.r.data(), a.r.size() * sizeof (double)) == 0;
    }

    void runParallel (size_t count, const std::function<void (size_t)>& job)
    {
        const unsigned int workers =
            std::max (1u, std::min<unsigned int> (std::thread::hardware_concurrency(), 20u));

        std::atomic<size_t> next { 0 };
        std::vector<std::thread> pool;

        for (unsigned int w = 0; w < workers; ++w)
            pool.emplace_back ([&]
            {
                for (;;)
                {
                    const size_t i = next.fetch_add (1);

                    if (i >= count)
                        return;

                    job (i);
                }
            });

        for (auto& t : pool)
            t.join();
    }

    // =======================================================================
    //  The user's controls, as a search space
    // =======================================================================
    constexpr double kDecayLow = 0.1, kDecayHigh = 20.0;

    [[nodiscard]] User toUser (const Controls& c)
    {
        User u;

        u.size      = std::clamp (c.v[0], 0.0, 1.0);
        u.decay     = kDecayLow * std::pow (kDecayHigh / kDecayLow, std::clamp (c.v[1], 0.0, 1.0));
        u.earlyLate = std::clamp (c.v[2], 0.0, 1.0);
        u.earlyDiff = std::clamp (c.v[3], 0.0, 1.0);
        u.attack    = std::clamp (c.v[4], 0.0, 1.0);
        u.modDepth  = std::clamp (c.v[5], 0.0, 1.0);
        u.width     = 2.0 * std::clamp (c.v[6], 0.0, 1.0);

        return u;
    }

    [[nodiscard]] Controls toControls (const User& u)
    {
        Controls c;

        c.v[0] = u.size;
        c.v[1] = std::log (u.decay / kDecayLow) / std::log (kDecayHigh / kDecayLow);
        c.v[2] = u.earlyLate;
        c.v[3] = u.earlyDiff;
        c.v[4] = u.attack;
        c.v[5] = u.modDepth;
        c.v[6] = 0.5 * u.width;

        return c;
    }

    [[nodiscard]] std::string describe (const Controls& c)
    {
        const auto u = toUser (c);

        return "Size " + fx (u.size * 100.0, 0) + " Decay " + fx (u.decay, 2)
             + " E/L " + fx (u.earlyLate * 100.0, 0) + " EDiff " + fx (u.earlyDiff * 100.0, 0)
             + " Atk " + fx (u.attack * 100.0, 0) + " Mod " + fx (u.modDepth * 100.0, 0)
             + " Width " + fx (u.width * 100.0, 0);
    }

    // =======================================================================
    //  The impostor search
    // =======================================================================
    /*
        Minimises, over the impostor's seven user controls, the WORST FAMILY
        between it and a target. One start at `startAt`, twenty-four from a
        fixed scatter; the three best are each refined by a pattern search
        whose step halves from 0.16 to 0.005, with oblique moves tried
        whenever no single knob helps. The last steps matter: Decay is
        searched on a log scale across 0.1-20 s, where 0.02 is an 11 % change,
        two just-noticeable differences wide. Deterministic.
    */
    [[nodiscard]] SearchResult mimic (const Features& target, const Structure& impostor,
                                      const Floor& floor, const Controls& startAt,
                                      unsigned int seed, int ignoreFamily)
    {
        SearchResult result;

        auto evaluate = [&] (const Controls& c)
        {
            ++result.evaluations;
            auto d = distance (target, featuresOf (impostor, toUser (c)));

            // A family can be left out, to ask what remains for a listener
            // who cannot hear it (a centred source and the lateral family).
            if (ignoreFamily >= 0)
                d.family[ignoreFamily] = 0.0;

            return score (d, floor);
        };

        struct Scored { Controls c; Score s; };

        std::vector<Scored> starts;
        starts.push_back ({ startAt, evaluate (startAt) });

        result.start = starts[0].s;

        unsigned int state = seed | 1u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        for (int i = 0; i < 24; ++i)
        {
            Controls c;

            for (auto& v : c.v)
                v = next01();

            // Attack, Early/Late and Width sit at their defaults in most real
            // use; part of the scatter keeps them there.
            if (i % 2 == 0) c.v[4] = 0.0;
            if (i % 4 == 0) c.v[2] = 0.0;
            if (i % 3 != 0) c.v[6] = 0.5;

            // Decay is the one control a listener sets by ear first, and the
            // family that measures it is the sharpest. Half the scatter
            // starts with Decay at the target's MEASURED reverberation time.
            if (i % 2 == 1 && target.base.rt60 > 0.0)
                c.v[1] = std::clamp (std::log (target.base.rt60 / kDecayLow)
                                     / std::log (kDecayHigh / kDecayLow), 0.0, 1.0);


            starts.push_back ({ c, evaluate (c) });
        }

        std::sort (starts.begin(), starts.end(), [] (const Scored& a, const Scored& b)
        {
            return a.s.objective() < b.s.objective();
        });

        result.best.worst = 1.0e9;

        for (int s = 0; s < 3; ++s)
        {
            Controls current = starts[static_cast<size_t> (s)].c;
            Score currentS   = starts[static_cast<size_t> (s)].s;

            double step = 0.16;
            int budget = 260;

            while (step >= 0.0049 && budget > 0)
            {
                bool improved = false;

                for (int k = 0; k < kControls && budget > 0; ++k)
                    for (double direction : { 1.0, -1.0 })
                    {
                        Controls trial = current;
                        trial.v[k] = std::clamp (trial.v[k] + direction * step, 0.0, 1.0);

                        if (trial.v[k] == current.v[k])
                            continue;

                        const auto sc = evaluate (trial);
                        --budget;

                        if (sc.objective() < currentS.objective())
                        {
                            current = trial;
                            currentS = sc;
                            improved = true;
                        }
                    }

                if (! improved)
                {
                    // A worst-family objective has ridges: the worst family
                    // changes identity as a knob moves, and a move along one
                    // axis can then never help. Eight oblique moves are tried
                    // before the step is given up on.
                    for (int attempt = 0; attempt < 8 && budget > 0 && ! improved; ++attempt)
                    {
                        Controls trial = current;

                        for (auto& v : trial.v)
                            v = std::clamp (v + step * (2.0 * next01() - 1.0), 0.0, 1.0);

                        const auto sc = evaluate (trial);
                        --budget;

                        if (sc.objective() < currentS.objective())
                        {
                            current = trial;
                            currentS = sc;
                            improved = true;
                        }
                    }
                }

                if (! improved)
                    step *= 0.5;

            }

            if (currentS.objective() < result.best.objective())
            {
                result.best = currentS;
                result.controls = current;
            }
        }

        return result;
    }

    // =======================================================================
    //  Operating points and reference structures
    // =======================================================================
    const Point kPoints[kPointCount]
    {
        { "medium / medium", 0.60, 2.5 },
        { "small / short",   0.25, 0.8 },
        { "large / long",    0.85, 5.0 },
    };

    [[nodiscard]] User userAt (const Point& p)
    {
        User u;
        u.size = p.size;
        u.decay = p.decay;
        return u;
    }

    [[nodiscard]] Structure hall()
    {
        return Structure {};
    }

    [[nodiscard]] Structure withDelays (const std::string& name, const std::array<double, 8>& ms)
    {
        Structure s;
        s.name = name;
        s.delaysMs = ms;
        return s;
    }

    /** A Stage 2 candidate, carried over field for field. */
    [[nodiscard]] Structure fromStage2 (const mode12::ModeCandidate& m)
    {
        Structure s;

        s.name            = "Stage 2 " + m.name;
        s.delaysMs        = m.delaysMs;
        s.diffuserStages  = m.diffuserStages;
        s.diffuserScale   = m.diffuserScale;
        s.allpassSections = m.allpassSections;
        s.modDepthFactor  = m.modDepthFactor;
        s.er              = m.er;
        s.erChannelSpread = m.erChannelSpread;

        return s;
    }

    Floor gFloor;
    bool  gFloorMeasured = false;

    /** The largest residual any negative control left. Set by 12.3.3. */
    double strongestNegative = 0.0;

    /** The floor, MEASURED IF IT HAS NOT BEEN. Until 2026-11-19 this returned
        whatever was there, which in a run filtered to a later stage was an
        empty floor: every family then divided by 1 and the scores came out
        unscaled, silently (Docs/Phase12-Stage9-Metric-Baseline-Correction.md).
        No caller can now read an unmeasured floor. The values are those
        ensureFloor() has always produced; nothing about the metric changes.
        The lock is for the searches, which read the floor from many threads. */
    const Floor& floorOf()
    {
        static std::mutex guard;
        const std::lock_guard<std::mutex> lock (guard);

        if (! gFloorMeasured)
            ensureFloor();

        return gFloor;
    }

    // =======================================================================
    //  A batch of impostor searches
    // =======================================================================
    void runTrials (std::vector<Trial>& trials)
    {
        runParallel (trials.size(), [&] (size_t i)
        {
            auto& t = trials[i];

            if (! t.hasTarget)
                t.target = featuresOf (t.structure, t.user);

            t.result = mimic (t.target, t.impostor, gFloor, toControls (t.user),
                              0x9E3779B9u ^ static_cast<unsigned int> ((i + 1) * 2654435761u),
                              t.ignoreFamily);
        });
    }

    void printTrials (const std::vector<Trial>& trials)
    {
        info ("    " + pad ("target", 30) + pad ("at", 17) + pad ("same knobs", 12)
              + pad ("after search", 14) + pad ("verdict", 12) + "worst family");

        for (const auto& t : trials)
        {
            info ("    " + pad (t.label, 30) + pad (t.point, 17)
                  + pad (fx (t.result.start.worst, 2), 12)
                  + pad (fx (t.result.best.worst, 2), 14)
                  + pad (verdictOf (t.result.best.worst), 12)
                  + kFamilyNames[t.result.best.worstFamily]);
            info ("        " + describe (t.result.controls));
        }
    }

    void printFamilies (const std::string& label, const Score& s)
    {
        std::string line = "    " + pad (label, 26);

        for (int k = 0; k < kFamilies; ++k)
            line += pad (fx (s.scaled[k], 2), 7);

        info (line);
    }

    void printFamilyHeader()
    {
        info ("    " + pad ("", 26) + "build  prof   dens   stereo decay  recur  textur repeat modal  shape  arriv  later  place");
    }
}

using namespace structural;

// ===========================================================================
namespace
{
    /** Applies a function to both channels of a response. */
    template <typename Fn>
    void eachChannel (Stereo& ir, Fn&& fn)
    {
        fn (ir.l);
        fn (ir.r);
    }

    /** EXPLORATORY, controls only: the raw numbers behind the families. */
    void runProbe()
    {
        beginSuite ("PHASE 12.3.0 - raw feature probe (AV_S3_PROBE only; exploratory)");

        const User u = userAt (kPoints[0]);

        struct Row { std::string name; Structure s; };
        std::vector<Row> rows;

        rows.push_back ({ "Hall", hall() });
        rows.push_back ({ "sibling", withDelays ("s", mode12::hallSibling().delaysMs) });
        { Structure s; s.matrix = MatrixKind::identity; rows.push_back ({ "no mix", s }); }
        { Structure s; s.matrix = MatrixKind::ring;     rows.push_back ({ "ring", s }); }

        for (const auto& r : rows)
        {
            const auto centre = impulseResponse (r.s, u, 4.0);
            const auto left   = impulseResponse (r.s, u, kLeftOnlySeconds, true, false);
            const auto f = analyse (centre, left);

            info ("  " + pad (r.name, 9) + " longLag " + fx (f.longLagPeak, 3) + " edt " + fx (f.edtRatio, 3) + " late " + fx (f.lateRatio, 3)
                  + " lat " + fx (f.lateral[0], 1) + " " + fx (f.lateral[1], 1) + " " + fx (f.lateral[2], 1)
                  + " place " + fx (f.placementSpreadDb, 1) + " " + fx (f.placementHard, 3)
                  + "  fluct " + fx (f.fluctuation[0]) + " " + fx (f.fluctuation[1])
                  + "  lagPeak " + fx (f.lagPeak, 3) + " lagRms " + fx (f.lagRmsDb) + " modal " + fx (f.modalDb)
                  + "  recur " + fx (f.base.recurrence, 3)
                  + "  arr " + fx (f.arrival[0]) + " " + fx (f.arrival[1]) + " " + fx (f.arrival[2]));

            // long-lag autocorrelation of the flattened tail, 200 ms on, 2 s
            constexpr size_t segment = 65536;
            const size_t start = msToSamples (200.0), half = msToSamples (10.0);

            std::vector<double> prefix (centre.l.size() + 1, 0.0);
            for (size_t i = 0; i < centre.l.size(); ++i)
                prefix[i + 1] = prefix[i] + centre.l[i] * centre.l[i];

            std::vector<std::complex<double>> x (2 * segment, 0.0);
            double k4 = 0.0, k2 = 0.0;

            for (size_t i = 0; i < segment; ++i)
            {
                const size_t at = start + i;
                const double local = std::sqrt ((prefix[at + half] - prefix[at - half]) / (2.0 * half));
                const double v = local > 0.0 ? centre.l[at] / local : 0.0;
                x[i] = v;
                k2 += v * v; k4 += v * v * v * v;
            }

            fft (x);
            for (auto& v : x) v = std::norm (v);
            ifft (x);

            auto band = [&] (double fromMs, double toMs)
            {
                double peak = 0.0, sumSq = 0.0, sum4 = 0.0; size_t count = 0;
                for (size_t lag = msToSamples (fromMs); lag <= msToSamples (toMs); ++lag)
                {
                    const double rho = x[lag].real() / x[0].real() * segment / (segment - lag);
                    peak = std::max (peak, std::abs (rho)); sumSq += rho * rho; sum4 += rho * rho * rho * rho; ++count;
                }
                const double ms2 = sumSq / count;
                return "peak " + fx (peak, 3) + " rms*sqrtN " + fx (std::sqrt (ms2 * segment), 2)
                     + " kurt " + fx (sum4 / count / (ms2 * ms2), 1);
            };

            info ("            lags 3-150: " + band (3, 150) + " | 150-400: " + band (150, 400)
                  + " | 400-1000: " + band (400, 1000)
                  + " | wave kurt " + fx (k4 / segment / ((k2 / segment) * (k2 / segment)), 2));
        }
    }

    void runCore()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.1 - the research core equals production");
        // ===================================================================

        info ("  ResearchCore is FdnCore copied by script with four seams. With the");
        info ("  seams inert it has to BE FdnCore. Checked through the whole rig:");
        info ("  Stage 3's rig on ResearchCore against Stage 2's rig on FdnCore,");
        info ("  which Stage 2 proved equal to the production tank.");
        info ("");

        struct Case { const char* label; double size, decay, earlyLate, earlyDiff, attack, modDepth; };

        constexpr Case cases[]
        {
            { "defaults",                 0.60, 2.5, 0.0, 0.70, 0.0, 0.30 },
            { "small, short, early on",   0.20, 0.6, 0.5, 0.40, 0.0, 0.10 },
            { "large, long, attack",      0.90, 6.0, 0.0, 0.90, 0.6, 0.80 },
            { "no modulation",            0.45, 1.8, 0.9, 0.00, 0.2, 0.00 },
        };

        bool all = true;

        for (const auto& c : cases)
        {
            User u;
            u.size = c.size; u.decay = c.decay; u.earlyLate = c.earlyLate;
            u.earlyDiff = c.earlyDiff; u.attack = c.attack; u.modDepth = c.modDepth;

            const auto research = impulseResponse (hall(), u, 1.5);
            const auto stage2   = mode12::impulseResponse (mode12::candidates()[mode12::hall],
                                                           u, 1.5, mode12::Trim::own);

            const bool same = identical (research, stage2);
            all = all && same;

            info ("    " + pad (c.label, 28) + (same ? "bit-identical" : "DIFFERENT"));
        }

        check (all, "Stage 3 rig on ResearchCore == Stage 2 rig on FdnCore, bit for bit, 4 settings");

        info ("");
        info ("  Every static matrix the rig can build, checked for what it claims:");
        info ("");
        info ("    " + pad ("matrix", 22) + pad ("|M'M - I| max", 16) + pad ("non-zeros/row", 16) + "largest |entry|");

        struct Kind { const char* label; MatrixKind kind; double coupling; };

        const Kind kinds[]
        {
            { "hadamard (shipping)", MatrixKind::hadamard, 0.0 },
            { "householder",         MatrixKind::householder, 0.0 },
            { "identity",            MatrixKind::identity, 0.0 },
            { "ring",                MatrixKind::ring, 0.0 },
            { "random dense",        MatrixKind::randomDense, 0.0 },
            { "hadamard permuted",   MatrixKind::hadamardPermuted, 0.0 },
            { "sparse chain",        MatrixKind::sparseChain, 0.0 },
            { "coupled blocks 0.2",  MatrixKind::coupledBlocks, 0.2 },
        };

        double worstError = 0.0;

        for (const auto& k : kinds)
        {
            Structure s;
            s.matrix = k.kind;
            s.coupling = k.coupling;

            double m[N * N];
            effectiveMatrix (s, m);

            double error = 0.0, largest = 0.0;
            int nonZero = 0;

            for (int a = 0; a < N; ++a)
                for (int b = 0; b < N; ++b)
                {
                    double dot = 0.0;

                    for (int r = 0; r < N; ++r)
                        dot += m[r * N + a] * m[r * N + b];

                    error = std::max (error, std::abs (dot - (a == b ? 1.0 : 0.0)));
                    largest = std::max (largest, std::abs (m[a * N + b]));

                    if (std::abs (m[a * N + b]) > 1.0e-12)
                        ++nonZero;
                }

            worstError = std::max (worstError, error);

            info ("    " + pad (k.label, 22) + pad (fx (error * 1.0e15, 2) + "e-15", 16)
                  + pad (fx (nonZero / 8.0, 1), 16) + fx (largest, 3));
        }

        checkAtMost (worstError, 1.0e-12, "every research matrix is orthogonal to rounding");
    }

    struct FloorPair { std::string label; Distance d; };

    /** The twelve same-family pairs the floor is the maximum of. Shared by
        12.3.2, which prints them, and ensureFloor(), which does not. */
    [[nodiscard]] std::vector<FloorPair> measureFloorPairs()
    {
        const auto siblingA = withDelays ("sibling A", mode12::hallSibling().delaysMs);
        const auto siblingB = withDelays ("sibling B", mode12::searchDelaySet (
            388.50, fdn::delaysMs[7] / fdn::delaysMs[0], 0x7B22u));

        std::vector<FloorPair> pairs (static_cast<size_t> (kPointCount) * 4);

        runParallel (pairs.size(), [&] (size_t i)
        {
            const auto& p = kPoints[i / 4];
            const auto  u = userAt (p);
            const auto  reference = featuresOf (hall(), u);

            auto nudged = u;
            Features other;
            std::string what;

            switch (i % 4)
            {
                case 0:  other = featuresOf (siblingA, u); what = "sibling A"; break;
                case 1:  other = featuresOf (siblingB, u); what = "sibling B"; break;
                case 2:  nudged.size += 0.02; other = featuresOf (hall(), nudged); what = "Size + 2"; break;
                default: nudged.size -= 0.02; other = featuresOf (hall(), nudged); what = "Size - 2"; break;
            }

            pairs[i] = { std::string (p.label) + ", " + what, distance (reference, other) };
        });

        return pairs;
    }

    void runFloor()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.2 - the same-family floor");
        // ===================================================================

        info ("  What 'the same reverb, another realisation' looks like through each");
        info ("  family: Hall against two sibling delay sets searched to its own sum");
        info ("  and spread, and against itself with Size nudged by two points, at");
        info ("  three operating points. Each family is later divided by the LARGEST");
        info ("  figure it shows here, or by 1 if that is larger.");
        info ("");

        const auto pairs = measureFloorPairs();

        gFloor = Floor {};
        gFloorMeasured = true;

        printFamilyHeader();

        for (const auto& p : pairs)
        {
            gFloor.absorb (p.d);

            Score raw;

            for (int k = 0; k < kFamilies; ++k)
                raw.scaled[k] = p.d.family[k];

            printFamilies (p.label, raw);
        }

        info ("");

        Score floorRow, divisor;
        double largest = 0.0;

        for (int k = 0; k < kFamilies; ++k)
        {
            floorRow.scaled[k] = gFloor.family[k];
            divisor.scaled[k]  = std::max (1.0, gFloor.family[k]);
            largest = std::max (largest, gFloor.family[k]);
        }

        printFamilies ("FLOOR", floorRow);
        printFamilies ("divisor", divisor);

        info ("");
        info ("  By construction every pair above now scores at most 1.00 in every");
        info ("  family. REPRODUCED is <= " + fx (kReproduced, 1) + ", DISTINCT is >= " + fx (kDistinct, 1) + ".");

        checkAtMost (largest, 6.0, "no family's same-family floor is so large it has stopped measuring");

        // Level cannot enter.
        {
            const auto u = userAt (kPoints[0]);

            auto centre = impulseResponse (hall(), u, irSecondsFor (u.decay));
            auto left   = impulseResponse (hall(), u, kLeftOnlySeconds, true, false);

            const auto quiet = analyse (centre, left);

            for (auto* ir : { &centre, &left })
                eachChannel (*ir, [] (std::vector<double>& x) { for (auto& v : x) v *= 3.7; });

            const auto loud = score (distance (quiet, analyse (centre, left)), gFloor);

            info ("");
            info ("  The same response 11.4 dB louder scores " + fx (loud.worst, 6) + ".");

            checkAtMost (loud.worst, 1.0e-6, "the augmented metric is invariant to level");
        }
    }

    void runNegativeControls()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.3 - NEGATIVE controls: ordinary changes stay reachable");
        // ===================================================================

        info ("  Each target is the SAME topology as Hall. Shipping Hall is then");
        info ("  searched, over the seven user controls, to imitate it. If the");
        info ("  metric calls any of these DISTINCT it is inventing topology.");
        info ("");
        info ("  Two kinds. (a) Hall itself with its knobs moved: the search starts");
        info ("  from the DEFAULTS and has to find them. (b) Hall with hidden data");
        info ("  changed in the ways Stage 2 proved a knob cancels.");
        info ("");

        std::vector<Trial> trials;

        // (a) knob recovery
        {
            struct Moved { const char* label; double size, decay, earlyLate, earlyDiff, attack, modDepth, width; };

            constexpr Moved moved[]
            {
                { "Hall, knobs moved 1", 0.33, 1.4, 0.30, 0.40, 0.30, 0.60, 1.0 },
                { "Hall, knobs moved 2", 0.80, 4.0, 0.00, 0.90, 0.00, 0.10, 1.5 },
                { "Hall, knobs moved 3", 0.45, 0.7, 0.60, 0.20, 0.00, 0.50, 0.6 },
                { "Hall, knobs moved 4", 0.70, 6.0, 0.00, 0.55, 0.70, 0.90, 1.0 },
            };

            for (const auto& m : moved)
            {
                User target;
                target.size = m.size; target.decay = m.decay; target.earlyLate = m.earlyLate;
                target.earlyDiff = m.earlyDiff; target.attack = m.attack;
                target.modDepth = m.modDepth; target.width = m.width;

                Trial t;
                t.label = m.label;
                t.point = "(moved)";
                t.target = featuresOf (hall(), target);
                t.hasTarget = true;
                t.impostor = hall();
                t.user = User {};          // the search starts at the defaults

                trials.push_back (t);
            }
        }

        const size_t recoveryCount = trials.size();

        // (b) hidden data a knob cancels
        {
            std::vector<Structure> hidden;

            hidden.push_back (withDelays ("sibling delay set", mode12::hallSibling().delaysMs));

            {
                Structure s = hall();
                s.name = "delay set x 0.5";

                for (auto& d : s.delaysMs)
                    d *= 0.5;

                hidden.push_back (s);
            }

            {
                Structure s = hall();
                s.name = "modulation x 1.4";
                s.modDepthFactor = 1.4;
                hidden.push_back (s);
            }

            {
                Structure s = hall();
                s.name = "allpass sections 2";
                s.allpassSections = 2;
                hidden.push_back (s);
            }

            hidden.push_back (fromStage2 (mode12::candidates()[mode12::room]));

            for (const auto& h : hidden)
                for (const auto& p : kPoints)
                {
                    Trial t;
                    t.label = h.name;
                    t.point = p.label;
                    t.structure = h;
                    t.impostor = hall();
                    t.user = userAt (p);

                    trials.push_back (t);
                }
        }

        runTrials (trials);
        printTrials (trials);

        double worstRecovery = 0.0, worstHidden = 0.0;

        for (size_t i = 0; i < trials.size(); ++i)
            (i < recoveryCount ? worstRecovery : worstHidden) =
                std::max (i < recoveryCount ? worstRecovery : worstHidden, trials[i].result.best.worst);

        strongestNegative = std::max (worstRecovery, worstHidden);

        info ("");
        checkAtMost (worstRecovery, kReproduced,
                     "NEGATIVE: Hall with its knobs moved is recovered from the defaults");
        checkAtMost (worstHidden, kReproduced,
                     "NEGATIVE: every knob-cancellable hidden change is REPRODUCED by Hall");
    }

    void runPositiveControls()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.4 - POSITIVE controls: changed topology is DISTINCT");
        // ===================================================================

        info ("  Each target is NOT Hall's topology. Shipping Hall is searched to");
        info ("  imitate it exactly as above. If Hall reaches any of these, the");
        info ("  metric is still blind and nothing after this section is valid.");
        info ("");
        info ("  Three are networks: the tank with no mixing at all (the control");
        info ("  Stage 2's metric scored 0.25, REPRODUCED); the tank whose matrix");
        info ("  is a permutation, one long loop; and two four-line tanks with no");
        info ("  path between them, one per channel. Three are SYNTHETIC: a");
        info ("  known transformation applied to Hall's own response, so that each");
        info ("  new family is shown a difference of exactly its own kind.");
        info ("");

        std::vector<Trial> trials;

        for (int kind = 0; kind < 3; ++kind)
            for (const auto& p : kPoints)
            {
                Trial t;
                t.structure = hall();

                if (kind == 0)
                {
                    t.structure.matrix = MatrixKind::identity;
                    t.structure.name = "no mixing matrix";
                }
                else if (kind == 1)
                {
                    t.structure.matrix = MatrixKind::ring;
                    t.structure.name = "ring (one loop)";
                }
                else
                {
                    // Two four-line tanks with no path between them, one per
                    // channel. Connectivity, not coefficients.
                    t.structure.matrix    = MatrixKind::coupledBlocks;
                    t.structure.coupling  = 0.0;
                    t.structure.injection = InjectionKind::split;
                    t.structure.readout   = ReadoutKind::split;
                    t.structure.name = "two uncoupled tanks";
                }

                t.label = t.structure.name;
                t.point = p.label;
                t.impostor = hall();
                t.user = userAt (p);

                trials.push_back (t);
            }

        const size_t networkCount = trials.size();

        // --- synthetic -------------------------------------------------------
        {
            const User u = userAt (kPoints[0]);

            const double seconds = irSecondsFor (6.0);

            // 1. dispersed: 240 first-order allpasses over Hall's response:
            //    4 kHz arrives 16 ms and 8 kHz 22 ms before 1 kHz.
            //    Plate magnitude. (160 sections, a 10 ms lead at 4 kHz, was tried
            //    first and scored 2.64: WEAK. That is this family's
            //    resolution, and it is recorded rather than hidden.)
            {
                auto centre = impulseResponse (hall(), u, irSecondsFor (u.decay));
                auto left   = impulseResponse (hall(), u, kLeftOnlySeconds, true, false);

                for (auto* ir : { &centre, &left })
                    eachChannel (*ir, [] (std::vector<double>& x)
                    {
                        Dispersion d;
                        d.prepare (240, -0.7);

                        for (auto& v : x)
                            v = d.process (v);
                    });

                Trial t;
                t.label = "synthetic: dispersed";
                t.point = kPoints[0].label;
                t.target = analyse (centre, left);
                t.hasTarget = true;
                t.impostor = hall();
                t.user = u;

                trials.push_back (t);
            }

            // 2. double slope: a 1.2 s Hall plus a 6 s Hall 12 dB down.
            {
                User fast = u, slow = u;
                fast.decay = 1.2;
                slow.decay = 6.0;

                auto centre = impulseResponse (hall(), fast, seconds);
                auto left   = impulseResponse (hall(), fast, kLeftOnlySeconds, true, false);

                const auto centreSlow = impulseResponse (hall(), slow, seconds);
                const auto leftSlow   = impulseResponse (hall(), slow, kLeftOnlySeconds, true, false);

                for (size_t n = 0; n < centre.l.size(); ++n)
                {
                    centre.l[n] += 0.25 * centreSlow.l[n];
                    centre.r[n] += 0.25 * centreSlow.r[n];
                }

                for (size_t n = 0; n < left.l.size(); ++n)
                {
                    left.l[n] += 0.25 * leftSlow.l[n];
                    left.r[n] += 0.25 * leftSlow.r[n];
                }

                Trial t;
                t.label = "synthetic: double slope";
                t.point = kPoints[0].label;
                t.target = analyse (centre, left);
                t.hasTarget = true;
                t.impostor = hall();
                t.user = u;

                trials.push_back (t);
            }

            // 3. lateral: a left source whose right-hand reverb fades in
            //    over 300 ms.
            {
                const auto centre = impulseResponse (hall(), u, irSecondsFor (u.decay));
                auto left = impulseResponse (hall(), u, kLeftOnlySeconds, true, false);

                for (size_t n = 0; n < left.r.size(); ++n)
                    left.r[n] *= std::min (1.0, static_cast<double> (n) / (0.3 * kRate));

                Trial t;
                t.label = "synthetic: slow crossing";
                t.point = kPoints[0].label;
                t.target = analyse (centre, left);
                t.hasTarget = true;
                t.impostor = hall();
                t.user = u;

                trials.push_back (t);
            }
        }

        runTrials (trials);
        printTrials (trials);

        info ("");
        printFamilyHeader();

        for (const auto& t : trials)
            printFamilies (t.label.substr (0, 25), t.result.best);

        double weakestNetwork = 1.0e9, weakestSynthetic = 1.0e9;

        for (size_t i = 0; i < trials.size(); ++i)
            (i < networkCount ? weakestNetwork : weakestSynthetic) =
                std::min (i < networkCount ? weakestNetwork : weakestSynthetic, trials[i].result.best.worst);

        double strongestNegativeSeen = structural::strongestNegative;

        info ("");
        info ("  Strongest negative control " + fx (strongestNegativeSeen, 2)
              + ", weakest positive control " + fx (std::min (weakestNetwork, weakestSynthetic), 2) + ".");
        info ("");

        check (std::min (weakestNetwork, weakestSynthetic) > kReproduced,
               "POSITIVE: no structurally different target is REPRODUCED by Hall at any point (weakest "
               + fx (std::min (weakestNetwork, weakestSynthetic), 2) + ")");
        check (std::min (weakestNetwork, weakestSynthetic) >= 2.0 * strongestNegativeSeen,
               "CONTROLS: the weakest positive stands at least twice as far out as the strongest negative");
        check (weakestSynthetic >= kDistinct,
               "POSITIVE: all three synthetic structural differences stay DISTINCT (weakest "
               + fx (weakestSynthetic, 2) + ")");
        check (weakestNetwork >= kDistinct,
               "FINDING: every topology-different network is DISTINCT from Hall at EVERY point (weakest "
               + fx (weakestNetwork, 2) + ")");
    }
}

namespace structural
{
    void ensureFloor()
    {
        if (gFloorMeasured)
            return;

        gFloor = Floor {};

        for (const auto& p : measureFloorPairs())
            gFloor.absorb (p.d);

        gFloorMeasured = true;
    }
}

void runPhase12StructuralTests()
{
    const auto started = std::chrono::steady_clock::now();

    if (std::getenv ("AV_S3_PROBE") != nullptr) { runProbe(); return; }

    // AV_S3_QUICK skips the two control batches (nine minutes) while the
    // candidate sections are being developed. The floor is always measured.
    const bool quick = std::getenv ("AV_S3_QUICK") != nullptr;

    runCore();
    runFloor();

    if (! quick)
    {
        runNegativeControls();
        runPositiveControls();
    }

    runPhase12StructuralCandidates();

    info ("");
    info ("  Stage 3 suite wall time: "
          + fx (std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count(), 0)
          + " s");
}

} // namespace avtest
