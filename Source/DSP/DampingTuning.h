#pragma once

#include <algorithm>
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

    Two consequences shape everything below.

      - If |H_i(w)| <= 1 everywhere then RT60(w) <= RT60_mid everywhere.
        Damping can only ever shorten a band, at any setting, and the tank's
        stability argument (max g_i < 1) survives unchanged.

      - For the tail to decay as ONE thing rather than as eight, the quantity
        log|H_i(w)| / d_i must be the same for every line. A single shared
        filter would leave the eight lines decaying at eight different rates in
        the damped bands. The shelf gains are therefore derived per line from
        that line's own loop delay, exactly as the decay gain already is.

    WHY SHELVES AND NOT LOWPASSES
    -----------------------------
    A lowpass has no unity plateau: its magnitude is already below 1 well below
    its cutoff, so mid-band RT60 would sag the moment High Damp was raised, and
    the Decay control would stop meaning what it says. A shelf pair leaves the
    mid band at unity by construction, which is why the existing RT60
    calibration survives Phase 7 untouched.

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
    A decay ratio is invariant under Size, under Decay and under sample rate by
    construction.

    Decay times are perceived multiplicatively - the same argument
    SmoothedParameter.h makes for LogSmoother - so the control maps
    exponentially rather than linearly:

        ratio(p) = ratioAtFull ^ p        p = damp% / 100

    p = 0 gives exactly 1.0, so a zeroed control is exactly transparent: not
    approximately, bit-exactly. p = 1 gives ratioAtFull.
*/

/** Decay ratio High Damp reaches at 100 %. Listening-tuned, not derived. */
inline constexpr double highRatioAtFull = 0.125;

/** Decay ratio Low Damp reaches at 100 %. Gentler than the high band on
    purpose: shortening the low end is a corrective move against a boomy tail,
    not a voicing gesture, and it runs out of musical usefulness sooner. */
inline constexpr double lowRatioAtFull = 0.25;

/*
    At the shipping defaults - Low Damp 20 %, High Damp 50 %, Decay 2.5 s -
    these give:

        low  band  ratio 0.758   ->  1.90 s
        mid  band  ratio 1.0     ->  2.50 s   (anchored, see below)
        high band  ratio 0.354   ->  0.88 s

    A mildly tightened low end and a natural darkening above 6 kHz, with the
    mid band untouched.
*/

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

    From the equation at the top, with RT60_band = ratio * RT60_mid:

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
                 exactly gainLow at DC, approaching 1 above its corner.

    High shelf:  H_hi(z) = gainHigh + (1 - gainHigh) * LP(z, coeffHigh)
                 exactly 1 at DC, exactly gainHigh at Nyquist.

    Both have the form G*1 + (1-G)*F with G in [0,1] and |F| <= 1, so by the
    triangle inequality |H| <= G + (1-G) = 1 for every parameter value, every
    sample rate and every combination. |LP| <= 1 with its maximum at DC;
    |1 - LP| <= 1 with its maximum 2a/(1+a) at Nyquist. No coefficient region
    needs excluding — this is a bound, not a tuning result.
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

/**
    |H(f)| for the shelf pair — the exact magnitude, not an approximation.

    Used in two places, which is why it lives here rather than in the filter:
    to normalise the mid band (see plateauGain below) and to prove
    max|H| <= 1 over a dense sweep in the test harness.
*/
[[nodiscard]] inline double magnitudeAt (const ShelfSpec& spec,
                                         double freqHz,
                                         double sampleRate) noexcept
{
    if (spec.isTransparent())
        return 1.0;

    if (sampleRate <= 0.0)
        return 1.0;

    const double w = 2.0 * pi * std::clamp (freqHz, 0.0, 0.5 * sampleRate) / sampleRate;
    const double cw = std::cos (w);
    const double sw = std::sin (w);

    // LP = (1-a) / (1 - a e^-jw), evaluated as (1-a) * conj(D) / |D|^2.
    const auto lowpass = [cw, sw] (double a, double& re, double& im) noexcept
    {
        const double dr = 1.0 - a * cw;
        const double di = a * sw;
        const double mag2 = dr * dr + di * di;

        if (mag2 <= 0.0)
        {
            re = 1.0;
            im = 0.0;
            return;
        }

        const double scale = (1.0 - a) / mag2;
        re =  scale * dr;
        im = -scale * di;
    };

    double lpRe = 0.0, lpIm = 0.0;

    // Low shelf: 1 - cLow * LP
    lowpass (spec.coeffLow, lpRe, lpIm);

    const double cLow = 1.0 - spec.gainLow;
    const double loRe = 1.0 - cLow * lpRe;
    const double loIm =     - cLow * lpIm;

    // High shelf: gainHigh + cHigh * LP
    lowpass (spec.coeffHigh, lpRe, lpIm);

    const double cHigh = 1.0 - spec.gainHigh;
    const double hiRe = spec.gainHigh + cHigh * lpRe;
    const double hiIm =                 cHigh * lpIm;

    return std::sqrt ((loRe * loRe + loIm * loIm) * (hiRe * hiRe + hiIm * hiIm));
}

