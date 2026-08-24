#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "DelayLine.h"
#include "Interpolation.h"
#include "Lfo.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    A delay line whose read position is continuously modulated.

    This is the building block every algorithm in the bank will use to keep long
    tails from turning into a comb filter. It is intentionally a thin composition
    of DelayLine + Lfo + OnePoleSmoother rather than a monolith, so an algorithm
    that needs a different modulation source can reuse the rest.

    THE PITCH PROBLEM
    -----------------
    Moving a delay tap resamples the signal passing through it. The instantaneous
    pitch ratio is 1 - d(delay)/dn, so modulation depth and rate are not free
    parameters: their product sets how far the tail detunes. A lush hall wants
    detuning below roughly 3-5 cents; past about 10 cents it stops sounding like
    air movement and starts sounding like a chorus pedal, which is exactly the
    artefact the reverb is trying to avoid.

    peakPitchDeviationCents() makes that budget explicit and checkable, so the
    algorithms in PHASE 9 onwards can choose depth and rate against a number
    instead of by ear alone.

    CLICK-FREE DELAY CHANGES
    ------------------------
    The base delay is smoothed with a one-pole rather than a linear ramp. A ramp
    has a discontinuous derivative at its start and end, and since the derivative
    is the pitch shift, a ramp produces two audible ticks of detune per move. The
    one-pole's derivative is continuous, so a base-delay change is heard as a
    smooth glide. Where a *pitch-free* change is required — pre-delay, algorithm
    size — the correct tool is a crossfade between two taps, which is PHASE 3.

    Real-time safe after prepare(). No allocation, locking or exceptions in the
    process path.
