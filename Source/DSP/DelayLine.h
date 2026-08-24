#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Interpolation.h"

namespace av::dsp
{

/**
    Fixed-capacity fractional delay line.

    DESIGN
    ------
    - One contiguous ring buffer, allocated once in prepare(). Nothing in the
      process path allocates, locks, throws or branches on capacity.
    - The buffer is rounded up to a power of two so wrapping is a single AND.
      The waste is bounded by 2x on a structure that is at most a few hundred
      milliseconds long, which is a good trade for removing a conditional from
      the innermost loop of every delay tap in the reverb.
    - write() and read() are separate. A delay line is almost never read once:
      early reflection networks and feedback delay networks read the same line
      at many taps, and forcing a single read per write would mean duplicating
      the storage per tap.

    INDEXING
    --------
    write() stores at writeIndex and then advances it, so after a write the most
    recent sample sits at writeIndex - 1. A delay of d means "d samples older
    than the most recent sample"; a delay of 0 returns the sample just written.

    FRACTIONAL READS
    ----------------
    A 4-point kernel needs one sample newer than the integer tap and two older,
    so interpolated reads are clamped to [1, maxDelaySamples]. The buffer is
    sized with three extra samples of headroom so the oldest tap a kernel can
    reach is always valid rather than aliasing onto the newest sample.

    Delay times are requested in seconds wherever the caller has a choice, and
    converted here, so the same code produces the same delay at every supported
    sample rate.
*/
template <typename SampleType = double>
class DelayLine
{
public:
    /** Shortest delay an interpolated read may request, in samples. */
    static constexpr double minInterpolatedDelay = 1.0;

    DelayLine() = default;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    /** Allocates storage for a delay of up to maxDelaySeconds at sampleRate. */
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        const auto samples = static_cast<int> (std::ceil (maxDelaySeconds * sampleRate));
        prepareSamples (samples);
        currentSampleRate = sampleRate;
    }

    /** Allocates storage for a delay of up to maxDelay samples. */
    void prepareSamples (int maxDelay)
    {
        maxDelaySamples = std::max (1, maxDelay);

        // + 3 so the oldest tap a 4-point kernel reaches (maxDelay + 2) is
        // still distinct from the newest sample.
        bufferSize = nextPowerOfTwo (maxDelaySamples + 4);
        mask       = bufferSize - 1;

        buffer.assign (static_cast<size_t> (bufferSize), SampleType (0));
        writeIndex = 0;
    }

    /** Clears the delay line without reallocating. Real-time safe. */
    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), SampleType (0));
        writeIndex = 0;
    }

    [[nodiscard]] bool isPrepared() const noexcept { return bufferSize > 1; }
    [[nodiscard]] int  getMaxDelaySamples() const noexcept { return maxDelaySamples; }
    [[nodiscard]] int  getBufferSize() const noexcept { return bufferSize; }
    [[nodiscard]] double getSampleRate() const noexcept { return currentSampleRate; }

    /** Longest delay expressible in seconds at the prepared sample rate. */
    [[nodiscard]] double getMaxDelaySeconds() const noexcept
    {
        return currentSampleRate > 0.0 ? maxDelaySamples / currentSampleRate : 0.0;
    }

    /** Converts seconds to samples at the prepared sample rate. */
    [[nodiscard]] double secondsToSamples (double seconds) const noexcept
    {
        return seconds * currentSampleRate;
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Pushes one sample and advances the write position. */
    void write (SampleType x) noexcept
    {
        buffer[static_cast<size_t> (writeIndex)] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Reads at an exact integer delay. A delay of 0 is the sample just written. */
    [[nodiscard]] SampleType readAt (int delaySamples) const noexcept
    {
        const int i = (writeIndex - 1 - delaySamples + bufferSize) & mask;
        return buffer[static_cast<size_t> (i)];
    }

    /**
        Reads at a fractional delay using the kernel chosen at compile time.

        The requested delay is clamped to the line's valid range; a caller that
        needs to know whether clamping occurred should compare against
        getMaxDelaySamples() itself.
    */
    template <Interpolation mode>
    [[nodiscard]] SampleType read (double delaySamples) const noexcept
    {
        if constexpr (mode == Interpolation::none)
        {
            const auto d = clampDelay (delaySamples, 0.0);
            return readAt (static_cast<int> (d));
        }
        else
        {
            const auto d = clampDelay (delaySamples, minInterpolatedDelay);
            const int  i = static_cast<int> (d);
            const auto t = static_cast<SampleType> (d - i);

            if constexpr (mode == Interpolation::linear)
            {
                return interpolateLinear (readAt (i), readAt (i + 1), t);
            }
            else if constexpr (mode == Interpolation::hermite)
            {
                return interpolateHermite (readAt (i - 1), readAt (i),
                                           readAt (i + 1), readAt (i + 2), t);
            }
            else
            {
                return interpolateLagrange3 (readAt (i - 1), readAt (i),
                                             readAt (i + 1), readAt (i + 2), t);
            }
        }
    }

    /** Convenience overload with the kernel chosen at run time. */
    [[nodiscard]] SampleType read (double delaySamples, Interpolation mode) const noexcept
    {
        switch (mode)
        {
            case Interpolation::none:      return read<Interpolation::none>      (delaySamples);
            case Interpolation::linear:    return read<Interpolation::linear>    (delaySamples);
            case Interpolation::hermite:   return read<Interpolation::hermite>   (delaySamples);
            case Interpolation::lagrange3: return read<Interpolation::lagrange3> (delaySamples);
        }

        return read<Interpolation::hermite> (delaySamples);
    }

    /** Default read: 3rd-order Lagrange. See Interpolation.h for the evidence. */
    [[nodiscard]] SampleType read (double delaySamples) const noexcept
    {
        return read<Interpolation::lagrange3> (delaySamples);
    }

private:
    [[nodiscard]] double clampDelay (double d, double lowerLimit) const noexcept
    {
        return std::clamp (d, lowerLimit, static_cast<double> (maxDelaySamples));
    }

    [[nodiscard]] static int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;

        return p;
    }

    std::vector<SampleType> buffer;
    int    bufferSize        = 0;
    int    mask              = 0;
    int    writeIndex        = 0;
    int    maxDelaySamples   = 0;
    double currentSampleRate = 0.0;
};

} // namespace av::dsp
