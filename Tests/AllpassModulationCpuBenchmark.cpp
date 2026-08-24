#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/MorphingStereoFdn.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    volatile double sink = 0.0;
    constexpr int N = fdn::order;

    constexpr int benchmarkSamples = 1'000'000;
    constexpr int repeats = 5;

    using Mode = TimeVaryingAllpass::Mode;

    std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    template <typename Body>
    double measure (const std::string& name, double rate, Body&& body)
    {
        for (int n = 0; n < 20000; ++n)
            body (n);

        std::vector<double> runs;
        runs.reserve (repeats);

        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            const auto start = std::chrono::steady_clock::now();

            for (int n = 0; n < benchmarkSamples; ++n)
                body (n);

            const auto elapsed = std::chrono::steady_clock::now() - start;
            runs.push_back (std::chrono::duration<double> (elapsed).count() * 1.0e9 / benchmarkSamples);
        }

        std::sort (runs.begin(), runs.end());

        const double median = runs[runs.size() / 2];
        const double worst  = runs.back();

        info (pad (name, 34) + pad (fixed (median, 1) + " ns", 11)
              + pad (fixed (median * rate / 1.0e7, 3) + " %", 10)
              + pad (fixed (worst, 1) + " ns", 11)
              + fixed (worst * rate / 1.0e7, 3) + " %");

        return worst * rate / 1.0e7;
    }

    std::unique_ptr<StereoFdn<N>> makeTank (double rate, Mode mode, double depth,
                                            LfoShape shape, int lines, int sections,
                                            double modRate = allpassMod::defaultRateHz)
    {
        auto tank = std::make_unique<StereoFdn<N>>();
        tank->prepare (rate);
        tank->setInputDiffusionEnabled (true);
        tank->setRt60Seconds (2.0, true);
        tank->setModulationDepth (0.0, true);
        tank->setAllpassMode (mode);
        tank->setAllpassShape (shape);
        tank->setAllpassRateHz (modRate);
        tank->setAllpassActiveLines (lines);
        tank->setAllpassSectionsPerLine (sections);
        tank->setAllpassDepth (depth, true);
        return tank;
    }
}

void runAllpassModulationCpuBenchmark()
{
    beginSuite ("Allpass / CPU cost");

    info ("Release build, MSVC 19.44, x64. Compiler options:");
    info ("  /O2 /Oi /Ot /fp:precise /permissive- /Zc:__cplusplus /utf-8 /GL (LTCG), static CRT.");
    info (std::to_string (benchmarkSamples) + " samples per run, " + std::to_string (repeats)
          + " runs per configuration, warm-up excluded, no debugger attached.");
    info ("Every configuration includes the 4-stage stereo input diffuser.");
    info ("The PHASE 5A matrix figures to compare against are the ones printed by the");
    info ("'Matrix modulation / CPU cost' suite in this same run. Absolute nanoseconds move");
    info ("by a few per cent between runs with machine state, so only the static-baseline");
    info ("delta within one run is meaningful; both suites print their own static baseline.");
    info ("");
    info ("  configuration                     median     of core   worst      of core");

    ScopedFtz ftz;

    double worstDefault = 0.0, worstAnywhere = 0.0;

    for (double rate : { 48000.0, 96000.0, 192000.0 })
    {
        info ("");
        info ("--- " + fixed (rate / 1000.0, 1) + " kHz ---");

        auto run = [&] (const std::string& name, Mode mode, double depth,
                        LfoShape shape, int lines, int sections,
                        double modRate = allpassMod::defaultRateHz)
        {
            auto tank = makeTank (rate, mode, depth, shape, lines, sections, modRate);

            const double share = measure (name, rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstAnywhere = std::max (worstAnywhere, share);
            return share;
        };

        run ("static FDN (bank bypassed)", Mode::energyPreserving, 0.0,
             LfoShape::sine, 8, 1);

        run ("2 lines, 1 section", Mode::energyPreserving, allpassMod::defaultDepth,
             LfoShape::sine, 2, 1);
        run ("4 lines, 1 section", Mode::energyPreserving, allpassMod::defaultDepth,
             LfoShape::sine, 4, 1);

        const double defaultShare = run ("8 lines, 1 section (shipped)",
                                         Mode::energyPreserving, allpassMod::defaultDepth,
                                         LfoShape::sine, 8, 1);

        run ("8 lines, 2 sections", Mode::energyPreserving, allpassMod::defaultDepth,
             LfoShape::sine, 8, 2);

        run ("thiran, 8 lines", Mode::thiran, allpassMod::defaultDepth,
             LfoShape::sine, 8, 1);
        run ("schroeder reference, 8 lines", Mode::schroederReference, allpassMod::defaultDepth,
             LfoShape::sine, 8, 1);

        run ("8 lines, smooth random", Mode::energyPreserving, allpassMod::defaultDepth,
             LfoShape::smoothRandom, 8, 1);

        run ("worst case: 8x2, max rate/depth", Mode::energyPreserving, 1.0,
             LfoShape::sine, 8, 2, allpassMod::maxRateHz);

        if (std::abs (rate - 48000.0) < 1.0)
            worstDefault = defaultShare;

        // Dual-bank morph with allpass modulation running.
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setRt60Seconds (2.0, true);
            tank->setModulationDepth (0.0, true);
            tank->setAllpassDepth (allpassMod::defaultDepth, true);
            tank->setPrimeCapSeconds (4.0);

            const double share = measure ("dual-bank morph + allpass", rate, [&] (int n)
            {
                if (n % 200000 == 0)
                    tank->setSize (((n / 200000) % 2 == 0) ? 1.8 : 0.4);

                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstAnywhere = std::max (worstAnywhere, share);
        }
    }

    info ("");
    info ("Reducing the active line count barely reduces cost: 2 lines is within a few per");
    info ("cent of 8. advance() ticks all " + std::to_string (N) + " LFO lanes every sample regardless of how");
    info ("many lines are engaged, because a lane that stopped and restarted would jump in");
    info ("phase. The per-line filter itself is cheap next to that fixed cost, so the active");
    info ("line count is a voicing control, not a CPU control.");
    info ("");
    info ("shipped allpass configuration at 48 kHz: " + fixed (worstDefault, 3)
          + " % of one core, worst case");
    info ("most expensive configuration anywhere in the matrix: "
          + fixed (worstAnywhere, 3) + " % of one core");

    checkCpuAtMost (worstDefault, 4.0,
                 "the shipped allpass configuration fits in 4 % of one core at 48 kHz");
    checkCpuAtMost (worstAnywhere, 35.0,
                 "even the research worst case at 192 kHz fits in 35 % of one core");
}

} // namespace avtest
