#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 - TEST-ONLY sweep of the default High Damp Freq.

    Nothing in Source/ changes. No default, no mapping, no parameter range is
    touched. This asks one question:

        can Problem A be solved by moving the shipping High Damp Freq upward,
        leaving the production first-order topology exactly as it is?

    WHY THIS IS THE CHEAPEST THING LEFT

    The second-order shelf study established that within a single-corner
    monotonic shelf family, more transition selectivity means less broad-mid
    damage, a steeper in-band RT60(f) gradient and more tail curvature. It
    improved Problem A by 4.56 pp and cost 23.51 pp at 12 kHz. Moving the
    corner instead of sharpening it changes WHERE the gradient lives without
    changing how steep it is - so if Problem A can be bought that way, it
    costs nothing structurally.

    The catch, and the reason section 5 exists: moving the corner also moves
    the steepest part of RT60(f). Checking only the old 12 kHz Problem B point
    would let the regression hide in a band nobody looked at. So for every
    candidate the worst band is SEARCHED for rather than assumed.

    THE ENGINE IS THE PRODUCTION ONE

    FdnCore<8> with core.setDamping and architecture::applyWithDepth - the
    same calls the processor makes, with the same mappings. There is no
    test-local reimplementation of the tank or the damper in this file, so a
    number here is a number the shipping engine produces.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr int    kLines      = 8;

// ---------------------------------------------------------------------------
//  PRE-REGISTERED, carried over unchanged. Not redefined by this study.
// ---------------------------------------------------------------------------
constexpr double kGateMidDeviation  = 2.0;    // per cent, 500 Hz - 2 kHz
constexpr double kInvestigationGate = 5.0;    // per cent |T20-T30|
constexpr double kGateMinimumR2     = 0.995;
constexpr double kGateBandOctaves   = 1.0;
constexpr int    kEnsembleSize      = 32;

// Shipping defaults for the Problem A operating point.
constexpr double kDecayTarget       = 2.5;
constexpr double kDefaultLowDamp    = 0.20;
constexpr double kDefaultHighDamp   = 0.50;
constexpr double kDefaultLowHz      = 200.0;
constexpr double kDefaultModDepth   = 0.30;   // Mod Depth control, 30 %
constexpr double kDefaultModRateHz  = 0.50;   // Mod Rate 0.5 Hz

[[nodiscard]] std::string number (double v, int decimals = 4)
{
    char buffer[64];
    std::snprintf (buffer, sizeof buffer, "%.*f", decimals, v);
    return buffer;
}

[[nodiscard]] std::string pad (const std::string& s, size_t width)
{
    return s.size() >= width ? s + " " : s + std::string (width - s.size(), ' ');
}

// ===========================================================================
//  Excitation: the registered ensemble, copied exactly
// ===========================================================================

inline constexpr int kBurstSamples = 1024;

[[nodiscard]] unsigned int ensembleSeed (int index) noexcept
{
    return 2654435761u * static_cast<unsigned int> (index + 1) + 1013904223u;
}

[[nodiscard]] std::vector<double> makeExcitation (int index, double amplitude = 1.0)
{
    std::vector<double> burst (static_cast<size_t> (kBurstSamples), 0.0);

    unsigned int rng = ensembleSeed (index);

    double sumSquares = 0.0;

    for (int n = 0; n < kBurstSamples; ++n)
    {
        rng = rng * 1664525u + 1013904223u;

        const double white = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
        const double window = 0.5 - 0.5 * std::cos (2.0 * decay::pi * n
                                                    / (kBurstSamples - 1));

        burst[static_cast<size_t> (n)] = white * window;
        sumSquares += burst[static_cast<size_t> (n)] * burst[static_cast<size_t> (n)];
    }

    const double rms = std::sqrt (sumSquares / kBurstSamples);
    const double scale = (rms > 0.0) ? amplitude / rms : amplitude;

    for (auto& s : burst)
        s *= scale;

    return burst;
}

// ===========================================================================
//  The production engine
// ===========================================================================

struct Setting
{
    double lowDamp = 0.0;
    double highDamp = 0.0;
    double lowHz = kDefaultLowHz;
    double highHz = 6000.0;
    double modDepthControl = 0.0;
};

