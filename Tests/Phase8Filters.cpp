#include "TestFramework.h"
#include "DspAnalysis.h"

#include "DSP/CutFilter.h"
#include "DSP/Denormal.h"
#include "DSP/PreDelay.h"
#include "DSP/SmoothedParameter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 8 - Low Cut, High Cut and Pre-Delay.

    The three controls were already declared, mapped, defaulted and smoothed
    before this phase; what was missing was that nothing applied them to
    audio. These tests cover what the new DSP does and, just as importantly,
    what it must not disturb.

    Two of the checks here exist because of specific failure modes identified
    before any code was written, both of which would have passed a casual
    listen:

      - the smoother consumption test. lowCut and highCut are advanced by
        advanceUnconsumed() once per chunk and read with getCurrentValue().
        Advancing them again in the filter stage would run every control-rate
        smoother at twice the rate. Automation would simply feel quicker. No
        audio test would fail.

      - the zero-delay test. DelayLine's interpolated read is clamped to >= 1
        sample, so a fractional pre-delay cannot reach zero. Reaching for it
        would have put a permanent one-sample delay on the wet path, which is
        inaudible, invisible and wrong.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    [[nodiscard]] std::string fx (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    [[nodiscard]] double toDb (double linear)
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear) : -300.0;
    }

    // -----------------------------------------------------------------------
    //  The analytic responses, written from the difference equations
    // -----------------------------------------------------------------------

    /**
        |LP(e^jw)| for the impulse-invariant one-pole, LP = (1-a)/(1 - a z^-1).

        This is the reference the implementation is measured against. It is
        deliberately NOT the ideal continuous-time RC response: the discrete
        filter is what ships, and comparing it to something it was never meant
        to be would turn a documented design property into a spurious error.
        How far it sits from the ideal is reported separately.
    */
    [[nodiscard]] double lowpassMagnitude (double a, double w)
    {
        const std::complex<double> z = std::polar (1.0, -w);
        return std::abs ((1.0 - a) / (1.0 - a * z));
    }

    /** |HP| for y = (x - LP(x)) * k. */
    [[nodiscard]] double highpassMagnitude (double a, double k, double w)
    {
        const std::complex<double> z = std::polar (1.0, -w);
        const std::complex<double> lp = (1.0 - a) / (1.0 - a * z);
        return std::abs ((1.0 - lp) * k);
    }

    /**
        |LP| for the bilinear-transform first-order lowpass, pre-warped at fc.

        Here as a REFERENCE, not as an implementation. The shipped filter is
        impulse invariant, matching DampingTuning.h; this exists so that the
        choice is a measured comparison rather than an inherited habit.
    */
    [[nodiscard]] double bltLowpassMagnitude (double fc, double sampleRate, double freq)
    {
        const double k = std::tan (pi * fc / sampleRate);
        const double b = k / (1.0 + k);
        const double a1 = (1.0 - k) / (1.0 + k);

        const std::complex<double> z = std::polar (1.0, -2.0 * pi * freq / sampleRate);

        return std::abs (b * (1.0 + z) / (1.0 - a1 * z));
    }

    /** The ideal first-order analogue responses, for reporting the warping. */
    [[nodiscard]] double idealHighpass (double f, double fc)
    {
        return f / std::sqrt (f * f + fc * fc);
    }

    [[nodiscard]] double idealLowpass (double f, double fc)
    {
        return fc / std::sqrt (f * f + fc * fc);
    }

    // -----------------------------------------------------------------------
    //  Measurement
    // -----------------------------------------------------------------------

    /**
        Steady-state magnitude at one frequency, by single-bin DFT.

        The obvious method - drive a sine and take the peak output sample - is
        wrong above a few kHz and wrong in a direction that looks like a
        filter defect. At 12 kHz and a 48 kHz rate there are four samples per
        cycle, so the largest sample can sit a quarter-cycle off the crest and
        under-read the true peak by up to 3 dB. The first run of this suite
        reported a 2.02 dB "error" that was entirely this.

        Correlating against cos and sin over a whole number of cycles recovers
        the amplitude exactly, whatever the sampling phase.
    */
    [[nodiscard]] double measureMagnitude (StereoCutFilters& f, double freqHz,
                                           double sampleRate)
    {
        f.reset();

        // A whole number of cycles, so the correlation has no leakage.
        const int cycles = std::max (16, static_cast<int> (freqHz * 0.25));
        const int measure = std::max (64, static_cast<int> (std::lround (cycles * sampleRate / freqHz)));
        const int settle = static_cast<int> (sampleRate * 0.5);

        double l = 0.0, r = 0.0;

        for (int n = 0; n < settle; ++n)
        {
            l = std::sin (2.0 * pi * freqHz * n / sampleRate);
            r = l;
            f.process (&l, &r, 1);
        }

        double cosSum = 0.0, sinSum = 0.0;

        for (int n = 0; n < measure; ++n)
        {
            const double phase = 2.0 * pi * freqHz * (settle + n) / sampleRate;

            l = std::sin (phase);
            r = l;
            f.process (&l, &r, 1);

            cosSum += l * std::cos (phase);
            sinSum += l * std::sin (phase);
        }

        return 2.0 * std::sqrt (cosSum * cosSum + sinSum * sinSum) / measure;
    }

    /** First sample index whose magnitude exceeds a threshold. */
    [[nodiscard]] int firstNonZero (const std::vector<double>& v, double threshold = 1.0e-12)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (std::abs (v[i]) > threshold)
                return static_cast<int> (i);

        return -1;
    }
}

