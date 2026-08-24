#pragma once

#include <algorithm>
#include <cmath>

#include "FdnTuning.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    Makes Size a change of space rather than a change of level.

    WHY THERE IS A LEVEL DIFFERENCE AT ALL
    --------------------------------------
    PHASE 4 established that a larger tank at the same RT60 really is quieter,
    and that this is physics rather than a bug. The reason is visible in the
    decay maths. A line of delay d with feedback gain g driven by noise of power
    P settles at a stored power of

        P / (1 - g^2)

    and the RT60 formula fixes g = 10^(-3 d / RT60), so

        g = exp(-3 ln10 * d / RT60)  ~  1 - 3 ln10 * d / RT60
        1 - g^2                      ~  6 ln10 * d / RT60

    for the small exponents a reverb actually uses. The steady-state power is
    therefore proportional to RT60 / d, and the level to sqrt(RT60 / d). Scaling
    every delay by the Size control scales d, so

        level(s) / level(1) = 1 / sqrt(s)      ->   -10 * log10(s) dB

    Two things follow, and both are worth stating because they decide the shape
    of this whole module. First, the correction is a function of Size alone: the
    RT60 term is common to every Size and cancels out of the ratio, so a
    two-dimensional Size-by-Decay surface should not be needed. Second, it is a
    closed form rather than a table, so it needs no calibration data and cannot
    go stale when the delay set or the sample rate changes.

    Both are predictions, not assumptions. SizeCompensationTests.cpp measures the
    real curve across Size, Decay, modulation mode, modulation depth and sample
    rate, and the residual table below carries whatever the closed form does not
    account for.

    WHERE IT IS APPLIED
    -------------------
    Outside the feedback loop, after the stereo readout, before the wet output.
    Nothing here touches a feedback gain, the tank state, the injection vectors
    or the readout vectors: a compensation that reached inside the loop would
    change the decay it was supposed to leave alone, and the RT60 correction in
    FdnCore would then be compensating for the compensation.

    Real-time safe. No allocation, no branching on parameter values in the audio
    path, and a smoother so that a Size change moves the gain continuously.
