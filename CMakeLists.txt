cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
#  AuroraVerb — Professional Algorithmic Reverb
#  PHASE 1: JUCE VST3 plugin skeleton
#
#  Product identity is defined here in one place so it can be renamed later
#  without touching any source file.
# ---------------------------------------------------------------------------

set(AV_PRODUCT_NAME     "AuroraVerb"    CACHE STRING "Plugin product name")
set(AV_COMPANY_NAME     "IndieDSP"      CACHE STRING "Company / vendor name")
set(AV_BUNDLE_ID        "com.indiedsp.auroraverb" CACHE STRING "Bundle identifier")
set(AV_PLUGIN_CODE      "Avrb"          CACHE STRING "4-char unique plugin code")
set(AV_MANUFACTURER_CODE "Idsp"         CACHE STRING "4-char manufacturer code")

project(AuroraVerb VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put every build artefact in one predictable place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if (MSVC)
    # Static CRT: the shipped VST3 must not depend on the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Applied globally (JUCE included) because they affect correctness/build speed,
    # not warning noise.
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /MP)

    # Fast, IEEE-sane float model. /fp:fast is deliberately NOT used: reverb
    # feedback paths need predictable rounding and denormal behaviour.
    add_compile_options($<$<CONFIG:Release>:/fp:precise>
                        $<$<CONFIG:Release>:/Oi>
                        $<$<CONFIG:Release>:/Ot>)

    # Warning level applies to our code only — see AV_WARNING_FLAGS below.
    set(AV_WARNING_FLAGS /W4)
else()
    set(AV_WARNING_FLAGS -Wall -Wextra -Wshadow)
endif()

# ---------------------------------------------------------------------------
#  JUCE
#  Either point AV_JUCE_PATH at a local JUCE checkout, or let CMake fetch it.
# ---------------------------------------------------------------------------

