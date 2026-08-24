#include "Suites.h"
#include "TestFramework.h"

#include "DSP/ScratchArena.h"

#include <string>

using namespace av::dsp;

namespace avtest
{

void runScratchArenaTests()
{
    beginSuite ("ScratchArena");

    ScratchArena<double> arena;
    arena.prepare (1000);

    checkClose (static_cast<double> (arena.getCapacity()), 1000.0, 0.0, "capacity matches the request");
    checkClose (static_cast<double> (arena.getUsed()), 0.0, 0.0, "starts empty");

    auto* a = arena.acquire (400);
    auto* b = arena.acquire (400);

    check (a != nullptr && b != nullptr, "two 400-sample regions fit in a 1000-sample arena");
    check (b == a + 400, "regions are contiguous and do not overlap");
    checkClose (static_cast<double> (arena.getUsed()), 800.0, 0.0, "used tracks the cursor");
    checkClose (static_cast<double> (arena.getAvailable()), 200.0, 0.0, "available is the remainder");

    check (arena.acquire (400) == nullptr, "an over-large request returns nullptr rather than allocating");
    checkClose (static_cast<double> (arena.getUsed()), 800.0, 0.0, "a failed acquire does not move the cursor");

    auto* c = arena.acquire (200);
    check (c != nullptr, "a request that exactly fits still succeeds");
    checkClose (static_cast<double> (arena.getAvailable()), 0.0, 0.0, "arena is now full");
    checkClose (static_cast<double> (arena.getHighWaterMark()), 1000.0, 0.0, "high water mark records the peak");

    // Written data must survive until the cursor is rewound past it.
    for (int i = 0; i < 400; ++i)
        a[i] = static_cast<double> (i);

    bool intact = true;
    for (int i = 0; i < 400; ++i)
        intact &= (a[i] == static_cast<double> (i));

    check (intact, "regions are independent storage, not aliases");

    arena.reset();
    checkClose (static_cast<double> (arena.getUsed()), 0.0, 0.0, "reset rewinds the cursor");
    checkClose (static_cast<double> (arena.getHighWaterMark()), 1000.0, 0.0, "reset keeps the high water mark");

    // acquire() hands back whatever was there; clearFirst must zero it.
    auto* reused = arena.acquire (400);
    check (reused == a, "reset lets the same memory be handed out again");
    check (reused[10] == 10.0, "acquire() does not clear by default");

    arena.reset();
    auto* cleared = arena.acquire (400, true);
    bool allZero = true;
    for (int i = 0; i < 400; ++i)
        allZero &= (cleared[i] == 0.0);

    check (allZero, "acquire(clearFirst) zeroes the region");

    // Scope: a temporary borrow inside a loop must not make the arena grow.
    arena.reset();
    (void) arena.acquire (100);

    bool everyBorrowSucceeded = true;

    for (int iteration = 0; iteration < 50; ++iteration)
    {
        ScratchArena<double>::Scope scope (arena);
        everyBorrowSucceeded &= (arena.acquire (500) != nullptr);
    }

    check (everyBorrowSucceeded, "the same 500-sample region is borrowable 50 times over");
    checkClose (static_cast<double> (arena.getUsed()), 100.0, 0.0,
                "Scope restores the cursor, so 50 borrows of 500 samples need only 600");

    arena.clearHighWaterMark();
    checkClose (static_cast<double> (arena.getHighWaterMark()), 0.0, 0.0, "high water mark can be cleared");
}

} // namespace avtest
