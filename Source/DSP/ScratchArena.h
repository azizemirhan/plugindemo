#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace av::dsp
{

/**
    Pre-allocated scratch memory for the audio thread.

    A feedback delay network needs a handful of temporary vectors per block —
    the channel vector before and after the mixing matrix, the early reflection
    sum, the damping filter inputs. Their sizes depend on the algorithm and on
    the host's block size, so giving every module its own fixed member buffer
    either wastes memory or caps the block size.

    This is a bump allocator over one contiguous block reserved in prepare().
    acquire() hands out a span and moves a cursor; reset() rewinds the cursor to
    zero at the top of each processBlock. There is no per-allocation bookkeeping
    and no free list, because nothing is ever freed individually.

    Scope gives back a region early, so a module can borrow a large temporary
    inside a loop without the arena growing to the sum of all borrows.

    Exhaustion returns nullptr rather than allocating. Callers must handle it;
    in a debug build getHighWaterMark() after a few blocks tells you the real
    requirement so prepare() can be sized correctly instead of guessed at.
*/
template <typename SampleType = double>
class ScratchArena
{
public:
    /** Reserves capacity. Allocates — never call from the audio thread. */
    void prepare (size_t capacityInSamples)
    {
        storage.assign (capacityInSamples, SampleType (0));
        cursor        = 0;
        highWaterMark = 0;
    }

    void release()
    {
        storage.clear();
        storage.shrink_to_fit();
        cursor        = 0;
        highWaterMark = 0;
    }

    /** Rewinds the cursor. Call once at the top of processBlock. */
    void reset() noexcept { cursor = 0; }

    /**
        Returns a pointer to `count` contiguous samples, or nullptr if the arena
        is exhausted. The contents are whatever the previous user left behind;
        pass clearFirst if zeroed memory is required.
    */
    [[nodiscard]] SampleType* acquire (size_t count, bool clearFirst = false) noexcept
    {
        if (cursor + count > storage.size())
            return nullptr;

        auto* p = storage.data() + cursor;
        cursor += count;
        highWaterMark = std::max (highWaterMark, cursor);

        if (clearFirst)
            std::fill (p, p + count, SampleType (0));

        return p;
    }

    [[nodiscard]] size_t getCapacity()      const noexcept { return storage.size(); }
    [[nodiscard]] size_t getUsed()          const noexcept { return cursor; }
    [[nodiscard]] size_t getAvailable()     const noexcept { return storage.size() - cursor; }
    [[nodiscard]] size_t getHighWaterMark() const noexcept { return highWaterMark; }

    void clearHighWaterMark() noexcept { highWaterMark = 0; }

    /** RAII mark/restore, so a temporary borrow does not permanently consume space. */
    class Scope
    {
    public:
        explicit Scope (ScratchArena& a) noexcept : arena (a), mark (a.cursor) {}
        ~Scope() noexcept { arena.cursor = mark; }

        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;

    private:
        ScratchArena& arena;
        size_t mark;
    };

private:
    std::vector<SampleType> storage;
    size_t cursor        = 0;
    size_t highWaterMark = 0;
};

} // namespace av::dsp
