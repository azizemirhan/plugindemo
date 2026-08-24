#pragma once

#include <algorithm>

#include "DelayLine.h"

namespace av::dsp
{

/**
    One delay line of a feedback delay network.

    INTEGER DELAYS ONLY
    -------------------
    This is deliberately not a fractional delay, and the reason is measured
    rather than stylistic. PHASE 3 established what an interpolator does when it
    sits inside a feedback loop: at M = 100.37 samples and a loop gain of 0.7,
    a Lagrange-interpolated allpass lost 21.8 % of its impulse energy and
    notched its response by 38 dB, while the same structure with a rounded delay
    was flat to 3.3e-13 dB. An FDN loop has exactly the same topology and the
    same exposure, with three additional consequences that matter more here:

      - The interpolator's rolloff makes the loop gain frequency dependent, so
        high frequencies decay faster than low ones. That is a damping filter
        nobody asked for, and it would arrive before the real one in PHASE 7.
      - It makes the measured RT60 depend on frequency, which would corrupt the
        decay-accuracy measurements this phase exists to establish.
      - It couples decay to Size, because the fractional part of each delay
        changes as the delays are scaled.

    So the delays are whole samples. The cost is that a delay length can only
    move in one-sample steps, which is why Size changes go through a dual-bank
    crossfade rather than being applied to a running tank.

    The line is a thin wrapper over the PHASE 2 ring buffer: same power-of-two
    masking, same allocation-free process path, with the fractional read paths
    deliberately not exposed.
*/
class FdnDelayLine
{
public:
    /** Reserves storage for a delay of up to `capacity` samples. Allocates. */
    void prepare (int capacity)
    {
        capacitySamples = std::max (1, capacity);
        line.prepareSamples (capacitySamples + 1);
        delaySamples = std::min (delaySamples, capacitySamples);
        delaySamples = std::max (1, delaySamples);
    }

    /** Sets the delay. Returns false, changing nothing, if it does not fit. */
    [[nodiscard]] bool setDelaySamples (int samples) noexcept
    {
        if (samples < 1 || samples > capacitySamples)
            return false;

        delaySamples = samples;
        return true;
    }

    void reset() noexcept { line.reset(); }

    void write (double x) noexcept { line.write (x); }

    /** The value written `delaySamples` samples ago. Call before write(). */
    [[nodiscard]] double read() const noexcept { return line.readAt (delaySamples - 1); }

    [[nodiscard]] int getDelaySamples()    const noexcept { return delaySamples; }
    [[nodiscard]] int getCapacitySamples() const noexcept { return capacitySamples; }
    [[nodiscard]] bool isPrepared()        const noexcept { return line.isPrepared(); }

private:
    DelayLine<double> line;
    int delaySamples    = 1;
    int capacitySamples = 1;
};

} // namespace av::dsp
