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
    PHASE 7 — empirical RT60(f) -> synthetic perfect-mode control.

    THE LAST MODEL-VALIDATION STEP FOR PROBLEM B
    --------------------------------------------
    The earlier synthetic control was fed an analytic decay law built from the
    mean of the eight per-line RT60 values, and that input failed its own health
    check by 21.54 % — the eight lines disagree by up to 25 % where it matters,
    and a mixture's tail follows its slowest component rather than the mean.

    So the model input is replaced by the thing itself: the MEASURED aggregate
    decay law of production T0, on a dense grid, through the validated ensemble
    estimator. The question is then clean:

        given the same RT60-versus-frequency law, does a system of perfect
        exponential modes — no feedback, no matrix, no filters — produce the
        same T20/T30 curvature the tank produces?

    NOTHING CURVATURE-RELATED IS FITTED
    -----------------------------------
    The only quantity taken from T0 is fitted RT60. T20, T30, their mismatch,
    EDT and R^2 are never used to build or tune the model, and the interpolation
    is validated on measurement centres held out of its construction.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kEnsemble = 32;
constexpr double kGateBandOctaves = 1.0;

// PRE-REGISTERED, fixed before the run.
constexpr double kHeldOutMedianLimit = 0.02;   // 2 %
constexpr double kHeldOutWorstLimit  = 0.05;   // 5 %
constexpr double kCriticalWindowPp   = 1.0;    // +/- 1 pp at 12 kHz
constexpr double kResidualLimit      = 3.0;    // <= 3 % is a real engine residual

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

// ---------------------------------------------------------------------------
//  Excitations and tank — the validated definitions
// ---------------------------------------------------------------------------

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

struct Tank
{
    FdnCore<8> core;

    Tank()
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (kDecayTarget, true);
        core.setDamping (mapping::lowDampRatioFor (0.0),
                         mapping::highDampRatioFor (1.0),
                         200.0, 6000.0, true);
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

/** One support point of the empirical law, with its own reliability figures. */
struct Support
{
    double hz = 0.0;
    double rt60 = 0.0, r2 = 0.0;
    double convergencePercent = 0.0;   ///< N16 -> N32 change in fitted RT60
    double halfSplitPercent = 0.0;     ///< 16 vs 16
    double mismatch = 0.0;             ///< recorded, never used to build the model
    bool valid = false;
    std::string reason;
};

[[nodiscard]] Support measureSupport (Tank& tank, double centreHz, int renderSamples)
{
    Support s;
    s.hz = centreHz;

    const auto band = decay::makeBand (centreHz, kGateBandOctaves, kSampleRate);

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

        // N=16 stands in for the convergence check: the difference between the
        // first half alone and the whole set is the same comparison the
        // convergence rule asks for.
        s.convergencePercent = std::abs (a.fitted - full.fitted)
                               / std::max (full.fitted, 1.0e-12) * 100.0;
    }

    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
//  Monotone (Fritsch-Carlson) interpolation in log frequency
// ---------------------------------------------------------------------------
/*
    Shape preserving on purpose. A high-order spline through a decay law that
    falls steeply through a transition will overshoot, and an overshoot here
    would put RT60 values into the synthetic model that the tank never had.
*/
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

