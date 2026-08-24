#pragma once

#include <algorithm>
#include <cmath>

#include "AllpassModulationTuning.h"
#include "DampingTuning.h"
#include "FdnTuning.h"

namespace av::dsp::mapping
{

/**
    Turns the user's controls into the tank's settings.

    Kept here, free of JUCE, for two reasons. It is the part of the parameter
    handling that has real content — a curve chosen so that the control's
    default position lands on the setting the measurements chose — and that
    content deserves tests. And a mapping that lived inside the processor could
    only be checked by loading a plugin host, which is not where a curve should
    be debugged.

    Every function here is a pure function of its argument: same input, same
    output, no state, no rounding that depends on what came before. That is what
    makes parameter automation reproducible and a saved session restore to the
    same sound.
*/

// ===========================================================================
//  Size
// ===========================================================================
/*
    Size is a 0..1 control and the tank wants a delay scale.

    The mapping is geometric rather than linear, because scaling delays is a
    multiplicative operation: going from 0.25 to 0.5 is the same musical step as
    going from 1.0 to 2.0, and a linear control would spend most of its travel
    in the large half of the range. Geometric spacing gives equal ratio per unit
    of travel, which is also what -10 log10(s) makes the level do — the Size
    compensation and the Size control then move at proportional rates.
*/

[[nodiscard]] inline double sizeScaleFor (double zeroToOne) noexcept
{
    const double u = std::clamp (zeroToOne, 0.0, 1.0);

    return fdn::minSizeScale * std::pow (fdn::maxSizeScale / fdn::minSizeScale, u);
}

/** Inverse of sizeScaleFor, for tests and for display. */
[[nodiscard]] inline double sizeControlFor (double scale) noexcept
{
    const double s = std::clamp (scale, fdn::minSizeScale, fdn::maxSizeScale);

    return std::log (s / fdn::minSizeScale) / std::log (fdn::maxSizeScale / fdn::minSizeScale);
}

// ===========================================================================
//  Decay
// ===========================================================================

[[nodiscard]] inline double rt60For (double seconds) noexcept
{
    return std::clamp (seconds, fdn::minRt60Seconds, fdn::maxRt60Seconds);
}

// ===========================================================================
//  Mod Depth
// ===========================================================================
/*
    The Mod Depth control is a percentage and defaults to 30 %. The architecture
    measured 0.35 as its default depth. Rather than move either number, the
    curve is chosen so that the control's existing default position produces the
    measured default setting:

        depth(u) = u^k,    with k chosen so that depth(0.30) = 0.35

    k = ln(0.35) / ln(0.30) = 0.8719. The curve is monotone, continuous, reaches
    exactly 0 at 0 and exactly 1 at 1, and gives the full research range at the
    top of the control rather than capping the user at the shipped default.

    Depth exactly 0 has to reach a true bypass, and u^k is exactly 0 at u = 0
    for any positive k, so no special case is needed.
*/

inline constexpr double depthControlDefault = 0.30;
inline constexpr double depthArchitectureDefault = allpassMod::defaultDepth;

/** ln(0.35) / ln(0.30), evaluated once. */
inline const double depthExponent =
    std::log (depthArchitectureDefault) / std::log (depthControlDefault);

[[nodiscard]] inline double modDepthFor (double zeroToOne) noexcept
{
    const double u = std::clamp (zeroToOne, 0.0, 1.0);

    if (u <= 0.0)
        return 0.0;

    return std::pow (u, depthExponent);
}

// ===========================================================================
//  Mod Rate
// ===========================================================================
/*
    The Mod Rate control runs 0.01 to 10 Hz and defaults to 0.5 Hz, while the
    architecture's measured default is 0.20 Hz and its usable range is 0.02 to
    2.0 Hz — above about 0.4 Hz spectral concentration starts collapsing and the
    reverb becomes a chorus.

    The control is therefore treated as a scale rather than as a frequency: its
    default position means "the rate the measurements chose", and moving it
    scales that proportionally. The result is clamped to the range the engine
    was measured over, so the top of the control is bounded by evidence rather
    than by the widest number the parameter range happens to allow.

    Reporting the clamp rather than hiding it: at the top of its travel the
    control reaches 2.0 Hz and stops, so the last part of the sweep does
    nothing. That is deliberate. The alternative is letting a control produce
    settings no measurement supports.
*/

inline constexpr double rateControlDefaultHz = 0.5;

[[nodiscard]] inline double modRateScaleFor (double controlHz) noexcept
{
    return std::max (0.0, controlHz) / rateControlDefaultHz;
}

[[nodiscard]] inline double allpassRateFor (double controlHz) noexcept
{
    const double scaled = allpassMod::defaultRateHz * modRateScaleFor (controlHz);

    return std::clamp (scaled, allpassMod::minRateHz, allpassMod::maxRateHz);
}

/** True where the control has run into the measured limit. */
[[nodiscard]] inline bool rateIsClamped (double controlHz) noexcept
{
    const double scaled = allpassMod::defaultRateHz * modRateScaleFor (controlHz);

    return scaled < allpassMod::minRateHz || scaled > allpassMod::maxRateHz;
}

// ===========================================================================
//  Damping (PHASE 7)
// ===========================================================================
/*
    The two Damp controls are decay ratios, not attenuations. The mapping and
    the reasoning behind it live in DampingTuning.h; these two wrappers exist
    so the processor reads the same way for damping as it does for every other
    parameter, and so the control-to-physics conversion has exactly one home.
*/

/** Low Damp 0..1 -> RT60_low / RT60_mid. */
[[nodiscard]] inline double lowDampRatioFor (double zeroToOne) noexcept
{
    return damping::decayRatioFor (zeroToOne, damping::lowRatioAtFull);
}

/** High Damp 0..1 -> RT60_high / RT60_mid. */
[[nodiscard]] inline double highDampRatioFor (double zeroToOne) noexcept
{
    return damping::decayRatioFor (zeroToOne, damping::highRatioAtFull);
}

// ===========================================================================
//  Preview wet trim
// ===========================================================================
/*
    A reverb tank driven by a sustained source integrates energy, and how much
    it integrates depends on the decay. A line of delay d with feedback gain g
    settles at a stored power of P / (1 - g^2), so the tank's steady-state gain
    over its input is

        sum over lines of 1 / (1 - g_i^2),      g_i = 10^(-3 d_i / RT60)

    which grows without bound as RT60 does. At the default 2.5 s decay the
    preview sat comfortably below full scale; at 20 s with sustained full-scale
    noise it reached 2.18, and a full-scale DC input reached 8.0. A fixed trim
    cannot cover both without making normal settings far too quiet — the span
    between them is about 18 dB.

    So the trim is not fixed. It is the reciprocal of the square root of that
    sum, normalised so that the reference setting is unity, which cancels the
    build-up the decay causes and leaves Decay controlling decay rather than
    doubling as a volume control.

    This is gain staging, not limiting. Nothing here looks at the signal or
    reacts to it: the value depends only on parameters, it is computed when they
    change rather than per sample, and it is exactly reversible. A peak that
    still exceeds full scale after this is a real one and will show up in the
    measurements rather than being flattened out of them.

    The paragraph above describes what this normalisation does about *decay*. It
    does nothing about input level or Size, and PHASE 5F / FU-2 found that no
    fixed value can: the ceiling set by musical material and the ceiling set by
    sustained full-scale stress are about 23 dB apart. See previewBaseTrim below
    for which end of that span the product sits at, and why.
*/

/** Steady-state power gain of the tank, per unit input power. */
[[nodiscard]] inline double tankPowerGain (double rt60Seconds, double sizeScale) noexcept
{
    const double rt60 = rt60For (rt60Seconds);
    const double scale = std::clamp (sizeScale, fdn::minSizeScale, fdn::maxSizeScale);

    double total = 0.0;

    for (int i = 0; i < fdn::order; ++i)
    {
        const double delaySeconds = fdn::delaysMs[i] * scale * 0.001;
        const double g = std::min (fdn::maxFeedbackGain,
                                   fdn::feedbackGainForRt60 (delaySeconds, rt60));

        total += 1.0 / std::max (1.0e-6, 1.0 - g * g);
    }

    return total;
}

/** Decay and Size the reference trim is defined at. */
inline constexpr double previewReferenceRt60 = 2.5;
inline constexpr double previewReferenceSize = 1.0;

/**
    Fixed part of the preview trim. Unity: there is no base attenuation.

    It used to be 0.25, documented as the headroom the worst sustained
    full-scale case needs. PHASE 5F / FU-2 measured that claim and it did not
    hold. Against the worst sustained tone found by a third-octave scan — 254 Hz,
    Size 100%, Decay 60 s — the wet path reaches an effective true peak of 6.38
    before this trim, so 0.25 left it about 4 dB *above* full scale. The value
    protected against nothing at that case while costing 22.6 dB on material a
    user actually sends: measured across four sources, the worst musical peak sat
    22.6 dB below full scale, and at Mix 100% the reverb landed 14.7 to 19.5 LU
    under its own dry signal.

    So the base attenuation is removed and the decay-aware normalisation below is
    kept. That normalisation is the part that was doing real work — it cancels
    the build-up decay causes, so Decay controls decay rather than doubling as a
    volume control — and it is unchanged.

    The consequence, stated rather than left to be discovered: an extreme
    pathological input (sustained full scale, maximum Size, long Decay) can drive
    the wet output past unity, by about 16 dB at the worst measured point. That
    is a deliberate product decision, not an oversight. Correct staging for
    normal material is preferred, and no guarantee of wet <= 1.0 is offered for
    that class of input. The value that would offer it is 0.157 — quieter than
    the 0.25 this replaces, and further into the level problem FU-2 exists to fix.

    Nothing signal-dependent is applied to bound it: no limiter, no clipper, no
    saturator, no auto-gain. A limiter would mean every later measurement of
    RT60, modal behaviour and level described the limiter rather than the reverb.
    Peaks above unity are carried, reported by the measurement suites, and left
    for the user's fader.

    See "Preview / gain safety" and "FU-2 / headroom and stress" in the test
    suite, and Docs/Phase5F-FU2-Decision.md for the blind listening result that
    chose this value: it won 7 of 8 groups, and all 4 at the default Mix 30%.
*/
inline constexpr double previewBaseTrim = 1.0;

/** The whole preview wet trim for a decay and Size. */
[[nodiscard]] inline double previewWetTrim (double rt60Seconds, double sizeScale) noexcept
{
    const double reference = tankPowerGain (previewReferenceRt60, previewReferenceSize);
    const double current   = tankPowerGain (rt60Seconds, sizeScale);

    if (current <= 0.0 || reference <= 0.0)
        return previewBaseTrim;

    return previewBaseTrim * std::sqrt (reference / current);
}


// ===========================================================================
//  Wet / dry crossfade
// ===========================================================================
/*
    The shipping mix law, chosen in PHASE 5F by measurement and then by ear.

        dry = cos(m * pi/2)
        wet = m^0.799604

    WHY NOT LINEAR, WHICH THIS REPLACED
    -----------------------------------
    dry = 1 - m, wet = m is constant-amplitude, which is right when the two
    signals are correlated. A reverb's wet output is not correlated with its
    dry input, so the sum loses energy: at the shipping default of 30% the dry
    signal alone was down 3.1 dB, and the measured loudness drop across four
    sources was 3.0 to 3.2 LU. In a blind comparison the linear law came last
    on all four, and every "this one sounds quieter" note pointed at it.

    WHY NOT EQUAL POWER
    -------------------
    dry = cos, wet = sin fixes the loudness - it is within 0.03 LU of this
    curve - but sin(0.3 pi/2) = 0.454 against linear's 0.300, so the same knob
    position gives about 19% more reverb. That is a second change riding along
    with the level fix: the default preset would sound wetter as well as
    louder. In the blind test it lost three of four, and the one comment it
    drew on percussive material was that it was too much.

    WHY THIS EXPONENT
    -----------------
    0.799604 is not a taste setting. It is the value that makes the wet/dry
    RATIO at the 30% default equal the linear law's, so switching to this curve
    changes the level and nothing else:

        m^k / cos(m pi/2) = m / (1 - m)   at m = 0.30
        k = ln(0.3/0.7 * cos(0.3 pi/2)) / ln(0.3) = 0.799604

    ENDPOINTS AND SHAPE
    -------------------
    Exact at both ends by construction: cos(0) = 1 and 0^k = 0; cos(pi/2) = 0
    and 1^k = 1. Both curves are monotone in m, so a fader move cannot reverse
    direction, and both are continuous, so automation cannot step.

    The wet curve is steeper than linear near zero - m^0.8 rises faster than m
    - so the first few percent of the control do more than they used to. That
    is measured rather than assumed: see the low-mix tests, which check the
    per-sample step a smoothed sweep produces against the linear law's.
*/

inline constexpr double mixWetExponent = 0.799604;

struct MixCoefficients { double dry = 1.0; double wet = 0.0; };

[[nodiscard]] inline MixCoefficients mixCoefficientsFor (double mix) noexcept
{
    const double m = std::clamp (mix, 0.0, 1.0);

    // The endpoints are pinned rather than left to the arithmetic. cos and pow
    // are correctly rounded here on every platform this builds for, but a mix
    // control parked at 0 must pass the dry signal *exactly*, not to within an
    // ulp, and the same at the other end.
    if (m <= 0.0) return { 1.0, 0.0 };
    if (m >= 1.0) return { 0.0, 1.0 };

    return { std::cos (m * 1.5707963267948966), std::pow (m, mixWetExponent) };
}
} // namespace av::dsp::mapping
