#pragma once

#include <atomic>
#include <cstddef>

/**
    Heap-allocation counter used to prove the real-time safety claim.

    The test binary replaces the global operator new/delete so that every
    allocation increments a counter. Wrapping a block of DSP code in a
    ScopedAllocationCounter then turns "this code does not allocate" from a
    comment into an assertion.

    Only the plain and array forms are replaced. The aligned (align_val_t)
    overloads are left to the standard library so their new/delete pairs stay
    consistent; nothing in the DSP core uses over-aligned types.
*/
namespace avtest
{

extern std::atomic<long long> allocationCount;

/**
    False in a sanitizer build, where the counter cannot work.

    AddressSanitizer installs its own operator new, so the replacement this
    counter depends on is not linked in there. Suites that assert "this
    allocated nothing" check this first and report themselves as not measured
    rather than passing on a counter that is guaranteed to stay at zero.
*/
[[nodiscard]] bool allocationCountingAvailable() noexcept;

class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept
        : startCount (allocationCount.load (std::memory_order_relaxed)) {}

    [[nodiscard]] long long count() const noexcept
    {
        return allocationCount.load (std::memory_order_relaxed) - startCount;
    }

    void restart() noexcept
    {
        startCount = allocationCount.load (std::memory_order_relaxed);
    }

private:
    long long startCount;
};

} // namespace avtest
