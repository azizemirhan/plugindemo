#pragma once

#include <algorithm>
#include <cmath>

namespace av::dsp::er
{

/**
    PHASE 10. Early reflection pattern data, separate from the engine.

    THE SPLIT IS THE POINT. `EarlyReflections` is a tap-list convolver with a
    crossfade; everything that decides WHERE the taps are, how loud they are
    and how the pattern scales lives here. A future Room, Hall, Chamber, Plate
    or Ambient mode supplies different data through `Pattern` without the
    engine being rewritten - which is the same split `DiffusionTuning.h` and
    `FdnTuning.h` already use.

    No mode is implemented now. One pattern exists, and it is the one PHASE 10
    Stage 2 selected.

    NOTHING HERE IS A MEASURED ROOM. The timing is a deterministic synthetic
    distribution: the span is divided into equal cells, one event per cell per
    channel, positioned inside its cell by a fixed-seed pseudo-random draw -
    velvet noise, in the sense the term is normally used for this problem. No
    impulse response was copied and no product's reflection pattern was
    referenced.

    THE PLACEMENT LAW CHANGED DURING STAGE 3, AND THE REASON IS RECORDED
    RATHER THAN QUIETLY DROPPED. The first implementation used PHASE 9's
    golden-ratio-inside-each-cell placement, and the schedule test caught what
    source reasoning had not: with one event per equal cell, frac(phi*k)
    advances by the SAME amount mod 1 at every step, so the position inside
    the cell moves by a constant and the gap between consecutive arrivals can
    only ever take two values. The merged stereo sequence came out as

        177 119 178 297 302 297 | 177 119 178 297 302 297 | 177 297 302

    - a gap pattern that repeats exactly every seven arrivals. That is a comb
    with a rhythm in it, which is the one thing an early-reflection layer must
    not be, and it is precisely what the irregular placement was supposed to
    prevent. The property is inherent to the construction rather than to the
    constant: any irrational multiplier produces the same two-valued gap, so
    changing phi for something else would not have helped.

    The draw is now pseudo-random inside the cell, from a fixed seed, which is
    just as deterministic and genuinely aperiodic. Each event is confined to
    the middle `cellOccupancy` of its cell, which bounds the shortest gap away
    from zero - the standard velvet-noise guard against two arrivals landing
    on top of each other across a cell boundary.

    NOTE FOR PHASE 9, NOT A CHANGE TO IT. dsp::AttackInjection uses the
    original construction and therefore has the same two-valued gap. It is not
    touched here: it was selected on mean arrival gap and third-octave ripple,
    it passed its listening validation, and its taps are about 8 ms apart
    where this layer's are 3 to 12 ms. Changing it would alter the shipped
    Attack sound for a property nobody reported hearing. Recorded so that the
    next person to look at it finds the measurement rather than repeating it.

    Specified in MILLISECONDS throughout, so a pattern means the same thing at
    every sample rate.
*/

/** How the taps are laid out. One instance today; Mode data later. */
struct Pattern
{
    /** Reflection events per channel. 8, from the Stage-2 comparison. */
    int events = 8;

    /** The span at the default Size, in milliseconds. */
    double windowMs = 80.0;

    /**
        Gain envelope decay, as a multiple of the window.

            gain(t) = exp(-t / (windowMs * decay)) / sqrt(t)

        ACCEPTED SHIPPING CONSTANT, PHASE 10 closure. It was a listening
        candidate through Stages 3 and 3.5; the final sanity listening found
        no audible collapse or disconnection on pad, vocal or piano, and the
        snare returned no repeatable defect. Do not reopen without new
        evidence.

        The reasoning that selected it is kept below rather than trimmed to a
        verdict, because the next person to touch this will need the argument
        and not the conclusion.

        Stage 2 showed the large-Size handoff valley is driven by this rather
        than by the ER span: raising the ER/late overlap from 44.5 to 67.3 ms
        moved the worst valley only 6.93 -> 6.48 dB, while raising this decay
        from 0.6 to 4.0 moved it 8.09 -> 5.15 dB. 1.5 takes about 70 % of the
        available improvement while keeping the ER tail at -16.4 dB, which
        matters because a layer still loud at the end of its span is edging
        toward being a second late reverb.

        It does NOT close the valley. The residual is structural - at large
        Size the late field starts around 54 ms and a quiet ER layer cannot
        bridge that - and whether it is heard as natural separation or as a
        hole is a listening question, not a metric one.
    */
    double envelopeDecay = 1.5;

