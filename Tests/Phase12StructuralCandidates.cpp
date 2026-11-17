#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include <array>
#include <chrono>
#include <cstdlib>

/*
    PHASE 12 - STAGE 3. Structural levers and candidates.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    Everything in this file was written and run AFTER the metric was frozen
    (see Docs/Phase12-Stage3-Preregistration.md). The lever list of 12.3.5 is
    the list fixed in that document, parameter for parameter.

      12.3.5  the isolated levers: Hall with ONE structural change
      12.3.6  onward: composites, semantics, safety, cost - added once the
              levers have been read
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace
{
    struct Lever
    {
        const char* item;       // the brief's letter
        Structure   structure;
    };

    /** The pre-registered list. Each is Hall with one thing changed. */
    [[nodiscard]] std::vector<Lever> buildLevers()
    {
        std::vector<Lever> levers;

        auto add = [&] (const char* item, const char* name, auto&& change)
        {
            Structure s;
            s.name = name;
            change (s);
            levers.push_back ({ item, s });
        };

        // --- A: the matrix ---------------------------------------------------
        add ("A", "matrix: householder",       [] (Structure& s) { s.matrix = MatrixKind::householder; });
        add ("A", "matrix: random dense",      [] (Structure& s) { s.matrix = MatrixKind::randomDense; s.matrixSeed = 1u; });
        add ("A", "matrix: hadamard permuted", [] (Structure& s) { s.matrix = MatrixKind::hadamardPermuted; });
        add ("A", "matrix: sparse chain",      [] (Structure& s) { s.matrix = MatrixKind::sparseChain; });
        add ("A/F", "matrix: coupled blocks",  [] (Structure& s) { s.matrix = MatrixKind::coupledBlocks; s.coupling = 0.20; });

        // --- B, C, D, H: the diffuser ----------------------------------------
        add ("B", "diffuser: 1 stage",   [] (Structure& s) { s.diffuserStages = 1; });
        add ("B", "diffuser: 2 stages",  [] (Structure& s) { s.diffuserStages = 2; });
        add ("B", "diffuser: 8 stages",  [] (Structure& s) { s.diffuserStages = 8; });
        add ("C", "diffuser: none",      [] (Structure& s) { s.diffuser = DiffuserKind::none; });
        add ("D", "diffuser: clustered", [] (Structure& s) { s.diffuser = DiffuserKind::clustered; });
        add ("D", "diffuser: wide",      [] (Structure& s) { s.diffuser = DiffuserKind::wide; });
        add ("H", "diffuser: velvet FIR", [] (Structure& s) { s.diffuser = DiffuserKind::velvet; });

        // --- E: injection ----------------------------------------------------
        add ("E", "injection: one line each", [] (Structure& s) { s.injection = InjectionKind::pair; });
        add ("E", "injection: staggered",     [] (Structure& s) { s.injection = InjectionKind::staggered; s.staggerMs = 30.0; });

        // --- F: connectivity -------------------------------------------------
        add ("F", "split channels, coupled", [] (Structure& s)
        {
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = 0.15;
            s.injection = InjectionKind::split;
            s.readout = ReadoutKind::split;
        });

        add ("F", "block A in, block B out", [] (Structure& s)
        {
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = 0.15;
            s.injection = InjectionKind::blockA;
            s.readout = ReadoutKind::blockB;
        });

        add ("F", "... with block B lossy", [] (Structure& s)
        {
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = 0.15;
            s.injection = InjectionKind::blockA;
            s.readout = ReadoutKind::blockB;
            s.blockDecayRatio = 0.35;
        });

        // --- G: the feedback path --------------------------------------------
        add ("G", "loop allpass x 1",   [] (Structure& s) { s.loopAllpasses = 1; });
        add ("G", "loop allpass x 2",   [] (Structure& s) { s.loopAllpasses = 2; });
        add ("G", "loop dispersion 4",  [] (Structure& s) { s.loopDispersion = 4; });
        add ("G", "loop dispersion 12", [] (Structure& s) { s.loopDispersion = 12; });

        // --- I: early / late relationship ------------------------------------
        add ("I", "readout: multi-tap",  [] (Structure& s) { s.readout = ReadoutKind::multiTap; });
        add ("I", "early feeds the tank", [] (Structure& s) { s.erFeedsTank = true; });

        // --- J: dispersion ahead of the tank ---------------------------------
        add ("J", "pre-dispersion 64",  [] (Structure& s) { s.preDispersion = 64; });
        add ("J", "pre-dispersion 240", [] (Structure& s) { s.preDispersion = 240; });

        return levers;
    }

    /** Filled by 12.3.8 and 12.3.11, read by nothing else; kept for the report. */
    std::vector<double> gDecayWorst, gCostRatio;

    struct Outcome
    {
        std::string name, item;
        double residual[kPointCount] {};
        double sameKnobs[kPointCount] {};
        int    family[kPointCount] {};
        std::string knobs[kPointCount];
        Score best[kPointCount];

        [[nodiscard]] double weakest() const noexcept
        {
            return *std::min_element (residual, residual + kPointCount);
        }
    };

    /** Scores a list of structures against one impostor at every point. */
    [[nodiscard]] std::vector<Outcome> scoreAgainst (const std::vector<Lever>& levers,
                                                     const Structure& impostor,
                                                     int ignoreFamily)
    {
        std::vector<Trial> trials;

        for (const auto& lever : levers)
            for (const auto& p : kPoints)
            {
                Trial t;
                t.label = lever.structure.name;
                t.point = p.label;
                t.structure = lever.structure;
                t.impostor = impostor;
                t.user = userAt (p);
                t.ignoreFamily = ignoreFamily;

                trials.push_back (t);
            }

        runTrials (trials);

        std::vector<Outcome> outcomes (levers.size());

        for (size_t i = 0; i < trials.size(); ++i)
        {
            auto& o = outcomes[i / kPointCount];
            const size_t p = i % kPointCount;

            o.name = levers[i / kPointCount].structure.name;
            o.item = levers[i / kPointCount].item;
            o.residual[p]  = trials[i].result.best.worst;
            o.sameKnobs[p] = trials[i].result.start.worst;
            o.family[p]    = trials[i].result.best.worstFamily;
            o.knobs[p]     = describe (trials[i].result.controls);
            o.best[p]      = trials[i].result.best;
        }

        return outcomes;
    }

    void printOutcomes (const std::vector<Outcome>& outcomes)
    {
        info ("    " + pad ("", 5) + pad ("structure", 28) + pad ("same knobs", 20)
              + pad ("after Hall's search", 22) + pad ("weakest", 9) + pad ("verdict", 12)
              + "family at the weakest point");

        for (const auto& o : outcomes)
        {
            int weakestPoint = 0;

            for (int p = 1; p < kPointCount; ++p)
                if (o.residual[p] < o.residual[weakestPoint])
                    weakestPoint = p;

            info ("    " + pad (o.item, 5) + pad (o.name, 28)
                  + pad (fx (o.sameKnobs[0], 1) + " " + fx (o.sameKnobs[1], 1) + " " + fx (o.sameKnobs[2], 1), 20)
                  + pad (fx (o.residual[0]) + " " + fx (o.residual[1]) + " " + fx (o.residual[2]), 22)
                  + pad (fx (o.weakest()), 9) + pad (verdictOf (o.weakest()), 12)
                  + kFamilyNames[o.family[weakestPoint]]);
            info ("         " + pad ("", 28) + "Hall at: " + o.knobs[weakestPoint]);
        }
    }

    void runLevers()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.5 - isolated structural levers");
        // ===================================================================

        info ("  Shipping Hall with ONE structural change, level-matched, at three");
        info ("  operating points, Early/Late at 0. Shipping Hall is then searched");
        info ("  over seven user controls to imitate it. The three figures are the");
        info ("  three points: medium/medium, small/short, large/long. A lever has");
        info ("  leverage only if a knob cannot take it away at ANY of them.");
        info ("");

        auto levers = buildLevers();

        runParallel (levers.size(), [&] (size_t i)
        {
            levers[i].structure = calibrate (levers[i].structure);
        });

        const auto outcomes = scoreAgainst (levers, hall(), -1);

        printOutcomes (outcomes);

        int distinct = 0, weak = 0, reproduced = 0;

        for (const auto& o : outcomes)
        {
            if (o.weakest() >= kDistinct)        ++distinct;
            else if (o.weakest() > kReproduced)  ++weak;
            else                                 ++reproduced;
        }

        info ("");
        info ("  " + std::to_string (levers.size()) + " levers: " + std::to_string (distinct)
              + " DISTINCT at every point, " + std::to_string (weak) + " WEAK, "
              + std::to_string (reproduced) + " REPRODUCED at some point.");
        info ("");
        info ("  Level corrections applied (dB), so loudness is not a way to differ:");

        std::string line = "    ";

        for (const auto& l : levers)
            if (l.structure.lateCorrection != 1.0)
                line += std::string (l.structure.name) + " "
                      + fx (20.0 * std::log10 (l.structure.lateCorrection), 1) + ";  ";

        info (line);

        check (distinct > 0,
               "FINDING: at least one isolated structural lever is DISTINCT from Hall at every point");
    }
}

