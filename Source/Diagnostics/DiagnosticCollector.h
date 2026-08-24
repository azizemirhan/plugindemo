#pragma once

#include <atomic>
#include <cmath>
#include <cstring>

#include "DiagnosticClock.h"
#include "DiagnosticSpscQueue.h"
#include "DiagnosticTypes.h"

namespace av::diag
{

/**
    The audio thread's half of the diagnostic layer.

    This is the only part any real-time code touches, and the rules it lives by
    are the ones stated in the phase brief: it may update atomic counters, fill
    in a preallocated struct, push to a lock-free queue, copy samples into a
    preallocated ring, and read a monotonic clock. It may not allocate, lock,
    format a string, open a file, or wait for anything.

    There is no std::string here, no stream, no container that can grow, and no
    virtual function. Everything it writes is scalars into memory that prepare()
    already reserved.

    HOW A SNAPSHOT REACHES THE CONSUMER
    -----------------------------------
    By being copied into an SPSC queue, and by nothing else. The audio thread
    owns `working` outright: no other thread reads it, ever. At control rate it
    copies `working` into a queue slot and publishes that slot with a release
    store. The consumer pops slots it now owns exclusively and keeps the last
    one in its own state.

    At no point do two threads touch the same non-atomic bytes.

    This replaces a sequence lock, and the reason is worth recording. A seqlock
    publishes by bumping a counter around a plain struct copy and lets the
    reader retry when it sees the counter move. In practice it worked — measured
    over 1.8 million reads with no inconsistency — but "in practice" is the
    problem. The payload copy is a non-atomic read racing a non-atomic write,
    which is a data race, and a data race is undefined behaviour: the standard
    permits the compiler to assume it cannot happen and optimise accordingly.
    A test that does not trigger it proves the optimiser has not yet chosen to
    exploit it on this compiler and this machine, not that the code is correct.
    Passing measurements are not a substitute for a defined memory model.

    The queue costs one struct copy per control-rate tick, on a thread that was
    already going to touch that struct. It never waits, and a full queue drops
    the newest snapshot and counts it.
*/
class DiagnosticCollector
{
public:
    /**
        Snapshot queue depth.

        The consumer polls every 100 ms and the producer publishes every 10 ms,
        so ten slots would just keep up. Sixty-four gives the consumer half a
        second of grace — a scheduling hiccup, a slow disk, a debugger pause —
        before anything is dropped, and costs about 60 kB.
    */
    static constexpr size_t defaultSnapshotCapacity = 64;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate, int maximumBlockSize,
                  size_t eventCapacity, size_t captureFrameCapacity,
                  size_t snapshotCapacity = defaultSnapshotCapacity)
    {
        events.prepare (eventCapacity);
        snapshots.prepare (snapshotCapacity);
        inputRing.prepare (captureFrameCapacity);
        outputRing.prepare (captureFrameCapacity);

        working = {};
        working.host.sampleRate = sampleRate;
        working.host.prepareBlockSize = maximumBlockSize;

        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        outputFramesWritten = 0;
        captureLimitHit.store (false, std::memory_order_relaxed);
        recomputeCaptureLimit();

        // Snapshots are produced at control rate rather than per block: a
        // monitor redrawing a few times a second has no use for one per
        // callback, and the copy is the only part of this that is not free.
        snapshotIntervalSamples = std::max (1, static_cast<int> (preparedSampleRate
                                                                 * snapshotIntervalSeconds));
        samplesSinceSnapshot = snapshotIntervalSamples;   // publish on the first block

        sampleTime.store (0, std::memory_order_relaxed);

        blockBudgetNanoseconds = maximumBlockSize > 0 && sampleRate > 0.0
            ? static_cast<int64_t> (1.0e9 * maximumBlockSize / sampleRate)
            : 0;
    }

    void setLevel (Level newLevel) noexcept
    {
        level.store (static_cast<int> (newLevel), std::memory_order_relaxed);
    }

