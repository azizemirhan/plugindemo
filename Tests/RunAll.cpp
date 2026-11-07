#include "Suites.h"
#include "TestFramework.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

/*
    Suite selection.

    The whole harness is a little over an hour of measurement, and most of that
    is the PHASE 6 and PHASE 7 studies. Re-running all of it to look at one
    suite is what made every iteration of the damping work cost an hour, so the
    runner takes substring filters:

        AuroraVerbTests.exe Residue Empirical

    With no arguments every suite runs, which is what CI and Scripts\run-tests.ps1
    do by default, so the gate is unchanged. The filter matches against the
    suite function's name, and a run that selects nothing says so rather than
    reporting a vacuous pass.
*/

namespace
{
    std::vector<std::string> suiteFilters;
    int suitesRun = 0;

    [[nodiscard]] bool wanted (const char* name)
    {
        if (suiteFilters.empty())
        {
            ++suitesRun;
            return true;
        }

        for (const auto& filter : suiteFilters)
            if (std::strstr (name, filter.c_str()) != nullptr)
            {
                ++suitesRun;
                return true;
            }

        return false;
    }
}

#define AV_RUN(fn) do { if (wanted (#fn)) fn(); } while (false)

namespace avtest
{

void setSuiteFilters (int count, const char* const* values)
{
    suiteFilters.clear();

    for (int i = 0; i < count; ++i)
        if (values[i] != nullptr && values[i][0] != '\0')
            suiteFilters.emplace_back (values[i]);
}

int runAllSuites()
{
    std::cout << "=================================================================\n"
              << "AuroraVerb DSP core tests\n"
              << "=================================================================\n";

    if (! suiteFilters.empty())
    {
        std::cout << "Suite filter:";

        for (const auto& f : suiteFilters)
            std::cout << ' ' << f;

        std::cout << "\n\n";
    }

    AV_RUN (runInterpolationTests);
    AV_RUN (runDelayLineTests);
    AV_RUN (runModulatedDelayTests);
    AV_RUN (runSmoothingTests);
    AV_RUN (runDenormalTests);
    AV_RUN (runScratchArenaTests);
    AV_RUN (runRealTimeSafetyTests);
    AV_RUN (runRateAndBlockSizeTests);
    AV_RUN (runCpuBenchmark);

    // PHASE 3
    AV_RUN (runAllpassDiffuserTests);
    AV_RUN (runDiffusionNetworkTests);
    AV_RUN (runStereoDiffusionTests);
    AV_RUN (runDiffusionRealTimeSafetyTests);
    AV_RUN (runDiffusionCpuBenchmark);

    // PHASE 4
    AV_RUN (runFdnMatrixTests);
    AV_RUN (runFdnDelaySetTests);
    AV_RUN (runFdnCoreTests);
    AV_RUN (runFdnStereoTests);
    AV_RUN (runFdnMorphTests);
    AV_RUN (runFdnRealTimeSafetyTests);
    AV_RUN (runFdnCpuBenchmark);

    // PHASE 5A
    AV_RUN (runMatrixModulationTests);
    AV_RUN (runMatrixModulationSweepTests);
    AV_RUN (runMatrixModulationCpuBenchmark);

    // PHASE 5B
    AV_RUN (runAllpassModulationTests);
    AV_RUN (runAllpassModulationSweepTests);
    AV_RUN (runAllpassModulationCpuBenchmark);

    // PHASE 5C
    AV_RUN (runModulationSelectionTests);
    AV_RUN (runHybridAnalysisTests);
    AV_RUN (runPitchSidebandTests);
    AV_RUN (runRt60CorrectionTests);
    AV_RUN (runSizeCompensationTests);
    AV_RUN (runPreviewIntegrationTests);
    AV_RUN (runPhase5cCpuBenchmark);

    // PHASE 5D
    AV_RUN (runDiagnosticQueueTests);
    AV_RUN (runDiagnosticSessionTests);
    AV_RUN (runDiagnosticAnalysisTests);
    AV_RUN (runDiagnosticCpuBenchmark);

    // FL Studio validation round 2
    AV_RUN (runGainStagingTests);

    // PHASE 5F / FU-2
    AV_RUN (runWetGainStagingTests);

    // PHASE 6
    AV_RUN (runModalMapTests);
    AV_RUN (runModalDecayTests);
    AV_RUN (runModalAdversarialTests);
    AV_RUN (runSizeTrackTests);
    AV_RUN (runSizeIntegrationTests);

    // PHASE 7
    AV_RUN (runDampingTests);
    AV_RUN (runDampingCheckpointTests);
    AV_RUN (runDampingTopologyTests);
    AV_RUN (runDampingRootCauseTests);
    AV_RUN (runDampingGateTests);
    AV_RUN (runDampingEnsembleTests);
    AV_RUN (runDampingSyntheticTests);
    AV_RUN (runDampingCoherenceTests);
    AV_RUN (runDampingPermutationTests);
    AV_RUN (runDampingEmpiricalTests);
    AV_RUN (runDampingModulationTests);
    AV_RUN (runDampingResidueTests);
    AV_RUN (runDampingSecondOrderTests);
    AV_RUN (runDampingCornerSweepTests);


    if (suitesRun == 0)
    {
        std::cout << "No suite matched the filter. Nothing was measured." << std::endl;
        return 2;
    }

    return finish();
}

} // namespace avtest