*/
namespace sizeComp
{

/**
    WHAT THE MEASUREMENT SAID
    -------------------------
    The closed form above is close but not close enough, and the reason it fails
    is instructive. The step from g to 1 - g^2 was linearised, which is fine
    while 3 ln10 * d / RT60 is small — and it stops being small at short decays,
    where g moves well away from 1. Measured against -10 log10(s) the error
    reaches 0.69 dB at the ends of the Size range, and, worse, it is not the
    same error at every decay: the correction Size 0.25 needs moves by 2.46 dB
    between a half-second decay and a ten-second one.

    That settles the question this phase was told to decide by measurement. The
    rule was a 1D Size table if the correction varies by less than 0.5 dB across
    Decay and a 2D surface otherwise, and 2.46 dB is not less than 0.5. So the
    table below is two-dimensional.

    A second measured dependence: engaging the allpass bank shifts the
    correction by up to 0.58 dB, and the shift is on Size rather than on depth
    — depth 0.35 and depth 1.00 differ by 0.06 dB. That is exactly what the
    loop-delay model predicts. The sections add a fixed number of samples to
    each loop, so at Size 0.25, where the delay lines are four times shorter, the
    same addition is four times the proportion. It is therefore modelled as a
    one-dimensional additive term in Size, switched by whether the bank is
    engaged, rather than as a third table axis. The rule was to model the effect
    separately once it exceeded 0.25 dB, not to add a dimension for it.

    All figures come from "Size / does the correction depend on anything but
    Size" in the test suite, which re-measures them and fails if the table has
    gone stale.
*/
inline constexpr int sizePoints  = 9;
inline constexpr int decayPoints = 10;

inline constexpr double tableSizes[sizePoints]
{
    0.25, 0.35, 0.50, 0.70, 1.00, 1.30, 1.60, 1.80, 2.00
};

/*
    The decay grid.

    PHASE 5C calibrated over 0.5 to 10 s, while the Decay parameter runs 0.1 to
    60 s. Everything outside that span was being clamped to the nearest measured
    row, and the accuracy there was an assumption rather than a measurement.

    The extension was asked to cover 0.3 to 100 s. Measuring the clamped region
    showed that was not far enough at the short end: clamping 0.1 s to the 0.3 s
    row is wrong by 6.96 dB, and 0.2 s by 1.70 dB, over a region the Decay
    control reaches easily. So the grid starts at the parameter's own minimum of
    0.1 s. Above 100 s the surface has stopped moving — clamping 150 s costs
    0.19 dB and 300 s costs 0.03 dB — so the top of the grid stays at 100 s.
*/
inline constexpr double tableDecays[decayPoints]
{
    0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 4.0, 10.0, 30.0, 100.0
};

/** Correction in dB, [decay][size], with the allpass bank bypassed. */
inline constexpr double tableDb[decayPoints][sizePoints]
{
    { -16.34, -13.48, -9.92, -5.86, 0.00, 5.33, 10.54, 14.02, 17.23 }, // 0.1 s
    { -11.08, -8.94, -6.40, -3.73, 0.00, 3.16, 6.18, 8.17, 9.91 },  //   0.2 s
    { -9.38, -7.49, -5.28, -3.04, 0.00, 2.45, 4.73, 6.24, 7.53 },   //   0.3 s
    { -8.05, -6.35, -4.41, -2.49, 0.00, 1.90, 3.57, 4.69, 5.67 },   //   0.5 s
    { -7.06, -5.47, -3.74, -2.06, 0.00, 1.51, 2.72, 3.53, 4.34 },   //   1.0 s
    { -6.57, -5.02, -3.38, -1.83, 0.00, 1.34, 2.35, 2.96, 3.70 },   //   2.0 s
    { -6.33, -4.78, -3.19, -1.71, 0.00, 1.26, 2.20, 2.71, 3.37 },   //   4.0 s
    { -6.16, -4.61, -3.06, -1.59, 0.00, 1.24, 2.15, 2.63, 3.19 },   //  10.0 s
    { -6.02, -4.55, -2.94, -1.59, 0.00, 1.27, 2.13, 2.60, 3.13 },   //  30.0 s
    { -6.20, -4.81, -3.09, -1.57, 0.00, 1.17, 1.92, 2.47, 3.03 },   // 100.0 s
};

/*
    What the extension changed.

    The 0.3 s row is 1.33 dB away from the 0.5 s row at Size 0.25 — the surface
    steepens sharply as the decay shortens, which is exactly where the
    linearisation behind the closed form gives out. Clamping 0.3 s to the 0.5 s
    row, which is what PHASE 5C did, was therefore wrong by more than a decibel
    over a region the Decay parameter reaches easily.

    At the other end the surface has almost stopped moving: 30 s and 100 s agree
    with 10 s to within about 0.2 dB. Clamping above 100 s costs little, and the
    rows are kept anyway so the claim is measured rather than argued.
*/

/** Added when the allpass bank is engaged, in dB, by Size. */
inline constexpr double engagedDeltaDb[sizePoints]
{
    0.58, 0.24, 0.21, 0.15, 0.00, 0.00, 0.13, 0.17, 0.01
};

/** Index and fraction for a value inside a monotone grid. */
struct GridPosition { int lower = 0; int upper = 0; double t = 0.0; };

[[nodiscard]] inline GridPosition locate (double value, const double* grid, int count) noexcept
{
    GridPosition p;

    if (value <= grid[0])
        return { 0, 0, 0.0 };

    if (value >= grid[count - 1])
        return { count - 1, count - 1, 0.0 };

    for (int i = 1; i < count; ++i)
    {
        if (value <= grid[i])
        {
            p.lower = i - 1;
            p.upper = i;

            // Interpolated in log, because both axes are ratios: halfway
            // between Size 0.5 and Size 1.0 is 0.707, not 0.75, and the
            // correction is far closer to linear against log Size than
            // against Size.
            const double lo = std::log (grid[i - 1]);
            const double hi = std::log (grid[i]);

            p.t = hi > lo ? (std::log (value) - lo) / (hi - lo) : 0.0;
            break;
        }
    }

    return p;
}

/**
    Never boost or cut by more than this.

    A compensation that can apply arbitrary gain is a fault waiting for an
    unusual parameter combination, and the measured surface does contain one.
    At a 0.1 s decay with Size 2.0 the table asks for +17.23 dB, because that
    configuration is degenerate: the longest delay line is 166 ms, longer than
    the whole RT60, so the tank barely circulates and what comes out is nearly
    dry and very sparse. Boosting that by 17 dB would be amplifying an artefact.

    The limit is kept at 12 dB and the clamp is reported rather than hidden.
    Everything at 0.2 s and longer is inside it — the largest correction there
    is +9.91 dB — so the clamp only bites in the corner where the parameter
    combination has stopped describing a reverb.
*/
inline constexpr double maxCorrectionDb = 12.0;

/** True where the table asks for more gain than the limit allows. */
[[nodiscard]] inline bool correctionIsClamped (double sizeScale, double rt60Seconds,
                                               bool allpassEngaged) noexcept;

/**
    Compensation gain, in dB, for a Size scale at a decay.

    Bilinear over the measured surface in log Size and log Decay, clamped at the
    edges rather than extrapolated: outside the calibrated range the honest
    answer is the nearest measured one, not a straight line drawn into territory
    nobody measured.
*/
[[nodiscard]] inline double correctionDb (double sizeScale, double rt60Seconds,
                                          bool allpassEngaged) noexcept
{
    const double s = std::clamp (sizeScale, fdn::minSizeScale, fdn::maxSizeScale);
    const double t = std::clamp (rt60Seconds, tableDecays[0], tableDecays[decayPoints - 1]);

    const auto sp = locate (s, tableSizes, sizePoints);
    const auto dp = locate (t, tableDecays, decayPoints);

    const double lowerRow = tableDb[dp.lower][sp.lower]
                          + sp.t * (tableDb[dp.lower][sp.upper] - tableDb[dp.lower][sp.lower]);

    const double upperRow = tableDb[dp.upper][sp.lower]
                          + sp.t * (tableDb[dp.upper][sp.upper] - tableDb[dp.upper][sp.lower]);

    double db = lowerRow + dp.t * (upperRow - lowerRow);

    if (allpassEngaged)
        db += engagedDeltaDb[sp.lower]
              + sp.t * (engagedDeltaDb[sp.upper] - engagedDeltaDb[sp.lower]);

    return std::clamp (db, -maxCorrectionDb, maxCorrectionDb);
}

/** The uncapped table value, so the clamp can be detected. */
[[nodiscard]] inline double uncappedCorrectionDb (double sizeScale, double rt60Seconds,
                                                  bool allpassEngaged) noexcept
{
    const double s = std::clamp (sizeScale, fdn::minSizeScale, fdn::maxSizeScale);
    const double t = std::clamp (rt60Seconds, tableDecays[0], tableDecays[decayPoints - 1]);

    const auto sp = locate (s, tableSizes, sizePoints);
    const auto dp = locate (t, tableDecays, decayPoints);

    const double lowerRow = tableDb[dp.lower][sp.lower]
                          + sp.t * (tableDb[dp.lower][sp.upper] - tableDb[dp.lower][sp.lower]);

    const double upperRow = tableDb[dp.upper][sp.lower]
                          + sp.t * (tableDb[dp.upper][sp.upper] - tableDb[dp.upper][sp.lower]);

    double db = lowerRow + dp.t * (upperRow - lowerRow);

    if (allpassEngaged)
        db += engagedDeltaDb[sp.lower]
              + sp.t * (engagedDeltaDb[sp.upper] - engagedDeltaDb[sp.lower]);

    return db;
}

[[nodiscard]] inline bool correctionIsClamped (double sizeScale, double rt60Seconds,
                                               bool allpassEngaged) noexcept
{
    return std::abs (uncappedCorrectionDb (sizeScale, rt60Seconds, allpassEngaged))
           > maxCorrectionDb;
}

/**
    Whether a decay is inside the calibrated span.

    Outside it the correction is clamped to the nearest measured row rather than
    extrapolated, which is the honest thing to do but is not the same as being
    accurate there. Callers that report accuracy — the diagnostic layer, in
    particular — need to be able to tell the difference rather than presenting a
    clamped value as a measured one.
*/
[[nodiscard]] inline bool isCalibrated (double rt60Seconds) noexcept
{
    return rt60Seconds >= tableDecays[0] && rt60Seconds <= tableDecays[decayPoints - 1];
}

/** Compensation as a linear gain. */
[[nodiscard]] inline double correctionGain (double sizeScale, double rt60Seconds,
                                            bool allpassEngaged) noexcept
{
    return std::pow (10.0, correctionDb (sizeScale, rt60Seconds, allpassEngaged) / 20.0);
}

/** How long the gain takes to reach a new Size's value. */
inline constexpr double smoothingSeconds = 0.05;

} // namespace sizeComp

