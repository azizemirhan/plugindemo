#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DelayLine.h"
#include "DSP/Denormal.h"
#include "DSP/ModulatedDelay.h"
#include "DSP/SmoothedParameter.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

using namespace av::dsp;

namespace avtest
{

namespace
{
    /** Kept volatile so nothing measured here can be optimised away. */
    volatile double sink = 0.0;

    constexpr int    benchmarkSamples = 4'000'000;
    constexpr double referenceRate    = 48000.0;

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

    /**
        Times `body` over benchmarkSamples iterations and reports the cost both
        per sample and as a share of one core at 48 kHz — the number that
        actually decides whether an algorithm is affordable.
    */
    template <typename Body>
    double measure (const std::string& name, Body&& body)
    {
        // A short warm-up so the first measurement is not paying for cold
        // caches and branch predictors.
        for (int n = 0; n < 20000; ++n)
            body (n);

        const auto start = std::chrono::steady_clock::now();

        for (int n = 0; n < benchmarkSamples; ++n)
            body (n);

        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double seconds     = std::chrono::duration<double> (elapsed).count();
        const double nsPerSample = seconds * 1.0e9 / benchmarkSamples;
        const double coreShare   = nsPerSample * referenceRate / 1.0e7; // percent of one core

        info (pad (name, 42) + pad (fixed (nsPerSample, 3) + " ns/sample", 20)
              + fixed (coreShare, 4) + " % of one core @48k");

        return nsPerSample;
    }
}

void runCpuBenchmark()
{
    beginSuite ("CPU cost");

    info ("Release build, " + std::to_string (benchmarkSamples) + " samples per measurement.");
    info ("'% of one core' is the steady-state cost of running one instance at 48 kHz.");
    info ("");

    ScopedFtz ftz; // measure under the same conditions as the audio callback

    // --- Delay line ---------------------------------------------------------

    DelayLine<double> line;
    line.prepare (referenceRate, 0.5);

    const double nsNone = measure ("DelayLine  write + read (none)", [&] (int n)
    {
        line.write (static_cast<double> (n & 255) * 0.001);
        sink = line.read<Interpolation::none> (1234.5);
    });

    const double nsLinear = measure ("DelayLine  write + read (linear)", [&] (int n)
    {
        line.write (static_cast<double> (n & 255) * 0.001);
        sink = line.read<Interpolation::linear> (1234.5);
    });

    const double nsHermite = measure ("DelayLine  write + read (hermite)", [&] (int n)
    {
        line.write (static_cast<double> (n & 255) * 0.001);
        sink = line.read<Interpolation::hermite> (1234.5);
    });

    const double nsLagrange = measure ("DelayLine  write + read (lagrange3)", [&] (int n)
    {
        line.write (static_cast<double> (n & 255) * 0.001);
        sink = line.read<Interpolation::lagrange3> (1234.5);
    });

    info ("");
    info ("hermite costs " + fixed (nsHermite / std::max (nsLinear, 1.0e-9), 2)
          + "x linear and " + fixed (nsHermite / std::max (nsNone, 1.0e-9), 2) + "x an integer read");

    // --- Modulated delay ----------------------------------------------------

    info ("");

    ModulatedDelay<double> mdSine;
    mdSine.prepare (referenceRate, 0.2);
    mdSine.setBaseDelaySamples (2000.0, true);
    mdSine.setDepthSamples (24.0, true);
    mdSine.setRateHz (1.3);
    mdSine.setShape (LfoShape::sine);

    const double nsModSine = measure ("ModulatedDelay  processSample (sine)", [&] (int n)
    {
        sink = mdSine.processSample (static_cast<double> (n & 255) * 0.001);
    });

    ModulatedDelay<double> mdTri;
    mdTri.prepare (referenceRate, 0.2);
    mdTri.setBaseDelaySamples (2000.0, true);
    mdTri.setDepthSamples (24.0, true);
    mdTri.setRateHz (1.3);
    mdTri.setShape (LfoShape::triangle);

    measure ("ModulatedDelay  processSample (triangle)", [&] (int n)
    {
        sink = mdTri.processSample (static_cast<double> (n & 255) * 0.001);
    });

    ModulatedDelay<double> mdRand;
    mdRand.prepare (referenceRate, 0.2);
    mdRand.setBaseDelaySamples (2000.0, true);
    mdRand.setDepthSamples (24.0, true);
    mdRand.setRateHz (1.3);
    mdRand.setShape (LfoShape::smoothRandom);

    measure ("ModulatedDelay  processSample (smoothRandom)", [&] (int n)
    {
        sink = mdRand.processSample (static_cast<double> (n & 255) * 0.001);
    });

    // --- Smoothers ----------------------------------------------------------

    info ("");

    LinearSmoother<double> lin;
    lin.prepare (referenceRate, 0.02);
    lin.snapTo (0.0);
    lin.setTarget (1.0);

    measure ("LinearSmoother  getNextValue", [&] (int n)
    {
        if ((n & 1023) == 0)
            lin.setTarget ((n & 2047) ? 1.0 : 0.0);

        sink = lin.getNextValue();
    });

    OnePoleSmoother<double> pole;
    pole.prepare (referenceRate, 0.01);
    pole.snapTo (0.0);

    measure ("OnePoleSmoother  getNextValue", [&] (int n)
    {
        if ((n & 1023) == 0)
            pole.setTarget ((n & 2047) ? 1.0 : 0.0);

        sink = pole.getNextValue();
    });

    LogSmoother<double> logS;
    logS.prepare (referenceRate, 0.03, 20.0);
    logS.snapTo (1000.0);

    measure ("LogSmoother  advance(32), cost per sample", [&] (int n)
    {
        if ((n & 31) == 0)
        {
            if ((n & 1023) == 0)
                logS.setTarget ((n & 2047) ? 8000.0 : 200.0);

            sink = logS.advance (32);
        }
    });

    // --- Projection ---------------------------------------------------------

    info ("");

    const double fdnLines  = 16.0;
    const double fdnShare  = nsModSine * fdnLines * referenceRate / 1.0e7;

    info ("Projection: 16 modulated delay lines (a typical FDN core) would cost "
          + fixed (fdnShare, 2) + " % of one core at 48 kHz,");
    info ("before the mixing matrix, damping filters and diffusion stages are added.");

    // These are not tight limits — they are tripwires. A regression that makes
    // a primitive an order of magnitude slower should fail the build, while
    // normal machine-to-machine variation should not.
    checkCpuAtMost (nsHermite,   60.0, "hermite delay read stays under 60 ns/sample");
    checkCpuAtMost (nsLagrange,  60.0, "lagrange3 delay read stays under 60 ns/sample");
    checkCpuAtMost (nsModSine,  120.0, "modulated delay stays under 120 ns/sample");
    check (nsHermite < nsLagrange * 3.0 && nsLagrange < nsHermite * 3.0,
           "hermite and lagrange3 cost roughly the same");
    checkCpuAtMost (fdnShare, 25.0, "a 16-line modulated FDN core fits in 25 % of one core");
}

} // namespace avtest
