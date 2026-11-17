#include "Phase12Stage5.h"
#include "TestFramework.h"

#include "DampingListening.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include <algorithm>
#include <cmath>
#include <memory>

/*
    PHASE 12 - STAGE 5. The four parts, in the order they are worked:
    Plate, Room, Chamber, Ambient.

    RESEARCH ONLY. Every candidate here is the one written down in
    Docs/Phase12-Stage5-Preregistration.md, parameter for parameter, unless a
    comment says it was appended later and where.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace stage5
{

namespace
{
    // =======================================================================
    //  Small shared pieces
    // =======================================================================
    [[nodiscard]] std::vector<double> monoOf (const std::vector<StereoFrame>& frames, double seconds)
    {
        const size_t n = std::min (frames.size(), static_cast<size_t> (seconds * kRate));

        std::vector<double> m (n);

        for (size_t i = 0; i < n; ++i)
            m[i] = 0.5 * (frames[i].l + frames[i].r);

        const size_t fade = std::min (n, static_cast<size_t> (0.030 * kRate));

        for (size_t i = 0; i < fade; ++i)
            m[n - 1 - i] *= static_cast<double> (i) / static_cast<double> (fade);

        return m;
    }

    struct Source { std::string name; std::vector<double> m; };

    [[nodiscard]] const std::vector<Source>& centredSources()
    {
        static const std::vector<Source> sources = []
        {
            std::vector<Source> s;

            s.push_back ({ "vocal", monoOf (makeVocalReference (kRate, 2.2), 2.2) });

            for (const auto& f : makeFu2Sources (kRate))
                if (f.name == "snare" || f.name == "piano")
                    s.push_back ({ f.name, monoOf (f.frames, 2.74) });

            s.push_back ({ "percussion", monoOf (makeBrightPercussion (kRate, 2.0), 2.0) });

            return s;
        }();

        return sources;
    }

    /** RMS difference, dB, between the 20 ms energy envelopes of two renders
        of the same centred source, level-matched, over every window within
        40 dB of the loudest. What a level meter would show of the difference. */
    [[nodiscard]] double envelopeDifference (const Stereo& a, const Stereo& b)
    {
        const size_t window = static_cast<size_t> (0.020 * kRate);
        const size_t count = std::min (a.l.size(), b.l.size()) / window;

        std::vector<double> ea (count, 0.0), eb (count, 0.0);

        double ta = 0.0, tb = 0.0;

        for (size_t w = 0; w < count; ++w)
            for (size_t n = w * window; n < (w + 1) * window; ++n)
            {
                ea[w] += a.l[n] * a.l[n] + a.r[n] * a.r[n];
                eb[w] += b.l[n] * b.l[n] + b.r[n] * b.r[n];
            }

        for (size_t w = 0; w < count; ++w) { ta += ea[w]; tb += eb[w]; }

        if (ta <= 0.0 || tb <= 0.0)
            return 0.0;

        const double gain = ta / tb;
        const double peak = *std::max_element (ea.begin(), ea.end());

        double sum = 0.0;
        int used = 0;

        for (size_t w = 0; w < count; ++w)
        {
            const double x = ea[w], y = eb[w] * gain;

            if (std::max (x, y) < 1.0e-4 * peak)
                continue;

            const double db = std::clamp (10.0 * std::log10 (std::max (1.0e-30, y) / std::max (1.0e-30, x)),
                                          -30.0, 30.0);

            sum += db * db;
            ++used;
        }

        return used > 0 ? std::sqrt (sum / used) : 0.0;
    }

    /** The centred-source table: each candidate against Hall on four musical
        sources at the medium point, beside what a sibling delay set of Hall
        itself shows on the same measure. */
    void centredSourceTable (const std::vector<Candidate>& list)
    {
        const auto& sources = centredSources();
        const User u = userAt (kPoints[0]);

        std::vector<Candidate> all;
        all.push_back (fixed ("Hall, sibling delay set", withDelays ("sibling", mode12::hallSibling().delaysMs)));

        for (const auto& c : list)
            all.push_back (c);

        std::vector<double> difference (all.size() * sources.size());

        std::vector<Stereo> reference (sources.size());

        const size_t tail = static_cast<size_t> (3.0 * kRate);

        runParallel (sources.size(), [&] (size_t i)
        {
            reference[i] = std::make_unique<Rig> (Structure {}, u)->render (sources[i].m, sources[i].m,
                                                                           sources[i].m.size() + tail);
        });

        runParallel (difference.size(), [&] (size_t i)
        {
            const auto& source = sources[i % sources.size()];
            const auto out = std::make_unique<Rig> (all[i / sources.size()].make (u), u)
                                 ->render (source.m, source.m, source.m.size() + tail);

            difference[i] = envelopeDifference (reference[i % sources.size()], out);
        });

        info ("");
        info ("    CENTRED MUSICAL SOURCES, same knobs, medium point, level-matched: RMS difference");
        info ("    of the 20 ms level envelope against Hall, dB. The first row is Hall against a");
        info ("    sibling of itself: what 'the same reverb' looks like on this measure.");

        std::string header = "      " + pad ("structure", 30);

        for (const auto& s : sources)
            header += pad (s.name, 12);

        info (header);

        for (size_t k = 0; k < all.size(); ++k)
        {
            std::string row = "      " + pad (all[k].name, 30);

            for (size_t i = 0; i < sources.size(); ++i)
                row += pad (fx (difference[k * sources.size() + i]), 12);

            info (row);
        }
    }

    [[nodiscard]] double lowestMedian (const Gate& g)
    {
        return *std::min_element (g.median, g.median + kPointCount);
    }

    [[nodiscard]] bool alive (const Gate& g)
    {
        return g.verdict == "SURVIVES" || g.verdict == "NOT REFUTED" || g.verdict == "passes G1";
    }

    // =======================================================================
    //  PART A - Plate
    // =======================================================================
    struct Reduced
    {
        std::string name, family;
        std::vector<ChainSection> chain;
        Structure structure;

        int sections = 0, order = 0;
        double vsHall[kPointCount] {}, vsRef[kPointCount] {};
        double arrival[3] {};       // at the medium point
        double cpu = 0.0;

        [[nodiscard]] double worstVsRef() const { return *std::max_element (vsRef, vsRef + kPointCount); }
        [[nodiscard]] double weakestVsHall() const { return *std::min_element (vsHall, vsHall + kPointCount); }
    };

    [[nodiscard]] std::vector<Reduced> buildPlateGrid()
    {
        std::vector<Reduced> grid;

        auto add = [&] (const std::string& family, const std::string& name, std::vector<ChainSection> chain)
        {
            Reduced r;
            r.family = family;
            r.name = name;
            r.chain = chain;
            r.sections = static_cast<int> (chain.size());

            for (const auto& c : chain)
                r.order += c.stretch;

            r.structure.name = name;
            r.structure.preChain = chain;

            grid.push_back (r);
        };

        auto uniform = [] (int n, double a, int stretch)
        {
            return std::vector<ChainSection> (static_cast<size_t> (n), ChainSection { a, stretch });
        };

        for (int n : { 240, 160, 128, 96, 80, 64, 48, 32 })
            for (double a : { -0.7, -0.8, -0.9 })
                add ("U", "U " + std::to_string (n) + " @ " + fx (a, 1), uniform (n, a, 1));

        for (int n : { 120, 96, 80, 64, 48, 32 })
            for (double a : { -0.5, -0.6, -0.7 })
                add ("S2", "S2 " + std::to_string (n) + " @ " + fx (a, 1), uniform (n, a, 2));

        for (int n : { 80, 64, 48, 32 })
            for (double a : { -0.4, -0.5, -0.6 })
                add ("S3", "S3 " + std::to_string (n) + " @ " + fx (a, 1), uniform (n, a, 3));

        // Mixed strength: low-frequency delay (1 + |a|) / (1 - |a|) spread
        // evenly from that of -0.5 (3 samples) to that of -0.9 (19).
        for (int n : { 96, 64, 48 })
        {
            std::vector<ChainSection> chain;

            for (int i = 0; i < n; ++i)
            {
                const double delay = 3.0 + 16.0 * i / (n - 1.0);
                chain.push_back ({ -(delay - 1.0) / (delay + 1.0), 1 });
            }

            add ("M", "M " + std::to_string (n) + " mixed", chain);
        }

        for (int n : { 96, 72, 48 })
        {
            auto chain = uniform (n / 3, -0.7, 1);
            const auto b = uniform (n / 3, -0.6, 2), c = uniform (n / 3, -0.5, 3);

            chain.insert (chain.end(), b.begin(), b.end());
            chain.insert (chain.end(), c.begin(), c.end());

            add ("G3", "G3 " + std::to_string (n) + " thirds", chain);
        }

        for (int n : { 80, 64, 48 })
        {
            auto chain = uniform (n / 2, -0.6, 2);
            const auto c = uniform (n / 2, -0.5, 3);

            chain.insert (chain.end(), c.begin(), c.end());

            add ("G2", "G2 " + std::to_string (n) + " halves", chain);
        }

        return grid;
    }

    [[nodiscard]] double chainDelayMs (const std::vector<ChainSection>& chain, double hz)
    {
        double samples = 0.0;

        for (const auto& c : chain)
            samples += c.stretch * dispersionDelay (c.a, hz * c.stretch, kRate);

        return samples * 1000.0 / kRate;
    }

    [[nodiscard]] Structure plateReference()
    {
        Structure s;
        s.name = "Plate A (reference)";
        s.preDispersion = 240;
        return s;
    }
}