/**
    The compensation gain as a smoothed, applicable object.

    Held separately from the tank so that a dual-bank morph can give each bank
    its own value: during a crossfade the two banks are different sizes and a
    single shared gain would be wrong for both.
*/
class SizeLevelCompensation
{
public:
    void prepare (double sampleRate)
    {
        gain.prepare (sampleRate, sizeComp::smoothingSeconds);
        gain.snapTo (targetGain());
    }

    /** Sets the Size this compensation is for. Real-time safe. */
    void setSize (double sizeScale, bool immediate = false) noexcept
    {
        currentSize = sizeScale;
        apply (immediate);
    }

    /** The decay the tank is set to. The correction depends on it — measured. */
    void setRt60Seconds (double seconds, bool immediate = false) noexcept
    {
        currentRt60 = seconds;
        apply (immediate);
    }

    /** Whether the allpass bank is in the loop, which lengthens it. */
    void setAllpassEngaged (bool engaged, bool immediate = false) noexcept
    {
        allpassEngaged = engaged;
        apply (immediate);
    }

    /** Turns the compensation off. Bypassed is exactly unity, not nearly. */
    void setEnabled (bool shouldBeEnabled, bool immediate = false) noexcept
    {
        enabled = shouldBeEnabled;
        apply (immediate);
    }

