#pragma once

#include <algorithm>
#include <cmath>

#include "TimeVaryingAllpass.h"

/**
    Tuning for the tank's allpass phase modulation.
*/
namespace av::dsp::allpassMod
{

/** One section per line, or two in cascade. */
inline constexpr int maxSectionsPerLine = 2;

// ===========================================================================
//  Section lengths
// ===========================================================================
/*
    Short compared with the FDN's own delays, which run 26 to 83 ms. A section
    long enough to spread a transient would be an input diffuser, not a phase
    modulator, so the base lengths sit below a millisecond and the coefficient
    stretches them: at g = 0.5 the DC group delay is three times the base, at
    g = 0.7 it is nearly six times.

    The values differ per line so the eight sections do not share a resonance,
    and are chosen with the same separation criteria as everything else.
*/

inline constexpr int maxLines = 8;

/** Base loop delay per line, in milliseconds. */
inline constexpr double sectionDelaysMs[maxLines]
{
    0.61, 0.83, 1.07, 1.29, 0.72, 0.97, 1.19, 1.43
};

/** Second cascade section, when two per line are used. */
inline constexpr double secondSectionDelaysMs[maxLines]
{
    0.89, 1.13, 1.37, 0.67, 1.51, 0.79, 1.31, 1.01
};

/** Longest section the storage is reserved for. */
inline constexpr double maxSectionDelayMs = 4.0;

// ===========================================================================
//  Coefficient trajectory
// ===========================================================================
/*
    The coefficient swings around a centre. Both ends matter: the sensitivity
    d(tau)/d(g) = 2M/(1-g)^2 grows quickly with g, so the same swing produces a
    much larger delay excursion near the top of the range than near the bottom.
    The centre is kept modest so the modulation is roughly symmetric in its
    effect, and the maximum swing is bounded so g can never approach the value
    at which the section starts ringing rather than delaying.
*/

inline constexpr double coefficientCentre = 0.45;
inline constexpr double maxCoefficientSwing = 0.30;

// ===========================================================================
//  Pitch budget
// ===========================================================================
/*
    Moving an allpass coefficient moves its group delay, and the derivative of
    a delay is a pitch shift. Unlike matrix modulation, which produces amplitude
    modulation and essentially no detune, this candidate produces real pitch
    movement and has to be budgeted for it.

        subtle      1-3 cent peak
        character   3-7 cent peak
        extreme     7-15 cent peak

    The default targets about 2 cents and must not exceed 3.

    PHASE 2's peakPitchDeviationCents() is NOT reused here. It was derived for a
    delay line whose length is modulated directly, where the delay excursion is
    the modulation depth in samples. Here the modulated quantity is a
    coefficient and the delay excursion is a nonlinear function of it, so the
    chain rule has an extra factor the PHASE 2 formula knows nothing about. The
    estimator below is derived for this structure and checked against
    measurement in the tests.
*/

inline constexpr double subtlePitchCents    = 3.0;
inline constexpr double characterPitchCents = 7.0;
inline constexpr double extremePitchCents   = 15.0;

/**
    Peak pitch deviation, in cents, for a sinusoidal coefficient trajectory.

    The group delay at DC is tau(g) = M (1+g)/(1-g), so

        dtau/dn = (dtau/dg)(dg/dn) = [2M/(1-g)^2] * A * 2*pi*f/fs

    evaluated where the sensitivity is largest, which is at the top of the
    swing. The instantaneous pitch ratio of a moving delay is 1 - dtau/dn.

    This is deliberately an upper bound, and a loose one. Across frequency the
    group delay is

        tau(omega) = M (1 - g^2) / (1 + g^2 - 2 g cos(M omega))

    and both tau and dtau/dg peak at DC: 2M/(1-g)^2 there against -4Mg/(1+g^2)^2
    at M omega = pi/2. Measurement puts the real excursion between a third and a
    tenth of what this returns. Over-reporting pitch is the safe direction for a
    budget check, so the bound is left conservative rather than tuned to fit.

    Note when measuring against it: at M omega = 0 (mod pi) the section is
    exactly transparent to its own coefficient, because H = (1-g)/(1-g) = 1 at
    DC and -(1+g)/(1+g) = -1 at Nyquist/M. Every frequency f = k fs/(2M) is a
    blind spot of the structure, and a probe tone placed there measures zero
    pitch deviation at any depth. Probe at f = fs/(4M) instead, where the
    sensitivity is greatest. See "Allpass / pitch deviation estimator" in the
    test suite, which measures the blind spots explicitly so they stay recorded.
*/
[[nodiscard]] inline double peakPitchCentsForCoefficientSwing (double baseDelaySamples,
                                                               double centre,
                                                               double swing,
                                                               double rateHz,
                                                               double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || rateHz <= 0.0 || swing <= 0.0 || baseDelaySamples <= 0.0)
        return 0.0;