namespace
{
    constexpr int kLateralFamily = 11;

    void printBreakdown (const std::vector<Outcome>& outcomes)
    {
        printFamilyHeader();

        for (const auto& o : outcomes)
            for (int p = 0; p < kPointCount; ++p)
                printFamilies (o.name.substr (0, 21) + " " + std::string (kPoints[p].label).substr (0, 3),
                               o.best[p]);
    }

    // =======================================================================
    //  The composite candidates
    // =======================================================================
    /*
        Built AFTER 12.3.5 was read, and only from levers that showed leverage
        alone: the split-channel coupled blocks, the block-A-in / block-B-out
        network (with and without loss), the diffuser stage count, and
        dispersion ahead of the tank. The one exception is Chamber, which is
        built from levers that showed NONE, because the brief asks for one
        more attempt to disprove it and nothing with leverage is left that
        means "chamber".
    */
    [[nodiscard]] std::vector<Lever> buildCandidates()
    {
        std::vector<Lever> c;

        auto add = [&] (const char* family, const char* name, auto&& change)
        {
            Structure s;
            s.name = name;
            change (s);
            c.push_back ({ family, s });
        };

        auto split = [] (Structure& s, double angle)
        {
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = angle;
            s.injection = InjectionKind::split;
            s.readout = ReadoutKind::split;
        };

        auto through = [] (Structure& s, double angle)
        {
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = angle;
            s.injection = InjectionKind::blockA;
            s.readout = ReadoutKind::blockB;
        };

        add ("ROOM", "Room A  split 0.15",          [&] (Structure& s) { split (s, 0.15); });
        add ("ROOM", "Room B  split 0.30",          [&] (Structure& s) { split (s, 0.30); });
        add ("ROOM", "Room C  split 0.15, 1 stage", [&] (Structure& s) { split (s, 0.15); s.diffuserStages = 1; });

        add ("PLATE", "Plate A  pre-disp 240",      [] (Structure& s) { s.preDispersion = 240; });
        add ("PLATE", "Plate B  pre-disp 120, 8 st", [] (Structure& s)
        {
            s.preDispersion = 120;
            s.preDispersionCoeff = -0.85;
            s.diffuserStages = 8;
        });

        add ("CHAMB", "Chamber  8 st, loop ap, taps", [] (Structure& s)
        {
            s.diffuserStages = 8;
            s.loopAllpasses = 2;
            s.readout = ReadoutKind::multiTap;
        });

        add ("AMBI", "Ambient A  through 0.15",       [&] (Structure& s) { through (s, 0.15); });
        add ("AMBI", "Ambient B  through 0.30, 8 st", [&] (Structure& s) { through (s, 0.30); s.diffuserStages = 8; });
        add ("AMBI", "Ambient C  through 0.15 lossy", [&] (Structure& s) { through (s, 0.15); s.blockDecayRatio = 0.35; });

        // Added after 12.3.6 was first read: the block candidates above use
        // sign vectors that are rows of the block's own matrix. These are
        // the same structures on vectors aligned with nothing. NEW rows;
        // the originals stay in the table.
        add ("ROOM", "Room D  split 0.15, generic", [&] (Structure& s) { split (s, 0.15); s.genericVectors = true; });
        add ("AMBI", "Ambient D  through 0.15, gen", [&] (Structure& s) { through (s, 0.15); s.genericVectors = true; });
        add ("AMBI", "Ambient E  through 0.30, gen", [&] (Structure& s) { through (s, 0.30); s.genericVectors = true; });

        return c;
    }

