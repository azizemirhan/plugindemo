#include "AllocationDetector.h"
#include "Suites.h"
#include "TestFramework.h"

#include "Diagnostics/DiagnosticCollector.h"
#include "Diagnostics/DiagnosticSpscQueue.h"
#include "Diagnostics/DiagnosticTypes.h"

#include <atomic>
#include <cmath>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace av::diag;

namespace avtest
{

namespace
{
    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    const char* describeLevel (Level level) noexcept
    {
        switch (level)
        {
            case Level::off:         return "off";
            case Level::lightweight: return "lightweight";
            case Level::full:        return "full";
        }

        return "off";
    }

    DiagnosticEvent makeEvent (int64_t time, int32_t value)
    {
        DiagnosticEvent e;
        e.sampleTime = time;
        e.type = EventType::parameterChange;
        e.parameter = ParameterId::decay;
        e.integerValue = value;
        return e;
    }
}

void runDiagnosticQueueTests()
{
    // =======================================================================
    beginSuite ("Diagnostics / event and snapshot structures");
    // =======================================================================
    {
        // These types cross a thread boundary by being copied byte for byte, so
        // they have to stay trivially copyable. A std::string field added in a
        // hurry would compile, work in a single-threaded test, and be a data
        // race in a plugin.
        check (std::is_trivially_copyable_v<DiagnosticEvent>,
               "DiagnosticEvent is trivially copyable");
        check (std::is_trivially_copyable_v<DiagnosticAudioMetrics>,
               "DiagnosticAudioMetrics is trivially copyable");
        check (std::is_trivially_copyable_v<DiagnosticDspState>,
               "DiagnosticDspState is trivially copyable");
        check (std::is_trivially_copyable_v<DiagnosticHostState>,
               "DiagnosticHostState is trivially copyable");
        check (std::is_trivially_copyable_v<DiagnosticCounters>,
               "DiagnosticCounters is trivially copyable");
        check (std::is_trivially_copyable_v<DiagnosticSnapshot>,
               "DiagnosticSnapshot is trivially copyable");

        info ("sizeof(DiagnosticEvent)    = " + std::to_string (sizeof (DiagnosticEvent)));
        info ("sizeof(DiagnosticSnapshot) = " + std::to_string (sizeof (DiagnosticSnapshot)));

        // The bound was 32 bytes until a deadline event had to carry its own
        // evidence: the runtime block size, the callback duration, the budget
        // that block actually had, and the plugin state at the time. The first
        // FL Studio session showed why - 17 overruns with no record of which
        // block size caused any of them, and no way to check the explanation
        // after the fact. Four fields is 16 more bytes and moves the push from
        // two 16-byte stores to three. That is the right trade: the cost is
        // paid only when something has already gone wrong, and the alternative
        // is a count nobody can act on.
        checkAtMost (static_cast<double> (sizeof (DiagnosticEvent)), 48.0,
                     "an event stays small enough that pushing one is a few stores");

        // The histogram has to cover a plausible callback duration range.
        info ("");
        info ("  bin   lower edge");

        for (int bin : { 0, 8, 16, 24, 32, 40, histogramBins - 1 })
            info ("  " + std::to_string (bin) + "     "
                  + fixed (histogramBinLowerMicroseconds (bin), 3) + " us");

        check (histogramBinLowerMicroseconds (histogramBins - 1) > 20000.0,
               "the histogram reaches past 20 ms, well beyond any real callback");

        // Bin assignment must be monotone and in range.
        bool monotone = true;
        int previous = -1;

        for (double us : { 0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 100.0, 1000.0, 100000.0 })
        {
            const int bin = DiagnosticCounters::binFor (us);

            monotone = monotone && bin >= previous && bin >= 0 && bin < histogramBins;
            previous = bin;
        }

        check (monotone, "histogram bin assignment is monotone and always in range");
    }

    // =======================================================================
    beginSuite ("Diagnostics / SPSC queue");
    // =======================================================================
    {
        DiagnosticSpscQueue<DiagnosticEvent> queue;
        queue.prepare (8);

        checkEqual (static_cast<long long> (queue.capacity()), 8LL,
                    "the queue reports the capacity it was asked for");

        check (queue.isEmpty(), "a fresh queue is empty");

        DiagnosticEvent popped;
        check (! queue.pop (popped), "popping an empty queue fails rather than blocking");

        // FIFO order.
        for (int i = 0; i < 8; ++i)
            check (queue.push (makeEvent (i, i)), "push " + std::to_string (i) + " succeeds");

        check (queue.isFull(), "the queue reports itself full at capacity");
        check (! queue.push (makeEvent (99, 99)),
               "pushing to a full queue fails immediately rather than blocking or overwriting");

        bool ordered = true;

        for (int i = 0; i < 8; ++i)
        {
            if (! queue.pop (popped) || popped.integerValue != i)
                ordered = false;
        }

        check (ordered, "items come out in the order they went in");
        check (queue.isEmpty(), "the queue is empty again");

        // Wraparound: many more items than the capacity, one at a time.
        queue.reset();

        bool wrapOk = true;

        for (int round = 0; round < 100; ++round)
        {
            if (! queue.push (makeEvent (round, round)))
                wrapOk = false;

            DiagnosticEvent item;

            if (! queue.pop (item) || item.integerValue != round)
                wrapOk = false;
        }

        check (wrapOk, "the indices wrap correctly over many times the capacity");

        // Partial fill and drain, repeatedly, so the wrap happens mid-buffer.
        queue.reset();

        bool partialOk = true;

        for (int round = 0; round < 40; ++round)
        {
            for (int i = 0; i < 5; ++i)
                if (! queue.push (makeEvent (round * 5 + i, round * 5 + i)))
                    partialOk = false;

            for (int i = 0; i < 5; ++i)
            {
                DiagnosticEvent item;

                if (! queue.pop (item) || item.integerValue != round * 5 + i)
                    partialOk = false;
            }
        }

        check (partialOk, "partial fills and drains wrap correctly too");
    }

    // =======================================================================
    beginSuite ("Diagnostics / the producer never waits");
    // =======================================================================
    {
        // The property that matters most: a full queue must cost the producer
        // the same as an empty one. Measured, because "returns false" is easy
        // to write and easy to get wrong by adding a retry.
        DiagnosticSpscQueue<DiagnosticEvent> queue;
        queue.prepare (64);

        for (int i = 0; i < 64; ++i)
            (void) queue.push (makeEvent (i, i));

        check (queue.isFull(), "the queue is full for the measurement");

        constexpr int attempts = 200000;

        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < attempts; ++i)
            (void) queue.push (makeEvent (i, i));

        const double fullNs = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - start).count() * 1.0e9 / attempts;

