#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "DelayLine.h"
#include "Denormal.h"
#include "Interpolation.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    Schroeder allpass section — the unit cell of every diffusion stage.

    ---------------------------------------------------------------------------
    THE EQUATION
    ---------------------------------------------------------------------------

    With a loop delay of M samples and a coefficient g:

        v[n] = x[n] + g * v[n-M]
        y[n] = v[n-M] - g * v[n]

    which is the single-delay-line form of

                 -g + z^-M
        H(z) = --------------
                1 - g * z^-M

    Substituting z = e^(jw) and taking the magnitude:

        |H| = |(-g + e^(-jwM))| / |(1 - g*e^(-jwM))|

    Numerator and denominator are complex conjugates scaled by the same factor
    for real g, so |H(e^jw)| = 1 at every frequency. The section changes only
    phase. That is the whole point: it smears a transient into a sequence of
    echoes without altering the spectrum.

    Impulse response, which the tests check exactly:

        h[0]   = -g
        h[M]   = 1 - g^2
        h[2M]  = g   * (1 - g^2)
        h[kM]  = g^(k-1) * (1 - g^2)      for k >= 1

    Total energy  =  g^2 + (1-g^2)^2 * sum_k g^(2k)
                  =  g^2 + (1-g^2)^2 / (1-g^2)
                  =  g^2 + 1 - g^2  =  1.

    ---------------------------------------------------------------------------
    STABILITY
    ---------------------------------------------------------------------------

    The poles sit at |z| = |g|^(1/M), so the section is stable for |g| < 1 and
    marginally stable (a lossless, forever-ringing comb) at |g| = 1. Two things
    make the practical limit lower than the theoretical one:

      1. The read position is fractional, so the loop contains an interpolation
         filter as well as a delay. The loop gain is g * I(e^jw), and a kernel
         whose magnitude exceeds unity anywhere pushes |loop gain| above |g|.
         Lagrange3 never exceeds unity and is therefore the safe default here;
         Hermite peaks slightly below Nyquist. AllpassDiffuserTests measures
         both inside the loop rather than trusting the open-loop result.

      2. The decay time of the section is M / -ln|g| samples. As |g| approaches
         1 that grows without bound, and long before instability the section
         stops sounding like diffusion and starts sounding like a tuned
         resonator. The audible limit arrives well before the numerical one.

    `stabilityLimit` is the hard clamp; `recommendedMaxCoefficient` is the
    musically useful ceiling. Both are justified by measurement in
    Tests/AllpassDiffuserTests.cpp rather than assumed.

    ---------------------------------------------------------------------------
    WHY THE LOOP DELAY IS ROUNDED BY DEFAULT
    ---------------------------------------------------------------------------

    Reading the loop at a fractional position puts an interpolation filter
    inside the recursion, and that breaks the allpass property outright. The
    numerator becomes -g + I(z)z^-M instead of -g + z^-M, so wherever |I| falls
    to g and the phase lines up, the numerator vanishes and the response
    notches. Measured at M = 100.37, g = 0.7 with the project's default kernel:

        quantisation   impulse energy   worst dip   worst rise
        rounded        1.000000         0.00 dB     0.00 dB
        fractional     0.78             38 dB       1.1 dB

    A fifth of the energy is gone, concentrated at the top of the band, and it
    compounds once per stage in a cascade. No interpolation kernel fixes this;
    it is a property of putting any lowpass inside a lossless loop.

    So the loop delay is rounded to a whole sample by default, which makes the
    section exactly allpass again. Three things make that a cheap price:

      - The diffuser's delays are structural, not modulated. Rounding 1.13 ms to
        54 samples at 48 kHz is a 0.4 % error, far inside the tolerance the
        delay set is tuned to.
      - The rounding still tracks the sample rate, so the character survives a
        rate change.
      - A delay glide under rounding moves the read position one sample at a
        time, and a one-sample delay change moves the output by exactly the
        signal's own sample-to-sample difference. It cannot produce a step
        larger than the signal is already making, which is what "click-free"
        means. This is measured, not assumed.

    Quantisation::fractional is kept because a modulated allpass in a later
    phase may want it, and because having both is how the measurement above
    stays honest.

    ---------------------------------------------------------------------------
    REAL-TIME BEHAVIOUR
    ---------------------------------------------------------------------------

    - No allocation, locking or exceptions after prepare().
    - Delay time and coefficient are smoothed, so automation cannot click.
      Delay uses a one-pole (its derivative is a pitch shift, so a linear ramp's
      corners would tick); the coefficient uses a linear ramp (it is a gain-like
      quantity and benefits from arriving exactly on target).
    - The recursive state carries an alternating-sign denormal guard, so a
      silent input cannot drive the section into subnormal arithmetic and cannot
      leave a DC offset behind either.
    - Processing is strictly sample-by-sample with no per-block storage, so any
      block size works, including one larger than the size announced to
      prepare().
*/
template <typename SampleType = double, Interpolation kernel = Interpolation::lagrange3>
class AllpassDiffuser
{
public:
    /** How the loop delay is resolved. See the note above for the measurements. */
    enum class Quantisation
    {
        rounded,    /**< Nearest whole sample. Exactly allpass. The default. */
        fractional  /**< Interpolated. Smooth in time, coloured in frequency. */
    };

