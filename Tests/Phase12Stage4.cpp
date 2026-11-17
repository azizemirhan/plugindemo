#include "Phase12Stage4.h"
#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DampingListening.h"
#include "DspAnalysis.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>

/*
    PHASE 12 - STAGE 4. Human structural validation: what the listener needs
    measured first.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    The metric, its scales, its thresholds and the search are Stage 3's,
    frozen at c18ecce, and are used here exactly as they are. Nothing in this
    file tunes a candidate: Room B and Plate A are the two structures Stage 3
    scored, parameter for parameter.

      12.4.1  the impostor search repeated over ten seeds
      12.4.2  Room B at the listening settings
      12.4.3  Plate A at the listening settings, and the transient audit
      12.4.4  where Plate A's CPU goes, and how it scales
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace
{
    constexpr int kLateralFamily = 11;
    constexpr int kSeeds = 10;

    // Carried from the chain measurement to the summary below it, and a sink
    // so the timing loops cannot be optimised away.
    double gChirpMs = 0.0, gChirpPeakDb = 0.0;
    volatile double gSink = 0.0;

    // The listening settings. Room uses two of Stage 3's own operating points
    // so the files and the scores describe the same thing.
    struct Setting { const char* label; double size, decay; };

    constexpr Setting kRoomCompact { "compact / short", 0.25, 0.8 };
    constexpr Setting kRoomMedium  { "medium / normal", 0.60, 2.5 };
    constexpr Setting kPlateShort  { "short plate",     0.50, 1.5 };
    constexpr Setting kPlateLong   { "longer plate",    0.60, 2.5 };

    [[nodiscard]] User userFor (const Setting& s)
    {
        User u;
        u.size = s.size;
        u.decay = s.decay;
        return u;
    }

    /** Stage 3's "Room B": channel-split coupled blocks, angle 0.30. */
    [[nodiscard]] const Structure& roomB()
    {
        static const Structure s = []
        {
            Structure r;
            r.name = "Room B";
            r.matrix = MatrixKind::coupledBlocks;
            r.coupling = 0.30;
            r.injection = InjectionKind::split;
            r.readout = ReadoutKind::split;
            return calibrate (r);
        }();

        return s;
    }

    /** Stage 3's "Plate A": 240 first-order sections ahead of the tank. */
    [[nodiscard]] const Structure& plateA()
    {
        static const Structure s = []
        {
            Structure p;
            p.name = "Plate A";
            p.preDispersion = 240;
            return calibrate (p);
        }();

        return s;
    }

    [[nodiscard]] double dbOf (double ratio)
    {
        return 10.0 * std::log10 (std::max (1.0e-30, ratio));
    }

    // =======================================================================
    //  12.4.1  ten seeds
    // =======================================================================
    struct Series
    {
        std::string label, point;
        Structure target;
        User targetUser, startUser;
        int ignoreFamily = -1;

        Features features;
        std::vector<double> residual;
    };

    struct Stats
    {
        double minimum = 0.0, median = 0.0, maximum = 0.0, deviation = 0.0;
        int reproduced = 0, weak = 0, distinct = 0;
    };

    [[nodiscard]] Stats statsOf (std::vector<double> v)
    {
        Stats s;

        std::sort (v.begin(), v.end());

        const size_t n = v.size();

        s.minimum = v.front();
        s.maximum = v.back();
        s.median  = n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);

        double mean = 0.0;

        for (double x : v)
            mean += x;

        mean /= static_cast<double> (n);

        double sum = 0.0;

        for (double x : v)
            sum += (x - mean) * (x - mean);

        s.deviation = n > 1 ? std::sqrt (sum / static_cast<double> (n - 1)) : 0.0;

        for (double x : v)
        {
            if (x <= kReproduced)      ++s.reproduced;
            else if (x >= kDistinct)   ++s.distinct;
            else                       ++s.weak;
        }

        return s;
    }

    [[nodiscard]] std::string statsRow (const Stats& s)
    {
        return pad (fx (s.minimum), 8) + pad (fx (s.median), 8) + pad (fx (s.maximum), 8)
             + pad (fx (s.deviation), 8) + pad (std::to_string (s.reproduced), 8)
             + pad (std::to_string (s.weak), 10) + std::to_string (s.distinct);
    }

    void runSeeds()
    {
        // ===================================================================
        beginSuite ("PHASE 12.4.1 - the impostor search over ten seeds");
        // ===================================================================

        info ("  Stage 3 scored each target with ONE search. The search is");
        info ("  stochastic (random restarts, random oblique moves), and Plate A");
        info ("  came out at 2.58 in one table and 3.13 in another. Here the same");
        info ("  frozen search is repeated with ten seeds per target. Metric,");
        info ("  scales, thresholds and search space are untouched.");
        info ("");
        info ("  A residual is how far the best imitation Hall found still is. The");
        info ("  search can only fail to find a closer one, never invent distance:");
        info ("  the MINIMUM over seeds is the tightest bound on the truth.");
        info ("");

        ensureFloor();

        std::vector<Series> series;

        auto addPoints = [&] (const std::string& label, const Structure& s, int ignore)
        {
            for (const auto& p : kPoints)
            {
                Series x;
                x.label = label;
                x.point = p.label;
                x.target = s;
                x.targetUser = x.startUser = userAt (p);
                x.ignoreFamily = ignore;
                series.push_back (x);
            }
        };

        addPoints ("Room B, any source",     roomB(),  -1);
        addPoints ("Room B, centred source", roomB(),  kLateralFamily);
        addPoints ("Plate A",                plateA(), -1);

        // Two references for reading the spread. A target Hall CAN reach,
        // started away from it; and the positive control that scored 2.90.
        {
            Series x;
            x.label = "control: Hall, knobs moved";
            x.point = kPoints[0].label;
            x.target = hall();
            x.startUser = userAt (kPoints[0]);
            x.targetUser = x.startUser;
            x.targetUser.size = 0.45;
            x.targetUser.decay = 1.8;
            x.targetUser.earlyDiff = 0.40;
            x.targetUser.attack = 0.30;
            x.targetUser.modDepth = 0.50;
            x.targetUser.width = 1.2;
            series.push_back (x);
        }
        {
            Structure ring;
            ring.name = "ring";
            ring.matrix = MatrixKind::ring;
            ring = calibrate (ring);

            Series x;
            x.label = "control: ring matrix";
            x.point = kPoints[2].label;
            x.target = ring;
            x.targetUser = x.startUser = userAt (kPoints[2]);
            series.push_back (x);
        }

        runParallel (series.size(), [&] (size_t i)
        {
            series[i].features = featuresOf (series[i].target, series[i].targetUser);
            series[i].residual.assign (kSeeds, 0.0);
        });

        runParallel (series.size() * kSeeds, [&] (size_t job)
        {
            auto& x = series[job / kSeeds];
            const size_t k = job % kSeeds;

            // Ten seeds, the same ten for every target, none of them a seed
            // Stage 3 used.
            const unsigned int seed = 0x9E3779B9u ^ static_cast<unsigned int> ((k + 101) * 2654435761u);

            x.residual[k] = mimic (x.features, hall(), floorOf(), toControls (x.startUser),
                                   seed, x.ignoreFamily).best.worst;
        });

        const std::string header = "    " + pad ("target", 30) + pad ("at", 17) + pad ("min", 8)
                                 + pad ("median", 8) + pad ("max", 8) + pad ("sd", 8)
                                 + pad ("<= 1.5", 8) + pad ("between", 10) + ">= 3.0";

        info (header);

        for (const auto& x : series)
            info ("    " + pad (x.label, 30) + pad (x.point, 17) + statsRow (statsOf (x.residual)));

        info ("");
        info ("  Every run, in seed order:");

        for (const auto& x : series)
        {
            std::string row = "    " + pad (x.label, 30) + pad (x.point, 17);

            for (double r : x.residual)
                row += pad (fx (r), 7);

            info (row);
        }

        // The verdict Stage 3's rule gives is the WEAKEST of the three points.
        // Per seed, that is one number per candidate.
        info ("");
        info ("  THE VERDICT-LEVEL FIGURE. Stage 3's rule reads the weakest of the");
        info ("  three operating points. Taken per seed:");
        info ("");
        info (header);

        Stats verdict[3];
        Stats pooled[3];

        for (int c = 0; c < 3; ++c)
        {
            std::vector<double> weakest (kSeeds, 1.0e30), all;

            for (int p = 0; p < kPointCount; ++p)
            {
                const auto& x = series[static_cast<size_t> (c * kPointCount + p)];

                for (int k = 0; k < kSeeds; ++k)
                {
                    weakest[static_cast<size_t> (k)] = std::min (weakest[static_cast<size_t> (k)],
                                                                  x.residual[static_cast<size_t> (k)]);
                    all.push_back (x.residual[static_cast<size_t> (k)]);
                }
            }

            verdict[c] = statsOf (weakest);
            pooled[c]  = statsOf (all);

            const auto& label = series[static_cast<size_t> (c * kPointCount)].label;

            info ("    " + pad (label, 30) + pad ("weakest point", 17) + statsRow (verdict[c]));
            info ("    " + pad (label, 30) + pad ("all 30 runs", 17) + statsRow (pooled[c]));
        }

        const Stats moved = statsOf (series[9].residual);
        const Stats ring  = statsOf (series[10].residual);

        info ("");
        info ("  HOW TO READ IT. The search's own scatter on one target is the sd");
        info ("  column. A candidate whose whole range sits on one side of a");
        info ("  threshold is stable. One whose range straddles 3.0 is threshold-");
        info ("  sensitive: its verdict depends on the seed, and 3.0 is a pre-");
        info ("  registered line, not a physical law - the ring control, a");
        info ("  genuinely different topology, sits at " + fx (ring.minimum) + " to " + fx (ring.maximum) + ".");

        check (moved.reproduced == kSeeds,
               "the search recovers a target Hall can reach, from every seed (worst "
               + fx (moved.maximum) + ")");

        check (pooled[0].distinct == 3 * kSeeds,
               "FINDING: Room B, any source, is DISTINCT in every run at every point (min "
               + fx (pooled[0].minimum) + ")");

        check (verdict[1].distinct == 0,
               "FINDING: Room B, centred source, is not DISTINCT under any seed (weakest-point max "
               + fx (verdict[1].maximum) + ")");

        check (verdict[1].reproduced == kSeeds,
               "FINDING: Room B, centred source, is REPRODUCED by Hall under every seed (weakest-point max "
               + fx (verdict[1].maximum) + ")");

        check (pooled[2].distinct == 3 * kSeeds,
               "FINDING: Plate A is DISTINCT in every run at every point (min "
               + fx (pooled[2].minimum) + ", " + std::to_string (pooled[2].distinct) + " of 30 runs)");

        check (pooled[2].reproduced == 0,
               "FINDING: Plate A is never REPRODUCED by Hall (min " + fx (pooled[2].minimum) + ")");
    }

    // =======================================================================
    //  Sources
    // =======================================================================
    [[nodiscard]] std::vector<double> monoOf (const std::vector<StereoFrame>& frames, double seconds)
    {
        const size_t n = std::min (frames.size(), static_cast<size_t> (seconds * kRate));

        std::vector<double> m (n);

        for (size_t i = 0; i < n; ++i)
            m[i] = 0.5 * (frames[i].l + frames[i].r);

        // A short fade so that a source cut mid-note does not click.
        const size_t fade = std::min (n, static_cast<size_t> (0.030 * kRate));

        for (size_t i = 0; i < fade; ++i)
            m[n - 1 - i] *= static_cast<double> (i) / static_cast<double> (fade);

        return m;
    }

    [[nodiscard]] std::vector<double> snareSource()
    {
        for (const auto& s : makeFu2Sources (kRate))
            if (s.name == "snare")
                return monoOf (s.frames, 2.74);     // five hits, 0.55 s apart

        return {};
    }

    [[nodiscard]] std::vector<double> vocalSource()
    {
        return monoOf (makeVocalReference (kRate, 2.2), 2.2);
    }

    [[nodiscard]] std::vector<double> percussionSource()
    {
        return monoOf (makeBrightPercussion (kRate, 1.0), 0.249);   // one hit
    }

    [[nodiscard]] Stereo renderMono (const Structure& s, const User& u, const std::vector<double>& m,
                                     bool hardLeft, size_t tail)
    {
        const std::vector<double> silent (m.size(), 0.0);

        return std::make_unique<Rig> (s, u)->render (m, hardLeft ? silent : m, m.size() + tail);
    }

    [[nodiscard]] Stereo cut (const Stereo& x, size_t from, size_t length)
    {
        Stereo out;

        const size_t a = std::min (from, x.l.size());
        const size_t b = std::min (from + length, x.l.size());

        out.l.assign (x.l.begin() + static_cast<std::ptrdiff_t> (a), x.l.begin() + static_cast<std::ptrdiff_t> (b));
        out.r.assign (x.r.begin() + static_cast<std::ptrdiff_t> (a), x.r.begin() + static_cast<std::ptrdiff_t> (b));

        return out;
    }

    [[nodiscard]] double energyBetween (const std::vector<double>& x, double fromMs, double toMs)
    {
        const size_t a = std::min (x.size(), static_cast<size_t> (fromMs * kRate / 1000.0));
        const size_t b = std::min (x.size(), static_cast<size_t> (toMs * kRate / 1000.0));

        double sum = 0.0;

        for (size_t i = a; i < b; ++i)
            sum += x[i] * x[i];

        return sum;
    }

    // =======================================================================
    //  12.4.2  Room B at the listening settings
    // =======================================================================
    void runRoom()
    {
        // ===================================================================
        beginSuite ("PHASE 12.4.2 - Room B at the listening settings");
        // ===================================================================

        ensureFloor();

        const Structure hallS = hall();

        info ("  What the listener is being handed, in numbers. Same knobs for both");
        info ("  reverbs, no search: this is the difference as it sits in the files.");
        info ("");
        info ("  SAME-KNOB SCORE against Hall (worst family, Stage 3 units):");
        info ("    " + pad ("setting", 20) + pad ("any source", 26) + "centred source");

        double centredWorst = 0.0;

        for (const auto& setting : { kRoomCompact, kRoomMedium })
        {
            const User u = userFor (setting);
            const Score s = score (distance (featuresOf (hall(), u), featuresOf (roomB(), u)), floorOf());

            double centred = 0.0;
            int family = 0;

            for (int k = 0; k < kFamilies; ++k)
                if (k != kLateralFamily && s.scaled[k] > centred)
                {
                    centred = s.scaled[k];
                    family = k;
                }

            centredWorst = std::max (centredWorst, centred);

            info ("    " + pad (setting.label, 20)
                  + pad (fx (s.worst) + "  (" + kFamilyNames[s.worstFamily] + ")", 26)
                  + fx (centred) + "  (" + kFamilyNames[family] + ")");
        }

        info ("");
        info ("  A HARD-LEFT IMPULSE: right against left output, dB, by time window.");
        info ("  0 = the reverb is centred; negative = it stays on the source's side.");
        info ("    " + pad ("setting", 20) + pad ("reverb", 10) + pad ("0-50 ms", 10)
              + pad ("50-200 ms", 11) + pad ("200-600 ms", 12) + "whole response");

        double roomLate = 0.0, hallLate = 0.0;

        for (const auto& setting : { kRoomCompact, kRoomMedium })
            for (const Structure* s : { &hallS, &roomB() })
            {
                const User u = userFor (setting);
                const auto ir = impulseResponse (*s, u, 2.0, true, false);

                auto ratio = [&] (double a, double b)
                {
                    return dbOf (energyBetween (ir.r, a, b) / std::max (1.0e-30, energyBetween (ir.l, a, b)));
                };

                const double late = ratio (200.0, 600.0);

                (s == &hallS ? hallLate : roomLate) = late;

                info ("    " + pad (setting.label, 20) + pad (s->name, 10) + pad (fx (ratio (0.0, 50.0), 1), 10)
                      + pad (fx (ratio (50.0, 200.0), 1), 11) + pad (fx (late, 1), 12)
                      + fx (ratio (0.0, 2000.0), 1));
            }

        info ("");
        info ("  THE RENDERED SOURCES, hard left: right against left, whole file, dB.");
        info ("    " + pad ("source", 12) + pad ("setting", 20) + pad ("Hall", 10) + "Room B");

        struct Named { const char* name; std::vector<double> m; };

        const Named sources[] { { "snare", snareSource() }, { "vocal", vocalSource() } };

        for (const auto& source : sources)
            for (const auto& setting : { kRoomCompact, kRoomMedium })
            {
                const User u = userFor (setting);
                const size_t tail = static_cast<size_t> ((setting.decay + 0.7) * kRate);

                auto balance = [&] (const Structure& s)
                {
                    const auto out = renderMono (s, u, source.m, true, tail);
                    return dbOf (energyBetween (out.r, 0.0, 1.0e9) / std::max (1.0e-30, energyBetween (out.l, 0.0, 1.0e9)));
                };

                info ("    " + pad (source.name, 12) + pad (setting.label, 20)
                      + pad (fx (balance (hall()), 1), 10) + fx (balance (roomB()), 1));
            }

        info ("");
        info ("  So on a hard-left source Hall's reverb comes back nearly centred");
        info ("  and Room B's stays on the left. On a centred source the two inputs");
        info ("  are equal, the two blocks are driven alike, and that difference");
        info ("  has nothing to act on.");

        check (roomLate < hallLate - 1.5,
               "Room B keeps a hard-left source's late reverb on its side where Hall does not ("
               + fx (roomLate, 1) + " against " + fx (hallLate, 1) + " dB)");

        check (centredWorst >= kDistinct,
               "FINDING: at the same knobs, Room B differs from Hall on a centred source by a DISTINCT margin (worst "
               + fx (centredWorst) + ")");
    }

    // =======================================================================
    //  12.4.3  Plate A: objective behaviour and the transient audit
    // =======================================================================
    struct Onset
    {
        double centroid = 0.0;              // ms, energy centroid of the window
        double t05 = 0.0, t10 = 0.0, t50 = 0.0;
        double first10 = 0.0, first20 = 0.0; // share of the window's energy
        double peak = 0.0, peakMs = 0.0;
    };

    [[nodiscard]] Onset onsetOf (const Stereo& x, double windowMs)
    {
        Onset o;

        const size_t n = std::min (x.l.size(), static_cast<size_t> (windowMs * kRate / 1000.0));

        double total = 0.0, moment = 0.0;

        for (size_t i = 0; i < n; ++i)
        {
            const double e = x.l[i] * x.l[i] + x.r[i] * x.r[i];
            const double a = std::max (std::abs (x.l[i]), std::abs (x.r[i]));

            total += e;
            moment += e * static_cast<double> (i);

            if (a > o.peak)
            {
                o.peak = a;
                o.peakMs = static_cast<double> (i) * 1000.0 / kRate;
            }
        }

        if (total <= 0.0)
            return o;

        o.centroid = moment / total * 1000.0 / kRate;

        double running = 0.0;
        bool got05 = false, got10 = false, got50 = false;

        for (size_t i = 0; i < n; ++i)
        {
            running += x.l[i] * x.l[i] + x.r[i] * x.r[i];

            const double ms = static_cast<double> (i) * 1000.0 / kRate;

            if (! got05 && running >= 0.05 * total) { o.t05 = ms; got05 = true; }
            if (! got10 && running >= 0.10 * total) { o.t10 = ms; got10 = true; }
            if (! got50 && running >= 0.50 * total) { o.t50 = ms; got50 = true; }

            if (i + 1 == static_cast<size_t> (0.010 * kRate)) o.first10 = running / total;
            if (i + 1 == static_cast<size_t> (0.020 * kRate)) o.first20 = running / total;
        }

        return o;
    }

    /** First time, ms, a 20 ms window of the response is at least `level`
        normalised echo density; -1 if it never is within 400 ms. */
    [[nodiscard]] double densityTime (const Stereo& ir, double level)
    {
        const size_t window = static_cast<size_t> (0.020 * kRate);
        const size_t step   = static_cast<size_t> (0.002 * kRate);
        const size_t last   = static_cast<size_t> (0.400 * kRate);

        for (size_t start = 0; start + window <= ir.l.size() && start <= last; start += step)
            if (normalisedEchoDensity (ir.l.data(), ir.l.size(), start, window) >= level)
                return static_cast<double> (start) * 1000.0 / kRate;

        return -1.0;
    }

    void runPlate()
    {
        // ===================================================================
        beginSuite ("PHASE 12.4.3 - Plate A at the listening settings, and the transient audit");
        // ===================================================================

        ensureFloor();

        info ("  SAME-KNOB SCORE against Hall (worst family, Stage 3 units), and when");
        info ("  each band arrives relative to 1 kHz (ms, time to 5 % of band energy):");
        info ("    " + pad ("setting", 16) + pad ("score", 24) + pad ("reverb", 10) + pad ("250 Hz", 9)
              + pad ("4 kHz", 9) + "8 kHz");

        for (const auto& setting : { kPlateShort, kPlateLong })
        {
            const User u = userFor (setting);
            const auto fh = featuresOf (hall(), u);
            const auto fp = featuresOf (plateA(), u);
            const Score s = score (distance (fh, fp), floorOf());

            info ("    " + pad (setting.label, 16)
                  + pad (fx (s.worst) + "  (" + kFamilyNames[s.worstFamily] + ")", 24)
                  + pad ("Hall", 10) + pad (fx (fh.arrival[0], 1), 9) + pad (fx (fh.arrival[1], 1), 9)
                  + fx (fh.arrival[2], 1));
            info ("    " + pad ("", 40) + pad ("Plate A", 10) + pad (fx (fp.arrival[0], 1), 9)
                  + pad (fx (fp.arrival[1], 1), 9) + fx (fp.arrival[2], 1));
        }

        // --- the dispersion chain on its own --------------------------------
        info ("");
        info ("  THE DISPERSION CHAIN ALONE (240 sections, a = -0.7), before any");
        info ("  reverb: what it does to a single click.");

        {
            const Structure& p = plateA();

            std::string row = "    group delay, ms:";

            for (double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
                row += "  " + fx (hz >= 1000.0 ? hz / 1000.0 : hz, 0) + (hz >= 1000.0 ? " kHz " : " Hz ")
                     + fx (dispersionDelay (p.preDispersionCoeff, hz, kRate) * p.preDispersion * 1000.0 / kRate, 1);

            info (row);

            Dispersion chain;
            chain.prepare (p.preDispersion, p.preDispersionCoeff);

            Stereo click;
            click.l.assign (static_cast<size_t> (0.2 * kRate), 0.0);

            for (size_t i = 0; i < click.l.size(); ++i)
                click.l[i] = chain.process (i == 0 ? 1.0 : 0.0);

            click.r.assign (click.l.size(), 0.0);

            const Onset o = onsetOf (click, 200.0);

            double t90 = 0.0, running = 0.0, total = energyBetween (click.l, 0.0, 200.0);

            for (size_t i = 0; i < click.l.size(); ++i)
            {
                running += click.l[i] * click.l[i];

                if (running >= 0.90 * total) { t90 = static_cast<double> (i) * 1000.0 / kRate; break; }
            }

            info ("    a unit click comes out as a " + fx (t90 - o.t10, 1) + " ms downward chirp (10 % to 90 % of");
            info ("    its energy, from " + fx (o.t10, 1) + " to " + fx (t90, 1) + " ms), energy unchanged ("
                  + fx (dbOf (total), 2) + " dB), peak " + fx (20.0 * std::log10 (std::max (1.0e-30, o.peak)), 1)
                  + " dB below the click.");

            gChirpMs = t90 - o.t10;
            gChirpPeakDb = 20.0 * std::log10 (std::max (1.0e-30, o.peak));
        }

        // --- the audit -------------------------------------------------------
        struct Named { const char* name; std::vector<double> m; };

        std::vector<Named> sources;

        sources.push_back ({ "impulse", std::vector<double> (1, 1.0) });

        {
            auto snare = snareSource();
            snare.resize (std::min (snare.size(), static_cast<size_t> (0.54 * kRate)));   // one hit
            sources.push_back ({ "snare", snare });
        }

        sources.push_back ({ "percussion", percussionSource() });

        info ("");
        info ("  THE AUDIT. One hit of each source through each reverb at the short-");
        info ("  plate setting, 100 % wet, Plate level-matched to Hall by energy.");
        info ("  Everything is measured over the first 100 ms of the wet response.");
        info ("");
        info ("    centroid   energy centroid, ms");
        info ("    t5 t10 t50 time to 5 / 10 / 50 % of that window's energy, ms");
        info ("    spread     t50 - t5: how long the onset takes to build, ms");
        info ("    in 10/20   share of the window's energy in its first 10 / 20 ms");
        info ("    peak       peak sample against Hall's, dB, and when it falls, ms");
        info ("");
        info ("    " + pad ("source", 12) + pad ("reverb", 10) + pad ("centroid", 10) + pad ("t5", 7)
              + pad ("t10", 7) + pad ("t50", 7) + pad ("spread", 8) + pad ("in 10", 8) + pad ("in 20", 8)
              + pad ("peak dB", 9) + "at ms");

        const User u = userFor (kPlateShort);
        const size_t tail = static_cast<size_t> (0.6 * kRate);

        double worstPeakDb = 0.0, worstSpreadGain = 0.0, meanCentroidShift = 0.0;

        struct Kept { Stereo hall, plate; };
        std::vector<Kept> kept;

        for (const auto& source : sources)
        {
            auto h = renderMono (hall(), u, source.m, false, tail);
            auto p = renderMono (plateA(), u, source.m, false, tail);

            const double gain = std::sqrt (energyOf (h) / std::max (1.0e-300, energyOf (p)));

            for (auto& v : p.l) v *= gain;
            for (auto& v : p.r) v *= gain;

            const Onset oh = onsetOf (h, 100.0);
            const Onset op = onsetOf (p, 100.0);

            auto row = [&] (const char* reverb, const Onset& o, double peakDb)
            {
                info ("    " + pad (source.name, 12) + pad (reverb, 10) + pad (fx (o.centroid, 1), 10)
                      + pad (fx (o.t05, 1), 7) + pad (fx (o.t10, 1), 7) + pad (fx (o.t50, 1), 7)
                      + pad (fx (o.t50 - o.t05, 1), 8) + pad (fx (o.first10, 3), 8) + pad (fx (o.first20, 3), 8)
                      + pad (fx (peakDb, 1), 9) + fx (o.peakMs, 1));
            };

            const double peakDb = 20.0 * std::log10 (std::max (1.0e-30, op.peak) / std::max (1.0e-30, oh.peak));

            row ("Hall", oh, 0.0);
            row ("Plate A", op, peakDb);

            worstPeakDb = std::min (worstPeakDb, peakDb);
            worstSpreadGain = std::max (worstSpreadGain, (op.t50 - op.t05) - (oh.t50 - oh.t05));
            meanCentroidShift += (op.centroid - oh.centroid) / static_cast<double> (sources.size());

            kept.push_back ({ h, p });
        }

        // --- per band: is it dispersion or blur? -----------------------------
        info ("");
        info ("  DISPERSION OR BLUR. A blur delays and smears every band alike. A");
        info ("  dispersion delays each band by a different amount and leaves each");
        info ("  band's own rise alone. Per band, first 150 ms of the wet response:");
        info ("  'arrives' is the time to 5 % of the band's energy, 'rise' the time");
        info ("  from 5 % to 30 %.");
        info ("");
        info ("    " + pad ("source", 12) + pad ("reverb", 10) + pad ("250 Hz", 16) + pad ("1 kHz", 16)
              + pad ("4 kHz", 16) + "8 kHz              (arrives / rise, ms)");

        constexpr double band[4] { 250.0, 1000.0, 4000.0, 8000.0 };

        double arrivalRange[2] { 0.0, 0.0 };       // Hall, Plate: spread of arrival across bands
        double riseChange = 0.0;                   // largest change of a band's own rise

        for (size_t k = 0; k < sources.size(); ++k)
        {
            double rise[2][4] {}, arrive[2][4] {};

            for (int which = 0; which < 2; ++which)
            {
                const Stereo& x = which == 0 ? kept[k].hall : kept[k].plate;

                std::string row = "    " + pad (sources[k].name, 12) + pad (which == 0 ? "Hall" : "Plate A", 10);

                for (int b = 0; b < 4; ++b)
                {
                    const auto e = detail::bandEnergy (x, band[b], static_cast<size_t> (0.150 * kRate));

                    arrive[which][b] = detail::onsetMs (e, 0.05);
                    rise[which][b]   = detail::onsetMs (e, 0.30) - arrive[which][b];

                    row += pad (fx (arrive[which][b], 1) + " / " + fx (rise[which][b], 1), 16);
                }

                info (row);

                const auto [lo, hi] = std::minmax_element (arrive[which], arrive[which] + 4);
                arrivalRange[which] = std::max (arrivalRange[which], *hi - *lo);
            }

            for (int b = 0; b < 4; ++b)
                riseChange = std::max (riseChange, std::abs (rise[1][b] - rise[0][b]));
        }

        // --- density ---------------------------------------------------------
        info ("");
        info ("  DENSITY GROWTH (impulse; normalised echo density in a 20 ms window,");
        info ("  1.0 = as dense as noise): the density by window, and the first time");
        info ("  it reaches each level, ms.");
        info ("");
        info ("    " + pad ("reverb", 10) + pad ("0-20", 8) + pad ("10-30", 8) + pad ("20-40", 8)
              + pad ("40-60", 8) + pad ("80-100", 9) + pad ("to 0.5", 9) + pad ("to 0.7", 9) + "to 0.9");

        double reach[2][3] {};

        for (int which = 0; which < 2; ++which)
        {
            const auto ir = impulseResponse (which == 0 ? hall() : plateA(), u, 1.0);

            std::string row = "    " + pad (which == 0 ? "Hall" : "Plate A", 10);

            for (double start : { 0.0, 10.0, 20.0, 40.0, 80.0 })
                row += pad (fx (normalisedEchoDensity (ir.l.data(), ir.l.size(),
                                                       static_cast<size_t> (start * kRate / 1000.0),
                                                       static_cast<size_t> (0.020 * kRate)), 2),
                            start == 80.0 ? 9 : 8);

            int i = 0;

            for (double level : { 0.5, 0.7, 0.9 })
            {
                reach[which][i] = densityTime (ir, level);
                row += pad (reach[which][i] < 0.0 ? "never" : fx (reach[which][i], 0), 9);
                ++i;
            }

            info (row);
        }

        info ("");
        info ("  READING IT.");
        info ("    - The chain turns a click into a " + fx (gChirpMs, 0) + " ms chirp " + fx (-gChirpPeakDb, 0)
              + " dB lower in peak. That");
        info ("      is the cost to a transient, and it is there before the tank.");
        info ("    - Across bands the arrival times spread over " + fx (arrivalRange[1], 1) + " ms in Plate A");
        info ("      against " + fx (arrivalRange[0], 1) + " ms in Hall; a band's own rise changes by at most "
              + fx (riseChange, 1) + " ms.");
        info ("    - Whether that is a plate's 'zing' or a softened attack is a");
        info ("      listening question. The measurement can say it is frequency-");
        info ("      ordered, not uniform; it cannot say it is pleasant.");

        check (arrivalRange[1] > arrivalRange[0] + 5.0,
               "Plate A orders the bands in time where Hall does not (" + fx (arrivalRange[1], 1)
               + " against " + fx (arrivalRange[0], 1) + " ms across 250 Hz - 8 kHz)");

        check (worstPeakDb > -3.0,
               "FINDING: Plate A keeps a transient's wet peak within 3 dB of Hall's at equal energy (worst "
               + fx (worstPeakDb, 1) + " dB)");

        check (worstSpreadGain < 5.0,
               "FINDING: Plate A lengthens no source's onset build (t5 to t50) by 5 ms or more (worst +"
               + fx (worstSpreadGain, 1) + " ms)");

        check (reach[1][2] >= 0.0 && reach[0][2] >= 0.0 && reach[1][2] <= reach[0][2] + 10.0,
               "FINDING: Plate A reaches echo density 0.9 no more than 10 ms after Hall ("
               + fx (reach[1][2], 0) + " against " + fx (reach[0][2], 0) + " ms)");

        (void) meanCentroidShift;
    }

    // =======================================================================
    //  12.4.4  CPU
    // =======================================================================
    [[nodiscard]] double nsPerSample (const Structure& s, const User& u, const std::vector<double>& noise)
    {
        double best = 1.0e30;

        for (int attempt = 0; attempt < 7; ++attempt)
        {
            Rig rig (s, u);

            const auto t0 = std::chrono::steady_clock::now();
            const auto out = rig.render (noise, noise, noise.size());
            const auto t1 = std::chrono::steady_clock::now();

            gSink = gSink + out.l.back();

            best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count()
                                   / static_cast<double> (noise.size()));
        }

        return best;
    }

    void runCpu()
    {
        // ===================================================================
        beginSuite ("PHASE 12.4.4 - where Plate A's CPU goes");
        // ===================================================================

        info ("  A profile, not an optimisation: nothing is changed. Single thread,");
        info ("  best of seven, 1.5 s of noise, the research rig as it stands. Parts");
        info ("  are found by switching one thing off and subtracting, and the tank");
        info ("  and the chain are also timed on their own as a cross-check.");
        info ("");

        const auto noise = whiteNoise (static_cast<size_t> (1.5 * kRate), 0xC0FFEEu);
        const User u;

        auto plateWith = [] (int sections)
        {
            Structure s;
            s.preDispersion = sections;
            return s;
        };

        // --- scaling ---------------------------------------------------------
        constexpr int counts[] { 0, 30, 60, 120, 240, 480 };
        constexpr int countN = sizeof counts / sizeof counts[0];

        double ns[countN] {};

        for (int i = 0; i < countN; ++i)
            ns[i] = nsPerSample (plateWith (counts[i]), u, noise);

        const double full = ns[4], base = ns[0];

        // --- parts -----------------------------------------------------------
        Structure noDiffuser;
        noDiffuser.diffuser = DiffuserKind::none;

        User still = u;
        still.modDepth = 0.0;

        const double withoutDiffuser   = nsPerSample (noDiffuser, u, noise);
        const double withoutModulation = nsPerSample (Structure {}, still, noise);

        double tankAlone = 1.0e30, chainAlone = 1.0e30;

        for (int attempt = 0; attempt < 7; ++attempt)
        {
            Rig rig (Structure {}, u);
            auto& core = rig.getCore();

            ScopedFtz ftz;

            double injection[N] {}, state[N] {};

            const auto t0 = std::chrono::steady_clock::now();

            for (double x : noise)
            {
                for (int i = 0; i < N; ++i)
                    injection[i] = 0.35 * x;

                core.processSample (injection, state);
                gSink = gSink + state[0];
            }

            const auto t1 = std::chrono::steady_clock::now();

            tankAlone = std::min (tankAlone, std::chrono::duration<double, std::nano> (t1 - t0).count()
                                             / static_cast<double> (noise.size()));
        }

        for (int attempt = 0; attempt < 7; ++attempt)
        {
            Dispersion l, r;
            l.prepare (240, -0.7);
            r.prepare (240, -0.7);

            ScopedFtz ftz;

            const auto t0 = std::chrono::steady_clock::now();

            for (double x : noise)
                gSink = gSink + l.process (x) + r.process (x);

            const auto t1 = std::chrono::steady_clock::now();

            chainAlone = std::min (chainAlone, std::chrono::duration<double, std::nano> (t1 - t0).count()
                                               / static_cast<double> (noise.size()));
        }

        const double chain      = full - base;
        const double diffusion  = std::max (0.0, base - withoutDiffuser);
        const double modulation = std::max (0.0, base - withoutModulation);
        const double tank       = std::max (0.0, tankAlone - modulation);
        const double other      = std::max (0.0, base - diffusion - tankAlone);

        auto share = [&] (double part) { return fx (100.0 * part / full, 1) + " %"; };

        info ("    Hall, whole wet path           " + fx (base, 1) + " ns / sample");
        info ("    Plate A, whole wet path        " + fx (full, 1) + " ns / sample   x " + fx (full / base));
        info ("");
        info ("    " + pad ("part of Plate A", 34) + pad ("ns / sample", 14) + "share");
        info ("    " + pad ("dispersion chain (2 x 240)", 34) + pad (fx (chain, 1), 14) + share (chain));
        info ("    " + pad ("tank, without modulation", 34) + pad (fx (tank, 1), 14) + share (tank));
        info ("    " + pad ("modulation", 34) + pad (fx (modulation, 1), 14) + share (modulation));
        info ("    " + pad ("input diffusion", 34) + pad (fx (diffusion, 1), 14) + share (diffusion));
        info ("    " + pad ("other (attack, level, early, I/O)", 34) + pad (fx (other, 1), 14) + share (other));
        info ("");
        info ("    cross-check: the chain timed on its own is " + fx (chainAlone, 1)
              + " ns / sample; by subtraction " + fx (chain, 1) + ".");
        info ("    'modulation' is what Mod Depth 0 saves, so it is a lower bound on");
        info ("    what the modulation bank costs if any of it still runs at 0.");

        // --- scaling, least squares -----------------------------------------
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0;

        for (int i = 0; i < countN; ++i)
        {
            sx += counts[i];
            sy += ns[i];
            sxx += static_cast<double> (counts[i]) * counts[i];
            sxy += counts[i] * ns[i];
            syy += ns[i] * ns[i];
        }

        const double slope = (countN * sxy - sx * sy) / (countN * sxx - sx * sx);
        const double intercept = (sy - slope * sx) / countN;
        const double r = (countN * sxy - sx * sy)
                       / std::sqrt ((countN * sxx - sx * sx) * (countN * syy - sy * sy));

        info ("");
        info ("  SECTION COUNT. Sections per channel against the whole wet path:");
        info ("    " + pad ("sections", 10) + pad ("ns / sample", 14) + pad ("x Hall", 9) + "straight-line fit");

        for (int i = 0; i < countN; ++i)
            info ("    " + pad (std::to_string (counts[i]), 10) + pad (fx (ns[i], 1), 14)
                  + pad (fx (ns[i] / base), 9) + fx (intercept + slope * counts[i], 1));

        info ("");
        info ("    fit: " + fx (intercept, 1) + " + " + fx (slope, 3) + " ns per section (both channels), r^2 = "
              + fx (r * r, 4) + ".");
        info ("    One section, one channel, one sample: " + fx (slope * 0.5, 3) + " ns.");
        info ("    Sections that would cost exactly one more Hall: " + fx (base / slope, 0) + ".");

        info ("");
        info ("  SIMD. Each section is  y = a x + s;  s = x - a y.  Section k needs");
        info ("  section k-1's y from the SAME sample, and its own s from the");
        info ("  PREVIOUS sample. Along the cascade that is one multiply-add after");
        info ("  another with nothing to do in parallel: the loop is bound by the");
        info ("  latency of that chain, not by how much arithmetic there is.");
        info ("    - Vectorising along the cascade: not possible as written.");
        info ("    - Across the two channels: possible, at most x 2, and an out-of-");
        info ("      order core already overlaps the two chains.");
        info ("    - Across time: also a recurrence (s), so no.");
        info ("    - In blocks, along anti-diagonals (section k at sample n with");
        info ("      section k-1 at sample n+1): independent, so SIMD is possible in");
        info ("      principle. Not built and not measured; that is optimisation,");
        info ("      and this stage does not optimise.");
        info ("  The serial recurrence therefore blocks the obvious vectorisation and");
        info ("  leaves only a restructured one. The cheaper question for a later");
        info ("  stage is whether fewer sections can carry the same identity.");

        check (r * r >= 0.98,
               "FINDING: Plate's cost grows linearly with section count (r^2 " + fx (r * r, 4) + ")");

        check (chain / full >= 0.5,
               "FINDING: the dispersion chain is the majority of Plate A's wet-path cost (" + share (chain) + ")");
    }
}

