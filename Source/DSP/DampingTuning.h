#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
    Tuning constants and closed-form maths for PHASE 7 frequency-dependent decay.

    As in FdnTuning.h and DiffusionTuning.h, every number that shapes the sound
    lives here with the reasoning that chose it, and the maths is written as
    free functions so the test harness can check it without instantiating a
    tank.

    WHAT DAMPING IS, IN ONE EQUATION
    --------------------------------
    The damping filter sits between the delay read and the per-line decay gain
    (FdnCore.h), so the two multiply. Per pass through line i at frequency w the
    signal is scaled by g_i * |H_i(w)|, and since g_i = 10^(-3 d_i / RT60) the
    band decay time follows directly:

           1              1          -log10 |H_i(w)|
        --------  =  ----------  +  -----------------
        RT60(w)      RT60_mid            3 * d_i

    For the tail to decay as ONE thing rather than as eight, log|H_i(w)| / d_i
    must be the same for every line. A single shared filter would leave the
    eight lines decaying at eight different rates in the damped bands. The
    shelf gains are therefore derived per line from that line's own loop delay,
    exactly as the decay gain already is.

    THE SEMANTICS, AND WHY NORMALISATION IS NOT OPTIONAL
    ----------------------------------------------------
    Decay is the LONGEST frequency-dependent RT60 in the tank, and damping is
    purely subtractive: with damping active, no frequency region may ring
    longer than the undamped Decay target.

    |H(w)| <= 1 alone does not deliver that, and the first implementation of
    this phase proved it the hard way. It anchored the mid band by dividing the
    decay gain by |H(f_mid)| at the geometric mean of the two corners. But the
    composite response does not peak at the geometric mean - it peaks where the
    two shelves' slopes balance, which moves with their depths - so everywhere
    above |H(f_mid)| the compensated loop exceeded the undamped gain. Measured:
    every one of 29951 normal-ordered combinations had a region decaying longer
    than baseline, 0.887 % at the shipping defaults, and in one reachable
    corner (100 Hz / 500 Hz, Low Damp 0 %, High Damp 100 %, Decay 10 s) the
    compensation asked for a loop gain above unity and the feedback clamp left
    a 363 second tail against a 10 second target.

    So the response is normalised by its own maximum instead:

        M_i    = max_w |H_i(w)|
        Hn_i   = H_i / M_i          =>  max_w |Hn_i(w)| = 1
        total  = g_i * |Hn_i(w)|    <=  g_i

    The normalisation is folded into the FILTER, not into the decay gain. That
    keeps g_i exactly what PHASE 6 left it, so the feedback clamp is never
    reached for a damping reason and the 0 % case stays bit-identical.

    WHY SHELVES AND NOT LOWPASSES
    -----------------------------
    A lowpass has no unity plateau: its magnitude is already below 1 well below
    its cutoff, so mid-band RT60 would sag the moment High Damp was raised. A
    shelf pair leaves the mid band at unity to first order, which is what lets
    the Decay control keep meaning what it says.

    WHY FIRST ORDER IS ENOUGH
    -------------------------
    The filter is applied once per pass, so after k circulations the effective
    response is |H(w)|^k: a gentle 6 dB/oct shelf becomes progressively steeper
    with time on its own. A steep per-pass filter would add ringing inside the
    loop for no benefit. This is a property of feedback topology, not a
    compromise.
