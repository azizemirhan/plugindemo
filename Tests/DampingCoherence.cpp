#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnDelayLine.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulatedFeedbackMatrix.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — is per-line decay coherence a material driver of the T20/T30
    curvature? Controlled, TEST ONLY.

    WHY THIS IS BEING ASKED AGAIN
    -----------------------------
    The topology study concluded PER-LINE FITTING NOT JUSTIFIED because cutting
    the per-line K spread from 25.28 % to 0.99 % moved the aggregate mismatch
    by 0.4 points. That measurement used the one-pole analysis band which was
    later shown to be the source of its own result, so the conclusion rests on
    evidence that no longer stands.

    WHY RAW T0 vs T2 IS NOT ENOUGH
    ------------------------------
    T2 changes two things at once: it improves per-line coherence AND, because
    its corner is recalibrated, it damps harder at the same control setting. A
    difference between them could belong to either. So a third candidate is
    built whose aggregate decay law matches T0 as closely as the solve allows,
    while keeping T2's coherence. If the mismatch then follows coherence, it is
    a driver; if it follows the decay law, it is not.

    THE SOLVE IS BLIND
    ------------------
    T2-MATCHED's High Damp amount and corner are solved against T0's ensemble
    FITTED RT60 at 6 kHz and 12 kHz and nothing else. The solver never reads
    T20, T30, their mismatch or R^2. 1 kHz and 3 kHz are held out of the solve
    entirely and used as out-of-sample validation.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kLines = 8;
constexpr int    kEnsemble = 32;
constexpr double kGateBandOctaves = 1.0;

// PRE-REGISTERED decision thresholds, fixed before the run.
constexpr double kMaterialDropPp    = 1.5;   // >= this is a material driver
constexpr double kImmaterialDropPp  = 0.5;   // <= this is not a driver
constexpr double kSolveTolerance    = 0.005; // 0.5 % at the two solve points

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
//  Candidate topology, as defined in the cascade study
// ---------------------------------------------------------------------------

enum class Pole { production, calibrated };

/*  Derived in the cascade study: solving |H_section(w_c)|^2 = g gives a
    quadratic in beta = 1 - a whose discriminant collapses to 8 g k. Repeated
    here rather than shared, because it is candidate maths and does not belong
    in the production header. */
[[nodiscard]] double calibratedPoleHigh (double g, double cornerHz, double sampleRate) noexcept
{
    const double f = std::clamp (cornerHz, damping::minCutoffHz, 0.49 * sampleRate);
    const double k = 1.0 - std::cos (2.0 * damping::pi * f / sampleRate);

    return 1.0 / (1.0 + std::sqrt (2.0 * std::clamp (g, 1.0e-12, 1.0) * k));
}

[[nodiscard]] double calibratedPoleLow (double g, double cornerHz, double sampleRate) noexcept
{
    const double gc = std::clamp (g, 1.0e-12, 1.0);
    const double f = std::clamp (cornerHz, damping::minCutoffHz, 0.49 * sampleRate);
    const double k = 1.0 - std::cos (2.0 * damping::pi * f / sampleRate);

    return gc / (gc + std::sqrt (2.0 * gc * k));
}

struct Candidate
{
    int sections = 1;
    Pole pole = Pole::production;
    const char* label = "T0";
};

struct LineSpec
{
    damping::ShelfSpec section {};
    int sections = 1;
    double normalisation = 1.0;
};

/** Certified upper bound on max|H_pair|, from the monotonicity proof alone. */
[[nodiscard]] double certifiedMaximum (const damping::ShelfSpec& spec, double sampleRate,
                                       int intervals = 1024) noexcept
{
    if (spec.isTransparent())
        return 1.0;

    const auto lowMag = [&spec] (double u)
    {
        return std::sqrt (std::max (0.0, damping::lowShelfMagnitudeSquared (spec, u)));
    };

    const auto highMag = [&spec] (double u)
    {
        return std::sqrt (std::max (0.0, damping::highShelfMagnitudeSquared (spec, u)));
    };

    const double lowest = std::log (1.0), highest = std::log (0.4999 * sampleRate);

    double previousHigh = highMag (1.0);
    double bound = 0.0;

    for (int k = 0; k <= intervals; ++k)
    {
        const double u = (k == intervals)
                         ? -1.0
                         : std::cos (2.0 * damping::pi
                                     * std::exp (lowest + (highest - lowest) * k / intervals)
                                     / sampleRate);

        const double low = lowMag (u), high = highMag (u);

        bound = std::max (bound, low * previousHigh);
        previousHigh = high;
    }

    return std::min (bound, 1.0);
}

