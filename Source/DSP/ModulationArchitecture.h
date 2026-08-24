#pragma once

#include "AllpassModulationTuning.h"
#include "ModulationTuning.h"
#include "TimeVaryingAllpass.h"

namespace av::dsp::architecture
{

/**
    The tank modulation architectures PHASE 5C compares, in one place.

    PHASE 5A built a time-varying orthogonal matrix and PHASE 5B built an
    energy-preserving allpass bank. Both are still present and both still work;
    what is decided here is which of them the reverb actually uses by default,
    and on what evidence.

    The candidates are named rather than described by a pile of loose arguments
    so that every measurement, every CPU figure and the preview build all refer
    to the same thing. Changing a candidate's definition changes it everywhere,
    including in the tests that justified the choice.

    Nothing in this header is a user parameter. Depth and Rate remain the user's
    controls; a candidate fixes the *architecture* those controls drive.
*/
enum class Candidate
{
    staticFdn,        /**< A — no modulation at all. The PHASE 4 tank. */
    matrixOnly,       /**< B — PHASE 5A as shipped: AB schedule, 7 deg, 0.40 Hz. */
    allpassOnly,      /**< C — PHASE 5B leader: 8 lines, 1 section, 0.35, 0.20 Hz. */
    lightHybrid,      /**< D — allpass as C plus 3 deg of matrix modulation. */
    mediumHybrid,     /**< E — allpass as C plus 5 deg of matrix modulation. */
    existingHybrid,   /**< F — allpass as C plus the full 7 deg of PHASE 5A. */
    thiranOnly        /**< G — the best measured Thiran-only setting. */
};

inline constexpr int candidateCount = 7;

inline constexpr Candidate allCandidates[candidateCount]
{
    Candidate::staticFdn,   Candidate::matrixOnly,     Candidate::allpassOnly,
    Candidate::lightHybrid, Candidate::mediumHybrid,   Candidate::existingHybrid,
    Candidate::thiranOnly
};

/**
    Matrix rate used by the hybrid candidates.

    NOT the PHASE 5A default of 0.40 Hz. The allpass lanes are built on a
    0.20 Hz base, and 0.40 Hz is exactly twice that, so a pair of lanes locks at
    2:1 and beats audibly however irregular the other fourteen are.

    Chosen by the deterministic scan in HybridAnalysisTests.cpp, which maximises
    the closest approach to a simple ratio over every allpass-lane against
    matrix-lane pair, across 0.240 to 0.480 Hz in 0.001 Hz steps. The scan is
    cross-engine only: including each engine's lanes against their own would add
    a fixed floor from the allpass bank that no choice of matrix rate can move,
    and the result would look flat for a reason that has nothing to do with the
    choice being made.

    Measured: 0.288 Hz keeps every cross-engine pair 0.423 % from the nearest
    simple fraction. The first value tried here, 0.317 Hz, managed 0.062 % —
    nearly seven times worse, and picked by eye rather than by search.
*/
inline constexpr double hybridMatrixRateHz = 0.288;

/** Matrix modulation depth for a rotation angle, given the 20 deg full scale. */
[[nodiscard]] inline constexpr double matrixDepthForDegrees (double degrees) noexcept
{
    return degrees / modulation::maxAngleDegrees;
}

/** Everything a candidate sets on a tank. */
struct Settings
{
    // --- Allpass phase modulation (PHASE 5B) ---
    double allpassDepth   = 0.0;
    double allpassRateHz  = allpassMod::defaultRateHz;
    TimeVaryingAllpass::Mode allpassMode = TimeVaryingAllpass::Mode::energyPreserving;
    int    allpassLines   = allpassMod::defaultActiveLines;
    int    allpassSections = allpassMod::defaultSectionsPerLine;

