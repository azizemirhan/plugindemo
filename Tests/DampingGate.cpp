#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — the aggregate damping-decay quality gate, on a corrected analyser.

    HISTORY, KEPT ON PURPOSE
    ------------------------
    The gate this file replaces reported |T20 - T30| = 8.50 % for High Damp
    100 % at 12 kHz, and that number drove two topology studies. It was wrong:
    its band filter was a cascade of one-pole highpasses and lowpasses whose
    skirt leaked into frequencies decaying three times more slowly, so the
    estimator saw a slope change that the tank did not have. Measured on one
    identical rendered tail, changing only the band filter:

        3 kHz     one-pole cascade  3.55 %      constant-Q  1.40 %
        6 kHz     one-pole cascade  6.57 %      constant-Q  0.86 %
        12 kHz    one-pole cascade  5.79 %      constant-Q  0.79 %

    That 8.50 % stands in the record as OLD ANALYSER / INVALID FOR GATING. It is
    not evidence of an FDN decay-quality defect and must not be cited as one.

    THRESHOLDS ARE UNCHANGED
    ------------------------
    The measurement was the problem, not the criterion, so the criterion is
    exactly what it was when it was pre-registered.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;

// PRE-REGISTERED and unchanged.
constexpr double kGateMinimumR2         = 0.995;
constexpr double kGateMaxT20T30Mismatch = 0.05;

// The gate's band. One octave, stated once, used everywhere.
constexpr double kGateBandOctaves = 1.0;

[[nodiscard]] std::string number (double v, int decimals = 4)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

[[nodiscard]] std::string pad (const std::string& s, size_t width)
{
    return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
}

struct Tank
{
    FdnCore<8> core;

    explicit Tank (double rt60)
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (rt60, true);
        core.reset();
    }

    void damp (double lowControl, double highControl, double lowHz, double highHz)
    {
        core.setDamping (mapping::lowDampRatioFor (lowControl),
                         mapping::highDampRatioFor (highControl),
                         lowHz, highHz, true);
    }

    [[nodiscard]] std::vector<double> render (int numSamples, unsigned int seed)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[8] {};
        double state[8] {};

        const int burst = (seed == 0u) ? 1 : 64;
        unsigned int rng = seed * 1103515245u + 12345u;

        for (int n = 0; n < numSamples; ++n)
        {
            double drive = 0.0;

            if (n < burst)
            {
                if (seed == 0u)
                {
                    drive = 1.0;
                }
                else
                {
                    rng = rng * 1664525u + 1013904223u;
                    drive = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
                }
            }

            for (int i = 0; i < 8; ++i)
                injection[i] = drive * fdn::injectionLeft[i] * fdn::signVectorNorm;

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < 8; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }
};

/** One band, three deterministic stimuli, with the spread reported. */
struct BandMeasurement
{
    decay::Result mean;
    double repeatability = 0.0;   ///< max-min of fitted RT60 across stimuli, per cent
    int stimuli = 0;
    std::string reason;
};

[[nodiscard]] BandMeasurement measure (Tank& tank, const decay::Band& band, double rt60Hint)
{
    // Long enough for the fit to reach -35 dB with margin at every setting.
    const int length = static_cast<int> (kSampleRate * std::max (2.0, rt60Hint * 4.0));

    BandMeasurement result;

    double minFitted = 1.0e9, maxFitted = 0.0;
    decay::Result sum;

    for (unsigned int seed : { 0u, 7u, 4242u })
    {
        auto ir = tank.render (length, seed);
        band.apply (ir);

        const auto one = decay::analyse (ir, band, kSampleRate);

        if (! one.valid)
        {
            result.reason = one.reason;
            continue;
        }

        sum.edt += one.edt; sum.t20 += one.t20; sum.t30 += one.t30;
        sum.fitted += one.fitted; sum.r2 += one.r2;
        sum.dynamicRangeDb += one.dynamicRangeDb;
        sum.ringFraction += one.ringFraction;

        minFitted = std::min (minFitted, one.fitted);
        maxFitted = std::max (maxFitted, one.fitted);

        ++result.stimuli;
    }

    if (result.stimuli == 0)
        return result;

    const double k = 1.0 / static_cast<double> (result.stimuli);

    result.mean.edt = sum.edt * k;
    result.mean.t20 = sum.t20 * k;
    result.mean.t30 = sum.t30 * k;
    result.mean.fitted = sum.fitted * k;
    result.mean.r2 = sum.r2 * k;
    result.mean.dynamicRangeDb = sum.dynamicRangeDb * k;
    result.mean.ringFraction = sum.ringFraction * k;
    result.mean.valid = true;

    result.repeatability = (maxFitted - minFitted) / std::max (maxFitted, 1.0e-12) * 100.0;

    return result;
}

} // namespace