[[nodiscard]] LineSpec buildLine (const Candidate& candidate, double delaySeconds,
                                  double rt60, double lowRatio, double highRatio,
                                  double lowHz, double highHz)
{
    LineSpec result;
    result.sections = candidate.sections;

    const double totalLow  = damping::shelfGainFor (delaySeconds, rt60, lowRatio);
    const double totalHigh = damping::shelfGainFor (delaySeconds, rt60, highRatio);

    const double inverseN = 1.0 / static_cast<double> (candidate.sections);

    const double sectionLow  = std::pow (totalLow,  inverseN);
    const double sectionHigh = std::pow (totalHigh, inverseN);

    result.section.gainLow  = sectionLow;
    result.section.gainHigh = sectionHigh;

    if (candidate.pole == Pole::production)
    {
        result.section.coeffLow  = damping::onePoleCoefficient (lowHz,  kSampleRate);
        result.section.coeffHigh = damping::onePoleCoefficient (highHz, kSampleRate);
    }
    else
    {
        result.section.coeffLow  = calibratedPoleLow  (sectionLow,  lowHz,  kSampleRate);
        result.section.coeffHigh = calibratedPoleHigh (sectionHigh, highHz, kSampleRate);
    }

    const auto closed = damping::compositeMaximum (result.section);
    const double certified = certifiedMaximum (result.section, kSampleRate);

    const double pairMaximum = std::max (closed.upper, certified);

    result.normalisation = result.section.isTransparent()
                           ? 1.0
                           : 1.0 / std::pow (pairMaximum,
                                             static_cast<double> (candidate.sections));

    return result;
}

[[nodiscard]] double normalisedMagnitude (const LineSpec& line, double freqHz) noexcept
{
    return std::pow (damping::magnitudeAt (line.section, freqHz, kSampleRate),
                     static_cast<double> (line.sections)) * line.normalisation;
}

class CascadeDamper
{
public:
    void configure (const LineSpec& spec) noexcept
    {
        sections = spec.sections;
        norm = spec.normalisation;
        cLow  = 1.0 - spec.section.gainLow;
        cHigh = 1.0 - spec.section.gainHigh;
        bLow  = 1.0 - spec.section.coeffLow;
        bHigh = 1.0 - spec.section.coeffHigh;
        transparent = spec.section.isTransparent();

        reset();
    }

    void reset() noexcept { zLow.fill (0.0); zHigh.fill (0.0); }

    [[nodiscard]] double process (double x) noexcept
    {
        if (transparent)
            return x;

        for (int n = 0; n < sections; ++n)
        {
            auto& z = zLow[static_cast<size_t> (n)];
            z += bLow * (x - z);
            x -= cLow * z;
        }

        for (int n = 0; n < sections; ++n)
        {
            auto& z = zHigh[static_cast<size_t> (n)];
            z += bHigh * (x - z);
            x -= cHigh * (x - z);
        }

        return x * norm;
    }

private:
    std::array<double, 4> zLow {}, zHigh {};
    double cLow = 0.0, cHigh = 0.0, bLow = 0.0, bHigh = 0.0, norm = 1.0;
    int sections = 1;
    bool transparent = true;
};