    // --- Matrix modulation (PHASE 5A) ---
    double matrixDepth  = 0.0;
    double matrixRateHz = modulation::defaultRateHz;
    double matrixDegrees = 0.0;   /**< Reported, not applied; depth carries it. */
};

[[nodiscard]] inline Settings settingsFor (Candidate c) noexcept
{
    Settings s;

    switch (c)
    {
        case Candidate::staticFdn:
            break;

        case Candidate::matrixOnly:
            s.matrixDegrees = 7.0;
            s.matrixDepth   = matrixDepthForDegrees (7.0);
            s.matrixRateHz  = modulation::defaultRateHz;   // 0.40 Hz, as shipped
            break;

        case Candidate::allpassOnly:
            s.allpassDepth = allpassMod::defaultDepth;
            break;

        case Candidate::lightHybrid:
            s.allpassDepth  = allpassMod::defaultDepth;
            s.matrixDegrees = 3.0;
            s.matrixDepth   = matrixDepthForDegrees (3.0);
            s.matrixRateHz  = hybridMatrixRateHz;
            break;

        case Candidate::mediumHybrid:
            s.allpassDepth  = allpassMod::defaultDepth;
            s.matrixDegrees = 5.0;
            s.matrixDepth   = matrixDepthForDegrees (5.0);
            s.matrixRateHz  = hybridMatrixRateHz;
            break;

        case Candidate::existingHybrid:
            s.allpassDepth  = allpassMod::defaultDepth;
            s.matrixDegrees = 7.0;
            s.matrixDepth   = matrixDepthForDegrees (7.0);
            s.matrixRateHz  = hybridMatrixRateHz;
            break;

        case Candidate::thiranOnly:
            // A first-order Thiran section moves a fraction of a sample rather
            // than a whole allpass loop, so it needs the full depth range to
            // reach the modal targets at all. PHASE 5B measured 0.35 as barely
            // adequate; the sweep in PHASE 5C picks the best it can do.
            s.allpassDepth = 1.0;
            s.allpassMode  = TimeVaryingAllpass::Mode::thiran;
            break;
    }

    return s;
}

[[nodiscard]] inline constexpr const char* describe (Candidate c) noexcept
{
    switch (c)
    {
        case Candidate::staticFdn:      return "A static FDN";
        case Candidate::matrixOnly:     return "B matrix-only 7deg";
        case Candidate::allpassOnly:    return "C allpass-only";
        case Candidate::lightHybrid:    return "D light hybrid 3deg";
        case Candidate::mediumHybrid:   return "E medium hybrid 5deg";
        case Candidate::existingHybrid: return "F hybrid 7deg";
        case Candidate::thiranOnly:     return "G thiran-only";
    }

    return "unknown";
}

/**
    The architecture the reverb uses.

    Set from the PHASE 5C multi-objective comparison. Allpass-only entered that
    comparison as the leader on the PHASE 5B evidence, and a hybrid could only
    displace it by showing a reproducible benefit that its extra amplitude
    modulation, mono cancellation and CPU did not cancel out. The suites in
    ModulationSelectionTests.cpp and HybridAnalysisTests.cpp are the record.
*/
inline constexpr Candidate defaultCandidate = Candidate::allpassOnly;

/**
    Applies a candidate to anything exposing the tank setters.

    Templated rather than virtual: this is called from prepare() and from the
    tests, never per sample, but the tank types do not share a base class and
    inventing one for this would be worse than a template.
*/
template <typename Tank>
inline void apply (Tank& tank, Candidate c, bool immediate = false)
{
    const auto s = settingsFor (c);

    tank.setAllpassMode (s.allpassMode);
    tank.setAllpassActiveLines (s.allpassLines);
    tank.setAllpassSectionsPerLine (s.allpassSections);
    tank.setAllpassRateHz (s.allpassRateHz);
    tank.setAllpassDepth (s.allpassDepth, immediate);

    tank.setModulationRateHz (s.matrixRateHz);
    tank.setModulationDepth (s.matrixDepth, immediate);
}

/**
    Scales a candidate by the user's Depth control.

    Depth 0 must reach a true bypass in every engine the candidate uses, and
    Depth 1 must reach the candidate's full setting, with everything in between
    monotone. Multiplying both engines by the same normalised control does all
    three, and keeps the ratio between the two engines fixed so that the
    character the candidate was chosen for does not change with Depth.
*/
template <typename Tank>
inline void applyWithDepth (Tank& tank, Candidate c, double depth, double rateScale = 1.0,
                            bool immediate = false)
{
    const auto s = settingsFor (c);
    const double d = depth < 0.0 ? 0.0 : (depth > 1.0 ? 1.0 : depth);

    tank.setAllpassMode (s.allpassMode);
    tank.setAllpassActiveLines (s.allpassLines);
    tank.setAllpassSectionsPerLine (s.allpassSections);
    tank.setAllpassRateHz (s.allpassRateHz * rateScale);
    tank.setAllpassDepth (s.allpassDepth * d, immediate);

    tank.setModulationRateHz (s.matrixRateHz * rateScale);
    tank.setModulationDepth (s.matrixDepth * d, immediate);
}

} // namespace av::dsp::architecture