Outcome runPlate()
{
    Outcome outcome;

    // =======================================================================
    beginSuite ("PHASE 12.5.1 - Plate: the 240-section reference");
    // =======================================================================

    const Structure reference = plateReference();
    const std::vector<ChainSection> referenceChain (240, ChainSection { -0.7, 1 });

    info ("  TOPOLOGY. Per channel, ahead of the tank and after the input diffuser:");
    info ("  240 identical first-order allpass sections in series,");
    info ("      y = a x + s;   s = x - a y;   a = -0.7,");
    info ("  one state per section. No delay lines, no feedback around the chain,");
    info ("  nothing time-varying. Magnitude is flat by construction.");
    info ("");
    info ("  COEFFICIENTS: all -0.7. DELAYS: none; each section's delay is its");
    info ("  group delay, 5.67 samples at DC falling to 0.18 at Nyquist.");
    info ("  MEMORY: 2 x 240 doubles = 3.75 KiB. ORDER: 240 per channel, so 240 pi");
    info ("  of phase: the area under the group-delay curve is fixed by that.");
    info ("  SERIAL DEPENDENCY: section k needs section k-1's output of the same");
    info ("  sample: 240 multiply-adds in a row per channel per sample.");
    info ("");

    {
        std::string row = "  GROUP DELAY, ms:";

        for (double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0 })
            row += "  " + fx (hz >= 1000.0 ? hz / 1000.0 : hz, 0) + (hz >= 1000.0 ? "k " : " ")
                 + fx (chainDelayMs (referenceChain, hz), 1);

        info (row);
    }

    info ("  Density, transient behaviour and the CPU breakdown of the reference are");
    info ("  Stage 4's sections 12.4.3 and 12.4.4 and are not repeated here.");

    // =======================================================================
    beginSuite ("PHASE 12.5.2 - Plate: the reduction grid");
    // =======================================================================

    auto grid = buildPlateGrid();

    info ("  " + std::to_string (grid.size()) + " chains, the list fixed in the pre-registration. For each, at the");
    info ("  same knobs and three operating points: the centred score against Hall");
    info ("  (far is good) and against the 240-section reference (near is good);");
    info ("  and the wet-path cost. 'order' is the phase carried, in sections of");
    info ("  the reference's kind: the reference is 240.");
    info ("");

    // Features: Hall, the reference, and every chain, at three points.
    const size_t rows = grid.size() + 2;
    std::vector<Features> features (rows * kPointCount);

    runParallel (features.size(), [&] (size_t i)
    {
        const size_t k = i / kPointCount;
        const User u = userAt (kPoints[i % kPointCount]);

        features[i] = featuresOf (k == 0 ? Structure {} : k == 1 ? reference : grid[k - 2].structure, u);
    });

    for (size_t k = 0; k < grid.size(); ++k)
        for (int p = 0; p < kPointCount; ++p)
        {
            const auto& f = features[(k + 2) * kPointCount + static_cast<size_t> (p)];

            grid[k].vsHall[p] = centredWorst (sameKnobs (features[static_cast<size_t> (p)], f));
            grid[k].vsRef[p]  = centredWorst (sameKnobs (features[kPointCount + static_cast<size_t> (p)], f));

            if (p == 0)
                for (int b = 0; b < 3; ++b)
                    grid[k].arrival[b] = f.arrival[b];
        }

    double referenceVsHall[kPointCount] {};

    for (int p = 0; p < kPointCount; ++p)
        referenceVsHall[p] = centredWorst (sameKnobs (features[static_cast<size_t> (p)],
                                                      features[kPointCount + static_cast<size_t> (p)]));

    // Cost, one at a time, nothing else running.
    const double referenceCpu = wetPathRatio (reference);

    for (auto& r : grid)
        r.cpu = wetPathRatio (r.structure);

    info ("    " + pad ("chain", 18) + pad ("sections", 10) + pad ("order", 7) + pad ("vs Hall  med sma lar", 24)
          + pad ("vs Plate A  med sma lar", 26) + pad ("x Hall", 8) + pad ("250 / 4k / 8k arrival", 24) + "");

    info ("    " + pad ("Plate A", 18) + pad ("240", 10) + pad ("240", 7)
          + pad (fx (referenceVsHall[0]) + " " + fx (referenceVsHall[1]) + " " + fx (referenceVsHall[2]), 24)
          + pad ("0.00 0.00 0.00", 26) + pad (fx (referenceCpu), 8)
          + fx (features[kPointCount].arrival[0], 1) + " / " + fx (features[kPointCount].arrival[1], 1)
          + " / " + fx (features[kPointCount].arrival[2], 1));

    for (const auto& r : grid)
    {
        const bool preserved = r.worstVsRef() <= kReproduced;

        info ("    " + pad (r.name, 18) + pad (std::to_string (r.sections), 10) + pad (std::to_string (r.order), 7)
              + pad (fx (r.vsHall[0]) + " " + fx (r.vsHall[1]) + " " + fx (r.vsHall[2]), 24)
              + pad (fx (r.vsRef[0]) + " " + fx (r.vsRef[1]) + " " + fx (r.vsRef[2]), 26)
              + pad (fx (r.cpu), 8)
              + pad (fx (r.arrival[0], 1) + " / " + fx (r.arrival[1], 1) + " / " + fx (r.arrival[2], 1), 24)
              + (preserved ? (r.cpu <= 1.5 ? "PRESERVED, <= 1.5 x" : r.cpu <= 1.75 ? "PRESERVED, <= 1.75 x"
                                                                                   : "PRESERVED") : ""));
    }

    // The identity check: the reference rebuilt as a chain is the reference.
    check (grid.front().worstVsRef() == 0.0,
           "the 240-section chain rebuilt section by section scores 0 against the reference");

    // --- the area prediction ------------------------------------------------
    {
        int cheapestOrder = 1 << 30;
        std::string who = "none";

        for (const auto& r : grid)
            if (r.family != "U" || r.sections != 240)
                if (r.worstVsRef() <= kReproduced && r.order < cheapestOrder)
                {
                    cheapestOrder = r.order;
                    who = r.name;
                }

        int cheapestPlain = 1 << 30;

        for (const auto& r : grid)
            if ((r.family == "U" || r.family == "M") && r.sections < 240 && r.worstVsRef() <= kReproduced)
                cheapestPlain = std::min (cheapestPlain, r.sections);

        info ("");
        info ("  THE PREDICTION. Written before measuring: phase is order x pi, so no");
        info ("  arrangement of ordinary sections much below ~170 can keep the curve.");
        info ("    fewest ordinary sections (U, M) that stay PRESERVED: "
              + (cheapestPlain == (1 << 30) ? std::string ("none below 240") : std::to_string (cheapestPlain)));
        info ("    lowest ORDER of any PRESERVED chain: "
              + (cheapestOrder == (1 << 30) ? std::string ("none") : std::to_string (cheapestOrder) + "  (" + who + ")"));

        check (cheapestPlain >= 128,
               "FINDING: as predicted, no ordinary-section chain under 128 sections preserves the reference");
    }

    // --- shortlist ----------------------------------------------------------
    std::vector<const Reduced*> eligible, shortlist;

    for (const auto& r : grid)
        if (r.sections < 240 && r.cpu <= 1.75)
            eligible.push_back (&r);

    std::vector<const Reduced*> preserved;

    for (const auto* r : eligible)
        if (r->worstVsRef() <= kReproduced)
            preserved.push_back (r);

    const bool identityKept = ! preserved.empty();

    if (identityKept)
    {
        std::sort (preserved.begin(), preserved.end(),
                   [] (const Reduced* a, const Reduced* b) { return a->cpu < b->cpu; });
        shortlist.assign (preserved.begin(), preserved.begin() + std::min<size_t> (3, preserved.size()));
    }
    else
    {
        std::sort (eligible.begin(), eligible.end(),
                   [] (const Reduced* a, const Reduced* b) { return a->worstVsRef() < b->worstVsRef(); });
        shortlist.assign (eligible.begin(), eligible.begin() + std::min<size_t> (3, eligible.size()));
    }

    info ("");
    info (identityKept
            ? "  SHORTLIST (rule: PRESERVED and <= 1.75 x Hall, the three cheapest):"
            : "  NO chain under 1.75 x Hall is PRESERVED. Shortlist: the three closest to Plate A under 1.75 x.");

    for (const auto* r : shortlist)
        info ("    " + pad (r->name, 18) + "x " + fx (r->cpu) + "   worst against Plate A " + fx (r->worstVsRef())
              + "   weakest against Hall " + fx (r->weakestVsHall()));

    check (identityKept, "FINDING: a chain within 1.75 x Hall preserves the reference's identity");

    // =======================================================================
    beginSuite ("PHASE 12.5.3 - Plate: the shortlist against Hall's search");
    // =======================================================================

    info ("  G2: five seeds per point, centred view. Reference-equivalent means no");
    info ("  run <= 1.5 and a median at each point no lower than the reference's");
    info ("  own ten-seed minimum there (2.95, 2.54, 2.78; Stage 4).");
    info ("");

    std::vector<Candidate> targets;

    for (const auto* r : shortlist)
        targets.push_back (fixed (r->name, r->structure));

    const Candidate hallC = fixed ("Hall", Structure {});
    const auto gates = gate (targets, hallC, kSeeds);

    printGate (targets, gates);

    constexpr double referenceMinimum[kPointCount] { 2.95, 2.54, 2.78 };

    int recommended = -1;

    info ("");

    for (size_t k = 0; k < targets.size(); ++k)
    {
        bool equivalent = gates[k].minimum > kReproduced;

        for (int p = 0; p < kPointCount; ++p)
            if (gates[k].median[p] < referenceMinimum[p])
                equivalent = false;

        info ("    " + pad (targets[k].name, 18) + (equivalent ? "reference-equivalent" : "NOT reference-equivalent")
              + "; class by the frozen rule: " + gates[k].verdict);

        if (equivalent && recommended < 0 && gates[k].verdict != "REJECTED")
            recommended = static_cast<int> (k);
    }

    // --- appended after the first run (pre-registration, addendum 1) -------
    // The shortlist rule asks what survives under 1.75 x. This asks the other
    // question the next stage needs: what is the CHEAPEST chain that is still
    // Plate A, whatever it costs.
    std::vector<const Reduced*> cheapestPreserved;

    for (const auto& r : grid)
        if (r.sections < 240 && r.worstVsRef() <= kReproduced)
            cheapestPreserved.push_back (&r);

    std::sort (cheapestPreserved.begin(), cheapestPreserved.end(),
               [] (const Reduced* a, const Reduced* b) { return a->sections < b->sections
                                                             || (a->sections == b->sections && a->cpu < b->cpu); });

    if (cheapestPreserved.size() > 3)
        cheapestPreserved.resize (3);

    int floorChain = -1;
    std::vector<Candidate> floorTargets;

    if (! cheapestPreserved.empty())
    {
        info ("");
        info ("  THE FLOOR, WHATEVER IT COSTS (appended; addendum 1). The three PRESERVED");
        info ("  chains with the fewest sections, through the same G2:");
        info ("");

        for (const auto* r : cheapestPreserved)
            floorTargets.push_back (fixed (r->name, r->structure));

        const auto floorGates = gate (floorTargets, hallC, kSeeds);

        printGate (floorTargets, floorGates);

        info ("");

        for (size_t k = 0; k < floorTargets.size(); ++k)
        {
            bool equivalent = floorGates[k].minimum > kReproduced;

            for (int p = 0; p < kPointCount; ++p)
                if (floorGates[k].median[p] < referenceMinimum[p])
                    equivalent = false;

            info ("    " + pad (floorTargets[k].name, 18) + "x " + fx (cheapestPreserved[k]->cpu) + " Hall   "
                  + (equivalent ? "reference-equivalent" : "NOT reference-equivalent")
                  + "; class: " + floorGates[k].verdict);

            if (equivalent && floorChain < 0)
                floorChain = static_cast<int> (k);
        }
    }

    // =======================================================================
    beginSuite ("PHASE 12.5.4 - Plate: semantics, safety and cost of the recommendation");
    // =======================================================================

    std::vector<Candidate> semanticsList;
    semanticsList.push_back (fixed ("Plate A (reference)", reference));

    for (const auto& t : targets)
        semanticsList.push_back (t);

    for (const auto& t : floorTargets)
        semanticsList.push_back (t);

    const auto g3 = semantics (semanticsList, 0.10, 1.75);

    centredSourceTable (semanticsList);

    if (recommended >= 0 && ! g3[static_cast<size_t> (recommended) + 1].pass)
    {
        // Fall through the shortlist for one that also passes G3.
        int alternative = -1;

        for (size_t k = 0; k < targets.size(); ++k)
        {
            bool equivalent = gates[k].minimum > kReproduced && gates[k].verdict != "REJECTED";

            for (int p = 0; p < kPointCount; ++p)
                if (gates[k].median[p] < referenceMinimum[p])
                    equivalent = false;

            if (equivalent && g3[k + 1].pass) { alternative = static_cast<int> (k); break; }
        }

        recommended = alternative;
    }

    if (recommended >= 0)
    {
        const auto* r = shortlist[static_cast<size_t> (recommended)];

        outcome.found = true;
        outcome.best = targets[static_cast<size_t> (recommended)];
        outcome.status = gates[static_cast<size_t> (recommended)].verdict + " - " + r->name + ", "
                       + std::to_string (r->sections) + " sections, x " + fx (g3[static_cast<size_t> (recommended) + 1].cpu)
                       + " Hall, reference-equivalent";

        info ("");
        info ("  RECOMMENDED: " + r->name + "  (" + std::to_string (r->sections) + " sections per channel, order "
              + std::to_string (r->order) + ")");

        std::string row = "    group delay, ms:";

        for (double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0 })
            row += "  " + fx (hz >= 1000.0 ? hz / 1000.0 : hz, 0) + (hz >= 1000.0 ? "k " : " ")
                 + fx (chainDelayMs (r->chain, hz), 1);

        info (row);
        info ("    memory: " + fx (2.0 * r->order * 8.0 / 1024.0, 2) + " KiB of state.");
    }
    else if (floorChain >= 0)
    {
        const auto* r = cheapestPreserved[static_cast<size_t> (floorChain)];

        outcome.found = false;
        outcome.best = floorTargets[static_cast<size_t> (floorChain)];
        outcome.status = "BLOCKED ON CPU - cheapest chain that is still Plate A: " + r->name + ", "
                       + std::to_string (r->sections) + " sections, x " + fx (r->cpu)
                       + " Hall; nothing under 1.75 x preserves the identity";
    }
    else
    {
        outcome.found = false;
        outcome.best = fixed ("Plate A (reference)", reference);
        outcome.status = "NO REDUCTION FOUND - the 240-section reference stands (NOT REFUTED, about 3-4 x Hall)";
    }

    check (recommended >= 0, "FINDING: a reduced Plate is reference-equivalent, passes G3 and costs <= 1.75 x Hall");

    return outcome;
}