*/
template <typename SampleType = double, Interpolation kernel = Interpolation::lagrange3>
class ModulatedDelay
{
public:
    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate, double maxDelaySeconds,
                  double delaySmoothingSeconds = 0.02)
    {
        currentSampleRate = sampleRate;

        line.prepare (sampleRate, maxDelaySeconds);
        lfo.prepare (sampleRate);

        smoothingSeconds = delaySmoothingSeconds;
        baseDelay.prepare (sampleRate, smoothingSeconds);
        depth.prepare (sampleRate, smoothingSeconds);

        baseDelay.snapTo (std::clamp (baseDelay.getTargetValue(),
                                      DelayLine<SampleType>::minInterpolatedDelay,
                                      static_cast<double> (line.getMaxDelaySamples())));
        depth.snapTo (depth.getTargetValue());

        currentDelay = baseDelay.getCurrentValue();
    }

    /** Clears the delay memory and restarts the LFO. Real-time safe. */
    void reset() noexcept
    {
        line.reset();
        lfo.reset();
        baseDelay.snapToTarget();
        depth.snapToTarget();
        currentDelay = baseDelay.getCurrentValue();
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setBaseDelaySamples (double samples, bool immediate = false) noexcept
    {
        const auto clamped = std::clamp (samples,
                                         DelayLine<SampleType>::minInterpolatedDelay,
                                         static_cast<double> (line.getMaxDelaySamples()));

        if (immediate) baseDelay.snapTo (clamped);
        else           baseDelay.setTarget (clamped);
    }

    void setBaseDelaySeconds (double seconds, bool immediate = false) noexcept
    {
        setBaseDelaySamples (seconds * currentSampleRate, immediate);
    }

    void setDepthSamples (double samples, bool immediate = false) noexcept
    {
        const auto clamped = std::max (0.0, samples);

        if (immediate) depth.snapTo (clamped);
        else           depth.setTarget (clamped);
    }

    void setDepthSeconds (double seconds, bool immediate = false) noexcept
    {
        setDepthSamples (seconds * currentSampleRate, immediate);
    }

    void setRateHz (double hz)        noexcept { lfo.setRateHz (hz); }
    void setShape (LfoShape s)        noexcept { lfo.setShape (s); }
    void setPhaseOffset (double c)    noexcept { lfo.setPhaseOffset (c); }
    void setSeed (uint32_t s)         noexcept { lfo.setSeed (s); }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Writes one sample, then reads at the current modulated position. */
    [[nodiscard]] SampleType processSample (SampleType input) noexcept
    {
        line.write (input);
        return readModulated();
    }

    /** Writes without reading, for lines driven by an external tap schedule. */
    void write (SampleType input) noexcept { line.write (input); }

    /** Advances the modulation by one sample and reads. Does not write. */
    [[nodiscard]] SampleType readModulated() noexcept
    {
        const double base = baseDelay.getNextValue();
        const double dep  = depth.getNextValue();
        const double mod  = lfo.getNextValue();

        currentDelay = std::clamp (base + dep * mod,
                                   DelayLine<SampleType>::minInterpolatedDelay,
                                   static_cast<double> (line.getMaxDelaySamples()));

        return line.template read<kernel> (currentDelay);
    }

    /** Reads at a fixed extra delay without touching the modulation state. */
    [[nodiscard]] SampleType readStatic (double delaySamples) const noexcept
    {
        return line.template read<kernel> (delaySamples);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] double getCurrentDelaySamples() const noexcept { return currentDelay; }
    [[nodiscard]] double getBaseDelaySamples()    const noexcept { return baseDelay.getCurrentValue(); }
    [[nodiscard]] double getDepthSamples()        const noexcept { return depth.getCurrentValue(); }
    [[nodiscard]] double getSampleRate()          const noexcept { return currentSampleRate; }

    [[nodiscard]] const DelayLine<SampleType>& getDelayLine() const noexcept { return line; }
    [[nodiscard]] DelayLine<SampleType>&       getDelayLine()       noexcept { return line; }

    /**
        Largest depth, in samples, that the current base delay can carry without
        the modulated position hitting either end of the line. Exceeding it does
        not misbehave — the read is clamped — but the clamping flattens the
        modulation peaks, which is audible as distortion of the movement.
    */
    [[nodiscard]] double getMaxSafeDepthSamples() const noexcept
    {
        const double base = baseDelay.getTargetValue();
        return std::max (0.0, std::min (base - DelayLine<SampleType>::minInterpolatedDelay,
                                        static_cast<double> (line.getMaxDelaySamples()) - base));
    }

    /**
        Peak pitch deviation, in cents, produced by the given modulation.

        The instantaneous pitch ratio of a moving tap is 1 - d(delay)/dn. The
        peak of |d(delay)/dn| over one cycle is depth * rate / sampleRate scaled
        by a shape-dependent factor:

            sine          2*pi   (exact)
            triangle      4      (exact; constant between turnarounds)
            smoothRandom  3      (upper bound on the Catmull-Rom slope for
                                  successive random points in [-1, 1])

        The value returned is the sharp-side deviation, which is the larger of
        the two in cents.
    */
    [[nodiscard]] static double peakPitchDeviationCents (double depthSamples,
                                                         double rateHz,
                                                         double sampleRate,
                                                         LfoShape shape = LfoShape::sine) noexcept
    {
        if (sampleRate <= 0.0 || depthSamples <= 0.0 || rateHz <= 0.0)
            return 0.0;

        const double factor = slopeFactor (shape);
        const double ratio  = 1.0 + (factor * depthSamples * rateHz / sampleRate);

        return 1200.0 * std::log2 (ratio);
    }

    /** Inverse of the above: the depth that hits a given cents budget. */
    [[nodiscard]] static double depthForPitchDeviation (double cents,
                                                        double rateHz,
                                                        double sampleRate,
                                                        LfoShape shape = LfoShape::sine) noexcept
    {
        if (sampleRate <= 0.0 || rateHz <= 0.0 || cents <= 0.0)
            return 0.0;

        const double ratio = std::pow (2.0, cents / 1200.0);
        return (ratio - 1.0) * sampleRate / (slopeFactor (shape) * rateHz);
    }

private:
    [[nodiscard]] static constexpr double slopeFactor (LfoShape shape) noexcept
    {
        switch (shape)
        {
            case LfoShape::sine:         return 6.283185307179586476;
            case LfoShape::triangle:     return 4.0;
            case LfoShape::smoothRandom: return 3.0;
        }

        return 6.283185307179586476;
    }

    DelayLine<SampleType> line;
    Lfo lfo;

    OnePoleSmoother<double> baseDelay;
    OnePoleSmoother<double> depth;

    double currentSampleRate = 48000.0;
    double smoothingSeconds  = 0.02;
    double currentDelay      = 1.0;
};

} // namespace av::dsp
