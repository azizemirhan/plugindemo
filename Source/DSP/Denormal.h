#pragma once

#include <cmath>
#include <cstdint>

#if defined (_M_X64) || defined (__x86_64__) || defined (__SSE2__) \
    || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #define AV_HAS_SSE_CONTROL 1
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#else
 #define AV_HAS_SSE_CONTROL 0
#endif

/**
    Denormal protection.

    A reverb tail decays towards zero forever. Once the values in a feedback path
    fall below ~1e-38 (float) or ~1e-308 (double) the CPU switches to
    microcoded subnormal arithmetic, and a plugin that used 2 % of a core can
    suddenly use 30 % — while sitting in silence. Three defences are used, in
    this order:

    1. ScopedFtz at the top of the audio callback.
       Sets the CPU's flush-to-zero and denormals-are-zero bits so subnormal
       results become exact zero at no cost. This is the primary defence.
       It is scoped because the host owns those bits and expects them restored.

    2. An additive guard inside long feedback paths.
       FTZ cannot be relied upon alone: a host may run our callback on a thread
       whose MXCSR we do not control, and non-x86 targets have no equivalent.
       Adding `denormalGuard` (1e-25, i.e. -500 dBFS) to a feedback accumulator
       parks it at a small *normal* value instead of letting it decay into the
       subnormal range. Costs one add.

    3. flushDenormal() at module boundaries.
       An explicit compare-and-zero for state that is read back rarely, where
       neither of the above applies.
*/
namespace av::dsp
{

/**
    Enables flush-to-zero and denormals-are-zero for the lifetime of the object,
    restoring the caller's floating-point control state on exit.

    Equivalent in effect to juce::ScopedNoDenormals, duplicated here so the DSP
    core stays free of JUCE and can be exercised by the offline test harness.
*/
class ScopedFtz
{
public:
    ScopedFtz() noexcept
    {
       #if AV_HAS_SSE_CONTROL
        savedState = _mm_getcsr();
        _mm_setcsr (savedState | flushToZero | denormalsAreZero);
       #endif
    }

    ~ScopedFtz() noexcept
    {
       #if AV_HAS_SSE_CONTROL
        _mm_setcsr (savedState);
       #endif
    }

    ScopedFtz (const ScopedFtz&) = delete;
    ScopedFtz& operator= (const ScopedFtz&) = delete;

    /** True if the CPU is currently flushing subnormals to zero. */
    [[nodiscard]] static bool isCurrentlyEnabled() noexcept
    {
       #if AV_HAS_SSE_CONTROL
        return (_mm_getcsr() & (flushToZero | denormalsAreZero)) == (flushToZero | denormalsAreZero);
       #else
        return false;
       #endif
    }

    /** True if this build can control the FPU's subnormal handling at all. */
    [[nodiscard]] static constexpr bool isSupported() noexcept
    {
        return AV_HAS_SSE_CONTROL != 0;
    }

private:
    static constexpr unsigned int flushToZero      = 0x8000;
    static constexpr unsigned int denormalsAreZero = 0x0040;

    unsigned int savedState = 0;
};

/**
    Additive anti-denormal constant, -500 dBFS.

    Add to a feedback accumulator each circulation. The accumulator settles at
    guard / (1 - feedbackGain) rather than decaying into the subnormal range.
*/
inline constexpr double denormalGuard = 1.0e-25;

/** Returns true if x is a subnormal (denormal) number. */
template <typename T>
[[nodiscard]] inline bool isSubnormal (T x) noexcept
{
    return std::fpclassify (x) == FP_SUBNORMAL;
}

/**
    Anti-denormal guard whose sign alternates every sample.

    A guard of fixed polarity keeps a feedback path out of the subnormal range,
    but it also injects a constant. Inside an allpass or an FDN that constant is
    integrated by the recursion and settles at guard/(1-g), which is a DC offset
    that every later stage then has to carry. With dozens of recursive elements
    the offsets sum, and a structure that is supposed to be silent in silence
    instead sits at a small non-zero level.

    Alternating the sign removes that: the injected sequence is
    +e, -e, +e, -e..., whose running mean is exactly zero on every even sample
    and bounded by e everywhere else. The energy lands at Nyquist rather than at
    DC, at -500 dBFS, where nothing downstream cares. The recursion cannot
    accumulate it because consecutive contributions cancel.

    One instance per recursive state, so that two states do not accidentally
    share a phase and turn a cancelling pair into a reinforcing one.
*/
class AlternatingGuard
{
public:
    void reset() noexcept { sign = 1.0; }

    /** Advances one sample and returns the guard value to add. */
    [[nodiscard]] double next() noexcept
    {
        sign = -sign;
        return denormalGuard * sign;
    }

    /** The value the next call to next() will not return — for tests. */
    [[nodiscard]] double currentSign() const noexcept { return sign; }

private:
    double sign = 1.0;
};

} // namespace av::dsp
