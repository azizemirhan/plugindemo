#include "AllocationDetector.h"

#include <cstdlib>
#include <new>

namespace avtest
{
    // Constant-initialised, so it is valid before any dynamic initialisation
    // runs and cannot miss an allocation made by a static constructor.
    std::atomic<long long> allocationCount { 0 };

    bool allocationCountingAvailable() noexcept { return true; }
}

void* operator new (std::size_t size)
{
    avtest::allocationCount.fetch_add (1, std::memory_order_relaxed);

    if (size == 0)
        size = 1;

    if (void* p = std::malloc (size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return operator new (size);
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    avtest::allocationCount.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size == 0 ? 1 : size);
}

void* operator new[] (std::size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new (size, tag);
}

void operator delete (void* p) noexcept                    { std::free (p); }
void operator delete[] (void* p) noexcept                  { std::free (p); }
void operator delete (void* p, std::size_t) noexcept       { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept     { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept   { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