// ===========================================================================
//  PART B - Room
// ===========================================================================
namespace
{
    [[nodiscard]] Candidate roomLocal (const std::string& name, double scale, bool hearBoth)
    {
        Blocks b;
        b.blocks.push_back ({ { 0, 1, 2, 3 }, scale, Mix::hadamard, 1.0 });
        b.blocks.push_back ({ { 4, 5, 6, 7 }, 1.0,   Mix::hadamard, 1.0 });
        b.theta = { 0.25 };
        b.in = { 0 };
        b.out = hearBoth ? std::vector<int> { 0, 1 } : std::vector<int> { 0 };

        return levelled (fixed (name, build (name, b)));
    }

    /** ADDENDUM 2. R1a's local block, with the exchange between the block and
        the field timed from Decay instead of fixed as an angle, and the RT60
        calibrated. */
    [[nodiscard]] Candidate roomTimed (const std::string& name, double scale, double k)
    {
        const Maker make = [name, scale, k] (const User& u)
        {
            auto base = Structure {}.delaysMs;
            std::sort (base.begin(), base.end());

            double mean = 0.0;

            for (int i = 0; i < 4; ++i)
                mean += base[static_cast<size_t> (i)];

            mean = mean / 4.0 * scale * mapping::sizeScaleFor (u.size) * 0.001;

            const double tau = k * mapping::rt60For (u.decay);

            Blocks b;
            b.blocks.push_back ({ { 0, 1, 2, 3 }, scale, Mix::hadamard, 1.0 });
            b.blocks.push_back ({ { 4, 5, 6, 7 }, 1.0,   Mix::hadamard, 1.0 });
            b.theta = { std::asin (std::sqrt (std::min (0.5, mean / tau))) };
            b.in = { 0 };
            b.out = { 0 };

            return build (name, b);
        };

        return levelled (decayCalibrated ({ name, make }));
    }

