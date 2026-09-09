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
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — TEST-ONLY topology study: fixed-order cascaded first-order shelves.

    Nothing in Source/ is changed by this file. The production topology, mapping,
    parameter IDs, ranges and defaults are all untouched; this is a measurement
    of what a different section count would do, run in a test-local FDN that
    mirrors FdnCore's structure with the damping stage swapped out.

    THE CANDIDATES
    --------------
        T0  one section per shelf   — the current production topology
        T1  two sections per shelf
        T2  three sections per shelf

    Every line uses the SAME section count. The per-line difference stays where
    it already is: in the total plateau gain G_i, derived from that line's loop
    delay. The section gain is g = G_i^(1/N), so the total asymptotic plateau
    gain is exactly G_i whatever N is.

    T0 is measured twice — once as it ships, and once with the new corner
    calibration below — because changing the corner definition and changing the
    section count are two separate variables and folding them together would
    make the comparison meaningless.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr int    kLines      = 8;

// PRE-REGISTERED gates, unchanged from the previous checkpoint.
constexpr double kGateMinimumR2         = 0.995;
constexpr double kGateMaxT20T30Mismatch = 0.05;
constexpr double kGateMidDeviation      = 2.0;    // per cent, 500 Hz - 2 kHz

// Precision of this study's own certified bound. Not a gate on the DSP.
constexpr double kStudyBoundTolerance   = 1.0 + 1.0e-7;

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

// ===========================================================================
//  Corner calibration, derived
// ===========================================================================
/*
    The requested Damp Freq must keep a meaningful meaning whatever N is, so it
    is defined on the SECTION and not left to whatever pole a formula from the
    single-section case happens to produce:

        at the corner, a section is halfway through its own plateau
        attenuation in dB, i.e.  |H_section(w_c)| = sqrt(g)

    With g = G^(1/N) that makes |H_total(w_c)| = G^(1/2) for every N — the
    definition the study was asked to hold.

    Solving |H_section(w_c)|^2 = g in closed form gives a quadratic in
    beta = 1 - a whose discriminant collapses to 8*g*k with k = 1 - cos(w_c).
    Both shelves reduce to one line each:

        s = sqrt(2 g k)

        high shelf:   beta = s / (1 + s)     ->  a = 1 / (1 + s)
        low  shelf:   beta = s / (g + s)     ->  a = g / (g + s)

    At g = 1 both give a = 1/(1 + 2|sin(w_c/2)|), which agrees with the
    impulse-invariant exp(-w_c) to first order — the undamped limit is
    continuous. No fitted constants, no offsets.
*/

[[nodiscard]] double calibratedPoleHigh (double sectionGain, double cornerHz,
                                         double sampleRate) noexcept
{
    const double f = std::clamp (cornerHz, damping::minCutoffHz, 0.49 * sampleRate);
    const double k = 1.0 - std::cos (2.0 * damping::pi * f / sampleRate);
    const double s = std::sqrt (2.0 * std::clamp (sectionGain, 1.0e-12, 1.0) * k);

    return 1.0 / (1.0 + s);
}

[[nodiscard]] double calibratedPoleLow (double sectionGain, double cornerHz,
                                        double sampleRate) noexcept
{
    const double g = std::clamp (sectionGain, 1.0e-12, 1.0);
    const double f = std::clamp (cornerHz, damping::minCutoffHz, 0.49 * sampleRate);
    const double k = 1.0 - std::cos (2.0 * damping::pi * f / sampleRate);
    const double s = std::sqrt (2.0 * g * k);

    return g / (g + s);
}

// ===========================================================================
//  Candidate topology
// ===========================================================================

enum class Corner { production, calibrated };

struct Topology
{
    int sections = 1;
    Corner corner = Corner::production;
    const char* label = "T0";
};

/** One line's cascade, as coefficients. */
struct LineSpec
{
    damping::ShelfSpec section {};   ///< the per-section shelf pair
    int    sections = 1;
    double normalisation = 1.0;      ///< 1 / max|H_cascade|
    double maximumRaw = 1.0;         ///< max|H_cascade| before normalising
    double closedFormPair = 1.0;     ///< what compositeMaximum returned
    double certifiedPair = 1.0;      ///< the interval bound, for comparison
};

