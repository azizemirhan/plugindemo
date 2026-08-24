#include "Suites.h"
#include "TestFramework.h"

#include "DSP/ModulatedDelay.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    const double rates[]     = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int    blockSizes[] = { 64, 128, 256, 512, 1024 };

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }
}

void runRateAndBlockSizeTests()
{
    // =======================================================================
    beginSuite ("Sample rate / delay time is specified in seconds");
    // =======================================================================
    {
        // A 10 ms delay must be 10 ms at every supported rate.
        for (double rate : rates)
        {
            ModulatedDelay<double> md;
            md.prepare (rate, 0.2);
            md.setBaseDelaySeconds (0.01, true);
            md.setDepthSamples (0.0, true);
            md.setRateHz (0.0);

            const int expected = static_cast<int> (std::lround (0.01 * rate));

            int    peakIndex = -1;
            double peakValue = 0.0;

            for (int n = 0; n < expected * 3; ++n)
            {
                const double y = md.processSample (n == 0 ? 1.0 : 0.0);

                if (std::abs (y) > peakValue)
                {
                    peakValue = std::abs (y);
                    peakIndex = n;
                }
            }

            const std::string label = fixed (rate / 1000.0, 1) + " kHz";

            checkClose (peakIndex, expected, 1.0, label + ": impulse arrives 10 ms late");
            checkClose (peakValue, 1.0, 1.0e-9, label + ": impulse passes through at unity gain");
        }
    }

    // =======================================================================
    beginSuite ("Sample rate / modulation character is preserved");
    // =======================================================================
    {
        // Depth in seconds and rate in Hz means the pitch deviation must come
        // out identical at every sample rate. That is the definition of the
        // reverb "sounding the same" after a rate change.
        constexpr double depthSeconds = 0.0005; // 0.5 ms
        constexpr double modRate      = 2.0;

        constexpr double baseSeconds = 0.02;

        std::vector<double> centsPerRate;
        double worstTrajectoryError = 0.0;

        for (double rate : rates)
        {
            const double depthSamples = depthSeconds * rate;
            centsPerRate.push_back (
                ModulatedDelay<double>::peakPitchDeviationCents (depthSamples, modRate,
                                                                 rate, LfoShape::sine));

            ModulatedDelay<double> md;
            md.prepare (rate, 0.2);
            md.setBaseDelaySeconds (baseSeconds, true);
            md.setDepthSeconds (depthSeconds, true);
            md.setRateHz (modRate);
            md.setShape (LfoShape::sine);

            // The delay trajectory, expressed in seconds, must be the same
            // function of time at every rate:
            //     tau(t) = base + depth * sin(2*pi * modRate * t)
            // Comparing each rate against that analytic form is stronger than
            // comparing the rates against each other on a resampled grid.
            const int total = static_cast<int> (rate); // one second

            for (int n = 0; n < total; ++n)
            {
                (void) md.processSample (0.0);

                const double t        = (n + 1) / rate; // the LFO advances before it is read
                const double expected = baseSeconds + depthSeconds * std::sin (2.0 * pi * modRate * t);
                const double actual   = md.getCurrentDelaySamples() / rate;

                worstTrajectoryError = std::max (worstTrajectoryError, std::abs (actual - expected));
            }
        }

        for (size_t i = 0; i < centsPerRate.size(); ++i)
            info (fixed (rates[i] / 1000.0, 1) + " kHz: peak deviation "
                  + fixed (centsPerRate[i], 6) + " cents");

        double worstCentsSpread = 0.0;
        for (double c : centsPerRate)
            worstCentsSpread = std::max (worstCentsSpread, std::abs (c - centsPerRate[0]));

        checkAtMost (worstCentsSpread, 1.0e-9,
                     "peak pitch deviation is identical at 44.1 / 48 / 88.2 / 96 kHz");

        info ("worst deviation from the analytic trajectory: "
              + fixed (worstTrajectoryError * 1.0e9, 4) + " nanoseconds");

        checkAtMost (worstTrajectoryError, 1.0e-9,
                     "the delay trajectory in seconds matches the analytic form at every rate");
    }

    // =======================================================================
    beginSuite ("Block size / rendering is independent of the block boundary");
    // =======================================================================
    {
        // Rendering the same input must produce bit-identical output whether
        // the host hands it over in one block or in a thousand.
        constexpr double sampleRate = 48000.0;
        constexpr int    totalSamples = 24000;

        auto renderInBlocks = [&] (int blockSize)
        {
            ModulatedDelay<double> md;
            md.prepare (sampleRate, 0.2);
            md.setBaseDelaySeconds (0.013, true);
            md.setDepthSeconds (0.0004, true);
            md.setRateHz (3.1);
            md.setShape (LfoShape::sine);

            std::vector<double> out;
            out.reserve (totalSamples);

            int n = 0;
            while (n < totalSamples)
            {
                const int count = std::min (blockSize, totalSamples - n);

                for (int i = 0; i < count; ++i, ++n)
                    out.push_back (md.processSample (std::sin (2.0 * pi * 440.0 * n / sampleRate)));
            }

            return out;
        };

        const auto reference = renderInBlocks (totalSamples);

        for (int blockSize : blockSizes)
        {
            const auto rendered = renderInBlocks (blockSize);

            double worst = 0.0;
            for (size_t i = 0; i < reference.size(); ++i)
                worst = std::max (worst, std::abs (rendered[i] - reference[i]));

            checkClose (worst, 0.0, 0.0,
                        "block size " + std::to_string (blockSize)
                        + " is bit-identical to a single-block render");
        }

        // A block size that changes every callback is the hard case.
        {
            ModulatedDelay<double> md;
            md.prepare (sampleRate, 0.2);
            md.setBaseDelaySeconds (0.013, true);
            md.setDepthSeconds (0.0004, true);
            md.setRateHz (3.1);
            md.setShape (LfoShape::sine);

            std::vector<double> out;
            out.reserve (totalSamples);

            int n = 0, sizeIndex = 0;
            while (n < totalSamples)
            {
                const int blockSize = blockSizes[sizeIndex % 5] + (sizeIndex % 7);
                sizeIndex++;

                const int count = std::min (blockSize, totalSamples - n);

                for (int i = 0; i < count; ++i, ++n)
                    out.push_back (md.processSample (std::sin (2.0 * pi * 440.0 * n / sampleRate)));
            }

            double worst = 0.0;
            for (size_t i = 0; i < reference.size(); ++i)
                worst = std::max (worst, std::abs (out[i] - reference[i]));

            checkClose (worst, 0.0, 0.0, "a block size that changes every callback is also bit-identical");
        }
    }

    // =======================================================================
    beginSuite ("Rate x block-size matrix");
    // =======================================================================
    {
        int    combinations = 0;
        bool   allFinite = true, allBounded = true, allInRange = true;
        double worstPeak = 0.0;

        for (double rate : rates)
        {
            for (int blockSize : blockSizes)
            {
                ModulatedDelay<double> md;
                md.prepare (rate, 0.2);
                md.setBaseDelaySeconds (0.025, true);
                md.setDepthSeconds (0.001, true);
                md.setRateHz (4.0);
                md.setShape (LfoShape::smoothRandom);
                md.setSeed (static_cast<uint32_t> (combinations + 1));

                const int totalSamples = static_cast<int> (rate); // one second

                int n = 0;
                while (n < totalSamples)
                {
                    const int count = std::min (blockSize, totalSamples - n);

                    for (int i = 0; i < count; ++i, ++n)
                    {
                        const double y = md.processSample (std::sin (2.0 * pi * 1000.0 * n / rate));

                        allFinite  &= std::isfinite (y);
                        worstPeak   = std::max (worstPeak, std::abs (y));

                        const double d = md.getCurrentDelaySamples();
                        allInRange &= (d >= 1.0 && d <= md.getDelayLine().getMaxDelaySamples());
                    }
                }

                ++combinations;
            }
        }

        allBounded = worstPeak <= 1.05;

        info (std::to_string (combinations) + " rate x block-size combinations, one second each");
        info ("worst output peak across the matrix: " + fixed (worstPeak, 6));

        checkClose (combinations, 20, 0, "all 4 rates x 5 block sizes were exercised");
        check (allFinite,  "every combination produced finite output");
        check (allBounded, "every combination stayed within 5 % of the input peak");
        check (allInRange, "the read position never left the delay line");
    }
}

} // namespace avtest