// ===========================================================================
//  Mid-band anchoring
// ===========================================================================
/*
    The mid band is unity BY CONSTRUCTION only in the limit of well-separated
    corners. In practice each shelf has begun to act a little by the time the
    other has finished, so the true mid-band gain sits slightly below 1.

    Measured from the closed form at the shipping defaults, the residual is
    about -0.032 dB per pass. That sounds negligible and is not: at 38 passes
    per second against a 24 dB/s baseline it makes the mid band decay about
    5 % short — well outside the +/-2 % this phase promises. It is the same
    class of error as the 1.7 % allpass overshoot corrected in PHASE 5C.

    Removing it is nearly free, because the residual is available in closed
    form: divide the per-line decay gain by |H_i| at a mid reference frequency.
    The maximum of the combined per-pass response then returns to g_i, so the
    stability bound is unchanged while the mid band becomes exact.

    Two guards on the division, both necessary:

      - a ceiling on the compensation, so the overlap region (where there is no
        plateau to anchor and |H_mid| can be genuinely small) cannot demand an
        unbounded gain increase;
      - the existing min(maxFeedbackGain, ...) clamp applied AFTER the
        division, which is what keeps g_i * max|H| < 1 unconditionally.

    Where the clamp engages, mid-band RT60 drifts. That happens only in the
    overlap region, which is exactly where the +/-2 % criterion is documented
    not to apply, and in the extreme-Decay corner where headroom under
    maxFeedbackGain is thin by design.
*/

/** Ceiling on the mid-band compensation factor. */
inline constexpr double maxPlateauCompensation = 2.0;

/**
    Reference frequency the mid band is anchored at: the geometric mean of the
    two corners, which is the middle of the plateau on a log axis and degrades
    gracefully into the overlap region. At the shipping defaults (200 Hz,
    6 kHz) this is 1095 Hz, a hair from the 1 kHz the exit criterion measures.
*/
[[nodiscard]] inline double midReferenceHz (double lowCornerHz, double highCornerHz) noexcept
{
    const double lo = std::max (lowCornerHz, minCutoffHz);
    const double hi = std::max (highCornerHz, lo);

    return std::clamp (std::sqrt (lo * hi), 125.0, 8000.0);
}

/** The mid-band gain to divide out. Exactly 1.0 when damping is transparent. */
[[nodiscard]] inline double plateauGain (const ShelfSpec& spec,
                                         double lowCornerHz, double highCornerHz,
                                         double sampleRate) noexcept
{
    if (spec.isTransparent())
        return 1.0;

    return magnitudeAt (spec, midReferenceHz (lowCornerHz, highCornerHz), sampleRate);
}

/** Compensation factor for one line, ceiling applied. */
[[nodiscard]] inline double plateauCompensation (const ShelfSpec& spec,
                                                 double lowCornerHz, double highCornerHz,
                                                 double sampleRate) noexcept
{
    const double p = plateauGain (spec, lowCornerHz, highCornerHz, sampleRate);

    if (p >= 1.0)
        return 1.0;

    return std::min (maxPlateauCompensation, 1.0 / std::max (p, 1.0e-9));
}

// ===========================================================================
//  Smoothing
// ===========================================================================

/** Log-domain glide for the two corner frequencies. Control rate. */
inline constexpr double frequencyGlideSeconds = 0.05;

} // namespace av::dsp::damping