/**
    A certified upper bound on max|H_pair|, independent of the closed form.

    |H_lo| never falls with frequency and |H_hi| never rises — proved in
    DampingTuning.h — so on any interval the product of the two favourable
    endpoints bounds the true maximum whatever shape it has inside. This is the
    method Option A used before the closed form replaced it; it is kept here
    precisely because it rests on the monotonicity proof alone and therefore
    holds for any pole family, calibrated or not.
*/
[[nodiscard]] double certifiedPairMaximum (const damping::ShelfSpec& spec,
                                           double sampleRate = kSampleRate,
                                           int intervals = 4096) noexcept
{
    if (spec.isTransparent())
        return 1.0;

    const auto lowMagnitude = [&spec] (double u)
    {
        return std::sqrt (std::max (0.0, damping::lowShelfMagnitudeSquared (spec, u)));
    };

    const auto highMagnitude = [&spec] (double u)
    {
        return std::sqrt (std::max (0.0, damping::highShelfMagnitudeSquared (spec, u)));
    };

    const double lowest  = std::log (1.0);
    const double highest = std::log (0.4999 * sampleRate);

    double previousHigh = highMagnitude (1.0);   // DC
    double bound = 0.0;

    for (int k = 0; k <= intervals; ++k)
    {
        const double u = (k == intervals)
                         ? -1.0
                         : std::cos (2.0 * damping::pi
                                     * std::exp (lowest + (highest - lowest) * k / intervals)
                                     / sampleRate);

        const double low  = lowMagnitude (u);
        const double high = highMagnitude (u);

        bound = std::max (bound, low * previousHigh);
        previousHigh = high;
    }

    return std::min (bound, 1.0);
}

[[nodiscard]] LineSpec buildLine (const Topology& topology,
                                  double delaySeconds, double rt60,
                                  double lowRatio, double highRatio,
                                  double lowHz, double highHz,
                                  double sampleRate)
{
    LineSpec result;
    result.sections = topology.sections;

    const double totalLow  = damping::shelfGainFor (delaySeconds, rt60, lowRatio);
    const double totalHigh = damping::shelfGainFor (delaySeconds, rt60, highRatio);

    const double inverseN = 1.0 / static_cast<double> (topology.sections);

    const double sectionLow  = std::pow (totalLow,  inverseN);
    const double sectionHigh = std::pow (totalHigh, inverseN);

    result.section.gainLow  = sectionLow;
    result.section.gainHigh = sectionHigh;

    if (topology.corner == Corner::production)
    {
        result.section.coeffLow  = damping::onePoleCoefficient (lowHz,  sampleRate);
        result.section.coeffHigh = damping::onePoleCoefficient (highHz, sampleRate);
    }
    else
    {
        result.section.coeffLow  = calibratedPoleLow  (sectionLow,  lowHz,  sampleRate);
        result.section.coeffHigh = calibratedPoleHigh (sectionHigh, highHz, sampleRate);
    }

    /*
        |H_cascade(w)| = |H_pair(w)|^N, and x^N is monotone, so the cascade
        peaks exactly where the single pair does. Only the value is raised to N.

        The maximum is taken as the larger of the production closed form and a
        certified interval bound. The closed form is exact for the pole family
        production uses and was validated against brute force there, but it is
        NOT validated for the calibrated pole family introduced in this study,
        and measurement showed it under-stating by up to 8e-6 on it. Under-
        stating is the one direction that breaks the subtractive invariant, so
        the study certifies its own bound rather than assuming the old result
        carries over. See the maximum-finder suite below, which measures the
        gap rather than hiding it.
    */
    const auto closedForm = damping::compositeMaximum (result.section);
    const double certified = certifiedPairMaximum (result.section, sampleRate, 3072);

    const double pairMaximum = std::max (closedForm.upper, certified);

    result.closedFormPair = closedForm.upper;
    result.certifiedPair = certified;

    result.maximumRaw = std::pow (closedForm.lower, static_cast<double> (topology.sections));

    const double upper = std::pow (pairMaximum, static_cast<double> (topology.sections));

    result.normalisation = (result.section.isTransparent()) ? 1.0 : 1.0 / upper;

    return result;
}

/** |Hn(w)| for a line: the cascade magnitude times its normalisation. */
[[nodiscard]] double normalisedMagnitude (const LineSpec& line, double freqHz,
                                          double sampleRate) noexcept
{
    const double pair = damping::magnitudeAt (line.section, freqHz, sampleRate);

    return std::pow (pair, static_cast<double> (line.sections)) * line.normalisation;
}

/** The runtime filter for one line: N low-shelf then N high-shelf sections. */
class CascadeDamper
{
public:
    void configure (const LineSpec& spec) noexcept
    {
        line = spec;

        cLow  = 1.0 - spec.section.gainLow;
        cHigh = 1.0 - spec.section.gainHigh;
        bLow  = 1.0 - spec.section.coeffLow;
        bHigh = 1.0 - spec.section.coeffHigh;

        transparent = spec.section.isTransparent();

        reset();
    }

    void reset() noexcept
    {
        zLow.fill (0.0);
        zHigh.fill (0.0);
    }

    [[nodiscard]] double process (double x) noexcept
    {
        if (transparent)
            return x;

        for (int n = 0; n < line.sections; ++n)
        {
            auto& z = zLow[static_cast<size_t> (n)];
            z += bLow * (x - z);
            x -= cLow * z;
        }

        for (int n = 0; n < line.sections; ++n)
        {
            auto& z = zHigh[static_cast<size_t> (n)];
            z += bHigh * (x - z);
            x -= cHigh * (x - z);
        }

        return x * line.normalisation;
    }

private:
    LineSpec line;

    std::array<double, 4> zLow {};
    std::array<double, 4> zHigh {};