    [[nodiscard]] Level getLevel() const noexcept
    {
        return static_cast<Level> (level.load (std::memory_order_relaxed));
    }

    [[nodiscard]] bool isOff() const noexcept { return getLevel() == Level::off; }

    void setCaptureEnabled (bool enabled) noexcept
    {
        captureEnabled.store (enabled, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isCaptureEnabled() const noexcept
    {
        return captureEnabled.load (std::memory_order_relaxed);
    }

    /**
        Caps how much audio a session records.

        The first real session recorded 43 minutes and produced two 925 MB
        files, growing without any bound at all. Two things put a ceiling on
        this whatever anyone wants:

        A RIFF data chunk length is a uint32, so a WAV file cannot exceed 4 GiB
        and in practice several readers treat 2 GiB as the limit because they
        hold the offset in a signed int. At the highest rate this plugin
        supports - 192 kHz, stereo, 32-bit float - that is 1.536 MB per second,
        so the format itself runs out after about 46 minutes.

        The default here is ten minutes, which is 922 MB at 192 kHz and 265 MB
        at 44.1 kHz. That is a quarter of the format limit at the worst rate,
        comfortably inside the 2 GiB figure conservative readers assume, and
        longer than any of the round-two test plans. A byte ceiling backs it up
        for the case where somebody raises the duration.

        Reaching the limit is not an error and does not stop the session. The
        capture is finalised, telemetry carries on, and the truncation is
        reported - never silent.
    */
    void setCaptureLimitSeconds (double seconds) noexcept
    {
        captureLimitSeconds = seconds;
        recomputeCaptureLimit();
    }

    [[nodiscard]] double getCaptureLimitSeconds() const noexcept { return captureLimitSeconds; }

    [[nodiscard]] int64_t getCaptureLimitFrames() const noexcept
    {
        return captureLimitFrames.load (std::memory_order_relaxed);
    }

    [[nodiscard]] bool isCaptureLimitReached() const noexcept
    {
        return captureLimitHit.load (std::memory_order_acquire);
    }

    /**
        The packed state an event would carry if one were pushed now.

        Exposed so a test can assert what the transport flag actually says,
        rather than inferring it from an event that may not have been pushed.
        Reads the audio thread's working copy, so call it from that thread or
        from a test that owns both.
    */
    [[nodiscard]] int32_t peekStateFlags() const noexcept { return currentStateFlags(); }

    /** Ten minutes. See setCaptureLimitSeconds for why. */
    static constexpr double defaultCaptureLimitSeconds = 600.0;

    /**
        Hard byte ceiling per capture file.

        2 GiB, not the 4 GiB the format allows: the difference buys nothing and
        readers that keep a file offset in a signed 32-bit int fail above it.
    */
    static constexpr int64_t captureByteCeiling = 2LL * 1024 * 1024 * 1024;

    // -----------------------------------------------------------------------
    //  Audio thread
    // -----------------------------------------------------------------------

    /** Called at the top of the process callback. Returns the start time. */
    [[nodiscard]] DiagnosticClock::TimePoint beginBlock() noexcept
    {
        return DiagnosticClock::now();
    }

    /**
        Called at the end of the process callback.

        Everything here is arithmetic on preallocated members plus, at most, one
        queue push and two ring writes.
    */
    void endBlock (DiagnosticClock::TimePoint blockStart, int numSamples) noexcept
    {
        if (isOff())
            return;

        const int64_t elapsed = DiagnosticClock::nanosecondsBetween (blockStart,
                                                                     DiagnosticClock::now());

        auto& counters = working.counters;

        ++counters.callbackCount;
        counters.processedSamples += numSamples;
        counters.totalProcessingNanoseconds += elapsed;

        if (elapsed > counters.worstProcessingNanoseconds)
        {
            counters.worstProcessingNanoseconds = elapsed;
            counters.worstCallbackBlockSize = numSamples;
        }

        // The audio thread drops the duration into a bin. Percentiles are the
        // reporting thread's problem.
        const int bin = DiagnosticCounters::binFor (static_cast<double> (elapsed) * 1.0e-3);
        ++counters.histogram[bin];

        // The budget is the period of the block that was actually delivered.
        // Nothing here uses the prepare block size: a host that announces 512
        // and then hands over 1 gives that callback a budget of one sample, and
        // measuring it against 512 hides every overrun it causes.
        const int64_t budget = (numSamples > 0 && preparedSampleRate > 0.0)
            ? static_cast<int64_t> (1.0e9 * numSamples / preparedSampleRate)
            : 0;

        counters.lastBlockSize = numSamples;
        counters.lastProcessingNanoseconds = elapsed;
        counters.lastBudgetNanoseconds = budget;

        bool missedDeadline = false;

        if (budget > 0)
        {
            const double ratio = static_cast<double> (elapsed) / static_cast<double> (budget);

            if (ratio > counters.worstBudgetRatio)
            {
                counters.worstBudgetRatio = ratio;
                counters.worstBudgetRatioBlockSize = numSamples;
                counters.worstBudgetRatioNanoseconds = elapsed;
            }

            if (elapsed > budget)
            {
                ++counters.deadlineExceededCount;
                missedDeadline = true;
            }
        }

        auto& host = working.host;

        if (host.minObservedBlockSize == 0 || numSamples < host.minObservedBlockSize)
            host.minObservedBlockSize = numSamples;

        if (numSamples > host.maxObservedBlockSize)
            host.maxObservedBlockSize = numSamples;

        working.sampleTime = sampleTime.fetch_add (numSamples, std::memory_order_relaxed)
                             + numSamples;

        // Pushed with its own evidence attached, so the report never has to
        // infer what kind of block caused it.
        if (missedDeadline)
        {
            DiagnosticEvent event;
            event.sampleTime = working.sampleTime;
            event.type = EventType::deadlineExceeded;
            event.integerValue = numSamples;
            event.microseconds = static_cast<float> (elapsed) * 1.0e-3f;
            event.budgetMicroseconds = static_cast<float> (budget) * 1.0e-3f;
            event.value = static_cast<float> (static_cast<double> (elapsed)
                                              / static_cast<double> (budget));
            event.flags = currentStateFlags();

            if (events.push (event)) ++counters.eventsPushed;
            else                     ++counters.eventsDropped;
        }

        samplesSinceSnapshot += numSamples;

        if (samplesSinceSnapshot >= snapshotIntervalSamples)
        {
            samplesSinceSnapshot = 0;
            publish();
        }
    }

    /** Accumulates the input side. Real-time safe. */
    void observeInput (const double* left, const double* right, int numSamples) noexcept
    {
        if (isOff())
            return;

        auto& audio = working.audio;

        for (int n = 0; n < numSamples; ++n)
        {
            const double l = left[n];
            const double r = right != nullptr ? right[n] : left[n];

            audio.inputPeakLeft  = std::max (audio.inputPeakLeft, std::abs (l));
            audio.inputPeakRight = std::max (audio.inputPeakRight, std::abs (r));

            audio.inputSumSquaresLeft  += l * l;
            audio.inputSumSquaresRight += r * r;
        }

        if (isCapturing())
        {
            const int64_t limit = captureLimitFrames.load (std::memory_order_relaxed);
            const int64_t already = working.counters.captureFramesWritten;

            // One comparison and, at most once per session, one event push.
            // No waiting, no allocation, and the audio path costs the same
            // after the limit as before it.
            if (limit > 0 && already >= limit)
            {
                if (! working.counters.captureLimitReached)
                {
                    working.counters.captureLimitReached = true;
                    working.counters.captureLimitFrames = limit;
                    captureLimitHit.store (true, std::memory_order_release);

                    DiagnosticEvent event;
                    event.sampleTime = sampleTime.load (std::memory_order_relaxed);
                    event.type = EventType::captureLimitReached;
                    event.value = static_cast<float> (limit);
                    event.integerValue = static_cast<int32_t> (limit / 1000);
                    event.flags = currentStateFlags();

                    if (events.push (event)) ++working.counters.eventsPushed;
                    else                     ++working.counters.eventsDropped;
                }
            }
            else
            {
                // Never write past the limit, so the file stops on an exact
                // frame boundary rather than wherever a block happened to land.
                const auto room = limit > 0
                    ? static_cast<size_t> (std::max<int64_t> (0, limit - already))
                    : static_cast<size_t> (numSamples);

                const auto wanted = std::min (static_cast<size_t> (numSamples), room);

                const size_t written = inputRing.write (left, right != nullptr ? right : left,
                                                        wanted);

                working.counters.captureFramesWritten += static_cast<int64_t> (written);
                working.counters.captureFramesDropped +=
                    static_cast<int64_t> (wanted - written);
            }
        }
    }

    /**
        Accumulates the output side.

        `rawPeak` is the internal peak before whatever output gain the caller
        applies, so a gain-staging problem cannot be hidden by the trim that
        comes after it.
    */
    void observeOutput (const double* left, const double* right, int numSamples,
                        double rawPeak) noexcept
    {
        if (isOff())
            return;

        auto& audio = working.audio;

        audio.rawInternalPeak = std::max (audio.rawInternalPeak, rawPeak);

        bool blockIsSilent = true;

        // Stereo correlation, mono downmix and DC are level 2 measurements, and
        // splitting the loop keeps them out of level 1 rather than computing
        // them and discarding them. Measured at 192 kHz, folding them into the
        // lightweight path cost about twice what the rest of it did.
        const bool wantStereoMetrics = getLevel() == Level::full;

        for (int n = 0; n < numSamples; ++n)
        {
            const double l = left[n];
            const double r = right != nullptr ? right[n] : left[n];

            if (std::isnan (l) || std::isnan (r)) ++audio.nanCount;
            if (std::isinf (l) || std::isinf (r)) ++audio.infCount;

            if (isSubnormalValue (l) || isSubnormalValue (r))
                ++audio.subnormalCount;

            const double absL = std::abs (l), absR = std::abs (r);

            if (absL > 1.0) ++audio.clippedCount;
            if (absR > 1.0) ++audio.clippedCount;

            audio.outputPeakLeft  = std::max (audio.outputPeakLeft, absL);
            audio.outputPeakRight = std::max (audio.outputPeakRight, absR);

            audio.outputSumSquaresLeft  += l * l;
            audio.outputSumSquaresRight += r * r;

            if (absL > silenceThreshold || absR > silenceThreshold)
                blockIsSilent = false;
        }

        if (wantStereoMetrics)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                const double l = left[n];
                const double r = right != nullptr ? right[n] : left[n];

                audio.outputSumProduct += l * r;

                const double mono = 0.5 * (l + r);
                audio.outputSumMonoSquares += mono * mono;

                audio.outputSumLeft  += l;
                audio.outputSumRight += r;
            }
        }

        audio.sampleCount += numSamples;

        if (rawPeak > 1.0)
            ++audio.overUnityCount;

        if (blockIsSilent)
        {
            ++audio.silentBlockCount;
            consecutiveSilence += numSamples;
            audio.longestSilenceSamples = std::max (audio.longestSilenceSamples,
                                                    consecutiveSilence);
        }
        else
        {
            consecutiveSilence = 0;
        }

        if (isCapturing() && ! working.counters.captureLimitReached)
        {
            const int64_t limit = captureLimitFrames.load (std::memory_order_relaxed);

            // The output side mirrors the input side exactly, including where
            // it stops, so the two files always hold the same frame count and
            // line up sample for sample.
            const auto room = limit > 0
                ? static_cast<size_t> (std::max<int64_t> (0, limit - outputFramesWritten))
                : static_cast<size_t> (numSamples);

            const auto wanted = std::min (static_cast<size_t> (numSamples), room);

            const size_t written = outputRing.write (left, right != nullptr ? right : left,
                                                     wanted);

            outputFramesWritten += static_cast<int64_t> (written);

            // Input and output rings are the same size and are written the same
            // number of times, so counting the output side would double-count.
        }
    }