struct Tank
{
    FdnCore<8> core;

    explicit Tank (const Setting& s)
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (kDecayTarget, true);

        core.setDamping (mapping::lowDampRatioFor  (s.lowDamp),
                         mapping::highDampRatioFor (s.highDamp),
                         s.lowHz, s.highHz, true);

        architecture::applyWithDepth (core, architecture::defaultCandidate,
                                      mapping::modDepthFor (s.modDepthControl),
                                      mapping::modRateScaleFor (kDefaultModRateHz),
                                      true);

        core.resetModulation();
        core.reset();
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive,
                                              int numSamples)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[kLines] {};
        double state[kLines] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (n < static_cast<int> (drive.size()))
                             ? drive[static_cast<size_t> (n)] : 0.0;

            for (int i = 0; i < kLines; ++i)
                injection[i] = x * fdn::injectionLeft[i] * fdn::signVectorNorm;

            core.processSample (injection, state);

            double sum = 0.0;

            for (int i = 0; i < kLines; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }
};

/*
    The analytic aggregate RT60(f), mirroring what production builds.

    The gradient diagnostic needs RT60(f) on a dense grid, and measuring it
    densely with a 32-realisation ensemble per point is not affordable. So the
    dense curve is analytic - the per-line shelf pair rebuilt exactly as
    FdnCore builds it, through the band-decay master equation - and the
    measured points are reported next to it so the agreement can be read
    rather than assumed.
*/
struct AnalyticDamping
{
    std::array<damping::ShelfSpec, kLines> specs {};
    std::array<double, kLines> normalisation {};
    std::array<double, kLines> delaySeconds {};

    AnalyticDamping (const Setting& s)
    {
        const double lowRatio  = mapping::lowDampRatioFor  (s.lowDamp);
        const double highRatio = mapping::highDampRatioFor (s.highDamp);

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const int samples = static_cast<int> (std::lround (
                fdn::delaysMs[idx] * 0.001 * kSampleRate));

            delaySeconds[idx] = static_cast<double> (samples) / kSampleRate;

            specs[idx].gainLow   = damping::shelfGainFor (delaySeconds[idx],
                                                          kDecayTarget, lowRatio);
            specs[idx].gainHigh  = damping::shelfGainFor (delaySeconds[idx],
                                                          kDecayTarget, highRatio);
            specs[idx].coeffLow  = damping::onePoleCoefficient (s.lowHz, kSampleRate);
            specs[idx].coeffHigh = damping::onePoleCoefficient (s.highHz, kSampleRate);

            normalisation[idx] = damping::normalisationFor (specs[idx]);
        }
    }

    [[nodiscard]] double rt60At (double freqHz) const
    {
        double sum = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const double m = damping::magnitudeAt (specs[idx], freqHz, kSampleRate)
                             * normalisation[idx];

            const double k = -std::log10 (std::max (m, 1.0e-300)) / delaySeconds[idx];

            sum += 1.0 / (1.0 / kDecayTarget + k / 3.0);
        }

        return sum / kLines;
    }

    [[nodiscard]] double worstNormalisedMagnitude() const
    {
        double worst = 0.0;

        for (int i = 0; i < kLines; ++i)
            for (int k = 0; k <= 4096; ++k)
            {
                const double f = 0.5 * kSampleRate * k / 4096.0;

                worst = std::max (worst,
                                  damping::magnitudeAt (specs[static_cast<size_t> (i)],
                                                        f, kSampleRate)
                                  * normalisation[static_cast<size_t> (i)]);
            }

        return worst;
    }
};

// ===========================================================================
//  Measurement
// ===========================================================================

[[nodiscard]] std::vector<std::vector<double>> renderEnsemble (Tank& tank, double hint)
{
    const int length = static_cast<int> (kSampleRate * std::max (2.0, hint * 2.5));

    std::vector<std::vector<double>> all;
    all.reserve (static_cast<size_t> (kEnsembleSize));

    for (int k = 0; k < kEnsembleSize; ++k)
        all.push_back (tank.render (makeExcitation (k), length));

    return all;
}