    [[nodiscard]] std::vector<Candidate> roomCandidates()
    {
        std::vector<Candidate> c;

        c.push_back (roomLocal ("R1a local block 0.25", 0.25, false));
        c.push_back (roomLocal ("R1b local block 0.40", 0.40, false));
        c.push_back (roomLocal ("R1c local, hear both", 0.25, true));

        {
            Blocks b;
            b.blocks.push_back ({ { 0, 1, 2, 3 }, 0.20, Mix::hadamard, 1.0 });
            b.blocks.push_back ({ { 4, 5 },       0.45, Mix::rotation, 1.0 });
            b.blocks.push_back ({ { 6, 7 },       1.00, Mix::rotation, 1.0 });
            b.theta = { 0.30, 0.30 };
            b.in = { 0 };
            b.out = { 0, 1, 2 };

            c.push_back (levelled (fixed ("R2 multi-region", build ("R2 multi-region", b))));
        }
        {
            Blocks b;
            b.blocks.push_back ({ { 0, 1, 2, 3 }, 0.30, Mix::identity, 1.0 });
            b.blocks.push_back ({ { 4, 5, 6, 7 }, 1.00, Mix::hadamard, 1.0 });
            b.theta = { 0.20 };
            b.in = { 0 };
            b.out = { 0, 1 };

            c.push_back (levelled (fixed ("R3 wall pairs", build ("R3 wall pairs", b))));
        }

        // ADDENDUM 2, appended after R1-R3 were read.
        c.push_back (roomTimed ("R4a timed block 0.25", 0.25, 0.06));
        c.push_back (roomTimed ("R4b timed block 0.40", 0.40, 0.06));
        c.push_back (roomTimed ("R4c timed, slower",    0.25, 0.12));

        return c;
    }

