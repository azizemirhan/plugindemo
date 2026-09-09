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

/** Where |H(w)| peaks, and what it peaks at. Dense log sweep plus refinement. */
struct Peak
{
    double frequencyHz = 0.0;
    double magnitude   = 0.0;
};

[[nodiscard]] Peak findPeak (const damping::ShelfSpec& spec, double sampleRate,
                             int coarse = 4096)
{
    const double lo = std::log (5.0);
    const double hi = std::log (0.4999 * sampleRate);

    Peak best;

    for (int i = 0; i <= coarse; ++i)
    {
        const double f = std::exp (lo + (hi - lo) * static_cast<double> (i)
                                          / static_cast<double> (coarse));
        const double m = damping::magnitudeAt (spec, f, sampleRate);

        if (m > best.magnitude)
        {
            best.magnitude = m;
            best.frequencyHz = f;
        }
    }

    // Refine around the coarse winner: the excess being hunted here is at the
    // fourth decimal place, so grid resolution is not a detail.
    const double span = (hi - lo) / static_cast<double> (coarse);
    double centre = std::log (best.frequencyHz);

    for (int round = 0; round < 6; ++round)
    {
        const double step = span / std::pow (4.0, round + 1);

        for (int k = -8; k <= 8; ++k)
        {
            const double f = std::exp (centre + k * step);

            if (f < 1.0 || f > 0.4999 * sampleRate)
                continue;

            const double m = damping::magnitudeAt (spec, f, sampleRate);

            if (m > best.magnitude)
            {
                best.magnitude = m;
                best.frequencyHz = f;
            }
        }

        centre = std::log (best.frequencyHz);
    }

    // DC and Nyquist are analytic extremes a log sweep never lands on.
    for (double f : { 0.0, 0.5 * sampleRate })
    {
        const double m = damping::magnitudeAt (spec, f, sampleRate);

        if (m > best.magnitude)
        {
            best.magnitude = m;
            best.frequencyHz = f;
        }
    }

    return best;
}