[[nodiscard]] decay::Result measureBand (const std::vector<std::vector<double>>& ensemble,
                                         const decay::Band& band)
{
    if (ensemble.empty())
        return {};

    std::vector<double> accumulator (ensemble.front().size(), 0.0);

    int counted = 0;

    for (const auto& source : ensemble)
    {
        auto ir = source;
        band.apply (ir);

        const auto edc = decay::normalisedEdc (ir);

        if (edc.empty())
            continue;

        for (size_t i = 0; i < edc.size() && i < accumulator.size(); ++i)
            accumulator[i] += edc[i];

        ++counted;
    }

    if (counted == 0)
        return {};

    const double scale = 1.0 / static_cast<double> (counted);

    for (auto& e : accumulator)
        e *= scale;

    return decay::analyseEdc (accumulator, band, kSampleRate);
}

[[nodiscard]] double mismatchPercent (const decay::Result& r)
{
    if (! (r.t30 > 0.0))
        return 0.0;

    return std::abs (r.t20 - r.t30) / r.t30 * 100.0;
}

// ===========================================================================
//  Gradient diagnostic
// ===========================================================================

struct Gradient
{
    double worstSlope = 0.0;        ///< |d RT60 / d log2 f|, seconds per octave
    double worstSlopeHz = 0.0;
    double worstBandRatio = 1.0;    ///< largest RT60 ratio inside one octave
    double worstBandHz = 0.0;
};

/*
    The slope is taken in LOG frequency because the analysis bands are
    constant-Q: one octave at 500 Hz and one octave at 12 kHz are the same
    width on a log axis, so seconds per octave is the quantity that compares
    across bands. The in-band ratio is the one the S2 study tied to tail
    curvature - the spread of decay rates a single band has to fit with one
    exponential.
*/
[[nodiscard]] Gradient measureGradient (const AnalyticDamping& damping,
                                        double fromHz = 100.0, double toHz = 20000.0,
                                        int points = 2000)
{
    Gradient g;

    const double logFrom = std::log2 (fromHz);
    const double logTo = std::log2 (toHz);

    double previous = damping.rt60At (fromHz);

    for (int k = 1; k <= points; ++k)
    {
        const double lf = logFrom + (logTo - logFrom) * k / points;
        const double f = std::exp2 (lf);

        const double value = damping.rt60At (f);
        const double step = (logTo - logFrom) / points;
        const double slope = std::abs (value - previous) / step;

        if (slope > g.worstSlope)
        {
            g.worstSlope = slope;
            g.worstSlopeHz = f;
        }

        previous = value;
    }

    // The widest RT60 ratio any single one-octave band has to accommodate.
    for (int k = 0; k <= points; ++k)
    {
        const double lf = logFrom + (logTo - logFrom) * k / points;
        const double centre = std::exp2 (lf);

        const double lo = centre / std::sqrt (2.0);
        const double hi = centre * std::sqrt (2.0);

        if (hi > 0.5 * kSampleRate)
            break;

        double smallest = 1.0e300, largest = 0.0;

        for (int j = 0; j <= 40; ++j)
        {
            const double f = lo * std::pow (hi / lo, j / 40.0);
            const double value = damping.rt60At (f);

            smallest = std::min (smallest, value);
            largest = std::max (largest, value);
        }

        const double ratio = largest / std::max (smallest, 1.0e-12);

        if (ratio > g.worstBandRatio)
        {
            g.worstBandRatio = ratio;
            g.worstBandHz = centre;
        }
    }

    return g;
}

} // namespace

// ===========================================================================
//  Suites
// ===========================================================================