*/
namespace av::dsp::damping
{

inline constexpr double pi = 3.14159265358979323846;

// ===========================================================================
//  Control mapping
// ===========================================================================
/*
    Low Damp and High Damp are RATIOS OF DECAY TIME, not attenuations and not
    filter coefficients.

        High Damp = RT60_high / RT60_mid
        Low  Damp = RT60_low  / RT60_mid

    Of the three candidates this was chosen from, it is the only one that is a
    time the user can hear. An attenuation in dB per pass means nothing to a
    listener and does something different on every line; a raw filter
    coefficient is not a musical quantity and changes meaning with sample rate.

    Decay times are perceived multiplicatively - the same argument
    SmoothedParameter.h makes for LogSmoother - so the control maps
    exponentially rather than linearly:

        ratio(p) = ratioAtFull ^ p        p = damp% / 100

    p = 0 gives exactly 1.0, so a zeroed control is exactly transparent: not
    approximately, bit-exactly.
*/

/** Decay ratio High Damp reaches at 100 %. Listening-tuned, not derived. */
inline constexpr double highRatioAtFull = 0.125;

/** Decay ratio Low Damp reaches at 100 %. Gentler than the high band on
    purpose: shortening the low end is a corrective move against a boomy tail,
    not a voicing gesture, and it runs out of musical usefulness sooner. */
inline constexpr double lowRatioAtFull = 0.25;

/** ratio(p) = ratioAtFull^p, clamped, with p = 0 exactly transparent. */
[[nodiscard]] inline double decayRatioFor (double control, double ratioAtFull) noexcept
{
    const double p = std::clamp (control, 0.0, 1.0);

    if (p <= 0.0)
        return 1.0;

    return std::pow (std::clamp (ratioAtFull, 1.0e-6, 1.0), p);
}

// ===========================================================================
//  Shelf gains
// ===========================================================================

/** Smallest shelf gain the maths will produce. -120 dB per pass is already a
    brickwall; the floor exists so an extreme Decay/Size/Damp combination
    cannot underflow rather than to limit the sound. */
inline constexpr double minShelfGain = 1.0e-6;

/**
    Per-pass shelf gain that turns RT60_mid into ratio * RT60_mid on one line.

        G = 10^( -3 * d * (1/ratio - 1) / RT60_mid )

    Returns exactly 1.0 for ratio >= 1, so an untouched control contributes
    nothing at all rather than a value that merely rounds to nothing.
*/
[[nodiscard]] inline double shelfGainFor (double delaySeconds,
                                          double rt60Seconds,
                                          double ratio) noexcept
{
    if (ratio >= 1.0 || rt60Seconds <= 0.0 || delaySeconds <= 0.0)
        return 1.0;

    const double excess = (1.0 / std::max (ratio, 1.0e-6)) - 1.0;

    return std::clamp (std::pow (10.0, -3.0 * delaySeconds * excess / rt60Seconds),
                       minShelfGain, 1.0);
}

// ===========================================================================
//  One-pole coefficient
// ===========================================================================

inline constexpr double minCutoffHz = 5.0;

/**
    Impulse-invariant one-pole coefficient: a = exp(-2 pi f / fs).

    Chosen over a bilinear-transformed biquad for two reasons that matter
    inside a feedback loop. It yields a in (0, 1) for every f > 0 at every
    sample rate, so there is no coefficient region to exclude and no
    pre-warping blow-up near Nyquist; and a one-pole's coefficient can change
    discontinuously without ringing, because it has no resonant memory, which
    is what makes cutoff automation click-free by construction rather than by
    smoothing hard enough.

    The corner is warped for f approaching Nyquist. That is documented and left
    uncorrected: correcting it would cost the unconditional stability above.
*/
[[nodiscard]] inline double onePoleCoefficient (double cutoffHz, double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return 0.0;

    const double f = std::clamp (cutoffHz, minCutoffHz, 0.49 * sampleRate);

    return std::exp (-2.0 * pi * f / sampleRate);
}

// ===========================================================================
//  Shelf pair
// ===========================================================================

/**
    One line's damping, as coefficients.

    Low shelf:   H_lo(z) = 1 - (1 - gainLow) * LP(z, coeffLow)
    High shelf:  H_hi(z) = gainHigh + (1 - gainHigh) * LP(z, coeffHigh)

    with LP the one-pole lowpass (1-a)/(1 - a z^-1).

    H_lo is exactly gainLow at DC; H_hi is exactly 1 at DC. Neither reaches its
    far-side asymptote exactly, because the one-pole does not reach zero at
    Nyquist: LP(Nyquist) = (1-a)/(1+a). So H_hi(Nyquist) is
    gainHigh + (1-gainHigh)(1-a)/(1+a), not gainHigh. The shortfall grows as
    the corner approaches Nyquist and is reported by the test harness rather
    than papered over - it means a high-band ratio target is approached, not
    hit exactly, at high corner frequencies.

    Both have the form G*1 + (1-G)*F with G in [0,1] and |F| <= 1, so by the
    triangle inequality |H| <= 1 for every parameter value and sample rate.
*/
struct ShelfSpec
{
    double gainLow   = 1.0;   ///< shelf gain below the low corner
    double coeffLow  = 0.0;   ///< one-pole a for the low corner
    double gainHigh  = 1.0;   ///< shelf gain above the high corner
    double coeffHigh = 0.0;   ///< one-pole a for the high corner

