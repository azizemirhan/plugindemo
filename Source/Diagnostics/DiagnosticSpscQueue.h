#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

/*
    MSVC's C4324 says a structure was padded because of an alignment specifier.
    That is not a problem here, it is the entire purpose: the two indices are
    deliberately placed on separate cache lines so that the producer's writes do
    not invalidate the line the consumer is reading. Suppressed narrowly, around
    these two types only, rather than project-wide.
*/
#if defined (_MSC_VER)
 #pragma warning (push)
 #pragma warning (disable : 4324)
#endif

namespace av::diag
{

/**
    Single-producer, single-consumer lock-free queue.

    THE CONTRACT, STATED BECAUSE IT IS NOT CHECKED AT RUNTIME
    --------------------------------------------------------
    Exactly one thread may call push(). Exactly one thread may call pop(). They
    may be different threads and they may run at the same time. Two threads
    pushing, or two popping, is undefined behaviour and there is no assertion
    that will catch it in a release build — the whole point of the structure is
    that neither side synchronises with anything.

    In this plugin the producer is the audio thread and the consumer is the
    diagnostic writer thread. Nothing else touches either end.

    WHY THE PRODUCER NEVER WAITS
    ----------------------------
    A full queue makes push() return false immediately. It does not block, does
    not spin, does not drop the oldest entry to make room. Dropping the oldest
    would mean the audio thread doing extra work precisely when the system is
    already struggling, and losing the events most likely to explain why. So the
    newest event is refused, a counter is incremented by the caller, and the
    audio thread carries on at the same cost as a successful push.

    One slot is always left empty. That is what lets full and empty be told
    apart from two indices without a separate count that both sides would have
    to update.

    CACHE LINES
    -----------
    The two indices are padded apart. Without that they share a cache line, and
    every push invalidates the line the consumer is reading — false sharing that
    costs the audio thread far more than the queue itself.
*/
template <typename T>
class DiagnosticSpscQueue
{
public:
    /** Allocates. Never call from the audio thread. */
    void prepare (size_t capacity)
    {
        // One extra slot, because one is always kept empty.
        storage.assign (capacity + 1, T {});

        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

    /** Usable capacity, which is one less than the allocated storage. */
    [[nodiscard]] size_t capacity() const noexcept
    {
        return storage.empty() ? 0 : storage.size() - 1;
    }

    /**
        Producer side. Real-time safe, never waits.

        Returns false if the queue is full; the caller decides what to count.
    */
    [[nodiscard]] bool push (const T& item) noexcept
    {
        if (storage.empty())
            return false;

        const size_t write = writeIndex.load (std::memory_order_relaxed);
        const size_t next = advance (write);

        // Acquire: everything the consumer did before publishing its read index
        // must be visible before this slot is reused.
        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        storage[write] = item;

        // Release: the item must be visible before the index that exposes it.
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer side. Returns false if the queue is empty. */
    [[nodiscard]] bool pop (T& item) noexcept
    {
        if (storage.empty())
            return false;

        const size_t read = readIndex.load (std::memory_order_relaxed);

        if (read == writeIndex.load (std::memory_order_acquire))
            return false;

        item = storage[read];

        readIndex.store (advance (read), std::memory_order_release);
        return true;
    }

    /** Approximate. Exact only when one side is known to be idle. */
    [[nodiscard]] size_t size() const noexcept
    {
        if (storage.empty())
            return 0;

        const size_t write = writeIndex.load (std::memory_order_acquire);
        const size_t read = readIndex.load (std::memory_order_acquire);

        return write >= read ? write - read : storage.size() - read + write;
    }

    [[nodiscard]] bool isEmpty() const noexcept { return size() == 0; }
    [[nodiscard]] bool isFull()  const noexcept { return size() == capacity(); }

    /** Not thread-safe. Only with both sides stopped. */
    void reset() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

private:
    [[nodiscard]] size_t advance (size_t index) const noexcept
    {
        const size_t next = index + 1;
        return next == storage.size() ? 0 : next;
    }

    static constexpr size_t cacheLine = 64;

    std::vector<T> storage;

    // alignas alone is enough: it forces each index onto its own cache line,
    // and the compiler pads the tail itself. Adding explicit padding members on
    // top of it only made MSVC warn that it had padded a structure that was
    // already correctly padded.
    alignas (cacheLine) std::atomic<size_t> writeIndex { 0 };
    alignas (cacheLine) std::atomic<size_t> readIndex { 0 };
};

/**
    Lock-free audio ring for capture.

    Same producer/consumer contract as above, but interleaved stereo frames and
    a bulk interface: copying a block one sample at a time through a generic
    queue would cost far more than a pair of memcpy-shaped loops.

    A full ring drops frames rather than blocking, for the same reason. The
    dropped count is what tells the report that the capture has a hole in it,
    so a listener does not mistake a gap for something the reverb did.
*/
class DiagnosticAudioRing
{
public:
    /** Allocates. Never call from the audio thread. */
    void prepare (size_t frameCapacity)
    {
        storage.assign ((frameCapacity + 1) * 2, 0.0f);
        frames = frameCapacity + 1;

        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

    [[nodiscard]] size_t capacity() const noexcept { return frames == 0 ? 0 : frames - 1; }

    /**
        Producer side. Writes as many frames as fit and reports how many.

        Never waits and never partially blocks: what does not fit is simply not
        written, and the caller counts the difference as dropped.
    */
    [[nodiscard]] size_t write (const double* left, const double* right, size_t count) noexcept
    {
        if (frames == 0 || count == 0)
            return 0;

        const size_t writePosition = writeIndex.load (std::memory_order_relaxed);
        const size_t readPosition = readIndex.load (std::memory_order_acquire);

        const size_t free = writePosition >= readPosition
                          ? frames - (writePosition - readPosition) - 1
                          : readPosition - writePosition - 1;

        const size_t toWrite = count < free ? count : free;

        size_t position = writePosition;

        for (size_t i = 0; i < toWrite; ++i)
        {
            storage[position * 2] = static_cast<float> (left[i]);
            storage[position * 2 + 1] = static_cast<float> (right[i]);

            ++position;

            if (position == frames)
                position = 0;
        }

        writeIndex.store (position, std::memory_order_release);
        return toWrite;
    }

    /** Consumer side. Reads up to `count` frames, returns how many it got. */
    [[nodiscard]] size_t read (float* interleaved, size_t count) noexcept
    {
        if (frames == 0 || count == 0)
            return 0;

        const size_t readPosition = readIndex.load (std::memory_order_relaxed);
        const size_t writePosition = writeIndex.load (std::memory_order_acquire);

        const size_t available = writePosition >= readPosition
                               ? writePosition - readPosition
                               : frames - readPosition + writePosition;

        const size_t toRead = count < available ? count : available;

        size_t position = readPosition;

        for (size_t i = 0; i < toRead; ++i)
        {
            interleaved[i * 2] = storage[position * 2];
            interleaved[i * 2 + 1] = storage[position * 2 + 1];

            ++position;

            if (position == frames)
                position = 0;
        }

        readIndex.store (position, std::memory_order_release);
        return toRead;
    }

    [[nodiscard]] size_t available() const noexcept
    {
        if (frames == 0)
            return 0;

        const size_t writePosition = writeIndex.load (std::memory_order_acquire);
        const size_t readPosition = readIndex.load (std::memory_order_acquire);

        return writePosition >= readPosition
             ? writePosition - readPosition
             : frames - readPosition + writePosition;
    }

    /** Not thread-safe. Only with both sides stopped. */
    void reset() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t cacheLine = 64;

    std::vector<float> storage;
    size_t frames = 0;

    // alignas alone is enough: it forces each index onto its own cache line,
    // and the compiler pads the tail itself. Adding explicit padding members on
    // top of it only made MSVC warn that it had padded a structure that was
    // already correctly padded.
    alignas (cacheLine) std::atomic<size_t> writeIndex { 0 };
    alignas (cacheLine) std::atomic<size_t> readIndex { 0 };
};

} // namespace av::diag

#if defined (_MSC_VER)
 #pragma warning (pop)
#endif
