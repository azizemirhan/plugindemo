#pragma once

#include <string>

/**
    Minimal test reporting used by the offline DSP harness.

    Deliberately not a general-purpose framework: the DSP tests are measurement
    driven, so what matters is that every check prints a line and that numeric
    results are visible even when they pass.
*/
namespace avtest
{

/** Starts a named group of checks. */
void beginSuite (const std::string& name);

/** Records a boolean check. */
void check (bool passed, const std::string& description);

/** Records a numeric check and prints the values when it fails. */
void checkClose (double actual, double expected, double tolerance,
                 const std::string& description);

/** Records a check that `actual` is at most `limit`, printing both. */
void checkAtMost (double actual, double limit, const std::string& description);

/** Exact equality, printing both values when it fails.

    Overloaded rather than templated so that a count, a level and a name each
    print in a readable form. Use checkClose for anything that has been through
    floating-point arithmetic — an exact comparison there is a test that will
    fail on a different compiler for no useful reason. */
void checkEqual (long long actual, long long expected, const std::string& description);
void checkEqual (double actual, double expected, const std::string& description);
void checkEqual (const std::string& actual, const std::string& expected,
                 const std::string& description);

/**
    Asserts that a measured allocation count is zero.

    Reports "not measured" instead of passing when the allocation counter is
    unavailable, which it is in a sanitizer build: AddressSanitizer installs its
    own operator new, so the replacement the counter depends on is not linked
    in and the count can only ever read zero. Passing on that would be reporting
    a guarantee the run did not check.
*/
void checkNoAllocations (long long observed, const std::string& description);

/**
    False when the build cannot produce representative timings.

    AddressSanitizer instruments every memory access and runs several times
    slower, so a CPU threshold written for an ordinary Release build fails there
    for a reason that has nothing to do with the DSP. The benchmarks still run
    and still print their tables — the numbers are useful for comparing
    configurations against each other — but their pass/fail limits are reported
    as not applicable rather than failed.
*/
[[nodiscard]] bool timingIsRepresentative() noexcept;

/** checkAtMost, unless the build cannot produce representative timings. */
void checkCpuAtMost (double actual, double limit, const std::string& description);

/** Prints a measurement or note. Neither passes nor fails. */
void info (const std::string& line);

/** Prints the summary. Returns 0 when everything passed. */
int finish();

int getFailureCount();
int getCheckCount();

} // namespace avtest