    /** Flutter, ringing, colour and short-decay figures for a list, against Hall. */
    void qualityTable (const std::vector<Candidate>& list)
    {
        std::vector<Candidate> all;
        all.push_back (fixed ("Hall", Structure {}));

        for (const auto& c : list)
            all.push_back (c);

        struct Row { double lag = 0, longLag = 0, modal = 0, ripple = 0, shortRt = 0; bool finite = true; };

        std::vector<Row> rows (all.size());
        std::vector<std::vector<double>> bands (all.size());

        runParallel (all.size(), [&] (size_t k)
        {
            auto& row = rows[k];

            // The worst of the three points for the repetition and modal figures.
            for (const auto& p : kPoints)
            {
                const User u = userAt (p);
                const auto f = featuresOf (all[k].make (u), u);

                row.lag     = std::max (row.lag, f.lagPeak);
                row.longLag = std::max (row.longLag, f.longLagPeak);
                row.modal   = std::max (row.modal, f.modalDb);
            }

            // Twenty-four bands, 100 Hz - 10 kHz, at the defaults.
            {
                const User u;
                const auto ir = impulseResponse (all[k].make (u), u, irSecondsFor (u.decay));

                double total = 0.0;

                for (int b = 0; b < 24; ++b)
                {
                    const double hz = 100.0 * std::pow (100.0, b / 23.0);
                    double e = 0.0;

                    for (double v : detail::bandEnergy (ir, hz, ir.l.size()))
                        e += v;

                    bands[k].push_back (e);
                    total += e;
                }

                for (auto& e : bands[k])
                    e /= total;
            }

            // Decay 0.3 s at a small Size.
            {
                User u;
                u.size = 0.25;
                u.decay = 0.3;

                const auto ir = impulseResponse (all[k].make (u), u, 1.5);

                for (size_t n = 0; n < ir.l.size(); ++n)
                    if (! std::isfinite (ir.l[n]) || ! std::isfinite (ir.r[n]))
                        row.finite = false;

                row.shortRt = mode12::rt60FromEnergy (detail::energyOfBoth (ir));
            }
        });

        for (size_t k = 0; k < all.size(); ++k)
            for (size_t b = 0; b < 24; ++b)
                rows[k].ripple = std::max (rows[k].ripple,
                                           std::abs (10.0 * std::log10 (bands[k][b] / bands[0][b])));

        info ("");
        info ("    QUALITY RISKS, against Hall. Descriptive flags; the numeric lines were chosen");
        info ("    here, not in the pre-registration, and are two metric units in each case.");
        info ("      repetition  peak autocorrelation of the flattened tail, 3-150 ms and 150-1000 ms");
        info ("                  (flutter, metallic repetition); flagged above Hall + 0.10");
        info ("      modal       prominence of single modes, dB (ringing); flagged above Hall + 3");
        info ("      colour      largest of 24 band deviations from Hall, 100 Hz - 10 kHz, dB; flagged above 1.5");
        info ("      short       RT60 at Decay 0.3 s, Size 25, against Hall's; flagged outside +/- 10 %");
        info ("");
        info ("      " + pad ("structure", 30) + pad ("repetition", 16) + pad ("modal", 9) + pad ("colour", 9)
              + pad ("short decay", 14) + "flags");

        for (size_t k = 0; k < all.size(); ++k)
        {
            const auto& r = rows[k];
            std::string flags;

            if (k > 0)
            {
                if (r.lag > rows[0].lag + 0.10 || r.longLag > rows[0].longLag + 0.10) flags += " REPETITION";
                if (r.modal > rows[0].modal + 3.0) flags += " RINGING";
                if (r.ripple > 1.5) flags += " COLOUR";
                if (! r.finite || std::abs (r.shortRt / rows[0].shortRt - 1.0) > 0.10) flags += " SHORT DECAY";
            }

            info ("      " + pad (all[k].name, 30) + pad (fx (r.lag) + " / " + fx (r.longLag), 16)
                  + pad (fx (r.modal, 1), 9) + pad (fx (r.ripple), 9)
                  + pad (fx (r.shortRt / rows[0].shortRt, 3), 14) + flags);
        }
    }

