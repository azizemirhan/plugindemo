#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Lfo.h"
#include "DSP/ModulatedDelay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    using MD = ModulatedDelay<double, Interpolation::lagrange3>;

    /** Largest sample-to-sample step in a signal — the click detector. */
    double maxFirstDifference (const std::vector<double>& x, int from = 1)
    {
        double worst = 0.0;

        for (size_t i = static_cast<size_t> (std::max (1, from)); i < x.size(); ++i)
            worst = std::max (worst, std::abs (x[i] - x[i - 1]));

        return worst;
    }
}

void runModulatedDelayTests()
{
    // =======================================================================
    beginSuite ("Lfo / shapes");
    // =======================================================================
    {
        for (auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::smoothRandom })
        {
            Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (shape);
            lfo.setRateHz (5.0);
            lfo.setSeed (12345u);
            lfo.reset();

            const char* name = shape == LfoShape::sine     ? "sine"
                             : shape == LfoShape::triangle ? "triangle"
                                                           : "smoothRandom";

            std::vector<double> values;
            values.reserve (48000);

            for (int n = 0; n < 48000; ++n)
                values.push_back (lfo.getNextValue());

            const double lo = *std::min_element (values.begin(), values.end());
            const double hi = *std::max_element (values.begin(), values.end());

            check (lo >= -1.0000001 && hi <= 1.0000001,
                   std::string (name) + ": stays inside [-1, 1]");

            // Continuity: at 5 Hz / 48 kHz no shape may step more than a small
            // fraction of full scale in one sample.
            checkAtMost (maxFirstDifference (values), 0.01,
                         std::string (name) + ": value is continuous");

            check (hi - lo > 0.5, std::string (name) + ": actually modulates");
        }

        // Determinism matters for reproducible tests and for giving each delay
        // line a stable, decorrelated noise stream.
        Lfo a, b, c;
        for (auto* l : { &a, &b, &c })
        {
            l->prepare (48000.0);
            l->setShape (LfoShape::smoothRandom);
            l->setRateHz (3.0);
        }

        a.setSeed (1u); b.setSeed (1u); c.setSeed (999u);
        a.reset(); b.reset(); c.reset();

        bool sameSeedIdentical = true, differentSeedDiverges = false;

        for (int n = 0; n < 20000; ++n)
        {
            const double va = a.getNextValue();
            const double vb = b.getNextValue();
            const double vc = c.getNextValue();

            sameSeedIdentical    &= (va == vb);
            differentSeedDiverges |= (std::abs (va - vc) > 0.1);
        }

        check (sameSeedIdentical,      "smoothRandom: identical seeds produce identical streams");
        check (differentSeedDiverges,  "smoothRandom: different seeds decorrelate");

        // Phase offset must shift a periodic shape without changing its shape.
        // Two lines an eighth of a cycle apart is how the algorithms will keep
        // delay lines from modulating in lockstep.
        Lfo p1, p2;
        for (auto* l : { &p1, &p2 })
        {
            l->prepare (48000.0);
            l->setShape (LfoShape::sine);
            l->setRateHz (1.0);
        }

        p2.setPhaseOffset (0.25);
        p1.reset();
        p2.reset();

        (void) p1.getNextValue();
        check (std::abs (p2.getNextValue() - 1.0) < 0.001,
               "phase offset 0.25 starts a sine at its positive peak");

        // A quarter-cycle offset means p2 leads p1 by exactly 12000 samples at
        // 1 Hz / 48 kHz.
        std::vector<double> s1, s2;
        p1.reset();
        p2.reset();

        for (int n = 0; n < 48000; ++n)
        {
            s1.push_back (p1.getNextValue());
            s2.push_back (p2.getNextValue());
        }

        double worstAlignment = 0.0;
        for (int n = 0; n < 24000; ++n)
            worstAlignment = std::max (worstAlignment, std::abs (s2[static_cast<size_t> (n)]
                                                               - s1[static_cast<size_t> (n + 12000)]));

        checkAtMost (worstAlignment, 1.0e-9, "phase offset is an exact time shift of the waveform");
    }

    // =======================================================================
    beginSuite ("ModulatedDelay / zero depth");
    // =======================================================================
    {
        MD md;
        md.prepare (48000.0, 0.1);
        md.setBaseDelaySamples (100.0, true);
        md.setDepthSamples (0.0, true);
        md.setRateHz (3.0);

        std::vector<double> out;
        out.reserve (400);

        for (int n = 0; n < 400; ++n)
            out.push_back (md.processSample (n == 0 ? 1.0 : 0.0));

        checkClose (out[100], 1.0, 1.0e-12, "impulse emerges after exactly 100 samples");
        checkClose (out[99],  0.0, 1.0e-12, "sample 99 is silent");
        checkClose (out[101], 0.0, 1.0e-12, "sample 101 is silent");
        checkClose (md.getCurrentDelaySamples(), 100.0, 1.0e-12,
                    "delay stays exactly at the base with zero depth");

        // With zero depth the modulated delay must be bit-identical to a plain
        // delay line reading at the same position.
        DelayLine<double> plain;
        plain.prepare (48000.0, 0.1);

        MD md2;
        md2.prepare (48000.0, 0.1);
        md2.setBaseDelaySamples (37.0, true);
        md2.setDepthSamples (0.0, true);

        bool identical = true;
        for (int n = 0; n < 5000; ++n)
        {
            const double x = std::sin (2.0 * pi * 700.0 * n / 48000.0);
            plain.write (x);
            identical &= (md2.processSample (x) == plain.read<Interpolation::lagrange3> (37.0));
        }

        check (identical, "zero depth is bit-identical to a static delay line read");
    }

    // =======================================================================
    beginSuite ("ModulatedDelay / low modulation");
    // =======================================================================
    {
        constexpr double sampleRate = 48000.0;
        constexpr double base       = 200.0;
        constexpr double depth      = 10.0;
        constexpr double rate       = 2.0;

        MD md;
        md.prepare (sampleRate, 0.1);
        md.setBaseDelaySamples (base, true);
        md.setDepthSamples (depth, true);
        md.setRateHz (rate);
        md.setShape (LfoShape::sine);

        double lo = 1.0e9, hi = -1.0e9, maxSlope = 0.0, previous = base;

        for (int n = 0; n < static_cast<int> (sampleRate); ++n)
        {
            (void) md.processSample (0.0);

            const double d = md.getCurrentDelaySamples();
            lo = std::min (lo, d);
            hi = std::max (hi, d);

            if (n > 0)
                maxSlope = std::max (maxSlope, std::abs (d - previous));

            previous = d;
        }

        check (lo >= base - depth - 1.0e-9 && hi <= base + depth + 1.0e-9,
               "modulated delay stays inside base +/- depth");
        checkClose (hi - lo, 2.0 * depth, 0.05, "modulation uses the full depth");

        // The slope of the delay is the pitch shift. Compare the measurement
        // against the analytic prediction the algorithms will budget against.
        const double predictedSlope = 2.0 * pi * depth * rate / sampleRate;
        checkClose (maxSlope, predictedSlope, predictedSlope * 0.03,
                    "measured peak delay slope matches 2*pi*depth*rate/fs");

        const double measuredCents  = 1200.0 * std::log2 (1.0 + maxSlope);
        const double predictedCents = MD::peakPitchDeviationCents (depth, rate, sampleRate, LfoShape::sine);

        info ("peak pitch deviation: measured " + std::to_string (measuredCents)
              + " cents, predicted " + std::to_string (predictedCents) + " cents");

        checkClose (measuredCents, predictedCents, predictedCents * 0.03,
                    "peakPitchDeviationCents() predicts the measured detune");

        // Round trip through the inverse.
        const double depthForFive = MD::depthForPitchDeviation (5.0, rate, sampleRate, LfoShape::sine);
        checkClose (MD::peakPitchDeviationCents (depthForFive, rate, sampleRate, LfoShape::sine),
                    5.0, 1.0e-9, "depthForPitchDeviation() inverts peakPitchDeviationCents()");

        info ("depth for a 5 cent budget at 2 Hz / 48 kHz: "
              + std::to_string (depthForFive) + " samples");
    }

    // =======================================================================
    beginSuite ("ModulatedDelay / high modulation");
    // =======================================================================
    {
        constexpr double sampleRate = 48000.0;

        MD md;
        md.prepare (sampleRate, 0.05);
        md.setBaseDelaySamples (1200.0, true);
        md.setRateHz (8.0);
        md.setShape (LfoShape::sine);

        const double safeDepth = md.getMaxSafeDepthSamples();
        check (safeDepth > 1000.0, "maxSafeDepth reflects the room between the base and the ends");

        md.setDepthSamples (safeDepth * 0.99, true);

        bool finite = true, bounded = true, inRange = true;
        double peak = 0.0;

        for (int n = 0; n < 96000; ++n)
        {
            const double x = std::sin (2.0 * pi * 500.0 * n / sampleRate);
            const double y = md.processSample (x);

            finite  &= std::isfinite (y);
            peak     = std::max (peak, std::abs (y));

            const double d = md.getCurrentDelaySamples();
            inRange &= (d >= 1.0 && d <= md.getDelayLine().getMaxDelaySamples());
        }

        // Hermite can overshoot slightly on a fast-moving read; anything beyond
        // a few percent would mean the interpolation is misbehaving.
        bounded = peak <= 1.05;

        check (finite,  "output stays finite at extreme depth");
        check (inRange, "read position never leaves the delay line");
        check (bounded, "output peak stays within 5 % of the input peak");
        info ("peak output at 99 % of max safe depth: " + std::to_string (peak));

        // Depth beyond the safe limit must clamp rather than read out of bounds.
        md.setDepthSamples (100000.0, true);

        bool stillSafe = true;
        for (int n = 0; n < 48000; ++n)
        {
            const double y = md.processSample (std::sin (2.0 * pi * 500.0 * n / sampleRate));
            stillSafe &= std::isfinite (y);
            stillSafe &= (md.getCurrentDelaySamples() >= 1.0);
            stillSafe &= (md.getCurrentDelaySamples() <= md.getDelayLine().getMaxDelaySamples());
        }

        check (stillSafe, "absurd depth clamps safely instead of reading out of bounds");
    }

    // =======================================================================
    beginSuite ("ModulatedDelay / delay-time changes are click-free");
    // =======================================================================
    {
        constexpr double sampleRate = 48000.0;
        constexpr double freq       = 1000.0;

        // 312 samples at 1 kHz / 48 kHz is 6.5 periods, so an instant jump
        // inverts the phase — the worst case a click test can be given.
        constexpr double fromDelay = 100.0;
        constexpr double toDelay   = 412.0;

        constexpr int changeAt = 4800;
        constexpr int totalSamples = 24000;

        auto runJump = [&] (bool immediate)
        {
            MD md;
            md.prepare (sampleRate, 0.2);
            md.setBaseDelaySamples (fromDelay, true);
            md.setDepthSamples (0.0, true);
            md.setRateHz (0.0);

            std::vector<double> all;
            all.reserve (totalSamples);

            for (int n = 0; n < totalSamples; ++n)
            {
                if (n == changeAt)
                    md.setBaseDelaySamples (toDelay, immediate);

                all.push_back (md.processSample (std::sin (2.0 * pi * freq * n / sampleRate)));
            }

            // The step that matters is the one straddling the change, between
            // the last sample at the old delay and the first at the new one, so
            // both regions are measured out of a single continuous render.
            double steady = 0.0, transition = 0.0;

            for (int i = 201; i < changeAt; ++i)
                steady = std::max (steady, std::abs (all[static_cast<size_t> (i)]
                                                   - all[static_cast<size_t> (i - 1)]));

            for (int i = changeAt; i < totalSamples; ++i)
                transition = std::max (transition, std::abs (all[static_cast<size_t> (i)]
                                                           - all[static_cast<size_t> (i - 1)]));

            return std::pair { steady, transition };
        };

        const auto smoothed  = runJump (false);
        const auto immediate = runJump (true);

        info ("max sample-to-sample step  steady: " + std::to_string (smoothed.first)
              + "   smoothed jump: " + std::to_string (smoothed.second)
              + "   instant jump: " + std::to_string (immediate.second));

        checkAtMost (smoothed.second, smoothed.first * 2.0,
                     "smoothed delay change never exceeds twice the steady-state step");

        check (immediate.second > smoothed.second * 5.0,
               "an instant jump really does click (control case)");

        // The glide must also arrive.
        MD md;
        md.prepare (sampleRate, 0.2);
        md.setBaseDelaySamples (fromDelay, true);
        md.setDepthSamples (0.0, true);
        md.setBaseDelaySamples (toDelay);

        for (int n = 0; n < 48000; ++n)
            (void) md.processSample (0.0);

        checkClose (md.getCurrentDelaySamples(), toDelay, 1.0e-6,
                    "the glide settles exactly on the requested delay");
    }

    // =======================================================================
    beginSuite ("ModulatedDelay / reset and re-prepare");
    // =======================================================================
    {
        MD md;
        md.prepare (48000.0, 0.1);
        md.setBaseDelaySamples (500.0, true);
        md.setDepthSamples (5.0, true);
        md.setRateHz (4.0);

        for (int n = 0; n < 2000; ++n)
            (void) md.processSample (1.0);

        check (std::abs (md.processSample (0.0)) > 0.5, "line holds signal before reset");

        md.reset();

        double energy = 0.0;
        for (int n = 0; n < 600; ++n)
            energy += std::abs (md.processSample (0.0));

        checkClose (energy, 0.0, 1.0e-12, "reset clears the delay memory");
        checkClose (md.getCurrentDelaySamples(), 500.0, 6.0,
                    "reset restarts modulation from the base delay");
    }
}

} // namespace avtest
