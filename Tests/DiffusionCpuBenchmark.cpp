#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/DiffusionNetwork.h"
#include "DSP/StereoDiffusionNetwork.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>

using namespace av::dsp;

namespace avtest
{

namespace
{
    volatile double sink = 0.0;

    constexpr int benchmarkSamples = 2'000'000;

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

    /** ns per sample, plus the share of one core needed to sustain `rate`. */
    template <typename Body>
    double measure (const std::string& name, double rate, Body&& body)
    {
        for (int n = 0; n < 20000; ++n)
            body (n);

        const auto start = std::chrono::steady_clock::now();

        for (int n = 0; n < benchmarkSamples; ++n)
            body (n);

        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double seconds     = std::chrono::duration<double> (elapsed).count();
        const double nsPerSample = seconds * 1.0e9 / benchmarkSamples;
        const double coreShare   = nsPerSample * rate / 1.0e7;

        info (pad (name, 26) + pad (fixed (nsPerSample, 2) + " ns/sample", 20)
              + fixed (coreShare, 3) + " % of one core");

        return nsPerSample;
    }
}

void runDiffusionCpuBenchmark()
{
    beginSuite ("Diffusion / CPU cost");

    info ("Release build, MSVC, x64. Compiler options:");
    info ("  /O2 /Oi /Ot /fp:precise /permissive- /Zc:__cplusplus /utf-8 /GL (LTCG), static CRT.");
    info ("  /fp:fast is deliberately NOT used: feedback paths need predictable rounding.");
    info (std::to_string (benchmarkSamples) + " samples per measurement, flush-to-zero enabled,");
    info ("input is a non-silent ramp so no measurement runs on a denormal path.");
    info ("");

    ScopedFtz ftz;

    double worstCoreShare = 0.0;

    for (double rate : { 48000.0, 96000.0 })
    {
        info ("--- " + fixed (rate / 1000.0, 1) + " kHz ---");

        for (int stages : { 2, 4, 8 })
        {
            auto net = std::make_unique<DiffusionNetwork<double>>();
            net->prepare (rate);
            net->setStageCount (stages, true);
            net->setDiffusion (0.75, true);

            const double ns = measure (std::to_string (stages) + "-stage mono", rate,
                                       [&] (int n)
                                       {
                                           sink = net->processSample (static_cast<double> (n & 63) * 0.01);
                                       });

            worstCoreShare = std::max (worstCoreShare, ns * rate / 1.0e7);
        }

        for (int stages : { 4, 8 })
        {
            auto net = std::make_unique<StereoDiffusionNetwork<double>>();
            net->prepare (rate);
            net->setStageCount (stages, true);
            net->setDiffusion (0.75, true);

            const double ns = measure (std::to_string (stages) + "-stage stereo", rate,
                                       [&] (int n)
                                       {
                                           double l = static_cast<double> (n & 63) * 0.01;
                                           double r = -l;
                                           net->processSample (l, r);
                                           sink = l + r;
                                       });

            worstCoreShare = std::max (worstCoreShare, ns * rate / 1.0e7);
        }

        info ("");
    }

    info ("worst configuration in the matrix: " + fixed (worstCoreShare, 3) + " % of one core");

    // A tripwire, not a tight budget: the diffusion block is the cheapest part
    // of the reverb and must stay that way.
    checkCpuAtMost (worstCoreShare, 8.0,
                 "the most expensive configuration (8-stage stereo at 96 kHz) fits in 8 % of one core");
}

} // namespace avtest