    /** G1, then G2 on what passes, then G3 on what is left. Returns the best. */
    [[nodiscard]] Outcome workFamily (const std::string& family, const std::vector<Candidate>& candidates,
                                      double decayLimit, std::vector<Candidate>* survivorsOut = nullptr)
    {
        Outcome outcome;
        outcome.status = "NO MECHANISM - every " + family + " hypothesis was rejected";

        const Candidate hallC = fixed ("Hall", Structure {});

        for (const auto& c : candidates)
        {
            const double error = orthogonalityError (c.make (User {}));

            if (error > 1.0e-9)
                info ("    NOT ORTHOGONAL: " + c.name + "  " + fx (error, 6));
        }

        info ("  G1 - one search per point, centred view. REPRODUCED anywhere is the end.");
        info ("");

        const auto quick = gate (candidates, hallC, 1);
        printGate (candidates, quick);

        std::vector<Candidate> passed;

        for (size_t k = 0; k < candidates.size(); ++k)
            if (alive (quick[k]))
                passed.push_back (candidates[k]);

        info ("");
        info ("  " + std::to_string (passed.size()) + " of " + std::to_string (candidates.size()) + " pass G1.");

        if (passed.empty())
            return outcome;

        info ("");
        info ("  G2 - five seeds per point, centred view.");
        info ("");

        const auto serious = gate (passed, hallC, kSeeds);
        printGate (passed, serious);

        std::vector<Candidate> survivors;
        std::vector<Gate> survivorGates;

        for (size_t k = 0; k < passed.size(); ++k)
            if (alive (serious[k]))
            {
                survivors.push_back (passed[k]);
                survivorGates.push_back (serious[k]);
            }

        qualityTable (passed);
        centredSourceTable (passed);

        if (survivors.empty())
            return outcome;

        info ("");
        info ("  G3 - semantics, safety and cost of what G2 left.");

        const auto g3 = semantics (survivors, decayLimit, 1.0e9);

        int best = -1;

        for (size_t k = 0; k < survivors.size(); ++k)
        {
            if (! g3[k].pass)
                continue;

            const bool better = best < 0
                || (survivorGates[k].verdict == "SURVIVES" && survivorGates[static_cast<size_t> (best)].verdict != "SURVIVES")
                || (survivorGates[k].verdict == survivorGates[static_cast<size_t> (best)].verdict
                    && lowestMedian (survivorGates[k]) > lowestMedian (survivorGates[static_cast<size_t> (best)]));

            if (better)
                best = static_cast<int> (k);
        }

        if (survivorsOut != nullptr)
            *survivorsOut = survivors;

        if (best < 0)
        {
            // Something is structurally distinct but fails a semantic limit.
            size_t strongest = 0;

            for (size_t k = 1; k < survivors.size(); ++k)
                if (lowestMedian (survivorGates[k]) > lowestMedian (survivorGates[strongest]))
                    strongest = k;

            outcome.best = survivors[strongest];
            outcome.status = "BLOCKED AT G3 - " + survivors[strongest].name + " is " + survivorGates[strongest].verdict
                           + " (lowest median " + fx (lowestMedian (survivorGates[strongest])) + ") but fails G3";
            return outcome;
        }

        outcome.found = true;
        outcome.best = survivors[static_cast<size_t> (best)];
        outcome.status = survivorGates[static_cast<size_t> (best)].verdict + " - " + outcome.best.name
                       + " (lowest median " + fx (lowestMedian (survivorGates[static_cast<size_t> (best)]))
                       + ", lowest run " + fx (survivorGates[static_cast<size_t> (best)].minimum)
                       + ", x " + fx (g3[static_cast<size_t> (best)].cpu) + " Hall)";

        return outcome;
    }
}

