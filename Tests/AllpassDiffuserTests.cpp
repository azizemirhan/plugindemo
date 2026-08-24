#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/AllpassDiffuser.h"

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

    using ApLinear    = AllpassDiffuser<double, Interpolation::linear>;
    using ApHermite   = AllpassDiffuser<double, Interpolation::hermite>;
    using ApLagrange3 = AllpassDiffuser<double, Interpolation::lagrange3>;
    using Ap          = ApLagrange3;

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string dbString (double magnitude, int decimals = 3)
    {
        return fixed (20.0 * std::log10 (std::max (magnitude, 1.0e-300)), decimals);
    }

    template <typename Allpass>
    Allpass makeAllpass (double sampleRate, double delaySamples, double g)
    {
        Allpass ap;
        ap.prepare (sampleRate, 0.05);
        ap.setDelaySamples (delaySamples, true);
        ap.setCoefficient (g, true);
        return ap;
    }
}

void runAllpassDiffuserTests()
{
    constexpr double sampleRate = 48000.0;

    // =======================================================================
    beginSuite ("Allpass / impulse response matches the equation");
    // =======================================================================
    {
        constexpr double m = 100.0;

        for (double g : { 0.0, 0.3, 0.5, 0.7, 0.9 })
        {
            auto ap = makeAllpass<Ap> (sampleRate, m, g);
            const auto ir = impulseResponseOf (ap, 1024);

            checkClose (ir[0], -g, 1.0e-12,
                        "g=" + fixed (g, 1) + ": h[0] = -g");
            checkClose (ir[100], 1.0 - g * g, 1.0e-12,
                        "g=" + fixed (g, 1) + ": h[M] = 1 - g^2");
            checkClose (ir[200], g * (1.0 - g * g), 1.0e-12,
                        "g=" + fixed (g, 1) + ": h[2M] = g(1 - g^2)");
            checkClose (ir[300], g * g * (1.0 - g * g), 1.0e-12,
                        "g=" + fixed (g, 1) + ": h[3M] = g^2(1 - g^2)");

            // Everything between the taps must be exactly silent.
            double betweenTaps = 0.0;
            for (int n = 1; n < 100; ++n)
                betweenTaps = std::max (betweenTaps, std::abs (ir[static_cast<size_t> (n)]));

            checkAtMost (betweenTaps, 1.0e-20,
                         "g=" + fixed (g, 1) + ": nothing between the echo taps");
        }
    }

    // =======================================================================
    beginSuite ("Allpass / energy preservation, integer delay");
    // =======================================================================
    {
        // An allpass has unit magnitude at every frequency, so an impulse in
        // must give exactly unit energy out.
        for (double g : { 0.0, 0.3, 0.5, 0.7, 0.9, 0.95 })
        {
            auto ap = makeAllpass<Ap> (sampleRate, 97.0, g);
            const auto ir = impulseResponseOf (ap, 200000);

            checkClose (energy (ir), 1.0, 1.0e-9,
                        "g=" + fixed (g, 2) + ": total energy is exactly 1");
        }
    }

    // =======================================================================
    beginSuite ("Allpass / frequency response with a fractional delay");
    // =======================================================================
    {
        // With a fractional read the interpolator sits inside the feedback
        // loop, so the section is only approximately allpass. This is the
        // measurement that decides which kernel the diffuser uses — the PHASE 2
        // result was open-loop and does not automatically carry over.
        constexpr size_t fftSize = 32768;
        constexpr double m = 100.37;
        constexpr double g = 0.7;

        struct Result { double energyError; double maxAboveDb; double maxBelowDb; double lowDb; double highDb; };

        auto measure = [&] (auto&& ap) -> Result
        {
            const auto ir = impulseResponseOf (ap, fftSize);
            const auto mag = magnitudeSpectrum (ir, fftSize, false);

            const auto full = bandDeviation (mag, sampleRate, fftSize, 20.0, 20000.0);
            const auto low  = bandDeviation (mag, sampleRate, fftSize, 20.0, 200.0);
            const auto high = bandDeviation (mag, sampleRate, fftSize, 10000.0, 20000.0);

            return { std::abs (energy (ir) - 1.0),
                     full.maxAboveDb, full.maxBelowDb,
                     std::max (low.maxAboveDb, low.maxBelowDb),
                     std::max (high.maxAboveDb, high.maxBelowDb) };
        };

        auto fractional = [&] (auto&& ap)
        {
            ap.setQuantisation (std::decay_t<decltype (ap)>::Quantisation::fractional);
            return measure (ap);
        };

        auto roundedAp = makeAllpass<ApLagrange3> (sampleRate, m, g);
        const auto rRounded = measure (roundedAp);

        auto linAp  = makeAllpass<ApLinear>    (sampleRate, m, g);
        auto hermAp = makeAllpass<ApHermite>   (sampleRate, m, g);
        auto lagrAp = makeAllpass<ApLagrange3> (sampleRate, m, g);

        const auto rLin  = fractional (linAp);
        const auto rHerm = fractional (hermAp);
        const auto rLagr = fractional (lagrAp);

        info ("M = 100.37 samples, g = 0.7, deviation from a flat response:");
        info ("  mode                  energy err   max +dB   max -dB   LF dev    HF dev");

        auto row = [] (const char* name, const Result& r)
        {
            return "  " + std::string (name) + std::string (22 - std::string (name).size(), ' ')
                 + fixed (r.energyError, 6) + "     "
                 + fixed (r.maxAboveDb, 3) + "     "
                 + fixed (r.maxBelowDb, 3) + "    "
                 + fixed (r.lowDb, 3) + "     "
                 + fixed (r.highDb, 3);
        };

        info (row ("rounded", rRounded));
        info (row ("fractional linear", rLin));
        info (row ("fractional hermite", rHerm));
        info (row ("fractional lagrange3", rLagr));

        // The headline result: rounding the loop delay restores the allpass
        // property exactly, and no interpolation kernel comes close.
        checkAtMost (rRounded.energyError, 1.0e-9,
                     "a rounded loop delay preserves energy exactly");
        checkAtMost (rRounded.maxAboveDb, 1.0e-6,
                     "a rounded loop delay has a perfectly flat magnitude response");

        check (rRounded.energyError < rLagr.energyError * 1.0e-3,
               "rounding beats the best interpolator by more than three orders of magnitude");

        // Among the interpolators, the PHASE 2 ranking is re-verified inside
        // the loop rather than assumed to carry over.
        check (rLagr.energyError < rLin.energyError,
               "lagrange3 preserves energy better than linear inside the loop");
        check (rHerm.energyError < rLin.energyError,
               "hermite preserves energy better than linear inside the loop");

        checkAtMost (rLagr.lowDb, 0.05,
                     "lagrange3: low frequencies are untouched even in fractional mode");

        // The property that matters most in a feedback path is not average
        // accuracy but whether the loop can ever exceed unity gain.
        info (rHerm.maxAboveDb > rLagr.maxAboveDb
                ? "hermite rises further above unity than lagrange3 (worse in a feedback loop)"
                : "lagrange3 rises further above unity than hermite");
    }

    // =======================================================================
    beginSuite ("Allpass / coefficient stability boundaries");
    // =======================================================================
    {
        // Sweep the coefficient and measure how long the section rings and how
        // peaky its spectrum becomes. The point is to replace "0.7 feels right"
        // with a number.
        constexpr size_t fftSize = 32768;
        constexpr double m = 100.37;

        info ("   g     decay to -60 dB    peak above median    spectral flatness");

        double previousDecay = 0.0;
        bool decayMonotonic = true, allStable = true, allFiniteOutputs = true;

        for (double g : { 0.3, 0.5, 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95 })
        {
            auto ap = makeAllpass<Ap> (sampleRate, m, g);
            const auto ir = impulseResponseOf (ap, fftSize);

            allFiniteOutputs &= allFinite (ir);

            // Time for the running energy to reach 99.9 % of the total.
            const double total = energy (ir);
            double running = 0.0;
            size_t decaySample = 0;

            for (size_t n = 0; n < ir.size(); ++n)
            {
                running += ir[n] * ir[n];

                if (running >= 0.999 * total)
                {
                    decaySample = n;
                    break;
                }
            }

            const auto mag = magnitudeSpectrum (ir, fftSize, false);
            const double peakDb  = peakAboveMedianDb (mag, sampleRate, fftSize, 100.0, 15000.0);
            const double flatness = spectralFlatness (mag, sampleRate, fftSize, 100.0, 15000.0);

            const double decayMs = 1000.0 * static_cast<double> (decaySample) / sampleRate;

            info ("  " + fixed (g, 2) + "      " + fixed (decayMs, 2) + " ms"
                  + std::string (decayMs < 10.0 ? 12 : 11, ' ')
                  + fixed (peakDb, 3) + " dB              " + fixed (flatness, 4));

            decayMonotonic &= (decayMs >= previousDecay - 1.0e-9);
            previousDecay = decayMs;

            // Stability: the tail must be decaying, not growing.
            const double firstHalf  = energy (ir.data(), ir.size() / 2);
            const double secondHalf = energy (ir.data() + ir.size() / 2, ir.size() / 2);
            allStable &= (secondHalf < firstHalf);
        }

        check (decayMonotonic,    "ringing time grows monotonically with the coefficient");
        check (allStable,         "every coefficient up to the clamp produces a decaying tail");
        check (allFiniteOutputs,  "no coefficient produces a non-finite output");

        // The clamp itself.
        auto ap = makeAllpass<Ap> (sampleRate, 100.0, 5.0);
        checkClose (ap.getCoefficient(), Ap::stabilityLimit, 1.0e-12,
                    "a coefficient above the limit is clamped, not accepted");

        ap.setCoefficient (-5.0, true);
        checkClose (ap.getCoefficient(), -Ap::stabilityLimit, 1.0e-12,
                    "the clamp is symmetric for negative coefficients");

        check (Ap::recommendedMaxCoefficient < Ap::stabilityLimit,
               "the recommended ceiling sits below the hard clamp");
    }

    // =======================================================================
    beginSuite ("Allpass / delay range and fractional behaviour");
    // =======================================================================
    {
        Ap ap;
        ap.prepare (sampleRate, 0.05); // 2400 samples

        ap.setDelaySamples (0.0, true);
        checkClose (ap.getDelaySamples(), Ap::minLoopDelaySamples, 1.0e-12,
                    "a delay below the minimum is clamped");

        ap.setDelaySamples (1.0e6, true);
        checkClose (ap.getDelaySamples(), ap.getMaxDelaySamples(), 1.0e-12,
                    "a delay above the capacity is clamped");

        // Rounded mode: a half-sample request lands on a whole sample.
        auto rounded = makeAllpass<Ap> (sampleRate, 50.4, 0.7);
        const auto roundedIr = impulseResponseOf (rounded, 4096);

        checkClose (roundedIr[50], 1.0 - 0.49, 1.0e-12,
                    "a 50.4 sample request rounds to a clean echo at 50 samples");
        checkAtMost (std::abs (roundedIr[51]), 1.0e-20,
                     "nothing leaks into the neighbouring sample");

        // Fractional mode: the echo is spread across the kernel's taps, and the
        // interpolator inside the loop costs real energy.
        auto fractional = makeAllpass<Ap> (sampleRate, 50.5, 0.7);
        fractional.setQuantisation (Ap::Quantisation::fractional);
        const auto ir = impulseResponseOf (fractional, 4096);

        check (std::abs (ir[50]) > 1.0e-3 && std::abs (ir[51]) > 1.0e-3,
               "in fractional mode a half-sample delay spreads the echo across two samples");

        double echoSum = 0.0;
        for (size_t n = 48; n <= 53; ++n)
            echoSum += ir[n];

        checkClose (echoSum, 1.0 - 0.7 * 0.7, 0.03,
                    "the kernel's taps carry the whole 1-g^2 echo between them");

        auto rounded2 = makeAllpass<Ap> (sampleRate, 50.5, 0.7);
        auto fractional2 = makeAllpass<Ap> (sampleRate, 50.5, 0.7);
        fractional2.setQuantisation (Ap::Quantisation::fractional);

        const double roundedEnergy    = energy (impulseResponseOf (rounded2, 200000));
        const double fractionalEnergy = energy (impulseResponseOf (fractional2, 200000));

        info ("impulse energy:  rounded " + fixed (roundedEnergy, 9)
              + "   fractional " + fixed (fractionalEnergy, 9)
              + "  (" + dbString (std::sqrt (fractionalEnergy)) + " dB)");

        checkClose (roundedEnergy, 1.0, 1.0e-9, "rounded mode preserves energy exactly");
        check (fractionalEnergy < 0.99,
               "fractional mode measurably loses energy (the reason it is not the default)");
    }

    // =======================================================================
    beginSuite ("Allpass / reset, wraparound and long-running stability");
    // =======================================================================
    {
        auto ap = makeAllpass<Ap> (sampleRate, 613.7, 0.75);

        // Wraparound: run far more samples than the buffer holds.
        auto noise = whiteNoise (500000, 4242u);
        auto out = processThrough (ap, noise);

        check (allFinite (out), "500k samples of noise stay finite");
        checkAtMost (peakAbsolute (out) / peakAbsolute (noise), 4.0,
                     "peak gain stays bounded across the wrap");
        checkClose (rms (out) / rms (noise), 1.0, 0.05,
                    "RMS is preserved across 500k samples");

        ap.reset();

        double residual = 0.0;
        for (int n = 0; n < 4000; ++n)
            residual = std::max (residual, std::abs (ap.processSample (0.0)));

        checkAtMost (residual, 1.0e-20, "reset clears the section completely");

        // 60 seconds of full-scale noise.
        auto longAp = makeAllpass<Ap> (sampleRate, 411.3, 0.85);
        double worstPeak = 0.0;
        bool finite = true;

        unsigned int state = 7u;
        for (int n = 0; n < 48000 * 60; ++n)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

            const double y = longAp.processSample (x);
            worstPeak = std::max (worstPeak, std::abs (y));
            finite &= std::isfinite (y);
        }

        check (finite, "60 s of full-scale noise at g=0.85 stays finite");
        info ("worst peak over 60 s of full-scale noise: " + fixed (worstPeak, 4));
        checkAtMost (worstPeak, 8.0, "no runaway growth over 60 s");
    }

    // =======================================================================
    beginSuite ("Allpass / parameter automation is click-free");
    // =======================================================================
    {
        auto maxStep = [] (const std::vector<double>& x, size_t from)
        {
            double worst = 0.0;
            for (size_t i = std::max<size_t> (1, from); i < x.size(); ++i)
                worst = std::max (worst, std::abs (x[i] - x[i - 1]));
            return worst;
        };

        constexpr double freq = 1000.0;
        constexpr size_t total = 48000;
        constexpr size_t changeAt = 12000;

        auto runAutomation = [&] (bool immediate, bool moveDelay)
        {
            auto ap = makeAllpass<Ap> (sampleRate, 200.0, 0.4);

            std::vector<double> out (total);

            for (size_t n = 0; n < total; ++n)
            {
                if (n == changeAt)
                {
                    if (moveDelay) ap.setDelaySamples (900.0, immediate);
                    else           ap.setCoefficient (0.9, immediate);
                }

                out[n] = ap.processSample (std::sin (2.0 * pi * freq * static_cast<double> (n) / sampleRate));
            }

            const double steady = maxStep (std::vector<double> (out.begin() + 2000,
                                                                out.begin() + static_cast<ptrdiff_t> (changeAt)), 1);
            const double after  = maxStep (std::vector<double> (out.begin() + static_cast<ptrdiff_t> (changeAt) - 1,
                                                                out.end()), 1);

            return std::pair { steady, after };
        };

        const auto delaySmooth  = runAutomation (false, true);
        const auto delayInstant = runAutomation (true,  true);
        const auto coeffSmooth  = runAutomation (false, false);
        const auto coeffInstant = runAutomation (true,  false);

        info ("delay 200 -> 900 samples:  steady step " + fixed (delaySmooth.first, 5)
              + ", smoothed " + fixed (delaySmooth.second, 5)
              + ", instant " + fixed (delayInstant.second, 5));
        info ("coefficient 0.4 -> 0.9:    steady step " + fixed (coeffSmooth.first, 5)
              + ", smoothed " + fixed (coeffSmooth.second, 5)
              + ", instant " + fixed (coeffInstant.second, 5));

        checkAtMost (delaySmooth.second, delaySmooth.first * 2.0,
                     "a smoothed delay change never exceeds twice the steady-state step");
        checkAtMost (coeffSmooth.second, coeffSmooth.first * 2.0,
                     "a smoothed coefficient change never exceeds twice the steady-state step");

        check (delayInstant.second > delaySmooth.second * 3.0,
               "an instant delay change really does click (control case)");
        check (coeffInstant.second > coeffSmooth.second * 1.5,
               "an instant coefficient change really does step (control case)");

        // Continuous automation must not pump energy into the section.
        auto ap = makeAllpass<Ap> (sampleRate, 400.0, 0.7);
        auto noise = whiteNoise (240000, 99u);
        std::vector<double> out (noise.size());

        for (size_t n = 0; n < noise.size(); ++n)
        {
            const double phase = 2.0 * pi * 3.0 * static_cast<double> (n) / sampleRate;
            ap.setDelaySamples (400.0 + 300.0 * std::sin (phase));
            ap.setCoefficient (0.5 + 0.4 * std::sin (phase * 0.37));

            out[n] = ap.processSample (noise[n]);
        }

        check (allFinite (out), "continuous automation of both parameters stays finite");
        checkAtMost (rms (out) / rms (noise), 1.5,
                     "continuous automation does not pump energy into the section");
        info ("RMS gain under continuous automation: " + fixed (rms (out) / rms (noise), 4));
    }

    // =======================================================================
    beginSuite ("Allpass / denormal guard has no DC component");
    // =======================================================================
    {
        // The guard itself.
        AlternatingGuard guard;
        guard.reset();

        std::vector<double> guardValues (1000000);
        bool alternates = true, correctLevel = true;

        for (size_t n = 0; n < guardValues.size(); ++n)
        {
            guardValues[n] = guard.next();
            correctLevel &= (std::abs (guardValues[n]) == denormalGuard);

            if (n > 0)
                alternates &= (guardValues[n] * guardValues[n - 1] < 0.0);
        }

        check (alternates,   "the guard's sign alternates on every sample");
        check (correctLevel, "the guard's magnitude is exactly the documented constant");
        checkClose (mean (guardValues), 0.0, 1.0e-40,
                    "the guard's long-term mean is exactly zero");
        info ("guard level: " + dbString (denormalGuard, 1) + " dBFS");

        // What the guard style does to a recursive state. Simulated directly so
        // both styles can be compared with everything else held identical.
        auto settledMean = [] (bool alternating)
        {
            constexpr int loop = 100;
            constexpr double g = 0.8;

            std::vector<double> state (loop, 0.0);
            std::vector<double> output (400000);

            double sign = 1.0;
            int index = 0;

            for (size_t n = 0; n < output.size(); ++n)
            {
                sign = alternating ? -sign : 1.0;

                const double v = g * state[static_cast<size_t> (index)] + denormalGuard * sign;
                state[static_cast<size_t> (index)] = v;
                index = (index + 1) % loop;

                output[n] = v;
            }

            return mean (output);
        };

        const double fixedSignMean   = settledMean (false);
        const double alternatingMean = settledMean (true);

        info ("settled mean of a g=0.8 recursion:  fixed-sign guard "
              + std::to_string (fixedSignMean) + ",  alternating guard "
              + std::to_string (alternatingMean));

        check (fixedSignMean > 0.0, "a fixed-sign guard does accumulate into a DC offset");
        check (std::abs (alternatingMean) < std::abs (fixedSignMean) * 1.0e-3,
               "the alternating guard leaves at least 1000x less DC behind");

        // And in the real section.
        auto ap = makeAllpass<Ap> (sampleRate, 257.3, 0.85);
        (void) ap.processSample (1.0);

        std::vector<double> tail (2000000);
        for (auto& s : tail)
            s = ap.processSample (0.0);

        check (allFinite (tail), "a 40 s silent tail stays finite");
        check (! anySubnormal (tail.data(), tail.size()),
               "a 40 s silent tail never produces a subnormal");

        // The DC measurement is taken well after the impulse has decayed. An
        // allpass has unity gain at DC, so the impulse itself contributes a
        // running sum of 1 and would otherwise swamp the guard by 18 orders of
        // magnitude.
        checkAtMost (std::abs (mean (tail.data() + 1500000, 500000)), 1.0e-22,
                     "the settled silent tail leaves no DC offset");
        checkAtMost (peakAbsolute (tail.data() + 1500000, 500000), 1.0e-20,
                     "the settled silent output stays below -400 dBFS");
    }

    // =======================================================================
    beginSuite ("Allpass / sample rate and block size");
    // =======================================================================
    {
        // A delay specified in milliseconds must produce the same echo times at
        // every rate, including 192 kHz.
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            Ap ap;
            ap.prepare (rate, 0.05);

            // 10 ms is a whole number of samples at every rate in the matrix,
            // so the echo amplitude can be checked exactly as well as its time.
            ap.setDelayMilliseconds (10.0, true);
            ap.setCoefficient (0.6, true);

            const int expected = static_cast<int> (std::lround (0.010 * rate));
            const auto ir = impulseResponseOf (ap, static_cast<size_t> (expected) * 3);

            // The largest sample in the response after the direct output is the
            // first echo, and it must land at 5 ms.
            size_t peakIndex = 1;
            double peakValue = 0.0;

            for (size_t n = 1; n < ir.size(); ++n)
            {
                if (std::abs (ir[n]) > peakValue)
                {
                    peakValue = std::abs (ir[n]);
                    peakIndex = n;
                }
            }

            const std::string label = fixed (rate / 1000.0, 1) + " kHz";

            checkClose (static_cast<double> (peakIndex), expected, 1.0,
                        label + ": the first echo lands at 10 ms");
            checkClose (peakValue, 1.0 - 0.36, 1.0e-9, label + ": the echo has the right amplitude");
        }

        // Block size, including one larger than the size announced to prepare.
        auto reference = makeAllpass<Ap> (sampleRate, 333.7, 0.7);
        const auto input = whiteNoise (20000, 31u);
        const auto expected = processThrough (reference, input);

        bool allMatch = true;

        for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 4096 })
        {
            auto ap = makeAllpass<Ap> (sampleRate, 333.7, 0.7);

            std::vector<double> out = input;
            int n = 0;

            while (n < static_cast<int> (out.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (out.size()) - n);
                ap.processBlock (out.data() + n, count);
                n += count;
            }

            double worst = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
                worst = std::max (worst, std::abs (out[i] - expected[i]));

            allMatch &= (worst == 0.0);
        }

        check (allMatch, "block size 1 to 4096 all render bit-identically");

        // A runtime block eight times larger than the prepared size.
        {
            Ap ap;
            ap.prepare (sampleRate, 0.05, 0.02);
            ap.setDelaySamples (333.7, true);
            ap.setCoefficient (0.7, true);

            std::vector<double> out = input;
            ap.processBlock (out.data(), static_cast<int> (out.size()));

            double worst = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
                worst = std::max (worst, std::abs (out[i] - expected[i]));

            checkClose (worst, 0.0, 0.0,
                        "a single 20000-sample block matches the sample-by-sample render");
        }
    }

    // =======================================================================
    beginSuite ("Allpass / extreme inputs");
    // =======================================================================
    {
        auto ap = makeAllpass<Ap> (sampleRate, 128.0, 0.9);

        // Full-scale impulse train.
        bool finite = true;
        double worst = 0.0;

        for (int n = 0; n < 480000; ++n)
        {
            const double x = (n % 64 == 0) ? ((n % 128 == 0) ? 1.0 : -1.0) : 0.0;
            const double y = ap.processSample (x);

            finite &= std::isfinite (y);
            worst = std::max (worst, std::abs (y));
        }

        check (finite, "a 10 s full-scale impulse train stays finite");
        info ("worst peak from a full-scale impulse train at g=0.9: " + fixed (worst, 4));

        // Sudden jumps between the parameter extremes, every 64 samples.
        auto jumper = makeAllpass<Ap> (sampleRate, 100.0, 0.0);
        auto noise = whiteNoise (240000, 5150u);
        std::vector<double> out (noise.size());

        for (size_t n = 0; n < noise.size(); ++n)
        {
            if (n % 64 == 0)
            {
                const bool high = ((n / 64) % 2) == 0;
                jumper.setDelaySamples (high ? 2000.0 : 3.0);
                jumper.setCoefficient (high ? Ap::stabilityLimit : -Ap::stabilityLimit);
            }

            out[n] = jumper.processSample (noise[n]);
        }

        check (allFinite (out), "jumping between parameter extremes every 64 samples stays finite");
        info ("RMS gain under extreme parameter jumping: " + fixed (rms (out) / rms (noise), 4));
        checkAtMost (rms (out) / rms (noise), 3.0, "extreme jumping does not run away");

        // Silence in, silence out.
        auto quiet = makeAllpass<Ap> (sampleRate, 321.0, 0.9);
        std::vector<double> silence (200000, 0.0);
        const auto quietOut = processThrough (quiet, silence);

        checkAtMost (peakAbsolute (quietOut), 1.0e-20,
                     "silence in gives silence out, to within the guard level");
        check (allFinite (quietOut), "silence in stays finite");
    }
}

} // namespace avtest