/**
    RT60 at a frequency where the compensated loop gain is R times the
    undamped baseline, as a fraction of the baseline.

        g_total = g_base * R,   g_base = 10^(-3 d / RT60_base)

        RT60(R) / RT60_base = 1 / (1 - RT60_base * log10(R) / (3 d))

    R > 1 lengthens; the shorter the line, the more a given R lengthens it,
    because a short line applies its per-pass gain more often per second.
*/
[[nodiscard]] double rt60RatioFor (double r, double delaySeconds, double rt60Base) noexcept
{
    const double denominator = 1.0 - rt60Base * std::log10 (r) / (3.0 * delaySeconds);

    if (denominator <= 0.0)
        return 1.0e9;

    return 1.0 / denominator;
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
                                    // gain, untouched by PHASE 7, times the
                                    // NORMALISED filter magnitude.
                                    const double norm = damping::normalisationFor (spec);

                                    const double g = std::min (
                                        fdn::maxFeedbackGain,
                                        fdn::feedbackGainForRt60 (d, rt60));

                                    worstCombined = std::max (worstCombined, g * peak * norm);

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

        checkAtMost (worstCombined, fdn::maxFeedbackGain + 1.0e-12,
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
        checkEqual (damped.core.getDampingNormalisation (0), 1.0,
                    "the normalisation is exactly 1.0 when damping is transparent");

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

            // Under the semantics chosen for this phase, Decay is the LONGEST
            // RT60 and the mid band is not required to equal it. The deviation
            // is reported rather than asserted; what is asserted is that
            // nothing exceeds the target.
            info ("    mid deviation from Decay: "
                  + number ((mid / decay - 1.0) * 100.0, 2) + " %");

            checkAtMost (std::max ({ low, mid, high }), decay * 1.02,
                         "no band exceeds the Decay target under default damping");

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

            info ("    mid deviation from Decay: "
                  + number ((mid / decay - 1.0) * 100.0, 2) + " %");

            checkAtMost (std::max (mid, high), decay * 1.02,
                         "no band exceeds the Decay target under heavy high damping");

            check (high < 0.5 * mid, "heavy high damping shortens the high band substantially");
        }

        {
            Tank lowHeavy (decay);
            lowHeavy.damp (0.90, 0.0, 200.0, 6000.0);

            const double low = bandRt60 (lowHeavy, 80.0, decay);
            const double mid = bandRt60 (lowHeavy, 1000.0, decay);

            info ("  low-heavy (90 % / 0 %):   80 Hz " + number (low, 3)
                  + " s, 1 kHz " + number (mid, 3) + " s");

            info ("    mid deviation from Decay: "
                  + number ((mid / decay - 1.0) * 100.0, 2) + " %");

            checkAtMost (std::max (mid, low), decay * 1.02,
                         "no band exceeds the Decay target under heavy low damping");

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
        info ("    normalisation on line 0: "
              + number (reversed.core.getDampingNormalisation (0), 6));

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

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - maximum finder accuracy");

    {
        /*
            The normalisation is only as trustworthy as the maximum it divides
            by, and it has to err in one direction: an UNDER-estimate puts the
            subtractive invariant back where it was, an over-estimate only
            damps marginally deeper than asked.

            The bracket exploits the two monotonicity results proved in
            DampingTuning.h - |H_lo| never falls with frequency, |H_hi| never
            rises - so on each interval the product of the two favourable
            endpoints bounds the true maximum whatever shape it has. No
            unimodality assumption, no optimiser, no data-dependent iteration
            count.

            Checked here against a brute-force reference an order of magnitude
            denser than the grid, over random and extreme combinations.
        */
        double worstUnderestimate = 0.0;   // reference - upper, must stay <= 0
        double worstOverestimate  = 0.0;   // upper - reference
        double worstBracket       = 0.0;   // upper - lower
        long long cases = 0;

        unsigned int seed = 987654321u;

        const auto nextRandom = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 16777216.0;
        };

        // Extremes first, then a random sweep. The extremes are where a naive
        // search is most likely to miss: one shelf transparent, or both
        // corners crushed together.
        struct Fixed { double gLo, fLo, gHi, fHi; };

        const Fixed fixed[]
        {
            { 1.0,   200.0, 0.1,  6000.0 },
            { 0.1,   200.0, 1.0,  6000.0 },
            { 1.0,    20.0, 0.001, 500.0 },
            { 0.001, 1000.0, 1.0,  20000.0 },
            { 0.5,   999.0, 0.5,  1000.0 },
            { 0.01,  1000.0, 0.01, 500.0 },
            { 0.9,    20.0, 0.9,  20000.0 },
        };

        const auto examine = [&] (const damping::ShelfSpec& spec)
        {
            const auto bound = damping::compositeMaximum (spec);
            const double reference = peakMagnitude (spec, kSampleRate, 200000);

            worstUnderestimate = std::max (worstUnderestimate, reference - bound.upper);
            worstOverestimate  = std::max (worstOverestimate,  bound.upper - reference);
            worstBracket       = std::max (worstBracket,       bound.bracketWidth());

            ++cases;
        };

        for (const auto& f : fixed)
        {
            damping::ShelfSpec spec;
            spec.gainLow   = f.gLo;
            spec.gainHigh  = f.gHi;
            spec.coeffLow  = damping::onePoleCoefficient (f.fLo, kSampleRate);
            spec.coeffHigh = damping::onePoleCoefficient (f.fHi, kSampleRate);

            examine (spec);
        }

        for (int trial = 0; trial < 4000; ++trial)
        {
            damping::ShelfSpec spec;

            spec.gainLow   = std::pow (10.0, -4.0 * nextRandom());
            spec.gainHigh  = std::pow (10.0, -4.0 * nextRandom());
            spec.coeffLow  = damping::onePoleCoefficient (
                                 20.0 * std::pow (50.0, nextRandom()), kSampleRate);
            spec.coeffHigh = damping::onePoleCoefficient (
                                 500.0 * std::pow (40.0, nextRandom()), kSampleRate);

            examine (spec);
        }

        info ("  cases examined                    " + std::to_string (cases));
        info ("  worst UNDER-estimate of the max   " + number (worstUnderestimate, 15));
        info ("  worst over-estimate of the max    " + number (worstOverestimate, 12));
        info ("  worst bracket width (upper-lower) " + number (worstBracket, 12));

        check (worstUnderestimate <= 1.0e-12,
               "the bound never under-states the true maximum");

        checkAtMost (worstBracket, 1.0e-3,
                     "the bracket is tight enough for the over-estimate to be numerical");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - subtractive invariant after normalisation");

    {
        /*
            The invariant the whole phase now rests on:

                Hn_i(w) = H_i(w) / M_i,   M_i = max_w |H_i(w)|
                total_i(w) = g_base_i * |Hn_i(w)|  <=  g_base_i

            so no frequency region can decay longer than the undamped Decay
            target. The first implementation of this phase failed exactly here,
            with 29951 of 29952 normal-ordered combinations showing a region
            that rang longer than baseline, so the grid is re-run intact.
        */
        const double lows[]    { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0 };
        const double highs[]   { 500.0, 1000.0, 2000.0, 4000.0, 6000.0, 12000.0, 20000.0 };
        const double amounts[] { 0.0, 0.2, 0.5, 0.8, 1.0 };
        const double decays[]  { 0.5, 2.5, 5.0, 10.0 };

        double worstNormalised = 0.0;   // max |Hn|
        double worstTotal      = 0.0;   // max g_base * |Hn|
        double worstExcess     = 0.0;   // worst RT60 extension, %
        long long extended = 0, residual = 0, total = 0;

        // The undamped gain a line would have, to compare the damped total to.
        Tank reference (2.5);

        for (double fl : lows)
            for (double fh : highs)
            {
                if (fh <= fl)
                    continue;

                for (double la : amounts)
                    for (double ha : amounts)
                    {
                        if (la <= 0.0 && ha <= 0.0)
                            continue;

                        for (double rt60 : decays)
                        {
                            Tank tank (rt60);
                            tank.damp (la, ha, fl, fh);

                            for (int i = 0; i < 8; ++i)
                            {
                                const auto& spec = tank.core.getDampingSpec (i);
                                const double norm = tank.core.getDampingNormalisation (i);

                                const auto peak = findPeak (spec, kSampleRate, 1024);
                                const double normalisedPeak = peak.magnitude * norm;

                                const double d = static_cast<double> (tank.core.getDelaySamples (i))
                                                 / kSampleRate;
                                const double gBase = fdn::feedbackGainForRt60 (d, rt60);

                                worstNormalised = std::max (worstNormalised, normalisedPeak);
                                worstTotal = std::max (worstTotal,
                                                       tank.core.getGain (i) * normalisedPeak);

                                const double excess =
                                    (rt60RatioFor (normalisedPeak, d, rt60) - 1.0) * 100.0;

                                worstExcess = std::max (worstExcess, excess);

                                ++total;

                                if (excess > 0.1)          ++extended;
                                else if (excess > 1.0e-9)  ++residual;

                                (void) gBase;
                            }
                        }
                    }
            }

        info ("  normal-ordered combinations examined  " + std::to_string (total));
        info ("  max |Hn(w)|                           " + number (worstNormalised, 12));
        info ("  max g_base * |Hn(w)|                  " + number (worstTotal, 12));
        info ("  worst RT60 extension over baseline    " + number (worstExcess, 6) + " %");
        info ("  combinations extending RT60 > 0.1 %   " + std::to_string (extended));
        info ("  combinations with a residual < 0.1 %  " + std::to_string (residual));

        checkEqual (static_cast<long long> (extended), 0LL,
                    "no combination extends any band beyond the undamped Decay target");

        checkAtMost (worstNormalised, 1.0 + 1.0e-12,
                     "the normalised response never exceeds unity");

        checkAtMost (worstTotal, fdn::maxFeedbackGain,
                     "the total per-pass gain stays under the feedback ceiling");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - per-line decay coherence");

    {
        /*
            Normalising each line by its OWN maximum is what keeps the eight
            lines on one decay envelope - but it is also the step most likely
            to break it, because every line has different shelf depths and so a
            different maximum.

            The quantity that has to agree is

                K_i(w) = -log10 |Hn_i(w)| / d_i

            because 1/RT60_i(w) = 1/RT60_base + K_i(w)/3. Equal K means the
            eight lines reach -60 dB together at that frequency; unequal K
            means the tail decays as eight things.
        */
        const double decay = 2.5;

        struct Probe { double hz; const char* label; };

        const Probe probes[]
        {
            {    40.0, "low plateau" },
            {   150.0, "low crossover" },
            {   500.0, "lower mid" },
            {  1000.0, "mid" },
            {  2000.0, "upper mid" },
            {  6000.0, "high crossover" },
            { 14000.0, "high plateau" },
        };

        struct Setting { double la, ha, fl, fh; const char* label; };

        const Setting settings[]
        {
            { 0.20, 0.50,  200.0, 6000.0, "default" },
            { 1.00, 0.00,  200.0, 6000.0, "low-heavy" },
            { 0.00, 1.00,  200.0, 6000.0, "high-heavy" },
            { 1.00, 1.00,  200.0, 6000.0, "both at full" },
        };

        double worstSpread = 0.0;
        double worstRawSpread = 0.0;
        std::string worstWhere;

        for (const auto& s : settings)
        {
            Tank tank (decay);
            tank.damp (s.la, s.ha, s.fl, s.fh);

            info ("  " + std::string (s.label)
                  + "  (low " + number (s.la * 100.0, 0) + " %, high "
                  + number (s.ha * 100.0, 0) + " %)");

            for (const auto& p : probes)
            {
                /*
                    Measured twice. `withNorm` is what the tank actually does.
                    `rawOnly` uses the same shelves with the normalisation
                    forced to 1, which isolates how much of any spread the
                    normalisation is responsible for and how much was already
                    in the shelf shapes.
                */
                double minRt = 1.0e9, maxRt = 0.0;
                double minRaw = 1.0e9, maxRaw = 0.0;

                for (int i = 0; i < 8; ++i)
                {
                    const auto& spec = tank.core.getDampingSpec (i);
                    const double norm = tank.core.getDampingNormalisation (i);
                    const double raw  = damping::magnitudeAt (spec, p.hz, kSampleRate);

                    const double d = static_cast<double> (tank.core.getDelaySamples (i))
                                     / kSampleRate;

                    const auto rt60At = [&] (double magnitude)
                    {
                        const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;
                        return 1.0 / (1.0 / decay + k / 3.0);
                    };

                    const double rt = rt60At (raw * norm);
                    const double rr = rt60At (raw);

                    minRt = std::min (minRt, rt);
                    maxRt = std::max (maxRt, rt);
                    minRaw = std::min (minRaw, rr);
                    maxRaw = std::max (maxRaw, rr);
                }

                const double spread    = (maxRt - minRt) / std::max (maxRt, 1.0e-12) * 100.0;
                const double rawSpread = (maxRaw - minRaw) / std::max (maxRaw, 1.0e-12) * 100.0;

                info ("    " + std::string (p.label) + " (" + number (p.hz, 0) + " Hz): RT60 "
                      + number (minRt, 4) + " - " + number (maxRt, 4)
                      + " s, spread " + number (spread, 2)
                      + " %   (shelves alone: " + number (rawSpread, 2) + " %)");

                if (spread > worstSpread)
                {
                    worstSpread = spread;
                    worstWhere = std::string (s.label) + " at " + number (p.hz, 0) + " Hz";
                }

                worstRawSpread = std::max (worstRawSpread, rawSpread);
            }
        }

        info ("");
        info ("  worst line-to-line spread, as shipped:  " + number (worstSpread, 2)
              + " %  (" + worstWhere + ")");
        info ("  worst spread from the shelves alone:    " + number (worstRawSpread, 2) + " %");
        info ("  An undamped tank has a spread of exactly 0 %: every line is given");
        info ("  the gain that reaches -60 dB at the same time. Any spread here is");
        info ("  the shelf SHAPES differing between lines - only their asymptotes");
        info ("  scale exactly with delay length - and the plateaus show that the");
        info ("  asymptotic part is right.");

        // The question the semantics actually care about is whether
        // normalisation broke coherence, so that is what is asserted: the
        // shipped spread must not be materially worse than the shelves alone
        // already are.
        checkAtMost (worstSpread, std::max (worstRawSpread * 1.25, 1.0),
                     "normalising per line does not add materially to the spread"
                     " the shelf shapes already carry");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - default mid-band consequence");

    {
        /*
            Under the new semantics Decay is the LONGEST RT60, so the mid band
            is no longer required to be exactly Decay. It is still required not
            to wander away from the character Aurora already has, and the
            shipping default is the configuration that decides that.
        */
        const double decay = 2.5;

        Tank tank (decay);
        tank.damp (0.20, 0.50, 200.0, 6000.0);

        info ("  default: Low 20 %, High 50 %, 200 Hz / 6 kHz, Decay "
              + number (decay, 2) + " s");

        struct Point { double hz; const char* label; };

        const Point points[]
        {
            {    80.0, "low" },
            {   500.0, "500 Hz" },
            {  1000.0, "1 kHz" },
            {  2000.0, "2 kHz" },
            { 12000.0, "high" },
        };

        double midDeviation = 0.0;
        double longest = 0.0;

        for (const auto& p : points)
        {
            const double rt = bandRt60 (tank, p.hz, decay);
            const double deviation = (rt / decay - 1.0) * 100.0;

            info ("    " + std::string (p.label) + " (" + number (p.hz, 0) + " Hz): "
                  + number (rt, 4) + " s   " + number (deviation, 2) + " % of Decay");

            longest = std::max (longest, rt);

            if (p.hz >= 500.0 && p.hz <= 2000.0)
                midDeviation = std::max (midDeviation, std::abs (deviation));
        }

        info ("");
        info ("  worst broad-mid deviation from Decay: " + number (midDeviation, 3) + " %");
        info ("  longest measured band RT60:           " + number (longest, 4) + " s");

        // Reported, not asserted. Whether the old +/-2 % mid criterion survives
        // the change of semantics is a decision about the product, not
        // something a test should quietly settle by choosing its own threshold.
        info ("  The +/-2 % mid criterion belonged to the mid-anchored scheme.");
        info ("  Under \"Decay = longest RT60\" the number above is a measurement,");
        info ("  and the assertion below is the invariant that replaced it.");

        checkAtMost (longest, decay * 1.02,
                     "no measured band exceeds the Decay target");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 damping - extreme semantic cases");

    {
        /*
            The case that broke the first implementation: with Low Damp at 0 %
            the low shelf is transparent, so the composite maximum is at DC
            while the old anchor sat inside the high shelf's skirt. The
            compensation asked for a loop gain above unity, the clamp caught
            it, and the low band was left with a 363 second tail against a
            10 second target.
        */
        struct Case { double la, ha, fl, fh, decay; const char* label; };

        const Case cases[]
        {
            { 0.0, 1.0, 100.0, 500.0, 10.0, "the old 363 s failure: low 0 %, high 100 %" },
            { 1.0, 0.0, 100.0, 500.0, 10.0, "its mirror: low 100 %, high 0 %" },
        };

        for (const auto& c : cases)
        {
            Tank tank (c.decay, 1.0);
            tank.damp (c.la, c.ha, c.fl, c.fh);

            info ("  " + std::string (c.label));
            info ("    corners " + number (c.fl, 0) + " Hz / " + number (c.fh, 0)
                  + " Hz, Decay " + number (c.decay, 1) + " s");

            const double low  = bandRt60 (tank, 60.0, c.decay);
            const double mid  = bandRt60 (tank, 1000.0, c.decay);
            const double high = bandRt60 (tank, 8000.0, c.decay);

            info ("    60 Hz   " + number (low, 3) + " s");
            info ("     1 kHz  " + number (mid, 3) + " s");
            info ("     8 kHz  " + number (high, 3) + " s");

            const double longest = std::max ({ low, mid, high });

            checkAtMost (longest, c.decay * 1.02,
                         "no band exceeds the Decay target: " + std::string (c.label));

            // The clamp must not be doing the work. If it engaged, the gain
            // would sit exactly on the ceiling.
            bool clamped = false;

            for (int i = 0; i < 8; ++i)
                if (tank.core.getGain (i) >= fdn::maxFeedbackGain - 1.0e-12)
                    clamped = true;

            check (! clamped,
                   "the feedback clamp is not engaged: " + std::string (c.label));

            if (c.la <= 0.0)
            {
                info ("    Low Damp is 0 %, so the low band should still be about "
                      + number (c.decay, 1) + " s");

                info ("    measured " + number (low, 3) + " s, "
                      + number ((low / c.decay - 1.0) * 100.0, 2) + " % of target");
                info ("    A first-order shelf has a long skirt: a 500 Hz corner still");
                info ("    attenuates measurably at 60 Hz, and the shortfall is that,");
                info ("    not a normalisation error.");

                check (high < low, "the damped high band is shorter than the low band");
            }
            else
            {
                info ("    High Damp is 0 %, so the high band should still be about "
                      + number (c.decay, 1) + " s");

                check (low < c.decay, "the damped low band is shorter than the target");
            }
        }
    }
}

} // namespace avtest