set(AV_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(AV_JUCE_TAG  "8.0.15" CACHE STRING "JUCE git tag to fetch when AV_JUCE_PATH is empty")

if (AV_JUCE_PATH)
    message(STATUS "AuroraVerb: using local JUCE at ${AV_JUCE_PATH}")
    add_subdirectory("${AV_JUCE_PATH}" JUCE)
else()
    message(STATUS "AuroraVerb: fetching JUCE ${AV_JUCE_TAG}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${AV_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
#  Plugin install location
#
#  The system-wide folder (%CommonProgramFiles%\VST3) needs an elevated shell,
#  which would make every build require admin. Default to the per-user VST3
#  folder defined by the VST3 spec instead — every major DAW scans it.
# ---------------------------------------------------------------------------

option(AV_COPY_PLUGIN_AFTER_BUILD "Install the built plugin into a VST3 folder" ON)

# The reverb engine is part of every build and is no longer behind a flag.
#
# It used to be: AV_DEV_DIFFUSION_PREVIEW, default OFF, decided whether the wet
# path was the FDN tank or a copy of the input. Nothing failed to build without
# it, so a default configuration produced a plugin with no reverb in it and said
# nothing. Every artefact ever validated in a host had the flag ON, carried in a
# CMake cache rather than passed by the build script - so "the default build" and
# "the build we tested" were never the same thing, and only the cache knew.
#
# The option is deliberately not kept as a no-op: a stale -DAV_DEV_DIFFUSION_PREVIEW
# on a command line should fail loudly rather than be silently ignored.

# Development only: selects which PHASE 5B tank modulation candidate Mod Depth
# drives, for A/B work. 0 = matrix modulation only (PHASE 5A), 1 = the
# energy-preserving allpass PHASE 5C selected, 2 = Thiran, 3 = Schroeder
# reference. 1 is the shipped architecture and the default; anything else is a
# comparison build and must not be shipped.
set(AV_DEV_ALLPASS_CANDIDATE "1" CACHE STRING "Tank modulation candidate (0-3, 1 = shipped)")
set_property(CACHE AV_DEV_ALLPASS_CANDIDATE PROPERTY STRINGS 0 1 2 3)

# Development only: compiles the diagnostic layer in. Off by default, so the
# shipped plugin carries no telemetry code at all — not a disabled code path, no
# thread, no queue, no allocation, nothing. See Source/Diagnostics.
option(AV_DEV_DIAGNOSTICS "Compile the diagnostic telemetry layer into the plugin" OFF)

# Development only: allows the diagnostic layer to capture input and output
# audio to WAV. Implies AV_DEV_DIAGNOSTICS; on its own it does nothing.
option(AV_DEV_AUDIO_CAPTURE "Allow the diagnostic layer to capture audio" OFF)

set(AV_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3"
    CACHE PATH "Where the VST3 is installed after a build")

if (WIN32 AND AV_COPY_PLUGIN_AFTER_BUILD)
    file(MAKE_DIRECTORY "${AV_VST3_COPY_DIR}")
endif()

# ---------------------------------------------------------------------------
#  Plugin target
# ---------------------------------------------------------------------------

juce_add_plugin(AuroraVerb
    PRODUCT_NAME                "${AV_PRODUCT_NAME}"
    COMPANY_NAME                "${AV_COMPANY_NAME}"
    BUNDLE_ID                   "${AV_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    "${AV_MANUFACTURER_CODE}"
    PLUGIN_CODE                 "${AV_PLUGIN_CODE}"
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             "Fx" "Reverb"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${AV_COPY_PLUGIN_AFTER_BUILD}
    VST3_COPY_DIR               "${AV_VST3_COPY_DIR}"

    # moduleinfo.json is optional VST3 metadata that only speeds up host
    # scanning, and JUCE generates it by running a helper executable it has just
    # built. Windows Smart App Control blocks locally-built unsigned
    # executables, so on a machine with it enabled that step fails the build
    # outright — intermittently, since the block is per file hash. The plugin is
    # valid without the manifest and passes pluginval at strictness 10 either
    # way, so the step is turned off rather than left as a build that sometimes
    # works. See the Smart App Control note in README.md.
    VST3_AUTO_MANIFEST          FALSE)

juce_generate_juce_header(AuroraVerb)

target_sources(AuroraVerb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters/ParameterIDs.h
    Source/Parameters/ParameterLayout.cpp
    Source/Parameters/ParameterLayout.h
    Source/DSP/AllpassDiffuser.h
    Source/DSP/AllpassModulationTuning.h
    Source/DSP/AllpassPhaseBank.h
    Source/DSP/ConfigResult.h
    Source/DSP/DelayLine.h
    Source/DSP/Denormal.h
    Source/DSP/DiffusionNetwork.h
    Source/DSP/DiffusionStage.h
    Source/DSP/DiffusionTuning.h
    Source/DSP/DspConfig.h
    Source/DSP/FdnCore.h
    Source/DSP/FdnDelayLine.h
    Source/DSP/FdnTuning.h
    Source/DSP/FeedbackMatrix.h
    Source/DSP/GivensRotation.h
    Source/DSP/Interpolation.h
    Source/DSP/ModulatedFeedbackMatrix.h
    Source/DSP/ModulationArchitecture.h
    Source/DSP/ModulationTuning.h
    Source/DSP/Lfo.h
    Source/DSP/ModulatedDelay.h
    Source/DSP/ParameterMapping.h
    Source/DSP/MorphingStereoFdn.h
    Source/DSP/ScratchArena.h
    Source/DSP/SizeCompensation.h
    Source/DSP/SmoothedParameter.h
    Source/DSP/StereoDiffusionNetwork.h
    Source/DSP/StereoFdn.h
    Source/DSP/TimeVaryingAllpass.h
    Source/Diagnostics/DiagnosticClock.h
    Source/Diagnostics/DiagnosticCollector.h
    Source/Diagnostics/DiagnosticPaths.cpp
    Source/Diagnostics/DiagnosticPaths.h
    Source/Diagnostics/DiagnosticReport.cpp
    Source/Diagnostics/DiagnosticReport.h
    Source/Diagnostics/DiagnosticSession.cpp
    Source/Diagnostics/DiagnosticSession.h
    Source/Diagnostics/DiagnosticSpscQueue.h
    Source/Diagnostics/DiagnosticTypes.h
    Source/Diagnostics/DiagnosticWav.h)

target_include_directories(AuroraVerb PRIVATE Source)
target_compile_options(AuroraVerb PRIVATE ${AV_WARNING_FLAGS})

target_compile_definitions(AuroraVerb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # Every DSP module must be able to assert that it is not allocating or
    # locking on the audio thread. Enabled in Debug only.
    $<$<CONFIG:Debug>:AV_AUDIO_THREAD_CHECKS=1>
    AV_DEV_ALLPASS_CANDIDATE=${AV_DEV_ALLPASS_CANDIDATE}
    AV_DEV_DIAGNOSTICS=$<BOOL:${AV_DEV_DIAGNOSTICS}>
    AV_DEV_AUDIO_CAPTURE=$<BOOL:${AV_DEV_AUDIO_CAPTURE}>)

target_link_libraries(AuroraVerb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Tests (PHASE 22 — target exists now so DSP modules are testable from day 1)
# ---------------------------------------------------------------------------

option(AV_BUILD_TESTS "Build the DSP unit / measurement tests" OFF)

if (AV_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