        // Fritsch-Carlson limiter.
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
//  Synthetic modal mixture
// ---------------------------------------------------------------------------

[[nodiscard]] inline double tankModalDensity() noexcept
{
    double total = 0.0;

    for (int i = 0; i < 8; ++i)
        total += fdn::delaysMs[static_cast<size_t> (i)] * 0.001;

    return total;
}

enum class Spacing { uniform, jittered };

struct Mixture
{
    std::vector<double> frequency, rt60;
    int modes = 0;
};

[[nodiscard]] Mixture buildMixture (double centreHz, double density, Spacing spacing,
                                    const MonotoneCurve* curve, double flatRt60)
{
    Mixture m;

    const double lowHz  = std::max (20.0, centreHz / 3.5);
    const double highHz = std::min (centreHz * 3.5, 0.49 * kSampleRate);

    const double spacingHz = 1.0 / density;

    unsigned int rng = 987654321u;

    for (double f = lowHz; f < highHz; )
    {
        m.frequency.push_back (f);
        m.rt60.push_back (curve != nullptr
                          ? std::max (0.05, curve->at (std::log (f)))
                          : flatRt60);

        double step = spacingHz;

        if (spacing == Spacing::jittered)
        {
            rng = rng * 1664525u + 1013904223u;
            const double u = static_cast<double> (rng >> 8) / 16777216.0;
            step = spacingHz * (0.6 + 0.8 * u);
        }

        f += step;
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

} // namespace

// ===========================================================================

void runDampingEmpiricalTests()
{
    Tank tank;

    std::vector<Support> supports;
    MonotoneCurve curve;
    bool modelValid = false;

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 empirical - measured aggregate RT60(f) of production T0");

    {
        info ("  Decay 2.5 s, Low Damp 0 %, High Damp 100 %, 200 Hz / 6 kHz.");
        info ("  1/12-octave centres, 32-realisation ensemble, validated estimator.");
        info ("");
        info ("    centre     RT60      R^2       conv     half-split   [mismatch]");

        const int renderSamples = static_cast<int> (kSampleRate * 5.0);

        // 1/12 octave from 2 kHz to 17 kHz.
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

            // Reliability is judged before the point is allowed into the model.
            const bool reliable = s.r2 >= 0.99
                                  && s.convergencePercent <= 2.0
                                  && s.halfSplitPercent <= 3.0;

            info ("    " + pad (number (f, 0), 11)
                  + pad (number (s.rt60, 4), 10)
                  + pad (number (s.r2, 5), 10)
                  + pad (number (s.convergencePercent, 2) + " %", 9)
                  + pad (number (s.halfSplitPercent, 2) + " %", 13)
                  + pad (number (s.mismatch, 2) + " %", 10)
                  + (reliable ? "" : "  REJECTED"));

            if (reliable)
                supports.push_back (s);
            else
                ++rejected;
        }

        info ("");
        info ("  support points accepted   " + std::to_string (supports.size()));
        info ("  points rejected           " + std::to_string (rejected));
        info ("");
        info ("  The mismatch column is printed for the record only. It is not used");
        info ("  to build, tune or validate the model.");

        check (supports.size() >= 24,
               "enough reliable support points to describe the transition");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 empirical - interpolation and held-out validation");

    {
        info ("  Monotone Fritsch-Carlson cubic Hermite in log frequency. Shape");
        info ("  preserving on purpose: an overshooting spline would hand the");
        info ("  synthetic model RT60 values the tank never had.");
        info ("");

        std::vector<double> fitX, fitY, holdX, holdY;

        for (size_t i = 0; i < supports.size(); ++i)
        {
            if (i % 2 == 0)
            {
                fitX.push_back (std::log (supports[i].hz));
                fitY.push_back (supports[i].rt60);
            }
            else
            {
                holdX.push_back (std::log (supports[i].hz));
                holdY.push_back (supports[i].rt60);
            }
        }

        MonotoneCurve halfCurve;
        halfCurve.build (fitX, fitY);

        std::vector<double> errors;

        info ("    held-out centre   measured    predicted   error");

        for (size_t i = 0; i < holdX.size(); ++i)
        {
            const double predicted = halfCurve.at (holdX[i]);
            const double error = std::abs (predicted - holdY[i]) / std::max (holdY[i], 1.0e-12);

            errors.push_back (error);

            if (i % 3 == 0 || error > 0.02)
                info ("    " + pad (number (std::exp (holdX[i]), 0), 18)
                      + pad (number (holdY[i], 4), 12)
                      + pad (number (predicted, 4), 12)
                      + number (error * 100.0, 3) + " %");
        }

        double median = 0.0, worst = 0.0;

        if (! errors.empty())
        {
            auto sorted = errors;
            std::sort (sorted.begin(), sorted.end());
            median = sorted[sorted.size() / 2];
            worst = sorted.back();
        }

        info ("");
        info ("  held-out points        " + std::to_string (errors.size()));
        info ("  median held-out error  " + number (median * 100.0, 3)
              + " %   (limit " + number (kHeldOutMedianLimit * 100.0, 0) + " %)");
        info ("  worst held-out error   " + number (worst * 100.0, 3)
              + " %   (limit " + number (kHeldOutWorstLimit * 100.0, 0) + " %)");

        modelValid = (median <= kHeldOutMedianLimit) && (worst <= kHeldOutWorstLimit);

        info ("");
        info (modelValid ? "  EMPIRICAL RT60 MODEL VALID"
                         : "  EMPIRICAL RT60 MODEL INVALID");

        checkAtMost (median, kHeldOutMedianLimit,
                     "the interpolated decay law predicts points it never saw");

        checkAtMost (worst, kHeldOutWorstLimit,
                     "no held-out point is badly mispredicted");

        // The model actually used is built from every accepted point.
        std::vector<double> allX, allY;

        for (const auto& s : supports)
        {
            allX.push_back (std::log (s.hz));
            allY.push_back (s.rt60);
        }

        curve.build (allX, allY);

        double residual = 0.0;

        for (size_t i = 0; i < allX.size(); ++i)
            residual = std::max (residual,
                                 std::abs (curve.at (allX[i]) - allY[i])
                                 / std::max (allY[i], 1.0e-12));

        info ("  worst residual at the support points themselves: "
              + number (residual * 100.0, 6) + " %");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 empirical - synthetic perfect-mode control");

    {
        if (! modelValid)
        {
            info ("  The empirical RT60 model did not pass its held-out validation,");
            info ("  so the synthetic comparison is not run.");
            info ("");
            info ("  CLASSIFICATION: STILL UNRESOLVED");

            check (true, "classification recorded against the pre-registered rule");
        }
        else
        {
            const double density = tankModalDensity();

            info ("  Perfect exponential modes at " + number (density, 4)
                  + " modes/Hz. No feedback, no matrix, no delay lines, no filters.");
            info ("  Every mode's RT60 is read from the measured law.");
            info ("");
            info ("    band     model            modes   T20     T30     RT60    R^2      |T20-T30|   T0");

            std::array<double, 4> t0Mismatch {}, syntheticUniform {};

            // T0's own figures on the same estimator, for the comparison column.
            for (size_t b = 0; b < 4; ++b)
            {
                const auto s = measureSupport (tank, kProbes[b],
                                               static_cast<int> (kSampleRate * 6.0));
                t0Mismatch[b] = s.valid ? s.mismatch : 0.0;
            }

            for (size_t b = 0; b < 4; ++b)
            {
                const double f = kProbes[b];
                const auto band = decay::makeBand (f, kGateBandOctaves, kSampleRate);
                const double centreRt60 = std::max (0.05, curve.at (std::log (f)));

                struct Variant { const char* label; Spacing spacing; bool flat; };

                const Variant variants[]
                {
                    { "empirical uniform ", Spacing::uniform,  false },
                    { "empirical jittered", Spacing::jittered, false },
                    { "flat control      ", Spacing::uniform,  true  },
                };

                for (const auto& v : variants)
                {
                    const auto mixture = buildMixture (f, density, v.spacing,
                                                       v.flat ? nullptr : &curve,
                                                       centreRt60);

                    const auto r = syntheticEnsemble (mixture, band, centreRt60);

                    std::string row = "    " + pad (number (f, 0), 9)
                                      + pad (v.label, 17)
                                      + pad (std::to_string (mixture.modes), 8);

                    if (! r.valid)
                    {
                        info (row + "INVALID - " + r.reason);
                        continue;
                    }

                    info (row + pad (number (r.t20, 3), 8)
                          + pad (number (r.t30, 3), 8)
                          + pad (number (r.fitted, 3), 8)
                          + pad (number (r.r2, 5), 9)
                          + pad (number (r.mismatch() * 100.0, 2) + " %", 12)
                          + number (t0Mismatch[b], 2) + " %");

                    if (! v.flat && v.spacing == Spacing::uniform)
                        syntheticUniform[b] = r.mismatch() * 100.0;
                }

                info ("");
            }

            // ---- excess curvature and the verdict -------------------------

            info ("    band      T0        synthetic   excess curvature");

            bool rises = true;
            double previous = -1.0;

            for (size_t b = 0; b < 4; ++b)
            {
                const double excess = t0Mismatch[b] - syntheticUniform[b];

                info ("    " + pad (number (kProbes[b], 0), 10)
                      + pad (number (t0Mismatch[b], 2) + " %", 10)
                      + pad (number (syntheticUniform[b], 2) + " %", 12)
                      + number (excess, 2) + " pp");

                if (previous >= 0.0 && syntheticUniform[b] < previous - 0.5)
                    rises = false;

                previous = syntheticUniform[b];
            }

            const double gap = std::abs (syntheticUniform[3] - t0Mismatch[3]);

            info ("");
            info ("  synthetic rises with frequency:   " + std::string (rises ? "yes" : "no"));
            info ("  12 kHz synthetic                  " + number (syntheticUniform[3], 2) + " %");
            info ("  12 kHz T0                         " + number (t0Mismatch[3], 2) + " %");
            info ("  distance                          " + number (gap, 2) + " pp");
            info ("");

            if (rises && gap <= kCriticalWindowPp)
            {
                info ("  CLASSIFICATION: EXPECTED SPECTRAL-DECAY MIXTURE");
                info ("  PROBLEM B = CLOSED, as NO DEMONSTRATED EXCESS TAIL-CURVATURE");
                info ("  DEFECT. Perfect exponential modes given the tank's own");
                info ("  measured decay law produce the tank's own curvature.");
            }
            else if (syntheticUniform[3] <= kResidualLimit && t0Mismatch[3] >= 5.0)
            {
                info ("  CLASSIFICATION: REAL ENGINE RESIDUAL");
                info ("  The measured decay law does not account for the curvature.");
            }
            else
            {
                info ("  CLASSIFICATION: STILL UNRESOLVED");
            }

            check (true, "classification recorded against the pre-registered rule");
        }
    }
}

} // namespace avtest
