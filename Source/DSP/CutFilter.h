#pragma once

#include <algorithm>
#include <cmath>

#include "DampingTuning.h"

namespace av::dsp
{

/**
    PHASE 8. Low Cut and High Cut on the wet path, before the pre-delay.

    WHAT THESE ARE, AND WHAT THEY ARE NOT
    -------------------------------------
    These filters sit OUTSIDE the feedback loop, on the signal that excites
    the tank. They change how much energy enters a band. They do not change
    how long a band takes to decay - that is PHASE 7 damping, which lives
    inside the loop. The two are not redundant and the difference is
    measurable rather than rhetorical:

        High Damp   RT60(f) changes            progressively darker tail
        High Cut    RT60(f) unchanged          uniformly darker tail

    A test asserts exactly that: sweeping either cut must leave RT60(f) within
    1 % while band energy follows the filter's magnitude. If a cut moves
    RT60(f), it has leaked into the loop.

    FILTER FORM
    -----------
    Both are first order, built on the same impulse-invariant one-pole as
    DampingTuning.h, for the reasons argued there: a = exp(-2 pi f / fs) lies
    in (0,1) for every f > 0 at every sample rate, so there is no coefficient
    region to exclude and no pre-warping blow-up near Nyquist; and a one-pole
    has no resonant memory, so a coefficient step produces no transient. That
    last property is what makes cutoff automation click-free by construction
    rather than by smoothing hard enough, and it is why the corner frequencies
    are updated at control rate and applied directly.

        LP(z) = (1 - a) / (1 - a z^-1)

        High Cut:  y = LP(x)
        Low Cut:   y = (x - LP(x)) * k

    LP is exactly 1 at DC, so High Cut needs no correction there.

    THE LOW-CUT NORMALISATION k
    ---------------------------
    x - LP(x) has the transfer function a(1 - z^-1)/(1 - a z^-1). Its
    magnitude rises monotonically from exactly 0 at DC to 2a/(1+a) at Nyquist
    - NOT to 1. Without correction the passband of a filter whose passband is
    supposed to be transparent droops: 0.11 dB at a 200 Hz corner, but 0.64 dB
    at the 1000 Hz top of the shipped range. A broadband 0.64 dB is louder
    than most things this project measures to three decimal places, so it is
    corrected rather than tolerated:

        k = (1 + a) / (2a)

    This is one multiply by a precomputed constant. Because the uncorrected
    magnitude is monotone, dividing by its maximum leaves |H| <= 1 everywhere,
    so the correction cannot make the filter clip. Over the shipped 20-1000 Hz
    range k stays within [1.000, 1.077].

    MONOTONICITY, for the record: |H|^2 = a^2 (2 - 2c) / (1 - 2ac + a^2) with
    c = cos w. d/dc of that is -2(1-a)^2 / (...)^2 < 0, so |H| increases with
    w for every a in (0,1). The maximum is therefore at Nyquist, always.

    NEUTRAL BYPASS
    --------------
    Following LineDamping, each filter is bypassed on a flag at its neutral
    endpoint rather than run with coefficients that happen to be nearly
    transparent. At Low Cut 20 Hz and High Cut 20 kHz the sample is passed
    through untouched, so the PHASE 7 signal path is reproduced bit for bit.

    The bypass decision is taken from the SETTLED TARGET, not the smoothed
    value. LogSmoother recovers its linear value with exp(log(x)), which need
    not return x bit-exactly, so an equality test on the smoothed value could
    miss the endpoint forever. The target comes straight from the parameter
    and is exact.

    There is a step at each endpoint, because the endpoint means OFF. It is
    measured and reported rather than morphed away.
*/

/** The neutral endpoints. These MUST match ParameterLayout's minima/maxima. */
inline constexpr double lowCutOffHz  = 20.0;
inline constexpr double highCutOffHz = 20000.0;

/**
    One first-order cut pair for one channel.

    Stereo is two of these driven from one set of coefficients, so the two
    channels cannot drift apart.
*/
class CutFilterState
{
public:
    void reset() noexcept
    {
        lowState  = 0.0;
        highState = 0.0;
    }

    /** Low cut, high cut, in that order. Coefficients come from the owner. */
    [[nodiscard]] double process (double x,
                                  bool lowActive,  double aLow,  double kLow,
                                  bool highActive, double aHigh) noexcept
    {
        if (lowActive)
        {
            lowState += (1.0 - aLow) * (x - lowState);
            x = (x - lowState) * kLow;
        }

        if (highActive)
        {
            highState += (1.0 - aHigh) * (x - highState);
            x = highState;
        }

        return x;
    }

private:
    double lowState  = 0.0;
    double highState = 0.0;
};

/**
    The stereo pair, with its coefficients.

    Corner frequencies are set at control rate - once per chunk - and applied
    directly. See the class comment for why that is safe for a one-pole.
*/
class StereoCutFilters
{
public:
    void prepare (double sampleRate) noexcept
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        setLowCut  (lowCutOffHz,  true);
        setHighCut (highCutOffHz, true);

        reset();
    }

    void reset() noexcept
    {
        left .reset();
        right.reset();
    }

    /**
        `settledAtEndpoint` is the caller's answer to "has the user parked this
        control on OFF?", taken from the smoother's target rather than its
        current value.
    */
    void setLowCut (double hz, bool settledAtEndpoint) noexcept
    {
        lowActive = ! (settledAtEndpoint && hz <= lowCutOffHz);

        if (! lowActive)
            return;

        aLow = damping::onePoleCoefficient (hz, currentSampleRate);

        // k = (1+a)/(2a). Clamped only so that the class stays well behaved if
        // a future range ever puts the corner near Nyquist, where a becomes
        // small; across the shipped 20-1000 Hz range k never exceeds 1.08.
        kLow = (aLow > 1.0e-6) ? std::min ((1.0 + aLow) / (2.0 * aLow), 2.0) : 1.0;
    }

    void setHighCut (double hz, bool settledAtEndpoint) noexcept
    {
        highActive = ! (settledAtEndpoint && hz >= highCutOffHz);

        if (highActive)
            aHigh = damping::onePoleCoefficient (hz, currentSampleRate);
    }

    /** True when both filters are bypassed and the stage is an identity. */
    [[nodiscard]] bool isBypassed() const noexcept { return ! lowActive && ! highActive; }

    [[nodiscard]] bool isLowActive()  const noexcept { return lowActive; }
    [[nodiscard]] bool isHighActive() const noexcept { return highActive; }
    [[nodiscard]] double getLowCoefficient()  const noexcept { return aLow; }
    [[nodiscard]] double getHighCoefficient() const noexcept { return aHigh; }
    [[nodiscard]] double getLowNormalisation() const noexcept { return kLow; }

    void process (double* l, double* r, int numSamples) noexcept
    {
        if (isBypassed())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            l[i] = left .process (l[i], lowActive, aLow, kLow, highActive, aHigh);
            r[i] = right.process (r[i], lowActive, aLow, kLow, highActive, aHigh);
        }
    }

private:
    CutFilterState left, right;

    double currentSampleRate = 48000.0;
    double aLow  = 0.0;
    double aHigh = 0.0;
    double kLow  = 1.0;

    bool lowActive  = false;
    bool highActive = false;
};

} // namespace av::dsp
