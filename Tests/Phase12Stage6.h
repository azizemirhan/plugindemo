#pragma once

#include "Phase12Stage5.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 6. What the parts share.

    RESEARCH ONLY. Gates, candidates and predictions are in
    Docs/Phase12-Stage6-Preregistration.md and were committed first.

    Defined in Phase12Stage6.cpp (shared machinery, Room, Ambient, Room
    against Ambient) and Phase12Stage6Parts.cpp (Plate, Chamber, the
    cross-mode matrix, cost and switching).
*/
namespace avtest::stage6
{
    using stage3::Structure;
    using stage3::User;
    using stage3::Features;
    using stage3::Score;
    using stage3::Stereo;
    using stage5::Candidate;
    using stage5::Maker;
    using stage5::Gate;

    using structural::kPointCount;

    // --- the search, with an impostor that follows its own knobs ---------------
    /** structural::mimic, statement for statement, with the impostor rebuilt
        at every evaluation. Section 12.6.1 holds the copy to the original. */
    [[nodiscard]] structural::SearchResult mimicWith (const Features& target, const Maker& impostor,
                                                      const stage3::Floor& floor,
                                                      const structural::Controls& startAt,
                                                      unsigned int seed, int ignoreFamily);

    struct Probe
    {
        Features target;
        Maker impostor;
        User start;
        unsigned int seed = 0;

        double residual = 0.0;
        int family = 0;
        std::string knobs;
        Score best;
    };

    void runProbes (std::vector<Probe>& probes);

    /** G1 / G2 against a knob-dependent impostor, centred view. `best`
        receives, per target and point, the score of the lowest run. */
    [[nodiscard]] std::vector<Gate> gateWith (const std::vector<Candidate>& targets, const Maker& impostor,
                                              int seeds,
                                              std::vector<std::array<Score, kPointCount>>* best = nullptr);

    // --- a family: one structure, as a target and as an impostor ---------------
    struct Family
    {
        std::string name;
        Maker raw;              // no level, no decay calibration
        Candidate target;       // level-matched; decay fitted per setting
        Candidate impostor;     // level-matched; decay from a smooth law
        bool structural = false; // not REJECTED by Hall's search
        bool standing = false;   // passed every gate of its own part
        std::string status;
    };

    /** Decay calibration as a law: fitted on a 4 x 4 grid of Size and Decay,
        interpolated. What a product would ship. */
    [[nodiscard]] Maker lawful (const Maker& raw);

    [[nodiscard]] Family familyOf (const std::string& name, const Maker& raw, bool calibrateDecay);
    [[nodiscard]] Family hallFamily();

    // --- measurements -----------------------------------------------------------
    struct Safety
    {
        double corr = 0, fold = 0, balance = 0, mirror = 0, toneWorst = 0, toneMedian = 0;
    };

    [[nodiscard]] Safety safetyOf (const Structure& s, const User& u = User {});

    /** Stage 3's limits, against Hall's own row. */
    [[nodiscard]] bool safetyDefect (const Safety& s, const Safety& hall);

    /** Seven octave shares, 125 Hz - 8 kHz, of a window of the response. */
    [[nodiscard]] std::array<double, 7> octaveShares (const Structure& s, const User& u,
                                                      double fromSeconds = 0.0, double toSeconds = 1.0e9);

    /** Largest |dB| between two sets of shares; `signedDb` receives each band's. */
    [[nodiscard]] double octaveDeviation (const std::array<double, 7>& a, const std::array<double, 7>& reference,
                                          std::array<double, 7>* signedDb = nullptr);

    /** Worst modal prominence over the three operating points, dB. */
    [[nodiscard]] double modalWorst (const Candidate& c);

    // --- Room ---------------------------------------------------------------------
    struct RoomSpec
    {
        std::string name = "R4c";

        double localScale = 0.25, k = 0.12;
        double tauPower = 0.0;      // exchange time x (s / s0)^tauPower
        double localPower = 0.0;    // local scale x (s / s0)^localPower

        int ringing = 0;            // 0 none; 1-5: RR1 .. RR5
        int vectors = 0;            // 0 Stage 5's; 1 mid/side; 2 generic; 3 the four below

        std::array<double, 4> inL {}, inR {}, outL {}, outR {};
    };

    [[nodiscard]] Maker roomRaw (const RoomSpec& spec);

    // --- Ambient -----------------------------------------------------------------
    struct AmbientSpec
    {
        std::string name = "A1b";

        std::vector<std::vector<int>> stages { { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        double k = 0.40;

        double gammaLow = 1.0, gammaHigh = 1.0;     // AT1
        std::vector<double> hear;                   // AT2: a weight per stage; empty = the last only
        double direct = 0.0;                        // AT3: share of the input energy into the last stage
    };

    [[nodiscard]] Maker ambientRaw (const AmbientSpec& spec);

    // --- the parts ---------------------------------------------------------------
    /** What stands when a part is not run in this process: its definition as
        the last full run of that part left it. */
    [[nodiscard]] RoomSpec standingRoomSpec();
    [[nodiscard]] AmbientSpec standingAmbientSpec();
    [[nodiscard]] Structure standingPlateStructure();

    [[nodiscard]] Family runRoom();
    [[nodiscard]] Family runAmbient();
    [[nodiscard]] bool   runRoomAgainstAmbient (const Family& room, const Family& ambient);
    [[nodiscard]] Family runPlate();
    [[nodiscard]] Family runChamber();
    /** Returns the names of the families (other than Hall) that another family reproduced. */
    [[nodiscard]] std::vector<std::string> runMatrix (const std::vector<Family>& families);
    void runEngineering (const std::vector<Family>& families);

    // --- Chamber specs, shared with the fast suite -------------------------------
    [[nodiscard]] std::vector<std::pair<std::string, std::shared_ptr<const OpenSpec>>> chamberSpecs();
    [[nodiscard]] std::shared_ptr<const OpenSpec> openHallSpec();

    /** The multirate designs PM1 and PM2 for the 240-section reference. */
    [[nodiscard]] std::shared_ptr<const MultirateSpec> multirateDesign (int bands);
}