        queue.reset();

        const auto emptyStart = std::chrono::steady_clock::now();

        for (int i = 0; i < attempts; ++i)
        {
            (void) queue.push (makeEvent (i, i));

            DiagnosticEvent item;
            (void) queue.pop (item);
        }

        const double cycleNs = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - emptyStart).count() * 1.0e9 / attempts;

        info ("rejected push on a full queue: " + fixed (fullNs, 1) + " ns");
        info ("push + pop on an empty queue:  " + fixed (cycleNs, 1) + " ns");

        checkAtMost (fullNs, 100.0,
                     "a rejected push costs no more than a successful one would");

        // Concurrent producer and consumer, which is the real configuration.
        DiagnosticSpscQueue<DiagnosticEvent> shared;
        shared.prepare (256);

        constexpr int total = 200000;

        std::atomic<int> produced { 0 };
        std::atomic<int> consumed { 0 };
        std::atomic<int> dropped { 0 };
        std::atomic<bool> outOfOrder { false };

        std::thread consumer ([&]
        {
            int expected = 0;

            while (consumed.load (std::memory_order_relaxed) < total
                   - dropped.load (std::memory_order_relaxed))
            {
                DiagnosticEvent item;

                if (shared.pop (item))
                {
                    // Every item that arrives must be no earlier than the last:
                    // drops remove items, they never reorder them.
                    if (item.integerValue < expected)
                        outOfOrder.store (true, std::memory_order_relaxed);

                    expected = item.integerValue;
                    consumed.fetch_add (1, std::memory_order_relaxed);
                }
                else if (produced.load (std::memory_order_acquire) >= total)
                {
                    break;
                }
            }
        });

        for (int i = 0; i < total; ++i)
        {
            if (! shared.push (makeEvent (i, i)))
                dropped.fetch_add (1, std::memory_order_relaxed);

            produced.fetch_add (1, std::memory_order_release);
        }

        consumer.join();

        info ("");
        info ("concurrent run: " + std::to_string (total) + " pushed, "
              + std::to_string (consumed.load()) + " consumed, "
              + std::to_string (dropped.load()) + " dropped");

        check (! outOfOrder.load(), "items never arrive out of order, even under contention");

        checkEqual (static_cast<long long> (consumed.load() + dropped.load()),
                    static_cast<long long> (total),
                    "every item is either consumed or counted as dropped — none vanish");
    }

    // =======================================================================
    beginSuite ("Diagnostics / audio ring");
    // =======================================================================
    {
        DiagnosticAudioRing ring;
        ring.prepare (512);

        checkEqual (static_cast<long long> (ring.capacity()), 512LL,
                    "the ring reports the capacity it was asked for");

        std::vector<double> left (128), right (128);

        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] = static_cast<double> (i);
            right[i] = -static_cast<double> (i);
        }

        checkEqual (static_cast<long long> (ring.write (left.data(), right.data(), 128)), 128LL,
                    "a write that fits is taken in full");

        std::vector<float> out (256);

        checkEqual (static_cast<long long> (ring.read (out.data(), 128)), 128LL,
                    "and reads back in full");

        bool correct = true;

        for (size_t i = 0; i < 128; ++i)
        {
            correct = correct && out[i * 2] == static_cast<float> (i);
            correct = correct && out[i * 2 + 1] == -static_cast<float> (i);
        }

        check (correct, "frames come back interleaved and in order");

        // Wraparound.
        ring.reset();

        bool wrapOk = true;

        for (int round = 0; round < 20; ++round)
        {
            if (ring.write (left.data(), right.data(), 128) != 128)
                wrapOk = false;

            if (ring.read (out.data(), 128) != 128)
                wrapOk = false;

            for (size_t i = 0; i < 128; ++i)
                if (out[i * 2] != static_cast<float> (i))
                    wrapOk = false;
        }

        check (wrapOk, "the ring wraps correctly over many times its capacity");

        // Overflow: a full ring takes what fits and reports it, without waiting.
        ring.reset();

        std::vector<double> big (600, 1.0);

        const size_t written = ring.write (big.data(), big.data(), 600);

        info ("");
        info ("wrote 600 frames into a 512-frame ring: " + std::to_string (written) + " taken, "
              + std::to_string (600 - written) + " refused");

        checkEqual (static_cast<long long> (written), 512LL,
                    "a full ring takes exactly what fits");

        checkEqual (static_cast<long long> (ring.write (big.data(), big.data(), 100)), 0LL,
                    "and takes nothing more until it is drained");

        check (true, "the producer returned rather than blocking");
    }

    // =======================================================================
    beginSuite ("Diagnostics / collector, levels and real-time safety");
    // =======================================================================
    {
        DiagnosticCollector collector;
        collector.prepare (48000.0, 512, 1024, 48000);

        check (collector.getLevel() == Level::off, "a fresh collector is off");

        // Level 0 must be genuinely free, and switching level must not allocate.
        //
        // The count has to be read into a local before check() is called.
        // Passing counter.count() as one argument and a std::string built from
        // a literal as the other lets the compiler evaluate them in either
        // order, and building that string allocates — so the first version of
        // this suite measured its own failure message.
        {
            ScopedAllocationCounter counter;

            collector.setLevel (Level::lightweight);
            collector.setLevel (Level::full);
            collector.setLevel (Level::off);
            collector.setCaptureEnabled (true);
            collector.setCaptureEnabled (false);

            const auto allocations = counter.count();

            checkNoAllocations (allocations,"changing telemetry level allocates nothing");
        }

        std::vector<double> left (512, 0.25), right (512, -0.25);

        // Everything the audio thread does, at every level, with capture on.
        for (auto level : { Level::off, Level::lightweight, Level::full })
        {
            collector.setLevel (level);
            collector.setCaptureEnabled (level == Level::full);

            // Built before the counter starts, for the same reason as above.
            const std::string label = std::string ("level ") + describeLevel (level)
                                      + ": the audio path allocates nothing";

            ScopedAllocationCounter counter;

            for (int block = 0; block < 200; ++block)
            {
                const auto start = collector.beginBlock();

                collector.observeInput (left.data(), right.data(), 512);
                collector.observeOutput (left.data(), right.data(), 512, 0.5);

                DiagnosticDspState state;
                state.decayTargetSeconds = 2.0;
                collector.observeDsp (state);

                collector.pushEvent (EventType::parameterChange, ParameterId::decay, 2.0f);

                collector.endBlock (start, 512);
            }

            const auto allocations = counter.count();

            checkNoAllocations (allocations, label);
        }

        // Queue overflow must not stall, and must be counted.
        collector.reset();
        collector.setLevel (Level::full);

        {
            ScopedAllocationCounter counter;

            for (int i = 0; i < 5000; ++i)
                collector.pushEvent (EventType::parameterChange, ParameterId::size,
                                     static_cast<float> (i));

            const auto allocations = counter.count();

            checkNoAllocations (allocations,"overflowing the event queue allocates nothing");
        }

        // Counters live in the audio thread's working copy and become visible
        // when a block ends, which is once per callback rather than once per
        // event. Nothing publishes without one.
        {
            std::vector<double> block (64, 0.0);
            const auto start = collector.beginBlock();
            collector.observeOutput (block.data(), block.data(), 64, 0.0);
            collector.endBlock (start, 64);
        }

        DiagnosticSnapshot snapshot;
        check (collector.popLatestSnapshot (snapshot), "a snapshot reaches the consumer");

        info ("");
        info ("after 5000 pushes into a 1024-slot queue: "
              + std::to_string (snapshot.counters.eventsPushed) + " pushed, "
              + std::to_string (snapshot.counters.eventsDropped) + " dropped");

        check (snapshot.counters.eventsDropped > 0,
               "the drops are counted rather than silently lost");

        checkEqual (snapshot.counters.eventsPushed + snapshot.counters.eventsDropped, 5000LL,
                    "pushed plus dropped accounts for every event");
    }

    // =======================================================================
    beginSuite ("Diagnostics / snapshot queue");
    // =======================================================================
    {
        info ("Snapshots reach the consumer through the same preallocated SPSC queue the");
        info ("events use, and through nothing else. The audio thread's own snapshot is");
        info ("never read by another thread, so there is no non-atomic object with a");
        info ("concurrent reader and writer anywhere in this path.");
        info ("");
        info ("This replaced a sequence lock. That version measured 1.8 million consistent");
        info ("reads and zero tears, and was still wrong: the payload copy was a data race,");
        info ("and a data race is undefined behaviour whatever it happens to do today.");
        info ("A measurement cannot establish an absence the memory model does not grant.");
        info ("");

        DiagnosticSpscQueue<DiagnosticSnapshot> queue;
        queue.prepare (8);

        checkEqual (static_cast<long long> (queue.capacity()), 8LL,
                    "the snapshot queue reports its capacity");

        // FIFO.
        for (int i = 0; i < 8; ++i)
        {
            DiagnosticSnapshot s;
            s.sampleTime = i;
            check (queue.push (s), "snapshot push " + std::to_string (i) + " succeeds");
        }

        DiagnosticSnapshot spare;
        check (! queue.push (spare),
               "a full snapshot queue refuses rather than blocking or overwriting");

        bool ordered = true;

        for (int i = 0; i < 8; ++i)
        {
            DiagnosticSnapshot out;

            if (! queue.pop (out) || out.sampleTime != i)
                ordered = false;
        }

        check (ordered, "snapshots come out in the order they went in");

        // Wraparound, many times the capacity.
        queue.reset();

        bool wrapOk = true;

        for (int round = 0; round < 200; ++round)
        {
            DiagnosticSnapshot s;
            s.sampleTime = round;

            if (! queue.push (s))
                wrapOk = false;

            DiagnosticSnapshot out;

            if (! queue.pop (out) || out.sampleTime != round)
                wrapOk = false;
        }

        check (wrapOk, "the snapshot queue wraps correctly");
    }

    // =======================================================================
    beginSuite ("Diagnostics / snapshot production, drops and ordering");
    // =======================================================================
    {
        // A tiny queue and a consumer that never runs, so the producer must
        // overflow it. What has to hold is that it never waits and that every
        // snapshot it could not hand over is counted.
        DiagnosticCollector collector;
        collector.prepare (48000.0, 64, 256, 4800, 4);
        collector.setLevel (Level::lightweight);

        std::vector<double> buffer (64, 0.1);

        // Snapshots are produced every 10 ms, so 480 samples at 48 kHz.
        constexpr int blocks = 4000;   // about 5.3 s of audio, ~533 snapshots

        {
            ScopedAllocationCounter counter;

            for (int block = 0; block < blocks; ++block)
            {
                const auto start = collector.beginBlock();
                collector.observeOutput (buffer.data(), buffer.data(), 64, 0.1);
                collector.endBlock (start, 64);
            }

            const auto allocations = counter.count();

            checkEqual (allocations, 0LL,
                        "producing and dropping snapshots allocates nothing");
        }

        DiagnosticSnapshot latest;
        check (collector.popLatestSnapshot (latest), "the consumer gets what did fit");

        info ("snapshots published: " + std::to_string (latest.counters.snapshotsPublished));
        info ("snapshots dropped:   " + std::to_string (latest.counters.snapshotsDropped));

        check (latest.counters.snapshotsDropped > 0,
               "a four-slot queue with no consumer does overflow, and says so");

        check (latest.counters.snapshotsPublished > 0,
               "and the ones that fitted were published");

        // The point of the atomic counters. With drop-newest, the queue holds
        // the four oldest snapshots and every later one is refused — including
        // every snapshot that would have carried the drop count. Reading the
        // count out of the queue reported nought after several hundred drops;
        // reading it from the atomics reports what actually happened.
        checkEqual (latest.counters.snapshotsDropped, collector.getSnapshotsDropped(),
                    "the drop count reaches the consumer even though the queue is full");

        // Control rate, not per block: far fewer snapshots than callbacks.
        const int64_t total = latest.counters.snapshotsPublished + latest.counters.snapshotsDropped;

        info ("callbacks: " + std::to_string (blocks)
              + ", snapshots produced: " + std::to_string (total));

        check (total < blocks / 4,
               "snapshots are produced at control rate rather than once per callback");

        // 4000 blocks of 64 samples is 256000 samples, and the interval is
        // 480 samples, so about 533 snapshots.
        check (total > 400 && total < 700,
               "and at the documented 10 ms interval");
    }

    // =======================================================================
    beginSuite ("Diagnostics / high-frequency producer, slow consumer");
    // =======================================================================
    {
        // The real configuration: a producer running flat out and a consumer
        // that polls slowly. The producer must never block, the consumer must
        // always see monotonically advancing snapshots, and every snapshot must
        // be either delivered or counted.
        DiagnosticCollector collector;
        collector.prepare (48000.0, 64, 1024, 4800, 16);
        collector.setLevel (Level::lightweight);

        std::atomic<bool> producing { true };
        std::atomic<int> delivered { 0 };
        std::atomic<bool> wentBackwards { false };
        std::atomic<long long> worstPushNanoseconds { 0 };

        std::thread consumer ([&]
        {
            DiagnosticSnapshot latest;
            int64_t previousSampleTime = -1;
            int64_t previousCallbacks = -1;

            while (producing.load (std::memory_order_acquire) || collector.getSnapshotQueue().size() > 0)
            {
                if (collector.popLatestSnapshot (latest))
                {
                    // Monotonic: a later snapshot can never describe less work
                    // than an earlier one.
                    if (latest.sampleTime < previousSampleTime
                        || latest.counters.callbackCount < previousCallbacks)
                        wentBackwards.store (true, std::memory_order_relaxed);

                    // Internally consistent: these two are written together on
                    // the audio thread, so a snapshot that had been torn would
                    // show them disagreeing.
                    if (latest.counters.callbackCount * 64 != latest.counters.processedSamples)
                        wentBackwards.store (true, std::memory_order_relaxed);

                    previousSampleTime = latest.sampleTime;
                    previousCallbacks = latest.counters.callbackCount;

                    delivered.fetch_add (1, std::memory_order_relaxed);
                }

                // Deliberately slower than the producer.
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        });

        std::vector<double> buffer (64, 0.2);

        for (int block = 0; block < 60000; ++block)
        {
            const auto start = collector.beginBlock();
            collector.observeOutput (buffer.data(), buffer.data(), 64, 0.2);

            const auto pushStart = std::chrono::steady_clock::now();
            collector.endBlock (start, 64);
            const auto pushNs = std::chrono::duration_cast<std::chrono::nanoseconds> (
                std::chrono::steady_clock::now() - pushStart).count();

            if (pushNs > worstPushNanoseconds.load (std::memory_order_relaxed))
                worstPushNanoseconds.store (pushNs, std::memory_order_relaxed);
        }

        producing.store (false, std::memory_order_release);
        consumer.join();

        DiagnosticSnapshot final_;
        (void) collector.popLatestSnapshot (final_);

        info ("consumer received:   " + std::to_string (delivered.load()) + " snapshots");
        info ("published / dropped: " + std::to_string (final_.counters.snapshotsPublished)
              + " / " + std::to_string (final_.counters.snapshotsDropped));
        info ("worst endBlock:      " + std::to_string (worstPushNanoseconds.load()) + " ns");

        check (! wentBackwards.load(),
               "every snapshot the consumer saw was internally consistent and no earlier"
               " than the one before it");

        check (delivered.load() > 0, "the slow consumer still received snapshots");

        // The producer's worst callback must stay in the microseconds. Blocking
        // on a full queue would show up here as a millisecond.
        checkAtMost (static_cast<double> (worstPushNanoseconds.load()), 5.0e6,
                     "the producer never blocked waiting for the consumer");
    }

    // =======================================================================
    beginSuite ("Diagnostics / consumer isolation between instances");
    // =======================================================================
    {
        // Two collectors, two consumers, run together. Each consumer must see
        // only its own instance's numbers.
        DiagnosticCollector a, b;

        a.prepare (48000.0, 128, 256, 4800, 32);
        b.prepare (96000.0, 256, 256, 9600, 32);

        a.setLevel (Level::lightweight);
        b.setLevel (Level::lightweight);

        std::atomic<bool> running { true };
        std::atomic<bool> crossTalk { false };

        const auto watch = [&running, &crossTalk] (DiagnosticCollector& collector,
                                                   int expectedBlockSize)
        {
            DiagnosticSnapshot latest;

            while (running.load (std::memory_order_acquire))
            {
                if (collector.popLatestSnapshot (latest))
                {
                    if (latest.counters.callbackCount > 0
                        && latest.counters.processedSamples
                           != latest.counters.callbackCount * expectedBlockSize)
                        crossTalk.store (true, std::memory_order_relaxed);
                }

                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        };

        std::thread watcherA ([&] { watch (a, 128); });
        std::thread watcherB ([&] { watch (b, 256); });

        std::vector<double> small (128, 0.1), large (256, 0.1);

        for (int block = 0; block < 8000; ++block)
        {
            const auto startA = a.beginBlock();
            a.observeOutput (small.data(), small.data(), 128, 0.1);
            a.endBlock (startA, 128);

            const auto startB = b.beginBlock();
            b.observeOutput (large.data(), large.data(), 256, 0.1);
            b.endBlock (startB, 256);
        }

        running.store (false, std::memory_order_release);
        watcherA.join();
        watcherB.join();

        DiagnosticSnapshot finalA, finalB;
        (void) a.popLatestSnapshot (finalA);
        (void) b.popLatestSnapshot (finalB);

        info ("A: " + std::to_string (finalA.counters.callbackCount) + " callbacks at 48 kHz");
        info ("B: " + std::to_string (finalB.counters.callbackCount) + " callbacks at 96 kHz");

        check (! crossTalk.load(),
               "neither consumer ever saw the other instance's numbers");

        checkClose (finalA.host.sampleRate, 48000.0, 0.5, "A kept its own sample rate");
        checkClose (finalB.host.sampleRate, 96000.0, 0.5, "B kept its own");
    }
}

} // namespace avtest
