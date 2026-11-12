#pragma once

#include "DecayAnalyzer.h"

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
    Shared harness for the Phase 7 default-sweep studies.

    Extracted verbatim from Tests/DampingCornerSweep.cpp, which stays as it
    was committed. Any study using this header is expected to reproduce one
    number from that file as its first check, so the extraction is proved
    faithful by measurement rather than by inspection.

    The engine is the production one. FdnCore<8> with core.setDamping and
    architecture::applyWithDepth - the same calls the processor makes, with
    the same mappings. There is no test-local reimplementation of the tank or
    the damper here, so a number from this harness is a number the shipping
    engine produces.
*/

namespace avtest::sweep
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr int    kLines      = 8;

// PRE-REGISTERED, carried over unchanged. Not redefined by any study.
constexpr double kGateMidDeviation  = 2.0;    // per cent, 500 Hz - 2 kHz
constexpr double kInvestigationGate = 5.0;    // per cent |T20-T30|
constexpr double kGateMinimumR2     = 0.995;
constexpr double kGateBandOctaves   = 1.0;
constexpr int    kEnsembleSize      = 32;

// Shipping defaults.
constexpr double kDecayTarget       = 2.5;
constexpr double kDefaultLowDamp    = 0.20;
constexpr double kDefaultHighDamp   = 0.50;
constexpr double kDefaultLowHz      = 200.0;
constexpr double kDefaultModDepth   = 0.30;   // Mod Depth control, 30 %
constexpr double kDefaultModRateHz  = 0.50;   // Mod Rate 0.5 Hz

// Provisional candidate from the corner sweep. Not a production default.
constexpr double kCandidateHighHz   = 10000.0;

[[nodiscard]] inline std::string number (double v, int decimals = 4)
{
    char buffer[64];
    std::snprintf (buffer, sizeof buffer, "%.*f", decimals, v);
    return buffer;
}

[[nodiscard]] inline std::string pad (const std::string& s, size_t width)
{
    return s.size() >= width ? s + " " : s + std::string (width - s.size(), ' ');
}

// ---------------------------------------------------------------------------
//  Excitation: the registered ensemble, copied exactly
// ---------------------------------------------------------------------------

inline constexpr int kBurstSamples = 1024;

[[nodiscard]] inline unsigned int ensembleSeed (int index) noexcept
{
    return 2654435761u * static_cast<unsigned int> (index + 1) + 1013904223u;
}

[[nodiscard]] inline std::vector<double> makeExcitation (int index,
                                                         double amplitude = 1.0)
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

// ---------------------------------------------------------------------------
//  The production engine
// ---------------------------------------------------------------------------

struct Setting
{
    double lowDamp = 0.0;
    double highDamp = 0.0;
    double lowHz = kDefaultLowHz;
    double highHz = kCandidateHighHz;
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

    /*
        The same render, both channels. Modulation's reason for existing is
        partly stereo, so the benefit metrics need L and R from one pass -
        the tank is stateful, so rendering them separately would not give
        the same realisation.
    */
    void renderStereo (const std::vector<double>& drive, int numSamples,
                       std::vector<double>& left, std::vector<double>& right)
    {
        core.reset();

        left.assign (static_cast<size_t> (numSamples), 0.0);
        right.assign (static_cast<size_t> (numSamples), 0.0);

        double injection[kLines] {};
        double state[kLines] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (n < static_cast<int> (drive.size()))
                             ? drive[static_cast<size_t> (n)] : 0.0;

            for (int i = 0; i < kLines; ++i)
                injection[i] = x * fdn::injectionLeft[i] * fdn::signVectorNorm;

            core.processSample (injection, state);

            double l = 0.0, r = 0.0;

            for (int i = 0; i < kLines; ++i)
            {
                l += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;
                r += state[i] * fdn::readoutRight[i] * fdn::signVectorNorm;
            }

            left[static_cast<size_t> (n)] = l;
            right[static_cast<size_t> (n)] = r;
        }
    }
};

/*
    The analytic aggregate RT60(f), mirroring what production builds.

    A gradient diagnostic needs RT60(f) on a dense grid, and measuring it
    densely with a 32-realisation ensemble per point is not affordable. So
    the dense curve is analytic - the per-line shelf pair rebuilt exactly as
    FdnCore builds it, through the band-decay master equation - and the
    measured points are reported next to it so the agreement can be read
    rather than assumed.
*/
struct AnalyticDamping
{
    std::array<damping::ShelfSpec, kLines> specs {};
    std::array<double, kLines> normalisation {};
    std::array<double, kLines> delaySeconds {};

    explicit AnalyticDamping (const Setting& s)
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

    [[nodiscard]] double kSpread (double freqHz) const
    {
        double lowest = 1.0e300, highest = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const double m = damping::magnitudeAt (specs[idx], freqHz, kSampleRate)
                             * normalisation[idx];

            const double k = -std::log10 (std::max (m, 1.0e-300)) / delaySeconds[idx];
            const double rt = 1.0 / (1.0 / kDecayTarget + k / 3.0);

            lowest  = std::min (lowest, rt);
            highest = std::max (highest, rt);
        }

        return (highest - lowest) / std::max (highest, 1.0e-12) * 100.0;
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

// ---------------------------------------------------------------------------
//  Measurement
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::vector<std::vector<double>> renderEnsemble (
    Tank& tank, double hint = kDecayTarget)
{
    const int length = static_cast<int> (kSampleRate * std::max (2.0, hint * 2.5));

    std::vector<std::vector<double>> all;
    all.reserve (static_cast<size_t> (kEnsembleSize));

    for (int k = 0; k < kEnsembleSize; ++k)
        all.push_back (tank.render (makeExcitation (k), length));

    return all;
}

[[nodiscard]] inline decay::Result measureBand (
    const std::vector<std::vector<double>>& ensemble, const decay::Band& band)
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

[[nodiscard]] inline double mismatchPercent (const decay::Result& r)
{
    if (! (r.t30 > 0.0))
        return 0.0;

    return std::abs (r.t20 - r.t30) / r.t30 * 100.0;
}

// ---------------------------------------------------------------------------
//  Gradient diagnostic
// ---------------------------------------------------------------------------

struct Gradient
{
    double worstSlope = 0.0;        ///< |d RT60 / d log2 f|, seconds per octave
    double worstSlopeHz = 0.0;
    double worstBandRatio = 1.0;    ///< largest RT60 ratio inside one octave
    double worstBandHz = 0.0;
};

/*
    The slope is taken in LOG frequency because the analysis bands are
    constant-Q: one octave at 60 Hz and one octave at 12 kHz are the same
    width on a log axis, so seconds per octave is the quantity that compares
    across bands. The in-band ratio is the one the second-order shelf study
    tied to tail curvature - the spread of decay rates a single band has to
    fit with one exponential.
*/
[[nodiscard]] inline Gradient measureGradient (const AnalyticDamping& damping,
                                               double fromHz, double toHz,
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

} // namespace avtest::sweep