void runPhase12Stage4Tests()
{
    const auto started = std::chrono::steady_clock::now();

    runSeeds();
    runRoom();
    runPlate();
    runCpu();

    info ("");
    info ("  Stage 4 suite wall time: "
          + fx (std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count(), 0)
          + " s");
}

// ===========================================================================
//  The listening pack
// ===========================================================================
int exportPhase12Stage4Pack (const std::string& directory, double)
{
    struct Trial
    {
        const char* what;           // for the key only
        const Structure* first;     // "reference" side
        const Structure* second;    // "candidate" side
        const char* source;
        bool hardLeft;
        Setting setting;
        int file;                   // blind file number, 1 .. 12
        bool referenceFirst;        // which side is heard first
    };

    const Structure hallS = hall();
    const Structure& room = roomB();
    const Structure& plate = plateA();

    // File numbers and orders are fixed, not random, and follow no pattern a
    // listener could learn: neither the group nor the order can be read from
    // the number.
    const Trial trials[]
    {
        { "Hall vs Room B, centred",   &hallS, &room,  "snare", false, kRoomCompact,  7, true  },
        { "Hall vs Room B, centred",   &hallS, &room,  "vocal", false, kRoomCompact,  2, false },
        { "Hall vs Room B, centred",   &hallS, &room,  "snare", false, kRoomMedium,  10, false },
        { "Hall vs Room B, centred",   &hallS, &room,  "vocal", false, kRoomMedium,   5, true  },
        { "Hall vs Room B, hard left", &hallS, &room,  "snare", true,  kRoomCompact, 12, false },
        { "Hall vs Room B, hard left", &hallS, &room,  "vocal", true,  kRoomMedium,   1, true  },
        { "Hall vs Plate A",           &hallS, &plate, "snare", false, kPlateShort,   9, true  },
        { "Hall vs Plate A",           &hallS, &plate, "vocal", false, kPlateShort,   4, false },
        { "Hall vs Plate A",           &hallS, &plate, "snare", false, kPlateLong,   11, false },
        { "Hall vs Plate A",           &hallS, &plate, "vocal", false, kPlateLong,    6, true  },
        { "Room B vs Plate A, hard left (cross-check)",
                                       &room,  &plate, "snare", true,  kRoomMedium,   3, false },
        { "Hall vs Hall (catch trial: identical)",
                                       &hallS, &hallS, "snare", false, kRoomMedium,   8, true  }
    };

    const auto snare = snareSource();
    const auto vocal = vocalSource();

    if (snare.empty() || vocal.empty())
        return -1;

    std::ofstream key (directory + "/KEY.txt", std::ios::binary);
    std::ofstream notes (directory + "/LISTENING.md", std::ios::binary);

    if (! key || ! notes)
        return -1;

    notes << "# Phase 12 Stage 4 - twelve blind comparisons\n\n"
          << "Each file is ONE comparison. You hear a sound through one reverb,\n"
          << "half a second of silence, then the SAME sound through another reverb.\n"
          << "Call them FIRST and SECOND.\n\n"
          << "What each file is, is in KEY.txt. Do not open it until you are done.\n\n"
          << "- Headphones or your normal monitors, a comfortable level, and leave\n"
          << "  the level alone once set.\n"
          << "- Play a file as often as you like. Any order.\n"
          << "- It is all reverb and no dry signal, on purpose.\n"
          << "- In two or three files the sound sits on the left. That is intended.\n"
          << "- There are no right answers. 'Same' is a useful answer, and at least\n"
          << "  one file really is the same thing twice.\n\n"
          << "## For every file, five questions\n\n"
          << "1. **Same or different?**  same / slightly different / clearly different\n"
          << "2. **If different: two different KINDS of reverb, or one reverb set\n"
          << "   slightly differently?**  different kinds / same kind / cannot say\n"
          << "3. **Which words fit, and for which half?** (FIRST / SECOND / neither)\n"
          << "   - smaller, closer, more like a real room around the sound\n"
          << "   - more metallic, springy or 'zingy'\n"
          << "   - softer or blurrier at the start of each hit or note\n"
          << "   - wider / more to one side\n"
          << "4. **Which would you rather use?**  FIRST / SECOND / no preference\n"
          << "5. **Would you want BOTH as separate choices in a plugin?**  yes / no\n\n"
          << "A line or two in your own words is worth more than the boxes.\n\n"
          << "## Answer sheet\n\n"
          << "| file | 1 same / different | 2 kind | 3 words | 4 prefer | 5 both? | notes |\n"
          << "|---|---|---|---|---|---|---|\n";

    for (int f = 1; f <= 12; ++f)
        notes << "| s4-" << (f < 10 ? "0" : "") << f << " | | | | | | |\n";

    key << "Phase 12 Stage 4 - key. Open only after listening.\n\n"
        << "All files: 100 % wet, 48 kHz, Early/Late 0, every other control at its\n"
        << "default and equal for both halves. The second-named reverb is level-\n"
        << "matched to the first by integrated energy (gain shown).\n\n";

    int written = 0;

    for (int f = 1; f <= 12; ++f)
        for (const auto& t : trials)
        {
            if (t.file != f)
                continue;

            const auto& m = std::string (t.source) == "snare" ? snare : vocal;
            const User u = userFor (t.setting);
            const size_t tail = static_cast<size_t> ((t.setting.decay * 0.9 + 0.8) * kRate);

            auto a = renderMono (*t.first,  u, m, t.hardLeft, tail);
            auto b = renderMono (*t.second, u, m, t.hardLeft, tail);

            const double gain = std::sqrt (energyOf (a) / std::max (1.0e-300, energyOf (b)));

            for (auto& v : b.l) v *= gain;
            for (auto& v : b.r) v *= gain;

            const Stereo& one = t.referenceFirst ? a : b;
            const Stereo& two = t.referenceFirst ? b : a;

            // One level for the whole file, so the match between the halves
            // survives: the louder half peaks at -6 dBFS.
            double peak = 0.0;

            for (const Stereo* x : { &one, &two })
                for (size_t n = 0; n < x->l.size(); ++n)
                    peak = std::max ({ peak, std::abs (x->l[n]), std::abs (x->r[n]) });

            const double level = peak > 0.0 ? 0.5 / peak : 1.0;

            const size_t gap  = static_cast<size_t> (0.5 * kRate);
            const size_t fade = static_cast<size_t> (0.020 * kRate);

            std::vector<StereoFrame> frames;
            frames.reserve (one.l.size() + two.l.size() + gap);

            for (const Stereo* x : { &one, &two })
            {
                const size_t n = x->l.size();

                for (size_t i = 0; i < n; ++i)
                {
                    const double g = level * (i + fade >= n ? static_cast<double> (n - 1 - i)
                                                              / static_cast<double> (fade) : 1.0);

                    frames.push_back ({ x->l[i] * g, x->r[i] * g });
                }

                if (x == &one)
                    frames.insert (frames.end(), gap, StereoFrame {});
            }

            const std::string name = std::string ("s4-") + (f < 10 ? "0" : "") + std::to_string (f);

            if (! writeStereoWav (directory + "/" + name + ".wav", frames, kRate))
                return -1;

            key << name << "  " << t.what << "\n"
                << "        source " << t.source << (t.hardLeft ? ", hard left" : ", centred")
                << "; " << t.setting.label << " (Size " << fx (t.setting.size * 100.0, 0)
                << ", Decay " << fx (t.setting.decay, 1) << " s)\n"
                << "        FIRST  = " << (t.referenceFirst ? t.first->name : t.second->name) << "\n"
                << "        SECOND = " << (t.referenceFirst ? t.second->name : t.first->name) << "\n"
                << "        " << t.second->name << " level-matched by " << fx (20.0 * std::log10 (gain), 2)
                << " dB\n\n";

            ++written;
        }

    return written;
}

} // namespace avtest
