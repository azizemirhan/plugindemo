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
    PHASE 7 — modulation isolation for the remaining T20/T30 residual.

    WHERE THIS PICKS UP
    -------------------
    The empirical checkpoint measured production T0's own aggregate RT60(f) and
    fed it to a system of perfect exponential modes. That reproduced the whole
    frequency TREND — 0.08 % at 1 kHz rising to 4.32 % at 12 kHz — and left an
    excess that is nearly independent of frequency: +1.19 / +1.44 / +1.54 /
    +1.68 pp. A residual that does not scale with the RT60 gradient is not
    coming from the decay law.

    The obvious difference between the two systems was never modelled: the tank
    modulates and the synthetic model is stationary. So modulation is isolated
    here before any amplitude-distribution model is invented, because a
    modal-residue law would be a new unvalidated assumption and this is a
    measurement.

    THE CONTROLLED COMPARISON
    -------------------------
    RT60(f) is re-measured independently for each modulation condition. That
    matters: modulation can move the decay law itself, and comparing both
    conditions against one shared law would fold that difference into the
    residual and call it curvature. Each condition gets its own measured law,
    its own held-out validation and its own synthetic control, so

        Delta_modulation = Excess(Mod 30 %) - Excess(Mod 0 %)

    is curvature beyond each condition's OWN spectral decay law.

    TEST ONLY. Nothing in Source/ is changed, the damping mapping and Option A
    normalisation are untouched, and every production default is used as it
    ships.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate  = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kEnsemble    = 32;
constexpr double kBandOctaves = 1.0;

// Production defaults, read from the shipping parameter layout.
constexpr double kDefaultModDepthControl = 0.30;   // Mod Depth 30 %
constexpr double kDefaultModRateHz       = 0.50;   // Mod Rate 0.5 Hz

// PRE-REGISTERED, fixed before the run.
constexpr double kHeldOutMedianLimit = 0.02;   // 2 %
constexpr double kHeldOutWorstLimit  = 0.05;   // 5 %
constexpr double kMaterialDropPp     = 0.75;   // >= this at 12 kHz => material
constexpr double kNotMaterialPp      = 0.25;   // <= this at 12 kHz => not material

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

inline constexpr int kBurstSamples = 1024;

[[nodiscard]] std::vector<double> makeExcitation (int index)
{
    std::vector<double> burst (static_cast<size_t> (kBurstSamples), 0.0);

    unsigned int rng = 2654435761u * static_cast<unsigned int> (index + 1) + 1013904223u;

    double sumSquares = 0.0;

    for (int n = 0; n < kBurstSamples; ++n)
    {
        rng = rng * 1664525u + 1013904223u;

        const double white = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
        const double window = 0.5 - 0.5 * std::cos (2.0 * decay::pi * n / (kBurstSamples - 1));

        burst[static_cast<size_t> (n)] = white * window;
        sumSquares += burst[static_cast<size_t> (n)] * burst[static_cast<size_t> (n)];
    }

    const double rms = std::sqrt (sumSquares / kBurstSamples);

    for (auto& s : burst)
        s /= std::max (rms, 1.0e-30);

    return burst;
}

// ---------------------------------------------------------------------------
//  The tank, in one of four configurations
// ---------------------------------------------------------------------------

struct Condition
{
    bool   dampingOn = true;
    double modDepthControl = 0.0;
    double modRateHz = kDefaultModRateHz;
    const char* label = "";
};

struct Tank
{
    FdnCore<8> core;

    explicit Tank (const Condition& condition)
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (kDecayTarget, true);

        if (condition.dampingOn)
            core.setDamping (mapping::lowDampRatioFor (0.0),
                             mapping::highDampRatioFor (1.0),
                             200.0, 6000.0, true);

        /*
            Exactly the call the processor makes for the shipped architecture,
            with the same mappings. Nothing here is a test-only modulation
            model: applyWithDepth is the production code path.
        */
        architecture::applyWithDepth (core, architecture::defaultCandidate,
                                      mapping::modDepthFor (condition.modDepthControl),
                                      mapping::modRateScaleFor (condition.modRateHz),
                                      true);

