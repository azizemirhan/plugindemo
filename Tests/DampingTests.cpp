#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/LineDamping.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

/*
    PHASE 7 — frequency-dependent decay, unit and synthetic suite.

    Everything here is offline and deterministic. The RT60 measurements use a
    band-limited impulse response and Schroeder backward integration, which is
    the same instrument PHASE 5C used for the broadband case; the only addition
    is the band filter in front of it.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string number (double v, int decimals = 4)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

/** Largest |H(w)| over a dense log sweep from 10 Hz to just under Nyquist. */
[[nodiscard]] double peakMagnitude (const damping::ShelfSpec& spec, double sampleRate,
                                    int points = 2048)
{
    const double lo = std::log (10.0);
    const double hi = std::log (0.4999 * sampleRate);

    double worst = 0.0;

    for (int i = 0; i <= points; ++i)
    {
        const double f = std::exp (lo + (hi - lo) * static_cast<double> (i)
                                          / static_cast<double> (points));

        worst = std::max (worst, damping::magnitudeAt (spec, f, sampleRate));
    }

    // DC and Nyquist are the two analytic extremes and must be included
    // explicitly: a log sweep never lands on either.
    worst = std::max (worst, damping::magnitudeAt (spec, 0.0, sampleRate));
    worst = std::max (worst, damping::magnitudeAt (spec, 0.5 * sampleRate, sampleRate));

    return worst;
}

/** A configured 8-line tank, prepared and silent. */
struct Tank
{
    FdnCore<8> core;

    explicit Tank (double rt60, double sizeScale = 1.0, double sampleRate = kSampleRate)
    {
        core.prepare (sampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, sizeScale);
        core.setRt60Seconds (rt60, true);
        core.reset();
    }

    void damp (double lowControl, double highControl, double lowHz, double highHz)
    {
        core.setDamping (mapping::lowDampRatioFor (lowControl),
                         mapping::highDampRatioFor (highControl),
                         lowHz, highHz, true);
    }

    /** Impulse response of line 0's readout, summed across lines. */
    [[nodiscard]] std::vector<double> impulseResponse (int numSamples)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[8] {};
        double state[8] {};

        for (int n = 0; n < numSamples; ++n)
        {
            for (int i = 0; i < 8; ++i)
                injection[i] = (n == 0) ? fdn::signVectorNorm : 0.0;

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < 8; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }
};

/**
    Fourth-order Butterworth-ish bandpass, as a cascade of two identical
    two-pole sections. Applied forwards and backwards so the band has zero
    phase distortion — this runs offline on a stored buffer, so filtfilt is
    available and there is no reason to accept the phase error.
*/
void bandpass (std::vector<double>& x, double centreHz, double octaves, double sampleRate)
{
    const double lowHz  = centreHz * std::pow (2.0, -0.5 * octaves);
    const double highHz = std::min (centreHz * std::pow (2.0, 0.5 * octaves),
                                    0.45 * sampleRate);

    const auto onePoleHigh = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;

        for (auto& s : v)
        {
            z += (1.0 - a) * (s - z);
            s -= z;                      // highpass = input - lowpass
        }
    };

    const auto onePoleLow = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;

        for (auto& s : v)
        {
            z += (1.0 - a) * (s - z);
            s = z;
        }
    };

    for (int pass = 0; pass < 2; ++pass)
    {
        onePoleHigh (x, lowHz);
        onePoleHigh (x, lowHz);
        onePoleLow  (x, highHz);
        onePoleLow  (x, highHz);

        std::reverse (x.begin(), x.end());
    }
}

/**
    RT60 from Schroeder backward integration.

    Fitted over -5 dB to -35 dB and extrapolated, which is the usual T30
    construction: the first few decibels carry the direct arrival and the last
    twenty sit in the noise floor of a finite buffer.
*/
[[nodiscard]] double rt60From (const std::vector<double>& ir, double sampleRate)
{
    const size_t n = ir.size();

    std::vector<double> energy (n + 1, 0.0);

    for (size_t i = n; i-- > 0;)
        energy[i] = energy[i + 1] + ir[i] * ir[i];

    if (energy[0] <= 0.0)
        return 0.0;

    const double reference = energy[0];

    const auto timeAt = [&] (double dB) -> double
    {
        const double target = reference * std::pow (10.0, dB / 10.0);

        for (size_t i = 0; i < n; ++i)
            if (energy[i] <= target)
                return static_cast<double> (i) / sampleRate;

        return -1.0;
    };

    const double t5  = timeAt (-5.0);
    const double t35 = timeAt (-35.0);

    if (t5 < 0.0 || t35 < 0.0 || t35 <= t5)
        return 0.0;

    return (t35 - t5) * (60.0 / 30.0);
}

