/*
    AuroraVerb offline DSP test harness — console entry point.

    PHASE 2 scope: the DSP core. Every module added from here on gets a suite in
    this binary before it is wired into the plugin, because a reverb that is only
    testable by ear is a reverb whose regressions are found by users.

    Exit code is 0 only when every check passes.

    Arguments, if any, are substring filters on suite names:

        AuroraVerbTests.exe Residue Empirical

    With none, every suite runs. The filter exists because the PHASE 6 and
    PHASE 7 measurement studies put the full run over an hour, and looking at
    one suite should not cost that.

    The same suites are also exported from AuroraVerbTests.dll (see
    TestExports.cpp), because Windows Smart App Control refuses to start
    locally-built unsigned executables while still permitting unsigned DLLs to
    be loaded. Scripts\run-tests.ps1 picks whichever route works.
*/

#include "Suites.h"

int main (int argc, char** argv)
{
    if (argc > 1)
        avtest::setSuiteFilters (argc - 1, argv + 1);

    return avtest::runAllSuites();
}