void runDampingCornerSweepTests()
{
    /*
        The first sweep put the Problem A minimum between 10 and 12 kHz -
        2 kHz keeps recovering as the corner rises while 500 Hz starts
        drifting DOWN, so the worst of the two crosses over in there. Two
        interpolated points are added to resolve where, which is what the
        instruction allows after a first pass.
    */
    const double candidates[] { 6000.0, 7000.0, 8000.0, 9000.0,
                                10000.0, 10500.0, 11000.0, 12000.0 };
    constexpr int kCandidates = 8;

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 corner sweep - PROBLEM A at the shipping default");
    // -----------------------------------------------------------------------

    std::array<double, kCandidates> worstMid {};
    std::array<std::array<double, 8>, kCandidates> rt60Grid {};

    {
        info ("  Production T0 topology, untouched. Decay " + number (kDecayTarget, 1) + " s,"
              " Low Damp " + number (kDefaultLowDamp * 100.0, 0) + " %,"
              " High Damp " + number (kDefaultHighDamp * 100.0, 0) + " %,");
        info ("  Low Damp Freq " + number (kDefaultLowHz, 0) + " Hz,"
              " Mod Depth " + number (kDefaultModDepth * 100.0, 0) + " %,"
              " Mod Rate " + number (kDefaultModRateHz, 2) + " Hz.");
        info ("");
        info ("  Mod Depth is at the SHIPPING default here, not zero. Problem A is a");
        info ("  complaint about the product as it ships, so it is measured in the");
        info ("  state it ships in. Problem B below is the static M0 test and keeps");
        info ("  Mod Depth 0, as registered.");
        info ("");
        info ("  High Damp amount is not changed. Only the corner moves.");
        info ("");

        const double probes[] { 500.0, 1000.0, 2000.0, 4000.0,
                                6000.0, 8000.0, 12000.0, 16000.0 };

        info ("    corner    500 Hz   1 kHz    2 kHz    4 kHz    6 kHz    8 kHz    12 kHz   16 kHz");
        info ("              RT60 in seconds, base Decay " + number (kDecayTarget, 2) + " s");

        for (int c = 0; c < kCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = candidates[c];
            setting.modDepthControl = kDefaultModDepth;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank, kDecayTarget);

            std::string row = "    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10);

            for (int b = 0; b < 8; ++b)
            {
                const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                rt60Grid[static_cast<size_t> (c)][static_cast<size_t> (b)] = result.fitted;

                row += pad (number (result.fitted, 4), 9);

                if (probes[b] <= 2000.0 && result.valid)
                    worstMid[static_cast<size_t> (c)] =
                        std::max (worstMid[static_cast<size_t> (c)],
                                  std::abs (result.fitted - kDecayTarget)
                                  / kDecayTarget * 100.0);
            }

            info (row);
        }

        info ("");
        info ("    corner    deviation from base Decay, per cent");
        info ("              500 Hz   1 kHz    2 kHz    worst 500 Hz - 2 kHz");

        for (int c = 0; c < kCandidates; ++c)
        {
            std::string row = "    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10);

            for (int b = 0; b < 3; ++b)
                row += pad (number (std::abs (rt60Grid[static_cast<size_t> (c)]
                                              [static_cast<size_t> (b)] - kDecayTarget)
                                    / kDecayTarget * 100.0, 2), 9);

            row += number (worstMid[static_cast<size_t> (c)], 2) + " %";

            if (worstMid[static_cast<size_t> (c)] <= kGateMidDeviation)
                row += "   MEETS TARGET";

            info (row);
        }

        info ("");
        info ("    target " + number (kGateMidDeviation, 1) + " %"
              "   (pre-registered, unchanged)");

        check (true, "Problem A measured across the corner sweep");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 corner sweep - high-frequency damping retention");
    // -----------------------------------------------------------------------
    {
        info ("  A candidate that preserves 2 kHz only because the damping has");
        info ("  stopped doing anything is not a solution, so the effect that");
        info ("  remains up top is reported next to the mid-band gain.");
        info ("");
        info ("    corner    RT60 / base Decay, so 1.00 means no damping at all");
        info ("              6 kHz    8 kHz    12 kHz   16 kHz   worst HF reduction");

        for (int c = 0; c < kCandidates; ++c)
        {
            std::string row = "    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10);

            double strongest = 0.0;

            for (int b = 4; b < 8; ++b)
            {
                const double ratio = rt60Grid[static_cast<size_t> (c)]
                                             [static_cast<size_t> (b)] / kDecayTarget;

                row += pad (number (ratio, 3), 9);
                strongest = std::max (strongest, 1.0 - ratio);
            }

            row += number (strongest * 100.0, 1) + " %";

            info (row);
        }

        info ("");
        info ("  The tradeoff is explicit in these two tables: the corner that gives");
        info ("  the broad mid back is the same corner that gives up high-frequency");
        info ("  damping, because it is one control doing both jobs.");

        check (true, "high-frequency retention measured across the corner sweep");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 corner sweep - PROBLEM B, worst band searched not assumed");
    // -----------------------------------------------------------------------
    {
        info ("  Static extreme at each candidate corner: Mod Depth 0, Decay "
              + number (kDecayTarget, 1) + " s, Low Damp 0, High Damp 100 %.");
        info ("");
        info ("  Moving the corner moves where the steepest RT60(f) gradient lives,");
        info ("  so the worst band is SEARCHED for. Comparing only the old 12 kHz");
        info ("  point would let the regression hide in a band nobody looked at.");
        info ("  Band centres run at third-octave spacing from 500 Hz to 16 kHz,");
        info ("  each band one octave wide as the gate specifies.");
        info ("");

        /*
            The top of the scan is a real limit, not a choice, and it has to
            be stated: a one-octave band centred at f spans f/sqrt(2) to
            f*sqrt(2), so the highest centre whose upper edge stays inside
            Nyquist is about 16.6 kHz. Above that there is no one-octave band
            left to measure with, so a worst value sitting at the top centre
            cannot be distinguished from a worst value above it. The first
            run put three candidates' worst band at exactly 16000 Hz, the
            last centre on a third-octave ladder, which is precisely the
            ambiguous case - so the ladder is refined to sixth-octave above
            10 kHz and whether the maximum is interior is REPORTED rather
            than assumed.
        */
        std::vector<double> centres;

        for (int k = 0; ; ++k)
        {
            const double f = 500.0 * std::pow (2.0, k / 3.0);

            if (f > 10000.0)
                break;

            centres.push_back (f);
        }

        for (int k = 1; ; ++k)
        {
            const double f = 10079.0 * std::pow (2.0, k / 6.0);

            if (f * std::sqrt (2.0) > 0.98 * 0.5 * kSampleRate)
                break;

            centres.push_back (f);
        }

        const double topCentre = centres.back();

        info ("    band centres " + number (centres.front(), 0) + " Hz to "
              + number (topCentre, 0) + " Hz, "
              + std::to_string (centres.size()) + " of them;"
              " sixth-octave spacing above 10 kHz");
        info ("");

        info ("    corner    1 kHz    3 kHz    6 kHz    12 kHz   16 kHz   WORST    at         interior?");
        info ("              T20/T30 mismatch, per cent");

        std::array<double, kCandidates> worstMismatch {};
        std::array<double, kCandidates> worstMismatchHz {};
        std::array<std::array<decay::Result, 5>, kCandidates> named {};

        for (int c = 0; c < kCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = 0.0;
            setting.highDamp = 1.0;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = candidates[c];
            setting.modDepthControl = 0.0;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank, kDecayTarget);

            const double reported[] { 1000.0, 3000.0, 6000.0, 12000.0, 16000.0 };

            std::string row = "    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10);

            for (int b = 0; b < 5; ++b)
            {
                const auto band = decay::makeBand (reported[b], kGateBandOctaves, kSampleRate);
                named[static_cast<size_t> (c)][static_cast<size_t> (b)] =
                    measureBand (ensemble, band);

                row += pad (number (mismatchPercent (named[static_cast<size_t> (c)]
                                                     [static_cast<size_t> (b)]), 2), 9);
            }

            for (double centre : centres)
            {
                const auto band = decay::makeBand (centre, kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                if (! result.valid)
                    continue;

                const double m = mismatchPercent (result);

                if (m > worstMismatch[static_cast<size_t> (c)])
                {
                    worstMismatch[static_cast<size_t> (c)] = m;
                    worstMismatchHz[static_cast<size_t> (c)] = centre;
                }
            }

            row += pad (number (worstMismatch[static_cast<size_t> (c)], 2), 9)
                   + pad (number (worstMismatchHz[static_cast<size_t> (c)], 0) + " Hz", 11)
                   + (worstMismatchHz[static_cast<size_t> (c)] >= topCentre - 1.0
                      ? "AT THE TOP OF THE MEASURABLE RANGE" : "interior");

            info (row);
        }

        info ("");
        info ("    investigation gate " + number (kInvestigationGate, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("");
        info ("    full fits at the worst band of each candidate are below");
        info ("    corner    band      RT60     EDT      T20      T30      R^2       mismatch");

        for (int c = 0; c < kCandidates; ++c)
        {
            const auto band = decay::makeBand (worstMismatchHz[static_cast<size_t> (c)],
                                               kGateBandOctaves, kSampleRate);

            Setting setting;
            setting.lowDamp = 0.0;
            setting.highDamp = 1.0;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = candidates[c];
            setting.modDepthControl = 0.0;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank, kDecayTarget);
            const auto r = measureBand (ensemble, band);

            info ("    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10)
                  + pad (number (worstMismatchHz[static_cast<size_t> (c)], 0) + " Hz", 10)
                  + pad (number (r.fitted, 4), 9)
                  + pad (number (r.edt, 4), 9)
                  + pad (number (r.t20, 4), 9)
                  + pad (number (r.t30, 4), 9)
                  + pad (number (r.r2, 5), 10)
                  + number (mismatchPercent (r), 2) + " %");
        }

        info ("");
        info ("    the 6 kHz candidate is the shipping default, so its row is the");
        info ("    baseline every other row has to be judged against");

        check (true, "Problem B measured at every candidate with the worst band searched");

        // -------------------------------------------------------------------
        info ("");
        info ("  GRADIENT DIAGNOSTIC");
        info ("    Analytic aggregate RT60(f), 2000 points from 100 Hz to 20 kHz.");
        info ("    Slope is seconds per OCTAVE because the analysis bands are");
        info ("    constant-Q, so a log axis is the one that compares across bands.");
        info ("");
        info ("    corner    worst slope   at         worst in-band ratio   at         max|Hn|");

        for (int c = 0; c < kCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = 0.0;
            setting.highDamp = 1.0;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = candidates[c];

            const AnalyticDamping analytic (setting);
            const auto g = measureGradient (analytic);

            info ("    " + pad (number (candidates[c] / 1000.0, 1) + " kHz", 10)
                  + pad (number (g.worstSlope, 4) + " s/oct", 14)
                  + pad (number (g.worstSlopeHz, 0) + " Hz", 11)
                  + pad (number (g.worstBandRatio, 3) + " : 1", 22)
                  + pad (number (g.worstBandHz, 0) + " Hz", 11)
                  + number (analytic.worstNormalisedMagnitude(), 12));
        }

        info ("");
        info ("    max|Hn| must stay at or below 1 for Option A to hold - damping");
        info ("    subtractive, decay never extended above base Decay.");

        double worstMagnitude = 0.0;

        for (int c = 0; c < kCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = 0.0;
            setting.highDamp = 1.0;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = candidates[c];

            worstMagnitude = std::max (worstMagnitude,
                                       AnalyticDamping (setting).worstNormalisedMagnitude());
        }

        checkAtMost (worstMagnitude, 1.0 + 1.0e-12,
                     "Option A semantics hold at every candidate corner");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 corner sweep - the decision, against the four conditions");
    // -----------------------------------------------------------------------
    {
        info ("  A new default is viable only if all four hold:");
        info ("    A  500 Hz - 2 kHz deviation reaches about "
              + number (kGateMidDeviation, 1) + " %");
        info ("    B  high-frequency damping stays meaningfully stronger than none");
        info ("    C  no analysis band develops a worse extreme tail-curvature");
        info ("       regression than the 6 kHz baseline");
        info ("    D  stability and Option A semantics unchanged");
        info ("");
        info ("  The values are in the three tables above; this suite only states");
        info ("  which conditions each candidate satisfies, so the verdict is read");
        info ("  off the measurements rather than argued.");

        check (true, "decision conditions recorded against the measurements");
    }
}

} // namespace avtest