/** The Room and Plate structures as Parts A and B left them, for a run of Part C
    alone: R4c, which is structurally SURVIVES and blocked at G3, and the
    240-section reference. Fixed definitions; nothing is re-measured. */
Outcome standingRoom()
{
    Outcome o;
    o.best = roomTimed ("R4c timed, slower", 0.25, 0.12);
    o.status = "(not re-run: R4c as defined)";
    return o;
}

Outcome standingPlate()
{
    Outcome o;
    o.best = fixed ("Plate A (reference)", plateReference());
    o.status = "(not re-run: the 240-section reference)";
    return o;
}

Outcome runRoom()
{
    // =======================================================================
    beginSuite ("PHASE 12.5.5 - Room: eight hypotheses on a centred source");
    // =======================================================================

    info ("  Room B is closed. Every candidate is driven and scored centred, with");
    info ("  Early/Late at 0; Hall's search may raise Early/Late against it.");
    info ("");

    const auto outcome = workFamily ("Room", roomCandidates(), 0.10);

    info ("");
    info ("  ROOM: " + outcome.status);

    check (outcome.found, "FINDING: a Room hypothesis passes G1, G2 and G3");

    return outcome;
}

// ===========================================================================
//  PART C - Chamber
// ===========================================================================
namespace
{
    [[nodiscard]] std::vector<Candidate> chamberCandidates()
    {
        std::vector<Candidate> c;

        {
            Blocks b;
            b.blocks.push_back ({ { 2, 5 },             1.0, Mix::identity,    1.0 });
            b.blocks.push_back ({ { 0, 1, 3, 4, 6, 7 }, 1.0, Mix::householder, 1.0 });
            b.theta = { 0.60 };
            b.in = { 0, 1 };
            b.out = { 0, 1 };

            c.push_back (levelled (fixed ("C1a two paths", build ("C1a two paths", b))));
        }
        {
            Blocks b;
            b.blocks.push_back ({ { 1, 3, 4, 6 }, 1.0, Mix::ring,     1.0 });
            b.blocks.push_back ({ { 0, 2, 5, 7 }, 1.0, Mix::hadamard, 1.0 });
            b.theta = { 0.45 };
            b.in = { 0, 1 };
            b.out = { 0, 1 };

            c.push_back (levelled (fixed ("C1b four-path ring", build ("C1b four-path ring", b))));
        }

        auto volumes = [] (const std::string& name, double ratio, double theta)
        {
            Blocks b;
            b.blocks.push_back ({ { 0, 2, 4, 6 }, 0.6, Mix::hadamard, ratio });
            b.blocks.push_back ({ { 1, 3, 5, 7 }, 1.0, Mix::hadamard, 1.0 });
            b.theta = { theta };
            b.in = { 0 };
            b.out = { 0 };

            return levelled (decayCalibrated (fixed (name, build (name, b))));
        };

        c.push_back (volumes ("C2a coupled volumes", 0.45, 0.12));
        c.push_back (volumes ("C2b coupled, stronger", 0.30, 0.20));

        // ADDENDUM 3, appended after C1-C2 and Parts B and D were read: the
        // coupled volumes again, with the exchange timed from Decay.
        auto timedVolumes = [] (const std::string& name, double scale, double ratio, double k)
        {
            const Maker make = [name, scale, ratio, k] (const User& u)
            {
                auto base = Structure {}.delaysMs;
                std::sort (base.begin(), base.end());

                double mean = 0.0;

                for (int i : { 0, 2, 4, 6 })
                    mean += base[static_cast<size_t> (i)];

                mean = mean / 4.0 * scale * mapping::sizeScaleFor (u.size) * 0.001;

                Blocks b;
                b.blocks.push_back ({ { 0, 2, 4, 6 }, scale, Mix::hadamard, ratio });
                b.blocks.push_back ({ { 1, 3, 5, 7 }, 1.0,   Mix::hadamard, 1.0 });
                b.theta = { std::asin (std::sqrt (std::min (0.5, mean / (k * mapping::rt60For (u.decay))))) };
                b.in = { 0 };
                b.out = { 0 };

                return build (name, b);
            };

            return levelled (decayCalibrated ({ name, make }));
        };

        c.push_back (timedVolumes ("C3a timed volumes",    0.6, 0.45, 0.12));
        c.push_back (timedVolumes ("C3b timed, lossier",   0.6, 0.30, 0.12));
        c.push_back (timedVolumes ("C3c timed, full size", 1.0, 0.45, 0.12));

        return c;
    }
}

