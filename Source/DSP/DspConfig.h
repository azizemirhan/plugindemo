#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "Denormal.h"

/**
    Project-wide DSP conventions.

    Every DSP module in this project follows the rules stated here. They are the
    non-negotiable constraints that the whole reverb engine is built on:

      - Internal processing is double precision unless a module documents a
        specific reason to use float (e.g. very long delay buffers where memory
        bandwidth matters more than the last few bits of precision).
      - No heap allocation, no locking, no file/system I/O on the audio thread.
        All buffers are sized in prepare() and only reused in process().
      - Every module is sample-rate independent: all times are specified in
        seconds and converted to samples in prepare().
      - Every feedback path is denormal-protected.
*/
namespace av
{

/** Default internal processing type for the reverb core. */
using Real = double;

/** Sample type used by the host callback (JUCE gives us float in practice). */
using HostSample = float;

namespace constants
{
    inline constexpr Real pi        = 3.14159265358979323846;
    inline constexpr Real twoPi     = 2.0 * pi;
    inline constexpr Real sqrtHalf  = 0.70710678118654752440;

    /** Sample rates the engine is validated against. */
    inline constexpr double supportedRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };

    /** Design reference rate. Delay-line lengths are authored at this rate and
        scaled to the running rate, so the reverb character is preserved when
        the host switches rate. */
    inline constexpr double referenceRate = 48000.0;

    /** Longest pre-delay the engine allocates for, in seconds. */
    inline constexpr Real maxPreDelaySeconds = 0.5;

    /** Longest decay time exposed to the user, in seconds. */
    inline constexpr Real maxDecaySeconds = 60.0;
}

// Denormal protection lives in DSP/Denormal.h; these aliases keep the short
// names available to code that only includes this header.
using dsp::denormalGuard;
using dsp::isSubnormal;
using dsp::ScopedFtz;

/** Returns x with values near the subnormal range flushed to zero. */
template <typename T>
[[nodiscard]] inline T flushDenormal (T x) noexcept
{
    return (std::abs (x) < static_cast<T> (1.0e-20)) ? T (0) : x;
}

/** Converts a decibel value to a linear gain. */
template <typename T>
[[nodiscard]] inline T decibelsToGain (T db, T minusInfinityDb = static_cast<T> (-100)) noexcept
{
    return db > minusInfinityDb ? std::pow (static_cast<T> (10), db * static_cast<T> (0.05))
                                : static_cast<T> (0);
}

/**
    Scales a delay length authored at constants::referenceRate to the running
    sample rate. Every hard-coded delay time in the algorithm bank goes through
    this so the reverb sounds identical at 44.1 and 96 kHz.
*/
[[nodiscard]] inline double scaleToRate (double lengthInSamplesAtReference,
                                         double runningSampleRate) noexcept
{
    return lengthInSamplesAtReference * (runningSampleRate / constants::referenceRate);
}

} // namespace av
