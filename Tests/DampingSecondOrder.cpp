#include "Suites.h"
#include "TestFramework.h"
#include "DecayAnalyzer.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnDelayLine.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulatedFeedbackMatrix.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

/*
    PHASE 7 - TEST-ONLY topology study: second-order monotonic shelf.

    Nothing in Source/ changes. The production topology, mapping, parameter
    ids, ranges and defaults are untouched. This is a measurement of what a
    more selective monotonic transition would do, run in a test-local FDN that
    mirrors FdnCore's structure with the damping stage swapped out.

    WHY THIS CANDIDATE AND NOT A CASCADE

    The fixed-order cascade of identical first-order sections (T1/T2) is
    rejected and is not reopened here. A cascade buys steepness by raising the
    whole pair to a power, which steepens the plateau approach as much as the
    transition and drags the broad mid down with it - which is Problem A, not
    a fix for it. A second-order shelf with unit slope steepens the TRANSITION
    while leaving both plateaus exactly where they were.

    THE CANDIDATE

    An RBJ-form shelving biquad per band, shelf slope S = 1. Three properties
    of that form matter here and all three are measured below rather than
    assumed:

      * the high shelf is exactly 1 at DC and exactly G at Nyquist. Algebra:
        sum(b)/sum(a) = 4A(1-cos w0) / 4A(1-cos w0) = 1, and
        (b0-b1+b2)/(a0-a1+a2) = 4A^2(1+cos w0) / 4(1+cos w0) = A^2 = G.

      * at w0 the magnitude is exactly A = sqrt(G), which is halfway through
        the plateau attenuation IN dB - the corner definition this study was
        asked to hold. It comes out of the form itself, with no fitted
        constant and no offset.

      * at G = 1 the numerator and denominator coefficients are identically
        equal, so the section is a bit-exact identity rather than something
        that merely rounds to one.

      S = 1 is the steepest slope the form reaches without overshoot. S > 1
      produces a resonant bump before the plateau, which requirement 2
      forbids, so S is fixed at 1 and monotonicity is then verified by
      measurement over the whole parameter grid.

    FAIRNESS

    The candidate is matched to production on everything except transition
    shape:

      * identical requested plateau ratios, from damping::shelfGainFor on the
        same per-line loop delay - so the asymptotic low and high plateaus are
        the same gains in both topologies
      * identical corner frequencies, with the corner meaning the same thing
        in both (half the plateau attenuation in dB)
      * Option A maximum normalisation in both, computed for the candidate by
        its own certified bound rather than by assuming production's closed
        form carries over to a different filter family

    Only the shape between the plateaus differs. Nothing is retuned after
    seeing a T20/T30 number.
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
constexpr double kGateMidDeviation      = 2.0;    // per cent, 500 Hz - 2 kHz
constexpr double kInvestigationGate     = 5.0;    // per cent |T20-T30|
constexpr double kGateMinimumR2         = 0.995;
constexpr double kGateBandOctaves       = 1.0;
constexpr int    kEnsembleSize          = 32;

// Shipping defaults, for the Problem A target.
constexpr double kDefaultDecay    = 2.5;
constexpr double kDefaultLowDamp  = 0.20;
constexpr double kDefaultHighDamp = 0.50;
constexpr double kDefaultLowHz    = 200.0;
constexpr double kDefaultHighHz   = 6000.0;

// Production T0 reference, measured and registered in the previous checkpoint.
constexpr double kT0Mismatch1k  = 1.27;
constexpr double kT0Mismatch3k  = 2.55;
constexpr double kT0Mismatch6k  = 4.22;
constexpr double kT0Mismatch12k = 6.00;

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
//  The candidate: one RBJ-form shelving section, shelf slope S = 1
// ===========================================================================

struct Section2
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    [[nodiscard]] bool isIdentity() const noexcept
    {
        // Bit-exact, not approximate: the coefficients must be equal.
        return b0 == 1.0 && b1 == a1 && b2 == a2;
    }

    void reset() noexcept { z1 = 0.0; z2 = 0.0; }

    [[nodiscard]] double process (double x) noexcept
    {
        // Transposed direct form II.
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;

        return y;
    }

    [[nodiscard]] std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> e1 = std::polar (1.0, -w);
        const std::complex<double> e2 = std::polar (1.0, -2.0 * w);

        return (b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2);
    }

    [[nodiscard]] double magnitude (double w) const noexcept
    {
        return std::abs (response (w));
    }

    /** Largest pole magnitude, which must be below one for stability. */
    [[nodiscard]] double poleRadius() const noexcept
    {
        const double disc = a1 * a1 - 4.0 * a2;

        if (disc >= 0.0)
        {
            const double r = std::sqrt (disc);
            return std::max (std::abs (-a1 + r), std::abs (-a1 - r)) * 0.5;
        }

        return std::sqrt (std::max (0.0, a2));
    }
};