    /** Copies the current DSP state in. Called at control rate, not per sample. */
    void observeDsp (const DiagnosticDspState& state) noexcept
    {
        if (isOff())
            return;

        working.dsp = state;
    }

    /**
        Updates only the transport, from the audio thread, every callback.

        Separate from observeHost because that one replaces the whole struct and
        is a message-thread call made once at prepare. Everything time-varying
        has to be written where time varies, or it is frozen - which is how a
        whole session came to report transport_playing=false while it played.

        Three plain writes to memory this thread owns. Nothing to allocate and
        nothing to lock.
    */
    void observeTransport (bool playing, bool offlineRendering) noexcept
    {
        if (isOff())
            return;

        working.host.transportAvailable = true;
        working.host.transportPlaying   = playing;
        working.host.offlineRendering   = offlineRendering;
    }

    void observeHost (const DiagnosticHostState& state) noexcept
    {
        if (isOff())
            return;

        // The observed block-size range is the collector's own record and must
        // survive a host update that does not know about it.
        const int32_t minObserved = working.host.minObservedBlockSize;
        const int32_t maxObserved = working.host.maxObservedBlockSize;

        working.host = state;
        working.host.minObservedBlockSize = minObserved;
        working.host.maxObservedBlockSize = maxObserved;
    }

