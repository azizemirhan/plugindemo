#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DelayLine.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** Fills a line with n samples of silence. */
    void pushSilence (DelayLine<double>& line, int n)
    {
        for (int i = 0; i < n; ++i)
            line.write (0.0);
    }
}

void runDelayLineTests()
{
    // =======================================================================
    beginSuite ("DelayLine / basic integer delay");
    // =======================================================================
    {
        DelayLine<double> line;
        line.prepare (48000.0, 0.1);

        check (line.isPrepared(), "prepared");

        // prepare() rounds up, and 0.1 is not exactly representable, so the
        // capacity is allowed to land one sample above the nominal length.
        check (line.getMaxDelaySamples() >= 4800 && line.getMaxDelaySamples() <= 4801,
               "0.1 s at 48 kHz reserves ~4800 samples");
        check (line.getBufferSize() >= line.getMaxDelaySamples() + 4,
               "buffer has headroom for the widest kernel");
        check ((line.getBufferSize() & (line.getBufferSize() - 1)) == 0,
               "buffer size is a power of two");

        line.write (1.0);
        checkClose (line.readAt (0), 1.0, 0.0, "delay 0 returns the sample just written");

        pushSilence (line, 5);
        checkClose (line.readAt (5), 1.0, 0.0, "impulse reappears at delay 5");
        checkClose (line.readAt (4), 0.0, 0.0, "delay 4 is silent");
        checkClose (line.readAt (6), 0.0, 0.0, "delay 6 is silent");

        // A counting sequence makes every tap independently checkable.
        line.reset();
        for (int n = 1; n <= 1000; ++n)
            line.write (static_cast<double> (n));

        bool allTapsCorrect = true;
        for (int d = 0; d < 900; ++d)
            allTapsCorrect &= (line.readAt (d) == static_cast<double> (1000 - d));

        check (allTapsCorrect, "900 simultaneous taps all read the correct sample");
    }

    // =======================================================================
    beginSuite ("DelayLine / fractional delay");
    // =======================================================================
    {
        DelayLine<double> line;
        line.prepare (48000.0, 0.05);

        // Ramp: sample written at step n has value n, so the value at delay d
        // is (N - d) after N writes. A fractional delay must land exactly on
        // the line between two integer taps.
        for (int n = 1; n <= 2000; ++n)
            line.write (static_cast<double> (n));

        checkClose (line.read<Interpolation::linear>    (10.0), 1990.0, 1.0e-9, "linear at an integer delay matches readAt");
        checkClose (line.read<Interpolation::hermite>   (10.0), 1990.0, 1.0e-9, "hermite at an integer delay matches readAt");
        checkClose (line.read<Interpolation::lagrange3> (10.0), 1990.0, 1.0e-9, "lagrange3 at an integer delay matches readAt");

        checkClose (line.read<Interpolation::linear>    (10.5), 1989.5, 1.0e-9, "linear halfway between taps");
        checkClose (line.read<Interpolation::hermite>   (10.5), 1989.5, 1.0e-9, "hermite halfway between taps");
        checkClose (line.read<Interpolation::lagrange3> (10.5), 1989.5, 1.0e-9, "lagrange3 halfway between taps");

        checkClose (line.read<Interpolation::hermite> (10.25), 1989.75, 1.0e-9, "hermite at t=0.25");
        checkClose (line.read<Interpolation::hermite> (10.75), 1989.25, 1.0e-9, "hermite at t=0.75");

        checkClose (line.read<Interpolation::none> (10.9), 1990.0, 1.0e-9, "none truncates towards the newer tap");

        // Default read must be the documented kernel.
        checkClose (line.read (10.5), line.read<Interpolation::lagrange3> (10.5), 0.0,
                    "default read() is lagrange3");
    }

    // =======================================================================
    beginSuite ("DelayLine / interpolated sine accuracy");
    // =======================================================================
    {
        constexpr double sampleRate = 48000.0;
        constexpr double freq       = 2000.0;
        constexpr double delay      = 20.37;

        DelayLine<double> line;
        line.prepare (sampleRate, 0.01);

        double sumLinear = 0.0, sumHermite = 0.0, sumLagrange = 0.0;
        int counted = 0;

        for (int n = 0; n < 8000; ++n)
        {
            line.write (std::sin (2.0 * pi * freq * n / sampleRate));

            if (n < 64)
                continue; // let the line fill past the read position

            const double exact = std::sin (2.0 * pi * freq * (n - delay) / sampleRate);

            const double eL = line.read<Interpolation::linear>    (delay) - exact;
            const double eH = line.read<Interpolation::hermite>   (delay) - exact;
            const double eG = line.read<Interpolation::lagrange3> (delay) - exact;

            sumLinear   += eL * eL;
            sumHermite  += eH * eH;
            sumLagrange += eG * eG;
            ++counted;
        }

        auto rmsDb = [counted] (double sumSq)
        {
            return 20.0 * std::log10 (std::max (std::sqrt (sumSq / counted), 1.0e-300));
        };

        info ("RMS error vs. the analytic delayed sine (2 kHz, 20.37 samples):");
        info ("  linear    " + std::to_string (rmsDb (sumLinear))   + " dB");
        info ("  hermite   " + std::to_string (rmsDb (sumHermite))  + " dB");
        info ("  lagrange3 " + std::to_string (rmsDb (sumLagrange)) + " dB");

        check (sumHermite  < sumLinear, "hermite is more accurate than linear on a real signal");
        check (sumLagrange < sumLinear, "lagrange3 is more accurate than linear on a real signal");
        checkAtMost (rmsDb (sumHermite), -70.0, "hermite error is below -70 dB");
    }

    // =======================================================================
    beginSuite ("DelayLine / buffer wraparound");
    // =======================================================================
    {
        DelayLine<double> line;
        line.prepareSamples (100); // small on purpose, so wrapping happens often

        const int bufferSize = line.getBufferSize();
        const int total      = bufferSize * 17 + 13; // many wraps, ending mid-buffer

        for (int n = 1; n <= total; ++n)
            line.write (static_cast<double> (n));

        bool ok = true;
        for (int d = 0; d <= 100; ++d)
            ok &= (line.readAt (d) == static_cast<double> (total - d));

        check (ok, "all taps stay correct after " + std::to_string (17) + " full wraps");

        // Fractional reads must also survive a wrap boundary.
        checkClose (line.read<Interpolation::hermite> (50.5),
                    static_cast<double> (total - 50) - 0.5, 1.0e-9,
                    "fractional read is correct across the wrap");
    }

    // =======================================================================
    beginSuite ("DelayLine / maximum delay and clamping");
    // =======================================================================
    {
        DelayLine<double> line;
        line.prepareSamples (500);

        for (int n = 1; n <= 5000; ++n)
            line.write (static_cast<double> (n));

        checkClose (line.readAt (500), 4500.0, 0.0, "readAt(max) is valid");

        const double atMax = line.read<Interpolation::hermite> (500.0);
        checkClose (atMax, 4500.0, 1.0e-9, "interpolated read at max delay is valid");

        checkClose (line.read<Interpolation::hermite> (5000.0), atMax, 0.0,
                    "delay beyond max is clamped, not wrapped");
        checkClose (line.read<Interpolation::hermite> (-10.0),
                    line.read<Interpolation::hermite> (DelayLine<double>::minInterpolatedDelay), 0.0,
                    "negative delay is clamped to the minimum interpolated delay");

        // The real point of clamping: it must never read outside the buffer.
        bool finite = true;
        for (double d = -1000.0; d < 20000.0; d += 7.3)
            finite &= std::isfinite (line.read<Interpolation::hermite> (d));

        check (finite, "no out-of-range read produces a non-finite value");
    }

    // =======================================================================
    beginSuite ("DelayLine / sample-rate independence");
    // =======================================================================
    {
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            DelayLine<double> line;
            line.prepare (rate, 0.05);

            const int expectedMax = static_cast<int> (std::ceil (0.05 * rate));
            checkClose (line.getMaxDelaySamples(), expectedMax, 0,
                        std::to_string (static_cast<int> (rate)) + " Hz: capacity matches 0.05 s");

            // A 10 ms delay must be 10 ms at every rate.
            const int delaySamples = static_cast<int> (std::lround (0.01 * rate));

            line.write (1.0);
            pushSilence (line, delaySamples);

            checkClose (line.readAt (delaySamples), 1.0, 0.0,
                        std::to_string (static_cast<int> (rate)) + " Hz: impulse arrives after exactly 10 ms");
            checkClose (line.secondsToSamples (0.01), delaySamples, 1.0,
                        std::to_string (static_cast<int> (rate)) + " Hz: secondsToSamples agrees");
        }

        // Re-preparing at a new rate must resize rather than keep stale state.
        DelayLine<double> line;
        line.prepare (44100.0, 0.05);
        const int at441 = line.getBufferSize();
        line.prepare (96000.0, 0.05);

        check (line.getBufferSize() > at441, "re-preparing at a higher rate grows the buffer");
        checkClose (line.readAt (10), 0.0, 0.0, "re-preparing clears the old contents");
    }

    // =======================================================================
    beginSuite ("DelayLine / reset");
    // =======================================================================
    {
        DelayLine<double> line;
        line.prepare (48000.0, 0.02);

        for (int n = 0; n < 500; ++n)
            line.write (0.5);

        check (line.readAt (100) != 0.0, "line holds data before reset");

        line.reset();

        bool allZero = true;
        for (int d = 0; d <= line.getMaxDelaySamples(); ++d)
            allZero &= (line.readAt (d) == 0.0);

        check (allZero, "every tap reads zero after reset");
        check (line.getBufferSize() > 0, "reset does not release the buffer");
        checkClose (line.read<Interpolation::hermite> (13.7), 0.0, 0.0,
                    "fractional read is zero after reset");
    }
}

} // namespace avtest
