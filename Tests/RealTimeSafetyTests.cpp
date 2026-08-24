#include "AllocationDetector.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DelayLine.h"
#include "DSP/ModulatedDelay.h"
#include "DSP/ScratchArena.h"
#include "DSP/SmoothedParameter.h"

#include <cmath>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    volatile double sink = 0.0;
}

void runRealTimeSafetyTests()
{
    // =======================================================================
    beginSuite ("Real-time safety / detector sanity");
    // =======================================================================
    {
        // If the detector cannot see a known allocation, every result below is
        // meaningless — so prove it can first.
        long long observed = 0;
        {
            ScopedAllocationCounter counter;
            std::vector<double> v (1024);
            sink = v[0];
            observed = counter.count();
        }

        if (allocationCountingAvailable())
            check (observed > 0, "the allocation detector observes a std::vector allocation");
        else
            info ("the allocation detector is not linked in this build (AddressSanitizer"
                  " installs its own allocator), so every allocation claim below reports"
                  " itself as not measured rather than passing on a counter that cannot rise");

        long long duringNothing = 0;
        {
            ScopedAllocationCounter counter;
            for (int n = 0; n < 1000; ++n)
                sink = sink + 1.0;

            duringNothing = counter.count();
        }

        checkNoAllocations (duringNothing,
                    "the detector reports zero for allocation-free code");
    }

    // =======================================================================
    beginSuite ("Real-time safety / setup allocates, processing does not");
    // =======================================================================
    {
        constexpr double sampleRate = 48000.0;
        constexpr int    blockSize  = 512;

        // --- prepare() is expected to allocate -----------------------------

        DelayLine<double> line;
        long long duringPrepare = 0;
        {
            ScopedAllocationCounter counter;
            line.prepare (sampleRate, 0.5);
            duringPrepare = counter.count();
        }

        if (allocationCountingAvailable())
            check (duringPrepare > 0, "DelayLine::prepare() allocates (as documented)");
        else
            info ("DelayLine::prepare() allocation: NOT MEASURED in a sanitizer build");

        // --- everything else must not --------------------------------------

        long long duringWriteRead = 0;
        {
            ScopedAllocationCounter counter;

            for (int n = 0; n < blockSize * 100; ++n)
            {
                line.write (static_cast<double> (n) * 1.0e-6);
                sink = line.read<Interpolation::hermite> (137.4);
                sink = line.read<Interpolation::lagrange3> (2000.9);
                sink = line.read<Interpolation::linear> (11.25);
                sink = line.readAt (55);
            }

            duringWriteRead = counter.count();
        }

        checkNoAllocations (duringWriteRead,
                    "51200 delay-line write/read cycles allocate nothing");

        long long duringReset = 0;
        {
            ScopedAllocationCounter counter;
            for (int n = 0; n < 100; ++n)
                line.reset();

            duringReset = counter.count();
        }

        checkNoAllocations (duringReset,
                    "DelayLine::reset() allocates nothing");

        // --- modulated delay ------------------------------------------------

        ModulatedDelay<double> md;
        md.prepare (sampleRate, 0.2);
        md.setBaseDelaySamples (400.0, true);
        md.setDepthSamples (12.0, true);
        md.setRateHz (1.7);

        long long duringModulated = 0;
        {
            ScopedAllocationCounter counter;

            for (auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::smoothRandom })
            {
                md.setShape (shape);

                for (int n = 0; n < blockSize * 40; ++n)
                    sink = md.processSample (static_cast<double> (n & 15) * 0.01);
            }

            // Parameter changes on the audio thread must be allocation-free too.
            for (int n = 0; n < 1000; ++n)
            {
                md.setBaseDelaySamples (100.0 + n * 0.3);
                md.setDepthSamples (n * 0.01);
                md.setRateHz (0.1 + n * 0.001);
                md.setPhaseOffset (n * 0.01);
                md.setSeed (static_cast<uint32_t> (n));
                sink = md.processSample (0.0);
            }

            md.reset();
            duringModulated = counter.count();
        }

        checkNoAllocations (duringModulated,
                    "modulated delay processing, parameter changes and reset allocate nothing");

        // --- smoothers -------------------------------------------------------

        LinearSmoother<double>  lin;
        OnePoleSmoother<double> pole;
        LogSmoother<double>     logS;

        lin.prepare (sampleRate, 0.02);
        pole.prepare (sampleRate, 0.01);
        logS.prepare (sampleRate, 0.03, 20.0);
        lin.snapTo (0.0);
        pole.snapTo (0.0);
        logS.snapTo (100.0);

        long long duringSmoothing = 0;
        {
            ScopedAllocationCounter counter;

            for (int block = 0; block < 200; ++block)
            {
                lin.setTarget (block & 1 ? 1.0 : 0.0);
                pole.setTarget (block * 0.5);
                logS.setTarget (100.0 + block * 30.0);

                for (int n = 0; n < blockSize; ++n)
                {
                    sink = lin.getNextValue();
                    sink = pole.getNextValue();
                }

                sink = logS.advance (blockSize);
            }

            duringSmoothing = counter.count();
        }

        checkNoAllocations (duringSmoothing,
                    "all three smoothers allocate nothing while running");

        // --- scratch arena ---------------------------------------------------

        ScratchArena<double> arena;
        arena.prepare (8192);

        long long duringArena = 0;
        {
            ScopedAllocationCounter counter;

            for (int block = 0; block < 500; ++block)
            {
                arena.reset();

                auto* left  = arena.acquire (blockSize, true);
                auto* right = arena.acquire (blockSize, true);

                {
                    ScratchArena<double>::Scope scope (arena);
                    auto* temp = arena.acquire (blockSize * 4);

                    if (temp != nullptr && left != nullptr && right != nullptr)
                    {
                        temp[0]  = 1.0;
                        left[0]  = temp[0];
                        right[0] = left[0];
                        sink     = right[0];
                    }
                }
            }

            duringArena = counter.count();
        }

        checkNoAllocations (duringArena,
                    "500 blocks of arena use allocate nothing");
    }

    // =======================================================================
    beginSuite ("Real-time safety / block-size independence");
    // =======================================================================
    {
        // The host may hand over any block size, including one that changes
        // between callbacks. Nothing may allocate or behave differently.
        constexpr double sampleRate = 48000.0;

        ModulatedDelay<double> md;
        md.prepare (sampleRate, 0.2);
        md.setBaseDelaySamples (300.0, true);
        md.setDepthSamples (8.0, true);
        md.setRateHz (2.0);

        const int sizes[] = { 1, 7, 64, 128, 256, 333, 512, 1024, 2048 };

        long long duringVaryingBlocks = 0;
        bool finite = true;

        {
            ScopedAllocationCounter counter;

            for (int repeat = 0; repeat < 20; ++repeat)
            {
                for (int size : sizes)
                {
                    for (int n = 0; n < size; ++n)
                    {
                        const double y = md.processSample (0.001);
                        finite &= std::isfinite (y);
                        sink = y;
                    }
                }
            }

            duringVaryingBlocks = counter.count();
        }

        check (finite, "output stays finite across randomly varying block sizes");
        checkNoAllocations (duringVaryingBlocks,
                    "varying the block size every callback allocates nothing");
    }
}

} // namespace avtest