    [[nodiscard]] bool isEnabled() const noexcept { return enabled; }

    void reset() noexcept { gain.snapToTarget(); }

    /** Applies one sample's worth of gain to a stereo pair. */
    void process (double& l, double& r) noexcept
    {
        const double g = gain.getNextValue();
        l *= g;
        r *= g;
    }

    /** The gain without advancing the smoother. For tests and for the morph. */
    [[nodiscard]] double getCurrentGain() const noexcept { return gain.getCurrentValue(); }
    [[nodiscard]] double getTargetGain()  const noexcept { return gain.getTargetValue(); }
    [[nodiscard]] double getSize()        const noexcept { return currentSize; }

    /** Advances the smoother and returns the gain, without applying it. */
    [[nodiscard]] double nextGain() noexcept { return gain.getNextValue(); }

private:
    [[nodiscard]] double targetGain() const noexcept
    {
        return enabled ? sizeComp::correctionGain (currentSize, currentRt60, allpassEngaged) : 1.0;
    }

    void apply (bool immediate) noexcept
    {
        if (immediate) gain.snapTo (targetGain());
        else           gain.setTarget (targetGain());
    }

    LinearSmoother<double> gain;

    double currentSize = 1.0;
    double currentRt60 = 2.0;
    bool   allpassEngaged = false;
    bool   enabled = true;
};

} // namespace av::dsp