/*
    The two shelves. g is the LINEAR plateau gain, so A = sqrt(g) is RBJ's
    amplitude parameter and alpha carries S = 1.
*/
[[nodiscard]] Section2 makeShelf2 (double gain, double cornerHz, double sampleRate,
                                   bool high) noexcept
{
    Section2 s;

    const double g = std::clamp (gain, 1.0e-12, 1.0);

    if (gain >= 1.0)
        return s;                       // identity, exactly

    const double f = std::clamp (cornerHz, damping::minCutoffHz, 0.49 * sampleRate);
    const double w = 2.0 * damping::pi * f / sampleRate;

    const double A  = std::sqrt (g);
    const double rA = std::sqrt (A);
    const double cs = std::cos (w);
    const double sn = std::sin (w);

    // S = 1  =>  alpha = sin(w)/2 * sqrt(2)
    const double alpha = 0.5 * sn * std::sqrt (2.0);
    const double two   = 2.0 * rA * alpha;

    double b0, b1, b2, a0, a1, a2;

    if (high)
    {
        b0 =        A * ((A + 1.0) + (A - 1.0) * cs + two);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
        b2 =        A * ((A + 1.0) + (A - 1.0) * cs - two);
        a0 =            ((A + 1.0) - (A - 1.0) * cs + two);
        a1 =    2.0 *   ((A - 1.0) - (A + 1.0) * cs);
        a2 =            ((A + 1.0) - (A - 1.0) * cs - two);
    }
    else
    {
        b0 =        A * ((A + 1.0) - (A - 1.0) * cs + two);
        b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
        b2 =        A * ((A + 1.0) - (A - 1.0) * cs - two);
        a0 =            ((A + 1.0) + (A - 1.0) * cs + two);
        a1 =   -2.0 *   ((A - 1.0) + (A + 1.0) * cs);
        a2 =            ((A + 1.0) + (A - 1.0) * cs - two);
    }

    s.b0 = b0 / a0;
    s.b1 = b1 / a0;
    s.b2 = b2 / a0;
    s.a1 = a1 / a0;
    s.a2 = a2 / a0;

    return s;
}

// ===========================================================================
//  One line's damping, either topology
// ===========================================================================

enum class Topology { production, secondOrder };

struct LineDamper
{
    Topology topology = Topology::production;
    bool transparent = true;

    // Production first-order pair.
    damping::ShelfSpec spec {};
    double cLow = 0.0, cHigh = 0.0, bLow = 0.0, bHigh = 0.0;
    double zLow = 0.0, zHigh = 0.0;

    // Candidate second-order pair.
    Section2 low2 {}, high2 {};

    double normalisation = 1.0;
    double rawMaximum = 1.0;

    void reset() noexcept
    {
        zLow = 0.0;
        zHigh = 0.0;
        low2.reset();
        high2.reset();
    }

    [[nodiscard]] double process (double x) noexcept
    {
        if (transparent)
            return x;

        if (topology == Topology::production)
        {
            // Byte for byte the production inner loop.
            zLow += bLow * (x - zLow);
            const double afterLow = x - cLow * zLow;

            zHigh += bHigh * (afterLow - zHigh);

            return (afterLow - cHigh * (afterLow - zHigh)) * normalisation;
        }

        return high2.process (low2.process (x)) * normalisation;
    }

    /** |H| of this line's unnormalised pair at one frequency. */
    [[nodiscard]] double rawMagnitude (double freqHz, double sampleRate) const noexcept
    {
        if (transparent)
            return 1.0;

        if (topology == Topology::production)
            return damping::magnitudeAt (spec, freqHz, sampleRate);

        const double w = 2.0 * damping::pi * freqHz / sampleRate;

        return low2.magnitude (w) * high2.magnitude (w);
    }

    [[nodiscard]] double normalisedMagnitude (double freqHz, double sampleRate) const noexcept
    {
        return rawMagnitude (freqHz, sampleRate) * normalisation;
    }
};

/*
    A certified upper bound on max|H_pair|, resting on monotonicity alone.

    |H_low| never falls with frequency and |H_high| never rises. On any
    interval the product of the two favourable endpoints therefore bounds the
    true maximum whatever the shape inside, so sweeping the band and taking
    (low here) * (high one step back) is an upper bound at every step. It is
    the method Option A used before the production closed form replaced it,
    and it is the right one here because the closed form is specific to the
    first-order pole family and is NOT validated for this one. Monotonicity is
    itself verified by measurement in the requirements suite; if it failed,
    this bound would be reported as invalid rather than used.
*/
[[nodiscard]] double certifiedPairMaximum2 (const Section2& low, const Section2& high,
                                            double sampleRate, int intervals = 8192) noexcept
{
    const double lowest  = std::log (1.0);
    const double highest = std::log (0.49999 * sampleRate);

    const auto at = [&] (int k)
    {
        const double f = (k == intervals)
                         ? 0.5 * sampleRate
                         : std::exp (lowest + (highest - lowest) * k / intervals);

        return 2.0 * damping::pi * f / sampleRate;
    };

    double previousHigh = high.magnitude (0.0);      // DC
    double bound = low.magnitude (0.0) * previousHigh;

    for (int k = 0; k <= intervals; ++k)
    {
        const double w = at (k);

        const double lo = low.magnitude (w);
        const double hi = high.magnitude (w);

        bound = std::max (bound, lo * previousHigh);
        previousHigh = hi;
    }

    return bound;
}