    /** The loop delay is read at M-1, and an interpolated read needs at least
        one sample of history, so the shortest usable loop delay is 2. */
    static constexpr double minLoopDelaySamples = 2.0;

    /** Hard clamp. Above this the section is a resonator, not a diffuser. */
    static constexpr double stabilityLimit = 0.95;

    /** Ceiling used by the diffusion mapping. Set by measurement. */
    static constexpr double recommendedMaxCoefficient = 0.75;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate, double maxDelaySeconds,
                  double smoothingSeconds = 0.02)
    {
        currentSampleRate = sampleRate;
        maxDelay = std::max (minLoopDelaySamples, maxDelaySeconds * sampleRate);

        // + 2 so the read at M-1 plus the kernel's trailing taps always fit.
        line.prepareSamples (static_cast<int> (std::ceil (maxDelay)) + 2);

        delaySmoother.prepare (sampleRate, smoothingSeconds);
        coeffSmoother.prepare (sampleRate, smoothingSeconds);

        delaySmoother.snapTo (clampDelay (delaySmoother.getTargetValue()));
        coeffSmoother.snapTo (clampCoefficient (coeffSmoother.getTargetValue()));

        guard.reset();
        line.reset();
    }

    /** Clears the section without reallocating. Real-time safe. */
    void reset() noexcept
    {
        line.reset();
        guard.reset();
        delaySmoother.snapToTarget();
        coeffSmoother.snapToTarget();
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setDelaySamples (double samples, bool immediate = false) noexcept
    {
        const double clamped = clampDelay (samples);

        if (immediate) delaySmoother.snapTo (clamped);
        else           delaySmoother.setTarget (clamped);
    }

    void setDelaySeconds (double seconds, bool immediate = false) noexcept
    {
        setDelaySamples (seconds * currentSampleRate, immediate);
    }

    void setDelayMilliseconds (double ms, bool immediate = false) noexcept
    {
        setDelaySamples (ms * 0.001 * currentSampleRate, immediate);
    }

    /** Signed: a negative coefficient is the same diffusion with inverted phase. */
    void setCoefficient (double g, bool immediate = false) noexcept
    {
        const double clamped = clampCoefficient (g);

        if (immediate) coeffSmoother.snapTo (clamped);
        else           coeffSmoother.setTarget (clamped);
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    void setQuantisation (Quantisation q) noexcept { quantisation = q; }
    [[nodiscard]] Quantisation getQuantisation() const noexcept { return quantisation; }

    [[nodiscard]] SampleType processSample (SampleType x) noexcept
    {
        const double m = delaySmoother.getNextValue();
        const double g = coeffSmoother.getNextValue();

        // v[n-M]. The most recent stored value is v[n-1], so a loop delay of M
        // is a read at M-1.
        const double vDelayed = quantisation == Quantisation::rounded
            ? static_cast<double> (line.readAt (static_cast<int> (m + 0.5) - 1))
            : static_cast<double> (line.template read<kernel> (m - 1.0));

        const double v = static_cast<double> (x) + g * vDelayed + guard.next();

        line.write (static_cast<SampleType> (v));

        return static_cast<SampleType> (vDelayed - g * v);
    }

    void processBlock (SampleType* data, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            data[n] = processSample (data[n]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] double getDelaySamples()     const noexcept { return delaySmoother.getCurrentValue(); }
    [[nodiscard]] double getTargetDelaySamples() const noexcept { return delaySmoother.getTargetValue(); }
    [[nodiscard]] double getCoefficient()      const noexcept { return coeffSmoother.getCurrentValue(); }
    [[nodiscard]] double getMaxDelaySamples()  const noexcept { return maxDelay; }
    [[nodiscard]] double getSampleRate()       const noexcept { return currentSampleRate; }
    [[nodiscard]] bool   isSmoothing()         const noexcept
    {
        return delaySmoother.isSmoothing() || coeffSmoother.isSmoothing();
    }

    /** Time for the section's ringing to fall by 60 dB, in samples. */
    [[nodiscard]] double getDecayTimeSamples() const noexcept
    {
        const double g = std::abs (coeffSmoother.getCurrentValue());

        if (g <= 0.0)
            return 0.0;

        if (g >= 1.0)
            return std::numeric_limits<double>::infinity();

        return delaySmoother.getCurrentValue() * std::log (0.001) / std::log (g);
    }

private:
    [[nodiscard]] double clampDelay (double d) const noexcept
    {
        return std::clamp (d, minLoopDelaySamples, maxDelay);
    }

    [[nodiscard]] static double clampCoefficient (double g) noexcept
    {
        return std::clamp (g, -stabilityLimit, stabilityLimit);
    }

    DelayLine<SampleType> line;

    OnePoleSmoother<double> delaySmoother;
    LinearSmoother<double>  coeffSmoother;

    AlternatingGuard guard;

    Quantisation quantisation = Quantisation::rounded;

    double currentSampleRate = 48000.0;
    double maxDelay          = minLoopDelaySamples;
};

} // namespace av::dsp
