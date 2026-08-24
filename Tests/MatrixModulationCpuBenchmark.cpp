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

        info (pad (name, 32) + pad (fixed (median, 1) + " ns", 11)
              + pad (fixed (median * rate / 1.0e7, 3) + " %", 10)
              + pad (fixed (worst, 1) + " ns", 11)
              + fixed (worst * rate / 1.0e7, 3) + " %");

        return worst * rate / 1.0e7;
    }

    std::unique_ptr<StereoFdn<N>> makeTank (double rate, double depth,
                                            const modulation::Schedule& schedule,
                                            LfoShape shape,
                                            StereoFdn<N>::Matrix::UpdateMode mode)
    {
        auto tank = std::make_unique<StereoFdn<N>>();
        tank->prepare (rate);
        tank->setInputDiffusionEnabled (true);
        tank->setRt60Seconds (2.0, true);
        tank->setModulationSchedule (schedule);
        tank->setModulationShape (shape);
        tank->setModulationRateHz (modulation::defaultRateHz);
        tank->setModulationUpdateMode (mode);
        tank->setModulationDepth (depth, true);
        return tank;
    }

    /** A single-rotation schedule, for the cost-per-rotation measurement. */
    constexpr RotationPair oneRotationPairs[] { { 0, 3 } };
    constexpr RotationPair twoRotationPairs[] { { 0, 3 }, { 1, 5 } };

    constexpr modulation::Schedule oneRotation { oneRotationPairs, 1, "1 rotation" };
    constexpr modulation::Schedule twoRotations { twoRotationPairs, 2, "2 rotations" };
}

void runMatrixModulationCpuBenchmark()
{
    beginSuite ("Matrix modulation / CPU cost");

    info ("Release build, MSVC 19.44, x64. Compiler options:");
    info ("  /O2 /Oi /Ot /fp:precise /permissive- /Zc:__cplusplus /utf-8 /GL (LTCG), static CRT.");
    info (std::to_string (benchmarkSamples) + " samples per run, " + std::to_string (repeats)
          + " runs per configuration, warm-up excluded, no debugger attached.");
    info ("Every configuration includes the 4-stage stereo input diffuser.");
    info ("");
    info ("  configuration                   median     of core   worst      of core");

    ScopedFtz ftz;

    using Mode = StereoFdn<N>::Matrix::UpdateMode;

    double worstDefault = 0.0, worstAnywhere = 0.0, staticAt48 = 0.0;

    for (double rate : { 48000.0, 96000.0, 192000.0 })
    {
        info ("");
        info ("--- " + fixed (rate / 1000.0, 1) + " kHz ---");

        auto run = [&] (const std::string& name, double depth,
                        const modulation::Schedule& schedule, LfoShape shape, Mode mode)
        {
            auto tank = makeTank (rate, depth, schedule, shape, mode);

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

        const double staticShare = run ("static FDN baseline (Depth 0)", 0.0,
                                        modulation::scheduleC, LfoShape::sine, Mode::perSample);

        if (std::abs (rate - 48000.0) < 1.0)
            staticAt48 = staticShare;

        run ("1 rotation, per-sample", modulation::defaultDepth, oneRotation,
             LfoShape::sine, Mode::perSample);
        run ("2 rotations, per-sample", modulation::defaultDepth, twoRotations,
             LfoShape::sine, Mode::perSample);

        run ("4 rotations, per-sample, sine", modulation::defaultDepth, modulation::scheduleC,
             LfoShape::sine, Mode::perSample);
        run ("4 rotations, control-rate", modulation::defaultDepth, modulation::scheduleC,
             LfoShape::sine, Mode::controlRate);
        run ("4 rotations, smooth random", modulation::defaultDepth, modulation::scheduleC,
             LfoShape::smoothRandom, Mode::perSample);

        // The shipped configuration: the two-layer schedule, eight rotations.
        const double defaultShare = run ("shipped default (8 rot, sine)",
                                         modulation::defaultDepth, modulation::defaultSchedule,
                                         LfoShape::sine, Mode::perSample);

        run ("worst case: 8 rot, max depth/rate", 1.0, modulation::scheduleAB,
             LfoShape::sine, Mode::perSample);

        if (std::abs (rate - 48000.0) < 1.0)
            worstDefault = defaultShare;

        // Dual-bank morph with modulation running.
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setRt60Seconds (2.0, true);
            tank->setModulationDepth (modulation::defaultDepth, true);
            tank->setPrimeCapSeconds (4.0);   // stay inside the transition

            const double share = measure ("dual-bank morph + modulation", rate, [&] (int n)
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
    info ("shipped default (8 rotations, per-sample, sine) at 48 kHz: "
          + fixed (worstDefault, 3) + " % of one core, worst case");
    info ("most expensive configuration anywhere in the matrix: "
          + fixed (worstAnywhere, 3) + " % of one core");

    // Asserted as a ratio to the static tank measured in the same run, not as
    // absolute nanoseconds. This threshold has fired twice on machine state
    // rather than on a code change: a leftover build process tripled every
    // figure in the table at once, which an absolute limit reads as a DSP
    // regression and a ratio does not.
    const double ratio = staticAt48 > 0.0 ? worstDefault / staticAt48 : 0.0;

    info ("shipped default is " + fixed (ratio, 2) + "x the static tank measured in this run");

    checkCpuAtMost (ratio, 3.0,
                 "the shipped default costs less than three times the static tank");

    checkCpuAtMost (worstDefault, 8.0,
                 "and stays inside a generous 8 % absolute ceiling at 48 kHz");
    checkCpuAtMost (worstAnywhere, 30.0,
                 "even the research worst case at 192 kHz fits in 30 % of one core");
}

} // namespace avtest
