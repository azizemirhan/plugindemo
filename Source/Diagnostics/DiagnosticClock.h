#pragma once

#include <chrono>
#include <cstdint>

namespace av::diag
{

/**
    Monotonic timing for the audio thread.

    steady_clock, not system_clock: the wall clock can jump backwards when the
    machine syncs its time, and a negative callback duration would poison every
    statistic downstream. steady_clock cannot go backwards by definition.

    On Windows this is QueryPerformanceCounter, which is a handful of
    nanoseconds and does not enter the kernel on any machine this project
    targets. That cost is not assumed — DiagnosticCpuBenchmark measures the
    timer against an empty loop and the figure is in the report.

    Nothing here allocates, locks, or formats anything.
*/
class DiagnosticClock
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    [[nodiscard]] static TimePoint now() noexcept { return Clock::now(); }

    [[nodiscard]] static int64_t nanosecondsBetween (TimePoint start, TimePoint end) noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds> (end - start).count();
    }

    [[nodiscard]] static double microsecondsBetween (TimePoint start, TimePoint end) noexcept
    {
        return static_cast<double> (nanosecondsBetween (start, end)) * 1.0e-3;
    }
};

/**
    Times one scope and hands the result to a callback.

    Deliberately not a class that knows about counters: the caller decides what
    to do with the number, so this can be used in a benchmark and in the audio
    path without either knowing about the other.
*/
template <typename OnFinished>
class ScopedDiagnosticTimer
{
public:
    explicit ScopedDiagnosticTimer (OnFinished&& callback) noexcept
        : onFinished (std::move (callback)), start (DiagnosticClock::now()) {}

    ~ScopedDiagnosticTimer()
    {
        onFinished (DiagnosticClock::nanosecondsBetween (start, DiagnosticClock::now()));
    }

    ScopedDiagnosticTimer (const ScopedDiagnosticTimer&) = delete;
    ScopedDiagnosticTimer& operator= (const ScopedDiagnosticTimer&) = delete;

private:
    OnFinished onFinished;
    DiagnosticClock::TimePoint start;
};

} // namespace av::diag