    /**
        Pushes an event. Never waits.

        A full queue increments the drop counter and returns; the audio thread
        pays the same cost either way.
    */
    /**
        pushEvent with extra flag bits OR-ed onto the current state.

        Used where an event carries a classification of its own - a seek reason,
        for instance - that the plugin state cannot supply.
    */
    void pushEventWithFlags (EventType type, ParameterId parameter,
                             float value, float previous,
                             int32_t integerValue, int32_t extraFlags) noexcept
    {
        if (isOff())
            return;

        DiagnosticEvent event;
        event.sampleTime = sampleTime.load (std::memory_order_relaxed);
        event.type = type;
        event.parameter = parameter;
        event.value = value;
        event.previous = previous;
        event.integerValue = integerValue;
        event.flags = currentStateFlags() | extraFlags;

        if (events.push (event)) ++working.counters.eventsPushed;
        else                     ++working.counters.eventsDropped;
    }

    void pushEvent (EventType type, ParameterId parameter = ParameterId::none,
                    float value = 0.0f, float previous = 0.0f,
                    int32_t integerValue = 0) noexcept
    {
        if (isOff())
            return;

        DiagnosticEvent event;
        event.sampleTime = sampleTime.load (std::memory_order_relaxed);
        event.type = type;
        event.parameter = parameter;
        event.value = value;
        event.previous = previous;
        event.integerValue = integerValue;

        // Stamped here, not only in the two places that build an event by hand.
        //
        // Deadline and capture-limit events set their own flags, so those two
        // read correctly while every event pushed through this function - every
        // parameter change, every morph, every transport edge - carried flags
        // of zero. That decodes as "stopped, capture off, telemetry level off",
        // which is not a missing field but a wrong answer: the first round-two
        // session reported telemetry_level "off" on 1417 parameter events
        // recorded at level full.
        event.flags = currentStateFlags();

        if (events.push (event))
            ++working.counters.eventsPushed;
        else
            ++working.counters.eventsDropped;
    }

