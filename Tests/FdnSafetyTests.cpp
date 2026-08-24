#include "AllocationDetector.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"

#include <cmath>
#include <memory>
#include <string>

using namespace av::dsp;

namespace avtest
{

namespace
{
    volatile double sink = 0.0;
    constexpr int N = fdn::order;
}

void runFdnRealTimeSafetyTests()
{
    // =======================================================================
    beginSuite ("FDN / real-time safety");
    // =======================================================================
    {
        auto tank = std::make_unique<MorphingStereoFdn<N>>();

        long long duringPrepare = 0;
        {
            ScopedAllocationCounter counter;
            const auto result = tank->prepare (48000.0);
            duringPrepare = counter.count();
            check (succeeded (result), "prepare succeeds");
        }

        if (allocationCountingAvailable())
            check (duringPrepare > 0, "prepare() allocates (as documented)");
        else
            info ("prepare() allocation: NOT MEASURED in a sanitizer build");

        bool finite = true;
        long long duringProcessing = 0;

        {
            ScopedAllocationCounter counter;

            // --- plain processing ------------------------------------------
            for (int n = 0; n < 48000; ++n)
            {
                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                finite &= std::isfinite (l) && std::isfinite (r);
                sink = l + r;
            }

            // --- decay automation, both directions --------------------------
            for (int n = 0; n < 48000; ++n)
            {
                tank->setRt60Seconds (0.1 + 99.9 * static_cast<double> (n % 1000) / 1000.0);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = l;
            }

            // --- Size morphs, including rapid target updates -----------------
            for (int n = 0; n < 240000; ++n)
            {
                tank->setSize (0.3 + 1.6 * static_cast<double> (n % 977) / 977.0);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = r;
            }

            // --- matrix and diffusion switching ------------------------------
            for (int n = 0; n < 20000; ++n)
            {
                tank->setMatrixType (n % 2 == 0 ? MorphingStereoFdn<N>::Matrix::Type::hadamard
                                                : MorphingStereoFdn<N>::Matrix::Type::householder);
                tank->setInputDiffusionEnabled (n % 3 != 0);
                tank->setInputDiffusion (static_cast<double> (n % 100) / 100.0);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = l;
            }

            // --- changing block size every callback --------------------------
            {
                const int sizes[] { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 };
                double left[8192] {};
                double right[8192] {};

                for (int repeat = 0; repeat < 4; ++repeat)
                    for (int size : sizes)
                        tank->processBlock (left, right, size);

                sink = left[0];
            }

            // --- reset --------------------------------------------------------
            for (int n = 0; n < 200; ++n)
                tank->reset();

            duringProcessing = counter.count();
        }

        check (finite, "everything above stayed finite");
        checkNoAllocations (duringProcessing,
                    "FDN processing, decay automation, Size morphs, rapid target updates, "
                    "matrix switching, varying block sizes and reset allocate exactly nothing");

        // The same at every supported sample rate, including after a re-prepare
        // — which is what a host does when the user changes the device rate or
        // restores a session.
        long long duringRates = 0;
        {
            auto rated = std::make_unique<MorphingStereoFdn<N>>();

            for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            {
                check (succeeded (rated->prepare (rate)),
                       "prepare succeeds at " + std::to_string (static_cast<int> (rate)) + " Hz");

                ScopedAllocationCounter counter;

                for (int n = 0; n < 8000; ++n)
                {
                    rated->setSize (0.5 + static_cast<double> (n % 500) / 500.0);
                    rated->setRt60Seconds (0.5 + static_cast<double> (n % 300) / 30.0);

                    double l = 0.01, r = -0.01;
                    rated->processSample (l, r);
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