    double cLow = 0.0, cHigh = 0.0, bLow = 0.0, bHigh = 0.0;
    bool transparent = true;
};

// ===========================================================================
//  Test-local FDN
// ===========================================================================
/*
    A stripped copy of FdnCore's structure — same delay set, same decay gain
    formula, same orthogonal matrix, same damping position in the loop — with
    the damping stage swapped for the candidate cascade. Modulation is left at
    zero, which is the state every earlier measurement in this phase was taken
    in, so the numbers are comparable.
*/
class StudyTank
{
public:
    StudyTank (double rt60Seconds, double sampleRate = kSampleRate)
        : rt60 (rt60Seconds), fs (sampleRate)
    {
        const int capacity = static_cast<int> (std::ceil (200.0 * fdn::maxSizeScale
                                                          * 0.001 * fs)) + 2;

        matrix.prepare (fs);

        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[static_cast<size_t> (i)];
            line.prepare (capacity);

            const int samples = static_cast<int> (std::lround (
                fdn::delaysMs[static_cast<size_t> (i)] * 0.001 * fs));

            (void) line.setDelaySamples (samples);

            delaySeconds[static_cast<size_t> (i)] = static_cast<double> (samples) / fs;

            gains[static_cast<size_t> (i)] =
                std::min (fdn::maxFeedbackGain,
                          fdn::feedbackGainForRt60 (delaySeconds[static_cast<size_t> (i)], rt60));
        }

        reset();
    }

    void configureDamping (const Topology& topology,
                           double lowRatio, double highRatio,
                           double lowHz, double highHz)
    {
        for (int i = 0; i < kLines; ++i)
        {
            specs[static_cast<size_t> (i)] =
                buildLine (topology, delaySeconds[static_cast<size_t> (i)], rt60,
                           lowRatio, highRatio, lowHz, highHz, fs);

            dampers[static_cast<size_t> (i)].configure (specs[static_cast<size_t> (i)]);
        }
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& d : dampers) d.reset();
    }

    [[nodiscard]] std::vector<double> render (int numSamples, unsigned int seed)
    {
        reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double v[kLines] {};

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

            double sum = 0.0;

            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<size_t> (i);

                const double delayed = lines[idx].read();
                const double damped  = dampers[idx].process (delayed);

                v[i] = damped * gains[idx];
                sum += v[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;
            }

            matrix.process (v);

            for (int i = 0; i < kLines; ++i)
                lines[static_cast<size_t> (i)].write (
                    v[i] + drive * fdn::injectionLeft[i] * fdn::signVectorNorm);

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }

    [[nodiscard]] const LineSpec& spec (int i) const noexcept
    {
        return specs[static_cast<size_t> (i)];
    }

    [[nodiscard]] double gain (int i) const noexcept
    {
        return gains[static_cast<size_t> (i)];
    }

    [[nodiscard]] double delay (int i) const noexcept
    {
        return delaySeconds[static_cast<size_t> (i)];
    }

private:
    std::array<FdnDelayLine, kLines> lines;
    std::array<CascadeDamper, kLines> dampers;
    std::array<LineSpec, kLines> specs {};
    std::array<double, kLines> gains {};
    std::array<double, kLines> delaySeconds {};

    ModulatedFeedbackMatrix<kLines> matrix;

    double rt60 = 2.5;
    double fs = kSampleRate;
};

// ===========================================================================
//  Measurement (identical method to the previous checkpoint)
// ===========================================================================

void bandpass (std::vector<double>& x, double centreHz, double sampleRate,
               double octaves = 1.0)
{
    const double half = std::pow (2.0, 0.5 * octaves);
    const double lowHz  = std::max (10.0, centreHz / half);
    const double highHz = std::min (centreHz * half, 0.45 * sampleRate);

    const auto onePoleHigh = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;
        for (auto& s : v) { z += (1.0 - a) * (s - z); s -= z; }
    };

    const auto onePoleLow = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;
        for (auto& s : v) { z += (1.0 - a) * (s - z); s = z; }
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

struct DecayFit
{
    double edt = 0.0, t20 = 0.0, t30 = 0.0, fitted = 0.0, r2 = 0.0;
    bool valid = false;
};