    [[nodiscard]] bool isTransparent() const noexcept
    {
        return gainLow >= 1.0 && gainHigh >= 1.0;
    }
};

// ---------------------------------------------------------------------------
//  Closed-form magnitudes, in u = cos(w)
// ---------------------------------------------------------------------------
/*
    Both shelf magnitudes are rational in u = cos(w), and - this is the part
    that makes a trustworthy maximum possible - both are MONOTONIC in it.

    For the low shelf, writing b = 1 - (1-G)(1-a):

        |H_lo|^2 = (b^2 + a^2 - 2 a b u) / (1 + a^2 - 2 a u)

        d/du = 2a (b - 1)(b - a^2) / (denominator)^2

    b <= 1 gives (b-1) <= 0, and b >= a >= a^2 gives (b - a^2) >= 0, so the
    derivative is <= 0: |H_lo| never decreases with frequency.

    For the high shelf, writing c = G + (1-G)(1-a):

        |H_hi|^2 = (c^2 + G^2 a^2 - 2 a c G u) / (1 + a^2 - 2 a u)

        d/du = 2a (c - G)(c - a^2 G) / (denominator)^2

    c - G = (1-G)(1-a) >= 0 and c >= G >= a^2 G, so the derivative is >= 0:
    |H_hi| never increases with frequency.

    A rising factor times a falling factor is not unimodal in general, so the
    maximum is not assumed unimodal. It is solved for exactly - see
    compositeMaximum below.
*/

[[nodiscard]] inline double lowShelfMagnitudeSquared (const ShelfSpec& spec, double u) noexcept
{
    const double a = spec.coeffLow;
    const double b = 1.0 - (1.0 - spec.gainLow) * (1.0 - a);

    const double denominator = 1.0 + a * a - 2.0 * a * u;

    if (denominator <= 0.0)
        return spec.gainLow * spec.gainLow;

    return (b * b + a * a - 2.0 * a * b * u) / denominator;
}

[[nodiscard]] inline double highShelfMagnitudeSquared (const ShelfSpec& spec, double u) noexcept
{
    const double a = spec.coeffHigh;
    const double g = spec.gainHigh;
    const double c = g + (1.0 - g) * (1.0 - a);

    const double denominator = 1.0 + a * a - 2.0 * a * u;

    if (denominator <= 0.0)
        return g * g;

    return (c * c + g * g * a * a - 2.0 * a * c * g * u) / denominator;
}

/** |H(f)| for the shelf pair - exact, from the closed forms above. */
[[nodiscard]] inline double magnitudeAt (const ShelfSpec& spec,
                                         double freqHz,
                                         double sampleRate) noexcept
{
    if (spec.isTransparent() || sampleRate <= 0.0)
        return 1.0;

    const double u = std::cos (2.0 * pi
                               * std::clamp (freqHz, 0.0, 0.5 * sampleRate) / sampleRate);

    return std::sqrt (std::max (0.0, lowShelfMagnitudeSquared  (spec, u))
                      * std::max (0.0, highShelfMagnitudeSquared (spec, u)));
}

// ===========================================================================
//  The composite maximum, in closed form
// ===========================================================================
/*
    The normalisation is only as trustworthy as the maximum it divides by, and
    the maximum has to be exact in one direction specifically: an UNDER-estimate
    leaves |Hn| above 1 and puts the subtractive invariant back where it was.

    A grid search cannot promise that, and bounding a grid from above is both
    loose and expensive. It is not needed, because the maximum has a closed
    form.

    Both factors are rational in u = cos(w), so their log-derivatives are too:

        d/du log|H_lo|^2 = K1 / (D1 * N1)      K1 = 2 a_l (b - 1)(b - a_l^2)
        d/du log|H_hi|^2 = K2 / (D2 * N2)      K2 = 2 a_h (c - G)(c - a_h^2 G)

    with D and N the linear denominators and numerators of the two magnitude
    formulas above. K1 <= 0 and K2 >= 0, which is the monotonicity result
    restated. Setting the sum to zero:

        K1 * D2 * N2  +  K2 * D1 * N1  =  0

    Each product is a quadratic in u, so this is one quadratic equation. Its
    real roots in [-1, 1], together with the two endpoints u = 1 (DC) and
    u = -1 (Nyquist), are the only places the composite response can peak.

    Exact, O(1), no grid, no iteration, and nothing whose accuracy depends on
    the parameter values. It runs when coefficients change, never per sample.
*/

/** Relative safety margin on the maximum. Numerical only, never a tuning knob. */
inline constexpr double maximumSafetyMargin = 1.0e-9;

struct MagnitudeBound
{
    double lower = 1.0;      ///< the largest magnitude actually evaluated
    double upper = 1.0;      ///< what the normalisation divides by
    double frequencyU = 1.0; ///< cos(w) where the maximum sits