        core.resetModulation();
        core.reset();
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive, int numSamples)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[8] {};
        double state[8] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (n < static_cast<int> (drive.size()))
                             ? drive[static_cast<size_t> (n)] : 0.0;

            for (int i = 0; i < 8; ++i)
                injection[i] = x * fdn::injectionLeft[i] * fdn::signVectorNorm;

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < 8; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }
};

// ---------------------------------------------------------------------------
//  Ensemble measurement (the validated pipeline, unchanged)
// ---------------------------------------------------------------------------

struct Support
{
    double hz = 0.0;
    double rt60 = 0.0, r2 = 0.0;
    double convergencePercent = 0.0;
    double halfSplitPercent = 0.0;
    double mismatch = 0.0;          ///< recorded, never used to build a model
    bool valid = false;
    std::string reason;
};

[[nodiscard]] Support measureSupport (Tank& tank, double centreHz, int renderSamples)
{
    Support s;
    s.hz = centreHz;

    const auto band = decay::makeBand (centreHz, kBandOctaves, kSampleRate);

    std::vector<double> all (static_cast<size_t> (renderSamples), 0.0);
    std::vector<double> first16 (static_cast<size_t> (renderSamples), 0.0);
    std::vector<double> last16 (static_cast<size_t> (renderSamples), 0.0);

    int counted = 0;

    for (int k = 0; k < kEnsemble; ++k)
    {
        auto ir = tank.render (makeExcitation (k), renderSamples);
        band.apply (ir);

        const auto edc = decay::normalisedEdc (ir);

        if (edc.empty())
            continue;

        for (size_t i = 0; i < edc.size(); ++i)
        {
            all[i] += edc[i];

            if (k < 16) first16[i] += edc[i];
            else        last16[i]  += edc[i];
        }

        ++counted;
    }

    if (counted == 0)
    {
        s.reason = "no energy in band";
        return s;
    }

    const auto finish = [&] (std::vector<double>& acc, int n)
    {
        const double k = 1.0 / static_cast<double> (std::max (n, 1));

        for (auto& e : acc)
            e *= k;

        return decay::analyseEdc (acc, band, kSampleRate);
    };

    const auto a = finish (first16, 16);
    const auto b = finish (last16, counted - 16);
    const auto full = finish (all, counted);

    if (! full.valid)
    {
        s.reason = full.reason;
        return s;
    }

    s.rt60 = full.fitted;
    s.r2 = full.r2;
    s.mismatch = full.mismatch() * 100.0;

    if (a.valid && b.valid)
    {
        s.halfSplitPercent = std::abs (a.fitted - b.fitted)
                             / std::max (a.fitted, 1.0e-12) * 100.0;

        s.convergencePercent = std::abs (a.fitted - full.fitted)
                               / std::max (full.fitted, 1.0e-12) * 100.0;
    }

    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
//  Monotone (Fritsch-Carlson) interpolation in log frequency
// ---------------------------------------------------------------------------

class MonotoneCurve
{
public:
    void build (const std::vector<double>& logF, const std::vector<double>& y)
    {
        x = logF;
        v = y;

        const size_t n = x.size();

        m.assign (n, 0.0);

        if (n < 2)
            return;

        std::vector<double> delta (n - 1, 0.0);

        for (size_t i = 0; i + 1 < n; ++i)
            delta[i] = (v[i + 1] - v[i]) / (x[i + 1] - x[i]);

        m[0] = delta[0];
        m[n - 1] = delta[n - 2];

        for (size_t i = 1; i + 1 < n; ++i)
            m[i] = (delta[i - 1] * delta[i] <= 0.0)
                   ? 0.0
                   : 0.5 * (delta[i - 1] + delta[i]);

        for (size_t i = 0; i + 1 < n; ++i)
        {
            if (delta[i] == 0.0)
            {
                m[i] = 0.0;
                m[i + 1] = 0.0;
                continue;
            }

            const double a = m[i] / delta[i];
            const double b = m[i + 1] / delta[i];
            const double s = a * a + b * b;

            if (s > 9.0)
            {
                const double t = 3.0 / std::sqrt (s);
                m[i] = t * a * delta[i];
                m[i + 1] = t * b * delta[i];
            }
        }
    }

    [[nodiscard]] double at (double logFreq) const
    {
        const size_t n = x.size();

        if (n == 0)
            return 0.0;

        if (n == 1 || logFreq <= x.front())
            return v.front();

        if (logFreq >= x.back())
            return v.back();

        size_t i = 0;

        while (i + 2 < n && logFreq > x[i + 1])
            ++i;

        const double h = x[i + 1] - x[i];
        const double t = (logFreq - x[i]) / h;
        const double t2 = t * t, t3 = t2 * t;

        return (2.0 * t3 - 3.0 * t2 + 1.0) * v[i]
             + (t3 - 2.0 * t2 + t) * h * m[i]
             + (-2.0 * t3 + 3.0 * t2) * v[i + 1]
             + (t3 - t2) * h * m[i + 1];
    }

private:
    std::vector<double> x, v, m;
};

// ---------------------------------------------------------------------------
//  Stationary perfect-mode synthetic, unchanged from the empirical checkpoint
// ---------------------------------------------------------------------------

[[nodiscard]] inline double tankModalDensity() noexcept
{
    double total = 0.0;

    for (int i = 0; i < 8; ++i)
        total += fdn::delaysMs[static_cast<size_t> (i)] * 0.001;

    return total;
}

struct Mixture
{
    std::vector<double> frequency, rt60;
    int modes = 0;
};

[[nodiscard]] Mixture buildMixture (double centreHz, double density,
                                    const MonotoneCurve* curve, double flatRt60)
{
    Mixture m;

    const double lowHz  = std::max (20.0, centreHz / 3.5);
    const double highHz = std::min (centreHz * 3.5, 0.49 * kSampleRate);

    const double spacingHz = 1.0 / density;

    for (double f = lowHz; f < highHz; f += spacingHz)
    {
        m.frequency.push_back (f);
        m.rt60.push_back (curve != nullptr
                          ? std::max (0.05, curve->at (std::log (f)))
                          : flatRt60);
    }

    m.modes = static_cast<int> (m.frequency.size());
    return m;
}

[[nodiscard]] std::vector<double> renderMixture (const Mixture& m, int realisation,
                                                 int numSamples)
{
    std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

    unsigned int rng = 2654435761u * static_cast<unsigned int> (realisation + 1) + 33333u;

    for (int k = 0; k < m.modes; ++k)
    {
        rng = rng * 1664525u + 1013904223u;

        const double phase = 2.0 * decay::pi
                             * (static_cast<double> (rng >> 8) / 16777216.0);

        const double w = 2.0 * decay::pi * m.frequency[static_cast<size_t> (k)] / kSampleRate;
        const double radius = std::pow (10.0, -3.0 / (m.rt60[static_cast<size_t> (k)]
                                                      * kSampleRate));

        const double cr = radius * std::cos (w);
        const double ci = radius * std::sin (w);

        double zr = std::cos (phase), zi = std::sin (phase);

        for (int n = 0; n < numSamples; ++n)
        {
            out[static_cast<size_t> (n)] += zi;

            const double nr = zr * cr - zi * ci;
            zi = zr * ci + zi * cr;
            zr = nr;
        }
    }

    return out;
}

[[nodiscard]] decay::Result syntheticEnsemble (const Mixture& m, const decay::Band& band,
                                               double rt60Hint)
{
    const int length = static_cast<int> (kSampleRate * std::max (1.0, rt60Hint * 1.6));

    std::vector<double> accumulator (static_cast<size_t> (length), 0.0);

    int counted = 0;

    for (int r = 0; r < kEnsemble; ++r)
    {
        auto signal = renderMixture (m, r, length);
        band.apply (signal);

        const auto edc = decay::normalisedEdc (signal);

        if (edc.empty())
            continue;

        for (size_t i = 0; i < edc.size(); ++i)
            accumulator[i] += edc[i];

        ++counted;
    }

    if (counted == 0)
        return {};

    const double k = 1.0 / static_cast<double> (counted);

    for (auto& e : accumulator)
        e *= k;

    return decay::analyseEdc (accumulator, band, kSampleRate);
}

const double kProbes[] { 1000.0, 3000.0, 6000.0, 12000.0 };

// ---------------------------------------------------------------------------
//  One complete condition: measured law, held-out check, synthetic, excess
// ---------------------------------------------------------------------------

struct ConditionResult
{
    std::vector<Support> supports;
    MonotoneCurve curve;
    bool modelValid = false;
    double medianHeldOut = 0.0, worstHeldOut = 0.0;

    std::array<double, 4> tankMismatch {};
    std::array<double, 4> syntheticMismatch {};
    std::array<double, 4> syntheticR2 {};
    std::array<double, 4> excess {};
};

void measureLaw (Tank& tank, ConditionResult& out, const char* label)
{
    info ("  " + std::string (label)
          + ": 1/12-octave centres, 32-realisation ensemble, validated estimator.");
    info ("");
    info ("    centre     RT60      R^2       conv     half-split   [mismatch]");

    const int renderSamples = static_cast<int> (kSampleRate * 5.0);

    const double lowest = 2000.0, highest = 17000.0;
    const int points = static_cast<int> (std::round (std::log2 (highest / lowest) * 12.0));

    int rejected = 0;

    for (int i = 0; i <= points; ++i)
    {
        const double f = lowest * std::pow (2.0, i / 12.0);

        const auto s = measureSupport (tank, f, renderSamples);

        if (! s.valid)
        {
            ++rejected;
            info ("    " + pad (number (f, 0), 11) + "INVALID - " + s.reason);
            continue;
        }

        const bool reliable = s.r2 >= 0.99
                              && s.convergencePercent <= 2.0
                              && s.halfSplitPercent <= 3.0;

        // Printed sparsely: every third point, plus anything rejected.
        if ((i % 3) == 0 || ! reliable)
            info ("    " + pad (number (f, 0), 11)
                  + pad (number (s.rt60, 4), 10)
                  + pad (number (s.r2, 5), 10)
                  + pad (number (s.convergencePercent, 2) + " %", 9)
                  + pad (number (s.halfSplitPercent, 2) + " %", 13)
                  + pad (number (s.mismatch, 2) + " %", 10)
                  + (reliable ? "" : "  REJECTED"));

        if (reliable)
            out.supports.push_back (s);
        else
            ++rejected;
    }

    info ("");
    info ("  support points accepted   " + std::to_string (out.supports.size()));
    info ("  points rejected           " + std::to_string (rejected));
}

void validateLaw (ConditionResult& out, const char* label)
{
    std::vector<double> fitLog, fitValue;
    std::vector<const Support*> heldOut;

    for (size_t i = 0; i < out.supports.size(); ++i)
    {
        if ((i % 2) == 0)
        {
            fitLog.push_back (std::log (out.supports[i].hz));
            fitValue.push_back (out.supports[i].rt60);
        }
        else
        {
            heldOut.push_back (&out.supports[i]);
        }
    }

    if (fitLog.size() < 4 || heldOut.empty())
    {
        info ("  " + std::string (label) + ": not enough points to validate");
        return;
    }

    out.curve.build (fitLog, fitValue);

    std::vector<double> errors;

    for (const auto* s : heldOut)
    {
        const double predicted = out.curve.at (std::log (s->hz));
        errors.push_back (std::abs (predicted - s->rt60) / std::max (s->rt60, 1.0e-12));
    }

    std::sort (errors.begin(), errors.end());

    out.medianHeldOut = errors[errors.size() / 2];
    out.worstHeldOut = errors.back();

    info ("  " + pad (std::string (label), 10)
          + "held-out " + pad (std::to_string (heldOut.size()), 5)
          + "median " + pad (number (out.medianHeldOut * 100.0, 3) + " %", 12)
          + "worst " + pad (number (out.worstHeldOut * 100.0, 3) + " %", 12)
          + ((out.medianHeldOut <= kHeldOutMedianLimit
              && out.worstHeldOut <= kHeldOutWorstLimit) ? "VALID" : "INVALID"));

    out.modelValid = (out.medianHeldOut <= kHeldOutMedianLimit
                      && out.worstHeldOut <= kHeldOutWorstLimit);

    // Rebuild on every accepted point once validated, so the model the
    // synthetic actually uses is the best available description.
    if (out.modelValid)
    {
        std::vector<double> allLog, allValue;

        for (const auto& s : out.supports)
        {
            allLog.push_back (std::log (s.hz));
            allValue.push_back (s.rt60);
        }

        out.curve.build (allLog, allValue);
    }
}

void runSynthetic (Tank& tank, ConditionResult& out, const char* label)
{
    const double density = tankModalDensity();

    info ("");
    info ("  " + std::string (label)
          + "   perfect exponential modes, " + number (density, 4) + " modes/Hz");
    info ("    band      tank      synthetic   R^2        excess");

    for (size_t b = 0; b < 4; ++b)
    {
        const double f = kProbes[b];

        const auto s = measureSupport (tank, f, static_cast<int> (kSampleRate * 6.0));
        out.tankMismatch[b] = s.valid ? s.mismatch : 0.0;

        const auto band = decay::makeBand (f, kBandOctaves, kSampleRate);
        const double centreRt60 = std::max (0.05, out.curve.at (std::log (f)));

        const auto mixture = buildMixture (f, density, &out.curve, centreRt60);
        const auto r = syntheticEnsemble (mixture, band, centreRt60);

        out.syntheticMismatch[b] = r.valid ? r.mismatch() * 100.0 : 0.0;
        out.syntheticR2[b] = r.valid ? r.r2 : 0.0;
        out.excess[b] = out.tankMismatch[b] - out.syntheticMismatch[b];

        info ("    " + pad (number (f, 0), 10)
              + pad (number (out.tankMismatch[b], 2) + " %", 10)
              + pad (number (out.syntheticMismatch[b], 2) + " %", 12)
              + pad (number (out.syntheticR2[b], 5), 11)
              + number (out.excess[b], 2) + " pp");
    }
}

} // namespace