// ===========================================================================

void runPhase8Tests()
{
    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 pre-delay - exact impulse timing");
    // -----------------------------------------------------------------------
    {
        const double rates[] { 44100.0, 48000.0, 96000.0 };
        const double times[] { 0.0, 1.0, 7.3, 20.0, 60.0, 250.0, 500.0 };

        info ("  An impulse in must come out at round(ms * fs / 1000) samples.");
        info ("  The tap is a whole number of samples and the fractional");
        info ("  interpolator is never used, so this is exact rather than close.");
        info ("");
        info ("    " + pad ("rate", 10) + pad ("ms", 8) + pad ("expected", 11)
              + pad ("measured", 11) + "peak");

        double worstTimingError = 0.0;
        double worstPeakError = 0.0;

        for (double rate : rates)
        {
            for (double ms : times)
            {
                PreDelay<double> pd;
                pd.prepare (rate);
                pd.snapTo (ms);

                const int expected = static_cast<int> (std::lround (ms * 0.001 * rate));

                const int length = expected + 64;

                std::vector<double> l (static_cast<size_t> (length), 0.0);
                std::vector<double> r = l;

                l[0] = 1.0;
                r[0] = 1.0;

                pd.process (l.data(), r.data(), length);

                const int measured = firstNonZero (l);

                worstTimingError = std::max (worstTimingError,
                                             std::abs (static_cast<double> (measured - expected)));

                const double peak = measured >= 0 ? l[static_cast<size_t> (measured)] : 0.0;

                worstPeakError = std::max (worstPeakError, std::abs (peak - 1.0));

                info ("    " + pad (fx (rate, 0), 10) + pad (fx (ms, 1), 8)
                      + pad (std::to_string (expected), 11)
                      + pad (std::to_string (measured), 11) + fx (peak, 12));
            }
        }

        checkEqual (worstTimingError, 0.0,
                    "every pre-delay lands on exactly the requested sample");

        checkAtMost (worstPeakError, 0.0,
                     "the impulse arrives with its amplitude unchanged");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 pre-delay - zero is exactly zero");
    // -----------------------------------------------------------------------
    {
        info ("  DelayLine's interpolated read is clamped to >= 1 sample, so a");
        info ("  fractional pre-delay cannot reach zero. Reaching for it would");
        info ("  have left a permanent one-sample delay on the wet path -");
        info ("  inaudible, invisible and wrong. The integer path is used");
        info ("  instead, and at tap 0 the sample is passed through untouched.");
        info ("");

        PreDelay<double> pd;
        pd.prepare (48000.0);
        pd.snapTo (0.0);

        check (pd.isBypassed(), "at 0 ms the stage reports itself bypassed");
        checkEqual (static_cast<long long> (pd.getCurrentTapSamples()), 0LL,
                    "the tap is 0 samples");

        std::vector<double> l, r;
        auto rng = whiteNoise (4096, 20250808u);

        l.assign (rng.begin(), rng.end());
        r = l;

        const auto originalL = l;
        const auto originalR = r;

        pd.process (l.data(), r.data(), static_cast<int> (l.size()));

        long long differing = 0;
        double worst = 0.0;

        for (size_t i = 0; i < l.size(); ++i)
        {
            if (l[i] != originalL[i] || r[i] != originalR[i])
                ++differing;

            worst = std::max (worst, std::abs (l[i] - originalL[i]));
            worst = std::max (worst, std::abs (r[i] - originalR[i]));
        }

        info ("    samples compared          " + std::to_string (l.size() * 2));
        info ("    samples differing         " + std::to_string (differing));
        info ("    worst difference          " + fx (worst, 18));

        checkEqual (differing, 0LL, "0 ms is a bit-exact identity, not an approximation");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 pre-delay - stereo alignment and block-size identity");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;
        const double ms = 37.0;

        // Stereo: a different signal per channel, same tap, so any divergence
        // in timing shows up as a channel that moved and one that did not.
        {
            PreDelay<double> pd;
            pd.prepare (rate);
            pd.snapTo (ms);

            const int expected = static_cast<int> (std::lround (ms * 0.001 * rate));
            const int length = expected + 128;

            std::vector<double> l (static_cast<size_t> (length), 0.0), r = l;
            l[0] = 1.0;
            r[3] = 1.0;

            pd.process (l.data(), r.data(), length);

            const int arriveL = firstNonZero (l);
            const int arriveR = firstNonZero (r);

            info ("    left impulse at 0  arrives at  " + std::to_string (arriveL));
            info ("    right impulse at 3 arrives at  " + std::to_string (arriveR));

            checkEqual (static_cast<long long> (arriveL), static_cast<long long> (expected),
                        "left channel tap is exact");
            checkEqual (static_cast<long long> (arriveR - arriveL), 3LL,
                        "right channel carries the same tap - the two cannot drift");
        }

        // Block-size identity, including across a crossfade.
        {
            auto run = [rate, ms] (int chunk)
            {
                PreDelay<double> pd;
                pd.prepare (rate);
                pd.snapTo (0.0);

                auto src = whiteNoise (8192, 99001u);

                std::vector<double> l (src.begin(), src.end()), r = l;

                // Ask for a move partway through, so the fade straddles a
                // chunk boundary in one case and not in the other.
                // The control event must land on the SAME SAMPLE in every run,
                // or this stops being a block-size test and becomes three
                // different automation sequences. The first version of this
                // test applied the change once per chunk, so the 4096 run -
                // one single chunk - never requested the move at all and
                // "failed" by 1.92. The event is pinned to sample 2048 and the
                // chunking happens within each half.
                const int eventAt = 2048;

                auto runSpan = [&] (int from, int to)
                {
                    int done = from;

                    while (done < to)
                    {
                        const int n = std::min (chunk, to - done);
                        pd.process (l.data() + done, r.data() + done, n);
                        done += n;
                    }
                };

                pd.setDelayMilliseconds (0.0);
                runSpan (0, eventAt);

                pd.setDelayMilliseconds (ms);
                runSpan (eventAt, static_cast<int> (l.size()));

                return l;
            };

            const auto a = run (4096);
            const auto b = run (64);
            const auto c = run (1);

            double worstAB = 0.0, worstAC = 0.0;

            for (size_t i = 0; i < a.size(); ++i)
            {
                worstAB = std::max (worstAB, std::abs (a[i] - b[i]));
                worstAC = std::max (worstAC, std::abs (a[i] - c[i]));
            }

            info ("    worst 4096 vs 64 difference   " + fx (worstAB, 18));
            info ("    worst 4096 vs 1 difference    " + fx (worstAC, 18));

            checkEqual (worstAB, 0.0, "one 4096 block equals 64-sample blocks, bit for bit");
            checkEqual (worstAC, 0.0, "and equals sample-by-sample processing");
        }
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 pre-delay - the crossfade, and rapid automation");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;

        PreDelay<double> pd;
        pd.prepare (rate);

        info ("  Policy: a fade always runs to completion. A request arriving");
        info ("  mid-fade is COALESCED into a single pending slot - a later one");
        info ("  overwrites it rather than queueing - and is honoured the moment");
        info ("  the fade ends. So automation faster than the fade produces a");
        info ("  sequence of complete fades converging on the last value asked");
        info ("  for, never a stutter of restarted ramps.");
        info ("");
        info ("    fade length  " + std::to_string (pd.getFadeLengthSamples()) + " samples ("
              + fx (1000.0 * pd.getFadeLengthSamples() / rate, 2) + " ms)");

        pd.snapTo (0.0);

        // Start a fade, then bombard it with new targets mid-flight.
        pd.setDelayMilliseconds (20.0);
        check (pd.isFading(), "a change from the idle state starts a fade");

        const int firstDestination = pd.tapFor (20.0);

        std::vector<double> l (16, 0.0), r (16, 0.0);

        pd.process (l.data(), r.data(), 8);

        pd.setDelayMilliseconds (50.0);
        pd.setDelayMilliseconds (80.0);
        pd.setDelayMilliseconds (35.0);

        checkEqual (static_cast<long long> (pd.getPendingTapSamples()),
                    static_cast<long long> (pd.tapFor (35.0)),
                    "three requests during one fade coalesce to the last of them");

        check (pd.isFading(), "the fade in flight is not restarted");

        // Run until the first fade completes; it must land on its original
        // destination and then immediately begin moving to the coalesced one.
        std::vector<double> big (static_cast<size_t> (pd.getFadeLengthSamples()), 0.0);
        std::vector<double> big2 = big;

        pd.process (big.data(), big2.data(), pd.getFadeLengthSamples());

        info ("    after the first fade: current tap " + std::to_string (pd.getCurrentTapSamples())
              + ", fading " + (pd.isFading() ? "yes" : "no"));

        check (pd.getCurrentTapSamples() == firstDestination || pd.isFading(),
               "the first fade reached its own destination before moving on");

        // Let everything settle, then confirm convergence on the last request.
        for (int i = 0; i < 8; ++i)
            pd.process (big.data(), big2.data(), pd.getFadeLengthSamples());

        checkEqual (static_cast<long long> (pd.getCurrentTapSamples()),
                    static_cast<long long> (pd.tapFor (35.0)),
                    "the taps converge on the last value requested");

        check (! pd.isFading(), "and the state machine comes to rest");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 pre-delay - no click, no stale audio, no NaN");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;

        // A sine through a transition: a linear crossfade of one correlated
        // signal against itself cannot exceed the input peak, and must not
        // produce a step.
        {
            PreDelay<double> pd;
            pd.prepare (rate);
            pd.snapTo (5.0);

            const int length = static_cast<int> (rate * 0.5);

            std::vector<double> l (static_cast<size_t> (length)), r (static_cast<size_t> (length));

            for (int n = 0; n < length; ++n)
            {
                l[static_cast<size_t> (n)] = std::sin (2.0 * pi * 440.0 * n / rate);
                r[static_cast<size_t> (n)] = l[static_cast<size_t> (n)];
            }

            // Warm the line so the taps read real audio rather than silence.
            pd.process (l.data(), r.data(), length);

            for (int n = 0; n < length; ++n)
            {
                l[static_cast<size_t> (n)] = std::sin (2.0 * pi * 440.0 * n / rate);
                r[static_cast<size_t> (n)] = l[static_cast<size_t> (n)];
            }

            pd.setDelayMilliseconds (25.0);
            pd.process (l.data(), r.data(), length);

            double peak = 0.0, worstStep = 0.0;

            for (size_t i = 0; i < l.size(); ++i)
            {
                peak = std::max (peak, std::abs (l[i]));

                if (i > 0)
                    worstStep = std::max (worstStep, std::abs (l[i] - l[i - 1]));
            }

            // One sample of a 440 Hz sine at 48 kHz steps by at most
            // 2*pi*440/48000 = 0.0576. A click would be far above that.
            const double naturalStep = 2.0 * pi * 440.0 / rate;

            info ("    peak through the transition   " + fx (peak, 6));
            info ("    worst sample-to-sample step   " + fx (worstStep, 6));
            info ("    a 440 Hz sine's natural step  " + fx (naturalStep, 6));

            checkAtMost (peak, 1.0 + 1.0e-9,
                         "a linear crossfade of correlated taps never exceeds unity");

            checkAtMost (worstStep, 3.0 * naturalStep,
                         "no discontinuity: the transition stays near the signal's own slope");

            check (allFinite (l) && allFinite (r), "no NaN or Inf through the transition");
        }

        // reset() must leave nothing behind.
        {
            PreDelay<double> pd;
            pd.prepare (rate);
            pd.snapTo (100.0);

            auto noise = whiteNoise (8192, 4242u);
            std::vector<double> l (noise.begin(), noise.end()), r = l;

            pd.process (l.data(), r.data(), static_cast<int> (l.size()));

            pd.reset();

            std::vector<double> silenceL (8192, 0.0), silenceR (8192, 0.0);
            pd.process (silenceL.data(), silenceR.data(), 8192);

            double worst = 0.0;

            for (size_t i = 0; i < silenceL.size(); ++i)
                worst = std::max (worst, std::max (std::abs (silenceL[i]), std::abs (silenceR[i])));

            info ("    worst output after reset, silence in   " + fx (worst, 18));

            checkEqual (worst, 0.0, "reset() leaves no stale audio in the delay buffer");
        }
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 cut filters - magnitude against the analytic response");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;

        info ("  Measured against the implemented difference equation, not an");
        info ("  idealised RC curve. The discrete filter is what ships; how far");
        info ("  it sits from the ideal is reported in the next suite rather");
        info ("  than treated as an error here.");
        info ("");
        info ("    " + pad ("filter", 10) + pad ("corner", 10) + pad ("probe", 10)
              + pad ("measured", 12) + pad ("analytic", 12) + "error dB");

        double worstError = 0.0;

        struct Probe { double corner, freq; };

        const Probe lowProbes[] {
            { 100.0, 50.0 }, { 100.0, 200.0 }, { 100.0, 1000.0 },
            { 200.0, 60.0 }, { 200.0, 200.0 }, { 200.0, 2000.0 },
            { 1000.0, 300.0 }, { 1000.0, 1000.0 }, { 1000.0, 8000.0 },
        };

        for (const auto& p : lowProbes)
        {
            StereoCutFilters f;
            f.prepare (rate);
            f.setLowCut (p.corner, false);
            f.setHighCut (highCutOffHz, true);

            const double measured = measureMagnitude (f, p.freq, rate);
            const double analytic = highpassMagnitude (f.getLowCoefficient(),
                                                       f.getLowNormalisation(),
                                                       2.0 * pi * p.freq / rate);

            const double errorDb = std::abs (toDb (measured) - toDb (analytic));
            worstError = std::max (worstError, errorDb);

            info ("    " + pad ("low cut", 10) + pad (fx (p.corner, 0), 10)
                  + pad (fx (p.freq, 0), 10) + pad (fx (toDb (measured), 4), 12)
                  + pad (fx (toDb (analytic), 4), 12) + fx (errorDb, 6));
        }

        const Probe highProbes[] {
            { 2000.0, 500.0 }, { 2000.0, 2000.0 }, { 2000.0, 12000.0 },
            { 8000.0, 1000.0 }, { 8000.0, 8000.0 }, { 8000.0, 18000.0 },
            { 16000.0, 1000.0 }, { 16000.0, 16000.0 },
        };

        for (const auto& p : highProbes)
        {
            StereoCutFilters f;
            f.prepare (rate);
            f.setLowCut (lowCutOffHz, true);
            f.setHighCut (p.corner, false);

            const double measured = measureMagnitude (f, p.freq, rate);
            const double analytic = lowpassMagnitude (f.getHighCoefficient(),
                                                      2.0 * pi * p.freq / rate);

            const double errorDb = std::abs (toDb (measured) - toDb (analytic));
            worstError = std::max (worstError, errorDb);

            info ("    " + pad ("high cut", 10) + pad (fx (p.corner, 0), 10)
                  + pad (fx (p.freq, 0), 10) + pad (fx (toDb (measured), 4), 12)
                  + pad (fx (toDb (analytic), 4), 12) + fx (errorDb, 6));
        }

        info ("");
        info ("    worst deviation from the analytic response  " + fx (worstError, 6) + " dB");

        checkAtMost (worstError, 0.05,
                     "both filters realise their own transfer function");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 cut filters - the one-pole's documented deviations");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;

        info ("  The impulse-invariant one-pole does not reach its far-side");
        info ("  asymptote. Reported, not corrected: correcting it would cost");
        info ("  the unconditional stability DampingTuning.h chose it for.");
        info ("");
        info ("    low cut: passband gain at Nyquist, with and without the");
        info ("    (1+a)/2a normalisation that PHASE 8 applies");
        info ("");
        info ("    " + pad ("corner", 10) + pad ("raw dB", 12) + pad ("normalised dB", 16) + "k");

        double worstNormalised = 0.0;

        for (double corner : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0 })
        {
            StereoCutFilters f;
            f.prepare (rate);
            f.setLowCut (corner, false);

            const double a = f.getLowCoefficient();
            const double k = f.getLowNormalisation();

            const double raw        = highpassMagnitude (a, 1.0, pi);
            const double normalised = highpassMagnitude (a, k,   pi);

            worstNormalised = std::max (worstNormalised, std::abs (toDb (normalised)));

            info ("    " + pad (fx (corner, 0), 10) + pad (fx (toDb (raw), 4), 12)
                  + pad (fx (toDb (normalised), 4), 16) + fx (k, 6));
        }

        checkAtMost (worstNormalised, 0.001,
                     "the normalisation makes the low-cut passband exactly unity at Nyquist");

        info ("");
        info ("    high cut: discrete response against the ideal first-order");
        info ("    analogue curve, which it deliberately is not");
        info ("");
        info ("    " + pad ("corner", 10) + pad ("probe", 10) + pad ("discrete", 12)
              + pad ("ideal", 12) + "difference");

        for (double corner : { 2000.0, 8000.0, 16000.0 })
        {
            for (double probe : { corner, 2.0 * corner, 20000.0 })
            {
                if (probe >= 0.5 * rate)
                    continue;

                StereoCutFilters f;
                f.prepare (rate);
                f.setHighCut (corner, false);

                const double discrete = lowpassMagnitude (f.getHighCoefficient(),
                                                          2.0 * pi * probe / rate);
                const double ideal = idealLowpass (probe, corner);

                info ("    " + pad (fx (corner, 0), 10) + pad (fx (probe, 0), 10)
                      + pad (fx (toDb (discrete), 3), 12) + pad (fx (toDb (ideal), 3), 12)
                      + fx (toDb (discrete) - toDb (ideal), 3) + " dB");
            }
        }

        info ("");
        info ("    The discrete filter rolls off more gently than the ideal near");
        info ("    Nyquist. For a control whose brief is smooth and musical");
        info ("    rather than surgical, that is the harmless direction.");

        // The low cut, against its own ideal, at the settings a user reaches for.
        info ("");
        info ("    low cut attenuation, discrete vs ideal first order");
        info ("    " + pad ("corner", 10) + pad ("probe", 10) + pad ("discrete", 12) + "ideal");

        for (double corner : { 100.0, 200.0, 400.0 })
        {
            for (double probe : { 0.3535534 * corner, 0.5 * corner, corner })
            {
                StereoCutFilters f;
                f.prepare (rate);
                f.setLowCut (corner, false);

                const double discrete = highpassMagnitude (f.getLowCoefficient(),
                                                           f.getLowNormalisation(),
                                                           2.0 * pi * probe / rate);
                const double ideal = idealHighpass (probe, corner);

                info ("    " + pad (fx (corner, 0), 10) + pad (fx (probe, 1), 10)
                      + pad (fx (toDb (discrete), 3), 12) + fx (toDb (ideal), 3));
            }
        }

        info ("");
        info ("    1.5 octaves below the corner a first-order high-pass gives");
        info ("    -9.54 dB by construction. The earlier >= 10 dB escalation");
        info ("    criterion was arithmetically impossible for the chosen order");
        info ("    and has been withdrawn; the filter is held to its own");
        info ("    response instead. Whether 6 dB/oct is musically enough is a");
        info ("    listening question, not a slope threshold.");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 cut filters - endpoint bypass, and the step it leaves");
    // -----------------------------------------------------------------------
    {
        const double rate = 48000.0;

        // Bit identity when both are parked on their endpoints.
        {
            StereoCutFilters f;
            f.prepare (rate);
            f.setLowCut  (lowCutOffHz,  true);
            f.setHighCut (highCutOffHz, true);

            check (f.isBypassed(), "both endpoints park the stage in bypass");

            auto noise = whiteNoise (4096, 8080u);
            std::vector<double> l (noise.begin(), noise.end()), r = l;
            const auto originalL = l;

            f.process (l.data(), r.data(), static_cast<int> (l.size()));

            long long differing = 0;

            for (size_t i = 0; i < l.size(); ++i)
                if (l[i] != originalL[i])
                    ++differing;

            checkEqual (differing, 0LL, "bypass is a bit-exact identity");
        }

        // An unsettled smoother must NOT bypass, even sitting on the endpoint.
        {
            StereoCutFilters f;
            f.prepare (rate);
            f.setLowCut (lowCutOffHz, false);

            check (f.isLowActive(),
                   "a cut gliding toward its endpoint keeps filtering until it settles");
        }

        info ("");
        info ("  The endpoint means OFF, so there is a step at the boundary.");
        info ("  Measured rather than morphed away.");
        info ("");
        info ("    " + pad ("probe", 10) + pad ("bypass dB", 12) + pad ("at 20.1 Hz", 14) + "step dB");

        StereoCutFilters lowEdge;
        lowEdge.prepare (rate);
        lowEdge.setLowCut (20.1, false);

        double worstLowStep = 0.0;

        for (double probe : { 20.0, 30.0, 50.0, 100.0, 200.0 })
        {
            const double active = highpassMagnitude (lowEdge.getLowCoefficient(),
                                                     lowEdge.getLowNormalisation(),
                                                     2.0 * pi * probe / rate);

            const double stepDb = std::abs (toDb (active));

            if (probe >= 30.0)
                worstLowStep = std::max (worstLowStep, stepDb);

            info ("    " + pad (fx (probe, 0) + " Hz", 10) + pad ("0.0000", 12)
                  + pad (fx (toDb (active), 4), 14) + fx (stepDb, 4));
        }

        info ("");
        info ("    " + pad ("probe", 10) + pad ("bypass dB", 12) + pad ("at 19.9 kHz", 14) + "step dB");

        StereoCutFilters highEdge;
        highEdge.prepare (rate);
        highEdge.setHighCut (19900.0, false);

        double worstHighStep = 0.0;

        for (double probe : { 1000.0, 5000.0, 10000.0, 16000.0, 20000.0 })
        {
            const double active = lowpassMagnitude (highEdge.getHighCoefficient(),
                                                    2.0 * pi * probe / rate);

            const double stepDb = std::abs (toDb (active));
            worstHighStep = std::max (worstHighStep, stepDb);

            info ("    " + pad (fx (probe, 0) + " Hz", 10) + pad ("0.0000", 12)
                  + pad (fx (toDb (active), 4), 14) + fx (stepDb, 4));
        }

        info ("");
        info ("    worst low-cut step at and above 30 Hz   " + fx (worstLowStep, 4) + " dB");
        info ("    worst high-cut step below 20 kHz        " + fx (worstHighStep, 4) + " dB");
        info ("");
        info ("    The low cut's step is concentrated at 20 Hz, which is the");
        info ("    bottom of hearing and below most monitoring. The high cut's");
        info ("    is spread across the top octaves and is the larger of the");
        info ("    two; it is recorded here for the listening pass to judge.");

        /*
            What is gated here is that the step is EXACTLY the filter's own
            response and nothing more - no extra discontinuity hiding in the
            bypass logic. How big that response is at 30 Hz is a product fact
            for the listening pass to judge, not a number to pick a threshold
            around: a first-order high-pass at 20.1 Hz takes 1.6 dB out at
            30 Hz by construction, and an arbitrary 1 dB gate would only have
            been a vote against first order, which is already decided.
        */
        {
            StereoCutFilters measured;
            measured.prepare (rate);
            measured.setLowCut (20.1, false);

            double worstDeviation = 0.0;

            for (double probe : { 30.0, 50.0, 100.0, 200.0 })
            {
                const double m = toDb (measureMagnitude (measured, probe, rate));
                const double a = toDb (highpassMagnitude (lowEdge.getLowCoefficient(),
                                                          lowEdge.getLowNormalisation(),
                                                          2.0 * pi * probe / rate));

                worstDeviation = std::max (worstDeviation, std::abs (m - a));
            }

            checkAtMost (worstDeviation, 0.05,
                         "the step at the boundary is the filter's own response, with nothing extra");
        }
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 cut filters - sample rates, Nyquist, automation, finiteness");
    // -----------------------------------------------------------------------
    {
        info ("    the same corner must give the same response at every rate");
        info ("");
        info ("    " + pad ("corner", 10) + pad ("probe", 10) + pad ("44.1k", 11)
              + pad ("48k", 11) + pad ("96k", 11) + "spread dB");

        double worstSpread = 0.0;

        struct Pair { double corner, probe; };

        /*
            Two physical regimes, so two gates rather than one invented number.

            The LOW CUT lives at 20-1000 Hz, which is 2 % of Nyquist even at
            44.1 kHz. Nothing warps there and the gate is tight: anything
            above a few hundredths of a dB would mean a coefficient computed
            against the wrong sample rate, which is a bug.

            The HIGH CUT reaches 20 kHz, where warping is unavoidable for any
            first-order IIR - the measured comparison against the bilinear
            form is below. Its gate is loose enough to admit the warping and
            tight enough to still catch a wrong-rate coefficient, which would
            be many dB rather than a fraction of one.

            Earlier revisions of this suite gated both at a single invented
            number, first 0.75 dB and then 0.25 dB. Both were picked to sit
            near whatever had just been measured, which is not a gate. These
            two are picked from what each regime can physically achieve, and
            the measurement is printed either way.
        */
        double worstLowRegime = 0.0;

        for (const auto& p : { Pair { 100.0, 50.0 }, Pair { 200.0, 100.0 },
                               Pair { 500.0, 1000.0 }, Pair { 1000.0, 300.0 },
                               Pair { 2000.0, 4000.0 }, Pair { 8000.0, 2000.0 },
                               Pair { 8000.0, 8000.0 } })
        {
            double values[3];
            int i = 0;

            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                StereoCutFilters f;
                f.prepare (rate);

                if (p.corner <= 1000.0)
                    f.setLowCut (p.corner, false);
                else
                    f.setHighCut (p.corner, false);

                values[i++] = toDb (measureMagnitude (f, p.probe, rate));
            }

            const double spread = *std::max_element (values, values + 3)
                                - *std::min_element (values, values + 3);

            worstSpread = std::max (worstSpread, spread);

            if (p.corner <= 1000.0)
                worstLowRegime = std::max (worstLowRegime, spread);

            info ("    " + pad (fx (p.corner, 0), 10) + pad (fx (p.probe, 0), 10)
                  + pad (fx (values[0], 4), 11) + pad (fx (values[1], 4), 11)
                  + pad (fx (values[2], 4), 11) + fx (spread, 4));
        }

        info ("");
        info ("    The gate above covers probes below a quarter of the lowest");
        info ("    rate, where sample-rate independence is achievable. Above");
        info ("    that it is not achievable by ANY first-order IIR: 16 kHz is");
        info ("    73 % of Nyquist at 44.1 kHz, and every discretisation has to");
        info ("    distort the analogue curve somewhere up there. Reported:");
        info ("");
        info ("    " + pad ("corner", 9) + pad ("probe", 9) + pad ("form", 20)
              + pad ("44.1k", 10) + pad ("48k", 10) + pad ("96k", 10) + "spread");

        for (const auto& p : { Pair { 8000.0, 16000.0 }, Pair { 4000.0, 16000.0 } })
        {
            double impulseInv[3], blt[3];
            int i = 0;

            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                StereoCutFilters f;
                f.prepare (rate);
                f.setHighCut (p.corner, false);

                impulseInv[i] = toDb (lowpassMagnitude (f.getHighCoefficient(),
                                                        2.0 * pi * p.probe / rate));
                blt[i] = toDb (bltLowpassMagnitude (p.corner, rate, p.probe));
                ++i;
            }

            auto spreadOf = [] (const double* v)
            {
                return *std::max_element (v, v + 3) - *std::min_element (v, v + 3);
            };

            info ("    " + pad (fx (p.corner, 0), 9) + pad (fx (p.probe, 0), 9)
                  + pad ("impulse invariant", 20)
                  + pad (fx (impulseInv[0], 3), 10) + pad (fx (impulseInv[1], 3), 10)
                  + pad (fx (impulseInv[2], 3), 10) + fx (spreadOf (impulseInv), 3));

            info ("    " + pad ("", 9) + pad ("", 9) + pad ("bilinear", 20)
                  + pad (fx (blt[0], 3), 10) + pad (fx (blt[1], 3), 10)
                  + pad (fx (blt[2], 3), 10) + fx (spreadOf (blt), 3));
        }

        info ("");
        info ("    The bilinear form was measured rather than assumed, because");
        info ("    it is the obvious fix and it is not one: pre-warping pins the");
        info ("    corner exactly but squeezes the whole analogue axis below");
        info ("    Nyquist, so near Nyquist it diverges MORE across rates than");
        info ("    the impulse-invariant form does. The shipped filter keeps the");
        info ("    form DampingTuning.h argued for, and the top octave at");
        info ("    44.1 kHz stays a documented limitation of first order.");

        checkAtMost (worstLowRegime, 0.05,
                     "in the low-cut regime a corner means the same thing at every rate");

        checkAtMost (worstSpread, 0.50,
                     "in the high-cut regime the cross-rate spread stays inside the warping");

        // Near Nyquist: the coefficient must stay in range and the response
        // must stay bounded and monotone.
        {
            bool allInRange = true;
            bool allFiniteResponse = true;

            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                for (double corner : { 15000.0, 19000.0, 19999.0, 20000.0 })
                {
                    StereoCutFilters f;
                    f.prepare (rate);
                    f.setHighCut (corner, false);

                    const double a = f.getHighCoefficient();

                    if (! (a > 0.0 && a < 1.0) || ! std::isfinite (a))
                        allInRange = false;

                    for (double w = 0.0; w <= pi; w += pi / 64.0)
                        if (! std::isfinite (lowpassMagnitude (a, w)) || lowpassMagnitude (a, w) > 1.0 + 1e-12)
                            allFiniteResponse = false;
                }
            }

            check (allInRange, "the one-pole coefficient stays in (0,1) near Nyquist, every rate");
            check (allFiniteResponse, "and |H| stays finite and at most unity");
        }

        // Rapid automation: slam the corner about and look for instability.
        {
            const double rate = 48000.0;

            StereoCutFilters f;
            f.prepare (rate);

            auto noise = whiteNoise (48000, 31337u);
            std::vector<double> l (noise.begin(), noise.end()), r = l;

            unsigned int seed = 7u;

            for (int offset = 0; offset < static_cast<int> (l.size()); offset += 32)
            {
                seed = seed * 1664525u + 1013904223u;

                const double t = static_cast<double> (seed) / 4294967296.0;

                f.setLowCut  (20.0 + t * 980.0, false);
                f.setHighCut (500.0 + (1.0 - t) * 19500.0, false);

                f.process (l.data() + offset, r.data() + offset,
                           std::min (32, static_cast<int> (l.size()) - offset));
            }

            double peak = 0.0;

            for (double x : l)
                peak = std::max (peak, std::abs (x));

            info ("");
            info ("    peak after 1500 random coefficient changes  " + fx (peak, 6));

            check (allFinite (l) && allFinite (r),
                   "no NaN or Inf under rapid coefficient automation");

            checkAtMost (peak, 4.0,
                         "the output stays bounded under rapid coefficient automation");
        }

        // reset() clears the filter memory.
        {
            StereoCutFilters f;
            f.prepare (48000.0);
            f.setLowCut (500.0, false);
            f.setHighCut (2000.0, false);

            auto noise = whiteNoise (4096, 606u);
            std::vector<double> l (noise.begin(), noise.end()), r = l;

            f.process (l.data(), r.data(), static_cast<int> (l.size()));

            f.reset();

            std::vector<double> silenceL (4096, 0.0), silenceR (4096, 0.0);
            f.process (silenceL.data(), silenceR.data(), 4096);

            double worst = 0.0;

            for (double x : silenceL)
                worst = std::max (worst, std::abs (x));

            checkEqual (worst, 0.0, "reset() clears the filter memory completely");
        }
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 smoothers - each advances exactly once per sample");
    // -----------------------------------------------------------------------
    {
        info ("  The quietest possible PHASE 8 bug: lowCut and highCut are now");
        info ("  consumed by the audio path, but through getCurrentValue(),");
        info ("  which reads without advancing. advanceUnconsumed() is what");
        info ("  advances them, once per chunk. Advancing them again in the");
        info ("  filter stage would run every control-rate smoother at twice");
        info ("  the rate - automation would simply feel quicker, nothing would");
        info ("  fail, and no audio test would notice.");
        info ("");
        info ("  So the property is asserted directly: a smoother advanced in");
        info ("  chunks must equal the same smoother advanced sample by sample.");
        info ("");

        const double rate = 48000.0;

        auto runChunked = [rate] (int chunk)
        {
            LogSmoother<double> s;
            s.prepare (rate, 0.03, 10.0);
            s.snapTo (20.0);
            s.setTarget (1000.0);

            int done = 0;

            while (done < 4800)
            {
                const int n = std::min (chunk, 4800 - done);
                s.skip (n);
                done += n;
            }

            return s.getCurrentValue();
        };

        const double bySample = runChunked (1);
        const double byChunk  = runChunked (32);
        const double byBlock  = runChunked (512);

        info ("    sample by sample   " + fx (bySample, 12));
        info ("    32-sample chunks   " + fx (byChunk, 12));
        info ("    512-sample blocks  " + fx (byBlock, 12));

        const double worst = std::max (std::abs (byChunk - bySample),
                                       std::abs (byBlock - bySample));

        info ("    worst difference   " + fx (worst, 18));

        checkAtMost (worst, 1.0e-9,
                     "advancing in chunks equals advancing sample by sample");

        // And the thing that would break if someone double-advanced: after
        // one time constant the smoother must have closed ~63 % of the gap,
        // not ~86 % (which is what two time constants gives).
        {
            LogSmoother<double> s;
            s.prepare (rate, 0.03, 10.0);
            s.snapTo (100.0);
            s.setTarget (1000.0);

            s.skip (static_cast<int> (0.03 * rate));

            const double closed = (std::log (s.getCurrentValue()) - std::log (100.0))
                                / (std::log (1000.0) - std::log (100.0));

            info ("");
            info ("    fraction of the gap closed in one time constant  " + fx (closed, 6));
            info ("    one time constant should close                   0.632");
            info ("    two would close                                  0.865");

            checkClose (closed, 0.632, 0.01,
                        "one time constant closes one time constant's worth of the gap");
        }
    }
}

} // namespace avtest