    // -----------------------------------------------------------------------
    //  Consumer side — the reporting thread only
    // -----------------------------------------------------------------------

    /**
        Drains every queued snapshot and keeps the most recent.

        CONSUMER THREAD ONLY. The bytes it writes into `destination` came out of
        a queue slot the producer has already released, so nothing else is
        touching them. Returns false when nothing new has arrived, in which case
        `destination` is untouched and the caller keeps whatever it had.

        Draining rather than popping one keeps the queue from backing up when a
        poll interval covers several control ticks, and means the consumer
        always works from the newest state rather than crawling through history
        it has no use for. Events are a different matter — those are drained
        individually, because each one is a fact about a moment.
    */
    [[nodiscard]] bool popLatestSnapshot (DiagnosticSnapshot& destination) noexcept
    {
        DiagnosticSnapshot item;
        bool gotOne = false;

        while (snapshots.pop (item))
        {
            destination = item;
            gotOne = true;
        }

        // Taken from the atomics rather than from the snapshot, so an overflow
        // is reported even when the snapshot that would have carried the news
        // is the one that got dropped.
        if (gotOne)
        {
            destination.counters.snapshotsPublished = publishedCount.load (std::memory_order_relaxed);
            destination.counters.snapshotsDropped = droppedCount.load (std::memory_order_relaxed);
        }

        return gotOne;
    }

