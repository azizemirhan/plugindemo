#pragma once

#include <cmath>

/**
    Fractional-delay interpolation kernels.

    ---------------------------------------------------------------------------
    WHY THESE KERNELS, AND WHY NOT THE OTHERS
    ---------------------------------------------------------------------------

    A modulated delay line is read at a continuously moving fractional position.
    The interpolator sits inside every feedback path of the reverb, so its error
    is not applied once — it is applied once per circulation, thousands of times
    over a long tail. Two properties therefore dominate the choice:

      (a) Magnitude flatness. Any deviation from unity gain compounds
          exponentially along the feedback path. A kernel whose gain depends on
          the fractional part turns modulation into amplitude modulation, and a
          kernel that rolls off the top octave turns a bright tail into a dull
          one after enough circulations.

      (b) Statelessness. The read position moves every sample. A kernel that
          carries internal state has to settle after every move, which smears
          transients and makes the artefact depend on modulation speed.

    LINEAR (2-point)
        Stateless and cheap, but its magnitude response is a fractional-part
        dependent lowpass: gain at Nyquist swings between 1.0 (frac = 0) and 0.0
        (frac = 0.5). Under modulation this is audible as a "breathing" high end
        and, in a feedback loop, as a moving damping factor. Kept here because it
        is the correct choice for *static* taps where the fraction never changes
        (early reflections, fixed diffusion delays) and it costs one multiply.

    LAGRANGE 3rd ORDER (4-point)  <-- THE DEFAULT
        Stateless, reproduces polynomials up to degree 3 exactly, and monotonic
        in its rolloff: it never exceeds unity gain, so it cannot brighten a
        tail one circulation at a time.

    CUBIC HERMITE / CATMULL-ROM (4-point, 3rd order)
        Stateless, reproduces polynomials up to degree 2 exactly. Its one
        structural advantage over Lagrange is C1 continuity: as a moving read
        position crosses an integer sample, both kernels switch to a new set of
        four points, and Catmull-Rom's tangent at that knot is defined
        identically from either side, while Lagrange's is not. Since the slope
        of a delay is a pitch shift, that looked like the property a modulated
        delay line should want, and it was this project's original default.

        Measurement said otherwise. Sweeping a tap across eight integer
        boundaries with a 5 kHz source at 48 kHz
        (Tests/InterpolationTests.cpp):

            kernel      RMS error    worst error curvature
            linear      -31.2 dB     0.0219
            hermite     -50.5 dB     0.0021
            lagrange3   -53.5 dB     0.0017

        Lagrange wins on both counts. Its error is only C0 across a knot, but
        the error is small enough that the size of the discontinuity is still
        below Catmull-Rom's larger, smoother error. It is also the cheaper of
        the two to evaluate here (8.6 vs 10.0 ns per write+read in the CPU
        benchmark). Hermite is kept as a selectable alternative rather than
        removed, because the balance can change for a kernel driven much harder
        than this, and because having two independent implementations is how
        the accuracy tests stay honest.

    REJECTED — FIRST-ORDER ALLPASS
        Perfectly flat magnitude, which looks ideal, but it is stateful. When the
        delay time moves, the allpass coefficient moves with it and the filter
        has to re-settle, producing a transient chirp on every modulation cycle.
        It is only usable for delays that change rarely, and this project has no
        such delays that would not be better served by Hermite. Deliberately not
        implemented rather than left as a trap.

    REJECTED — WINDOWED SINC
        The best magnitude/aliasing behaviour available, but a useful kernel is
        16-32 taps wide. With dozens of modulated delay lines running, that cost
        buys less audible improvement than spending the same cycles on more
        delay lines and better modulation. Revisit only if measurements in
        PHASE 22 show interpolation to be the limiting artefact.

    ---------------------------------------------------------------------------
    CONVENTION
    ---------------------------------------------------------------------------

    All 4-point kernels take samples in increasing-time order and a fraction
    t in [0, 1) that interpolates between x0 and x1:

        xm1 = value at t = -1
        x0  = value at t =  0
        x1  = value at t = +1
        x2  = value at t = +2
*/
namespace av::dsp
{

/** Selects a fractional-delay interpolation kernel. */
enum class Interpolation
{
    none,      /**< Truncate to the integer sample. Zero cost, aliases badly. */
    linear,    /**< 2-point. Static taps only. */
    hermite,   /**< 4-point Catmull-Rom. C1 across knots, but less accurate. */
    lagrange3  /**< 4-point 3rd-order Lagrange. The project default. */
};

/** Number of samples the kernel needs before x0 (0 or 1). */
[[nodiscard]] inline constexpr int interpolationTapsBefore (Interpolation mode) noexcept
{
    return (mode == Interpolation::hermite || mode == Interpolation::lagrange3) ? 1 : 0;
}

/** Number of samples the kernel needs after x0 (0, 1 or 2). */
[[nodiscard]] inline constexpr int interpolationTapsAfter (Interpolation mode) noexcept
{
    switch (mode)
    {
        case Interpolation::none:      return 0;
        case Interpolation::linear:    return 1;
        case Interpolation::hermite:   return 2;
        case Interpolation::lagrange3: return 2;
    }

    return 2;
}

template <typename T>
[[nodiscard]] inline T interpolateLinear (T x0, T x1, T t) noexcept
{
    return x0 + t * (x1 - x0);
}

/** 4-point cubic Hermite with Catmull-Rom tangents, in Horner form. */
template <typename T>
[[nodiscard]] inline T interpolateHermite (T xm1, T x0, T x1, T x2, T t) noexcept
{
    const T c0 = x0;
    const T c1 = static_cast<T> (0.5) * (x1 - xm1);
    const T c2 = xm1 - static_cast<T> (2.5) * x0 + static_cast<T> (2.0) * x1
                     - static_cast<T> (0.5) * x2;
    const T c3 = static_cast<T> (0.5) * (x2 - xm1)
               + static_cast<T> (1.5) * (x0 - x1);

    return ((c3 * t + c2) * t + c1) * t + c0;
}

/** 4-point 3rd-order Lagrange. Exact for any cubic through the four points. */
template <typename T>
[[nodiscard]] inline T interpolateLagrange3 (T xm1, T x0, T x1, T x2, T t) noexcept
{
    constexpr T sixth = static_cast<T> (1.0 / 6.0);
    constexpr T half  = static_cast<T> (0.5);

    const T dm1 = t + static_cast<T> (1.0);
    const T d1  = t - static_cast<T> (1.0);
    const T d2  = t - static_cast<T> (2.0);

    return (-sixth * t   * d1 * d2) * xm1
         + ( half  * dm1 * d1 * d2) * x0
         + (-half  * dm1 * t  * d2) * x1
         + ( sixth * dm1 * t  * d1) * x2;
}

} // namespace av::dsp