    [[nodiscard]] double bracketWidth() const noexcept { return upper - lower; }
};

/**
    The exact maximum of |H(w)| over the whole frequency axis.

    `upper` carries a relative margin of maximumSafetyMargin so that rounding
    in the root solve can only ever make the damping infinitesimally deeper,
    never shallower.
*/
[[nodiscard]] inline MagnitudeBound compositeMaximum (const ShelfSpec& spec) noexcept
{
    if (spec.isTransparent())
        return {};

    const double al = spec.coeffLow;
    const double ah = spec.coeffHigh;
    const double g  = spec.gainHigh;

    const double b = 1.0 - (1.0 - spec.gainLow) * (1.0 - al);
    const double c = g + (1.0 - g) * (1.0 - ah);

    // D1 * N1 = (p + q u)(r + s u), and likewise for the high shelf.
    const double p = 1.0 + al * al,  q = -2.0 * al;
    const double r = b * b + al * al, s = -2.0 * al * b;

    const double p2 = 1.0 + ah * ah,        q2 = -2.0 * ah;
    const double r2 = c * c + g * g * ah * ah, s2 = -2.0 * ah * c * g;

    const double k1 = 2.0 * al * (b - 1.0) * (b - al * al);
    const double k2 = 2.0 * ah * (c - g)   * (c - ah * ah * g);

    const double quadratic = k1 * q2 * s2 + k2 * q * s;
    const double linear    = k1 * (p2 * s2 + q2 * r2) + k2 * (p * s + q * r);
    const double constant  = k1 * p2 * r2 + k2 * p * r;

    // Candidates always include both endpoints, so a degenerate solve - one
    // shelf transparent, or both corners identical - is safe rather than
    // special-cased.
    double candidates[4] { 1.0, -1.0, 1.0, -1.0 };
    int count = 2;

    const auto addRoot = [&candidates, &count] (double u) noexcept
    {
        if (u > -1.0 && u < 1.0 && count < 4)
            candidates[count++] = u;
    };

    if (std::abs (quadratic) > 1.0e-300)
    {
        const double discriminant = linear * linear - 4.0 * quadratic * constant;

        if (discriminant >= 0.0)
        {
            // The numerically stable pairing, so the small root does not come
            // out of a subtraction of two nearly equal numbers.
            const double root = std::sqrt (discriminant);
            const double sign = (linear >= 0.0) ? 1.0 : -1.0;
            const double half = -0.5 * (linear + sign * root);

            addRoot (half / quadratic);

            if (std::abs (half) > 1.0e-300)
                addRoot (constant / half);
        }
    }
    else if (std::abs (linear) > 1.0e-300)
    {
        addRoot (-constant / linear);
    }

    MagnitudeBound result { 0.0, 0.0, 1.0 };

    for (int i = 0; i < count; ++i)
    {
        const double u = candidates[i];

        const double magnitude =
            std::sqrt (std::max (0.0, lowShelfMagnitudeSquared  (spec, u))
                       * std::max (0.0, highShelfMagnitudeSquared (spec, u)));

        if (magnitude > result.lower)
        {
            result.lower = magnitude;
            result.frequencyU = u;
        }
    }

    result.upper = std::clamp (result.lower * (1.0 + maximumSafetyMargin),
                               minShelfGain, 1.0);

    return result;
}

/**
    Filter gain that makes the response peak at exactly 1.

    The normalised response is at or below unity everywhere, which is what
    makes damping purely subtractive: with the decay gain untouched, no
    frequency can decay longer than the undamped Decay target.
*/
[[nodiscard]] inline double normalisationFor (const ShelfSpec& spec) noexcept
{
    if (spec.isTransparent())
        return 1.0;

    return 1.0 / compositeMaximum (spec).upper;
}

// ===========================================================================
//  Smoothing
// ===========================================================================

/** Log-domain glide for the two corner frequencies. Control rate. */
inline constexpr double frequencyGlideSeconds = 0.05;

} // namespace av::dsp::damping
