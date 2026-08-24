#pragma once

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "DiagnosticCollector.h"
#include "DiagnosticReport.h"
#include "DiagnosticWav.h"

namespace av::diag
{

/**
    The consumer side: one background thread that drains everything the audio
    thread produced and writes it to disk.

    This is the only place that opens a file, and it is never the audio thread.
    The separation is the whole design: the audio thread fills preallocated
    memory and moves on, and this thread does the work that would have made it
    miss a deadline.

    THREADING
    ---------
    Exactly one instance of this owns the consumer end of one collector's queue
    and rings. It is the single consumer the SPSC contract requires. It sleeps
    between polls rather than spinning, because the data it is draining is
    seconds of session and there is nothing to be gained by burning a core.

    SHUTDOWN
    --------
    stop() asks the thread to finish, waits for it, drains whatever is left in
    the queues, and writes the final report. The audio thread may still be
    running when stop() is called — a host can and does destroy a plugin while
    audio is in flight — so the collector stays alive until after the thread has
    joined.
*/
class DiagnosticSessionWriter
{
public:
    DiagnosticSessionWriter() = default;

    ~DiagnosticSessionWriter() { stop(); }

    DiagnosticSessionWriter (const DiagnosticSessionWriter&) = delete;
    DiagnosticSessionWriter& operator= (const DiagnosticSessionWriter&) = delete;

    /**
        Creates the session folder and starts the writer thread.

        Returns false if the folder could not be created or is not writable, in
        which case nothing is started and the plugin carries on without
        diagnostics rather than failing.
    */
    [[nodiscard]] bool start (DiagnosticCollector& collectorToUse,
                              const DiagnosticSessionInfo& sessionInfo,
                              const std::string& rootDirectory,
                              bool captureAudio);

    /** Drains, writes the final report, and joins. Safe to call twice. */
    void stop();

    [[nodiscard]] bool isRunning() const noexcept { return running.load (std::memory_order_acquire); }
    [[nodiscard]] const std::string& getSessionPath() const noexcept { return sessionPath; }
    [[nodiscard]] const std::string& getLastError() const noexcept { return lastError; }

    /** Verdict of the last completed session. */
    [[nodiscard]] Verdict getVerdict() const noexcept { return finalVerdict; }

    /**
        Does one poll's worth of work synchronously.

        Exposed so the tests can drive the consumer deterministically instead of
        sleeping and hoping. The background thread calls exactly this.
    */
    void pump();

    /** Interval between polls. */
    static constexpr int pollMilliseconds = 100;

private:
    void threadMain();
    void writeFinalReport();

    DiagnosticCollector* collector = nullptr;
    DiagnosticSessionInfo info;

    std::string sessionPath;
    std::string lastError;

    std::thread thread;
    std::atomic<bool> running { false };
    std::atomic<bool> shouldStop { false };

    std::unique_ptr<WavWriter> inputWav;
    std::unique_ptr<WavWriter> outputWav;

    std::vector<float> transferBuffer;

    std::ofstream timeline;
    std::ofstream eventLog;

    /**
        Consumer-owned. Written only by this thread, in pump() and in
        writeFinalReport() after the thread has joined. The watcher and the
        report builder read this and never the collector's own state.
    */
    DiagnosticSnapshot latest;
    bool haveSnapshot = false;

    Verdict finalVerdict = Verdict::pass;
};

/**
    Owns a collector and its writer, and decides whether either exists.

    The controller is what the processor holds. Everything above it is
    compile-time guarded, so in a build without diagnostics this class is not
    instantiated at all and the plugin carries no diagnostic code path.
*/
class DiagnosticController
{
public:
    /** Allocates. Message thread only. */
    void prepare (double sampleRate, int maximumBlockSize);

    void setLevel (Level level);
    [[nodiscard]] Level getLevel() const noexcept { return collector.getLevel(); }

    void setCaptureEnabled (bool enabled);
    [[nodiscard]] bool isCaptureEnabled() const noexcept { return collector.isCaptureEnabled(); }

    /** Seconds of audio a session records before it stops. See the collector. */
    void setCaptureLimitSeconds (double seconds) { collector.setCaptureLimitSeconds (seconds); }
    [[nodiscard]] double getCaptureLimitSeconds() const noexcept
    {
        return collector.getCaptureLimitSeconds();
    }

    /** Starts a session. Message thread only. Returns false if it could not. */
    [[nodiscard]] bool startSession (const DiagnosticSessionInfo& info);

    void stopSession();

    [[nodiscard]] bool isSessionRunning() const noexcept { return writer.isRunning(); }
    [[nodiscard]] std::string getSessionPath() const { return writer.getSessionPath(); }

    [[nodiscard]] DiagnosticCollector& getCollector() noexcept { return collector; }
    [[nodiscard]] const DiagnosticCollector& getCollector() const noexcept { return collector; }

    /** How many events and capture frames prepare() reserves. */
    static constexpr size_t eventCapacity = 8192;
    static constexpr double captureSeconds = 4.0;

private:
    DiagnosticCollector collector;
    DiagnosticSessionWriter writer;

    double preparedSampleRate = 48000.0;
};

} // namespace av::diag
