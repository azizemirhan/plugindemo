/*
    PHASE 12 - STAGE 7b. The one piece of the Plate validation tool that
    talks to the operating system, kept in its own file so that no other
    test source sees <windows.h>.
*/

#include <string>

#if defined (_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

namespace avtest::stage7
{

std::string pinAndPrioritise()
{
   #if defined (_WIN32)
    const bool priority = SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS) != 0;

    // The highest-numbered core the process may use: core 0 takes most of
    // the system's interrupts.
    DWORD_PTR process = 0, system = 0;
    DWORD_PTR chosen = 0;

    if (GetProcessAffinityMask (GetCurrentProcess(), &process, &system) != 0)
        for (DWORD_PTR bit = static_cast<DWORD_PTR> (1) << (sizeof (DWORD_PTR) * 8 - 1); bit != 0; bit >>= 1)
            if ((process & bit) != 0)
            {
                chosen = bit;
                break;
            }

    const bool pinned = chosen != 0 && SetThreadAffinityMask (GetCurrentThread(), chosen) != 0;

    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    return std::string (priority ? "high priority" : "PRIORITY NOT RAISED")
         + ", " + (pinned ? "pinned to one core" : "NOT PINNED");
   #else
    return "priority and affinity not set on this platform";
   #endif
}

} // namespace avtest::stage7
