#include "AllocationDetector.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DiffusionNetwork.h"
#include "DSP/StereoDiffusionNetwork.h"

#include <cmath>
#include <memory>
#include <string>

using namespace av::dsp;

namespace avtest
{

namespace
{
    volatile double sink = 0.0;
}

void runDiffusionRealTimeSafetyTests()
{
    constexpr double sampleRate = 48000.0;

    // =======================================================================
    beginSuite ("Diffusion / real-time safety");
    // =======================================================================
    {
        // Everything below has to be reachable from the audio thread, so
        // everything below has to allocate exactly nothing.
        auto mono = std::make_unique<DiffusionNetwork<double>>();
        auto stereo = std::make_unique<StereoDiffusionNetwork<double>>();

        long long duringPrepare = 0;
        {
            ScopedAllocationCounter counter;
            mono->prepare (sampleRate);
            stereo->prepare (sampleRate);
            duringPrepare = counter.count();
        }

        if (allocationCountingAvailable())
            check (duringPrepare > 0, "prepare() allocates (as documented)");
        else
            info ("prepare() allocation: NOT MEASURED in a sanitizer build");

        bool configOk = true;
        long long duringProcessing = 0;
        {
            ScopedAllocationCounter counter;

            // --- mono processing -------------------------------------------
            for (int n = 0; n < 48000; ++n)
                sink = mono->processSample (static_cast<double> (n & 31) * 0.01);

            // --- stereo processing -----------------------------------------
            for (int n = 0; n < 48000; ++n)
            {
                double l = static_cast<double> (n & 31) * 0.01;
                double r = -l;
                stereo->processSample (l, r);
                sink = l + r;
            }

            // --- a full 0 % -> 100 % diffusion sweep ------------------------
            for (int n = 0; n <= 10000; ++n)
            {
                const double d = static_cast<double> (n) / 10000.0;
                mono->setDiffusion (d);
                stereo->setDiffusion (d);
                sink = mono->processSample (0.001);

                double l = 0.001, r = 0.001;
                stereo->processSample (l, r);
                sink = l;
            }

            // --- stage count changes, both directions ----------------------
            for (int n = 0; n < 2000; ++n)
            {
                mono->setStageCount (n % (DiffusionNetwork<double>::maxStages + 1));
                stereo->setStageCount (n % (DiffusionNetwork<double>::maxStages + 1));

                sink = mono->processSample (0.0);

                double l = 0.0, r = 0.0;
                stereo->processSample (l, r);
            }

            // --- delay scaling ---------------------------------------------
            for (int n = 0; n < 5000; ++n)
            {
                const double scale = 0.5 + 1.0 * static_cast<double> (n % 100) / 100.0;
                configOk &= succeeded (mono->setDelayScale (scale));
                configOk &= succeeded (stereo->setDelayScale (scale));
                sink = mono->processSample (0.0);
            }

            // --- shared stages and cross-coupling, which rebuild patterns ---
            for (int n = 0; n < 2000; ++n)
            {
                configOk &= succeeded (stereo->setSharedStages (n % 5));
                stereo->setCrossCoupling (static_cast<double> (n % 100) / 100.0);
                stereo->setCrossCouplingStage (n % 6);

                double l = 0.0, r = 0.0;
                stereo->processSample (l, r);
            }

            // --- changing block size every callback ------------------------
            const int sizes[] { 1, 16, 32, 64, 128, 256, 512, 1024, 4096 };
            double buffer[4096] {};
            double bufferR[4096] {};

            for (int repeat = 0; repeat < 4; ++repeat)
            {
                for (int size : sizes)
                {
                    mono->processBlock (buffer, size);
                    stereo->processBlock (buffer, bufferR, size);
                }
            }

            sink = buffer[0];

            // --- reset ------------------------------------------------------
            for (int n = 0; n < 100; ++n)
            {
                mono->reset();
                stereo->reset();
            }

            duringProcessing = counter.count();
        }

        checkNoAllocations (duringProcessing,
                    "diffusion processing, automation, stage changes, delay scaling, "
                    "stereo rebuilds, varying block sizes and reset allocate exactly nothing");

        check (configOk, "every configuration change stayed inside the reserved capacity");

        // The same for every prepared sample rate.
        long long duringRates = 0;
        {
            auto net = std::make_unique<StereoDiffusionNetwork<double>>();

            for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            {
                net->prepare (rate);

                ScopedAllocationCounter counter;

                for (int n = 0; n < 8000; ++n)
                {
                    double l = 0.01, r = -0.01;
                    net->setDiffusion (static_cast<double> (n % 1000) / 1000.0);
                    net->processSample (l, r);
                    sink = l + r;
                }

                duringRates += counter.count();
            }
        }

        checkNoAllocations (duringRates,
                    "processing allocates nothing at 44.1 / 48 / 88.2 / 96 / 192 kHz");
    }
}

} // namespace avtest
