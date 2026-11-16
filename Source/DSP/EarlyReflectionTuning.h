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

        PRODUCTION LISTENING CANDIDATE, not a settled shipping constant.

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

        0.70 leaves a 15 % guard at each end, so two consecutive arrivals in
        one channel are never closer than 0.30 of a cell nor further apart
        than 1.70. Without it a pseudo-random draw eventually puts two events
        on adjacent sides of a cell boundary, which is an audible doublet
        rather than two reflections.
    */
    double cellOccupancy = 0.70;

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
//  Balance
// ===========================================================================
/**
    Static energy calibration between the two branches.

    The ER branch and the late branch do not arrive with comparable energy at
    unit gain, so a plain crossfade between them would also be a level change.
    `K` equalises them once.

    IT IS A CONSTANT, DELIBERATELY. Computing it from the running signal would
    be dynamic normalisation - gain pumping, program-dependent semantics, an
    AGC in all but name - and PHASE 10's brief forbids all three. So it is
    measured once at the reference configuration and frozen here, and a test
    re-measures the ratio and fails if the constant has drifted away from it.

    MEASURED AGAINST THE TRIMMED LATE BRANCH. Stage 2 reported 0.7811, taken
    against an untrimmed one, and that figure does not transfer: production
    multiplies the tank by the decay-aware compensation and the early layer by
    nothing, so the ratio the balance actually sees is a different ratio. The
    value here is 62.659 over 121.322 at Size 60 %, Decay 2.5 s, Early
    Diffusion 70 %, Mod Depth 30 %, driven by a 10 ms noise burst. With it the
    total wet energy moves 0.03 dB across the whole Early/Late travel.

    THE COST, STATED RATHER THAN HIDDEN. The late branch's energy moves with
    Decay and Size and a fixed K cannot follow it, so the calibration is exact
    at the reference and approximate elsewhere. Over Decay 0.5-10 s and Size
    20-100 % the worst error is 6.14 dB, at short Decay and small Size where
    the late branch is quietest. In other words Early/Late is a clean
    composition control at and near the defaults, and at the extremes it also
    changes the level somewhat. That is the price of refusing to normalise
    dynamically, and it is a reported characteristic rather than a defect
    awaiting a detector.
*/
inline constexpr double balanceCalibration = 0.7187;

/**
    Branch gains for a balance position.

    BAL-B, the energy-calibrated constant-power law Stage 2 selected: it held
    total wet energy to 0.01 dB across the travel, against 2.15 dB for an
    uncalibrated equal-power law and 3.02 dB for a linear one.

        theta = balance * pi/2
        late  = cos(theta)
        er    = sin(theta) * K

    BOTH ENDPOINTS ARE EXACT, AND THEY ARE WRITTEN OUT RATHER THAN TRUSTED TO
    THE LIBRARY. cos(0) is exactly 1 and sin(0) is exactly 0, so the lower end
    would come out right anyway. The upper end would NOT: pi/2 is not
    representable, so cos of the nearest double is about 6.1e-17 instead of
    zero, and the late branch at "early only" would sit 324 dB down rather
    than silent.

    That is inaudible and it is still worth removing, because it is the
    difference between a property that can be MEASURED and one that can only
    be approximated. With an exact endpoint, "Attack cannot reach the early
    layer" is a bit-level comparison at balance 100 %; with 6.1e-17 of late
    leaking through it is a test with a tolerance, and a tolerance is a thing
    that gets widened later.

    At balance 0 the processor does not multiply by these gains at all. It
    takes an explicit bypass branch, because PHASE 9 showed that arithmetic
    which happens to evaluate to a passthrough is not the same thing as a
    passthrough.
*/
struct BranchGains { double late = 1.0, early = 0.0; };

[[nodiscard]] inline BranchGains gainsForBalance (double balance01) noexcept
{
    const double u = std::clamp (balance01, 0.0, 1.0);

    if (u <= 0.0) return { 1.0, 0.0 };
    if (u >= 1.0) return { 0.0, balanceCalibration };

    const double theta = u * 1.5707963267948966;   // pi/2

    return { std::cos (theta), std::sin (theta) * balanceCalibration };
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
