# ---------------------------------------------------------------------------
#  DSP test harness
#
#  Every DSP module is measurable offline — delay accuracy, interpolation error,
#  modulation pitch deviation, smoothing behaviour, denormal handling,
#  real-time safety and CPU cost — without loading a host.
#
#  The suites deliberately do NOT link JUCE. Nothing in Source/DSP may depend on
#  JUCE or on AuroraVerbProcessor, and this target failing to compile is how
#  that rule is enforced.
#
#  Two artefacts are produced from the same sources:
#
#    AuroraVerbTests.exe   the normal console runner
#    AuroraVerbTests.dll   the same suites behind an exported entry point
#
#  The DLL exists because Windows Smart App Control refuses to start
#  locally-built unsigned executables while still allowing unsigned DLLs to be
#  loaded. Scripts\run-tests.ps1 tries the executable first and falls back to
#  the DLL, so the suites stay runnable without weakening that setting.
# ---------------------------------------------------------------------------

set(AV_TEST_SUITE_SOURCES
    RunAll.cpp
    TestFramework.cpp
    InterpolationTests.cpp
    DelayLineTests.cpp
    ModulatedDelayTests.cpp
    SmoothingTests.cpp
    DenormalTests.cpp
    ScratchArenaTests.cpp
    RealTimeSafetyTests.cpp
    RateAndBlockSizeTests.cpp
    CpuBenchmark.cpp
    DspAnalysis.cpp
    AllpassDiffuserTests.cpp
    DiffusionNetworkTests.cpp
    StereoDiffusionTests.cpp
    DiffusionSafetyTests.cpp
    DiffusionCpuBenchmark.cpp
    FdnAnalysis.cpp
    FdnMatrixTests.cpp
    FdnDelaySetTests.cpp
    FdnCoreTests.cpp
    FdnStereoTests.cpp
    FdnMorphTests.cpp
    FdnSafetyTests.cpp
    FdnCpuBenchmark.cpp
    ModulationAnalysis.cpp
    MatrixModulationTests.cpp
    MatrixModulationSweepTests.cpp
    MatrixModulationCpuBenchmark.cpp
    AllpassModulationTests.cpp
    AllpassModulationSweepTests.cpp
    AllpassModulationCpuBenchmark.cpp
    CandidateAnalysis.cpp
    ModulationSelectionTests.cpp
    HybridAnalysisTests.cpp
    PitchSidebandTests.cpp
    Rt60CorrectionTests.cpp
    SizeCompensationTests.cpp
    PreviewIntegrationTests.cpp
    Phase5cCpuBenchmark.cpp
    SignalExport.cpp
    GainStaging.cpp
    GainStagingTests.cpp
    WetGainStaging.cpp
    WetGainStagingTests.cpp
    ModalMap.cpp
    ModalMapTests.cpp
    ModalSweep.cpp
    ModalDecay.cpp
    ModalDecayTests.cpp
    ModalLocal.cpp
    ModalAdversarialTests.cpp
    SizeTrack.cpp
    DampingTests.cpp
    DampingCheckpoint.cpp
    DampingTopology.cpp
    DampingRootCause.cpp
    DampingGate.cpp
    DampingEnsemble.cpp
    DampingSynthetic.cpp
    DampingCoherence.cpp
    DampingPermutation.cpp
    DampingEmpirical.cpp
    DampingModulation.cpp
    DampingResidue.cpp
    DampingSecondOrder.cpp
    SizeTrackTests.cpp
    SizeRetrack.cpp
    SizeCrossValidation.cpp
    Phase6Regenerate.cpp
    PerceptualBridge.cpp
    DiagnosticAnalyzer.cpp
    DiagnosticQueueTests.cpp
    DiagnosticSessionTests.cpp
    DiagnosticAnalysisTests.cpp
    DiagnosticCpuBenchmark.cpp
    ${CMAKE_SOURCE_DIR}/Source/Diagnostics/DiagnosticPaths.cpp
    ${CMAKE_SOURCE_DIR}/Source/Diagnostics/DiagnosticReport.cpp
    ${CMAKE_SOURCE_DIR}/Source/Diagnostics/DiagnosticSession.cpp)

option(AV_TEST_ASAN "Build the test target with AddressSanitizer" OFF)

# AllocationDetector.cpp replaces the global operator new/delete, so it is
# compiled directly into each final artefact rather than reached through a
# static library, where the linker could legitimately drop it.
#
# AddressSanitizer replaces the allocator itself, so the two cannot coexist.
# Under AV_TEST_ASAN the detector is left out and the suites that count
# allocations report themselves as skipped rather than silently measuring the
# sanitizer's allocator.
if (AV_TEST_ASAN)
    set(AV_TEST_COMMON_SOURCES
        ${AV_TEST_SUITE_SOURCES}
        AllocationDetectorStub.cpp)
else()
    set(AV_TEST_COMMON_SOURCES
        ${AV_TEST_SUITE_SOURCES}
        AllocationDetector.cpp)
endif()

add_executable(AuroraVerbTests
    TestMain.cpp
    ${AV_TEST_COMMON_SOURCES})

add_library(AuroraVerbTestsShared SHARED
    TestExports.cpp
    ${AV_TEST_COMMON_SOURCES})

set_target_properties(AuroraVerbTestsShared PROPERTIES
    OUTPUT_NAME "AuroraVerbTests")

# ---------------------------------------------------------------------------
#  Sanitizers
#
#  AddressSanitizer is available in MSVC and is worth running over the lock-free
#  structures: a ring buffer with hand-written index arithmetic is exactly the
#  kind of code that walks off the end of its storage without anybody noticing.
#
#  ThreadSanitizer, which is what would actually prove the absence of a data
#  race, is NOT available for MSVC on Windows. That is stated in the report
#  rather than glossed over: the argument for the snapshot transport's
#  correctness rests on ownership - no non-atomic object is reachable from two
#  threads - and not on a tool having failed to complain.
#
#  ASan replaces the allocator, so the allocation detector cannot be linked in
#  at the same time; the sanitizer build drops it and skips the suites that
#  depend on counting allocations.
# ---------------------------------------------------------------------------

foreach(target AuroraVerbTests AuroraVerbTestsShared)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_options(${target} PRIVATE ${AV_WARNING_FLAGS})

    if (AV_TEST_ASAN AND MSVC)
        # /fsanitize=address is incompatible with /GL and with the edit-and-
        # continue debug format; both are switched off for this build only.
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        target_compile_definitions(${target} PRIVATE AV_TEST_ASAN=1)
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        LIBRARY_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/tests"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/tests"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests")
endforeach()
