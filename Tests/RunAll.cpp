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
    FAST AND HEAVY.

    Since PHASE 12 the harness also holds research suites that are mostly
    stochastic impostor searches: an hour and a half of them. They are marked
    AV_RUN_HEAVY below. Nothing about them is optional for a gate:

        AuroraVerbTests.exe            everything, as before. THE GATE.
        AuroraVerbTests.exe --fast     everything except the heavy suites

    --fast is for ordinary development. It names every suite it skipped and
    says so again on the last line, so a fast run cannot be read as a full
    one. The heavy suites are deterministic (fixed seeds) and are required for
    final Mode acceptance, any major topology change, PHASE 12 closure and
    release validation. --fast combines with substring filters.
*/

namespace
{
    std::vector<std::string> suiteFilters;
    std::vector<std::string> heavySkipped;
    bool fastOnly = false;
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
    [[nodiscard]] bool matches (const char* name)
    {
        if (suiteFilters.empty())
            return true;

        for (const auto& filter : suiteFilters)
            if (std::strstr (name, filter.c_str()) != nullptr)
                return true;

        return false;
    }
}

#define AV_RUN(fn) do { if (wanted (#fn)) fn(); } while (false)

// A heavy suite: skipped, by name, under --fast.
#define AV_RUN_HEAVY(fn) do { if (fastOnly) { if (matches (#fn)) heavySkipped.emplace_back (#fn); } \
                              else if (wanted (#fn)) fn(); } while (false)

// A suite that runs ONLY when a filter is exactly its name. For measurements
// whose result depends on the state of the machine (the Plate CPU campaign):
// they must never run as part of a regression, fast or full, and a substring
// filter such as "Phase12" must not start one by accident.
#define AV_RUN_ON_REQUEST(fn) do { if (requested (#fn)) { ++suitesRun; fn(); } } while (false)

namespace
{
    [[nodiscard]] bool requested (const char* name)
    {
        for (const auto& filter : suiteFilters)
            if (filter == name)
                return true;

        return false;
    }
}

namespace avtest
{

void setSuiteFilters (int count, const char* const* values)
{
    suiteFilters.clear();
    fastOnly = false;

    for (int i = 0; i < count; ++i)
        if (values[i] != nullptr && values[i][0] != '\0')
        {
            if (std::strcmp (values[i], "--fast") == 0)
                fastOnly = true;
            else
                suiteFilters.emplace_back (values[i]);
        }
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

    if (fastOnly)
        std::cout << "FAST RUN: heavy research suites are skipped. This is not the gate.\n\n";

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
    AV_RUN (runDampingLowCornerSweepTests);
    AV_RUN (runDampingLowAmountSweepTests);
    AV_RUN (runDampingHighAmountSweepTests);
    AV_RUN (runDampingOverlapTests);
    AV_RUN (runDampingModulationCornerTests);

    // PHASE 8
    AV_RUN (runPhase8Tests);
    AV_RUN (runPhase8SystemTests);
    AV_RUN (runPhase8SignoffTests);
    AV_RUN (runPhase9ResearchTests);
    AV_RUN (runPhase9Stage2Tests);
    AV_RUN (runPhase9Stage3Tests);
    AV_RUN (runPhase10ResearchTests);
    AV_RUN (runPhase10Stage2Tests);
    AV_RUN (runPhase10ProductionTests);
    AV_RUN (runPhase10Stage35Tests);
    AV_RUN (runPhase10BalanceLawTests);
    AV_RUN (runPhase11StereoAuditTests);
    AV_RUN (runPhase11ReadoutTests);
    AV_RUN (runPhase11DelaySetTests);
    AV_RUN (runPhase11Stage4Tests);
    AV_RUN_HEAVY (runPhase12ModeFamilyTests);
    AV_RUN_HEAVY (runPhase12StructuralTests);
    AV_RUN_HEAVY (runPhase12Stage4Tests);
    AV_RUN (runPhase12Stage5FastTests);
    AV_RUN_HEAVY (runPhase12Stage5Tests);
    AV_RUN (runPhase12Stage6FastTests);
    AV_RUN_HEAVY (runPhase12Stage6Tests);
    AV_RUN (runPhase12Stage7PlateFastTests);

    // The Plate validation tool. Named exactly, or not run at all.
    AV_RUN_ON_REQUEST (runPhase12Stage7PlateCpu);
    AV_RUN_ON_REQUEST (runPhase12Stage7PlateStress);
    AV_RUN_ON_REQUEST (runPhase12Stage7PlateRates);

    // Stage 8, Ambient. Research measurements, named exactly.
    AV_RUN_ON_REQUEST (runPhase12Stage8AmbientDiagnosis);


    if (suitesRun == 0)
    {
        std::cout << "No suite matched the filter. Nothing was measured." << std::endl;
        return 2;
    }

    const int result = finish();

    if (fastOnly)
    {
        std::cout << "\nFAST RUN. " << heavySkipped.size() << " heavy suite(s) NOT run:";

        for (const auto& name : heavySkipped)
            std::cout << "\n    " << name;

        std::cout << "\nThis result is not a full gate. Run without --fast for one." << std::endl;
    }

    return result;
}

} // namespace avtest