/** Band RT60 of a tank, measured on its own impulse response. */
[[nodiscard]] double bandRt60 (Tank& tank, double centreHz, double rt60Hint,
                               double sampleRate = kSampleRate)
{
    const int length = static_cast<int> (std::ceil (sampleRate * std::max (1.0, rt60Hint * 2.5)));

    auto ir = tank.impulseResponse (length);
    bandpass (ir, centreHz, 1.0, sampleRate);

    return rt60From (ir, sampleRate);
}

} // namespace

// ===========================================================================

void runDampingTests()
{
    beginSuite ("PHASE 7 damping - control mapping");

    {
        checkEqual (damping::decayRatioFor (0.0, damping::highRatioAtFull), 1.0,
                    "Damp 0 % is exactly ratio 1.0");

        checkClose (damping::decayRatioFor (1.0, damping::highRatioAtFull),
                    damping::highRatioAtFull, 1.0e-12,
                    "High Damp 100 % reaches its full ratio");

        checkClose (damping::decayRatioFor (0.5, damping::highRatioAtFull),
                    std::sqrt (damping::highRatioAtFull), 1.0e-12,
                    "the mapping is exponential, so 50 % is the geometric middle");

        // Monotonicity: more damping is never less damping.
        bool monotonic = true;
        double previous = 2.0;

        for (int i = 0; i <= 100; ++i)
        {
            const double r = damping::decayRatioFor (i * 0.01, damping::highRatioAtFull);

            if (r > previous + 1.0e-15)
                monotonic = false;

            previous = r;
        }

        check (monotonic, "ratio decreases monotonically across the whole control");

        info ("  default Low Damp 20 %  -> ratio "
              + number (mapping::lowDampRatioFor (0.20)));
        info ("  default High Damp 50 % -> ratio "
              + number (mapping::highDampRatioFor (0.50)));
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - shelf gains and per-line derivation");

    {
        // The shelf gain must scale with delay so that every line reaches the
        // same band RT60. Two lines, one 3.16x longer than the other.
        const double rt60 = 2.5;
        const double ratio = 0.354;

        const double gShort = damping::shelfGainFor (0.02627, rt60, ratio);
        const double gLong  = damping::shelfGainFor (0.08292, rt60, ratio);

        info ("  26.27 ms line: shelf gain " + number (gShort)
              + " (" + number (20.0 * std::log10 (gShort), 2) + " dB per pass)");
        info ("  82.92 ms line: shelf gain " + number (gLong)
              + " (" + number (20.0 * std::log10 (gLong), 2) + " dB per pass)");

        // log(G_i) / d_i must be equal across lines: that is the condition for
        // the tail to decay as one thing rather than as eight.
        const double rateShort = std::log (gShort) / 0.02627;
        const double rateLong  = std::log (gLong)  / 0.08292;

        checkClose (rateLong / rateShort, 1.0, 1.0e-9,
                    "log(G)/d is equal across lines - all lines share one band decay rate");

        checkEqual (damping::shelfGainFor (0.02627, rt60, 1.0), 1.0,
                    "ratio 1.0 gives exactly unity gain");

        check (gLong < gShort, "the longer line needs the deeper shelf");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - stability grid");

    {
        /*
            The bound |H| <= 1 is proved in DampingTuning.h by the triangle
            inequality. A proof and a test are not substitutes for each other,
            so it is also swept exhaustively here: every combination of both
            amounts, both corners and every supported sample rate.
        */
        const double rates[]   { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const double lowHz[]   { 20.0, 100.0, 200.0, 500.0, 1000.0 };
        const double highHz[]  { 500.0, 1000.0, 4000.0, 6000.0, 20000.0 };
        const double amounts[] { 0.0, 0.1, 0.2, 0.5, 0.8, 1.0 };
        const double decays[]  { 0.1, 0.5, 2.5, 10.0, 100.0 };
        const double delays[]  { 0.02627 * 0.25, 0.02627, 0.08292, 0.08292 * 2.0 };

        double worstMagnitude = 0.0;
        double worstCombined  = 0.0;
        long long combinations = 0;

        for (double fs : rates)
            for (double fl : lowHz)
                for (double fh : highHz)
                    for (double lowAmount : amounts)
                        for (double highAmount : amounts)
                            for (double rt60 : decays)
                                for (double d : delays)
                                {
                                    damping::ShelfSpec spec;

                                    spec.coeffLow  = damping::onePoleCoefficient (fl, fs);
                                    spec.coeffHigh = damping::onePoleCoefficient (fh, fs);
                                    spec.gainLow   = damping::shelfGainFor (
                                                        d, rt60, mapping::lowDampRatioFor (lowAmount));
                                    spec.gainHigh  = damping::shelfGainFor (
                                                        d, rt60, mapping::highDampRatioFor (highAmount));

                                    const double peak = peakMagnitude (spec, fs, 256);

                                    worstMagnitude = std::max (worstMagnitude, peak);

                                    // What stability actually needs: the decay
                                    // gain including the mid-band compensation,
                                    // times the worst-case filter magnitude.
                                    const double comp =
                                        damping::plateauCompensation (spec, fl, fh, fs);

                                    const double g = std::min (
                                        fdn::maxFeedbackGain,
                                        fdn::feedbackGainForRt60 (d, rt60) * comp);

                                    worstCombined = std::max (worstCombined, g * peak);

                                    ++combinations;
                                }

        info ("  swept " + std::to_string (combinations) + " coefficient combinations");
        info ("  worst max|H(w)|            " + number (worstMagnitude, 12));
        info ("  excess over 1, relative    " + number (worstMagnitude - 1.0, 18));
        info ("  worst g_i * max|H(w)|      " + number (worstCombined, 12));

        /*
            The analytic bound is exactly 1 and the excess measured here is
            double rounding in the closed form, not an overshoot in the filter:
            at DC the lowpass evaluates (1-a)/|D|^2 * dr, which is 1 only up to
            the rounding of that division. The tolerance is a few epsilon and
            the measured excess is printed above rather than hidden behind it.

            What actually governs stability is the line below, which needs no
            tolerance at all.
        */
        checkAtMost (worstMagnitude, 1.0 + 1.0e-12,
                     "max|H| never exceeds 1 beyond double rounding");

        checkAtMost (worstCombined, fdn::maxFeedbackGain,
                     "combined per-pass gain stays under the feedback ceiling");

        check (worstCombined < 1.0, "combined per-pass gain is strictly below unity");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - transparency at 0 %");

    {
        /*
            Damp 0 % must be a bit-exact identity, not a filter that happens to
            be flat. This is checked on the whole engine, not on the filter:
            the compensation, the gain clamp and the bypass all have to come
            out exactly where PHASE 6 left them.
        */
        Tank reference (2.5);
        Tank damped (2.5);

        damped.damp (0.0, 0.0, 200.0, 6000.0);

        const auto a = reference.impulseResponse (48000);
        const auto b = damped.impulseResponse (48000);

        long long differing = 0;

        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                ++differing;

        checkEqual (differing, 0LL,
                    "Damp 0 % renders bit-identically to a tank with no damping at all");

        check (! damped.core.isDampingActive(), "damping reports itself inactive at 0 %");
        checkEqual (damped.core.getPlateauCompensation (0), 1.0,
                    "compensation is exactly 1.0 when damping is transparent");

        for (int i = 0; i < 8; ++i)
            if (reference.core.getGain (i) != damped.core.getGain (i))
                check (false, "decay gains are unchanged at 0 %");

        checkEqual (reference.core.getGain (0), damped.core.getGain (0),
                    "line 0 decay gain is bit-identical at 0 %");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - block-size identity (core)");

    {
        /*
            Same sample-accurate target schedule, four different chunkings,
            bit-identical output. This is the core-level requirement: the
            control-rate counter is free-running on a sample index, so how the
            caller chops the stream must not reach the coefficients.

            The plugin-level requirement is deliberately weaker and is not
            tested here - host automation genuinely arrives on block
            boundaries, so a different block size is a different schedule.
        */
        const int total = 24000;
        const int chunks[] { 64, 128, 512, 2048 };

        std::vector<std::vector<double>> renders;

        for (int chunk : chunks)
        {
            Tank tank (2.5);
            tank.damp (0.20, 0.50, 200.0, 6000.0);

            // A corner move part-way through, so the control-rate path is
            // genuinely exercised rather than sitting settled.
            tank.core.setDamping (mapping::lowDampRatioFor (0.20),
                                  mapping::highDampRatioFor (0.50),
                                  400.0, 3000.0, false);

            tank.core.reset();

            std::vector<double> out;
            out.reserve (static_cast<size_t> (total));

            double injection[8] {};
            double state[8] {};
            int n = 0;

            while (n < total)
            {
                const int count = std::min (chunk, total - n);

                for (int k = 0; k < count; ++k, ++n)
                {
                    for (int i = 0; i < 8; ++i)
                        injection[i] = (n == 0) ? fdn::signVectorNorm : 0.0;

                    tank.core.processSample (injection, state);

                    double sum = 0.0;
                    for (int i = 0; i < 8; ++i)
                        sum += state[i] * fdn::readoutLeft[i];

                    out.push_back (sum);
                }
            }

            renders.push_back (std::move (out));
        }

        long long differing = 0;

        for (size_t r = 1; r < renders.size(); ++r)
            for (size_t i = 0; i < renders[0].size(); ++i)
                if (renders[r][i] != renders[0][i])
                    ++differing;

        checkEqual (differing, 0LL,
                    "64 / 128 / 512 / 2048 chunking renders bit-identically");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - RT60 by band");

    {
        const double decay = 2.5;

        // Undamped reference first: the measurement instrument has to agree
        // with the tank before any damped number means anything.
        {
            Tank tank (decay);

            const double mid = bandRt60 (tank, 1000.0, decay);

            info ("  undamped, 1 kHz band: " + number (mid, 3) + " s");

            checkClose (mid / decay, 1.0, 0.05,
                        "the band measurement agrees with the Decay target on an undamped tank");
        }

        // Default damping.
        {
            Tank tank (decay);
            tank.damp (0.20, 0.50, 200.0, 6000.0);

            const double low  = bandRt60 (tank, 80.0, decay);
            const double mid  = bandRt60 (tank, 1000.0, decay);
            const double high = bandRt60 (tank, 12000.0, decay);

            info ("  default damping (20 % / 50 %, 200 Hz / 6 kHz), Decay " + number (decay, 2) + " s");
            info ("     80 Hz  " + number (low, 3) + " s   (target ratio "
                  + number (mapping::lowDampRatioFor (0.20), 3) + ")");
            info ("    1 kHz   " + number (mid, 3) + " s   (anchor)");
            info ("   12 kHz   " + number (high, 3) + " s   (target ratio "
                  + number (mapping::highDampRatioFor (0.50), 3) + ")");

            checkClose (mid / decay, 1.0, 0.02,
                        "mid band stays within 2 % of the Decay target under default damping");

            check (high < mid, "the high band decays faster than the mid band");
            check (low  < mid, "the low band decays faster than the mid band");
        }

        // High-heavy and low-heavy, to confirm the controls act on their own
        // band and that the anchor holds.
        {
            Tank highHeavy (decay);
            highHeavy.damp (0.0, 0.90, 200.0, 6000.0);

            const double mid  = bandRt60 (highHeavy, 1000.0, decay);
            const double high = bandRt60 (highHeavy, 12000.0, decay);

            info ("  high-heavy (0 % / 90 %):  1 kHz " + number (mid, 3)
                  + " s, 12 kHz " + number (high, 3) + " s");

            checkClose (mid / decay, 1.0, 0.02,
                        "mid band holds under heavy high damping");

            check (high < 0.5 * mid, "heavy high damping shortens the high band substantially");
        }

        {
            Tank lowHeavy (decay);
            lowHeavy.damp (0.90, 0.0, 200.0, 6000.0);

            const double low = bandRt60 (lowHeavy, 80.0, decay);
            const double mid = bandRt60 (lowHeavy, 1000.0, decay);

            info ("  low-heavy (90 % / 0 %):   80 Hz " + number (low, 3)
                  + " s, 1 kHz " + number (mid, 3) + " s");

            checkClose (mid / decay, 1.0, 0.02,
                        "mid band holds under heavy low damping");

            check (low < mid, "heavy low damping shortens the low band");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - crossover overlap region");

    {
        /*
            lowDampFreq reaches 1000 Hz and highDampFreq starts at 500 Hz, so
            the two shelves can overlap or reverse. No parameter is clamped and
            no range is changed: the documented semantics are two subtractive
            shelves acting at once, with no physical mid plateau.

            Stability, boundedness and monotonicity are mandatory here. The
            +/-2 % mid-band criterion is NOT applied, because the plateau it
            measures does not exist. What the region actually does is measured
            and reported instead of being asserted away.
        */
        const double decay = 2.5;

        Tank reversed (decay);
        reversed.damp (0.50, 0.50, 1000.0, 500.0);

        for (int i = 0; i < 8; ++i)
        {
            const auto& spec = reversed.core.getDampingSpec (i);

            checkAtMost (peakMagnitude (spec, kSampleRate), 1.0,
                         "reversed corners: |H| stays bounded on line " + std::to_string (i));
        }

        checkAtMost (reversed.core.getMaxGain(), fdn::maxFeedbackGain,
                     "reversed corners: decay gain stays under the ceiling");

        const auto ir = reversed.impulseResponse (48000);

        bool finite = true;
        for (double s : ir)
            if (! std::isfinite (s))
                finite = false;

        check (finite, "reversed corners: no NaN or Inf in the impulse response");

        const double midReversed = bandRt60 (reversed, 1000.0, decay);

        info ("  reversed corners (low 1 kHz, high 500 Hz, both 50 %):");
        info ("    1 kHz band " + number (midReversed, 3)
              + " s against a Decay of " + number (decay, 2)
              + " s  (no plateau exists; +/-2 % not applied)");
        info ("    compensation on line 0: "
              + number (reversed.core.getPlateauCompensation (0), 4)
              + ", ceiling " + number (damping::maxPlateauCompensation, 1));

        // Monotonicity across the overlap: more damping must never produce a
        // longer band.
        double previousHigh = 1.0e9;
        bool monotonic = true;

        for (int step = 0; step <= 4; ++step)
        {
            Tank t (decay);
            t.damp (0.0, step * 0.25, 900.0, 600.0);

            const double h = bandRt60 (t, 4000.0, decay);

            if (h > previousHigh * 1.02)
                monotonic = false;

            previousHigh = h;
        }

        check (monotonic,
               "overlap region: increasing High Damp never lengthens the high band");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - sample rates and edge cases");

    {
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Tank tank (2.5, 1.0, fs);
            tank.damp (0.20, 0.50, 200.0, 6000.0);

            const auto ir = tank.impulseResponse (static_cast<int> (fs));

            bool finite = true;
            double peak = 0.0;

            for (double s : ir)
            {
                if (! std::isfinite (s))
                    finite = false;

                peak = std::max (peak, std::abs (s));
            }

            check (finite, "no NaN or Inf at " + number (fs, 0) + " Hz");
            check (peak > 0.0 && peak < 100.0,
                   "output stays bounded at " + number (fs, 0) + " Hz");

            checkAtMost (tank.core.getMaxGain(), fdn::maxFeedbackGain,
                         "gain ceiling respected at " + number (fs, 0) + " Hz");
        }

        // The two extremes of Decay, where headroom under the gain ceiling is
        // thinnest and where the compensation has least room to work.
        for (double rt60 : { fdn::minRt60Seconds, 100.0 })
        {
            Tank tank (rt60);
            tank.damp (1.0, 1.0, 1000.0, 500.0);

            checkAtMost (tank.core.getMaxGain(), fdn::maxFeedbackGain,
                         "gain ceiling respected at Decay " + number (rt60, 2) + " s"
                         " with both controls at 100 % and reversed corners");
        }

        // Corner frequencies at the ends of their parameter ranges.
        Tank extremes (2.5);
        extremes.damp (1.0, 1.0, 20.0, 20000.0);

        for (int i = 0; i < 8; ++i)
            checkAtMost (peakMagnitude (extremes.core.getDampingSpec (i), kSampleRate), 1.0,
                         "corners at their range ends stay bounded on line "
                         + std::to_string (i));
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - automation continuity");

    {
        /*
            A sweep of both amounts and both corners, rendered against noise,
            looking for a discontinuity rather than for a spectrum. What would
            show up as a click is a sample-to-sample step far outside the
            envelope of its neighbours.

            The bound is measured, not chosen. Broadband noise through a reverb
            already has large sample-to-sample motion — for a Gaussian signal
            the largest of 96000 steps sits near 6x RMS with nothing wrong at
            all — so a fixed threshold would be indistinguishable from a lucky
            guess. The same noise is therefore rendered through a tank whose
            damping never moves, and the swept tank is required not to exceed
            that control by a meaningful margin.
        */
        const int total = 96000;

        const auto render = [total] (bool sweep) -> std::pair<double, double>
        {
            Tank tank (2.5);
            tank.damp (sweep ? 0.0 : 0.20, sweep ? 0.0 : 0.50, 200.0, 6000.0);

            double injection[8] {};
            double state[8] {};

            double previous = 0.0;
            double worstStep = 0.0;
            double rms = 0.0;

            unsigned int seed = 22222u;

            for (int n = 0; n < total; ++n)
            {
                if (sweep)
                {
                    const double t = static_cast<double> (n) / static_cast<double> (total);

                    tank.core.setDamping (mapping::lowDampRatioFor (t),
                                          mapping::highDampRatioFor (t),
                                          20.0 + t * 980.0,
                                          20000.0 - t * 19500.0,
                                          false);
                }

                seed = seed * 1664525u + 1013904223u;
                const double noise = (static_cast<double> (seed >> 8) / 8388608.0 - 1.0) * 0.1;

                for (int i = 0; i < 8; ++i)
                    injection[i] = noise * fdn::signVectorNorm;

                tank.core.processSample (injection, state);

                double sum = 0.0;
                for (int i = 0; i < 8; ++i)
                    sum += state[i] * fdn::readoutLeft[i];

                if (n > 0)
                    worstStep = std::max (worstStep, std::abs (sum - previous));

                rms += sum * sum;
                previous = sum;
            }

            return { std::sqrt (rms / static_cast<double> (total)), worstStep };
        };

        const auto control = render (false);
        const auto swept   = render (true);

        const double controlRatio = control.second / std::max (control.first, 1.0e-12);
        const double sweptRatio   = swept.second   / std::max (swept.first,   1.0e-12);

        info ("  static damping control:  RMS " + number (control.first, 6)
              + ", worst step " + number (control.second, 6)
              + ", ratio " + number (controlRatio, 3));
        info ("  full sweep of both amounts and both corners over 2 s:");
        info ("                           RMS " + number (swept.first, 6)
              + ", worst step " + number (swept.second, 6)
              + ", ratio " + number (sweptRatio, 3));

        check (std::isfinite (swept.second) && std::isfinite (swept.first),
               "the sweep stays finite throughout");

        checkAtMost (sweptRatio, controlRatio * 1.5,
                     "sweeping the controls adds no step beyond what static damping already shows");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - mid-band compensation");

    {
        /*
            The compensation is what makes the mid band exact rather than
            approximately right. Measuring what it is worth is the point: if it
            were negligible it should not exist, and if it is large the clamp
            interaction matters.
        */
        Tank tank (2.5);
        tank.damp (0.20, 0.50, 200.0, 6000.0);

        for (int i : { 0, 7 })
        {
            const double comp = tank.core.getPlateauCompensation (i);

            info ("  line " + std::to_string (i) + " compensation "
                  + number (comp, 6) + "  ("
                  + number (20.0 * std::log10 (comp), 4) + " dB per pass)");

            check (comp >= 1.0 && comp <= damping::maxPlateauCompensation,
                   "compensation on line " + std::to_string (i)
                   + " is a bounded gain increase");
        }

        checkEqual (mapping::lowDampRatioFor (0.0), 1.0,
                    "0 % low damp maps to ratio 1 exactly");
        checkEqual (mapping::highDampRatioFor (0.0), 1.0,
                    "0 % high damp maps to ratio 1 exactly");
    }
}

} // namespace avtest