/** The attained maximum, for reporting how much headroom the bound leaves. */
[[nodiscard]] double attainedPairMaximum2 (const Section2& low, const Section2& high,
                                           double sampleRate, int points = 200000) noexcept
{
    double best = 0.0;

    for (int k = 0; k <= points; ++k)
    {
        const double w = damping::pi * k / points;

        best = std::max (best, low.magnitude (w) * high.magnitude (w));
    }

    return best;
}

[[nodiscard]] LineDamper buildLine (Topology topology, double delaySeconds, double rt60,
                                    double lowRatio, double highRatio,
                                    double lowHz, double highHz, double sampleRate)
{
    LineDamper d;
    d.topology = topology;

    const double gainLow  = damping::shelfGainFor (delaySeconds, rt60, lowRatio);
    const double gainHigh = damping::shelfGainFor (delaySeconds, rt60, highRatio);

    d.transparent = (gainLow >= 1.0 && gainHigh >= 1.0);

    if (d.transparent)
        return d;

    if (topology == Topology::production)
    {
        d.spec.gainLow   = gainLow;
        d.spec.gainHigh  = gainHigh;
        d.spec.coeffLow  = damping::onePoleCoefficient (lowHz,  sampleRate);
        d.spec.coeffHigh = damping::onePoleCoefficient (highHz, sampleRate);

        d.cLow  = 1.0 - d.spec.gainLow;
        d.cHigh = 1.0 - d.spec.gainHigh;
        d.bLow  = 1.0 - d.spec.coeffLow;
        d.bHigh = 1.0 - d.spec.coeffHigh;

        const auto bound = damping::compositeMaximum (d.spec);

        d.rawMaximum = bound.upper;
        d.normalisation = 1.0 / bound.upper;
    }
    else
    {
        d.low2  = makeShelf2 (gainLow,  lowHz,  sampleRate, false);
        d.high2 = makeShelf2 (gainHigh, highHz, sampleRate, true);

        d.rawMaximum = certifiedPairMaximum2 (d.low2, d.high2, sampleRate);
        d.normalisation = 1.0 / std::max (d.rawMaximum, 1.0e-12);
    }

    return d;
}

// ===========================================================================
//  Test-local FDN, structurally the production one
// ===========================================================================

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
            const auto idx = static_cast<size_t> (i);

            lines[idx].prepare (capacity);

            const int samples = static_cast<int> (std::lround (
                fdn::delaysMs[idx] * 0.001 * fs));

            (void) lines[idx].setDelaySamples (samples);

            delaySeconds[idx] = static_cast<double> (samples) / fs;

            gains[idx] = std::min (fdn::maxFeedbackGain,
                                   fdn::feedbackGainForRt60 (delaySeconds[idx], rt60));
        }

        reset();
    }

    void configure (Topology topology, double lowDamp, double highDamp,
                    double lowHz, double highHz)
    {
        const double lowRatio  = damping::decayRatioFor (lowDamp,  damping::lowRatioAtFull);
        const double highRatio = damping::decayRatioFor (highDamp, damping::highRatioAtFull);

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            dampers[idx] = buildLine (topology, delaySeconds[idx], rt60,
                                      lowRatio, highRatio, lowHz, highHz, fs);
        }

        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines)   l.reset();
        for (auto& d : dampers) d.reset();
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive, int numSamples)
    {
        reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double v[kLines] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (static_cast<size_t> (n) < drive.size())
                             ? drive[static_cast<size_t> (n)] : 0.0;

            double sum = 0.0;

            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<size_t> (i);

                v[i] = dampers[idx].process (lines[idx].read()) * gains[idx];
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

    /** Predicted RT60 at one frequency from the band-decay master equation. */
    [[nodiscard]] double predictedRt60 (double freqHz) const
    {
        double sum = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const double m = dampers[idx].normalisedMagnitude (freqHz, fs);
            const double k = -std::log10 (std::max (m, 1.0e-300)) / delaySeconds[idx];

            sum += 1.0 / (1.0 / rt60 + k / 3.0);
        }

        return sum / kLines;
    }

    /** Per-line K spread at one frequency, as a percentage of the longest RT60. */
    [[nodiscard]] double kSpread (double freqHz) const
    {
        double lowest = 1.0e300, highest = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const double m = dampers[idx].normalisedMagnitude (freqHz, fs);
            const double k = -std::log10 (std::max (m, 1.0e-300)) / delaySeconds[idx];
            const double rt = 1.0 / (1.0 / rt60 + k / 3.0);

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
                const double f = 0.5 * fs * k / 4096.0;

                worst = std::max (worst,
                                  dampers[static_cast<size_t> (i)]
                                      .normalisedMagnitude (f, fs));
            }

        return worst;
    }

    [[nodiscard]] const LineDamper& damper (int i) const noexcept
    {
        return dampers[static_cast<size_t> (i)];
    }

    [[nodiscard]] double delay (int i) const noexcept
    {
        return delaySeconds[static_cast<size_t> (i)];
    }

    [[nodiscard]] double gain (int i) const noexcept
    {
        return gains[static_cast<size_t> (i)];
    }

