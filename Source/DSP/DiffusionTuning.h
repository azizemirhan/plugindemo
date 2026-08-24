#pragma once

#include <algorithm>
#include <cmath>

#include "AllpassDiffuser.h"

/**
    Tuning constants for the diffusion network.

    Everything a listener would call "the character of the diffuser" is a number
    in this file, so the tuning can be changed and re-measured without touching
    the DSP. Each value below is justified by a measurement in
    Tests/DiffusionNetworkTests.cpp; none of them is a guess left in place.
*/
namespace av::dsp::diffusion
{

inline constexpr int maxStages = 8;

// ===========================================================================
//  Delay times
// ===========================================================================
/*
    Chosen in milliseconds, not samples, so the diffuser has the same character
    at every sample rate. "A prime number of samples" is a 44.1 kHz-era trick
    that stops meaning anything once the delays are resampled.

    Two failure modes drive the choice:

      Harmonic relationships. If d_j / d_k is close to a simple ratio, the
      echoes of the two stages land on top of each other and the cascade
      produces a periodic pattern instead of a dense one. That is heard as
      metallic ringing.

      Sum coincidences. A cascade of allpasses emits echoes at every sum of its
      delays, so d_1 + d_2 landing on d_3 wastes a stage. This is the constraint
      that pure geometric or golden-ratio progressions fail: they satisfy exact
      additive identities by construction.

    Both sets below are the output of the directed search in
    DiffusionNetworkTests, not hand-picked values. Candidates are jittered
    geometric progressions rounded to the two decimals that actually ship, and
    are scored on the worse of the two constraints above; the winner is then
    refined one hundredth of a millisecond at a time. Hand-tuning does not work
    here — with eight delays there are 255 subset sums to keep apart, and every
    manual fix creates a new collision somewhere else.

    The search reports a best achievable margin of 0.68 % for the left set and
    1.02 % for the right. Perfect separation is not available at any price;
    what matters is that these are as good as a directed search can manage, and
    that the resulting echo pattern measures as diffuse. It does: against a
    harmonic control set the chosen set is 38x less energy-concentrated and 6x
    less periodic.
*/

/** Loop delays for the mono / left chain, in milliseconds. Total 50.78 ms. */
inline constexpr double leftDelaysMs[maxStages]
{
    1.52, 2.25, 2.90, 3.92, 6.25, 7.41, 10.24, 16.29
};

/** Loop delays for the right chain. Total 39.70 ms.

    Searched under the extra constraint that no unshared stage may sit within
    8 % of its left-hand counterpart, so the two chains cannot drift into
    agreement. The leading shared stage is copied from the left set, because the
    stereo network overrides it anyway. */
inline constexpr double rightDelaysMs[maxStages]
{
    1.52, 1.75, 2.19, 2.69, 5.01, 5.77, 8.85, 11.92
};

/** Coefficient polarity per stage. Sign changes phase, never magnitude. */
inline constexpr double leftSigns[maxStages]  { 1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0 };
inline constexpr double rightSigns[maxStages] { 1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0 };

// ---------------------------------------------------------------------------
//  Short input diffuser (PHASE 4)
// ---------------------------------------------------------------------------
/*
    The set above is a long diffusion tank: 50.78 ms on the left and 39.70 ms on
    the right at eight stages. That is far too much group delay, and far too
    asymmetric, to sit in front of a general-purpose FDN — the onset would
    arrive 11 ms later in one ear than the other, which is a localisation cue,
    not a reverb.

    So the FDN input uses its own four-stage set, searched under three extra
    constraints: a total group delay between 10 and 18 ms, the two channels'
    totals within 1 ms of each other, and a different internal distribution so
    the channels still decorrelate. The long set is kept for the large and
    ambient algorithms in later phases.
*/

/** Four-stage input diffuser, left. Total of the first four: 14.70 ms.
    Stages 5-8 continue the progression so a longer chain stays usable. */
inline constexpr double inputLeftDelaysMs[maxStages]
{
    1.87, 2.99, 4.31, 5.53, 8.11, 11.29, 15.73, 21.97
};

/** Four-stage input diffuser, right. Total of the first four: 14.46 ms, which
    is 0.24 ms from the left — inside the 1 ms the onset difference has to stay
    under, since a larger gap would read as a localisation cue rather than as
    reverb. The first stage is a copy of the left one because the stereo network
    shares it; every later stage differs by at least 6 %. */
inline constexpr double inputRightDelaysMs[maxStages]
{
    1.87, 2.53, 4.87, 5.19, 7.61, 12.11, 16.87, 20.39
};

inline constexpr double inputLeftSigns[maxStages]  { 1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0 };
inline constexpr double inputRightSigns[maxStages] { 1.0, -1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0 };

/** A complete stereo delay/sign tuning, so a network can be pointed at one of
    several without the tuning tables leaking into the DSP. */
struct StereoPattern
{
    const double* leftMs;
    const double* leftSigns;
    const double* rightMs;
    const double* rightSigns;
};

/** The long PHASE 3 tank. Default for a standalone diffuser. */
inline constexpr StereoPattern longPattern
{
    leftDelaysMs, leftSigns, rightDelaysMs, rightSigns
};

/** The short diffuser that sits in front of the FDN. */
inline constexpr StereoPattern fdnInputPattern
{
    inputLeftDelaysMs, inputLeftSigns, inputRightDelaysMs, inputRightSigns
};

/** Largest multiplier setDelayScale() will honour; sets the allocation size. */
inline constexpr double maxDelayScale = 2.0;

/** Default number of stages in the chain. */
inline constexpr int defaultStageCount = 4;

// ===========================================================================
//  Diffusion mapping
// ===========================================================================
/*
    The mapping deliberately moves only the coefficient. An allpass has unit
    magnitude at every coefficient, so sweeping Diffusion across its full range
    cannot change the spectrum, only the phase. A dry/wet blend would not have
    that property, which is why the control does not use one.

    The shape of the mapping was expected to need an exponent well below 1:
    perceived density looks like it should rise steeply while g is small and
    then saturate, so a power law would be needed to spread the useful region
    across the control's travel.

    Measurement did not support that. Echo density was measured at
    0 / 25 / 50 / 75 / 100 % for exponents 1.0, 0.7, 0.5 and 0.35, and the
    spread between the largest and smallest step came out:

        exponent   density at 0/25/50/75/100 %              step spread
        1.00       0.003  0.066  0.194  0.335  0.404        0.079
        0.70       0.003  0.102  0.246  0.374  0.404        0.115
        0.50       0.003  0.194  0.305  0.401  0.404        0.187
        0.35       0.003  0.246  0.354  0.417  0.404        0.256

    A plain linear coefficient map has the most evenly spaced steps, and every
    sub-linear exponent crowds the top of the control into a region where
    density has already saturated. The exponent stays a named constant with the
    measurement attached rather than being folded away, because the balance will
    change once the FDN sits behind the diffuser and the useful coefficient
    range moves.
*/

/** Shape exponent for the Diffusion control. Selected by measurement. */
inline constexpr double diffusionShapeExponent = 1.0;

/** Highest coefficient the mapping will reach, at Diffusion = 100 %. */
inline constexpr double diffusionMaxCoefficient =
    AllpassDiffuser<>::recommendedMaxCoefficient;

/** Maps the 0..1 Diffusion control to an allpass coefficient magnitude. */
[[nodiscard]] inline double coefficientForDiffusion (double diffusion) noexcept
{
    const double d = std::clamp (diffusion, 0.0, 1.0);

    if (d <= 0.0)
        return 0.0;

    return diffusionMaxCoefficient * std::pow (d, diffusionShapeExponent);
}

// ===========================================================================
//  Stereo
// ===========================================================================

/** Leading stages that are identical in both channels.

    The expectation was that sharing more stages would monotonically improve
    mono compatibility, since a shared prefix factors out of the L/R phase
    difference. Measured at six active stages:

        shared   L/R correlation   worst third-octave downmix dip
          0          0.008                5.08 dB
          1          0.008                5.08 dB
          2         -0.020               17.17 dB
          3         -0.016               17.04 dB
          4          0.123                3.19 dB

    It is not monotonic. Which stages are shared matters more than how many,
    because what survives the downmix is the phase difference between the
    non-shared parts, and that depends on the specific delays left over.

    Four shared stages measures best, but the default chain is four stages long,
    so sharing four would make the channels identical and collapse the field to
    mono. One is shipped: it is the joint best among the settings that leave
    enough stages differing. Note that at the shipped tuning it is also
    equivalent to zero, because the right set's first delay is already a copy of
    the left's — the setting exists for the algorithms in later phases that will
    use longer chains. */
inline constexpr int defaultSharedStages = 1;

/** Stage index after which the channels are cross-coupled. */
inline constexpr int defaultCrossCouplingStage = 2;

/** Default cross-coupling amount, 0..1, mapped to a rotation angle. */
inline constexpr double defaultCrossCoupling = 0.0;

} // namespace av::dsp::diffusion