    /**
        How the span follows Size: span = windowMs * sizeScaleRatio^alpha.

        0.25, from the Stage-2 sweep. Two constraints bound it. The span must
        still reach past the late field's own first arrival, which every alpha
        satisfies. And it must stay under about 120 ms, or the layer stops
        being reflections and becomes a second late reverb - which rejects
        alpha 0.5 and above. 0.25 gives 58.6 to 98.5 ms across the Size range,
        a 1.68x travel against the late network's 8x, so the two move together
        in direction without moving as one rigid object.
    */
    double sizeAlpha = 0.25;

    /**
        The share of each cell an event may land in, centred.

        0.40 leaves a 30 % guard at each end, so two consecutive arrivals in
        one channel are never closer than 0.60 of a cell nor further apart
        than 1.40. Some guard is essential: without it a pseudo-random draw
        eventually puts two events on adjacent sides of a cell boundary, which
        is an audible doublet rather than two reflections.

        0.40 IS THE ACCEPTED PHASE 10 CONSTANT, AND IT REPLACED 0.70 ON
        EVIDENCE. Accepted at closure after the final sanity listening; do
        not reopen without new evidence. The occupancy sweep found this field
        dominates the
        large-Size handoff valley: 16.47 dB at 0.70 against 8.86 dB here and
        7.26 dB at 0.20, with 0.70 sitting just below a cliff (0.95 gives
        42 dB). The blind A/B that followed - 0.20 against 0.40, a transient
        and a vocal, both at Size 100 % - returned NO MEANINGFUL PREFERENCE
        on either case, so the choice between the two survivors fell back to
        measurement.

        0.40 wins that on the axis the valley does not cover. Third-octave
        ripple is 3.76 dB here against 5.18 dB at 0.20, and arrivals sit
        further from their cell centres, which is the direction away from
        regular spacing and therefore away from slap. Both values are fully
        aperiodic - 0 % gap repetition, fifteen distinct intervals out of
        fifteen - where 0.15 and 0.30 are not, and both are indistinguishable
        on correlation, mono downmix and CPU.

        So: 0.40 gives up 1.6 dB of handoff against 0.20 and takes 1.4 dB of
        spectral smoothness for it, having been told by a listener that
        neither difference was audible. That is a defensible compromise
        rather than an optimum, and it is recorded as a compromise.
    */
    double cellOccupancy = 0.40;

