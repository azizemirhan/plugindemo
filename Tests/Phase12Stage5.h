#pragma once

#include "Phase12Structural.h"

#include <functional>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 5. What the four parts share.

    RESEARCH ONLY. Gates and hypotheses are in
    Docs/Phase12-Stage5-Preregistration.md and were committed first.

    Defined in Phase12Stage5.cpp; the parts are in Phase12Stage5Parts.cpp.
*/
namespace avtest::stage5
{
    using stage3::Structure;
    using stage3::User;

    inline constexpr int kLateralFamily = 11;
    inline constexpr int kSeeds = 5;

    /** A structure that may depend on the knobs (Ambient's does). */
    using Maker = std::function<Structure (const User&)>;

    struct Candidate
    {
        std::string name;
        Maker make;
    };

    [[nodiscard]] Candidate fixed (const std::string& name, Structure s);

    // --- blocks ---------------------------------------------------------------
    enum class Mix { hadamard, householder, identity, ring, rotation };

    struct Block
    {
        std::vector<int> lines;     // indices into Hall's delays, sorted ascending
        double delayScale = 1.0;
        Mix    mix = Mix::hadamard;
        double decayRatio = 1.0;
    };

    struct Blocks
    {
        std::vector<Block>  blocks;
        std::vector<double> theta;      // between block k and k + 1
        std::vector<int>    in, out;    // block indices
    };

    /** The structure for a partition. Not level-calibrated. */
    [[nodiscard]] Structure build (const std::string& name, const Blocks& b);

    /** Largest |M Mt - I| entry of a structure's matrix, loss ignored. */
    [[nodiscard]] double orthogonalityError (const Structure& s);

    // --- scoring --------------------------------------------------------------
    /** Worst family with the lateral family left out. */
    [[nodiscard]] double centredWorst (const stage3::Score& s, int* family = nullptr);

    [[nodiscard]] stage3::Score sameKnobs (const stage3::Features& a, const stage3::Features& b);

    struct Search
    {
        std::string label;
        int point = 0;
        stage3::Features target;
        Structure impostor;
        User start;
        unsigned int seed = 0;

        double residual = 0.0;
        int family = 0;
        std::string knobs;
    };

    [[nodiscard]] unsigned int seedFor (int k);

    /** Runs every search in parallel, centred view. */
    void runSearches (std::vector<Search>& searches);

    struct Gate
    {
        double residual[structural::kPointCount][kSeeds] {};
        int    family[structural::kPointCount] {};          // of the lowest run at the point
        std::string knobs[structural::kPointCount];        // Hall's knobs in that run
        double median[structural::kPointCount] {};
        double minimum = 0.0;
        int    seeds = 0;
        std::string verdict;        // SURVIVES / NOT REFUTED / REJECTED
    };

    /** G1 (seeds = 1) or G2 (seeds = kSeeds) against one impostor. */
    [[nodiscard]] std::vector<Gate> gate (const std::vector<Candidate>& targets,
                                          const Candidate& impostor, int seeds);

    void printGate (const std::vector<Candidate>& targets, const std::vector<Gate>& gates);

    // --- G3 -------------------------------------------------------------------
    struct Semantics
    {
        bool sizeMonotone = false;
        double decayLow = 0.0, decayHigh = 0.0;
        bool stereoDefect = false;
        double toneDb = 0.0;
        double cpu = 0.0;
        bool pass = false;
    };

    /** Prints the G3 tables for the list and returns each one's result. */
    [[nodiscard]] std::vector<Semantics> semantics (const std::vector<Candidate>& list,
                                                    double decayLimit, double cpuLimit);

    /** Level-matches a maker's output to Hall at the defaults. */
    [[nodiscard]] Candidate levelled (const Candidate& c);

    /** Fits decayScale per knob setting so the measured RT60 equals Hall's. */
    [[nodiscard]] Candidate decayCalibrated (const Candidate& c);

    [[nodiscard]] double wetPathNs (const Structure& s);

    /** Wet-path cost against Hall, the two measured alternately. */
    [[nodiscard]] double wetPathRatio (const Structure& s);

    // --- the parts --------------------------------------------------------------
    struct Outcome
    {
        bool found = false;
        Candidate best;
        std::string status;
    };

    [[nodiscard]] Outcome runPlate();
    [[nodiscard]] Outcome runRoom();
    [[nodiscard]] Outcome runChamber (const Outcome& room, const Outcome& plate);
    [[nodiscard]] Outcome runAmbient();

    [[nodiscard]] Outcome standingRoom();
    [[nodiscard]] Outcome standingPlate();

    // --- for Stage 6 ----------------------------------------------------------
    /** A1b as Part D left it. */
    [[nodiscard]] Outcome standingAmbient();

    /** Stage 5's quality-risk and centred-source tables, for any list. */
    void qualityRisks (const std::vector<Candidate>& list);
    void centredSources (const std::vector<Candidate>& list);
}