    /** Snapshot counters without needing a snapshot. Any thread. */
    [[nodiscard]] int64_t getSnapshotsPublished() const noexcept
    {
        return publishedCount.load (std::memory_order_relaxed);
    }

    [[nodiscard]] int64_t getSnapshotsDropped() const noexcept
    {
        return droppedCount.load (std::memory_order_relaxed);
    }

    [[nodiscard]] DiagnosticSpscQueue<DiagnosticEvent>& getEventQueue() noexcept { return events; }
    [[nodiscard]] DiagnosticSpscQueue<DiagnosticSnapshot>& getSnapshotQueue() noexcept { return snapshots; }

    /** Set in prepare(), never written afterwards. Safe from any thread. */
    [[nodiscard]] double getSampleRate() const noexcept { return preparedSampleRate; }
    [[nodiscard]] DiagnosticAudioRing& getInputRing()  noexcept { return inputRing; }
    [[nodiscard]] DiagnosticAudioRing& getOutputRing() noexcept { return outputRing; }

    [[nodiscard]] int64_t getBlockBudgetNanoseconds() const noexcept
    {
        return blockBudgetNanoseconds;
    }

    /** Only with the audio thread stopped. */
    void reset() noexcept
    {
        working.audio = {};
        working.counters = {};
        working.sampleTime = 0;

        consecutiveSilence = 0;
        samplesSinceSnapshot = snapshotIntervalSamples;
        snapshotsPublished = 0;
        snapshotsDropped = 0;
        outputFramesWritten = 0;
        captureLimitHit.store (false, std::memory_order_relaxed);

        publishedCount.store (0, std::memory_order_relaxed);
        droppedCount.store (0, std::memory_order_relaxed);

        sampleTime.store (0, std::memory_order_relaxed);

        events.reset();
        snapshots.reset();
        inputRing.reset();
        outputRing.reset();

        publish();
    }

private:
    [[nodiscard]] bool isCapturing() const noexcept
    {
        return getLevel() == Level::full && isCaptureEnabled();
    }

    /** The plugin's state at this instant, packed for an event. */
    [[nodiscard]] int32_t currentStateFlags() const noexcept
    {
        int32_t flags = eventFlags::packLevel (static_cast<int> (getLevel()));

        if (working.host.transportPlaying)      flags |= eventFlags::transportPlaying;
        if (working.host.offlineRendering)      flags |= eventFlags::offlineRendering;
        if (working.dsp.morphActive)            flags |= eventFlags::morphActive;
        if (isCaptureEnabled())                 flags |= eventFlags::captureEnabled;
        if (working.counters.captureLimitReached) flags |= eventFlags::captureLimited;

        return flags;
    }