    std::vector<Lever> gCandidates;

    void runCandidates()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.6 - structural candidates against Hall");
        // ===================================================================

        info ("  Twelve structures, each built from levers 12.3.5 showed to have");
        info ("  leverage (Chamber excepted, see the source). Same rule: level-");
        info ("  matched, three points, Early/Late 0, shipping Hall searched over");
        info ("  seven controls.");
        info ("");

        gCandidates = buildCandidates();

        runParallel (gCandidates.size(), [&] (size_t i)
        {
            gCandidates[i].structure = calibrate (gCandidates[i].structure);
        });

        const auto outcomes = scoreAgainst (gCandidates, hall(), -1);

        printOutcomes (outcomes);
        info ("");
        info ("  Every family, after Hall's search (med, sma, lar are the three points):");
        info ("");
        printBreakdown (outcomes);

        info ("");
        info ("  THE CENTRED-SOURCE VIEW. The lateral family is measured with a");
        info ("  hard-left impulse. A mono send hears none of it. The same search");
        info ("  again with that family left out of both the search and the verdict:");
        info ("  what is left of each candidate for a centred source.");
        info ("");

        const auto centred = scoreAgainst (gCandidates, hall(), kLateralFamily);

        printOutcomes (centred);

        info ("");
        info ("    " + pad ("candidate", 34) + pad ("any source", 24) + "centred source");

        int survive = 0, surviveCentred = 0;

        for (size_t i = 0; i < outcomes.size(); ++i)
        {
            info ("    " + pad (outcomes[i].name, 34)
                  + pad (fx (outcomes[i].weakest()) + "  " + verdictOf (outcomes[i].weakest()), 24)
                  + fx (centred[i].weakest()) + "  " + verdictOf (centred[i].weakest()));

            if (outcomes[i].weakest() >= kDistinct) ++survive;
            if (centred[i].weakest()  >= kDistinct) ++surviveCentred;
        }

        info ("");
        check (survive > 0, "FINDING: at least one structural candidate is DISTINCT from Hall at every point");
        check (surviveCentred > 0,
               "FINDING: at least one structural candidate is DISTINCT from Hall for a CENTRED source");

        auto weakestOf = [&] (const char* family, const std::vector<Outcome>& from)
        {
            double best = 0.0;

            for (size_t i = 0; i < from.size(); ++i)
                if (std::string (gCandidates[i].item) == family)
                    best = std::max (best, from[i].weakest());

            return best;
        };

