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

    struct Timing
    {
        double medianNs = 0.0;
        double worstNs  = 0.0;
    };

    /**
        Times `body` several times and reports the median and the worst run.

        A single run of a benchmark on a desktop machine measures the scheduler
        as much as the code. The median is what the code costs; the worst run is
        what a host would actually have to survive.
    */
    template <typename Body>
    Timing measure (const std::string& name, double rate, Body&& body)
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
            runs.push_back (std::chrono::duration<double> (elapsed).count()
                            * 1.0e9 / benchmarkSamples);
        }

        std::sort (runs.begin(), runs.end());

        Timing timing;
        timing.medianNs = runs[runs.size() / 2];
        timing.worstNs  = runs.back();

        const double medianShare = timing.medianNs * rate / 1.0e7;
        const double worstShare  = timing.worstNs  * rate / 1.0e7;

        info (pad (name, 34) + pad (fixed (timing.medianNs, 1) + " ns", 12)
              + pad (fixed (medianShare, 3) + " %", 11)
              + pad (fixed (timing.worstNs, 1) + " ns", 12)
              + fixed (worstShare, 3) + " %");

        return timing;
    }
}

void runFdnCpuBenchmark()
{
    beginSuite ("FDN / CPU cost");

    info ("Release build, MSVC 19.44, x64. Compiler options:");
    info ("  /O2 /Oi /Ot /fp:precise /permissive- /Zc:__cplusplus /utf-8 /GL (LTCG), static CRT.");
    info ("  /fp:fast is deliberately NOT used: feedback paths need predictable rounding.");
    info (std::to_string (benchmarkSamples) + " samples per run, " + std::to_string (repeats)
          + " runs per configuration, warm-up excluded, no debugger attached.");
    info ("");
    info ("  configuration                     median      of core    worst       of core");

    ScopedFtz ftz;

    double worstShare = 0.0;

    for (double rate : { 48000.0, 96000.0, 192000.0 })
    {
        info ("");
        info ("--- " + fixed (rate / 1000.0, 1) + " kHz ---");

        // --- the tank alone, no input diffusion ----------------------------
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setInputDiffusionEnabled (false);
            tank->setRt60Seconds (2.0, true);

            const auto t = measure ("8x8 FDN, no diffusion", rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShare = std::max (worstShare, t.worstNs * rate / 1.0e7);
        }

        // --- with the 4-stage stereo input diffuser -------------------------
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setInputDiffusionEnabled (true);
            tank->setRt60Seconds (2.0, true);

            const auto t = measure ("8x8 FDN + 4-stage diffusion", rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShare = std::max (worstShare, t.worstNs * rate / 1.0e7);
        }

        // --- during a Size morph, both banks running ------------------------
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setInputDiffusionEnabled (true);
            tank->setRt60Seconds (2.0, true);
            tank->setMorphSeconds (2.0); // long enough to stay in the morph

            const auto t = measure ("dual-bank Size morph active", rate, [&] (int n)
            {
                if (n % 100000 == 0)
                    tank->setSize (((n / 100000) % 2 == 0) ? 1.8 : 0.4);

                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShare = std::max (worstShare, t.worstNs * rate / 1.0e7);
        }

        // --- decay settings -------------------------------------------------
        for (double rt60 : { 0.1, 2.0, 10.0, 100.0 })
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            tank->prepare (rate);
            tank->setInputDiffusionEnabled (true);
            tank->setRt60Seconds (rt60, true);

            const auto t = measure ("decay " + fixed (rt60, 1) + " s", rate, [&] (int n)
            {
                double l = static_cast<double> (n & 63) * 0.01;
                double r = -l;
                tank->processSample (l, r);
                sink = l + r;
            });

            worstShare = std::max (worstShare, t.worstNs * rate / 1.0e7);
        }
    }

    info ("");
    info ("worst configuration anywhere in the matrix: " + fixed (worstShare, 3) + " % of one core");

    // A tripwire, not a budget. The tank is the expensive part of the reverb
    // and its cost has to stay in a range a DAW can host many instances of.
    checkCpuAtMost (worstShare, 25.0,
                 "the most expensive configuration fits in 25 % of one core");
}

} // namespace avtest
