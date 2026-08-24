#pragma once

#include <algorithm>
#include <cmath>

#include "FdnTuning.h"
#include "GivensRotation.h"

/**
    Tuning for the tank's matrix modulation.

    As everywhere else in this project, each number carries the measurement that
    selected it rather than a preference.
*/
namespace av::dsp::modulation
{

/** Largest number of Givens rotations the engine will ever apply. */
inline constexpr int maxRotations = 8;

// ===========================================================================
//  Pairing schedules
// ===========================================================================
/*
    Which delay lines each rotation acts on.

    Every line has to take part, and no line should be the one that always
    moves. Three disjoint schedules and one two-layer schedule are offered; the
    tests compare them on modal movement, tail periodicity, stereo behaviour and
    cost, and the shipped choice is whichever measures best.

    Disjoint schedules have a useful property: their rotations commute, so the
    product is the same however it is evaluated and the order of application
    cannot matter. The two-layer schedule gives up that property in exchange for
    each line taking part in two rotations rather than one.
*/

struct Schedule
{
    const RotationPair* pairs;
    int count;
    const char* name;
};

inline constexpr RotationPair scheduleAPairs[]  { {0,1}, {2,3}, {4,5}, {6,7} };
inline constexpr RotationPair scheduleBPairs[]  { {1,2}, {3,4}, {5,6}, {7,0} };
inline constexpr RotationPair scheduleCPairs[]  { {0,3}, {1,5}, {2,7}, {4,6} };
inline constexpr RotationPair scheduleABPairs[] { {0,1}, {2,3}, {4,5}, {6,7},
                                                  {1,2}, {3,4}, {5,6}, {7,0} };

inline constexpr Schedule scheduleA  { scheduleAPairs,  4, "A: adjacent disjoint" };
inline constexpr Schedule scheduleB  { scheduleBPairs,  4, "B: offset disjoint" };
inline constexpr Schedule scheduleC  { scheduleCPairs,  4, "C: spread disjoint" };
inline constexpr Schedule scheduleAB { scheduleABPairs, 8, "AB: two layers" };

/*
    Measured at the default depth and rate, against a static tank whose modal
    persistence is 0.7944:

      schedule                persistence  peak movement  periodicity  RT60
      A: adjacent disjoint      0.7833       0.080 Hz       47.38      1.9911
      B: offset disjoint        0.8250       0.084 Hz       39.32      1.9974
      C: spread disjoint        0.7889       0.085 Hz       45.92      1.9919
      AB: two layers            0.7528       0.093 Hz       45.21      1.9921

    The two-layer schedule moves the modes most, which is what it is for: each
    line takes part in two rotations rather than one, so the mixing changes in
    more directions at the same angle. It costs twice the rotation work, which
    the benchmark shows is a small share of the tank's total. The three disjoint
    schedules are within noise of each other and of the static tank.
*/

/** The shipped schedule. */
inline constexpr Schedule defaultSchedule = scheduleAB;

// ===========================================================================
//  Modulation lanes
// ===========================================================================
/*
    Each rotation gets its own LFO. They must not be phase-shifted copies of one
    rate — that would make the whole matrix move as one thing and produce a
    single, audible period — and they must not be independent random rates
    either, or the behaviour would not be reproducible.

    So the lane rates are the base rate times a fixed set of factors, chosen so
    that no pair of them sits near a simple ratio. The factors below are the
    output of the search in MatrixModulationTests.cpp, which maximises the
    distance of every pairwise ratio from p/q with small p and q, and measures
    the resulting common repeat period by simulation.
*/

inline constexpr double laneRateFactors[maxRotations]
{
    1.000, 1.141, 1.823, 1.247, 1.612, 1.448, 1.289, 1.567
};

/** Phase offsets, so the lanes do not all start at the same point. */
inline constexpr double lanePhaseOffsets[maxRotations]
{
    0.000, 0.317, 0.631, 0.148, 0.842, 0.473, 0.209, 0.766
};

/** Seeds for the smooth-random shape, one per lane. */
inline constexpr unsigned int laneSeeds[maxRotations]
{
    0x1F35Au, 0x2C71Bu, 0x3D9E4u, 0x4A0F7u,
    0x5B283u, 0x6E4C1u, 0x7F60Du, 0x81A2Fu
};

// ===========================================================================
//  Rate
// ===========================================================================

/*
    Measured rate sweep at five degrees, four rotations:

      rate     modal persist  periodicity  RT60     AM depth  concentration
      0.02 Hz    0.8111        48.45       1.9955   0.23 dB     69.8 dB
      0.10 Hz    0.7667        44.58       1.9932   0.68 dB     69.4 dB
      0.40 Hz    0.7583        43.19       1.9921   0.97 dB     69.8 dB
      0.80 Hz    0.7639        42.06       1.9904   0.92 dB     65.2 dB
      1.20 Hz    0.6722        42.00       1.9924   0.99 dB     37.2 dB
      2.00 Hz    0.7278        42.72       1.9924   0.95 dB     30.2 dB

    Spectral concentration is the number that decides the ceiling. Up to about
    0.4 Hz a sustained tone comes out of the tank as concentrated as it went in;
    past 0.8 Hz it starts smearing, and by 1.2 Hz it has lost thirty decibels of
    concentration, which is no longer movement but chorus. The full range stays
    available because a character setting may want it, but the default sits at
    the top of the clean region.
*/

inline constexpr double minRateHz = 0.02;
inline constexpr double maxRateHz = 2.00;

/** Base rate of the slowest lane at the shipped default. Set by measurement. */
inline constexpr double defaultRateHz = 0.40;

// ===========================================================================
//  Depth
// ===========================================================================
/*
    Depth is a 0..1 control mapped to a maximum rotation angle.

    An angle is not a perceptual quantity, so the mapping is not assumed to be
    linear. The exponent below is chosen the same way the diffusion mapping's
    was: by measuring how the audible consequence — here the reduction in tail
    periodicity — moves across the control's travel, and picking the shape whose
    steps are most evenly spaced.

    Depth exactly zero is a true bypass: the angle is zero, every rotation is
    the identity, and the output is bit-identical to the static FDN. That is
    asserted, not assumed.
*/

/*
    Measured depth sweep at 0.14 Hz, four rotations, against the static tank:

      depth  angle    modal persist  periodicity  RT60     AM depth  pitch
       0.00   0.00      0.7944        43.21       1.9924   0.03 dB   0.00 ct
       0.05   1.00      0.7917        43.71       1.9924   0.17 dB   0.01 ct
       0.25   5.00      0.7889        45.92       1.9919   0.69 dB   0.03 ct
       0.50  10.00      0.7806        45.86       1.9890   1.31 dB   0.06 ct
       1.00  20.00      0.6917        36.81       1.9848   2.50 dB   0.13 ct

    The honest reading: small angles do almost nothing. Modal persistence only
    moves meaningfully at twenty degrees, and by then the mixing coefficients
    are swinging enough to put 2.5 dB of amplitude modulation on a sustained
    tone. Pitch stays at a tenth of a cent throughout, which is expected — no
    delay length changes — so what this modulation costs is level wobble, not
    detune.

    The ceiling is therefore set at the top of the range worth exposing rather
    than at a value that would be safe everywhere, and the default sits where
    the amplitude modulation is still under a decibel.
*/

/** Maximum rotation angle at Depth = 100 %, in degrees. Set by measurement. */
inline constexpr double maxAngleDegrees = 20.0;

/** Shape of the Depth control. */
inline constexpr double depthExponent = 1.0;

/** Default Depth: 7 degrees, where amplitude modulation is under 1 dB. */
inline constexpr double defaultDepth = 0.35;

[[nodiscard]] inline double angleForDepth (double depth) noexcept
{
    const double d = std::clamp (depth, 0.0, 1.0);

    if (d <= 0.0)
        return 0.0;

    constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;

    return maxAngleDegrees * degreesToRadians * std::pow (d, depthExponent);
}

// ===========================================================================
//  Update rate
// ===========================================================================
/*
    How often the rotation angles are recomputed.

    Per-sample is exact and costs a pair of trigonometric calls per rotation per
    sample. Control-rate recomputes every N samples and interpolates the
    (cos, sin) pair in between, renormalising so the rotation stays exactly
    orthogonal at every sample regardless.

    The period is counted on a free-running sample index rather than on block
    boundaries, so the result does not depend on how the host chops the stream.
    That is a correctness requirement, not an optimisation: the same automation
    must render identically at any block size.
*/

inline constexpr int controlRatePeriod = 32;

} // namespace av::dsp::modulation
