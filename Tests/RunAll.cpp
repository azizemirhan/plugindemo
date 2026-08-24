#include "Suites.h"
#include "TestFramework.h"

#include <iostream>

namespace avtest
{

int runAllSuites()
{
    std::cout << "=================================================================\n"
              << "AuroraVerb DSP core tests\n"
              << "=================================================================\n";

    runInterpolationTests();
    runDelayLineTests();
    runModulatedDelayTests();
    runSmoothingTests();
    runDenormalTests();
    runScratchArenaTests();
    runRealTimeSafetyTests();
    runRateAndBlockSizeTests();
    runCpuBenchmark();

    // PHASE 3
    runAllpassDiffuserTests();
    runDiffusionNetworkTests();
    runStereoDiffusionTests();
    runDiffusionRealTimeSafetyTests();
    runDiffusionCpuBenchmark();

    // PHASE 4
    runFdnMatrixTests();
    runFdnDelaySetTests();
    runFdnCoreTests();
    runFdnStereoTests();
    runFdnMorphTests();
    runFdnRealTimeSafetyTests();
    runFdnCpuBenchmark();

    // PHASE 5A
    runMatrixModulationTests();
    runMatrixModulationSweepTests();
    runMatrixModulationCpuBenchmark();

    // PHASE 5B
    runAllpassModulationTests();
    runAllpassModulationSweepTests();
    runAllpassModulationCpuBenchmark();

    // PHASE 5C
    runModulationSelectionTests();
    runHybridAnalysisTests();
    runPitchSidebandTests();
    runRt60CorrectionTests();
    runSizeCompensationTests();
    runPreviewIntegrationTests();
    runPhase5cCpuBenchmark();

    // PHASE 5D
    runDiagnosticQueueTests();
    runDiagnosticSessionTests();
    runDiagnosticAnalysisTests();
    runDiagnosticCpuBenchmark();

    // FL Studio validation round 2
    runGainStagingTests();

    // PHASE 5F / FU-2
    runWetGainStagingTests();

    // PHASE 6
    runModalMapTests();
    runModalDecayTests();
    runModalAdversarialTests();
    runSizeTrackTests();
    runSizeIntegrationTests();

    // PHASE 7
    runDampingTests();

    return finish();
}

} // namespace avtest