/** The same tank structure for every candidate; only the damper differs. */
class Tank
{
public:
    Tank()
    {
        const int capacity = static_cast<int> (std::ceil (200.0 * fdn::maxSizeScale
                                                          * 0.001 * kSampleRate)) + 2;
        matrix.prepare (kSampleRate);

        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[static_cast<size_t> (i)];
            line.prepare (capacity);

            const int samples = static_cast<int> (std::lround (
                fdn::delaysMs[static_cast<size_t> (i)] * 0.001 * kSampleRate));

            (void) line.setDelaySamples (samples);

            delaySeconds[static_cast<size_t> (i)] = static_cast<double> (samples) / kSampleRate;

            gains[static_cast<size_t> (i)] = std::min (
                fdn::maxFeedbackGain,
                fdn::feedbackGainForRt60 (delaySeconds[static_cast<size_t> (i)], kDecayTarget));
        }
    }

    void configure (const Candidate& candidate, double lowControl, double highControl,
                    double lowHz, double highHz)
    {
        const double lowRatio  = mapping::lowDampRatioFor (lowControl);
        const double highRatio = mapping::highDampRatioFor (highControl);

        for (int i = 0; i < kLines; ++i)
        {
            specs[static_cast<size_t> (i)] =
                buildLine (candidate, delaySeconds[static_cast<size_t> (i)], kDecayTarget,
                           lowRatio, highRatio, lowHz, highHz);

            dampers[static_cast<size_t> (i)].configure (specs[static_cast<size_t> (i)]);
        }
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive, int numSamples)
    {
        for (auto& l : lines) l.reset();
        for (auto& d : dampers) d.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double v[kLines] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (n < static_cast<int> (drive.size()))
                             ? drive[static_cast<size_t> (n)] : 0.0;

            double sum = 0.0;

            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<size_t> (i);
                const double damped = dampers[idx].process (lines[idx].read());

                v[i] = damped * gains[idx];
                sum += v[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;
            }

            matrix.process (v);

            for (int i = 0; i < kLines; ++i)
                lines[static_cast<size_t> (i)].write (
                    v[i] + x * fdn::injectionLeft[i] * fdn::signVectorNorm);

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }

    /** Per-line K spread at one frequency, as a percentage of RT60. */
    [[nodiscard]] double kSpread (double freqHz) const
    {
        double lowest = 1.0e9, highest = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            const double magnitude = normalisedMagnitude (specs[static_cast<size_t> (i)], freqHz);
            const double d = delaySeconds[static_cast<size_t> (i)];
            const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;
            const double rt = 1.0 / (1.0 / kDecayTarget + k / 3.0);

            lowest = std::min (lowest, rt);
            highest = std::max (highest, rt);
        }

        return (highest - lowest) / std::max (highest, 1.0e-12) * 100.0;
    }

private:
    std::array<FdnDelayLine, kLines> lines;
    std::array<CascadeDamper, kLines> dampers;
    std::array<LineSpec, kLines> specs {};
    std::array<double, kLines> gains {}, delaySeconds {};

    ModulatedFeedbackMatrix<kLines> matrix;
};

// ---------------------------------------------------------------------------
//  Excitation bank — identical to the validated ensemble
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