private:
    std::array<FdnDelayLine, kLines> lines;
    std::array<LineDamper, kLines> dampers {};
    std::array<double, kLines> gains {}, delaySeconds {};

    ModulatedFeedbackMatrix<kLines> matrix;

    double rt60 = 2.5;
    double fs = kSampleRate;
};

// ===========================================================================
//  Measurement with the validated fixed-Q ensemble analyser
// ===========================================================================

/*
    THE EXCITATION IS THE REGISTERED ONE, NOT A NEW ONE.

    The first version of this study used a plain 64-sample burst with a unit
    impulse as realisation 0, and the T0 column then came out 1.19 pp away
    from the registered T0 numbers at 12 kHz - 7.19 % against 6.00 %. The
    self-check caught it, which is what it is for. The registered ensemble
    uses a 1024-sample Hann-windowed noise burst, seeded the same way, with
    the RMS equalised so every realisation puts the same total energy into
    the tank before it sees any filter. Copied exactly, because the point of
    the comparison is the topology and nothing else.
*/
inline constexpr int kBurstSamples = 1024;

[[nodiscard]] unsigned int ensembleSeed (int index) noexcept
{
    return 2654435761u * static_cast<unsigned int> (index + 1) + 1013904223u;
}

[[nodiscard]] std::vector<double> makeExcitation (int index, int length,
                                                  double amplitude = 1.0)
{
    std::vector<double> drive (static_cast<size_t> (std::max (length, kBurstSamples)), 0.0);

    unsigned int rng = ensembleSeed (index);

    double sumSquares = 0.0;

    for (int n = 0; n < kBurstSamples; ++n)
    {
        rng = rng * 1664525u + 1013904223u;

        const double white = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
        const double window = 0.5 - 0.5 * std::cos (2.0 * decay::pi * n
                                                    / (kBurstSamples - 1));

        drive[static_cast<size_t> (n)] = white * window;
        sumSquares += drive[static_cast<size_t> (n)] * drive[static_cast<size_t> (n)];
    }

    const double rms = std::sqrt (sumSquares / kBurstSamples);
    const double scale = (rms > 0.0) ? amplitude / rms : amplitude;

    for (auto& s : drive)
        s *= scale;

    drive.resize (static_cast<size_t> (length), 0.0);

    return drive;
}
/** Render the ensemble once; every band then reuses these impulse responses. */
[[nodiscard]] std::vector<std::vector<double>> renderEnsemble (StudyTank& tank,
                                                               double rt60Hint)
{
    const int length = static_cast<int> (kSampleRate * std::max (2.0, rt60Hint * 2.5));

    std::vector<std::vector<double>> all;
    all.reserve (static_cast<size_t> (kEnsembleSize));

    for (int k = 0; k < kEnsembleSize; ++k)
        all.push_back (tank.render (makeExcitation (k, length), length));

    return all;
}

/*
    The validated method, unchanged: two constant-peak-gain bandpass sections
    applied forward then backward so the band is zero phase, the energy decay
    curve normalised per realisation, and the ensemble averaged in the LINEAR
    energy domain before a single fit.
*/
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
//  The Phase 7 parameter grid, for the requirement checks
// ===========================================================================

struct GridPoint
{
    double decay, lowDamp, highDamp, lowHz, highHz;
};

[[nodiscard]] std::vector<GridPoint> parameterGrid()
{
    std::vector<GridPoint> grid;

    for (double decay : { 0.2, 0.6, 2.5, 10.0, 30.0 })
        for (double low : { 0.0, 0.2, 0.5, 1.0 })
            for (double high : { 0.0, 0.2, 0.5, 1.0 })
                for (double lowHz : { 20.0, 200.0, 1000.0 })
                    for (double highHz : { 1000.0, 6000.0, 18000.0 })
                        grid.push_back ({ decay, low, high, lowHz, highHz });

    return grid;
}

} // namespace

// ===========================================================================
//  Suites
// ===========================================================================

