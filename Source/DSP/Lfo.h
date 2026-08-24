#pragma once

#include <cmath>
#include <cstdint>

#include "Interpolation.h"

namespace av::dsp
{

/** Modulation waveform. The full modulation engine (PHASE 7) builds on these. */
enum class LfoShape
{
    sine,          /**< Smooth, one dominant modulation frequency. */
    triangle,      /**< Constant modulation velocity, so constant pitch offset. */
    smoothRandom   /**< Band-limited noise. Breaks up periodic colouration. */
};

/**
    Low-frequency oscillator for delay-line modulation.

    All shapes return values in [-1, 1] and are continuous in value; sine and
    smoothRandom are also continuous in slope, which matters because the slope
    of the modulation *is* the pitch shift applied to the delayed signal
    (see ModulatedDelay::peakPitchDeviationCents). Triangle has a slope
    discontinuity at its peaks — a deliberate property, since constant slope
    means a constant, non-warbling detune between turnarounds.

    smoothRandom draws a new random value once per cycle and Catmull-Rom
    interpolates between the last four, so the output has no steps and no
    corners. The generator is a seeded xorshift: two delay lines given different
    seeds decorrelate, and every run is reproducible for testing.

    Real-time safe after prepare(). No allocation, no locking, no branches on
    rate other than the phase wrap.
*/
class Lfo
{
public:
    void prepare (double sampleRate) noexcept
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        setRateHz (rateHz);
        reset();
    }

    /** Restarts the waveform. Keeps rate, shape, phase offset and seed. */
    void reset() noexcept
    {
        phase = 0.0;
        rngState = seed != 0 ? seed : 0x9E3779B9u;

        for (auto& v : randomPoints)
            v = nextRandomBipolar();
    }

    void setShape (LfoShape s) noexcept { shape = s; }

    void setRateHz (double hz) noexcept
    {
        rateHz    = hz < 0.0 ? 0.0 : hz;
        increment = rateHz / currentSampleRate;
    }

    /** Static offset added to the read phase, in cycles. Periodic shapes only. */
    void setPhaseOffset (double cycles) noexcept
    {
        phaseOffset = cycles - std::floor (cycles);
    }

    /** Decorrelates smoothRandom between instances. Takes effect on reset(). */
    void setSeed (uint32_t s) noexcept { seed = s; }

    [[nodiscard]] LfoShape getShape()  const noexcept { return shape; }
    [[nodiscard]] double   getRateHz() const noexcept { return rateHz; }
    [[nodiscard]] double   getPhase()  const noexcept { return phase; }

    /** Advances one sample and returns the new value in [-1, 1]. */
    [[nodiscard]] double getNextValue() noexcept
    {
        phase += increment;

        while (phase >= 1.0)
        {
            phase -= 1.0;

            // A cycle boundary is where smoothRandom draws its next target.
            randomPoints[0] = randomPoints[1];
            randomPoints[1] = randomPoints[2];
            randomPoints[2] = randomPoints[3];
            randomPoints[3] = nextRandomBipolar();
        }

        return currentValue = evaluate();
    }

    [[nodiscard]] double getCurrentValue() const noexcept { return currentValue; }

    /**
        Advances the phase by one sample without evaluating the waveform.

        For a bypassed modulation lane. Evaluating a sine costs a trigonometric
        call whose result is about to be multiplied by zero, but the phase still
        has to move so that coming out of bypass does not jump. This keeps the
        state identical to what getNextValue() would have left, minus the
        returned value.
    */
    void skipSample() noexcept
    {
        phase += increment;

        while (phase >= 1.0)
        {
            phase -= 1.0;

            randomPoints[0] = randomPoints[1];
            randomPoints[1] = randomPoints[2];
            randomPoints[2] = randomPoints[3];
            randomPoints[3] = nextRandomBipolar();
        }
    }

private:
    [[nodiscard]] double evaluate() const noexcept
    {
        switch (shape)
        {
            case LfoShape::sine:
            {
                const double p = wrapped (phase + phaseOffset);
                return std::sin (6.283185307179586476 * p);
            }

            case LfoShape::triangle:
            {
                // Phase-aligned with sine: 0 at p = 0, +1 at 0.25, -1 at 0.75.
                const double p = wrapped (phase + phaseOffset);

                if (p < 0.25) return 4.0 * p;
                if (p < 0.75) return 2.0 - 4.0 * p;
                return 4.0 * p - 4.0;
            }

            case LfoShape::smoothRandom:
            default:
                return interpolateHermite (randomPoints[0], randomPoints[1],
                                           randomPoints[2], randomPoints[3], phase);
        }
    }

    [[nodiscard]] static double wrapped (double p) noexcept
    {
        return p - std::floor (p);
    }

    [[nodiscard]] double nextRandomBipolar() noexcept
    {
        // xorshift32 — cheap, deterministic, long enough period for modulation.
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;

        return (static_cast<double> (rngState) * (2.0 / 4294967296.0)) - 1.0;
    }

    LfoShape shape = LfoShape::sine;

    double currentSampleRate = 48000.0;
    double rateHz            = 1.0;
    double increment         = 1.0 / 48000.0;
    double phase             = 0.0;
    double phaseOffset       = 0.0;
    double currentValue      = 0.0;

    uint32_t seed     = 0x9E3779B9u;
    uint32_t rngState = 0x9E3779B9u;

    double randomPoints[4] { 0.0, 0.0, 0.0, 0.0 };
};

} // namespace av::dsp