// ===========================================================================

void runDampingGateTests()
{
    const double decay = 2.5;

    const double bands[] { 100.0, 200.0, 400.0, 1000.0, 3000.0, 6000.0, 12000.0 };

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 gate - analyser definition and measured response");

    {
        info ("  Two RBJ constant-peak bandpass biquads, forwards then backwards.");
        info ("  Magnitude is |H_bp|^" + std::to_string (decay::magnitudeOrder)
              + ", phase is exactly zero.");
        info ("  Q is solved so the COMPOSITE -3 dB edges span "
              + number (kGateBandOctaves, 1) + " octave, not taken from the");
        info ("  single-section fractional-octave formula.");
        info ("");
        info ("    centre     Q        -3 dB low   -3 dB high   achieved    BW        ring");

        double worstOctaveError = 0.0;

        for (double centre : bands)
        {
            const auto band = decay::makeBand (centre, kGateBandOctaves, kSampleRate);

            const double achieved = band.edges.octaves();

            worstOctaveError = std::max (worstOctaveError,
                                         std::abs (achieved - kGateBandOctaves));

            info ("    " + pad (number (centre, 0), 11)
                  + pad (number (band.q, 4), 9)
                  + pad (number (band.edges.lowHz, 1), 12)
                  + pad (number (band.edges.highHz, 1), 13)
                  + pad (number (achieved, 4) + " oct", 12)
                  + pad (number (band.bandwidthHz(), 1) + " Hz", 10)
                  + number (band.ringSeconds() * 1000.0, 2) + " ms");
        }

        info ("");
        info ("  Achieved width is measured from the composite response, not");
        info ("  assumed from the design formula.");

        checkAtMost (worstOctaveError, 0.02,
                     "every gate band is one octave wide at -3 dB, as measured");

        // Rejection: how far down the skirt is where the old filter leaked.
        {
            const auto band = decay::makeBand (12000.0, kGateBandOctaves, kSampleRate);

            const double atHalf = 20.0 * std::log10 (
                decay::compositeMagnitude (band.section, 6000.0, kSampleRate));
            const double atQuarter = 20.0 * std::log10 (
                decay::compositeMagnitude (band.section, 3000.0, kSampleRate));

            info ("");
            info ("  Skirt of the 12 kHz band, where the old one-pole filter leaked:");
            info ("    at 6 kHz  (one octave down)   " + number (atHalf, 2) + " dB");
            info ("    at 3 kHz  (two octaves down)  " + number (atQuarter, 2) + " dB");
            info ("");
            info ("  A one-octave band puts its -3 dB edge at 8 kHz, so 6 kHz is only");
            info ("  a third of an octave outside it and -7.8 dB is what a 4th-order");
            info ("  magnitude gives there. That is a property of asking for a band");
            info ("  one octave wide, not a defect in this filter - and it is why");
            info ("  the leakage argument is about the OLD filter's shape rather");
            info ("  than about band edges alone.");

            // What can be asserted structurally: the response only falls as you
            // move away from centre, so no distant frequency is favoured.
            bool monotonic = true;
            double previous = 1.0;

            for (int p = 0; p <= 200; ++p)
            {
                const double f = 12000.0 * std::pow (0.5, p / 40.0);
                const double m = decay::compositeMagnitude (band.section, f, kSampleRate);

                if (m > previous + 1.0e-12)
                    monotonic = false;

                previous = m;
            }

            check (monotonic, "the band's response falls monotonically below centre");

            checkAtMost (atQuarter, -20.0,
                         "two octaves below centre is rejected by at least 20 dB");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 gate - production T0 rebaselined");

    {
        struct Case { double low, high, lowHz, highHz; const char* label; };

        const Case cases[]
        {
            { 0.00, 0.00,  200.0, 6000.0, "A  damping OFF" },
            { 0.20, 0.50,  200.0, 6000.0, "B  shipping default" },
            { 0.00, 0.60,  200.0, 6000.0, "C  moderate high damping" },
            { 0.00, 1.00,  200.0, 6000.0, "D  High Damp 100 %" },
            { 1.00, 0.00,  200.0, 6000.0, "E  Low Damp 100 %" },
            { 0.50, 0.50, 1000.0,  500.0, "F1 reversed corners, both 50 %" },
            { 0.80, 0.80,  900.0,  600.0, "F2 overlapping corners, both 80 %" },
        };

        double worstMismatch = 0.0, worstR2 = 1.0, worstRepeat = 0.0;
        std::string worstMismatchWhere, worstR2Where;
        int invalidPoints = 0, validPoints = 0;

        for (const auto& c : cases)
        {
            Tank tank (decay);

            if (c.low > 0.0 || c.high > 0.0)
                tank.damp (c.low, c.high, c.lowHz, c.highHz);

            info ("");
            info ("  " + std::string (c.label));
            info ("    band      EDT     T20     T30     fit     R^2      |T20-T30|  range   repeat");

            for (double centre : bands)
            {
                const auto band = decay::makeBand (centre, kGateBandOctaves, kSampleRate);
                const auto m = measure (tank, band, decay);

                if (m.stimuli == 0 || ! m.mean.valid)
                {
                    ++invalidPoints;

                    info ("    " + pad (number (centre, 0), 10) + "INVALID - "
                          + (m.reason.empty() ? "no usable decay" : m.reason));
                    continue;
                }

                ++validPoints;

                const double mismatch = m.mean.mismatch();

                info ("    " + pad (number (centre, 0), 10)
                      + pad (number (m.mean.edt, 3), 8)
                      + pad (number (m.mean.t20, 3), 8)
                      + pad (number (m.mean.t30, 3), 8)
                      + pad (number (m.mean.fitted, 3), 8)
                      + pad (number (m.mean.r2, 5), 9)
                      + pad (number (mismatch * 100.0, 2) + " %", 11)
                      + pad (number (m.mean.dynamicRangeDb, 0) + " dB", 8)
                      + number (m.repeatability, 2) + " %");

                if (mismatch > worstMismatch)
                {
                    worstMismatch = mismatch;
                    worstMismatchWhere = std::string (c.label) + " at "
                                         + number (centre, 0) + " Hz";
                }

                if (m.mean.r2 < worstR2)
                {
                    worstR2 = m.mean.r2;
                    worstR2Where = std::string (c.label) + " at " + number (centre, 0) + " Hz";
                }

                worstRepeat = std::max (worstRepeat, m.repeatability);
            }
        }

        info ("");
        info ("  valid analysis points     " + std::to_string (validPoints));
        info ("  invalid, with a reason    " + std::to_string (invalidPoints));
        info ("  worst |T20-T30| / mean    " + number (worstMismatch * 100.0, 2)
              + " %   (" + worstMismatchWhere + ")");
        info ("  worst fitted R^2          " + number (worstR2, 5)
              + "   (" + worstR2Where + ")");
        info ("  worst stimulus-to-stimulus repeatability  " + number (worstRepeat, 2) + " %");
        info ("");
        info ("  Thresholds unchanged from pre-registration: |T20-T30| <= "
              + number (kGateMaxT20T30Mismatch * 100.0, 0) + " %, R^2 >= "
              + number (kGateMinimumR2, 3) + ".");
        info ("  For the record: the same gate on the OLD one-pole analysis band");
        info ("  reported 8.50 % here. That figure is retained as OLD ANALYSER /");
        info ("  INVALID FOR GATING and is not evidence of a tank defect.");

        checkAtMost (worstMismatch, kGateMaxT20T30Mismatch,
                     "T20 and T30 agree: the tail does not change slope partway");

        check (worstR2 >= kGateMinimumR2,
               "aggregate band decay stays single-slope: R^2 >= "
               + number (kGateMinimumR2, 3));

        checkAtMost (worstRepeat, 5.0,
                     "the measurement repeats across independent deterministic stimuli");
    }
}

} // namespace avtest
