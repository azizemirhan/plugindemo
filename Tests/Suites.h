#pragma once

namespace avtest
{

void runInterpolationTests();
void runDelayLineTests();
void runModulatedDelayTests();
void runSmoothingTests();
void runDenormalTests();
void runScratchArenaTests();
void runRealTimeSafetyTests();
void runRateAndBlockSizeTests();
void runCpuBenchmark();

// --- PHASE 3: diffusion ---------------------------------------------------

void runAllpassDiffuserTests();
void runDiffusionNetworkTests();
void runStereoDiffusionTests();
void runDiffusionRealTimeSafetyTests();
void runDiffusionCpuBenchmark();

// --- PHASE 4: feedback delay network --------------------------------------

void runFdnMatrixTests();
void runFdnDelaySetTests();
void runFdnCoreTests();
void runFdnStereoTests();
void runFdnMorphTests();
void runFdnRealTimeSafetyTests();
void runFdnCpuBenchmark();

// --- PHASE 5A: matrix modulation ------------------------------------------

void runMatrixModulationTests();
void runMatrixModulationSweepTests();
void runMatrixModulationCpuBenchmark();

// --- PHASE 5B: allpass phase modulation -----------------------------------

void runAllpassModulationTests();
void runAllpassModulationSweepTests();
void runAllpassModulationCpuBenchmark();

// --- PHASE 5C: final modulation, RT60 correction, Size compensation -------

void runModulationSelectionTests();
void runHybridAnalysisTests();
void runPitchSidebandTests();
void runRt60CorrectionTests();
void runSizeCompensationTests();
void runPreviewIntegrationTests();
void runPhase5cCpuBenchmark();

// --- PHASE 5D: diagnostic layer -------------------------------------------

void runDiagnosticQueueTests();
void runDiagnosticSessionTests();
void runDiagnosticAnalysisTests();
void runDiagnosticCpuBenchmark();

// --- FL Studio validation round 2 -----------------------------------------

void runGainStagingTests();

// --- PHASE 5F / FU-2: preview and wet gain staging ------------------------

void runWetGainStagingTests();

// --- PHASE 6: Aurora Sound - voicing and character -------------------------

void runModalMapTests();
void runModalDecayTests();
void runModalAdversarialTests();
void runSizeTrackTests();
void runSizeIntegrationTests();

// PHASE 7
void runDampingTests();
void runDampingCheckpointTests();
void runDampingTopologyTests();
void runDampingRootCauseTests();
void runDampingGateTests();
void runDampingEnsembleTests();
void runDampingSyntheticTests();
void runDampingCoherenceTests();
void runDampingPermutationTests();
void runDampingEmpiricalTests();
void runDampingModulationTests();
void runDampingResidueTests();
void runDampingSecondOrderTests();
void runDampingCornerSweepTests();
void runDampingLowCornerSweepTests();
void runDampingLowAmountSweepTests();
void runDampingHighAmountSweepTests();
void runDampingOverlapTests();
void runDampingModulationCornerTests();
void runPhase8Tests();
void runPhase8SystemTests();
void runPhase8SignoffTests();

/** Runs every suite and returns the process exit code (0 = all passed). */
int runAllSuites();

/** Substring filters on suite function names. No filters means every suite. */
void setSuiteFilters (int count, const char* const* values);

} // namespace avtest