    void recomputeCaptureLimit() noexcept
    {
        if (captureLimitSeconds <= 0.0)
        {
            captureLimitFrames.store (0, std::memory_order_relaxed);
            return;
        }

        const auto byDuration = static_cast<int64_t> (captureLimitSeconds * preparedSampleRate);

        // Stereo 32-bit float is 8 bytes a frame; the byte ceiling wins if the
        // duration would take the file past it.
        const int64_t byBytes = (captureByteCeiling - 44) / 8;

        captureLimitFrames.store (std::min (byDuration, byBytes), std::memory_order_relaxed);
    }

    [[nodiscard]] static bool isSubnormalValue (double x) noexcept
    {
        return x != 0.0 && std::abs (x) < 2.2250738585072014e-308;
    }

    /**
        Copies the audio thread's own state into a queue slot.

        The counters written here are the ones the consumer will report, so the
        snapshot's drop count has to describe the queue *before* this push,
        which is why it is stamped into the copy rather than into `working`.
    */
    void publish() noexcept
    {
        DiagnosticSnapshot outgoing = working;
        outgoing.counters.snapshotsPublished = snapshotsPublished;
        outgoing.counters.snapshotsDropped = snapshotsDropped;

        if (snapshots.push (outgoing))
        {
            ++snapshotsPublished;
        }
        else
        {
            // The consumer has fallen behind. The newest snapshot is dropped
            // and the oldest kept, because taking the oldest would mean the
            // producer popping — and only the consumer may pop, or the queue
            // stops being single-producer single-consumer and the whole
            // argument for its safety collapses.
            ++snapshotsDropped;
        }

        working.counters.snapshotsPublished = snapshotsPublished;
        working.counters.snapshotsDropped = snapshotsDropped;

        // Also published where the queue cannot swallow them.
        //
        // These counters exist to say "the queue overflowed", and the queue is
        // the one channel that cannot carry that message: once it is full, no
        // snapshot describing the overflow can get in, so the consumer's newest
        // snapshot reports zero drops forever. The first version of this had
        // exactly that hole — a test that overflowed a four-slot queue several
        // hundred times read back a drop count of nought.
        //
        // A lone atomic integer is not a data race, so this is a defined and
        // very cheap way out.
        publishedCount.store (snapshotsPublished, std::memory_order_relaxed);
        droppedCount.store (snapshotsDropped, std::memory_order_relaxed);
    }

    static constexpr double silenceThreshold = 1.0e-7;

    /** How often a snapshot is produced. A monitor cannot use more. */
    static constexpr double snapshotIntervalSeconds = 0.010;

    /**
        Owned by the audio thread and by nothing else.

        No other thread reads this. Everything the consumer sees is a copy that
        travelled through the queue, which is what keeps the whole structure
        free of concurrent access to non-atomic memory.
    */
    DiagnosticSnapshot working;

    std::atomic<int64_t> sampleTime { 0 };
    std::atomic<int> level { static_cast<int> (Level::off) };
    std::atomic<bool> captureEnabled { false };

    DiagnosticSpscQueue<DiagnosticEvent> events;
    DiagnosticSpscQueue<DiagnosticSnapshot> snapshots;
    DiagnosticAudioRing inputRing;
    DiagnosticAudioRing outputRing;

    int64_t consecutiveSilence = 0;
    int64_t blockBudgetNanoseconds = 0;

    /** Audio-thread only. */
    int64_t snapshotsPublished = 0;
    int64_t snapshotsDropped = 0;

    /** The same two figures, on a path the queue cannot block. */
    std::atomic<int64_t> publishedCount { 0 };
    std::atomic<int64_t> droppedCount { 0 };

    int  snapshotIntervalSamples = 480;
    int  samplesSinceSnapshot = 0;
    double preparedSampleRate = 48000.0;

    double captureLimitSeconds = defaultCaptureLimitSeconds;
    std::atomic<int64_t> captureLimitFrames { 0 };
    std::atomic<bool> captureLimitHit { false };

    /** Audio-thread only. Mirrors the input side so the two files match. */
    int64_t outputFramesWritten = 0;
};

} // namespace av::diag