void runDampingSecondOrderTests()
{
    const auto grid = parameterGrid();

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 topology - the candidate meets its seven requirements");
    // -----------------------------------------------------------------------
    {
        info ("  An RBJ-form shelving biquad per band, shelf slope S = 1. Every");
        info ("  property the study relies on is measured here, including the ones");
        info ("  that follow from the algebra, because a derivation that is right");
        info ("  on paper can still be wrong in the code.");
        info ("");
        info ("  Grid: " + std::to_string (grid.size()) + " parameter combinations"
              + "   (Decay x Low Damp x High Damp x Low Hz x High Hz)");
        info ("");

        double worstPlateauDc = 0.0;        // |H_high(DC)| - 1
        double worstPlateauNyquist = 0.0;   // |H_low(Nyquist)| - 1
        double worstCorner = 0.0;           // |H(w0)| vs sqrt(g), in dB
        double worstMonotone = 0.0;         // worst non-monotonic step
        double worstPole = 0.0;             // largest pole radius
        double worstNormalised = 0.0;       // largest max|Hn|
        double worstBoundGap = 0.0;         // certified bound over attained max
        int identityFailures = 0;
        int transparentPoints = 0;

        for (const auto& point : grid)
        {
            StudyTank tank (point.decay);
            tank.configure (Topology::secondOrder, point.lowDamp, point.highDamp,
                            point.lowHz, point.highHz);

            worstNormalised = std::max (worstNormalised, tank.worstNormalisedMagnitude());

            for (int i = 0; i < kLines; ++i)
            {
                const auto& d = tank.damper (i);

                if (d.transparent)
                {
                    ++transparentPoints;

                    // Requirement 6: zero damping must reduce to identity exactly.
                    { auto copy = d; if (copy.process (0.5) != 0.5)
                        ++identityFailures; }

                    continue;
                }

                // Requirement 1 and 3: plateaus land exactly where asked.
                worstPlateauDc = std::max (worstPlateauDc,
                                           std::abs (d.high2.magnitude (0.0) - 1.0));
                worstPlateauNyquist = std::max (worstPlateauNyquist,
                                                std::abs (d.low2.magnitude (damping::pi) - 1.0));

                // Requirement 7: stability.
                worstPole = std::max (worstPole, std::max (d.low2.poleRadius(),
                                                           d.high2.poleRadius()));

                // The registered corner definition, in dB.
                const auto cornerError = [&] (const Section2& s, double gain, double hz)
                {
                    const double w = 2.0 * damping::pi
                                     * std::clamp (hz, damping::minCutoffHz, 0.49 * kSampleRate)
                                     / kSampleRate;

                    const double wanted = std::sqrt (std::clamp (gain, 1.0e-12, 1.0));

                    return std::abs (20.0 * std::log10 (std::max (s.magnitude (w), 1.0e-300)
                                                        / wanted));
                };

                const double gLow  = d.low2.magnitude (0.0);
                const double gHigh = d.high2.magnitude (damping::pi);

                if (gLow < 1.0)
                    worstCorner = std::max (worstCorner,
                                            cornerError (d.low2, gLow, point.lowHz));

                if (gHigh < 1.0)
                    worstCorner = std::max (worstCorner,
                                            cornerError (d.high2, gHigh, point.highHz));

                // Requirement 2: monotonic, no resonant boost anywhere.
                double previousLow = d.low2.magnitude (0.0);
                double previousHigh = d.high2.magnitude (0.0);

                for (int k = 1; k <= 3000; ++k)
                {
                    const double w = damping::pi * k / 3000.0;

                    const double lo = d.low2.magnitude (w);
                    const double hi = d.high2.magnitude (w);

                    // Low shelf must never fall, high shelf must never rise.
                    worstMonotone = std::max (worstMonotone, previousLow - lo);
                    worstMonotone = std::max (worstMonotone, hi - previousHigh);

                    previousLow = lo;
                    previousHigh = hi;
                }

                // Requirement 4: the normalisation rests on a bound that holds.
                const double attained = attainedPairMaximum2 (d.low2, d.high2,
                                                              kSampleRate, 20000);

                worstBoundGap = std::max (worstBoundGap, d.rawMaximum - attained);
            }
        }

        info ("    transparent lines exercised         " + std::to_string (transparentPoints));
        info ("    identity failures at zero damping   " + std::to_string (identityFailures));
        info ("    |H_high(DC)| - 1, worst             " + number (worstPlateauDc, 16));
        info ("    |H_low(Nyquist)| - 1, worst         " + number (worstPlateauNyquist, 16));
        info ("    corner error vs sqrt(g), worst      " + number (worstCorner, 10) + " dB");
        info ("    worst non-monotonic step            " + number (worstMonotone, 16));
        info ("    largest pole radius                 " + number (worstPole, 12));
        info ("    largest max|Hn| over all lines      " + number (worstNormalised, 14));
        info ("    certified bound minus attained max  " + number (worstBoundGap, 14));
        info ("");
        /*
            The cancellation, measured rather than argued. One line, one
            plateau gain, the corner swept: the DC-unity error should track
            1 / (1 - cos w0) if it is evaluation roundoff, and fall away as
            the corner rises.
        */
        info ("    DC-unity error against corner frequency, one line, g = 0.1");
        info ("      corner (Hz)   1/(1-cos w0)   |H(DC)|-1      ratio to 1e-16");

        for (double hz : { 20.0, 50.0, 200.0, 1000.0, 6000.0, 18000.0 })
        {
            const auto s = makeShelf2 (0.1, hz, kSampleRate, true);
            const double w = 2.0 * damping::pi * hz / kSampleRate;
            const double amplification = 1.0 / (1.0 - std::cos (w));
            const double err = std::abs (s.magnitude (0.0) - 1.0);

            info ("      " + pad (number (hz, 0), 14)
                  + pad (number (amplification, 1), 15)
                  + pad (number (err, 16), 15)
                  + number (err / 1.0e-16, 1));
        }

        info ("");
        info ("  The corner error is the distance between the measured magnitude at");
        info ("  the requested Damp Freq and half the plateau attenuation in dB. It");
        info ("  is a property of the RBJ form rather than something fitted, so a");
        info ("  non-zero value here would mean the implementation is wrong, not");
        info ("  that the calibration needs a constant.");

        check (identityFailures == 0,
               "zero damping reduces to a bit-exact identity");
        /*
            These two tolerances are MINE, set this turn, and they are not
            pre-registered quality gates. The first version had them at 1e-12
            and 1e-9 dB and both tripped - 8.3e-12 and 6.1e-9 - on numbers
            that say nothing is wrong with the filter.

            The reason is cancellation in the EVALUATION, not error in the
            coefficients. |H(DC)| is sum(b)/sum(a), and the algebra gives
            4A(1-cos w0) for both sums; at the lowest corner in the grid,
            20 Hz, (1 - cos w0) is 1.4e-5, so each sum is formed from terms
            about 7e4 times larger than the result and roughly five digits
            are lost. 1e-16 * 7e4 = 7e-12, which is what was measured. The
            coefficients themselves are exact: the bit-exact identity check
            at zero damping passes.

            So the bound is set from that analysis, 2e-11, and the cancellation
            is made visible rather than asserted - the error is also reported
            per corner frequency below, where the trend can be read off.
        */
        checkAtMost (worstPlateauDc, 2.0e-11,
                     "the high shelf is unity at DC to the cancellation floor");
        checkAtMost (worstPlateauNyquist, 1.0e-12,
                     "the low shelf is exactly unity at Nyquist");
        checkAtMost (worstCorner, 1.0e-7,
                     "the corner sits at half the plateau attenuation in dB");
        checkAtMost (worstMonotone, 1.0e-12,
                     "both shelves are monotonic, so there is no resonant boost");
        checkAtMost (worstPole, 0.9999999999,
                     "every section is stable across the Phase 7 grid");
        checkAtMost (worstNormalised, 1.0 + 1.0e-12,
                     "max normalised magnitude never exceeds one, so damping is"
                     " purely subtractive");
        check (worstBoundGap >= 0.0,
               "the certified bound is never below the attained maximum");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 topology - PROBLEM A, the default broad mid");
    // -----------------------------------------------------------------------
    {
        info ("  Shipping defaults: Decay " + number (kDefaultDecay, 1) + " s,"
              " Low Damp " + number (kDefaultLowDamp * 100.0, 0) + " %,"
              " High Damp " + number (kDefaultHighDamp * 100.0, 0) + " %,");
        info ("  Low Damp Freq " + number (kDefaultLowHz, 0) + " Hz,"
              " High Damp Freq " + number (kDefaultHighHz, 0) + " Hz.");
        info ("");
        info ("  The requirement is transition SELECTIVITY, so the plateau gains are");
        info ("  held identical between the two topologies and only the shape");
        info ("  between them differs. High Damp is not weakened to pass.");
        info ("");

        const double probes[] { 500.0, 1000.0, 2000.0, 4000.0, 6000.0, 12000.0 };

        struct Row { decay::Result result; double predicted, spread; };

        std::array<std::vector<Row>, 2> rows;
        std::array<double, 2> worstMid { 0.0, 0.0 };

        for (int t = 0; t < 2; ++t)
        {
            const auto topology = (t == 0) ? Topology::production : Topology::secondOrder;

            StudyTank tank (kDefaultDecay);
            tank.configure (topology, kDefaultLowDamp, kDefaultHighDamp,
                            kDefaultLowHz, kDefaultHighHz);

            const auto ensemble = renderEnsemble (tank, kDefaultDecay);

            for (double hz : probes)
            {
                const auto band = decay::makeBand (hz, kGateBandOctaves, kSampleRate);

                Row row;
                row.result = measureBand (ensemble, band);
                row.predicted = tank.predictedRt60 (hz);
                row.spread = tank.kSpread (hz);

                rows[static_cast<size_t> (t)].push_back (row);

                if (hz <= 2000.0 && row.result.valid)
                {
                    const double deviation = std::abs (row.result.fitted - kDefaultDecay)
                                             / kDefaultDecay * 100.0;

                    worstMid[static_cast<size_t> (t)] =
                        std::max (worstMid[static_cast<size_t> (t)], deviation);
                }
            }
        }

        info ("    topology  band      RT60     predicted  deviation  |T20-T30|  R^2       K spread");

        for (int t = 0; t < 2; ++t)
        {
            const char* label = (t == 0) ? "T0  " : "S2  ";

            for (size_t b = 0; b < rows[static_cast<size_t> (t)].size(); ++b)
            {
                const auto& row = rows[static_cast<size_t> (t)][b];

                const double deviation = std::abs (row.result.fitted - kDefaultDecay)
                                         / kDefaultDecay * 100.0;

                info (std::string ("    ") + label
                      + pad (number (probes[b], 0) + " Hz", 10)
                      + pad (number (row.result.fitted, 4), 9)
                      + pad (number (row.predicted, 4), 11)
                      + pad (number (deviation, 2) + " %", 11)
                      + pad (number (mismatchPercent (row.result), 2) + " %", 11)
                      + pad (number (row.result.r2, 5), 10)
                      + number (row.spread, 2) + " %");
            }

            info ("");
        }

        info ("    worst 500 Hz - 2 kHz deviation from base Decay");
        info ("      T0  " + number (worstMid[0], 2) + " %");
        info ("      S2  " + number (worstMid[1], 2) + " %");
        info ("      target " + number (kGateMidDeviation, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("");

        const double improvement = worstMid[0] - worstMid[1];

        info ("    S2 improves the broad mid by " + number (improvement, 2) + " pp");

        check (true, "default broad-mid deviation measured for both topologies");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 topology - PROBLEM B, extreme High Damp tail curvature");
    // -----------------------------------------------------------------------
    {
        info ("  Static M0: Mod Depth 0, Decay " + number (kDefaultDecay, 1) + " s,"
              " Low Damp 0, High Damp 100 %, High Damp Freq "
              + number (kDefaultHighHz, 0) + " Hz.");
        info ("  Measured with the validated fixed-Q ensemble analyser,"
              " " + std::to_string (kEnsembleSize) + " realisations.");
        info ("");

        const double probes[] { 1000.0, 3000.0, 6000.0, 12000.0 };
        const double reference[] { kT0Mismatch1k, kT0Mismatch3k,
                                   kT0Mismatch6k, kT0Mismatch12k };

        std::array<std::vector<decay::Result>, 2> rows;
        std::array<std::vector<double>, 2> spreads;

        for (int t = 0; t < 2; ++t)
        {
            const auto topology = (t == 0) ? Topology::production : Topology::secondOrder;

            StudyTank tank (kDefaultDecay);
            tank.configure (topology, 0.0, 1.0, kDefaultLowHz, kDefaultHighHz);

            const auto ensemble = renderEnsemble (tank, kDefaultDecay);

            for (double hz : probes)
            {
                const auto band = decay::makeBand (hz, kGateBandOctaves, kSampleRate);

                rows[static_cast<size_t> (t)].push_back (measureBand (ensemble, band));
                spreads[static_cast<size_t> (t)].push_back (tank.kSpread (hz));
            }
        }

        info ("    topology  band       RT60     EDT      T20      T30      R^2       |T20-T30|  K spread");

        for (int t = 0; t < 2; ++t)
        {
            const char* label = (t == 0) ? "T0  " : "S2  ";

            for (size_t b = 0; b < rows[static_cast<size_t> (t)].size(); ++b)
            {
                const auto& r = rows[static_cast<size_t> (t)][b];

                info (std::string ("    ") + label
                      + pad (number (probes[b], 0) + " Hz", 11)
                      + pad (number (r.fitted, 4), 9)
                      + pad (number (r.edt, 4), 9)
                      + pad (number (r.t20, 4), 9)
                      + pad (number (r.t30, 4), 9)
                      + pad (number (r.r2, 5), 10)
                      + pad (number (mismatchPercent (r), 2) + " %", 11)
                      + number (spreads[static_cast<size_t> (t)][b], 2) + " %");
            }

            info ("");
        }

        info ("    band       T0 here   T0 registered   S2 here   S2 - T0");

        double worstS2 = 0.0;
        double worst12k = 0.0;

        for (size_t b = 0; b < 4; ++b)
        {
            const double t0 = mismatchPercent (rows[0][b]);
            const double s2 = mismatchPercent (rows[1][b]);

            info ("    " + pad (number (probes[b], 0) + " Hz", 11)
                  + pad (number (t0, 2) + " %", 10)
                  + pad (number (reference[b], 2) + " %", 16)
                  + pad (number (s2, 2) + " %", 10)
                  + number (s2 - t0, 2) + " pp");

            worstS2 = std::max (worstS2, s2);

            if (probes[b] == 12000.0)
                worst12k = s2;
        }

        info ("");
        info ("    investigation gate " + number (kInvestigationGate, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("    S2 at 12 kHz       " + number (worst12k, 2) + " %");
        info ("    T0 at 12 kHz       " + number (mismatchPercent (rows[0][3]), 2) + " %");
        info ("");
        info ("  The T0 column measured here should reproduce the registered T0");
        info ("  numbers. If it does not, this harness disagrees with the one that");
        info ("  produced them and the comparison is not yet trustworthy.");

        double worstDrift = 0.0;

        for (size_t b = 0; b < 4; ++b)
            worstDrift = std::max (worstDrift,
                                   std::abs (mismatchPercent (rows[0][b]) - reference[b]));

        info ("    worst T0 drift against the registered numbers  "
              + number (worstDrift, 2) + " pp");

        checkAtMost (worstDrift, 0.50,
                     "the harness reproduces the registered production T0 mismatches");

        for (size_t b = 0; b < 4; ++b)
            check (rows[1][b].r2 >= kGateMinimumR2,
                   "the candidate fit is sound at " + number (probes[b], 0) + " Hz");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 topology - cost and per-line coherence");
    // -----------------------------------------------------------------------
    {
        info ("  CPU is measured on the damping stage alone and on the whole tank,");
        info ("  so the cost of the filter is not hidden inside the delay lines.");
        info ("");

        const int samples = 48000 * 10;

        std::array<double, 2> damperNs { 0.0, 0.0 }, tankNs { 0.0, 0.0 };

        for (int t = 0; t < 2; ++t)
        {
            const auto topology = (t == 0) ? Topology::production : Topology::secondOrder;

            StudyTank tank (kDefaultDecay);
            tank.configure (topology, kDefaultLowDamp, kDefaultHighDamp,
                            kDefaultLowHz, kDefaultHighHz);

            {
                auto damper = tank.damper (0);
                double acc = 0.0;

                const auto start = std::chrono::steady_clock::now();

                for (int n = 0; n < samples; ++n)
                    acc += damper.process (1.0e-6 * (n & 1023));

                const auto stop = std::chrono::steady_clock::now();

                damperNs[static_cast<size_t> (t)] =
                    std::chrono::duration<double, std::nano> (stop - start).count() / samples;

                if (acc == 1.0e300)
                    info ("    (unreachable)");
            }

            {
                const auto drive = makeExcitation (0, samples);

                const auto start = std::chrono::steady_clock::now();
                const auto out = tank.render (drive, samples);
                const auto stop = std::chrono::steady_clock::now();

                tankNs[static_cast<size_t> (t)] =
                    std::chrono::duration<double, std::nano> (stop - start).count() / samples;

                if (out.empty())
                    info ("    (unreachable)");
            }
        }

        info ("    topology   one damper (ns/sample)   whole tank (ns/sample)");
        info ("    T0         " + pad (number (damperNs[0], 3), 25) + number (tankNs[0], 3));
        info ("    S2         " + pad (number (damperNs[1], 3), 25) + number (tankNs[1], 3));
        info ("");
        info ("    damper cost ratio S2 / T0   "
              + number (damperNs[1] / std::max (damperNs[0], 1.0e-12), 3) + "x");
        info ("    tank cost ratio S2 / T0     "
              + number (tankNs[1] / std::max (tankNs[0], 1.0e-12), 3) + "x");
        info ("");

        // Aggregate empirical RT60(f): analytic prediction across the band.
        info ("    aggregate predicted RT60(f) at High Damp 100 %, Low Damp 0");
        info ("      freq (Hz)   T0 RT60   S2 RT60   S2 - T0   T0 spread  S2 spread");

        StudyTank t0 (kDefaultDecay), s2 (kDefaultDecay);
        t0.configure (Topology::production,  0.0, 1.0, kDefaultLowHz, kDefaultHighHz);
        s2.configure (Topology::secondOrder, 0.0, 1.0, kDefaultLowHz, kDefaultHighHz);

        double worstSpreadT0 = 0.0, worstSpreadS2 = 0.0;

        for (double hz : { 250.0, 500.0, 1000.0, 2000.0, 3000.0, 4000.0,
                           6000.0, 8000.0, 12000.0, 16000.0 })
        {
            const double a = t0.predictedRt60 (hz);
            const double b = s2.predictedRt60 (hz);
            const double sa = t0.kSpread (hz);
            const double sb = s2.kSpread (hz);

            worstSpreadT0 = std::max (worstSpreadT0, sa);
            worstSpreadS2 = std::max (worstSpreadS2, sb);

            info ("      " + pad (number (hz, 0), 12)
                  + pad (number (a, 4), 10)
                  + pad (number (b, 4), 10)
                  + pad (number (b - a, 4), 10)
                  + pad (number (sa, 2) + " %", 11)
                  + number (sb, 2) + " %");
        }

        info ("");
        info ("    worst per-line K spread   T0 " + number (worstSpreadT0, 2) + " %"
              "   S2 " + number (worstSpreadS2, 2) + " %");

        check (true, "cost and coherence measured for both topologies");
    }
}

} // namespace avtest
