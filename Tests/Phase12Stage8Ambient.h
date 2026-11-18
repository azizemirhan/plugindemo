#pragma once

#include "Phase12Stage6.h"

#include <array>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 8. Ambient: repairing A1b without losing it.

    RESEARCH ONLY. Source/ IS NOT CHANGED. Gates, diagnostics and candidate
    families are in Docs/Phase12-Stage8-Ambient-Repair-Preregistration.md
    and its addenda, committed first.

    A1b is the frozen structural reference: stage6::ambientRaw (AmbientSpec {}),
    never edited here. Every candidate is a separate structure.

    The suites run only when named exactly (see RunAll.cpp):

        AuroraVerbTests.exe runPhase12Stage8AmbientDiagnosis
        AuroraVerbTests.exe runPhase12Stage8AmbientRepairs
*/
namespace avtest::stage8
{
    using stage3::Structure;
    using stage3::User;
    using stage3::Stereo;
    using stage5::Candidate;
    using stage5::Maker;

    using Bands = std::array<double, 7>;

    /** Every delay multiplied by 1 + 0.04 u, u uniform in -1 .. 1 from the
        seed. Changes where the modes fall and nothing else. Seed 0: unchanged. */
    [[nodiscard]] Structure perturbed (Structure s, unsigned int seed);

    /** The three broadband sources of the Stage 7 package, and the piano phrase. */
    struct Source { std::string name; std::vector<double> m; bool gates = true; };
    [[nodiscard]] const std::vector<Source>& sources();

    /** Requirement 4. With the structure level-matched to Hall on its impulse
        response AT THIS SETTING, the gain in dB still needed to match Hall on
        each source. `lawDb` receives the level match itself. */
    [[nodiscard]] std::vector<double> sourceGains (const Structure& s, const User& u, double* lawDb = nullptr);

    /** Largest minus smallest over the gating sources. */
    [[nodiscard]] double spreadOf (const std::vector<double>& gains);
}
