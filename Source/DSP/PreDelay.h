#pragma once

#include <algorithm>
#include <cmath>

#include "DelayLine.h"

namespace av::dsp
{

/**
    PHASE 8. Pre-delay on the wet path, after the cut filters.

    WHOLE SAMPLES, ON PURPOSE
    -------------------------
    The tap is an integer number of samples and the fractional interpolator is
    never used. One sample is 20.8 us at 48 kHz; nobody sets a pre-delay to a
    precision of twenty microseconds, and quantising buys three things that
    matter more than a resolution nobody wants:

      1. Zero means zero. DelayLine::readAt(0) returns the sample just
         written, so 0 ms is an exact identity. The interpolated read is
         clamped to >= 1 sample - a 4-point kernel needs a newer neighbour -
         so a fractional path CANNOT reach zero, and reaching for it would
         have produced a silent one-sample delay on the wet path.
      2. Timing is exact and testable. An impulse comes out at
         round(ms * fs / 1000) samples, with nothing to interpret.
      3. No interpolation filtering. A 4-point kernel is a mild lowpass; the
         pre-delay should not quietly darken the wet path.

    MOVING THE TAP
    --------------
    Not by sliding it. The derivative of a delay time IS the pitch shift
    applied to whatever is passing through it, so gliding the read position
    from 0 to 100 ms over a 10 ms time constant is not a glide, it is a
    destroyed signal. This is a user control, not a chorus.

    Instead there is one delay line with TWO read taps and a short linear
    crossfade between them. No tap ever moves while it is being read, so no
    sample is ever pitch-shifted. The only artifact is the two delay times
    overlapping for the length of the fade, which is what a professional
    stepped delay control does.

    Linear rather than equal-power: the two taps are the same signal at two
    offsets and can be highly correlated, and an equal-power pair sums above
    unity when its inputs correlate. Linear sums to exactly one.

    THE COALESCING POLICY, stated exactly
    -------------------------------------
    A fade, once begun, always runs to completion. It is never restarted,
    retargeted or shortened mid-flight.

      - idle, and the request differs from the current tap
            -> begin a fade from current to the request.
      - fading, and a request arrives
            -> record it as the pending request and keep fading. A later
               request simply overwrites the pending one; they are coalesced,
               not queued.
      - a fade completes
            -> the destination becomes the current tap. If a pending request
               differs from it, begin the next fade immediately.

    So automation faster than the fade produces a sequence of complete
    20 ms fades that always converges on the last value the user asked for,
    and never a stutter of restarted ramps. Worst-case lag behind a control
    being swept continuously is one fade length.

    The same state drives both channels, so L and R are always on the same
    pair of taps with the same gain. They cannot drift.
*/
template <typename SampleType = double>
class PreDelay
{
public:
    /** Longest pre-delay the shipped parameter can ask for. */
    static constexpr double maxDelaySeconds = 0.5;

    /** Crossfade length. 20 ms: long enough that the overlap is not heard as
        a step, short enough that a control sweep does not feel detached. */
    static constexpr double crossfadeSeconds = 0.020;

    // -----------------------------------------------------------------------
    //  Setup. Allocates; never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        const int maxSamples = static_cast<int> (std::ceil (maxDelaySeconds * currentSampleRate));

        left .prepareSamples (maxSamples);
        right.prepareSamples (maxSamples);

        maxTap = maxSamples;

        fadeLength = std::max (1, static_cast<int> (std::round (crossfadeSeconds * currentSampleRate)));

        currentTap = 0;
        pendingTap = 0;
        fadeFrom   = 0;
        fadeTo     = 0;
        fadePos    = 0;
        fading     = false;

        reset();
    }

    /** Clears the stored audio. Taps and the fade state are left alone. */
    void reset() noexcept
    {
        left .reset();
        right.reset();
    }

    /** Drops any fade in flight and parks on the current target. For the
        first block after prepare, where there is no previous value to move
        away from. */
    void snapTo (double milliseconds) noexcept
    {
        currentTap = tapFor (milliseconds);
        pendingTap = currentTap;
        fading     = false;
        fadePos    = 0;
    }

    // -----------------------------------------------------------------------
    //  Control rate - once per chunk
    // -----------------------------------------------------------------------

    /** Requests a new delay. See the coalescing policy in the class comment. */
    void setDelayMilliseconds (double milliseconds) noexcept
    {
        const int tap = tapFor (milliseconds);

        pendingTap = tap;

        if (! fading && tap != currentTap)
            beginFade (tap);
    }

    // -----------------------------------------------------------------------
    //  Audio rate
    // -----------------------------------------------------------------------

    /** True when the stage is an exact identity and can be skipped. */
    [[nodiscard]] bool isBypassed() const noexcept
    {
        return ! fading && currentTap == 0;
    }

    void process (SampleType* l, SampleType* r, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // The write is UNCONDITIONAL, including while bypassed. A bypass
            // that also stopped writing would leave the buffer stale, and the
            // first fade away from 0 ms would cross into audio from whenever
            // the control was last moved. Two stores per sample is a small
            // price for a buffer that is always truthful.
            left .write (l[i]);
            right.write (r[i]);

            if (! fading)
            {
                if (currentTap != 0)
                {
                    l[i] = left .readAt (currentTap);
                    r[i] = right.readAt (currentTap);
                }

                // currentTap == 0: readAt(0) would return exactly l[i], so the
                // sample is left alone. Identity by construction rather than
                // by arithmetic that happens to cancel.
                continue;
            }

            const auto g = static_cast<SampleType> (fadePos) / static_cast<SampleType> (fadeLength);

            const SampleType fromL = left .readAt (fadeFrom);
            const SampleType fromR = right.readAt (fadeFrom);
            const SampleType toL   = left .readAt (fadeTo);
            const SampleType toR   = right.readAt (fadeTo);

            l[i] = fromL + (toL - fromL) * g;
            r[i] = fromR + (toR - fromR) * g;

            if (++fadePos >= fadeLength)
            {
                currentTap = fadeTo;
                fading     = false;
                fadePos    = 0;

                // A request that arrived mid-fade is honoured now, not dropped.
                if (pendingTap != currentTap)
                    beginFade (pendingTap);
            }
        }
    }

    // --- Introspection, for the tests --------------------------------------

    [[nodiscard]] int  getCurrentTapSamples() const noexcept { return currentTap; }
    [[nodiscard]] int  getPendingTapSamples() const noexcept { return pendingTap; }
    [[nodiscard]] bool isFading()             const noexcept { return fading; }
    [[nodiscard]] int  getFadeLengthSamples() const noexcept { return fadeLength; }
    [[nodiscard]] int  getMaxTapSamples()     const noexcept { return maxTap; }

    [[nodiscard]] int tapFor (double milliseconds) const noexcept
    {
        if (! (milliseconds > 0.0))
            return 0;

        const auto samples = static_cast<int> (std::lround (milliseconds * 0.001 * currentSampleRate));

        return std::clamp (samples, 0, maxTap);
    }

private:
    void beginFade (int destination) noexcept
    {
        fadeFrom = currentTap;
        fadeTo   = destination;
        fadePos  = 0;
        fading   = true;
    }

    DelayLine<SampleType> left, right;

    double currentSampleRate = 48000.0;

    int maxTap     = 0;
    int currentTap = 0;
    int pendingTap = 0;

    int  fadeFrom   = 0;
    int  fadeTo     = 0;
    int  fadePos    = 0;
    int  fadeLength = 1;
    bool fading     = false;
};

} // namespace av::dsp
