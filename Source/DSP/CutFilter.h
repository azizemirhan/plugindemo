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
    the tank. They change how much energy enters a band. PHASE 7 damping
    lives inside the loop and changes how long a band takes to decay. The two
    are not redundant, and the difference is structural:

        High Damp   enters the recurrence      progressively darker tail
        High Cut    does not                   uniformly darker tail

    STATED PRECISELY, because STAGE 2 stated it too strongly and STAGE 2.5
    had to correct it.

    The cuts are EXCITATION filters. Their parameters do not enter any tank
    control value - not RT60, not the damping ratios or shelf coefficients,
    not the feedback gains, not the delay lengths, not the modulation depth or
    rate - and therefore appear nowhere in the tank's recurrence equations.
    They cannot change the decay law. Two tests prove it on the complete
    production tank at the shipped Mod Depth: every readable tank control
    value is identical across a full cut sweep, and the signal reaching
    processBlock replays bit-identically into a tank that was never told a cut
    parameter.

    They CAN change modal excitation weighting, the energy observed in a band,
    what a finite-band RT60 estimator reports, and - the tank being modulated,
    hence time-varying - the time-varying weighting of what reaches the
    output. The STAGE 2 harness measured the third of those at up to 9 %.

    So the claim is NOT "a cut can never change a measured RT60". That is too
    strong for a finite, modulated, modal system, and it is not what the
    architecture needs to be true.

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

    PRIMING ON RE-ENTRY
    -------------------
    A bypassed filter's state is neither advanced nor cleared - process()
    returns before touching it. On its own that means a filter parked on OFF
    for two seconds comes back carrying the sample history of two seconds ago,
    and computes its first output from it. STAGE 2.5 measured that, by running
    the same signal and the same automation twice with different histories
    before the bypass: with the state left alone the two outputs differed by
    6.9 % of peak at re-entry, and every one of the 9600 samples after it
    differed. Nothing about a signal from before a bypass should be audible
    after it, so the first sample of each active period PRIMES the state:

        high cut   s = x            (a s + (1-a) x = x exactly)
        low cut    s = x (a - 1/k) / a

    Both solve "make this sample pass through unchanged", which is what the
    filter would have done with it had it been running all along, and which
    depends only on the sample in hand. The audio after a bypass is then
    bit-identical whatever happened before it - a test asserts exactly that -
    and the first sample is continuous with the bypassed signal rather than
    stepping away from it, so the OFF boundary cannot click.

    What priming does NOT do, and is not meant to: remove the SETTLING that
    follows. A low cut entering on a bass-heavy signal still takes its time
    constant to remove the lows. That is the filter doing its job, it is
    smooth by construction, and it is what a tonal change across OFF sounds
    like.

    This costs one branch per block, not per sample, and it cannot touch the
    neutral path: a bypassed stage never reaches the loop at all.
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

    /**
        Low cut, high cut, in that order. Coefficients come from the owner.

        `primeLow` and `primeHigh` are set for the first sample after a filter
        comes back from bypass. See PRIMING ON RE-ENTRY in the class comment:
        each sets the state so that this sample passes through unchanged, which
        is what a filter that had been running all along would have done with
        it, and which carries nothing over from before the bypass.
    */
    [[nodiscard]] double process (double x,
                                  bool lowActive,  double aLow,  double kLow,
                                  bool highActive, double aHigh,
                                  bool primeLow = false,
                                  bool primeHigh = false) noexcept
    {
        if (lowActive)
        {
            // Solve y = x for the state that the update below will produce:
            //   s' = a s + (1-a) x,  y = (x - s') k  =>  s' = x (1 - 1/k)
            //   =>  s = x (a - 1/k) / a
            if (primeLow)
                lowState = (aLow > 1.0e-12 && kLow > 0.0)
                           ? x * (aLow - 1.0 / kLow) / aLow
                           : 0.0;

            lowState += (1.0 - aLow) * (x - lowState);
            x = (x - lowState) * kLow;
        }

        if (highActive)
        {
            // s' = a s + (1-a) x = x exactly when s = x. The high cut's input
            // is the low cut's OUTPUT, which is why priming happens here and
            // not from the caller's buffer.
            if (primeHigh)
                highState = x;

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

        primeLowPending  = false;
        primeHighPending = false;
    }

    /**
        `settledAtEndpoint` is the caller's answer to "has the user parked this
        control on OFF?", taken from the smoother's target rather than its
        current value.
    */
    void setLowCut (double hz, bool settledAtEndpoint) noexcept
    {
        const bool wasActive = lowActive;

        lowActive = ! (settledAtEndpoint && hz <= lowCutOffHz);

        if (! lowActive)
            return;

        if (! wasActive)
            primeLowPending = true;

        aLow = damping::onePoleCoefficient (hz, currentSampleRate);

        // k = (1+a)/(2a). Clamped only so that the class stays well behaved if
        // a future range ever puts the corner near Nyquist, where a becomes
        // small; across the shipped 20-1000 Hz range k never exceeds 1.08.
        kLow = (aLow > 1.0e-6) ? std::min ((1.0 + aLow) / (2.0 * aLow), 2.0) : 1.0;
    }

    void setHighCut (double hz, bool settledAtEndpoint) noexcept
    {
        const bool wasActive = highActive;

        highActive = ! (settledAtEndpoint && hz >= highCutOffHz);

        if (! highActive)
            return;

        if (! wasActive)
            primeHighPending = true;

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

        int i = 0;

        // The priming sample, taken out of the loop so that the loop itself
        // passes compile-time false and is exactly what it was before.
        if ((primeLowPending || primeHighPending) && numSamples > 0)
        {
            l[0] = left .process (l[0], lowActive, aLow, kLow, highActive, aHigh,
                                  primeLowPending, primeHighPending);
            r[0] = right.process (r[0], lowActive, aLow, kLow, highActive, aHigh,
                                  primeLowPending, primeHighPending);

            primeLowPending  = false;
            primeHighPending = false;

            i = 1;
        }

        for (; i < numSamples; ++i)
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

    // Set on a bypass -> active edge, consumed by the next sample processed.
    bool primeLowPending  = false;
    bool primeHighPending = false;
};

} // namespace av::dsp
