#pragma once

#include "GainStaging.h"

#include <string>
#include <vector>

/**
    Deterministic dry/wet renders, for listening before a DAW session exists.

    PHASE 5C's last goal is a preview that can be taken into FL Studio and
    judged by ear. Before that, it helps to have the same material rendered
    offline: a WAV pair can be looked at, levelled, and compared between
    architectures without a host in the loop, and it is reproducible in a way a
    live session is not.

    This is developer tooling and lives only in the test target. Nothing here is
    reachable from the plugin, and the production VST3 gains no file-writing
    code from it. It never runs as part of the suites either — writing files is
    not something a test run should do as a side effect — so it has its own
    entry point.

    Not diagnostic telemetry. Nothing measures or logs anything about a running
    plugin; this renders fixed signals through an offline tank and writes the
    result. Telemetry is PHASE 5D's subject.
*/
namespace avtest
{

/**
    Renders the standard signal set, dry and wet, into `directory`.

    Returns the number of files written, or a negative value if the directory
    could not be written to. Deterministic: the same build produces the same
    bytes every time.
*/
int exportTestSignals (const std::string& directory, double sampleRate = 48000.0);

/**
    The vocal-like reference from the developer signal set.

    A formant synthesis of an "ah" with singer vibrato, not a recording.
    Exposed so that a listening set can use the same vocal reference the export
    set uses rather than growing a second copy of it, which would drift.

    Returned as GainStaging stereo frames; the export path keeps its own frame
    type and is unchanged by this.
*/
[[nodiscard]] std::vector<StereoFrame> makeVocalReference (double sampleRate, double seconds);

} // namespace avtest