Outcome runChamber (const Outcome& room, const Outcome& plate)
{
    // =======================================================================
    beginSuite ("PHASE 12.5.6 - Chamber: seven hypotheses, then the adversarial gate");
    // =======================================================================

    info ("  Not a size between Room and Hall. C1: reflections inside the");
    info ("  recursion. C2: two coupled volumes with different decay, the double");
    info ("  slope, its RT60 held to Hall's by an explicit calibration.");
    info ("");

    auto outcome = workFamily ("Chamber", chamberCandidates(), 0.10);

    if (outcome.found)
    {
        info ("");
        info ("  ADVERSARIAL GATE. The best Chamber as the target of the best Room's");
        info ("  and the recommended Plate's own seven-control searches, three seeds");
        info ("  per point. (Hall's search is G2 above.) A run <= 1.5 is a collapse.");
        info ("");

        bool collapsed = false;

        for (const Outcome* other : { &room, &plate })
        {
            if (other->best.make == nullptr)
            {
                info ("    (no candidate available from the other family)");
                continue;
            }

            Candidate impostor = other->best;
            impostor.name = "by " + other->best.name;

            std::vector<Candidate> target { outcome.best };
            target[0].name = outcome.best.name + " <- " + other->best.name;

            const auto g = gate (target, impostor, 3);
            printGate (target, g);

            if (g[0].minimum <= kReproduced)
                collapsed = true;
        }

        if (collapsed)
        {
            outcome.found = false;
            outcome.status = "COLLAPSES - " + outcome.best.name + " is reproduced by another family's controls";
        }

        check (! collapsed, "FINDING: the best Chamber is not reproduced by Room's or Plate's controls");
    }

    info ("");
    info ("  CHAMBER: " + outcome.status);

    check (outcome.found, "FINDING: a Chamber hypothesis passes G1, G2, G3 and the adversarial gate");

    return outcome;
}

// ===========================================================================
//  PART D - Ambient
// ===========================================================================
namespace
{
    /** Staged migration. The per-pass transfer of each stage is set from a
        time: sin^2(theta) = (mean loop time of the stage) / tau, the total
        migration time k x Decay split evenly over the joins. */
    [[nodiscard]] Candidate ambientChain (const std::string& name, std::vector<std::vector<int>> stages, double k)
    {
        const Maker make = [name, stages, k] (const User& u)
        {
            auto base = Structure {}.delaysMs;
            std::sort (base.begin(), base.end());

            const double scale = mapping::sizeScaleFor (u.size);
            const double total = k * mapping::rt60For (u.decay);
            const double tau = total / static_cast<double> (stages.size() - 1);

            Blocks b;

            for (const auto& lines : stages)
                b.blocks.push_back ({ lines, 1.0, lines.size() == 2 ? Mix::rotation : Mix::hadamard, 1.0 });

            for (size_t s = 0; s + 1 < stages.size(); ++s)
            {
                double mean = 0.0;

                for (int line : stages[s])
                    mean += base[static_cast<size_t> (line)];

                mean = mean / static_cast<double> (stages[s].size()) * scale * 0.001;

                b.theta.push_back (std::asin (std::sqrt (std::min (0.5, mean / tau))));
            }

            b.in = { 0 };
            b.out = { static_cast<int> (stages.size()) - 1 };

            return build (name, b);
        };

        return levelled (decayCalibrated ({ name, make }));
    }
}

Outcome runAmbient()
{
    // =======================================================================
    beginSuite ("PHASE 12.5.7 - Ambient: staged migration timed from Decay");
    // =======================================================================

    info ("  Ambient A-E are closed. New here: several stages, and a transfer rate");
    info ("  set from a time (k x Decay) so the swell does not shrink with Size.");
    info ("  Each candidate's decayScale is fitted per setting to Hall's RT60.");
    info ("  Decay limit 5 %.");
    info ("");

    std::vector<Candidate> candidates;

    candidates.push_back (ambientChain ("A1a four stages k 0.25", { { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } }, 0.25));
    candidates.push_back (ambientChain ("A1b four stages k 0.40", { { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } }, 0.40));
    candidates.push_back (ambientChain ("A3 three stages k 0.30", { { 0, 4 }, { 1, 5 }, { 2, 3, 6, 7 } }, 0.30));

    std::vector<Candidate> survivors;

    const auto outcome = workFamily ("Ambient", candidates, 0.05, &survivors);

    if (! survivors.empty())
    {
        info ("");
        info ("  THE DECAY CALIBRATION that was fitted (decayScale; what a production law would");
        info ("  have to reproduce), Size 25 / 60 / 85 by Decay 0.8 / 2.5 / 5.0 s:");

        for (const auto& c : survivors)
        {
            std::string row = "    " + pad (c.name, 30);

            for (double size : { 0.25, 0.60, 0.85 })
                for (double decay : { 0.8, 2.5, 5.0 })
                {
                    User u;
                    u.size = size;
                    u.decay = decay;
                    row += pad (fx (c.make (u).decayScale, 3), 7);
                }

            info (row);
        }
    }

    info ("");
    info ("  AMBIENT: " + outcome.status);

    check (outcome.found, "FINDING: an Ambient hypothesis passes G1, G2 and G3");

    return outcome;
}

} // namespace stage5

} // namespace avtest
