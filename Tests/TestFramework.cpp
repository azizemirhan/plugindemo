#include "TestFramework.h"

#include "AllocationDetector.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace avtest
{

namespace
{
    struct SuiteResult
    {
        std::string name;
        int checks   = 0;
        int failures = 0;
    };

    std::vector<SuiteResult> suites;
    int totalChecks   = 0;
    int totalFailures = 0;

    SuiteResult& currentSuite()
    {
        if (suites.empty())
            suites.push_back ({ "(unnamed)", 0, 0 });

        return suites.back();
    }

    std::string number (double v)
    {
        std::ostringstream s;
        s << std::setprecision (10) << v;
        return s.str();
    }
}

void beginSuite (const std::string& name)
{
    suites.push_back ({ name, 0, 0 });
    std::cout << "\n" << name << "\n" << std::string (name.size(), '-') << "\n";
}

void check (bool passed, const std::string& description)
{
    auto& suite = currentSuite();
    ++suite.checks;
    ++totalChecks;

    if (! passed)
    {
        ++suite.failures;
        ++totalFailures;
    }

    std::cout << (passed ? "  PASS  " : "  FAIL  ") << description << "\n";
}

void checkClose (double actual, double expected, double tolerance,
                 const std::string& description)
{
    const double diff   = std::abs (actual - expected);
    const bool   passed = std::isfinite (actual) && diff <= tolerance;

    if (passed)
    {
        check (true, description);
    }
    else
    {
        check (false, description + "  [expected " + number (expected)
                                  + ", got " + number (actual)
                                  + ", diff " + number (diff)
                                  + ", tolerance " + number (tolerance) + "]");
    }
}

void checkAtMost (double actual, double limit, const std::string& description)
{
    const bool passed = std::isfinite (actual) && actual <= limit;

    if (passed)
        check (true, description + "  (" + number (actual) + " <= " + number (limit) + ")");
    else
        check (false, description + "  [" + number (actual) + " > " + number (limit) + "]");
}

void checkEqual (long long actual, long long expected, const std::string& description)
{
    if (actual == expected)
        check (true, description + "  (" + std::to_string (actual) + ")");
    else
        check (false, description + "  [" + std::to_string (actual)
                      + " != " + std::to_string (expected) + "]");
}

void checkEqual (double actual, double expected, const std::string& description)
{
    if (actual == expected)
        check (true, description + "  (" + number (actual) + ")");
    else
        check (false, description + "  [" + number (actual) + " != " + number (expected) + "]");
}

void checkEqual (const std::string& actual, const std::string& expected,
                 const std::string& description)
{
    if (actual == expected)
        check (true, description + "  (\"" + actual + "\")");
    else
        check (false, description + "  [\"" + actual + "\" != \"" + expected + "\"]");
}

void checkNoAllocations (long long observed, const std::string& description)
{
    if (! allocationCountingAvailable())
    {
        info (description + ": NOT MEASURED (allocation counting is unavailable"
                            " in a sanitizer build)");
        return;
    }

    checkEqual (observed, 0LL, description);
}

bool timingIsRepresentative() noexcept
{
   #if defined (AV_TEST_ASAN)
    return false;
   #else
    return true;
   #endif
}

void checkCpuAtMost (double actual, double limit, const std::string& description)
{
    if (! timingIsRepresentative())
    {
        info (description + ": NOT APPLICABLE (" + number (actual)
              + " measured under AddressSanitizer, which is several times slower than the"
                " build this limit describes)");
        return;
    }

    checkAtMost (actual, limit, description);
}

void info (const std::string& line)
{
    std::cout << "        " << line << "\n";
}

int getFailureCount() { return totalFailures; }
int getCheckCount()   { return totalChecks; }

int finish()
{
    std::cout << "\n=================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "=================================================================\n";

    size_t widest = 0;
    for (const auto& s : suites)
        widest = std::max (widest, s.name.size());

    for (const auto& s : suites)
    {
        std::cout << "  " << std::left << std::setw (static_cast<int> (widest) + 2) << s.name
                  << std::right << std::setw (4) << (s.checks - s.failures)
                  << " / " << std::setw (4) << s.checks
                  << (s.failures == 0 ? "   ok" : "   FAILED") << "\n";
    }

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "  " << (totalChecks - totalFailures) << " / " << totalChecks
              << " checks passed";

    if (totalFailures == 0)
        std::cout << ".  ALL TESTS PASSED.\n";
    else
        std::cout << ".  " << totalFailures << " FAILURE(S).\n";

    return totalFailures == 0 ? 0 : 1;
}

} // namespace avtest
