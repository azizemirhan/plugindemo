#include "AllocationDetector.h"

/*
    The allocation counter, minus the operator new replacement.

    Used only in the AddressSanitizer build. ASan installs its own allocator and
    its own new/delete, so replacing them again either fails to link or defeats
    the sanitizer — and a sanitizer that has been quietly disabled is worse than
    no sanitizer at all.

    The counter still exists and still compiles, so nothing has to be #ifdef'd
    at every call site. It simply never moves, which is why the suites that
    assert "this allocated nothing" report themselves as not measured in a
    sanitizer build rather than passing on a counter that cannot rise.

    The real allocation figures come from the ordinary build, which is where the
    delivery report quotes them from.
*/
namespace avtest
{
    std::atomic<long long> allocationCount { 0 };

    bool allocationCountingAvailable() noexcept { return false; }
}
