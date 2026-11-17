#pragma once

#include "Phase12Stage3Metric.h"

#include <functional>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 3. What the control sections and the candidate sections
    share: the search space, the impostor search and the trial batch.

    RESEARCH ONLY. Defined in Phase12Structural.cpp.
*/
namespace avtest
{

namespace structural
{
    inline constexpr int kControls = 7;

    inline constexpr const char* kControlNames[kControls]
    {
        "Size", "Decay", "Early/Late", "Early Diff", "Attack", "Mod Depth", "Width"
    };

    struct Controls { double v[kControls] {}; };

    struct SearchResult
    {
        Controls controls;
        stage3::Score start, best;      // at the starting knobs; after the search
        int evaluations = 0;
    };

    struct Point { const char* label; double size, decay; };

    inline constexpr int kPointCount = 3;
    extern const Point kPoints[kPointCount];

    /** One impostor search: `impostor` is searched to imitate a target, which
        is either `structure` at `user` or a ready feature vector. */
    struct Trial
    {
        std::string label, point;

        stage3::Structure structure;
        stage3::User user;              // the target's knobs, and the search's start

        stage3::Features target;
        bool hasTarget = false;

        stage3::Structure impostor;

        /** A family left out of the search and the verdict, or -1. */
        int ignoreFamily = -1;

        SearchResult result;
    };

    [[nodiscard]] std::string fx (double v, int digits = 2);
    [[nodiscard]] std::string pad (const std::string& s, size_t width);
    [[nodiscard]] bool identical (const stage3::Stereo& a, const stage3::Stereo& b);

    void runParallel (size_t count, const std::function<void (size_t)>& job);

    [[nodiscard]] stage3::User toUser (const Controls& c);
    [[nodiscard]] Controls toControls (const stage3::User& u);
    [[nodiscard]] std::string describe (const Controls& c);

    [[nodiscard]] SearchResult mimic (const stage3::Features& target,
                                      const stage3::Structure& impostor,
                                      const stage3::Floor& floor,
                                      const Controls& startAt, unsigned int seed,
                                      int ignoreFamily = -1);

    [[nodiscard]] stage3::User userAt (const Point& p);
    [[nodiscard]] stage3::Structure hall();
    [[nodiscard]] stage3::Structure withDelays (const std::string& name,
                                                const std::array<double, 8>& ms);
    [[nodiscard]] stage3::Structure fromStage2 (const mode12::ModeCandidate& m);

    /** The floor measured by section 12.3.2. */
    [[nodiscard]] const stage3::Floor& floorOf();

    /** Measures the floor if 12.3.2 has not run in this process (a filtered
        run of a later stage). Same pairs, same result, nothing printed. */
    void ensureFloor();

    void runTrials (std::vector<Trial>& trials);
    void printTrials (const std::vector<Trial>& trials);
    void printFamilyHeader();
    void printFamilies (const std::string& label, const stage3::Score& s);
}

/** Sections 12.3.5 onward. Empty until the metric has been frozen. */
void runPhase12StructuralCandidates();

} // namespace avtest