[[nodiscard]] decay::Result ensemble (Tank& tank, const decay::Band& band,
                                      double rt60Hint, int members = kEnsemble)
{
    const int length = static_cast<int> (kSampleRate * std::max (1.5, rt60Hint * 2.2));

    std::vector<double> accumulator (static_cast<size_t> (length), 0.0);

    int counted = 0;

    for (int k = 0; k < members; ++k)
    {
        auto ir = tank.render (makeExcitation (k), length);
        band.apply (ir);

        const auto edc = decay::normalisedEdc (ir);

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

void runDampingCoherenceTests()
{
    const Candidate t0 { 1, Pole::production,  "T0 production" };
    const Candidate t2 { 3, Pole::calibrated,  "T2 three sections" };

    std::vector<decay::Band> bands;

    for (double f : kProbes)
        bands.push_back (decay::makeBand (f, kGateBandOctaves, kSampleRate));

    std::array<double, 4> t0Rt60 {};

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 coherence - raw T0 and raw T2, corrected analyser");

    {
        info ("  Low Damp 0 %, High Damp 100 %, 200 Hz / 6 kHz, Decay 2.5 s.");
        info ("  32-realisation ensemble, normalised linear EDC average, fixed-Q band.");
        info ("  Descriptive only: T2 changes coherence AND damping strength.");
        info ("");
        info ("    candidate       band     RT60    EDT     T20     T30     R^2      |T20-T30|  K spread");

        for (const auto& candidate : { t0, t2 })
        {
            Tank tank;
            tank.configure (candidate, 0.0, 1.0, 200.0, 6000.0);

            for (size_t b = 0; b < 4; ++b)
            {
                const auto r = ensemble (tank, bands[b], 2.5);

                if (! r.valid)
                {
                    info ("    " + pad (candidate.label, 16) + pad (number (kProbes[b], 0), 9)
                          + "INVALID - " + r.reason);
                    continue;
                }

                if (candidate.sections == 1)
                    t0Rt60[b] = r.fitted;

                info ("    " + pad (candidate.label, 16)
                      + pad (number (kProbes[b], 0), 9)
                      + pad (number (r.fitted, 3), 8)
                      + pad (number (r.edt, 3), 8)
                      + pad (number (r.t20, 3), 8)
                      + pad (number (r.t30, 3), 8)
                      + pad (number (r.r2, 5), 9)
                      + pad (number (r.mismatch() * 100.0, 2) + " %", 11)
                      + number (tank.kSpread (kProbes[b]), 2) + " %");
            }

            info ("");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 coherence - solving T2-MATCHED against T0's RT60 only");

    double solvedAmount = 1.0, solvedCorner = 6000.0;
    bool solveValid = false;

    {
        /*
            Two unknowns, two targets. The objective is T0's ensemble fitted
            RT60 at 6 kHz and 12 kHz — nothing else is ever evaluated, so the
            experiment cannot be circular.

            Both maps are monotone: more High Damp shortens 12 kHz, a higher
            corner lengthens 6 kHz. So each inner solve is a bisection, and the
            two alternate until they stop moving. A small ensemble is used
            inside the loop because RT60 converges by N=4 (measured: 0.02-0.46 %
            from N=16 to N=32); the final answer is then verified at N=32.
        */
        const auto rt60At = [&] (double amount, double corner, size_t bandIndex, int members)
        {
            Tank tank;
            tank.configure (t2, 0.0, amount, 200.0, corner);

            const auto r = ensemble (tank, bands[bandIndex], 2.5, members);

            return r.valid ? r.fitted : 0.0;
        };

        info ("  Targets, from T0's own ensemble:");
        info ("     6 kHz  " + number (t0Rt60[2], 4) + " s");
        info ("    12 kHz  " + number (t0Rt60[3], 4) + " s");
        info ("");
        info ("  The solver reads fitted RT60 and nothing else. T20, T30, their");
        info ("  mismatch and R^2 are not evaluated anywhere in this loop.");
        info ("");
        info ("    pass   High Damp    corner Hz    6 kHz err   12 kHz err");

        for (int pass = 0; pass < 18; ++pass)
        {
            // Amount against 12 kHz: monotone decreasing.
            {
                double lo = 0.0, hi = 1.0;

                for (int i = 0; i < 12; ++i)
                {
                    const double mid = 0.5 * (lo + hi);

                    if (rt60At (mid, solvedCorner, 3, 3) > t0Rt60[3])
                        lo = mid;
                    else
                        hi = mid;
                }

                solvedAmount = 0.5 * (lo + hi);
            }

            // Corner against 6 kHz: monotone increasing.
            {
                double lo = 1000.0, hi = 20000.0;

                for (int i = 0; i < 12; ++i)
                {
                    const double mid = std::sqrt (lo * hi);

                    if (rt60At (solvedAmount, mid, 2, 3) < t0Rt60[2])
                        lo = mid;
                    else
                        hi = mid;
                }

                solvedCorner = std::sqrt (lo * hi);
            }

            if (pass % 4 != 3 && pass != 17)
                continue;

            const double e6  = rt60At (solvedAmount, solvedCorner, 2, 3) / t0Rt60[2] - 1.0;
            const double e12 = rt60At (solvedAmount, solvedCorner, 3, 3) / t0Rt60[3] - 1.0;

            info ("    " + pad (std::to_string (pass), 7)
                  + pad (number (solvedAmount * 100.0, 3) + " %", 13)
                  + pad (number (solvedCorner, 1), 13)
                  + pad (number (e6 * 100.0, 3) + " %", 12)
                  + number (e12 * 100.0, 3) + " %");
        }

        info ("");
        info ("  solved High Damp amount   " + number (solvedAmount * 100.0, 4) + " %");
        info ("  solved High Damp Freq     " + number (solvedCorner, 2) + " Hz");
        solveValid = true;
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 coherence - T2-MATCHED response fit, verified at N=32");

    std::array<double, 4> matchedRt60 {};
    bool matchAcceptable = false;

    {
        Tank tank;
        tank.configure (t2, 0.0, solvedAmount, 200.0, solvedCorner);

        info ("    band      T0 RT60    T2-MATCHED   error      role");

        double worstSolve = 0.0, worstHoldout = 0.0;

        for (size_t b = 0; b < 4; ++b)
        {
            const auto r = ensemble (tank, bands[b], 2.5);

            if (! r.valid)
            {
                info ("    " + pad (number (kProbes[b], 0), 10) + "INVALID - " + r.reason);
                continue;
            }

            matchedRt60[b] = r.fitted;

            const double error = r.fitted / t0Rt60[b] - 1.0;
            const bool isSolvePoint = (b >= 2);

            info ("    " + pad (number (kProbes[b], 0), 10)
                  + pad (number (t0Rt60[b], 4), 11)
                  + pad (number (r.fitted, 4), 13)
                  + pad (number (error * 100.0, 3) + " %", 11)
                  + (isSolvePoint ? "solve point" : "held out"));

            if (isSolvePoint)
                worstSolve = std::max (worstSolve, std::abs (error));
            else
                worstHoldout = std::max (worstHoldout, std::abs (error));
        }

        const double error1k = std::abs (matchedRt60[0] / t0Rt60[0] - 1.0);
        const double error3k = std::abs (matchedRt60[1] / t0Rt60[1] - 1.0);

        info ("");
        info ("  worst error at the two solve points   " + number (worstSolve * 100.0, 3)
              + " %   (target <= " + number (kSolveTolerance * 100.0, 1) + " %)");
        info ("  out-of-sample 1 kHz                   " + number (error1k * 100.0, 3)
              + " %   (preferred <= 2 %)");
        info ("  out-of-sample 3 kHz                   " + number (error3k * 100.0, 3)
              + " %   (preferred <= 3 %)");

        matchAcceptable = solveValid
                          && worstSolve <= kSolveTolerance
                          && error1k <= 0.02
                          && error3k <= 0.03;

        info ("");

        if (matchAcceptable)
        {
            info ("  The matched control reproduces T0's decay law. The comparison");
            info ("  below therefore isolates coherence.");
        }
        else
        {
            info ("  MATCHED CONTROL INSUFFICIENT - no causal conclusion may be drawn");
            info ("  from the comparison below.");
        }

        (void) worstHoldout;
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 coherence - K spread across the three candidates");

    {
        Tank a, b, c;
        a.configure (t0, 0.0, 1.0, 200.0, 6000.0);
        b.configure (t2, 0.0, 1.0, 200.0, 6000.0);
        c.configure (t2, 0.0, solvedAmount, 200.0, solvedCorner);

        info ("    band      T0        raw T2    T2-MATCHED");

        double worstMatched = 0.0, worstT0 = 0.0;

        for (double f : kProbes)
        {
            const double sa = a.kSpread (f), sb = b.kSpread (f), sc = c.kSpread (f);

            info ("    " + pad (number (f, 0), 10)
                  + pad (number (sa, 2) + " %", 10)
                  + pad (number (sb, 2) + " %", 10)
                  + number (sc, 2) + " %");

            worstT0 = std::max (worstT0, sa);
            worstMatched = std::max (worstMatched, sc);
        }

        info ("");
        info ("  worst T0 spread          " + number (worstT0, 2) + " %");
        info ("  worst T2-MATCHED spread  " + number (worstMatched, 2) + " %");
        info ("");
        info ("  If matching brought T2's coherence back near T0's, the experiment");
        info ("  cannot isolate coherence and the result is unresolved.");

        check (worstMatched < worstT0 * 0.5,
               "T2-MATCHED still has substantially better coherence than T0");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 coherence - T0 against T2-MATCHED, and the verdict");

    {
        Tank tankT0, tankMatched;
        tankT0.configure (t0, 0.0, 1.0, 200.0, 6000.0);
        tankMatched.configure (t2, 0.0, solvedAmount, 200.0, solvedCorner);

        info ("    band      candidate      RT60    T20     T30     R^2      |T20-T30|  K spread");

        double criticalT0 = 0.0, criticalMatched = 0.0;

        for (size_t b = 0; b < 4; ++b)
        {
            const auto ra = ensemble (tankT0, bands[b], 2.5);
            const auto rb = ensemble (tankMatched, bands[b], 2.5);

            if (ra.valid)
                info ("    " + pad (number (kProbes[b], 0), 10) + pad ("T0", 15)
                      + pad (number (ra.fitted, 3), 8)
                      + pad (number (ra.t20, 3), 8)
                      + pad (number (ra.t30, 3), 8)
                      + pad (number (ra.r2, 5), 9)
                      + pad (number (ra.mismatch() * 100.0, 2) + " %", 11)
                      + number (tankT0.kSpread (kProbes[b]), 2) + " %");

            if (rb.valid)
                info ("    " + pad (number (kProbes[b], 0), 10) + pad ("T2-MATCHED", 15)
                      + pad (number (rb.fitted, 3), 8)
                      + pad (number (rb.t20, 3), 8)
                      + pad (number (rb.t30, 3), 8)
                      + pad (number (rb.r2, 5), 9)
                      + pad (number (rb.mismatch() * 100.0, 2) + " %", 11)
                      + number (tankMatched.kSpread (kProbes[b]), 2) + " %");

            if (b == 3)
            {
                criticalT0 = ra.valid ? ra.mismatch() * 100.0 : 0.0;
                criticalMatched = rb.valid ? rb.mismatch() * 100.0 : 0.0;
            }

            info ("");
        }

        const double drop = criticalT0 - criticalMatched;

        info ("  critical 12 kHz: T0 " + number (criticalT0, 2)
              + " %  ->  T2-MATCHED " + number (criticalMatched, 2)
              + " %   change " + number (drop, 2) + " pp");
        info ("");
        info ("  Pre-registered rule: >= " + number (kMaterialDropPp, 1)
              + " pp means coherence is a material driver; <= "
              + number (kImmaterialDropPp, 1) + " pp means it is not.");
        info ("");

        if (! matchAcceptable)
        {
            info ("  CLASSIFICATION: UNRESOLVED");
            info ("  The matched-response control did not reproduce T0's decay law");
            info ("  closely enough for the comparison to isolate coherence.");
        }
        else if (drop >= kMaterialDropPp)
        {
            info ("  CLASSIFICATION: COHERENCE MATERIAL DRIVER");
            info ("  Holding the decay law fixed and improving only per-line");
            info ("  coherence removed a material part of the curvature.");
        }
        else if (drop <= kImmaterialDropPp)
        {
            info ("  CLASSIFICATION: COHERENCE NOT MATERIAL DRIVER");
            info ("  With the decay law held fixed, a 25x improvement in per-line");
            info ("  coherence left the curvature essentially where it was.");
        }
        else
        {
            info ("  CLASSIFICATION: UNRESOLVED");
            info ("  The change falls between the two pre-registered brackets.");
        }

        check (true, "classification recorded against the pre-registered rule");
    }
}

} // namespace avtest
