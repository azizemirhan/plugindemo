#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

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

    using Morph = MorphingStereoFdn<N>;
    using Candidate = architecture::Candidate;

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

    struct Cost { double medianNs = 0.0; double worstNs = 0.0; };

    template <typename Body>
    Cost measure (const std::string& name, double rate, Body&& body)
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

        const Cost cost { runs[runs.size() / 2], runs.back() };

        info (pad (name, 38) + pad (fixed (cost.medianNs, 1) + " ns", 11)
              + pad (fixed (cost.medianNs * rate / 1.0e7, 3) + " %", 10)
              + pad (fixed (cost.worstNs, 1) + " ns", 11)
              + fixed (cost.worstNs * rate / 1.0e7, 3) + " %");

        return cost;
    }

    std::unique_ptr<Morph> makeTank (double rate, Candidate candidate,
                                     bool compensation, double rt60 = 2.0)
    {
        auto tank = std::make_unique<Morph>();
        tank->prepare (rate);
        tank->setInputDiffusionEnabled (true);
        tank->setRt60Seconds (rt60, true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        architecture::apply (*tank, candidate, true);
        tank->setCompensationEnabled (compensation, true);
        return tank;
    }
}

void runPhase5cCpuBenchmark()
{
    beginSuite ("PHASE 5C / CPU cost");

    info ("Release build, MSVC 19.44, x64. Compiler options:");
    info ("  /O2 /Oi /Ot /fp:precise /permissive- /Zc:__cplusplus /utf-8 /GL (LTCG), static CRT.");
    info (std::to_string (benchmarkSamples) + " samples per run, " + std::to_string (repeats)
          + " runs per configuration, warm-up excluded, no debugger attached.");
    info ("Every configuration includes the 4-stage stereo input diffuser and runs through");
    info ("MorphingStereoFdn, so the dual-bank machinery is present even when idle.");
    info ("");
    info ("Costs are also reported as a multiple of the static tank measured in the same");
    info ("run. Absolute nanoseconds move by a few per cent with machine state — a stray");
    info ("build server process tripled every figure in one run of this suite — so the");
    info ("ratio is the number that means something across runs.");
    info ("");
    info ("  configuration                         median     of core   worst      of core");

    ScopedFtz ftz;

    double shippedShare48 = 0.0;
    double worstShareAnywhere = 0.0;
    double shippedRatio48 = 0.0;

    for (double rate : { 48000.0, 96000.0, 192000.0 })
    {
        info ("");
        info ("--- " + fixed (rate / 1000.0, 1) + " kHz ---");

        const auto run = [&] (const std::string& name, Candidate candidate, bool compensation)
        {
            auto tank = makeTank (rate, candidate, compensation);

            const auto cost = measure (name, rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShareAnywhere = std::max (worstShareAnywhere, cost.worstNs * rate / 1.0e7);
            return cost;
        };

        const auto staticCost = run ("A static FDN", Candidate::staticFdn, false);

        run ("B matrix-only", Candidate::matrixOnly, false);

        const auto allpassCost = run ("C allpass-only (shipped)", Candidate::allpassOnly, false);

        run ("D light hybrid", Candidate::lightHybrid, false);
        run ("E medium hybrid", Candidate::mediumHybrid, false);

        run ("A static + Size compensation", Candidate::staticFdn, true);

        const auto shippedCost = run ("C allpass-only + compensation",
                                      Candidate::allpassOnly, true);

        // Worst-case rate and depth on the shipped architecture.
        {
            auto tank = makeTank (rate, Candidate::allpassOnly, true);
            tank->setAllpassRateHz (allpassMod::maxRateHz);
            tank->setAllpassDepth (1.0, true);

            const auto cost = measure ("C at maximum rate and depth", rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShareAnywhere = std::max (worstShareAnywhere, cost.worstNs * rate / 1.0e7);
        }

        // Dual-bank morph, with and without the rest.
        {
            auto tank = makeTank (rate, Candidate::staticFdn, false);
            tank->setPrimeCapSeconds (4.0);

            const auto cost = measure ("dual-bank Size morph, static", rate, [&] (int n)
            {
                if (n % 200000 == 0)
                    tank->setSize (((n / 200000) % 2 == 0) ? 1.8 : 0.4);

                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShareAnywhere = std::max (worstShareAnywhere, cost.worstNs * rate / 1.0e7);
        }

        {
            auto tank = makeTank (rate, Candidate::allpassOnly, true);
            tank->setPrimeCapSeconds (4.0);

            const auto cost = measure ("morph + shipped modulation + comp", rate, [&] (int n)
            {
                if (n % 200000 == 0)
                    tank->setSize (((n / 200000) % 2 == 0) ? 1.8 : 0.4);

                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShareAnywhere = std::max (worstShareAnywhere, cost.worstNs * rate / 1.0e7);
        }

        info ("");
        info ("  Size compensation alone costs "
              + fixed (100.0 * ((shippedCost.medianNs / allpassCost.medianNs) - 1.0), 1)
              + " % on top of the modulated tank.");
        info ("  Shipped architecture is "
              + fixed (shippedCost.medianNs / staticCost.medianNs, 2)
              + "x the static tank.");

        if (std::abs (rate - 48000.0) < 1.0)
        {
            shippedShare48 = shippedCost.worstNs * rate / 1.0e7;
            shippedRatio48 = shippedCost.medianNs / staticCost.medianNs;
        }
    }

    info ("");
    info ("shipped configuration at 48 kHz: " + fixed (shippedShare48, 3)
          + " % of one core, worst case, "
          + fixed (shippedRatio48, 2) + "x the static tank");
    info ("most expensive configuration anywhere in the matrix: "
          + fixed (worstShareAnywhere, 3) + " % of one core");

    // The ratio is the regression test; the absolute share is a sanity ceiling
    // generous enough that machine state cannot trip it.
    checkCpuAtMost (shippedRatio48, 3.0,
                 "the shipped architecture costs less than three times the static tank");

    checkCpuAtMost (shippedShare48, 6.0,
                 "and fits inside 6 % of one core at 48 kHz");

    checkCpuAtMost (worstShareAnywhere, 40.0,
                 "even the worst configuration at 192 kHz fits inside 40 % of one core");
}

} // namespace avtest