[[nodiscard]] DecayFit analyseDecay (const std::vector<double>& ir, double sampleRate)
{
    const size_t n = ir.size();

    std::vector<double> energy (n + 1, 0.0);

    for (size_t i = n; i-- > 0;)
        energy[i] = energy[i + 1] + ir[i] * ir[i];

    DecayFit result;

    if (energy[0] <= 0.0)
        return result;

    std::vector<double> curve (n, 0.0);

    for (size_t i = 0; i < n; ++i)
        curve[i] = 10.0 * std::log10 (std::max (energy[i] / energy[0], 1.0e-300));

    const auto timeAt = [&] (double dB) -> double
    {
        for (size_t i = 0; i < n; ++i)
            if (curve[i] <= dB)
                return static_cast<double> (i) / sampleRate;

        return -1.0;
    };

    const double t5  = timeAt (-5.0);
    const double t10 = timeAt (-10.0);
    const double t25 = timeAt (-25.0);
    const double t35 = timeAt (-35.0);

    if (t5 < 0.0 || t10 <= 0.0 || t25 <= t5 || t35 <= t5)
        return result;

    result.edt = t10 * 6.0;
    result.t20 = (t25 - t5) * 3.0;
    result.t30 = (t35 - t5) * 2.0;

    const size_t first = static_cast<size_t> (t5 * sampleRate);
    const size_t last  = static_cast<size_t> (t35 * sampleRate);

    if (last <= first + 16)
        return result;

    const size_t stride = std::max<size_t> (1, (last - first) / 2000);

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0, count = 0.0;

    for (size_t i = first; i <= last; i += stride)
    {
        const double x = static_cast<double> (i) / sampleRate;
        const double y = curve[i];

        sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
        count += 1.0;
    }

    const double denominator = count * sxx - sx * sx;

    if (std::abs (denominator) < 1.0e-12)
        return result;

    const double slope = (count * sxy - sx * sy) / denominator;

    if (slope >= 0.0)
        return result;

    result.fitted = -60.0 / slope;

    const double numerator = count * sxy - sx * sy;

    result.r2 = (numerator * numerator) / (denominator * (count * syy - sy * sy));
    result.valid = true;

    return result;
}

[[nodiscard]] DecayFit measureBand (StudyTank& tank, double centreHz, double rt60Hint,
                                    int seedCount = 3, double octaves = 1.0)
{
    const int length = static_cast<int> (std::ceil (kSampleRate
                                                    * std::max (0.5, rt60Hint * 2.2)));

    const unsigned int seeds[] { 0u, 7u, 4242u };

    DecayFit sum;
    int used = 0;

    for (int s = 0; s < std::min (seedCount, 3); ++s)
    {
        auto ir = tank.render (length, seeds[s]);
        bandpass (ir, centreHz, kSampleRate, octaves);

        const auto fit = analyseDecay (ir, kSampleRate);

        if (! fit.valid)
            continue;

        sum.edt += fit.edt; sum.t20 += fit.t20; sum.t30 += fit.t30;
        sum.fitted += fit.fitted; sum.r2 += fit.r2;
        ++used;
    }

    if (used == 0)
        return sum;

    const double k = 1.0 / static_cast<double> (used);

    sum.edt *= k; sum.t20 *= k; sum.t30 *= k; sum.fitted *= k; sum.r2 *= k;
    sum.valid = true;

    return sum;
}

// ---------------------------------------------------------------------------

struct Verdict
{
    double midWorst = 0.0;
    double rt12k = 0.0;
    double worstMismatch = 0.0;
    double worstR2 = 1.0;
    double worstKSpread = 0.0;
    double maxNormalised = 0.0;
    long long aboveDecay = 0;
    double relativeCpu = 1.0;
    bool zeroDampIdentity = false;
    bool stable = false;
};

} // namespace

// ===========================================================================