    const double worstG = std::min (0.95, centre + swing);
    const double denominator = std::max (1.0e-6, 1.0 - worstG);

    const double sensitivity = 2.0 * baseDelaySamples / (denominator * denominator);
    const double peakSlope   = sensitivity * swing * 2.0 * 3.14159265358979323846 * rateHz / sampleRate;

    return 1200.0 * std::log2 (1.0 + std::abs (peakSlope));
}

// ===========================================================================
//  Rate and depth
// ===========================================================================

inline constexpr double minRateHz = 0.02;
inline constexpr double maxRateHz = 2.00;

/**
    Base rate of the slowest lane. Set by measurement.

    Chosen from the rate sweep on spectral concentration, which is what turns
    movement into chorus: 69.9 dB static, 70.3 dB at 0.20 Hz, 57.7 dB at 0.40 Hz,
    22.2 dB at 0.80 Hz. 0.20 Hz leaves concentration untouched while already
    meeting both modal targets.
*/
inline constexpr double defaultRateHz = 0.20;

/**
    Depth, 0..1, scaling the coefficient swing. Set by measurement.

    The modal targets (persistence <= 0.70, periodicity <= 30) are already met at
    depth 0.10, but its modal movement of 0.062 is barely above the 0.058 that
    PHASE 5A matrix modulation reaches on its own. 0.35 is the lowest point in
    the sweep that puts movement clearly above matrix modulation (0.106, 1.8x)
    without touching spectral concentration, at 0.12 cents of measured pitch.
*/
inline constexpr double defaultDepth = 0.35;

/** Shape of the Depth control. */
inline constexpr double depthExponent = 1.0;

[[nodiscard]] inline double swingForDepth (double depth) noexcept
{
    const double d = std::clamp (depth, 0.0, 1.0);

    if (d <= 0.0)
        return 0.0;

    return maxCoefficientSwing * std::pow (d, depthExponent);
}

// ===========================================================================
//  Lanes
// ===========================================================================
/*
    Each line gets its own lane. The same reasoning as the matrix lanes: not
    phase-shifted copies of one rate, not independent random rates, but fixed
    factors with no simple ratio between them. Reusing the matrix factors would
    lock the two modulations together, so this set is searched separately.
*/

inline constexpr double laneRateFactors[maxLines]
{
    1.000, 1.213, 1.457, 1.681, 1.109, 1.379, 1.531, 1.847
};

inline constexpr double lanePhaseOffsets[maxLines]
{
    0.000, 0.427, 0.183, 0.739, 0.291, 0.651, 0.093, 0.573
};

inline constexpr unsigned int laneSeeds[maxLines]
{
    0xA13F5u, 0xB27E4u, 0xC38D3u, 0xD49C2u,
    0xE5AB1u, 0xF6BA0u, 0x07C9Fu, 0x18D8Eu
};

/** Per-line depth variation, so the lines do not all swing equally. */
inline constexpr double laneDepthScales[maxLines]
{
    1.00, 0.87, 0.94, 0.79, 1.00, 0.91, 0.83, 0.97
};

// ===========================================================================
//  Bank configuration
// ===========================================================================

/**
    How many of the eight lines carry a section by default.

    Measured: 2 lines leaves the tank essentially static (persistence 0.7694,
    periodicity 44.1 against a static 0.7944 / 43.2), 4 lines fixes periodicity
    (22.3) but not persistence (0.7528), and only 8 moves both (0.6556 / 20.30).

    This is a voicing control, not a CPU control. Fewer active lines costs almost
    nothing less, because advance() ticks all eight LFO lanes every sample
    regardless — a lane that stopped and restarted would jump in phase.
*/
inline constexpr int defaultActiveLines = 8;

/**
    Sections per active line by default.

    A second cascaded section buys periodicity (15.39 against 20.30) but costs
    persistence (0.6833 against 0.6556), triples measured pitch (0.40 ct against
    0.12 ct) and lengthens each loop enough to push RT60 to 2.0846 s against a
    2.0 s target — 4.2 %, outside the 3 % band a single section holds. Kept at
    one; the second section stays available as a research setting.
*/
inline constexpr int defaultSectionsPerLine = 1;

/** Default realisation. */
inline constexpr TimeVaryingAllpass::Mode defaultMode =
    TimeVaryingAllpass::Mode::energyPreserving;

} // namespace av::dsp::allpassMod