        check (weakestOf ("ROOM", outcomes) >= kDistinct,
               "FINDING: a ROOM candidate is DISTINCT from Hall at every point (best "
               + fx (weakestOf ("ROOM", outcomes)) + ")");
        check (weakestOf ("ROOM", centred) >= kDistinct,
               "FINDING: a ROOM candidate is DISTINCT from Hall for a centred source (best "
               + fx (weakestOf ("ROOM", centred)) + ")");
        check (weakestOf ("PLATE", outcomes) >= kDistinct,
               "FINDING: a PLATE candidate is DISTINCT from Hall at every point (best "
               + fx (weakestOf ("PLATE", outcomes)) + ")");
        check (weakestOf ("CHAMB", outcomes) >= kDistinct,
               "FINDING: the CHAMBER candidate is DISTINCT from Hall at every point (best "
               + fx (weakestOf ("CHAMB", outcomes)) + ")");
        check (weakestOf ("AMBI", outcomes) >= kDistinct,
               "FINDING: an AMBIENT candidate is DISTINCT from Hall at every point (best "
               + fx (weakestOf ("AMBI", outcomes)) + ")");
        check (weakestOf ("AMBI", centred) >= kDistinct,
               "FINDING: an AMBIENT candidate is DISTINCT from Hall for a centred source (best "
               + fx (weakestOf ("AMBI", centred)) + ")");
    }

    void runCross()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.7 - the candidates against each other");
        // ===================================================================

        info ("  Being unlike Hall is not enough: two Modes that reproduce each");
        info ("  other are one Mode. One representative per family, each searched");
        info ("  over its own seven controls to imitate each of the others.");
        info ("");

        const size_t pick[] { 0, 3, 6 };     // Room A, Plate A, Ambient A

        std::vector<Trial> trials;

        for (size_t target : pick)
            for (size_t impostor : pick)
            {
                if (target == impostor)
                    continue;

                for (const auto& p : kPoints)
                {
                    Trial t;
                    t.label = gCandidates[target].structure.name.substr (0, 9) + " <- "
                            + gCandidates[impostor].structure.name.substr (0, 9);
                    t.point = p.label;
                    t.structure = gCandidates[target].structure;
                    t.impostor = gCandidates[impostor].structure;
                    t.user = userAt (p);

                    trials.push_back (t);
                }
            }

        runTrials (trials);
        printTrials (trials);

        double weakest = 1.0e9;

        for (const auto& t : trials)
            weakest = std::min (weakest, t.result.best.worst);

        info ("");
        check (weakest >= kDistinct,
               "FINDING: the Room, Plate and Ambient representatives are mutually DISTINCT (weakest "
               + fx (weakest) + ")");
    }

    // =======================================================================
    //  Semantics, safety, tone, cost
    // =======================================================================
    [[nodiscard]] std::vector<Structure> everything()
    {
        std::vector<Structure> all;
        all.push_back (hall());

        for (const auto& c : gCandidates)
            all.push_back (c.structure);

        return all;
    }

    [[nodiscard]] double rt60Of (const Stereo& ir)
    {
        return mode12::rt60FromEnergy (detail::energyOfBoth (ir));
    }

    [[nodiscard]] double bandRt60 (const Stereo& ir, double hz)
    {
        return mode12::rt60FromEnergy (detail::bandEnergy (ir, hz, ir.l.size()));
    }

    void runSemantics()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.8 - Size and Decay semantics");
        // ===================================================================

        const auto all = everything();

        info ("  SIZE. Decay 2.5 s, Size 10 / 30 / 50 / 70 / 90. A larger Size must");
        info ("  read as later and slower: the early energy centroid (ms) and the");
        info ("  time to 10 % of the energy (ms) must not fall as Size rises.");
        info ("");
        info ("    " + pad ("structure", 34) + pad ("early centroid, ms", 34) + "t10, ms");

        constexpr double sizes[5] { 0.10, 0.30, 0.50, 0.70, 0.90 };

        struct SizeRow { double centroid[5] {}, t10[5] {}; };
        std::vector<SizeRow> sizeRows (all.size());

        runParallel (all.size() * 5, [&] (size_t i)
        {
            User u;
            u.size = sizes[i % 5];

            const auto f = mode12::analyse (impulseResponse (all[i / 5], u, 2.5));

            sizeRows[i / 5].centroid[i % 5] = f.earlyCentroid;
            sizeRows[i / 5].t10[i % 5] = f.t10;
        });

        int nonMonotone = 0;

        for (size_t k = 0; k < all.size(); ++k)
        {
            std::string a, b;
            bool monotone = true;

            for (int j = 0; j < 5; ++j)
            {
                a += pad (fx (sizeRows[k].centroid[j], 1), 6);
                b += pad (fx (sizeRows[k].t10[j], 1), 7);

                if (j > 0 && (sizeRows[k].centroid[j] < sizeRows[k].centroid[j - 1] - 0.5
                              || sizeRows[k].t10[j] < sizeRows[k].t10[j - 1] - 0.5))
                    monotone = false;
            }

            if (! monotone)
                ++nonMonotone;

            info ("    " + pad (all[k].name, 34) + pad (a, 34) + b + (monotone ? "" : "  NOT MONOTONE"));
        }

        checkEqual (static_cast<long long> (nonMonotone), 0LL,
                    "FINDING: Size is monotone in every structure");

        info ("");
        info ("  DECAY. Measured RT60 (-5 to -20 dB) over nine settings, as a ratio");
        info ("  to Hall's at the same knobs. A Mode must not redefine Decay. Also");
        info ("  the decay SHAPE: early decay time and late slope against the mid");
        info ("  slope, where 1.00 / 1.00 is a single exponential.");
        info ("");
        info ("    " + pad ("structure", 34) + pad ("RT60 / Hall, min..max", 26)
              + pad ("EDT ratio", 12) + "late ratio   (at the defaults)");

        constexpr double decays[3] { 0.6, 2.5, 6.0 };
        constexpr double dsizes[3] { 0.25, 0.60, 0.85 };

        std::vector<std::array<double, 9>> rt (all.size());
        std::vector<Features> shape (all.size());

        runParallel (all.size() * 9, [&] (size_t i)
        {
            User u;
            u.decay = decays[(i % 9) / 3];
            u.size  = dsizes[(i % 9) % 3];

            const auto ir = impulseResponse (all[i / 9], u, irSecondsFor (u.decay));

            rt[i / 9][i % 9] = rt60Of (ir);

            if (i % 9 == 4)
                shape[i / 9] = analyse (ir, impulseResponse (all[i / 9], u, kLeftOnlySeconds, true, false));
        });

        gDecayWorst.assign (all.size(), 0.0);

        for (size_t k = 0; k < all.size(); ++k)
        {
            double low = 1.0e9, high = 0.0;

            for (int j = 0; j < 9; ++j)
            {
                const double ratio = rt[0][static_cast<size_t> (j)] > 0.0
                                       ? rt[k][static_cast<size_t> (j)] / rt[0][static_cast<size_t> (j)] : 0.0;

                low  = std::min (low, ratio);
                high = std::max (high, ratio);
            }

            gDecayWorst[k] = std::max (std::abs (low - 1.0), std::abs (high - 1.0));

            info ("    " + pad (all[k].name, 34) + pad (fx (low, 3) + " .. " + fx (high, 3), 26)
                  + pad (fx (shape[k].edtRatio, 2), 12) + fx (shape[k].lateRatio, 2)
                  + (gDecayWorst[k] > 0.10 ? "   DECAY REDEFINED" : ""));
        }

        info ("");
        info ("  Hall's own RT60 against the request, nine settings: "
              + [&] { std::string s; for (int j = 0; j < 9; ++j)
                          s += fx (rt[0][static_cast<size_t> (j)] / mapping::rt60For (decays[j / 3]), 2) + " ";
                      return s; }());

        int redefined = 0;

        for (double w : gDecayWorst)
            if (w > 0.10)
                ++redefined;

        checkEqual (static_cast<long long> (redefined), 0LL,
                    "FINDING: no structure moves the measured RT60 more than 10 % from Hall's (two JNDs)");
    }

    void runStereo()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.9 - stereo and mono safety");
        // ===================================================================

        const auto all = everything();

        info ("  The Phase 11 checks, repeated on every structure, at the defaults.");
        info ("");
        info ("    corr      late L/R correlation, 200-600 ms, centred impulse");
        info ("    fold      mono fold-down of the whole response, dB (0 = no loss)");
        info ("    L/R       left against right output energy, centred impulse, dB");
        info ("    mirror    left-only against right-only input, total output, dB");
        info ("    cross     right/left output for a LEFT-only impulse, 0-50 ms and");
        info ("              200-600 ms, dB");
        info ("    hard      share of late partials more than 15 dB to mid or side");
        info ("    tone      24 tones 180 Hz-2.2 kHz, steady state, mono fold-down:");
        info ("              the worst and the median, dB");
        info ("    burst     a 300 ms noise burst, mono fold-down, dB");
        info ("    width     side/mid of the response at Width 50 / 100 / 150, dB");
        info ("");
        info ("    " + pad ("structure", 30) + pad ("corr", 7) + pad ("fold", 7) + pad ("L/R", 7)
              + pad ("mirror", 8) + pad ("cross", 13) + pad ("hard", 7) + pad ("tone worst/med", 16)
              + pad ("burst", 7) + "width");

        struct Row
        {
            double corr = 0, fold = 0, balance = 0, mirror = 0, cross[2] {}, hard = 0;
            double toneWorst = 0, toneMedian = 0, burst = 0, width[3] {};
        };

        std::vector<Row> rows (all.size());

        auto foldDb = [] (const Stereo& x, size_t from)
        {
            double total = 0.0, mono = 0.0;

            for (size_t n = from; n < x.l.size(); ++n)
            {
                total += x.l[n] * x.l[n] + x.r[n] * x.r[n];

                const double m = 0.5 * (x.l[n] + x.r[n]);
                mono += 2.0 * m * m;
            }

            return total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-30, mono / total)) : 0.0;
        };

        runParallel (all.size(), [&] (size_t k)
        {
            const User u;
            auto& row = rows[k];

            const auto centre = impulseResponse (all[k], u, 3.0);
            const auto left   = impulseResponse (all[k], u, 3.0, true, false);
            const auto right  = impulseResponse (all[k], u, 3.0, false, true);

            const auto f = analyse (centre, left);

            row.corr = f.base.corr[2];
            row.fold = foldDb (centre, 0);
            row.hard = f.placementHard;
            row.cross[0] = f.lateral[0];
            row.cross[1] = f.lateral[2];

            double ll = 0.0, rr = 0.0;

            for (size_t n = 0; n < centre.l.size(); ++n)
            {
                ll += centre.l[n] * centre.l[n];
                rr += centre.r[n] * centre.r[n];
            }

            row.balance = 10.0 * std::log10 (ll / rr);
            row.mirror  = 10.0 * std::log10 (energyOf (left) / energyOf (right));

            // Tones.
            std::vector<double> folds;

            for (int t = 0; t < 24; ++t)
            {
                const double hz = 180.0 * std::pow (2200.0 / 180.0, t / 23.0);
                const auto length = static_cast<size_t> (1.6 * kRate);

                std::vector<double> drive (length);

                for (size_t n = 0; n < length; ++n)
                    drive[n] = 0.25 * std::min (1.0, n / (0.02 * kRate))
                             * std::sin (2.0 * detail::kPi * hz * static_cast<double> (n) / kRate);

                Rig rig (all[k], u);

                folds.push_back (foldDb (rig.render (drive, drive, length), length / 2));
            }

            std::sort (folds.begin(), folds.end());

            row.toneWorst  = folds.front();
            row.toneMedian = folds[folds.size() / 2];

            // Noise burst.
            {
                const auto noise = whiteNoise (static_cast<size_t> (0.3 * kRate), 0x5EEDu);

                Rig rig (all[k], u);

                row.burst = foldDb (rig.render (noise, noise, static_cast<size_t> (2.0 * kRate)), 0);
            }

            constexpr double widths[3] { 0.5, 1.0, 1.5 };

            for (int w = 0; w < 3; ++w)
            {
                User wide;
                wide.width = widths[w];

                const auto ir = impulseResponse (all[k], wide, 2.0);

                double mid = 0.0, side = 0.0;

                for (size_t n = 0; n < ir.l.size(); ++n)
                {
                    mid  += (ir.l[n] + ir.r[n]) * (ir.l[n] + ir.r[n]);
                    side += (ir.l[n] - ir.r[n]) * (ir.l[n] - ir.r[n]);
                }

                row.width[w] = 10.0 * std::log10 (std::max (1.0e-30, side) / std::max (1.0e-30, mid));
            }
        });

        int worse = 0;

        for (size_t k = 0; k < all.size(); ++k)
        {
            const auto& r = rows[k];

            const bool defect = r.toneWorst < rows[0].toneWorst - 3.0
                             || r.hard > rows[0].hard + 0.05
                             || std::abs (r.balance) > 1.0 || std::abs (r.mirror) > 1.0
                             || r.fold < rows[0].fold - 1.5;

            if (defect && k > 0)
                ++worse;

            info ("    " + pad (all[k].name, 30) + pad (fx (r.corr), 7) + pad (fx (r.fold, 1), 7)
                  + pad (fx (r.balance, 1), 7) + pad (fx (r.mirror, 1), 8)
                  + pad (fx (r.cross[0], 1) + " " + fx (r.cross[1], 1), 13) + pad (fx (r.hard, 3), 7)
                  + pad (fx (r.toneWorst, 1) + " / " + fx (r.toneMedian, 1), 16) + pad (fx (r.burst, 1), 7)
                  + fx (r.width[0], 1) + " " + fx (r.width[1], 1) + " " + fx (r.width[2], 1)
                  + (defect && k > 0 ? "   WORSE THAN HALL" : ""));
        }

        info ("");
        info ("  'WORSE THAN HALL' means: the worst tone folds more than 3 dB deeper");
        info ("  than Hall's, or 5 points more partials are hard-placed, or the");
        info ("  channels are more than 1 dB out of balance or mirror symmetry, or");
        info ("  the broadband fold-down is 1.5 dB worse.");

        checkEqual (static_cast<long long> (worse), 0LL,
                    "FINDING: no structure is worse than Hall on the Phase 11 stereo and mono checks");
    }

    void runTone()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.10 - tonal neutrality");
        // ===================================================================

        const auto all = everything();

        info ("  Phase 12 does not own tone. Two things are measured against Hall");
        info ("  at the defaults: the octave-band energy of the whole response");
        info ("  (125 Hz-8 kHz, total normalised out), and the RT60 in three bands.");
        info ("");
        info ("    " + pad ("structure", 34) + pad ("largest band deviation, dB", 30)
              + "band RT60 / Hall at 250 Hz, 1 kHz, 4 kHz");

        constexpr double bands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

        struct Row { double energy[7] {}, rt[3] {}; };
        std::vector<Row> rows (all.size());

        runParallel (all.size(), [&] (size_t k)
        {
            const User u;
            const auto ir = impulseResponse (all[k], u, irSecondsFor (u.decay));

            double total = 0.0;

            for (int b = 0; b < 7; ++b)
            {
                for (double v : detail::bandEnergy (ir, bands[b], ir.l.size()))
                    rows[k].energy[b] += v;

                total += rows[k].energy[b];
            }

            for (auto& e : rows[k].energy)
                e /= total;

            rows[k].rt[0] = bandRt60 (ir, 250.0);
            rows[k].rt[1] = bandRt60 (ir, 1000.0);
            rows[k].rt[2] = bandRt60 (ir, 4000.0);
        });

        double worstDb = 0.0, worstRt = 0.0;

        for (size_t k = 0; k < all.size(); ++k)
        {
            double deviation = 0.0;

            for (int b = 0; b < 7; ++b)
                deviation = std::max (deviation, std::abs (10.0 * std::log10 (rows[k].energy[b]
                                                                              / rows[0].energy[b])));

            std::string rt;

            for (int b = 0; b < 3; ++b)
            {
                const double ratio = rows[k].rt[b] / rows[0].rt[b];

                rt += fx (ratio, 3) + "  ";

                // The lossy candidate is excluded from this figure: its decay
                // is not one exponential and 12.3.8 already says so.
                if (all[k].blockDecayRatio >= 1.0)
                    worstRt = std::max (worstRt, std::abs (ratio - 1.0));
            }

            worstDb = std::max (worstDb, deviation);

            info ("    " + pad (all[k].name, 34) + pad (fx (deviation, 2), 30) + rt);
        }

        info ("");
        checkAtMost (worstDb, 1.0, "no structure moves an octave band more than 1 dB against Hall");
        checkAtMost (worstRt, 0.10, "FINDING: no lossless structure moves a band RT60 more than 10 % against Hall");
    }

    void runCost()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.11 - CPU and memory");
        // ===================================================================

        const auto all = everything();

        info ("  The research rig is not optimised: the custom matrix is a dense");
        info ("  64-multiply product where shipping uses a 24-add butterfly, and the");
        info ("  dispersion is a scalar loop. The ratios are therefore UPPER bounds");
        info ("  on what a production implementation would cost. Single thread,");
        info ("  best of five, 2 s of noise, whole wet path.");
        info ("");
        info ("    " + pad ("structure", 34) + pad ("ns / sample", 14) + pad ("x Hall", 9) + "added state");

        const auto noise = whiteNoise (static_cast<size_t> (2.0 * kRate), 0xC0FFEEu);

        std::vector<double> ns (all.size(), 0.0);

        for (size_t k = 0; k < all.size(); ++k)
        {
            double best = 1.0e30;

            for (int attempt = 0; attempt < 5; ++attempt)
            {
                const User u;
                Rig rig (all[k], u);

                const auto t0 = std::chrono::steady_clock::now();
                const auto out = rig.render (noise, noise, noise.size());
                const auto t1 = std::chrono::steady_clock::now();

                (void) out;

                best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count()
                                       / static_cast<double> (noise.size()));
            }

            ns[k] = best;
        }

        gCostRatio.assign (all.size(), 1.0);

        for (size_t k = 0; k < all.size(); ++k)
        {
            const auto& s = all[k];

            // Added state, in doubles, at 48 kHz and the default Size.
            double added = 0.0;
            std::string what;

            if (s.preDispersion > 0)
            {
                added += 2.0 * s.preDispersion;
                what += std::to_string (2 * s.preDispersion) + " allpass states; ";
            }

            if (s.loopAllpasses > 0 || s.readout == ReadoutKind::multiTap)
            {
                const User u;
                Rig rig (s, u);

                double lineSamples = 0.0;

                for (int i = 0; i < N; ++i)
                    lineSamples += rig.getCore().getDelaySamples (i);

                if (s.loopAllpasses > 0)
                {
                    double fraction = 0.0;

                    for (int j = 0; j < s.loopAllpasses; ++j)
                        fraction += patterns::loopFraction[j];

                    added += fraction * lineSamples;
                    what += "in-loop allpass buffers; ";
                }

                if (s.readout == ReadoutKind::multiTap)
                {
                    added += lineSamples;
                    what += "a second copy of every line (production would tap the line itself: 0); ";
                }
            }

            if (s.matrix == MatrixKind::coupledBlocks)
                what += "none: same eight lines, a different matrix; ";

            if (s.diffuserStages > 4)
                what += "none: stages 5-8 are already reserved in production; ";

            gCostRatio[k] = ns[k] / ns[0];

            info ("    " + pad (s.name, 34) + pad (fx (ns[k], 1), 14) + pad (fx (gCostRatio[k], 2), 9)
                  + (added > 0.0 ? fx (added * 8.0 / 1024.0, 1) + " KiB; " : "") + what);
        }

        info ("");
        info ("  SIMD AND BRANCHES. The coupled-block matrix is two 4-point Hadamard");
        info ("  butterflies (16 adds) and four plane rotations (8 multiplies): fewer");
        info ("  adds than the shipping 8-point butterfly, no branch, and the same");
        info ("  shape SIMD would want. The dispersion cascade is a serial recurrence:");
        info ("  each section needs the previous one's output, so it does not");
        info ("  vectorise ALONG the cascade - only across the two channels.");

        double worst = 0.0;

        for (double r : gCostRatio)
            worst = std::max (worst, r);

        check (worst < 2.0, "FINDING: no candidate doubles the cost of the wet path (worst x "
                            + fx (worst) + ", unoptimised)");
    }

    void runSwitching()
    {
        // ===================================================================
        beginSuite ("PHASE 12.3.12 - Mode switching between different topologies");
        // ===================================================================

        info ("  Stage 2 found that a Mode change should be a primed output");
        info ("  crossfade between two networks, the mechanism Size already uses.");
        info ("  That was measured between networks of ONE topology. Here the");
        info ("  incoming network is a different topology, so no state can be");
        info ("  carried across: the only question is whether priming still works.");
        info ("  Steady noise in; the incoming network is started from silence,");
        info ("  runs on the live input for a priming time, then a 120 ms equal-");
        info ("  power crossfade. Level is read in 10 ms windows.");
        info ("");
        info ("    " + pad ("Hall ->", 34) + pad ("primed 400 ms", 30) + "unprimed");
        info ("    " + pad ("", 34) + pad ("worst excursion, dB", 30) + "worst dip, dB");

        const auto all = everything();

        const auto total = static_cast<size_t> (3.0 * kRate);
        const auto noise = whiteNoise (total, 0xFADEu);

        struct Row { double primed = 0.0, unprimed = 0.0; };
        std::vector<Row> rows (all.size());

        runParallel (all.size(), [&] (size_t k)
        {
            for (int mode = 0; mode < 2; ++mode)
            {
                const User u;

                Rig from (hall(), u), to (all[k], u);
                ScopedFtz ftz;

                const size_t startAt = static_cast<size_t> (1.5 * kRate);
                const size_t prime   = mode == 0 ? static_cast<size_t> (0.4 * kRate) : 0;
                const size_t fade    = static_cast<size_t> (0.12 * kRate);

                // Where the incoming network would settle, for a reference.
                Rig settled (all[k], u);

                std::vector<double> out (total), target (total);

                for (size_t n = 0; n < total; ++n)
                {
                    double al = noise[n], ar = noise[n];
                    from.processSample (al, ar);

                    double sl = noise[n], sr = noise[n];
                    settled.processSample (sl, sr);

                    double bl = 0.0, br = 0.0;

                    if (n >= startAt)
                    {
                        bl = noise[n];
                        br = noise[n];
                        to.processSample (bl, br);
                    }

                    double x = 0.0;

                    if (n >= startAt + prime)
                        x = std::min (1.0, static_cast<double> (n - startAt - prime) / fade);

                    const double ga = std::cos (x * 1.5707963267948966);
                    const double gb = std::sin (x * 1.5707963267948966);

                    out[n]    = (ga * al + gb * bl) * (ga * al + gb * bl) + (ga * ar + gb * br) * (ga * ar + gb * br);
                    target[n] = ga * ga * (al * al + ar * ar) + gb * gb * (sl * sl + sr * sr);
                }

                // 10 ms windows, smoothed over 5, from the switch to 0.6 s on.
                const size_t window = msToSamples (50.0);
                double worst = 0.0;

                for (size_t n = startAt; n + window < startAt + static_cast<size_t> (0.9 * kRate); n += msToSamples (10.0))
                {
                    double a = 0.0, b = 0.0;

                    for (size_t j = 0; j < window; ++j)
                    {
                        a += out[n + j];
                        b += target[n + j];
                    }

                    const double db = 10.0 * std::log10 (std::max (1.0e-30, a) / std::max (1.0e-30, b));

                    worst = mode == 0 ? std::max (worst, std::abs (db)) : std::min (worst, db);
                }

                (mode == 0 ? rows[k].primed : rows[k].unprimed) = worst;
            }
        });

        double worstPrimed = 0.0;

        for (size_t k = 0; k < all.size(); ++k)
        {
            worstPrimed = std::max (worstPrimed, rows[k].primed);

            info ("    " + pad (all[k].name, 34) + pad (fx (rows[k].primed), 30) + fx (rows[k].unprimed));
        }

        info ("");
        info ("  The reference is what the level WOULD be if the incoming network");
        info ("  had been running all along. A primed excursion is how far the real");
        info ("  crossfade strays from that.");

        checkAtMost (worstPrimed, 3.0,
                     "FINDING: a 400 ms primed crossfade holds level within 3 dB into every topology");
    }
}

void runPhase12StructuralCandidates()
{
    if (std::getenv ("AV_S3_SKIP_LEVERS") == nullptr)
        runLevers();

    runCandidates();
    runCross();
    runSemantics();
    runStereo();
    runTone();
    runCost();
    runSwitching();
}

} // namespace avtest