    /** Share of the window the first event may not precede, as a fraction. */
    double earliestFraction = 0.0;
};

/** The pattern PHASE 10 ships. Mode data would add siblings, not edit this. */
inline constexpr Pattern defaultPattern {};

/**
    The Size control position the pattern's `windowMs` is quoted at.

    Must track the Size parameter's default in ParameterLayout.cpp. It is
    repeated rather than shared because the DSP headers deliberately do not
    include the parameter layout, and a test asserts the two agree so the
    duplication cannot drift silently.
*/
inline constexpr double referenceSizeControl = 0.60;

/** Longest span the engine allocates for, across every Size and pattern. */
inline constexpr double maxSpanMs = 160.0;

/** Highest number of events per channel any pattern may ask for. */
inline constexpr int maxEvents = 32;

// ===========================================================================
//  Balance: the parameter-aware calibration, and the gain law
// ===========================================================================
/**
    BAL-4. The Early/Late calibration, as a function of Decay and Size.

    WHY A FUNCTION AND NOT A CONSTANT. The ER branch and the late branch do not
    arrive with comparable energy at unit gain, so a crossfade between them is
    also a level change unless something equalises them. PHASE 10 shipped a
    single constant for that and Stage 3.5 measured what it costs: the ideal
    ratio spans 30.68 dB across the Decay x Size plane, so one number is wrong
    by up to 26.26 dB at the short-Decay corner. The ER branch is not the
    variable - its energy moves 0.02 dB over the whole plane, because the taps
    are L2-normalised - so all of it is the late branch, and all of it is a
    deterministic function of two parameters.

    WHAT IT IS NOT. It is not derived from the audio. No level detection, no
    envelope follower, no correlation measurement, no source classification,
    no AGC. K changes when the user changes Decay or Size and at no other
    time, which is what makes the control predictable and automatable. The
    alternative - tracking the programme material - was rejected on purpose: a
    balance that calibrated itself to the signal would behave differently on a
    snare and on a pad, which is exactly the unpredictability a composition
    control must not have.

    WHY A TABLE AND NOT A FORMULA. Stage 3.5 fitted three analytic forms and
    the best of them, a four-coefficient log-quadratic, leaves 8.71 dB at
    Decay 0.1 s where this table leaves 1.26 dB. The surface has an inverted
    corner there - ideal K FALLS with Size at 0.1 s while rising with it
    everywhere else, because the decay-aware trim's 12 dB safety limit is
    active - and a smooth formula cannot pass through it without bending
    everywhere else. Fifteen measured numbers can.

    HOW IT IS INTERPOLATED. Bilinearly in log(rt60) x log(sizeScale). The log
    domain is what keeps the surface monotone between knots, so turning Decay
    up never makes the early layer quieter on one side of a knot and louder on
    the other. Outside the knots it clamps rather than extrapolating.

    MEASURED WITH THE FINAL PATTERN. The table is a property of the ER
    pattern, so it was re-measured after cellOccupancy became 0.40 rather than
    carried over. A test re-measures it and fails on drift, and the pattern
    fingerprint test fails if the pattern changes at all.
*/
inline constexpr int calibrationDecayKnots = 5;
inline constexpr int calibrationSizeKnots  = 3;

/**
    The knot axes, in RT60 seconds and in Size scale - the engine's units, not
    the control's.

    Stated as literals so this header needs no dependency on the parameter
    mapping, and checked against mapping::rt60For and mapping::sizeScaleFor by
    a test, so the duplication cannot drift silently. They correspond to Decay
    0.1 / 0.5 / 2.5 / 10 / 60 s and Size 0 / 50 / 100 %.
*/
inline constexpr double calibrationRt60[calibrationDecayKnots]
    { 0.1, 0.5, 2.5, 10.0, 60.0 };

inline constexpr double calibrationSizeScale[calibrationSizeKnots]
    { 0.25, 0.70710678118654752, 2.0 };

/**
    The measured ideal K at each knot: sqrt(late energy / ER energy) with the
    production pattern, a 10 ms noise burst, a 4 s window, and every other
    control at its default.

    Indexed [decay][size].
*/
inline constexpr double calibrationTable[calibrationDecayKnots][calibrationSizeKnots]
{
    //  Size 0 %   Size 50 %  Size 100 %
    { 0.0963,    0.0643,    0.0350 },   // Decay  0.1 s
    { 0.3049,    0.4245,    0.5210 },   // Decay  0.5 s
    { 0.4156,    0.6575,    1.0324 },   // Decay  2.5 s
    { 0.4426,    0.7217,    1.1937 },   // Decay 10.0 s
    { 0.3750,    0.6169,    1.0375 },   // Decay 60.0 s
};

/*
    THE 0.1 s ROW RUNS THE OTHER WAY, and that is measured rather than a typo.

    Every other row rises with Size. That one falls - 0.0963 down to 0.0350 -
    because at a 0.1 s decay the trim's 12 dB safety limit is active and the
    late branch is not being compensated the way it is everywhere else.
    PHASE 6 recorded that limit as a known unmet target at exactly this decay
    and attributed it to the limit rather than to its table.

    This is the corner that rules out an analytic fit: a smooth surface cannot
    invert here without bending everywhere else, and the best four-coefficient
    fit leaves 8.71 dB at this decay where the table leaves 1.26 dB.
*/

/**
    K for an RT60 and a Size scale. Real-time safe: two logs, a short search
    over five and three knots, and four more logs for the corners. No
    allocation, no branches on data the audio thread cannot see.

    The corner values are interpolated in the LOG domain and exponentiated
    once, which makes the result a geometric blend rather than an arithmetic
    one - a gain should interpolate as a ratio.
*/
[[nodiscard]] inline double calibrationFor (double rt60, double sizeScale) noexcept
{
    const double lr = std::log (std::max (1.0e-6, rt60));
    const double ls = std::log (std::max (1.0e-6, sizeScale));

    auto axis = [] (const double* knots, int count, double value,
                    int& lower, double& t)
    {
        lower = 0;

        while (lower + 2 < count && value > std::log (knots[lower + 1]))
            ++lower;

        const double lo = std::log (knots[lower]);
        const double hi = std::log (knots[lower + 1]);

        t = hi > lo ? (value - lo) / (hi - lo) : 0.0;
        t = std::clamp (t, 0.0, 1.0);          // clamp, never extrapolate
    };

    int di = 0, si = 0;
    double dt = 0.0, st = 0.0;

    axis (calibrationRt60,      calibrationDecayKnots, lr, di, dt);
    axis (calibrationSizeScale, calibrationSizeKnots,  ls, si, st);

    const double a = std::log (calibrationTable[di][si]);
    const double b = std::log (calibrationTable[di][si + 1]);
    const double c = std::log (calibrationTable[di + 1][si]);
    const double d = std::log (calibrationTable[di + 1][si + 1]);

    const double top    = a + st * (b - a);
    const double bottom = c + st * (d - c);

    return std::exp (top + dt * (bottom - top));
}

/**
    The reference configuration's K - Decay 2.5 s, Size 60 % - for tests and
    for the report.

    IT IS NO LONGER THE CALIBRATION. Production looks K up from the table
    above; this is one point on that surface, kept because a handful of tests
    need a single representative value and because the earlier reports quote
    it. Anything that uses this where it should use `calibrationFor` is a bug,
    and the name is deliberately not `balanceCalibrationConstant` so that it
    reads as a reference rather than as a setting.
*/
inline constexpr double balanceCalibration = 0.7187;

/**
    Branch gains for a balance position, given that position's K.

    THE LAW IS LINEAR IN POWER, and that is a change from PHASE 10's original
    trigonometric form rather than a restatement of it:

        late  = sqrt(1 - t)
        early = sqrt(t) * K

    Both forms are exactly constant power - the squared gains sum to one - and
    they are not equivalent. This one splits POWER linearly in the control,
    which makes the total energy

        total / Le  =  (1 - t)  +  t * (K^2 Ee / Le)

    a straight line in t for ANY K, right or wrong. The trigonometric form
    also sums to one, but its split is not linear in t, so a mismatched K
    makes the curve bow away from its own chord - a dip in the middle of the
    control rather than a clean tilt.

    THAT PROPERTY IS THE WHOLE REASON FOR THE CHANGE, because K will always be
    somewhat wrong: it is a function of two parameters and the ideal value also
    depends on the programme material, by 6 to 7.6 dB. Measured across a 24 dB
    span of deliberate mis-calibration the trigonometric form bows by up to
    0.189 dB and this one stays flat to three decimals; on a sustained pad the
    worst sag falls from 1.28 dB to 0.08 dB.

    An exponent family between constant power and linear amplitude was also
    measured and rejected: sag grows monotonically with the exponent on every
    source, because the branches never get correlated enough - 0.199 at worst -
    for amplitude addition to be the better model.

    BOTH ENDPOINTS ARE WRITTEN OUT EXPLICITLY. sqrt(0) and sqrt(1) are exact
    in IEEE arithmetic, so the branches are redundant; they are here anyway,
    because an endpoint that is correct by coincidence of the library is not
    the same claim as one that is correct by construction, and PHASE 10 has
    been bitten once already by trusting cos(pi/2) to be zero.

    At balance 0 the processor does not multiply by these gains at all. It
    takes an explicit bypass branch, because PHASE 9 showed that arithmetic
    which happens to evaluate to a passthrough is not the same thing as a
    passthrough.
*/
struct BranchGains { double late = 1.0, early = 0.0; };

[[nodiscard]] inline BranchGains gainsForBalance (double balance01, double k) noexcept
{
    const double u = std::clamp (balance01, 0.0, 1.0);

    if (u <= 0.0) return { 1.0, 0.0 };
    if (u >= 1.0) return { 0.0, k };

    return { std::sqrt (1.0 - u), std::sqrt (u) * k };
}

/** The span, in milliseconds, for a pattern at a Size scale ratio. */
[[nodiscard]] inline double spanFor (const Pattern& pattern, double sizeScaleRatio) noexcept
{
    const double ratio = std::max (1.0e-6, sizeScaleRatio);

    return std::min (maxSpanMs, pattern.windowMs * std::pow (ratio, pattern.sizeAlpha));
}

/** The gain envelope at a time, for a pattern over a span. */
[[nodiscard]] inline double envelopeAt (const Pattern& pattern,
                                        double timeMs, double spanMs) noexcept
{
    const double t = std::max (1.0, timeMs);

    return std::exp (-t / (spanMs * pattern.envelopeDecay)) / std::sqrt (t);
}

} // namespace av::dsp::er