// ===========================================================================

void runDampingModulationTests()
{
    const Condition m0   { true, 0.0,  kDefaultModRateHz, "M0  Mod Depth 0 %" };
    const Condition m30  { true, kDefaultModDepthControl, kDefaultModRateHz,
                           "M30 Mod Depth 30 %" };

    ConditionResult r0, r30;

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 modulation - measured RT60(f), Mod Depth 0 %");

    {
        info ("  Decay 2.5 s, Low Damp 0 %, High Damp 100 %, 200 Hz / 6 kHz.");
        info ("  Modulation applied through the production architecture path.");
        info ("");

        Tank tank (m0);
        measureLaw (tank, r0, "M0");

        check (r0.supports.size() >= 24,
               "M0: enough reliable support points to describe the transition");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 modulation - measured RT60(f), Mod Depth 30 %");

    {
        info ("  Identical in every other respect. Mod Rate at the shipping");
        info ("  default of " + number (kDefaultModRateHz, 2) + " Hz.");
        info ("");

        Tank tank (m30);
        measureLaw (tank, r30, "M30");

        check (r30.supports.size() >= 24,
               "M30: enough reliable support points to describe the transition");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 modulation - independent held-out validation");

    {
        info ("  Each condition gets its OWN law. Comparing both against one");
        info ("  shared law would fold any decay-law difference caused by");
        info ("  modulation into the residual and then call it curvature.");
        info ("");

        validateLaw (r0, "M0");
        validateLaw (r30, "M30");

        info ("");
        info ("  Limits: median <= " + number (kHeldOutMedianLimit * 100.0, 0)
              + " %, worst <= " + number (kHeldOutWorstLimit * 100.0, 0) + " %.");

        check (r0.modelValid, "M0 empirical RT60 model passes its held-out validation");
        check (r30.modelValid, "M30 empirical RT60 model passes its held-out validation");

        // How far modulation moved the decay law itself, which is a result in
        // its own right and not part of the residual.
        if (r0.modelValid && r30.modelValid)
        {
            info ("");
            info ("    band      RT60 M0    RT60 M30   difference");

            for (double f : kProbes)
            {
                const double a = r0.curve.at (std::log (f));
                const double b = r30.curve.at (std::log (f));

                info ("    " + pad (number (f, 0), 10)
                      + pad (number (a, 4), 11)
                      + pad (number (b, 4), 11)
                      + number ((b / std::max (a, 1.0e-12) - 1.0) * 100.0, 2) + " %");
            }
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 modulation - excess curvature per condition");

    {
        if (! (r0.modelValid && r30.modelValid))
        {
            info ("  One of the two empirical laws did not validate, so the");
            info ("  comparison is not run.");
            info ("");
            info ("  CLASSIFICATION: UNRESOLVED");

            check (true, "classification recorded against the pre-registered rule");
        }
        else
        {
            Tank tank0 (m0);
            Tank tank30 (m30);

            runSynthetic (tank0, r0, "M0 ");
            runSynthetic (tank30, r30, "M30");

            info ("");
            info ("    band      Excess_M0   Excess_M30   Delta_modulation");

            for (size_t b = 0; b < 4; ++b)
                info ("    " + pad (number (kProbes[b], 0), 10)
                      + pad (number (r0.excess[b], 2) + " pp", 12)
                      + pad (number (r30.excess[b], 2) + " pp", 13)
                      + number (r30.excess[b] - r0.excess[b], 2) + " pp");

            const double delta12k = r30.excess[3] - r0.excess[3];
            const double drop = r30.excess[3] - r0.excess[3];

            info ("");
            info ("  12 kHz Excess_M0    " + number (r0.excess[3], 2) + " pp");
            info ("  12 kHz Excess_M30   " + number (r30.excess[3], 2) + " pp");
            info ("  Delta_modulation    " + number (delta12k, 2) + " pp");
            info ("");
            info ("  Pre-registered: >= " + number (kMaterialDropPp, 2)
                  + " pp means material, <= " + number (kNotMaterialPp, 2)
                  + " pp means not material.");

            if (std::abs (drop) >= kMaterialDropPp)
                info ("  CLASSIFICATION: MODULATION MATERIAL DRIVER");
            else if (std::abs (drop) <= kNotMaterialPp)
                info ("  CLASSIFICATION: MODULATION NOT MATERIAL DRIVER");
            else
                info ("  CLASSIFICATION: UNRESOLVED");

            check (true, "classification recorded against the pre-registered rule");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 modulation - damping OFF control");

    {
        /*
            With damping off the decay law is spectrally flat, so any T20/T30
            offset that appears between Mod 0 % and Mod 30 % here belongs to
            modulation alone and to nothing about frequency-dependent decay.
            Half-split and convergence are printed with every number, because a
            difference of a few hundredths of a percent means nothing without
            them.
        */
        const Condition flat0  { false, 0.0, kDefaultModRateHz, "" };
        const Condition flat30 { false, kDefaultModDepthControl, kDefaultModRateHz, "" };

        Tank tank0 (flat0);
        Tank tank30 (flat30);

        info ("    band      Mod 0 %                        Mod 30 %                       diff");
        info ("              mismatch  conv    half         mismatch  conv    half");

        double worstDifference = 0.0;
        double worstPrecision = 0.0;

        for (double f : kProbes)
        {
            const auto a = measureSupport (tank0, f, static_cast<int> (kSampleRate * 6.0));
            const auto b = measureSupport (tank30, f, static_cast<int> (kSampleRate * 6.0));

            if (! (a.valid && b.valid))
            {
                info ("    " + pad (number (f, 0), 10) + "INVALID");
                continue;
            }

            const double difference = b.mismatch - a.mismatch;

            worstDifference = std::max (worstDifference, std::abs (difference));
            worstPrecision = std::max ({ worstPrecision, a.halfSplitPercent,
                                         b.halfSplitPercent });

            info ("    " + pad (number (f, 0), 10)
                  + pad (number (a.mismatch, 2) + " %", 10)
                  + pad (number (a.convergencePercent, 2) + " %", 8)
                  + pad (number (a.halfSplitPercent, 2) + " %", 13)
                  + pad (number (b.mismatch, 2) + " %", 10)
                  + pad (number (b.convergencePercent, 2) + " %", 8)
                  + pad (number (b.halfSplitPercent, 2) + " %", 13)
                  + number (difference, 2) + " pp");
        }

        info ("");
        info ("  worst Mod 0 -> Mod 30 difference   " + number (worstDifference, 3) + " pp");
        info ("  worst half-ensemble precision      " + number (worstPrecision, 3) + " %");
        info ("");
        info ("  A difference smaller than the precision figure is not evidence");
        info ("  of anything and is not read as one.");

        check (true, "damping-off control measured with its precision alongside");
    }
}

} // namespace avtest