void runDampingTopologyTests()
{
    const Topology candidates[]
    {
        { 1, Corner::production, "T0 production" },
        { 1, Corner::calibrated, "T0 recalibrated" },
        { 2, Corner::calibrated, "T1 two sections" },
        { 3, Corner::calibrated, "T2 three sections" },
    };

    constexpr int candidateCount = 4;

    Verdict verdicts[candidateCount];

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - corner calibration is exact");

    {
        info ("  Requirement: |H_section(f_c)| = sqrt(g), so |H_total(f_c)| = sqrt(G).");
        info ("  Derived:  s = sqrt(2 g k),  k = 1 - cos(w_c)");
        info ("            high shelf  a = 1 / (1 + s)");
        info ("            low  shelf  a = g / (g + s)");
        info ("");

        double worstError = 0.0;

        for (double corner : { 20.0, 200.0, 1000.0, 6000.0, 20000.0 })
        {
            for (double g : { 0.999, 0.9, 0.7, 0.5, 0.25, 0.05, 0.005 })
            {
                for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
                {
                    if (corner >= 0.49 * fs)
                        continue;

                    damping::ShelfSpec high;
                    high.gainLow = 1.0;
                    high.coeffLow = 0.0;
                    high.gainHigh = g;
                    high.coeffHigh = calibratedPoleHigh (g, corner, fs);

                    const double gotHigh = damping::magnitudeAt (high, corner, fs);

                    damping::ShelfSpec low;
                    low.gainHigh = 1.0;
                    low.coeffHigh = 0.0;
                    low.gainLow = g;
                    low.coeffLow = calibratedPoleLow (g, corner, fs);

                    const double gotLow = damping::magnitudeAt (low, corner, fs);

                    const double want = std::sqrt (g);

                    worstError = std::max ({ worstError,
                                             std::abs (gotHigh - want) / want,
                                             std::abs (gotLow  - want) / want });
                }
            }
        }

        info ("  worst relative error against sqrt(g), over 140 combinations: "
              + number (worstError * 100.0, 6) + " %");

        checkAtMost (worstError, 1.0e-9,
                     "the derived pole puts the corner exactly at half the shelf, in dB");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - closed-form maximum on the calibrated poles");

    {
        /*
            Option A's closed-form maximum was validated against brute force on
            the pole family production uses. The calibration in this study is a
            different family, so the old result does not carry over and is not
            assumed to. Measured here directly.
        */
        double worstUnderstatement = 0.0;
        std::string where;
        long long cases = 0;

        for (double corner : { 20.0, 200.0, 1000.0, 6000.0, 20000.0 })
            for (double g : { 0.9, 0.5, 0.25, 0.05, 0.005 })
                for (double gLow : { 1.0, 0.9, 0.5, 0.1 })
                {
                    damping::ShelfSpec spec;

                    spec.gainHigh  = g;
                    spec.coeffHigh = calibratedPoleHigh (g, corner, kSampleRate);
                    spec.gainLow   = gLow;
                    spec.coeffLow  = calibratedPoleLow (gLow, 200.0, kSampleRate);

                    const double closed = damping::compositeMaximum (spec).upper;
                    const double certified = certifiedPairMaximum (spec, kSampleRate, 16384);

                    const double gap = certified - closed;

                    if (gap > worstUnderstatement)
                    {
                        worstUnderstatement = gap;
                        where = "high g " + number (g, 3) + ", low g " + number (gLow, 3)
                                + ", corner " + number (corner, 0) + " Hz";
                    }

                    ++cases;
                }

        info ("  cases " + std::to_string (cases));
        info ("  worst amount the closed form falls short of a certified bound: "
              + number (worstUnderstatement, 12));

        if (worstUnderstatement > 1.0e-9)
            info ("  worst at: " + where);

        info ("");
        info ("  This does not affect shipped Option A: production uses the");
        info ("  exp(-w) pole family, where the closed form measured exactly zero");
        info ("  under-statement over 4007 brute-force comparisons. It does mean");
        info ("  the closed form must be revalidated, or the certified bound kept");
        info ("  alongside it, before any topology change that moves the poles.");

        // Reported, not asserted: this is a property of a candidate that has
        // not been adopted, and the study normalises with the certified bound.
        check (true, "closed-form behaviour on the calibrated poles is measured and reported");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - does cascading restore delay proportionality");

    {
        /*
            The coherence question, stated as maths rather than as a rendered
            tail: coherence needs log|H_i(w)| proportional to d_i. It holds at
            the asymptotes by construction and fails in the transition. Whether
            splitting the same total gain across N shallower sections reduces
            that failure is measured, not assumed.
        */
        const double decay = 2.5;
        const double reference = fdn::delaysMs[0] * 0.001;

        info ("  Worst deviation of log|H_i| / d_i from the reference line,");
        info ("  swept across frequency, at High Damp 100 % (the hardest case).");
        info ("");
        info ("    topology              worst deviation");

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            LineSpec referenceLine = buildLine (topology, reference, decay,
                                                1.0, mapping::highDampRatioFor (1.0),
                                                200.0, 6000.0, kSampleRate);

            double worst = 0.0;

            for (int i = 1; i < kLines; ++i)
            {
                const double d = fdn::delaysMs[static_cast<size_t> (i)] * 0.001;

                const LineSpec line = buildLine (topology, d, decay,
                                                 1.0, mapping::highDampRatioFor (1.0),
                                                 200.0, 6000.0, kSampleRate);

                for (int p = 0; p <= 96; ++p)
                {
                    const double f = 20.0 * std::pow (1000.0, p / 96.0);

                    const double mine = std::log (normalisedMagnitude (line, f, kSampleRate));
                    const double ref  = std::log (normalisedMagnitude (referenceLine, f, kSampleRate));

                    if (ref >= -1.0e-9)
                        continue;

                    const double ratio = (mine / d) / (ref / reference);

                    worst = std::max (worst, std::abs (ratio - 1.0));
                }
            }

            info ("    " + pad (topology.label, 22) + number (worst * 100.0, 2) + " %");
        }

        info ("");
        info ("  A smaller number means log|H| tracks delay length more closely,");
        info ("  which is what per-line decay coherence needs.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - default character");

    {
        const double decay = 2.5;
        const double bands[] { 80.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 6000.0, 12000.0 };

        const double lowRatio  = mapping::lowDampRatioFor  (0.20);
        const double highRatio = mapping::highDampRatioFor (0.50);

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            StudyTank tank (decay);
            tank.configureDamping (topology, lowRatio, highRatio, 200.0, 6000.0);

            info ("");
            info ("  " + std::string (topology.label));
            info ("    band     RT60    EDT     T20     T30     R^2      dev%");

            double worstMid = 0.0;
            double longest = 0.0;
            double rt12k = 0.0;

            for (double f : bands)
            {
                const auto fit = measureBand (tank, f, decay, 2);

                if (! fit.valid)
                    continue;

                const double deviation = (fit.fitted / decay - 1.0) * 100.0;

                info ("    " + pad (number (f, 0), 9)
                      + pad (number (fit.fitted, 3), 8)
                      + pad (number (fit.edt, 3), 8)
                      + pad (number (fit.t20, 3), 8)
                      + pad (number (fit.t30, 3), 8)
                      + pad (number (fit.r2, 5), 9)
                      + number (deviation, 2));

                longest = std::max (longest, fit.fitted);

                if (f >= 500.0 && f <= 2000.0)
                    worstMid = std::max (worstMid, std::abs (deviation));

                if (f >= 12000.0)
                    rt12k = fit.fitted;
            }

            verdicts[c].midWorst = worstMid;
            verdicts[c].rt12k = rt12k;

            info ("    worst 500 Hz - 2 kHz deviation " + number (worstMid, 3)
                  + " %   -> " + std::string (worstMid <= kGateMidDeviation ? "PASSES" : "FAILS")
                  + " the +/-2 % gate");
            info ("    12 kHz RT60 " + number (rt12k, 4) + " s");
            info ("    longest measured band " + number (longest, 4) + " s");

            checkAtMost (longest, decay * 1.02,
                         std::string (topology.label) + ": no band exceeds the Decay target");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - aggregate crossover coherence");

    {
        const double decay = 2.5;

        struct Case { double low, high; const char* label; };

        const Case cases[]
        {
            { 0.00, 0.00, "damping OFF" },
            { 0.20, 0.50, "shipping default" },
            { 0.00, 0.60, "moderate high" },
            { 0.00, 1.00, "High Damp 100 %" },
            { 1.00, 0.00, "Low Damp 100 %" },
        };

        const double probes[] { 100.0, 200.0, 400.0, 1000.0, 3000.0, 6000.0, 12000.0 };

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            double worstMismatch = 0.0, worstR2 = 1.0;
            std::string where;

            for (const auto& config : cases)
            {
                StudyTank tank (decay);

                if (config.low > 0.0 || config.high > 0.0)
                    tank.configureDamping (topology,
                                           mapping::lowDampRatioFor (config.low),
                                           mapping::highDampRatioFor (config.high),
                                           200.0, 6000.0);

                for (double f : probes)
                {
                    const auto fit = measureBand (tank, f, decay, 3);

                    if (! fit.valid)
                        continue;

                    const double mean = 0.5 * (fit.t20 + fit.t30);
                    const double mismatch = std::abs (fit.t20 - fit.t30)
                                            / std::max (mean, 1.0e-12);

                    if (mismatch > worstMismatch)
                    {
                        worstMismatch = mismatch;
                        where = std::string (config.label) + " at " + number (f, 0) + " Hz";
                    }

                    worstR2 = std::min (worstR2, fit.r2);
                }
            }

            verdicts[c].worstMismatch = worstMismatch;
            verdicts[c].worstR2 = worstR2;

            info ("  " + pad (std::string (topology.label), 20)
                  + "worst |T20-T30| " + pad (number (worstMismatch * 100.0, 2) + " %", 10)
                  + "worst R^2 " + pad (number (worstR2, 5), 10)
                  + where);
        }

        info ("");
        info ("  Gate unchanged: R^2 >= " + number (kGateMinimumR2, 3)
              + ", |T20-T30| <= " + number (kGateMaxT20T30Mismatch * 100.0, 0) + " %.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - per-line K spread (diagnostic)");

    {
        const double decay = 2.5;

        struct Probe { double hz; const char* label; };

        const Probe probes[]
        {
            {    40.0, "low plateau" },
            {   500.0, "500 Hz" },
            {  1000.0, "1 kHz" },
            {  2000.0, "2 kHz" },
            {  6000.0, "high crossover" },
            { 14000.0, "high plateau" },
        };

        info ("  High Damp 100 % — the configuration that produced the 25.28 %");
        info ("  spread on the current topology. Diagnostic only: the gate is the");
        info ("  aggregate T20/T30 result above.");
        info ("");
        info ("    topology            low pl.  500 Hz   1 kHz    2 kHz    high x   high pl.");

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            StudyTank tank (decay);
            tank.configureDamping (topology, 1.0, mapping::highDampRatioFor (1.0),
                                   200.0, 6000.0);

            std::string row = "    " + pad (topology.label, 20);
            double worst = 0.0;

            for (const auto& p : probes)
            {
                double minRt = 1.0e9, maxRt = 0.0;

                for (int i = 0; i < kLines; ++i)
                {
                    const double magnitude = normalisedMagnitude (tank.spec (i), p.hz, kSampleRate);
                    const double d = tank.delay (i);
                    const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;
                    const double rt = 1.0 / (1.0 / decay + k / 3.0);

                    minRt = std::min (minRt, rt);
                    maxRt = std::max (maxRt, rt);
                }

                const double spread = (maxRt - minRt) / std::max (maxRt, 1.0e-12) * 100.0;

                row += pad (number (spread, 2) + " %", 9);
                worst = std::max (worst, spread);
            }

            verdicts[c].worstKSpread = worst;
            info (row);
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - is the tail gate measuring the tank or the band");

    {
        /*
            Cascading cut the per-line K spread by 25x and moved the aggregate
            T20/T30 mismatch by almost nothing. Those two numbers were supposed
            to be cause and effect, so one of them is measuring something else.

            The candidate explanation: a one-octave band spans frequencies whose
            RT60 genuinely differ once damping is steep, so the band sums
            exponentials of different rates. That mixture always reads T30 > T20
            — not because the tank changes slope, but because the measurement
            averaged a gradient.

            The test is simple. Narrow the band. If the mismatch falls with
            bandwidth, the gate was reading the analysis filter; if it stays,
            the tank really does change slope and the finding stands.
        */
        const double decay = 2.5;

        info ("  High Damp 100 %, the worst configuration, at three bandwidths.");
        info ("");
        info ("    topology            band       1 oct     1/3 oct   1/6 oct");

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            for (double f : { 3000.0, 12000.0 })
            {
                StudyTank tank (decay);
                tank.configureDamping (topology, 1.0, mapping::highDampRatioFor (1.0),
                                       200.0, 6000.0);

                std::string row = "    " + pad (topology.label, 20)
                                  + pad (number (f, 0) + " Hz", 11);

                for (double octaves : { 1.0, 1.0 / 3.0, 1.0 / 6.0 })
                {
                    const auto fit = measureBand (tank, f, decay, 3, octaves);

                    if (! fit.valid)
                    {
                        row += pad ("-", 10);
                        continue;
                    }

                    const double mean = 0.5 * (fit.t20 + fit.t30);
                    const double mismatch = std::abs (fit.t20 - fit.t30)
                                            / std::max (mean, 1.0e-12) * 100.0;

                    row += pad (number (mismatch, 2) + " %", 10);
                }

                info (row);
            }
        }

        info ("");
        info ("  A column that falls steeply from left to right means the gate was");
        info ("  reading the width of the analysis band, not a slope change in the");
        info ("  tank. A flat row means the tank really does change slope.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - Option A revalidation per candidate");

    {
        /*
            The corner calibration moves the pole, so the maximum sits somewhere
            new and none of the previous conclusions carry over untested. Dense
            coverage, per candidate: |Hn| <= 1, nothing above base Decay, no
            damping-induced clamp, and zero-damp bit identity.
        */
        const double lows[]    { 20.0, 200.0, 1000.0 };
        const double highs[]   { 500.0, 6000.0, 20000.0 };
        const double amounts[] { 0.0, 0.5, 1.0 };
        const double decays[]  { 0.1, 2.5, 100.0 };
        const double rates[]   { 44100.0, 48000.0, 192000.0 };

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            double maxNormalised = 0.0, maxTotal = 0.0;
            long long above = 0, combinations = 0;

            for (double fs : rates)
                for (double fl : lows)
                    for (double fh : highs)
                        for (double la : amounts)
                            for (double ha : amounts)
                            {
                                if (la <= 0.0 && ha <= 0.0)
                                    continue;

                                for (double rt60 : decays)
                                    for (int i = 0; i < kLines; ++i)
                                    {
                                        const double d = fdn::delaysMs[static_cast<size_t> (i)]
                                                         * 0.001;

                                        const LineSpec line =
                                            buildLine (topology, d, rt60,
                                                       mapping::lowDampRatioFor (la),
                                                       mapping::highDampRatioFor (ha),
                                                       fl, fh, fs);

                                        double peak = 0.0;

                                        for (int p = 0; p <= 200; ++p)
                                        {
                                            const double f = std::exp (
                                                std::log (1.0)
                                                + (std::log (0.4999 * fs) - std::log (1.0))
                                                  * p / 200.0);

                                            peak = std::max (peak,
                                                             normalisedMagnitude (line, f, fs));
                                        }

                                        peak = std::max ({ peak,
                                                           normalisedMagnitude (line, 0.0, fs),
                                                           normalisedMagnitude (line, 0.5 * fs, fs) });

                                        const double g = std::min (
                                            fdn::maxFeedbackGain,
                                            fdn::feedbackGainForRt60 (d, rt60));

                                        maxNormalised = std::max (maxNormalised, peak);
                                        maxTotal = std::max (maxTotal, g * peak);

                                        if (peak > kStudyBoundTolerance)
                                            ++above;

                                        ++combinations;
                                    }
                            }

            verdicts[c].maxNormalised = maxNormalised;
            verdicts[c].aboveDecay = above;
            verdicts[c].stable = (maxTotal <= fdn::maxFeedbackGain + 1.0e-12);

            info ("  " + pad (std::string (topology.label), 20)
                  + "combinations " + pad (std::to_string (combinations), 10)
                  + "max|Hn| " + pad (number (maxNormalised, 10), 14)
                  + "max g*|Hn| " + number (maxTotal, 8));

            // The tolerance is the STUDY bound's precision on a pole family the
            // production closed form was never built for - see the maximum
            // suite above. T1 and T2 come out at exactly 1.0; only the
            // recalibrated single-section control needs the allowance.
            checkAtMost (maxNormalised, kStudyBoundTolerance,
                         std::string (topology.label) + ": normalised response stays at unity");

            checkEqual (above, 0LL,
                        std::string (topology.label) + ": no combination rings above base Decay");

            checkAtMost (maxTotal, fdn::maxFeedbackGain + 1.0e-12,
                         std::string (topology.label) + ": no damping-induced feedback clamp");

            // Zero damping must be a bit-exact identity for every candidate.
            StudyTank plain (2.5);
            StudyTank zeroed (2.5);
            zeroed.configureDamping (topology, 1.0, 1.0, 200.0, 6000.0);

            const auto a = plain.render (24000, 0u);
            const auto b = zeroed.render (24000, 0u);

            long long differing = 0;

            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i])
                    ++differing;

            verdicts[c].zeroDampIdentity = (differing == 0);

            checkEqual (differing, 0LL,
                        std::string (topology.label) + ": Damp 0 % is a bit-exact identity");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - relative CPU");

    {
        /*
            Relative only, and measured on the damping stage in isolation as
            well as in the tank, so the figure is not swamped by the delay lines
            and the matrix. The Size-morph column is simply twice the steady
            cost: MorphingStereoFdn runs two banks for the duration.
        */
        const int samples = 400000;

        double baseline = 1.0;

        info ("    topology            steady tank   damping stage   Size morph (2 banks)");

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& topology = candidates[c];

            StudyTank tank (2.5);
            tank.configureDamping (topology, mapping::lowDampRatioFor (0.20),
                                   mapping::highDampRatioFor (0.50), 200.0, 6000.0);

            const auto start = std::chrono::steady_clock::now();
            const auto ir = tank.render (samples, 0u);
            const auto stop = std::chrono::steady_clock::now();

            const double seconds = std::chrono::duration<double> (stop - start).count();

            // Keep the render alive so the compiler cannot elide it.
            double guard = 0.0;
            for (double s : ir) guard += s;
            (void) guard;

            if (c == 0)
                baseline = seconds;

            const double relative = seconds / std::max (baseline, 1.0e-12);

            verdicts[c].relativeCpu = relative;

            info ("    " + pad (topology.label, 20)
                  + pad (number (relative, 3) + "x", 14)
                  + pad (std::to_string (topology.sections * 2) + " sections", 16)
                  + number (relative * 2.0, 3) + "x of T0 steady");
        }

        info ("");
        info ("  Relative to T0 production. Timing on one machine, one run:");
        info ("  a ranking, not a budget. The absolute figure belongs to the CPU");
        info ("  measurement in the full validation, against the ~1.05 % baseline.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 topology - decision table");

    {
        const char* rows[]
        {
            "default 500-2k worst dev",
            "12k default RT60",
            "high-heavy worst T20/T30",
            "worst R^2",
            "worst K_i spread",
            "max |Hn|",
            "RT60 above Decay count",
            "relative CPU",
            "zero-damp bit identity",
            "stability",
        };

        info ("                            "
              + pad ("T0 current", 14) + pad ("T0 recal", 14)
              + pad ("T1 2-stage", 14) + "T2 3-stage");

        for (int r = 0; r < 10; ++r)
        {
            std::string line = "    " + pad (rows[r], 28);

            for (int c = 0; c < candidateCount; ++c)
            {
                const auto& v = verdicts[c];
                std::string cell;

                switch (r)
                {
                    case 0: cell = number (v.midWorst, 2) + " %"; break;
                    case 1: cell = number (v.rt12k, 3) + " s"; break;
                    case 2: cell = number (v.worstMismatch * 100.0, 2) + " %"; break;
                    case 3: cell = number (v.worstR2, 5); break;
                    case 4: cell = number (v.worstKSpread, 2) + " %"; break;
                    case 5: cell = number (v.maxNormalised, 8); break;
                    case 6: cell = std::to_string (v.aboveDecay); break;
                    case 7: cell = number (v.relativeCpu, 2) + "x"; break;
                    case 8: cell = v.zeroDampIdentity ? "exact" : "BROKEN"; break;
                    default: cell = v.stable ? "proven" : "FAILED"; break;
                }

                line += pad (cell, 14);
            }

            info (line);
        }

        info ("");
        info ("  Gates: mid deviation <= " + number (kGateMidDeviation, 1)
              + " %, |T20-T30| <= " + number (kGateMaxT20T30Mismatch * 100.0, 0)
              + " %, R^2 >= " + number (kGateMinimumR2, 3) + ".");

        for (int c = 0; c < candidateCount; ++c)
        {
            const auto& v = verdicts[c];

            const bool passesMid  = v.midWorst <= kGateMidDeviation;
            const bool passesTail = v.worstMismatch <= kGateMaxT20T30Mismatch
                                    && v.worstR2 >= kGateMinimumR2;

            info ("  " + pad (std::string (candidates[c].label), 20)
                  + "mid gate " + pad (passesMid ? "PASS" : "FAIL", 8)
                  + "tail gate " + (passesTail ? "PASS" : "FAIL"));
        }
    }
}

} // namespace avtest
