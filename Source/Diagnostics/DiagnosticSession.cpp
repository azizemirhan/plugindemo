#include "DiagnosticSession.h"

#include "DiagnosticPaths.h"

#include <algorithm>
#include <chrono>

namespace av::diag
{

bool DiagnosticSessionWriter::start (DiagnosticCollector& collectorToUse,
                                     const DiagnosticSessionInfo& sessionInfo,
                                     const std::string& rootDirectory,
                                     bool captureAudio)
{
    stop();

    collector = &collectorToUse;
    info = sessionInfo;
    lastError.clear();

    const std::string folder = paths::sessionFolderName (info.startTimestamp, info.instanceId);
    sessionPath = paths::join (rootDirectory, folder);

    if (! paths::createDirectories (sessionPath))
    {
        lastError = "could not create " + sessionPath;
        sessionPath.clear();
        return false;
    }

    if (! paths::isWritable (sessionPath))
    {
        // Proved by writing a probe file, so a read-only location or a full
        // disk fails here rather than halfway through a session.
        lastError = "not writable: " + sessionPath;
        sessionPath.clear();
        return false;
    }

    info.sessionPath = sessionPath;

    timeline.open (paths::join (sessionPath, "timeline.csv"), std::ios::binary | std::ios::trunc);

    if (! timeline.is_open())
    {
        lastError = "could not open timeline.csv";
        sessionPath.clear();
        return false;
    }

    timeline << timelineHeader();

    eventLog.open (paths::join (sessionPath, "events.jsonl"), std::ios::binary | std::ios::trunc);

    if (! eventLog.is_open())
    {
        lastError = "could not open events.jsonl";
        timeline.close();
        sessionPath.clear();
        return false;
    }

    if (captureAudio)
    {
        inputWav = std::make_unique<WavWriter>();
        outputWav = std::make_unique<WavWriter>();

        // Taken from the collector's prepared rate rather than from a snapshot:
        // it is fixed at prepare() and never written again, so reading it here
        // needs no synchronisation and does not depend on the audio thread
        // having produced anything yet.
        const double sampleRate = collector->getSampleRate();

        if (! inputWav->open (paths::join (sessionPath, "input.wav"), sampleRate, 2)
            || ! outputWav->open (paths::join (sessionPath, "output.wav"), sampleRate, 2))
        {
            lastError = "could not open capture files";
            inputWav.reset();
            outputWav.reset();
        }
    }

    // Sized for one poll's worth of audio at a generous rate, so the transfer
    // never has to grow while the session is running.
    transferBuffer.assign (static_cast<size_t> (192000 * 2), 0.0f);

    haveSnapshot = false;
    shouldStop.store (false, std::memory_order_release);
    running.store (true, std::memory_order_release);

    thread = std::thread ([this] { threadMain(); });

    return true;
}

void DiagnosticSessionWriter::threadMain()
{
    while (! shouldStop.load (std::memory_order_acquire))
    {
        pump();
        std::this_thread::sleep_for (std::chrono::milliseconds (pollMilliseconds));
    }

    // One last pass, so whatever the audio thread produced between the final
    // poll and the stop request is not lost.
    pump();
}

void DiagnosticSessionWriter::pump()
{
    if (collector == nullptr)
        return;

    // --- Events ---
    DiagnosticEvent event;

    const double sampleRate = haveSnapshot && latest.host.sampleRate > 0.0
                            ? latest.host.sampleRate : 48000.0;

    while (collector->getEventQueue().pop (event))
    {
        if (eventLog.is_open())
            eventLog << eventJsonLine (event, sampleRate) << "\n";
    }

    // --- Snapshots and timeline ---
    //
    // `latest` belongs to this thread. Nothing else reads or writes it, and the
    // bytes in it came out of a queue slot the producer had already released.
    if (collector->popLatestSnapshot (latest))
    {
        haveSnapshot = true;

        if (timeline.is_open())
            timeline << timelineRow (latest, computeStatistics (latest));
    }

    // --- Audio ---
    const auto drain = [this] (DiagnosticAudioRing& ring, WavWriter* writer)
    {
        if (writer == nullptr)
        {
            // Capture is off, but the ring may still hold frames from before it
            // was turned off. Discarding them keeps the ring from staying full
            // and making the audio thread count drops for a capture nobody
            // asked for.
            while (ring.available() > 0)
                (void) ring.read (transferBuffer.data(),
                                  std::min (ring.available(), transferBuffer.size() / 2));

            return;
        }

        while (true)
        {
            const size_t wanted = std::min (ring.available(), transferBuffer.size() / 2);

            if (wanted == 0)
                break;

            const size_t got = ring.read (transferBuffer.data(), wanted);

            if (got == 0)
                break;

            (void) writer->writeFrames (transferBuffer.data(), got);
        }
    };

    drain (collector->getInputRing(), inputWav.get());
    drain (collector->getOutputRing(), outputWav.get());

    if (timeline.is_open())
        timeline.flush();

    if (eventLog.is_open())
        eventLog.flush();
}

void DiagnosticSessionWriter::stop()
{
    if (! running.load (std::memory_order_acquire))
        return;

    shouldStop.store (true, std::memory_order_release);

    if (thread.joinable())
        thread.join();

    running.store (false, std::memory_order_release);

    writeFinalReport();

    if (inputWav != nullptr)  { (void) inputWav->close();  inputWav.reset(); }
    if (outputWav != nullptr) { (void) outputWav->close(); outputWav.reset(); }

    if (timeline.is_open())
        timeline.close();

    if (eventLog.is_open())
        eventLog.close();

    collector = nullptr;
}

void DiagnosticSessionWriter::writeFinalReport()
{
    if (sessionPath.empty())
        return;

    // The thread has already joined, so this is the only thread left touching
    // the queue: draining it here flushes whatever the audio thread produced
    // after the last poll into the final report.
    if (collector != nullptr)
        (void) collector->popLatestSnapshot (latest);

    const DiagnosticSnapshot& snapshot = latest;

    const auto statistics = computeStatistics (snapshot);
    const auto warnings = evaluate (snapshot, statistics);

    finalVerdict = verdictFor (warnings);

    {
        std::ofstream json (paths::join (sessionPath, "session.json"),
                            std::ios::binary | std::ios::trunc);

        if (json.is_open())
            json << buildSessionJson (info, snapshot, statistics, warnings);
    }

    {
        std::ofstream summary (paths::join (sessionPath, "summary.txt"),
                               std::ios::binary | std::ios::trunc);

        if (summary.is_open())
            summary << buildSummaryText (info, snapshot, statistics, warnings);
    }

    {
        std::ofstream environment (paths::join (sessionPath, "environment.txt"),
                                   std::ios::binary | std::ios::trunc);

        if (environment.is_open())
        {
            environment << "AuroraVerb diagnostic environment\n";
            environment << "=================================\n\n";
            environment << "schema version:  " << schemaVersion << "\n";
            environment << "build:           " << info.buildConfiguration << "\n";
            environment << "architecture:    " << info.architectureName << "\n";
            environment << "diagnostics:     " << (info.diagnosticsCompiledIn ? "compiled in" : "absent") << "\n";
            environment << "engine:          " << (info.engineActive ? "FDN active"
                                                                    : "NONE - identity wet path") << "\n";
            environment << "build profile:   " << info.buildProfile << "\n";
            environment << "telemetry level: " << describe (info.level) << "\n";
            environment << "capture:         " << (info.captureEnabled ? "enabled" : "disabled") << "\n";
            // Through the same function summary.txt uses. The first session
            // reported "FL Studio unavailable" here and "FL Studio (version
            // unavailable)" there, which reads as two different facts when it
            // is one; writing the sentence twice is how that happened, so it is
            // written once now.
            environment << "host:            " << describeHost (info) << "\n";
            environment << "format:          " << info.pluginFormat << "\n";
            environment << "session path:    " << sessionPath << "\n";
        }
    }
}

// ===========================================================================
//  Controller
// ===========================================================================

void DiagnosticController::prepare (double sampleRate, int maximumBlockSize)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Sized from the sample rate so the capture ring holds the same amount of
    // time whatever the host is running at. Allocated here, never later.
    const auto captureFrames = static_cast<size_t> (captureSeconds * preparedSampleRate);

    collector.prepare (preparedSampleRate, maximumBlockSize, eventCapacity, captureFrames);
}

void DiagnosticController::setLevel (Level level)
{
    // Everything the level switch could need is already allocated, so changing
    // it is one relaxed store and cannot allocate on any thread.
    collector.setLevel (level);
}

void DiagnosticController::setCaptureEnabled (bool enabled)
{
    collector.setCaptureEnabled (enabled);
}

bool DiagnosticController::startSession (const DiagnosticSessionInfo& sessionInfo)
{
    auto info = sessionInfo;
    info.level = collector.getLevel();
    info.captureEnabled = collector.isCaptureEnabled();

    if (info.startTimestamp.empty())
        info.startTimestamp = paths::timestampNow();

    const bool started = writer.start (collector, info, paths::diagnosticRoot(),
                                       collector.isCaptureEnabled());

    if (started)
        collector.pushEvent (EventType::sessionStart);

    return started;
}

void DiagnosticController::stopSession()
{
    if (! writer.isRunning())
        return;

    collector.pushEvent (EventType::sessionStop);
    writer.stop();
}

} // namespace av::diag
