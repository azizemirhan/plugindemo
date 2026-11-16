/*
    DLL entry points for the DSP test suites.

    Windows Smart App Control blocks the creation of a process from a
    locally-built, unsigned executable, but still allows an unsigned DLL to be
    loaded into an allowed process. Exporting the suites lets the tests run on a
    machine with Smart App Control enabled without weakening that setting.

    The results are written to a file rather than to the caller's console,
    because the loading process (PowerShell) does not reliably surface native
    stdout.
*/

#include "DampingListening.h"
#include "DiagnosticAnalyzer.h"
#include "Phase8Listening.h"
#include "Phase9Listening.h"
#include "Phase10Listening.h"
#include "Phase11Listening.h"
#include "Phase12Listening.h"
#include "GainStaging.h"
#include "WetGainStaging.h"
#include "ModalMap.h"
#include "PerceptualBridge.h"
#include "SizeTrack.h"
#include "SignalExport.h"
#include "Suites.h"

#include <fstream>
#include <iostream>

extern "C"
{

/** Runs every suite, printing to the caller's stdout. Returns 0 on success. */
__declspec (dllexport) int RunAuroraVerbTests()
{
    return avtest::runAllSuites();
}

/** Runs every suite, writing the report to `path`. Returns 0 on success. */
__declspec (dllexport) int RunAuroraVerbTestsToFile (const char* path)
{
    if (path == nullptr)
        return RunAuroraVerbTests();

    std::ofstream out (path, std::ios::binary | std::ios::trunc);

    if (! out.is_open())
        return -2;

    auto* previous = std::cout.rdbuf (out.rdbuf());
    const int result = avtest::runAllSuites();
    std::cout.rdbuf (previous);

    return result;
}

/**
    Renders the developer listening set into `directory`.

    Separate from the suites on purpose: a test run must not write files as a
    side effect. Returns the number of files written, or a negative value on
    failure.
*/
__declspec (dllexport) int ExportAuroraVerbTestSignals (const char* directory, double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportTestSignals (directory, sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the four gain-staging A/B files into `directory`.

    dry-reference, preview-current, preview-matched and preview-wet-100. The
    matched one differs from the current one by a single gain, so an A/B between
    them is about the reverb rather than about the level.
*/
__declspec (dllexport) int ExportAuroraVerbGainStaging (const char* directory, double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportGainStagingRenders (directory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Analyses a finished diagnostic session folder, writing analysis.json and
    analysis.txt into it.

    Exposed here rather than in the plugin because the analysis is developer
    tooling and because it reuses the offline measurement library, which the
    shipped VST3 has no reason to carry.

    Returns 0 pass, 1 pass with warnings, 2 fail, 3 missing or invalid session.
*/
__declspec (dllexport) int AnalyseAuroraVerbSession (const char* folder)
{
    if (folder == nullptr)
        return 3;

    return avtest::analyseSessionFolder (folder);
}

/**
    Analyses one capture, saying whether the source was impulsive.

    analyseSessionFolder passes impulsive = false, because a host session
    normally holds music and an RT60 measured from music is a confident wrong
    answer. A session recorded specifically as one impulse into silence is the
    exception, and there was no way to tell the analyser so.

    Nothing about the measurement changes: this calls the same analyseCapture
    the folder entry point calls, with the one argument it hard-codes. No DSP,
    no telemetry, and no plugin code is involved - this lives only in the test
    DLL, and the VST3 binary is not rebuilt by it.
*/
__declspec (dllexport) int AnalyseAuroraVerbCapture (const char* wavPath,
                                                     const char* outputFolder,
                                                     int impulsive)
{
    if (wavPath == nullptr || outputFolder == nullptr)
        return 3;

    return avtest::analyseCaptureToFolder (wavPath, outputFolder, impulsive != 0);
}

/**
    Renders the Phase 5F candidate listening set into `directory`.

    Blind copies, a key and a manifest are written alongside the named files.
    The key is deliberately a separate file so it can be left unread.
*/
__declspec (dllexport) int ExportAuroraVerbMixLawCandidates (const char* directory, double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportMixLawCandidates (directory,
                                           sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the Phase 5F / FU-2 preview-trim candidate listening set.

    Four candidate base trims, four sources, Mix 30% and Mix 100%. Blind names,
    a key in a separate file, and a manifest. Nothing in the plugin changes:
    the candidates are applied as a multiply on one shared wet render.
*/
__declspec (dllexport) int ExportAuroraVerbTrimCandidates (const char* directory, double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportTrimCandidates (directory,
                                        sampleRate > 0.0 ? sampleRate : 48000.0);
}

/** Runs the Phase 6 / FU-1 modal sweep, writing its CSVs into `directory`. */
__declspec (dllexport) int RunAuroraVerbModalSweep (const char* directory, double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::runModalSweep (directory, sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Re-tracks an existing size-sweep-raw.csv into `outDirectory`.

    Nothing is rendered and the input is not written to: the peak lists are
    already the measurement, and only the tracking is redone. Separate from the
    sweep entry point so that a re-track cannot be mistaken for new data.
*/
__declspec (dllexport) int RetrackAuroraVerbSizeSweep (const char* rawCsvPath,
                                                       const char* outDirectory)
{
    if (rawCsvPath == nullptr || outDirectory == nullptr)
        return -1;

    return avtest::retrackSizeSweep (rawCsvPath, outDirectory);
}

/**
    Cross-validates the Size tracker's q-space prior into `outDirectory`.

    Leave-one-Size-out over the existing raw CSV, plus a small number of fresh
    renders at Size settings halfway between the sweep's own. Renders, so it is
    kept out of the test suites; reads the production DSP and changes nothing.
*/
__declspec (dllexport) int CrossValidateAuroraVerbSizeTracker (const char* rawCsvPath,
                                                               const char* outDirectory,
                                                               double sampleRate)
{
    if (rawCsvPath == nullptr || outDirectory == nullptr)
        return -1;

    return avtest::crossValidateSizeTracker (rawCsvPath, outDirectory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Regenerates the Phase 6 analysis layer from an existing sweep folder.

    Reads `sourceDirectory`, writes `outDirectory`. No render, no change to the
    production DSP, and the source folder is never written to.
*/
__declspec (dllexport) int RegenerateAuroraVerbPhase6 (const char* sourceDirectory,
                                                       const char* outDirectory)
{
    if (sourceDirectory == nullptr || outDirectory == nullptr)
        return -1;

    return avtest::regeneratePhase6Outputs (sourceDirectory, outDirectory);
}

/**
    Renders round one of the Phase 6 / FU-1 targeted listening set.

    Blind names, a key in a separate file, a manifest and a content-free render
    check. The production DSP runs at its shipping defaults and is not modified.
*/
__declspec (dllexport) int ExportAuroraVerbListeningSet (const char* directory,
                                                         double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPerceptualBridgeSet (directory,
                                              sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders round 1b of the listening set: Mod Depth 0 against the default,
    inside one mode at a time.
*/
__declspec (dllexport) int ExportAuroraVerbModulationSet (const char* directory,
                                                          double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportModulationPerceptualSet (directory,
                                                  sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders round 1c: the same Mod Depth question, counterbalanced.

    Three modes, each heard twice on a different excitation with the treatment
    on opposite sides of the pair, so that a real effect and a positional habit
    predict different answers.
*/
__declspec (dllexport) int ExportAuroraVerbCounterbalancedSet (const char* directory,
                                                               double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportCounterbalancedModulationSet (directory,
                                                       sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders round 2: the high band, rated absolutely.

    Eight single-file rating trials plus two whole-tail context renders. Mod
    Depth sits at the shipping value in every file and is not under test.
*/
__declspec (dllexport) int ExportAuroraVerbHighBandSet (const char* directory,
                                                        double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportHighBandListeningSet (directory,
                                               sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders round 2b: the stimulus-alone control.

    Round 2's excitations with no tank, so the excitation's own contribution to
    the metallicity ratings can be measured instead of assumed.
*/
__declspec (dllexport) int ExportAuroraVerbStimulusAnchors (const char* directory,
                                                            double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportStimulusAnchorSet (directory,
                                            sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders round 2c: the decision gate.

    Three full-band sources, each dry, at 100% wet and at the shipping Mix. Asks
    whether the metallic character is objectionable in real use; localises
    nothing.
*/
__declspec (dllexport) int ExportAuroraVerbDecisionGate (const char* directory,
                                                         double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportDecisionGateSet (directory,
                                          sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the Phase 7 damping default A/B set.

    Two damping operating points - the default the plugin shipped with before
    Phase 7 and the one it ships with now - across five sources, at the
    shipping Mix and at 100% wet, unmatched and loudness-matched. Blind
    letters, with the mapping and every per-file measurement in a key file.
*/
__declspec (dllexport) int ExportAuroraVerbDampingListening (const char* directory,
                                                             double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportDampingListeningSet (directory,
                                              sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the PHASE 8 Stage 3 listening set.

    Pre-Delay, Low Cut and High Cut, four cases over four sources, at the
    shipping Mix and wet-only where the tail is what is judged. Only case 4 -
    High Cut against High Damp - is blind, and its mapping is in a key file.
    The production cut filters and pre-delay are the ones rendered; nothing
    here is a reimplementation.
*/
__declspec (dllexport) int ExportAuroraVerbPhase8Listening (const char* directory,
                                                            double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase8ListeningSet (directory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the PHASE 9 Stage 4 targeted Attack listening set.

    Three cases over three sparse transient sources: the primary Mod-Depth-0
    periodicity risk at 100 %% wet, normal product use at the shipping Mod
    Depth and Mix, and a three-file Attack-against-Pre-Delay confirmation.
    Only the high-Attack pair is blind, and its mapping is in a key file.
*/
__declspec (dllexport) int ExportAuroraVerbPhase9Listening (const char* directory,
                                                            double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase9ListeningSet (directory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the PHASE 10 Stage 3 early-reflection listening set.

    Four cases over four sources: the large-Size handoff valley at 100 %% wet,
    discrete slap and rhythm on a sparse transient, coloration at the shipping
    Mix, and the stereo image against its own mono downmix. Only the
    primary-risk pair is blind, and its mapping is in a key file.
*/
__declspec (dllexport) int ExportAuroraVerbPhase10Listening (const char* directory,
                                                             double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase10ListeningSet (directory,
                                              sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the final PHASE 10 occupancy A/B: cellOccupancy 0.20 against 0.40.

    Four files, two blind cases - a transient at Size 100 / Early/Late 25, and
    a vocal at Size 100 / Early/Late 50. Replaces the earlier eight-file set,
    whose primary comparison the research has overtaken. The mapping is
    randomised per case and lives in a key file.
*/
__declspec (dllexport) int ExportAuroraVerbPhase10OccupancyAb (const char* directory,
                                                               double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase10OccupancyAb (directory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the four PHASE 10 final sanity files on the complete production
    candidate.

    Not blind and nothing to choose between: snare, vocal, piano and pad, each
    sweeping Early/Late from 0 to 100 %% across its length so that level
    movement - which a static render cannot show - is audible along with the
    handoff, the slap region and the ER-heavy end.
*/
__declspec (dllexport) int ExportAuroraVerbPhase10FinalSanity (const char* directory,
                                                               double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase10FinalSanity (directory,
                                             sampleRate > 0.0 ? sampleRate : 48000.0);
}

/**
    Renders the PHASE 11 narrow-band mono listening pack.

    Six files, two cases, plus a 60 %% Mix reference. Each file plays a control
    pitch, the measured failing pitch, then the control again, and each stereo
    render is paired with its own un-normalised mono fold-down.
*/
__declspec (dllexport) int ExportAuroraVerbPhase11MonoPack (const char* directory,
                                                            double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase11MonoPack (directory,
                                          sampleRate > 0.0 ? sampleRate : 48000.0);
}

/*
    Renders the PHASE 12 Stage 2 impostor pairs: each Mode candidate at the
    product defaults beside the closest imitation another Mode can reach with
    its ordinary user controls. See Phase12Listening.h.
*/
__declspec (dllexport) int ExportAuroraVerbPhase12ImpostorPack (const char* directory,
                                                                double sampleRate)
{
    if (directory == nullptr)
        return -1;

    return avtest::exportPhase12ImpostorPack (directory,
                                              sampleRate > 0.0 ? sampleRate : 48000.0);
}

} // extern "C"
