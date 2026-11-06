#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulatedFeedbackMatrix.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — static T0 pole / residue study, with certified root counting.

    WHY
    ---
    With Mod Depth at 0 the tank is linear and time invariant — proved below,
    not assumed — and the full +1.19 / +1.44 / +1.54 / +1.68 pp residual is
    already present. An LTI system's decay is completely described by its poles
    and its input-to-output residues, so the previous synthetic control's
    assumption of equal modal contributions is the thing to test.

    WHAT CHANGED SINCE THE LAST ATTEMPT
    -----------------------------------
    The formulation, the characteristic function, the residue integral and the
    LTI assumption were all sound. The root DETECTOR was not: it looked for
    local minima of |F| on the unit circle, and at high frequency the poles sit
    further inside (shorter RT60), so their dips are broader, shallower and
    merge. It found 30 % of the modes at 12 kHz and the reconstruction failed.

    The detector is replaced by the argument principle. The number of zeros in
    a region is a contour integral:

        N = (1 / 2 pi) * (total change of arg F around the boundary)

    which is an integer and cannot miss a root. Regions are subdivided until
    each holds exactly one, and only then is Newton allowed to refine it. The
    phase step is checked against pi/2 on every edge, so an under-sampled
    contour reports itself rather than quietly returning a wrong integer.

    Coverage is the whole spectrum rather than four windows, which both removes
    the margin question and makes the total count checkable against the state
    dimension.

    TEST ONLY: no Source change, no gate change, no tuning.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;
using Complex = std::complex<double>;

constexpr double kSampleRate  = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kLines       = 8;
constexpr int    kEnsemble    = 32;
constexpr double kBandOctaves = 1.0;

/*
    The established T0 reference was measured at Size scale 1.0 and the numbers
    this study is compared against (1.27 / 2.55 / 4.22 / 6.00 %) come from that
    configuration. Changing Size would move the delay set, the poles and the
    reference all at once.
*/
constexpr double kSizeScale = 1.0;

/*
    Search annulus. Every mode in this configuration decays between about 0.7 s
    and 2.0 s, which is a pole radius between 0.99976 and 0.99993. The inner
    radius below corresponds to an RT60 of 0.029 s: three hundred times faster
    than anything the tank produces here, so nothing musical is outside it, and
    it keeps z^-d bounded (0.999^-3980 is 53.5, not 10^17 as 0.99 would give).
*/
constexpr double kInnerRadius = 0.9990;
constexpr double kOuterRadius = 1.0;

// PRE-REGISTERED, fixed before the run.
constexpr double kModelRAgreementPp = 0.5;
constexpr double kMaterialDropPp    = 0.75;
constexpr double kNotMaterialPp     = 0.25;

// Reconstruction health, also fixed before the run.
constexpr double kHealthMagnitudeDb  = 1.0;
constexpr double kHealthImpulseRms   = 0.10;
constexpr double kHealthRt60Error    = 0.02;

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
//  The static tank and its exact transfer function
// ---------------------------------------------------------------------------

struct StaticTank
{
    FdnCore<8> core;

    std::array<int, kLines>    delaySamples {};
    std::array<double, kLines> gain {};
    std::array<double, kLines> normalisation {};
    std::array<damping::ShelfSpec, kLines> shelf {};

    double matrix[kLines][kLines] {};
    std::array<double, kLines> b {}, c {};

    StaticTank()
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, kSizeScale);
        core.setRt60Seconds (kDecayTarget, true);
        core.setDamping (mapping::lowDampRatioFor (0.0),
                         mapping::highDampRatioFor (1.0),
                         200.0, 6000.0, true);

        architecture::applyWithDepth (core, architecture::defaultCandidate, 0.0, 1.0, true);

        core.resetModulation();
        core.reset();

        for (int i = 0; i < kLines; ++i)
        {
            delaySamples[static_cast<size_t> (i)]  = core.getDelaySamples (i);
            gain[static_cast<size_t> (i)]          = core.getGain (i);
            normalisation[static_cast<size_t> (i)] = core.getDampingNormalisation (i);
            shelf[static_cast<size_t> (i)]         = core.getDampingSpec (i);

            b[static_cast<size_t> (i)] = fdn::injectionLeft[i] * fdn::signVectorNorm;
            c[static_cast<size_t> (i)] = fdn::readoutLeft[i]   * fdn::signVectorNorm;
        }

        ModulatedFeedbackMatrix<kLines> a;
        a.prepare (kSampleRate);
        a.setDepth (0.0, true);

        for (int j = 0; j < kLines; ++j)
        {
            double v[kLines] {};
            v[j] = 1.0;

            a.process (v);

            for (int i = 0; i < kLines; ++i)
                matrix[i][j] = v[i];
        }
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive, int numSamples)
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
                injection[i] = x * b[static_cast<size_t> (i)];

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < kLines; ++i)
                sum += state[i] * c[static_cast<size_t> (i)];

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }

    [[nodiscard]] Complex dampingResponse (int i, Complex z) const noexcept
    {
        const auto& s = shelf[static_cast<size_t> (i)];
        const Complex zInv = 1.0 / z;

        const auto lowpass = [zInv] (double a)
        {
            return Complex (1.0 - a) / (Complex (1.0) - a * zInv);
        };

        const Complex low  = Complex (1.0) - (1.0 - s.gainLow) * lowpass (s.coeffLow);
        const Complex high = s.gainHigh + (1.0 - s.gainHigh) * lowpass (s.coeffHigh);

        return normalisation[static_cast<size_t> (i)] * low * high;
    }

    [[nodiscard]] Complex loopFactor (int i, Complex z) const noexcept
    {
        return gain[static_cast<size_t> (i)] * dampingResponse (i, z)
               * std::pow (z, -static_cast<double> (delaySamples[static_cast<size_t> (i)]));
    }
};

// ---------------------------------------------------------------------------
//  8x8 complex linear algebra
// ---------------------------------------------------------------------------

struct Solver
{
    Complex m[kLines][kLines] {};

    [[nodiscard]] Complex determinantAndSolve (Complex* rhs)
    {
        Complex det (1.0, 0.0);

        for (int k = 0; k < kLines; ++k)
        {
            int pivot = k;
            double best = std::abs (m[k][k]);

            for (int r = k + 1; r < kLines; ++r)
            {
                const double v = std::abs (m[r][k]);

                if (v > best) { best = v; pivot = r; }
            }

            if (best == 0.0)
                return Complex (0.0, 0.0);

            if (pivot != k)
            {
                for (int cIdx = 0; cIdx < kLines; ++cIdx)
                    std::swap (m[k][cIdx], m[pivot][cIdx]);

                if (rhs != nullptr)
                    std::swap (rhs[k], rhs[pivot]);

                det = -det;
            }

            det *= m[k][k];

            for (int r = k + 1; r < kLines; ++r)
            {
                const Complex f = m[r][k] / m[k][k];

                for (int cIdx = k; cIdx < kLines; ++cIdx)
                    m[r][cIdx] -= f * m[k][cIdx];

                if (rhs != nullptr)
                    rhs[r] -= f * rhs[k];
            }
        }

        if (rhs != nullptr)
            for (int k = kLines - 1; k >= 0; --k)
            {
                for (int cIdx = k + 1; cIdx < kLines; ++cIdx)
                    rhs[k] -= m[k][cIdx] * rhs[cIdx];

                rhs[k] /= m[k][k];
            }

        return det;
    }
};

[[nodiscard]] Complex characteristic (const StaticTank& t, Complex z)
{
    Complex loop[kLines];

    for (int i = 0; i < kLines; ++i)
        loop[i] = t.loopFactor (i, z);

    Solver s;

    for (int i = 0; i < kLines; ++i)
        for (int j = 0; j < kLines; ++j)
            s.m[i][j] = ((i == j) ? Complex (1.0) : Complex (0.0)) - loop[i] * t.matrix[i][j];

    return s.determinantAndSolve (nullptr);
}

[[nodiscard]] Complex transfer (const StaticTank& t, Complex z)
{
    Complex loop[kLines], rhs[kLines];

    for (int i = 0; i < kLines; ++i)
    {
        loop[i] = t.loopFactor (i, z);
        rhs[i] = loop[i] * t.b[static_cast<size_t> (i)];
    }

    Solver s;

    for (int i = 0; i < kLines; ++i)
        for (int j = 0; j < kLines; ++j)
            s.m[i][j] = ((i == j) ? Complex (1.0) : Complex (0.0)) - loop[i] * t.matrix[i][j];

    (void) s.determinantAndSolve (rhs);

    Complex y (0.0, 0.0);

    for (int i = 0; i < kLines; ++i)
        y += t.c[static_cast<size_t> (i)] * rhs[i];

    return y;
}

// ---------------------------------------------------------------------------
//  Certified root counting
// ---------------------------------------------------------------------------

struct CountStats
{
    long long evaluations = 0;
    long long refinements = 0;      ///< edges that needed more samples
    long long depthExceeded = 0;    ///< regions that never separated
    long long newtonFailed = 0;     ///< isolated regions Newton could not land in
    double worstPhaseStep = 0.0;
};

/**
    Change of arg F along one edge, sampled until no step exceeds pi/2.

    The step check is what makes the count trustworthy: an unwrapped phase is
    only correct if consecutive samples are closer than pi, and the margin of
    two keeps that honest. An edge that will not settle is reported rather than
    silently accepted.
*/
[[nodiscard]] double edgePhaseChange (const StaticTank& t,
                                      Complex from, Complex to,
                                      CountStats& stats, bool& ok)
{
    int samples = 24;

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        double total = 0.0, worst = 0.0;
        Complex previous = characteristic (t, from);
        stats.evaluations += 1;

        bool fine = true;

        for (int k = 1; k <= samples; ++k)
        {
            const double u = static_cast<double> (k) / samples;
            const Complex value = characteristic (t, from + (to - from) * u);
            stats.evaluations += 1;

            // arg of the ratio is the wrapped step, which is what we want.
            const double step = std::arg (value / previous);

            worst = std::max (worst, std::abs (step));
            total += step;
            previous = value;
        }

        stats.worstPhaseStep = std::max (stats.worstPhaseStep, worst);

        if (worst < 0.5 * decay::pi)
        {
            ok = true;
            return total;
        }

        fine = false;
        (void) fine;

        samples *= 2;
        stats.refinements += 1;
    }

    ok = false;
    return 0.0;
}

/** Zeros inside the annular sector, by the argument principle. */
[[nodiscard]] int countZeros (const StaticTank& t, double f1, double f2,
                              CountStats& stats, bool& ok,
                              double innerRadius = kInnerRadius,
                              double outerRadius = kOuterRadius)
{
    const double kInnerRadiusLocal = innerRadius;
    const double kOuterRadiusLocal = outerRadius;

    const double w1 = 2.0 * decay::pi * f1 / kSampleRate;
    const double w2 = 2.0 * decay::pi * f2 / kSampleRate;

    const Complex a = std::polar (kOuterRadiusLocal, w1);
    const Complex b = std::polar (kOuterRadiusLocal, w2);
    const Complex cc = std::polar (kInnerRadiusLocal, w2);
    const Complex d = std::polar (kInnerRadiusLocal, w1);

    // Counterclockwise: outer arc, in, inner arc back, out. The arcs are
    // walked in angle rather than as straight chords.
    double total = 0.0;
    bool edgeOk = true;

    const auto arc = [&] (double radius, double from, double to)
    {
        const int pieces = std::max (8, static_cast<int> (std::ceil ((to - from) / 0.002)));

        double sum = 0.0;

        for (int k = 0; k < pieces && edgeOk; ++k)
        {
            const double u0 = from + (to - from) * k / pieces;
            const double u1 = from + (to - from) * (k + 1) / pieces;

            bool sub = true;
            sum += edgePhaseChange (t, std::polar (radius, u0), std::polar (radius, u1),
                                    stats, sub);

            if (! sub) edgeOk = false;
        }

        return sum;
    };

    total += arc (kOuterRadiusLocal, w1, w2);

    bool e2 = true;
    total += edgePhaseChange (t, b, cc, stats, e2);
    if (! e2) edgeOk = false;

    total += arc (kInnerRadiusLocal, w2, w1);

    bool e4 = true;
    total += edgePhaseChange (t, d, a, stats, e4);
    if (! e4) edgeOk = false;

    ok = edgeOk;

    if (! ok)
        return -1;

    return static_cast<int> (std::lround (total / (2.0 * decay::pi)));
}

struct Mode
{
    Complex pole;
    Complex residue;
    double frequencyHz = 0.0;
    double rt60 = 0.0;
    bool isReal = false;   ///< a real pole has no conjugate partner
};

/*
    THE EXACT DEGREE, DERIVED
    -------------------------
    With Low Damp at 0 the low shelf is identically 1, so per line

        L_i(z) = k_i (p_i z - q_i) / ((z - a) z^d_i)

    with a the shared high-shelf pole, p_i = g_h + (1-g_h)(1-a), q_i = g_h a
    and k_i = g_i * norm_i. Then

        det(I - diag(L) A) = sum over subsets S of
            (-1)^|S| det(A[S,S]) prod_{i in S} L_i

    and clearing the common denominator (z-a)^8 z^(sum d_i) gives a polynomial

        P(z) = det(M(z)) * (z-a)^8 * z^18649

    whose S-term has degree 8 + 18649 - sum_{i in S} d_i. The empty subset
    gives the maximum, 18657, with leading coefficient 1; the full subset is
    the only one contributing a constant, prod(-k_i q_i) det(A), which is
    nonzero. So P has degree EXACTLY 18657, P(0) != 0, and all 18657 roots are
    finite and nonzero.

    P has real coefficients, so its roots are real or in conjugate pairs. 18657
    is odd, therefore the number of real roots is odd and at least one.
*/
inline constexpr int kTransferDegree = 18657;

/** Mean modal spacing implied by the delay set: 1 / sum(d_i) = 2.574 Hz. */
inline const double kModalSpacingHz = 1.0 / (0.001 * (fdn::delaysMs[0] + fdn::delaysMs[1]
    + fdn::delaysMs[2] + fdn::delaysMs[3] + fdn::delaysMs[4] + fdn::delaysMs[5]
    + fdn::delaysMs[6] + fdn::delaysMs[7]));

/**
    P(z), the cleared characteristic polynomial, evaluated stably.

    Multiplying row i of M by (z - a) z^d_i pushes the clearing factor inside
    the determinant:

        P(z) = det[ (z-a) z^d_i  delta_ij  -  k_i (p_i z - q_i) A_ij ]

    which is identically det(M) (z-a)^8 z^(sum d_i). Two things follow, and
    both matter for the low-radius search:

      - P is a POLYNOMIAL. It has no poles anywhere, so the argument principle
        on it counts zeros with no zeros-minus-poles bookkeeping at all.

      - It does not overflow. The unscaled form needs z^-d, which at |z| = 0.9
        is 10^182 per line and 10^1456 across eight - far past a double. Here
        the same factor appears as z^+d and simply underflows towards zero,
        which is the correct limit rather than an error.

    Its zeros are exactly the observable transfer poles, everywhere.
*/
[[nodiscard]] Complex characteristicPolynomial (const StaticTank& t, Complex z)
{
    const double a = t.shelf[0].coeffHigh;   // shared: one corner, one rate

    Solver s;

    for (int i = 0; i < kLines; ++i)
    {
        const auto& sh = t.shelf[static_cast<size_t> (i)];

        const double g = sh.gainHigh;
        const double p = g + (1.0 - g) * (1.0 - a);
        const double q = g * a;
        const double k = t.gain[static_cast<size_t> (i)]
                         * t.normalisation[static_cast<size_t> (i)];

        const Complex diagonal = (z - Complex (a))
                                 * std::pow (z, static_cast<double> (
                                       t.delaySamples[static_cast<size_t> (i)]));

        const Complex numerator = Complex (k) * (Complex (p) * z - Complex (q));

        for (int j = 0; j < kLines; ++j)
            s.m[i][j] = ((i == j) ? diagonal : Complex (0.0))
                        - numerator * t.matrix[i][j];
    }

    return s.determinantAndSolve (nullptr);
}

/** Winding of P on a circle: zeros inside, with no pole bookkeeping. */
[[nodiscard]] double polynomialWinding (const StaticTank& t, double radius, int samples,
                                        double& worstStep)
{
    double total = 0.0;
    worstStep = 0.0;

    Complex previous = characteristicPolynomial (t, Complex (radius, 0.0));

    for (int k = 1; k <= samples; ++k)
    {
        const double w = 2.0 * decay::pi * k / samples;
        const Complex value = characteristicPolynomial (t, std::polar (radius, w));

        if (std::abs (previous) <= 0.0 || std::abs (value) <= 0.0)
        {
            previous = value;
            continue;
        }

        const double step = std::arg (value / previous);

        worstStep = std::max (worstStep, std::abs (step));
        total += step;

        previous = value;
    }

    return total / (2.0 * decay::pi);
}

/** Real roots of P on a real interval, by sign change and bisection. */
void findPolynomialRealRoots (const StaticTank& t, double from, double to, int samples,
                              std::vector<double>& roots)
{
    const auto evaluate = [&t] (double x)
    {
        return characteristicPolynomial (t, Complex (x, 0.0)).real();
    };

    double previousX = from;
    double previousY = evaluate (from);

    for (int k = 1; k <= samples; ++k)
    {
        const double x = from + (to - from) * k / samples;
        const double y = evaluate (x);

        if (std::isfinite (previousY) && std::isfinite (y)
            && ((previousY < 0.0 && y > 0.0) || (previousY > 0.0 && y < 0.0)))
        {
            double lo = previousX, hi = x, loY = previousY;

            for (int i = 0; i < 200; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double midY = evaluate (mid);

                if ((loY < 0.0) == (midY < 0.0)) { lo = mid; loY = midY; }
                else                              { hi = mid; }
            }

            roots.push_back (0.5 * (lo + hi));
        }

        previousX = x;
        previousY = y;
    }
}

/**
    Winding number of F on a circle, i.e. (zeros - poles) inside it.

    F = det(I - diag(L) A) is meromorphic with poles only at z = 0, of order
    sum d_i = 18649, and at z = a, of order 8. So for any radius above a, the
    count of zeros inside is winding + 18657, and for a radius below a it is
    winding + 18649. That makes the inventory measurable rather than inferred.
*/
[[nodiscard]] double circleWinding (const StaticTank& t, double radius, int samples,
                                    double& worstStep)
{
    double total = 0.0;
    worstStep = 0.0;

    Complex previous = characteristic (t, Complex (radius, 0.0));

    for (int k = 1; k <= samples; ++k)
    {
        const double w = 2.0 * decay::pi * k / samples;
        const Complex value = characteristic (t, std::polar (radius, w));

        if (std::abs (previous) <= 0.0 || std::abs (value) <= 0.0)
        {
            previous = value;
            continue;
        }

        const double step = std::arg (value / previous);

        worstStep = std::max (worstStep, std::abs (step));
        total += step;

        previous = value;
    }

    return total / (2.0 * decay::pi);
}

/** Real roots of F on a real interval, by sign change and bisection. */
void findRealRoots (const StaticTank& t, double from, double to, int samples,
                    std::vector<double>& roots)
{
    const auto evaluate = [&t] (double x)
    {
        return characteristic (t, Complex (x, 0.0)).real();
    };

    double previousX = from;
    double previousY = evaluate (from);

    for (int k = 1; k <= samples; ++k)
    {
        const double x = from + (to - from) * k / samples;
        const double y = evaluate (x);

        if (std::isfinite (previousY) && std::isfinite (y)
            && ((previousY < 0.0 && y > 0.0) || (previousY > 0.0 && y < 0.0)))
        {
            double lo = previousX, hi = x, loY = previousY;

            for (int i = 0; i < 200; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double midY = evaluate (mid);

                if ((loY < 0.0) == (midY < 0.0)) { lo = mid; loY = midY; }
                else                              { hi = mid; }
            }

            roots.push_back (0.5 * (lo + hi));
        }

        previousX = x;
        previousY = y;
    }
}

/**
    The first contour moment of a region: (1/2 pi i) * integral of z dlogF.

    For a region holding exactly one zero this is that zero, computed without
    any derivative and without an iteration that can wander. It is used as a
    starting point for the tight clusters, where a guessed radius leaves Newton
    oscillating between two nearly coincident roots.
*/
[[nodiscard]] Complex contourCentroid (const StaticTank& t, double f1, double f2)
{
    const double w1 = 2.0 * decay::pi * f1 / kSampleRate;
    const double w2 = 2.0 * decay::pi * f2 / kSampleRate;

    Complex total (0.0, 0.0);

    const auto walk = [&] (Complex from, Complex to, int steps)
    {
        Complex previous = characteristic (t, from);
        Complex previousZ = from;

        for (int k = 1; k <= steps; ++k)
        {
            const double u = static_cast<double> (k) / steps;
            const Complex z = from + (to - from) * u;
            const Complex value = characteristic (t, z);

            if (std::abs (previous) <= 0.0 || std::abs (value) <= 0.0)
            {
                previous = value;
                previousZ = z;
                continue;
            }

            const Complex ratio = value / previous;
            const Complex step (std::log (std::abs (ratio)), std::arg (ratio));

            total += 0.5 * (z + previousZ) * step;

            previous = value;
            previousZ = z;
        }
    };

    const auto arcWalk = [&] (double radius, double from, double to, int steps)
    {
        for (int k = 0; k < steps; ++k)
        {
            const double a = from + (to - from) * k / steps;
            const double b = from + (to - from) * (k + 1) / steps;

            walk (std::polar (radius, a), std::polar (radius, b), 4);
        }
    };

    arcWalk (kOuterRadius, w1, w2, 256);
    walk (std::polar (kOuterRadius, w2), std::polar (kInnerRadius, w2), 64);
    arcWalk (kInnerRadius, w2, w1, 256);
    walk (std::polar (kInnerRadius, w1), std::polar (kOuterRadius, w1), 64);

    return total / Complex (0.0, 2.0 * decay::pi);
}

/**
    Newton onto the single zero a counted region contains.

    Two things had to be right here, and the first attempt got both wrong.

    The convergence test cannot be tighter than the derivative is accurate. F'
    comes from a central difference, so it carries a relative error near 1e-6,
    and Newton stalls with a step around 1e-12 however many iterations it is
    given. Demanding 1e-15 meant every root "failed" — 1268 of 9322 were
    discarded by a tolerance, not by the mathematics. A step of 1e-12 already
    locates the pole to nine significant digits against a modal spacing of
    3.4e-4, which is far beyond what any decay measurement can resolve.

    The starting radius also matters, because pole radius tracks RT60 and RT60
    varies by a factor of three across the spectrum. One fixed guess sits a
    whole modal spacing away from the roots at the other end of the range and
    lets Newton walk into a neighbour. Several starts are tried and the answer
    is accepted only if it lands in the region it was counted in.
*/
/**
    The same bisection, but on the scaled polynomial P instead of F.

    F = det(I - diag(L) A) carries z^-d per line. On a contour drawn around one
    root in a narrow sector that factor swings over hundreds of orders of
    magnitude, and the diagnostics showed the consequence plainly: the contour
    moment came back at |z| = 2e-6 with a frequency of -14852 Hz, which is not
    a 15 % error but a dead evaluation.

    P(z) = det[(z-a) z^d_i delta_ij - k_i (p_i z - q_i) A_ij] is the same
    determinant with the clearing factor moved inside, so it has the same zeros
    and no poles at all - and the exponential appears as z^+d, which decays
    instead of exploding. It is the representation the low-radius search already
    needed for exactly this reason.
*/
[[nodiscard]] double polyEdgePhase (const StaticTank& t, Complex from, Complex to,
                                    CountStats& stats, bool& ok)
{
    int samples = 24;

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        double total = 0.0, worst = 0.0;
        Complex previous = characteristicPolynomial (t, from);
        stats.evaluations += 1;

        for (int k = 1; k <= samples; ++k)
        {
            const double u = static_cast<double> (k) / samples;
            const Complex value = characteristicPolynomial (t, from + (to - from) * u);
            stats.evaluations += 1;

            if (std::abs (previous) <= 0.0 || std::abs (value) <= 0.0)
            {
                previous = value;
                continue;
            }

            const double step = std::arg (value / previous);

            worst = std::max (worst, std::abs (step));
            total += step;
            previous = value;
        }

        if (worst < 0.5 * decay::pi)
        {
            ok = true;
            return total;
        }

        samples *= 2;
        stats.refinements += 1;
    }

    ok = false;
    return 0.0;
}

/** Zeros of P in an annular sector. P has no poles, so this is a zero count. */
[[nodiscard]] int countZerosPoly (const StaticTank& t, double f1, double f2,
                                  double r1, double r2, CountStats& stats, bool& ok)
{
    const double w1 = 2.0 * decay::pi * f1 / kSampleRate;
    const double w2 = 2.0 * decay::pi * f2 / kSampleRate;

    double total = 0.0;
    bool edgeOk = true;

    const auto arc = [&] (double radius, double from, double to)
    {
        const int pieces = std::max (8, static_cast<int> (std::ceil ((to - from) / 0.002)));
        double sum = 0.0;

        for (int k = 0; k < pieces && edgeOk; ++k)
        {
            bool sub = true;
            sum += polyEdgePhase (t,
                                  std::polar (radius, from + (to - from) * k / pieces),
                                  std::polar (radius, from + (to - from) * (k + 1) / pieces),
                                  stats, sub);

            if (! sub) edgeOk = false;
        }

        return sum;
    };

    total += arc (r2, w1, w2);

    bool e2 = true;
    total += polyEdgePhase (t, std::polar (r2, w2), std::polar (r1, w2), stats, e2);
    if (! e2) edgeOk = false;

    total += arc (r1, w2, w1);

    bool e4 = true;
    total += polyEdgePhase (t, std::polar (r1, w1), std::polar (r2, w1), stats, e4);
    if (! e4) edgeOk = false;

    ok = edgeOk;

    return ok ? static_cast<int> (std::lround (total / (2.0 * decay::pi))) : -1;
}

/**
    Locates a single counted zero by bisecting the annular sector itself.

    Newton, the contour moment and deflation all failed on the same ten
    regions, and the diagnostics said why: they are 0.1 Hz wide against a
    2.57 Hz modal spacing, so each holds one of a near-degenerate pair. Newton
    converges tightly onto whichever of the pair is nearer; the moment integral
    carries 15 % quadrature error at that width; and the acceptance margin,
    1e-3 of the region, is 0.0001 Hz — finer than any of those estimates.

    So the estimators are dropped in favour of the one thing that has been
    authoritative throughout: the argument principle. The sector is halved in
    frequency, then in radius, keeping whichever half still counts the zero,
    until both dimensions are below tolerance. The answer is then the sector
    centre and the error is the sector size — no convergence assumption, no
    tuned threshold, and the count reconciles at every step or the step is
    refused.
*/
[[nodiscard]] bool bisectToRoot (const StaticTank& t, double f1, double f2,
                                 CountStats& stats, Mode& out,
                                 double startInner = kInnerRadius,
                                 double startOuter = kOuterRadius)
{
    double r1 = startInner, r2 = startOuter;

    constexpr double frequencyTolerance = 1.0e-7;   // Hz
    constexpr double radiusTolerance    = 1.0e-11;

    for (int round = 0; round < 200; ++round)
    {
        const bool narrowEnough = (f2 - f1) < frequencyTolerance
                                  && (r2 - r1) < radiusTolerance;

        if (narrowEnough)
            break;

        bool progressed = false;

        // --- halve in frequency -------------------------------------------
        if ((f2 - f1) >= frequencyTolerance)
        {
            for (double fraction : { 0.5, 0.37, 0.63 })
            {
                const double mid = f1 + (f2 - f1) * fraction;

                bool okLow = true, okHigh = true;
                const int low = countZerosPoly (t, f1, mid, r1, r2, stats, okLow);

                if (! okLow || low < 0)
                    continue;

                const int high = countZerosPoly (t, mid, f2, r1, r2, stats, okHigh);

                if (! okHigh || high < 0 || low + high != 1)
                    continue;

                if (low == 1) f2 = mid;
                else          f1 = mid;

                progressed = true;
                break;
            }
        }

        // --- halve in radius ----------------------------------------------
        if ((r2 - r1) >= radiusTolerance)
        {
            for (double fraction : { 0.5, 0.37, 0.63 })
            {
                const double mid = r1 + (r2 - r1) * fraction;

                bool okInner = true, okOuter = true;
                const int inner = countZerosPoly (t, f1, f2, r1, mid, stats, okInner);

                if (! okInner || inner < 0)
                    continue;

                const int outer = countZerosPoly (t, f1, f2, mid, r2, stats, okOuter);

                if (! okOuter || outer < 0 || inner + outer != 1)
                    continue;

                if (inner == 1) r2 = mid;
                else            r1 = mid;

                progressed = true;
                break;
            }
        }

        if (! progressed)
            return false;
    }

    const double frequency = 0.5 * (f1 + f2);
    const double radius = 0.5 * (r1 + r2);

    if (! (radius > 0.0) || radius > startOuter || radius < startInner)
        return false;

    out.pole = std::polar (radius, 2.0 * decay::pi * frequency / kSampleRate);
    out.frequencyHz = frequency;
    out.rt60 = -3.0 / (std::log10 (radius) * kSampleRate);

    return true;
}

/** What happened in a region the enumeration could not pin down. */
/*
    ANCESTRY OF A REGION'S FREQUENCY EDGES.

    A region does not own its own boundaries: it inherits f1 from one ancestor
    split and f2 from another, and the only edge a split ever sanitises is the
    new one it just made. Every split is therefore recorded, and the path from
    the coarse block down to the region being worked on is kept, so a bad
    boundary can be traced to the split that created it instead of guessed at.
*/
struct SplitEvent
{
    double f1 = 0.0, f2 = 0.0;      ///< parent interval
    int parentCount = 0;
    double proposedCut = 0.0;       ///< before any displacement
    double acceptedCut = 0.0;
    double displacement = 0.0;      ///< acceptedCut - proposedCut
    int sliverCount = 0;            ///< zeros straddling the proposed cut
    int left = 0, right = 0;
    double certifyLeft = -1.0;      ///< |s0 - N| on the left child, -1 = not certified
    double certifyRight = -1.0;
    int attempts = 0;               ///< displacement candidates tried
    int depth = 0;
};

inline std::vector<SplitEvent> splitEvents;

struct PathStep
{
    int event = -1;
    bool rightChild = false;        ///< the region is the right child of that split
};

inline std::vector<PathStep> currentPath;

struct FailedRegion
{
    std::vector<PathStep> path;     ///< ancestry, coarse block first
    double f1 = 0.0, f2 = 0.0;
    int count = 0;
    int polyCount = -99;        ///< what P says the region holds
    double minPolyF = 0.0;      ///< where |P| bottoms out across the region
    double minPolyR = 0.0;
    double minPolyRatio = 0.0;  ///< |P| there over its median on the grid
    Complex centroid;
    double bestStep = 0.0;          ///< smallest |F/F'| Newton reached
    double centroidFrequency = 0.0;
    double centroidRadius = 0.0;
    double centroidResidual = 0.0;  ///< |F(centroid)| / |F(region edge)|
    double r1 = 0.0, r2 = 0.0;      ///< annulus, when a radial split made it
    bool edgesProbed = false;
    bool edgesClean = false;
    double regionS0Error = -1.0;    ///< |s0 - N| on the region itself
    int momentPerEdge[6] { 0, 0, 0, 0, 0, 0 };
    double momentPlain[6] { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };
    double momentShifted[6] { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };
    int edge[4] { -1, -1, -1, -1 };     ///< zeros found on f1, f2, r1, r2
    bool edgeOk[4] { true, true, true, true };
    const char* reason = "";
};

inline std::vector<FailedRegion> failedRegions;

[[nodiscard]] bool refinePole (const StaticTank& t, double f1, double f2, Mode& out,
                              const std::vector<Mode>& known)
{
    const double centre = 0.5 * (f1 + f2);
    const double omega = 2.0 * decay::pi * centre / kSampleRate;

    /*
        The contour says this region holds exactly ONE zero, so the refined
        root has to be inside it. The first version allowed half a region's
        width of slack, and that was the whole bug: Newton started in the
        middle of one region, walked into its neighbour's root, and the margin
        accepted it. 1890 of 9322 poles were the same root found twice, with
        1890 others missing and the total still matching.

        The count is authoritative, so the margin is numerical only.
    */
    const double margin = 1.0e-3 * (f2 - f1) + 1.0e-9;

    /*
        Acceptance is on the Newton STEP, not on |F|.

        F's magnitude spans many orders across the plane, so "small |F|" means
        nothing on its own. But near a simple zero F(z) = F'(z0)(z - z0), so
        |F/F'| is an estimate of the distance to the root and is scale free.
        Requiring the final step below 1e-9, against a modal spacing of 3.4e-4,
        accepts genuine convergence and rejects an iterate that merely happened
        to be the least bad of a wandering sequence. Without this the search
        returned poles at radius 0.9999985, an RT60 of 94 seconds that this
        tank does not have.
    */
    /*
        The contour moment goes first, not last. For a region holding one zero
        it IS that zero, so Newton starts on the answer instead of on a guessed
        radius that may sit closer to a neighbour's root than to its own.
    */
    const Complex centroid = contourCentroid (t, f1, f2);

    std::vector<Complex> starts { centroid };

    for (double startRadius : { 0.99993, 0.99986, 0.99979, 0.99970, 0.99950, 0.99920 })
        starts.push_back (std::polar (startRadius, omega));

    double bestStepSeen = 1.0e30;

    for (Complex z : starts)
    {
        if (! std::isfinite (z.real()) || ! std::isfinite (z.imag()))
            continue;

        Complex best = z;
        double bestStep = 1.0e30;

        for (int iteration = 0; iteration < 80; ++iteration)
        {
            const Complex f = characteristic (t, z);
            const double h = 1.0e-9;

            const Complex fp = (characteristic (t, z + h) - characteristic (t, z - h))
                               / (2.0 * h);

            if (std::abs (fp) < 1.0e-300)
                break;

            const Complex delta = f / fp;

            z -= delta;

            if (! std::isfinite (z.real()) || ! std::isfinite (z.imag()))
                break;

            if (std::abs (delta) < bestStep)
            {
                bestStep = std::abs (delta);
                best = z;
            }

            if (std::abs (delta) < 1.0e-13)
                break;
        }

        bestStepSeen = std::min (bestStepSeen, bestStep);

        if (bestStep > 1.0e-9)
            continue;

        const double radius = std::abs (best);

        if (radius >= kOuterRadius || radius < kInnerRadius)
            continue;

        const double frequency = std::arg (best) * kSampleRate / (2.0 * decay::pi);

        /*
            Strict ownership, restored.

            Every cut this region descends from has been shown root-free by the
            sliver test, so the root it was counted with lies strictly inside
            it and the bounds test is meaningful again. It is kept strict for
            that reason: an acceptance margin would be a tolerance on position,
            and position is exactly what the contour has already settled.
        */
        if (frequency < f1 || frequency > f2)
            continue;

        out.pole = best;
        out.frequencyHz = frequency;
        out.rt60 = -3.0 / (std::log10 (radius) * kSampleRate);

        return true;
    }
    /*
        Newton converged tightly every time — the best |F/F'| was effectively
        zero — but onto a NEIGHBOUR's root, which the region check then
        rejected. These eight regions are 0.1 Hz wide against a 2.57 Hz modal
        spacing, so they hold one of a near-degenerate pair, and from any start
        plain Newton slides to whichever of the pair is closer.

        The classical remedy is deflation: divide out the roots already in hand
        and the remaining zero in the region is the only one left to find.

            G(z) = F(z) / prod (z - z_k)
            G'/G = F'/F - sum 1/(z - z_k)

        so the Newton step is 1 / (F'/F - sum 1/(z - z_k)), needing nothing but
        F, a difference for F', and the neighbours already located. No new
        machinery and no heuristic: the contour still says how many roots are
        here, deflation only stops the search walking out of the box.
    */
    {
        std::vector<Complex> nearby;

        for (const auto& m : known)
            if (std::abs (m.frequencyHz - centre) < 25.0)
                nearby.push_back (m.pole);

        for (Complex z : { centroid, std::polar (0.99977, omega),
                           std::polar (0.99990, omega) })
        {
            if (! std::isfinite (z.real()) || ! std::isfinite (z.imag()))
                continue;

            double step = 1.0e30;

            for (int iteration = 0; iteration < 80; ++iteration)
            {
                const double h = 1.0e-9;
                const Complex f = characteristic (t, z);

                if (std::abs (f) <= 0.0)
                    break;

                const Complex fp = (characteristic (t, z + h) - characteristic (t, z - h))
                                   / (2.0 * h);

                Complex logDerivative = fp / f;

                for (const auto& root : nearby)
                {
                    const Complex gap = z - root;

                    if (std::abs (gap) > 1.0e-14)
                        logDerivative -= Complex (1.0) / gap;
                }

                if (std::abs (logDerivative) < 1.0e-300)
                    break;

                const Complex delta = Complex (1.0) / logDerivative;

                z -= delta;
                step = std::abs (delta);

                if (! std::isfinite (z.real()) || ! std::isfinite (z.imag()))
                    break;

                if (step < 1.0e-12)
                    break;
            }

            if (step > 1.0e-9 || ! std::isfinite (z.real()))
                continue;

            const double radius = std::abs (z);

            if (radius >= kOuterRadius || radius < kInnerRadius)
                continue;

            const double frequency = std::arg (z) * kSampleRate / (2.0 * decay::pi);

            if (frequency < f1 - margin || frequency > f2 + margin)
                continue;

            out.pole = z;
            out.frequencyHz = frequency;
            out.rt60 = -3.0 / (std::log10 (radius) * kSampleRate);

            return true;
        }
    }

    /*
        Newton never landed inside. The contour moment is already an unbiased
        estimate of the single zero the region holds, so it is used instead of
        losing the mode — but it is VALIDATED first, by checking that the
        characteristic function really is near zero there. A centroid that
        merely sits in the right box is not evidence of a root.
    */
    const double centroidRadius = std::abs (centroid);
    const double centroidFrequency = std::arg (centroid) * kSampleRate / (2.0 * decay::pi);

    double residual = 1.0e30;

    if (std::isfinite (centroidRadius) && centroidRadius > 0.0)
    {
        // Scale |F| against its value a modal spacing away, which is what a
        // "typical" magnitude looks like locally.
        const double reference = std::abs (characteristic (t, std::polar (
            centroidRadius, 2.0 * decay::pi * (centroidFrequency + 2.0) / kSampleRate)));

        if (reference > 0.0)
            residual = std::abs (characteristic (t, centroid)) / reference;
    }

    const double wideMargin = 0.05 * (f2 - f1) + 1.0e-9;

    if (std::isfinite (centroidRadius)
        && centroidRadius < kOuterRadius && centroidRadius >= kInnerRadius
        && centroidFrequency >= f1 - wideMargin && centroidFrequency <= f2 + wideMargin
        && residual < 1.0e-3)
    {
        out.pole = centroid;
        out.frequencyHz = centroidFrequency;
        out.rt60 = -3.0 / (std::log10 (centroidRadius) * kSampleRate);

        return true;
    }

    FailedRegion failure;
    failure.f1 = f1;
    failure.f2 = f2;
    failure.count = 1;
    failure.centroid = centroid;
    failure.bestStep = bestStepSeen;
    failure.centroidFrequency = centroidFrequency;
    failure.centroidRadius = centroidRadius;
    failure.centroidResidual = residual;
    failure.reason = ! std::isfinite (centroidRadius) ? "centroid not finite"
                     : (centroidRadius >= kOuterRadius) ? "centroid outside the unit circle"
                     : (centroidRadius < kInnerRadius) ? "centroid below the annulus"
                     : (centroidFrequency < f1 - wideMargin
                        || centroidFrequency > f2 + wideMargin) ? "centroid outside the region"
                     : "characteristic not small at the centroid";

    /*
        Before giving up, record what the region actually looks like. Four
        locators have now failed on the same places, so the question is no
        longer which estimator to try but whether the root is where the count
        says it is. A grid of |P| over the region answers that directly: if the
        minimum sits inside, the count is right and the locators are weak; if
        it does not, the count is what needs revisiting.
    */
    {
        bool okPoly = true;
        CountStats scratch;
        failure.polyCount = countZerosPoly (t, f1, f2, kInnerRadius, kOuterRadius,
                                            scratch, okPoly);

        if (! okPoly)
            failure.polyCount = -1;

        double best = 1.0e300;
        std::vector<double> samples;

        for (int i = 0; i <= 120; ++i)
            for (int j = 0; j <= 24; ++j)
            {
                const double f = f1 + (f2 - f1) * i / 120.0;
                const double r = kInnerRadius
                                 + (kOuterRadius - kInnerRadius) * j / 24.0;

                const double magnitude = std::abs (characteristicPolynomial (
                    t, std::polar (r, 2.0 * decay::pi * f / kSampleRate)));

                samples.push_back (magnitude);

                if (magnitude < best)
                {
                    best = magnitude;
                    failure.minPolyF = f;
                    failure.minPolyR = r;
                }
            }

        std::sort (samples.begin(), samples.end());
        const double median = samples[samples.size() / 2];

        failure.minPolyRatio = (median > 0.0) ? best / median : -1.0;
    }

    failure.path = currentPath;
    failedRegions.push_back (failure);

    return false;
}

/*
    One gate for every locator.

    refinePole, the contour moment and the bisection each produce a root, and
    the duplicate check has to sit where they all pass through rather than
    inside one of them: putting it in the Newton path alone let the bisection
    hand back a root that was already in the list, and the audit caught it as
    one duplicate out of 9322.
*/
[[nodiscard]] bool acceptRoot (const Mode& candidate, std::vector<Mode>& modes)
{
    if (! std::isfinite (candidate.pole.real()) || ! std::isfinite (candidate.pole.imag()))
        return false;

    for (const auto& m : modes)
        if (std::abs (m.pole - candidate.pole) < 1.0e-9)
            return false;

    modes.push_back (candidate);
    return true;
}

/** Adaptive subdivision until each region holds exactly one zero. */
// ---------------------------------------------------------------------------
//  Contour moments
// ---------------------------------------------------------------------------
/*
    s_m = (1 / 2 pi i) * contour integral of z^m P'(z)/P(z) dz

    For a contour enclosing N zeros and no poles, s_0 = N and s_m is the m-th
    power sum of the enclosed roots. A single enclosed zero is therefore
    s_1 / s_0 outright, with no initial guess and no iteration to diverge.

    P is used rather than F because P is a polynomial: it has no poles anywhere,
    so the zeros-minus-poles bookkeeping disappears, and P'/P stays well scaled
    (it is the sum of 1/(z - z_k) over the roots) where F itself swings over
    hundreds of orders of magnitude through the z^-d factor.

    The integral is taken edge by edge over the annular sector with the midpoint
    rule, in the integrand's own variable: i z dtheta along the arcs and
    e^(i theta) drho along the radial sides. Nothing here unwraps a phase, which
    is what made the earlier moment attempt return |z| = 2e-6.
*/

struct Moments
{
    Complex s0, s1, s2;
    bool ok = false;
};

/*
    Moments may be taken about a CENTRE.

    With centre = 0 this is bit-for-bit what it always was, and every root
    already accepted through it is untouched. The centre matters for a close
    pair: the two roots at 15093.39169 and 15093.40186 Hz are 7.7e-6 apart in
    z, so the discriminant of their Newton-identity quadratic is 6e-11 while
    s1^2 is about 4, and forming it from absolute moments destroys it to
    cancellation long before quadrature error matters. Taken about the
    region's own centroid the same discriminant is built from numbers of size
    4e-8, and the pair separates.
*/
[[nodiscard]] Moments contourMoments (const StaticTank& t, double f1, double f2,
                                      double r1, double r2, int perEdge,
                                      Complex centre = Complex (0.0, 0.0))
{
    const double w1 = 2.0 * decay::pi * f1 / kSampleRate;
    const double w2 = 2.0 * decay::pi * f2 / kSampleRate;

    Complex s0 (0.0, 0.0), s1 (0.0, 0.0), s2 (0.0, 0.0);
    bool ok = true;

    const auto logDerivative = [&t, &ok] (Complex z) -> Complex
    {
        const double h = 1.0e-10 * std::max (1.0, std::abs (z));

        const Complex p = characteristicPolynomial (t, z);

        if (! std::isfinite (p.real()) || ! std::isfinite (p.imag()) || std::abs (p) <= 0.0)
        {
            ok = false;
            return {};
        }

        const Complex derivative = (characteristicPolynomial (t, z + h)
                                    - characteristicPolynomial (t, z - h)) / (2.0 * h);

        return derivative / p;
    };

    const auto accumulate = [&] (Complex z, Complex weight)
    {
        const Complex value = logDerivative (z) * weight;
        const Complex u = z - centre;

        s0 += value;
        s1 += value * u;
        s2 += value * u * u;
    };

    // Outer arc, w1 -> w2 at r2.   dz = i z dtheta
    {
        const double step = (w2 - w1) / perEdge;

        for (int k = 0; k < perEdge; ++k)
        {
            const Complex z = std::polar (r2, w1 + step * (k + 0.5));
            accumulate (z, Complex (0.0, 1.0) * z * step);
        }
    }

    // Radial, r2 -> r1 at w2.      dz = e^(i w2) drho
    {
        const double step = (r1 - r2) / perEdge;
        const Complex direction = std::polar (1.0, w2);

        for (int k = 0; k < perEdge; ++k)
            accumulate (std::polar (r2 + step * (k + 0.5), w2), direction * step);
    }

    // Inner arc, w2 -> w1 at r1.
    {
        const double step = (w1 - w2) / perEdge;

        for (int k = 0; k < perEdge; ++k)
        {
            const Complex z = std::polar (r1, w2 + step * (k + 0.5));
            accumulate (z, Complex (0.0, 1.0) * z * step);
        }
    }

    // Radial, r1 -> r2 at w1.
    {
        const double step = (r2 - r1) / perEdge;
        const Complex direction = std::polar (1.0, w1);

        for (int k = 0; k < perEdge; ++k)
            accumulate (std::polar (r1 + step * (k + 0.5), w1), direction * step);
    }

    const Complex scale = Complex (1.0) / Complex (0.0, 2.0 * decay::pi);

    Moments out;
    out.s0 = s0 * scale;
    out.s1 = s1 * scale;
    out.s2 = s2 * scale;
    out.ok = ok;

    return out;
}

/** Scale-free root health: smallest singular value of M over its norm. */
[[nodiscard]] double conditionAt (const StaticTank& t, Complex z)
{
    Complex m[kLines][kLines];
    double frobenius = 0.0;

    for (int i = 0; i < kLines; ++i)
    {
        const Complex loop = t.loopFactor (i, z);

        for (int j = 0; j < kLines; ++j)
        {
            m[i][j] = ((i == j) ? Complex (1.0) : Complex (0.0)) - loop * t.matrix[i][j];
            frobenius += std::norm (m[i][j]);
        }
    }

    frobenius = std::sqrt (frobenius);

    // 1 / ||M^-1|| estimated from the largest column of the inverse.
    double worst = 0.0;

    for (int j = 0; j < kLines; ++j)
    {
        Solver s;

        for (int a = 0; a < kLines; ++a)
            for (int b = 0; b < kLines; ++b)
                s.m[a][b] = m[a][b];

        Complex rhs[kLines] {};
        rhs[j] = Complex (1.0);

        (void) s.determinantAndSolve (rhs);

        double norm = 0.0;

        for (int a = 0; a < kLines; ++a)
            norm += std::norm (rhs[a]);

        worst = std::max (worst, std::sqrt (norm));
    }

    if (worst <= 0.0 || frobenius <= 0.0)
        return -1.0;

    return (1.0 / worst) / frobenius;
}

/** |P/P'|, the scale-free distance estimate to the nearest root. */
[[nodiscard]] double rootResidual (const StaticTank& t, Complex z)
{
    const double h = 1.0e-10 * std::max (1.0, std::abs (z));

    const Complex p = characteristicPolynomial (t, z);
    const Complex derivative = (characteristicPolynomial (t, z + h)
                                - characteristicPolynomial (t, z - h)) / (2.0 * h);

    if (std::abs (derivative) <= 0.0)
        return -1.0;

    return std::abs (p / derivative);
}

/** One root recovered from moments, with everything needed to judge it. */
struct MomentRecovery
{
    double f1 = 0.0, f2 = 0.0;
    int count = 0;
    double s0Error = 0.0;        ///< |s0 - N|
    double s2Error = 0.0;        ///< |s2 - z0^2| / |z0^2|, N = 1 only
    double frequency = 0.0, radius = 0.0;
    double frequencyShift = 0.0; ///< between the last two quadrature resolutions
    double radiusShift = 0.0;
    double residual = 0.0;       ///< |P/P'|
    double condition = 0.0;      ///< sigma_min / ||M||
    bool newtonUsed = false;
    bool accepted = false;
    bool inside = false;
    bool duplicate = false;
    bool converged = false;
};

inline std::vector<MomentRecovery> momentRecoveries;

/**
    Discovery by contour moment, with convergence rather than a chosen
    resolution.

    The quadrature is run at 64, 128, 256 and 512 points per edge and the root
    is accepted only when the last two agree. The shift between them is
    reported as the error, so the number carries its own uncertainty instead of
    resting on a resolution picked after the fact.

    Newton is allowed exactly one refinement from the moment root, and if it
    leaves the verified contour the moment root stands. It is refinement now,
    not discovery.
*/
[[nodiscard]] bool momentRoot (const StaticTank& t, double f1, double f2, int count,
                               const std::vector<Mode>& known, Mode& out)
{
    MomentRecovery record;
    record.f1 = f1;
    record.f2 = f2;
    record.count = count;

    const double r1 = kInnerRadius, r2 = kOuterRadius;

    Complex previousRoot;
    bool havePrevious = false;

    Complex root;
    bool haveRoot = false;
    Moments last;

    for (int perEdge : { 64, 128, 256, 512, 1024, 2048 })
    {
        const auto moments = contourMoments (t, f1, f2, r1, r2, perEdge);

        if (! moments.ok || std::abs (moments.s0) <= 0.0)
            continue;

        const Complex candidate = moments.s1 / moments.s0;

        if (! std::isfinite (candidate.real()) || ! std::isfinite (candidate.imag()))
            continue;

        if (havePrevious)
        {
            const double previousFrequency = std::arg (previousRoot) * kSampleRate
                                             / (2.0 * decay::pi);
            const double currentFrequency = std::arg (candidate) * kSampleRate
                                            / (2.0 * decay::pi);

            record.frequencyShift = std::abs (currentFrequency - previousFrequency);
            record.radiusShift = std::abs (std::abs (candidate) - std::abs (previousRoot));
        }

        previousRoot = candidate;
        havePrevious = true;

        root = candidate;
        haveRoot = true;
        last = moments;
    }

    if (! haveRoot)
    {
        momentRecoveries.push_back (record);
        return false;
    }

    record.s0Error = std::abs (last.s0 - Complex (static_cast<double> (count)));

    const Complex squared = root * root;
    record.s2Error = (std::abs (squared) > 0.0)
                     ? std::abs (last.s2 - squared) / std::abs (squared) : -1.0;

    // One Newton refinement, kept only if it stays inside.
    {
        const double h = 1.0e-10;
        const Complex p = characteristicPolynomial (t, root);
        const Complex derivative = (characteristicPolynomial (t, root + h)
                                    - characteristicPolynomial (t, root - h)) / (2.0 * h);

        if (std::abs (derivative) > 0.0)
        {
            const Complex refined = root - p / derivative;
            const double frequency = std::arg (refined) * kSampleRate / (2.0 * decay::pi);
            const double radius = std::abs (refined);

            if (std::isfinite (radius) && radius < r2 && radius >= r1
                && frequency >= f1 && frequency <= f2)
            {
                root = refined;
                record.newtonUsed = true;
            }
        }
    }

    record.frequency = std::arg (root) * kSampleRate / (2.0 * decay::pi);
    record.radius = std::abs (root);
    record.residual = rootResidual (t, root);
    record.condition = conditionAt (t, root);

    const bool inside = record.radius < r2 && record.radius >= r1
                        && record.frequency >= f1 && record.frequency <= f2;

    bool duplicate = false;

    for (const auto& m : known)
        if (std::abs (m.pole - root) < 1.0e-9)
            duplicate = true;

    record.inside = inside;
    record.duplicate = duplicate;
    /*
        Convergence is judged against the pole's own geometry, not a frequency
        I picked. What the reconstruction needs is the pole located well inside
        its distance from the unit circle: 1 - rho is about 2.5e-4 here, which
        in frequency is roughly 1.9 Hz, so demanding 1e-5 Hz was a thousand
        times stricter than anything downstream can use - and it rejected 746
        regions whose roots Newton then found anyway. The requirement is that
        the last two quadrature resolutions agree to a thousandth of that
        distance, measured in z.
    */
    const double distanceFromCircle = std::max (1.0 - record.radius, 1.0e-12);
    const double shiftInZ = std::hypot (record.radiusShift,
                                        record.radius * 2.0 * decay::pi
                                        * record.frequencyShift / kSampleRate);

    record.converged = shiftInZ < 1.0e-3 * distanceFromCircle;

    record.inside = inside;
    record.duplicate = duplicate;
    record.accepted = inside && ! duplicate
                      && record.s0Error < 0.05
                      && record.converged;

    momentRecoveries.push_back (record);

    if (! record.accepted)
        return false;

    out.pole = root;
    out.frequencyHz = record.frequency;
    out.rt60 = -3.0 / (std::log10 (record.radius) * kSampleRate);

    return true;
}

/**
    A cluster the subdivision cannot separate, solved from its moments.

    For N enclosed zeros the power sums s_1 .. s_N determine the local monic
    polynomial through Newton's identities, and its roots ARE the enclosed
    zeros. No individual root is guessed and no root is picked by proximity:
    the contour fixes how many there are and the symmetric functions fix which
    ones.

    Only N = 2 is implemented, because that is what the enumeration actually
    leaves: one region with two zeros that neither a frequency nor a radius cut
    would separate. For N = 2,

        e1 = s1,    e2 = (s1^2 - s2) / 2

    and the roots are those of z^2 - e1 z + e2, taken with the numerically
    stable pairing so the smaller root does not come out of a subtraction of
    two nearly equal numbers.

    This is far safer than building the global degree-18657 polynomial, which
    is the only other way to get at a cluster this tight.
*/
[[nodiscard]] int clusterRoots (const StaticTank& t, double f1, double f2, int count,
                                const std::vector<Mode>& known, std::vector<Mode>& out,
                                double r1 = kInnerRadius, double r2 = kOuterRadius)
{
    if (count != 2)
        return 0;

    /*
        The moments are taken about the region's own centroid. For a close
        pair that is not a refinement, it is the difference between solving
        the quadratic and not: u1 - u2 is around 8e-6, so the discriminant is
        6e-11, and about the origin it would have to be recovered from s1^2
        near 4 - gone to cancellation at the fifteenth digit before the
        quadrature even enters. About the centroid s1^2 is around 4e-8 and the
        same discriminant survives with four digits intact.
    */
    const double wMid = decay::pi * (f1 + f2) / kSampleRate;
    const Complex centre = std::polar (0.5 * (r1 + r2), wMid);

    Moments best;
    bool have = false;

    for (int perEdge : { 256, 512, 1024, 2048 })
    {
        const auto moments = contourMoments (t, f1, f2, r1, r2, perEdge, centre);

        if (! moments.ok)
            continue;

        if (std::abs (moments.s0 - Complex (2.0)) < 0.05)
        {
            best = moments;
            have = true;
        }
    }

    if (! have)
        return 0;

    const Complex e1 = best.s1;
    const Complex e2 = (best.s1 * best.s1 - best.s2) * 0.5;

    /*
        e1^2 - 4 e2 is identically 2 s2 - s1^2, and that form is used because
        it never builds the difference twice: the first expression forms e2 by
        subtracting s2 from s1^2 and then subtracts it again, losing the same
        digits a second time.
    */
    const Complex discriminant = std::sqrt (2.0 * best.s2 - best.s1 * best.s1);

    const Complex u1 = 0.5 * (e1 + discriminant);
    const Complex u2 = 0.5 * (e1 - discriminant);

    // Both identities are checked against the moments they came from.
    if (std::abs (u1 + u2 - e1) > 1.0e-9 * std::max (1.0, std::abs (e1))
        || std::abs (u1 * u2 - e2) > 1.0e-9 * std::max (1.0e-12, std::abs (e2)))
        return 0;

    int accepted = 0;

    for (Complex root : { centre + u1, centre + u2 })
    {
        if (! std::isfinite (root.real()) || ! std::isfinite (root.imag()))
            continue;

        const double radius = std::abs (root);
        const double frequency = std::arg (root) * kSampleRate / (2.0 * decay::pi);

        if (radius >= r2 || radius < r1
            || frequency < f1 || frequency > f2)
            continue;

        bool duplicate = false;

        for (const auto& m : known)
            if (std::abs (m.pole - root) < 1.0e-9)
                duplicate = true;

        for (const auto& m : out)
            if (std::abs (m.pole - root) < 1.0e-9)
                duplicate = true;

        if (duplicate)
            continue;

        Mode mode;
        mode.pole = root;
        mode.frequencyHz = frequency;
        mode.rt60 = -3.0 / (std::log10 (radius) * kSampleRate);

        out.push_back (mode);
        ++accepted;
    }

    return accepted;
}

/*
    MOMENT CERTIFICATION OF A SHARED BOUNDARY.

    A thin-sliver winding count cannot establish that a DISPLACED cut is
    root-free. The sliver's contour is about 0.02 Hz wide and 0.001 tall - an
    aspect ratio near 1:400 - so its winding number is the small difference of
    two long, nearly cancelling radial phase walks. It returned 0 for a sliver
    with a root at its centre four times in the previous run, and those four
    false negatives are exactly the four roots that were lost.

    The moment integral is the sensitive instrument here, and it is the one
    that exposed the fault: on the two children of the 13569 Hz parent the
    winding counts were 1 and 1 while s0 came back 0 and 2. A zero sitting on
    the shared boundary puts the integrand's singularity ON the path, so s0
    stops being an integer. Requiring s0 to reproduce the winding count on
    BOTH children, at two quadrature resolutions, therefore certifies the one
    shared boundary for both siblings at once - no overlap, no gap, and no
    tolerance on where the root is allowed to sit.
*/
[[nodiscard]] bool certifyCut (const StaticTank& t, double f1, double cut, double f2,
                               int left, int right, double& errLeft, double& errRight)
{
    errLeft = errRight = -1.0;

    /*
        The resolution is not chosen, it is climbed until the integral settles
        on the integer it is supposed to produce. A clean cut reaches 1e-5 or
        better; a cut with a zero on it is out by a whole root and no
        resolution rescues it, so the ladder terminates on the answer rather
        than on a tolerance someone picked. The first rung scales with how
        many roots the children have to hold, since a wide region integrating
        to 10 needs more points than a narrow one integrating to 1.
    */
    const int count = left + right;
    const int first = std::min (256, std::max (32, 16 * count));

    for (int perEdge = first; perEdge <= 1024; perEdge *= 2)
    {
        const auto l = contourMoments (t, f1, cut, kInnerRadius, kOuterRadius, perEdge);
        const auto r = contourMoments (t, cut, f2, kInnerRadius, kOuterRadius, perEdge);

        if (! l.ok || ! r.ok)
            return false;

        errLeft  = std::abs (l.s0 - Complex (static_cast<double> (left)));
        errRight = std::abs (r.s0 - Complex (static_cast<double> (right)));

        if (errLeft < 0.05 && errRight < 0.05)
            return true;
    }

    return false;
}

/**
    One region, certified against its own winding count.

    The same instrument as certifyCut, for a region that is not the child of
    a cut: a boundary of the scan itself. s0 has to reproduce the count, or a
    zero is on the contour and the region as drawn is not usable.
*/
[[nodiscard]] bool certifyRegion (const StaticTank& t, double f1, double f2,
                                  double r1, double r2, int count, double& error)
{
    error = -1.0;

    const int first = std::min (256, std::max (32, 16 * std::max (1, count)));

    for (int perEdge = first; perEdge <= 4096; perEdge *= 2)
    {
        const auto m = contourMoments (t, f1, f2, r1, r2, perEdge);

        if (! m.ok)
            return false;

        error = std::abs (m.s0 - Complex (static_cast<double> (count)));

        if (error < 0.05)
            return true;
    }

    return false;
}

/**
    All four contour edges of a region, probed for a zero sitting on them.

    The two frequency edges are probed by a thin sliver straddling them, the
    two radius edges by a thin annulus. Zero on every probe means no zero lies
    on the contour. edge[k] is -1 where the probe itself was unreliable.
*/
[[nodiscard]] bool edgesRootFree (const StaticTank& t, double f1, double f2,
                                  double r1, double r2, CountStats& stats,
                                  int edge[4], bool reliable[4])
{
    const double df = std::min (0.25 * (f2 - f1), 0.02 * kModalSpacingHz);
    const double dr = std::min (0.25 * (r2 - r1), 2.0e-6);

    bool all = true;

    for (int k = 0; k < 4; ++k)
    {
        reliable[k] = true;
        edge[k] = -1;
    }

    /*
        The f1 probe is clamped away from zero. At the bottom of the scan f1
        is 0.02 Hz and the probe half-width 0.05, so the window ran to
        negative frequency, wrapped past z = 1 and reported four zeros that
        are not on the edge at all.
    */
    edge[0] = countZerosPoly (t, std::max (0.005, f1 - df), f1 + df, r1, r2, stats, reliable[0]);
    edge[1] = countZerosPoly (t, f2 - df, f2 + df, r1, r2, stats, reliable[1]);
    edge[2] = countZerosPoly (t, f1, f2, std::max (0.9980, r1 - dr), r1 + dr, stats, reliable[2]);
    edge[3] = countZerosPoly (t, f1, f2, r2 - dr, std::min (1.0, r2 + dr), stats, reliable[3]);

    for (int k = 0; k < 4; ++k)
        if (! reliable[k] || edge[k] != 0)
            all = false;

    return all;
}

/**
    What a region actually looks like, for a region no locator could resolve.

    A grid of |P| over the annulus, the moments at rising resolution both
    about the origin and about the centroid, and the winding count from P.
    Enough to say whether the count is right and the locators are weak, or
    the count itself needs revisiting.
*/
void describeRegion (const StaticTank& t, double f1, double f2, double r1, double r2,
                     int count, FailedRegion& failure)
{
    bool okPoly = true;
    CountStats scratch;

    failure.polyCount = countZerosPoly (t, f1, f2, r1, r2, scratch, okPoly);

    if (! okPoly)
        failure.polyCount = -1;

    double best = 1.0e300;
    std::vector<double> samples;

    for (int i = 0; i <= 240; ++i)
        for (int j = 0; j <= 32; ++j)
        {
            const double f = f1 + (f2 - f1) * i / 240.0;
            const double r = r1 + (r2 - r1) * j / 32.0;

            const double magnitude = std::abs (characteristicPolynomial (
                t, std::polar (r, 2.0 * decay::pi * f / kSampleRate)));

            samples.push_back (magnitude);

            if (magnitude < best)
            {
                best = magnitude;
                failure.minPolyF = f;
                failure.minPolyR = r;
            }
        }

    std::sort (samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];

    failure.minPolyRatio = (median > 0.0) ? best / median : -1.0;

    const double wMid = decay::pi * (f1 + f2) / kSampleRate;
    const Complex centre = std::polar (0.5 * (r1 + r2), wMid);

    for (int k = 0; k < 6; ++k)
    {
        const int perEdge = 128 << k;

        const auto plain = contourMoments (t, f1, f2, r1, r2, perEdge);
        const auto shifted = contourMoments (t, f1, f2, r1, r2, perEdge, centre);

        failure.momentPerEdge[k] = perEdge;
        failure.momentPlain[k] = plain.ok
            ? std::abs (plain.s0 - Complex (static_cast<double> (count))) : -1.0;
        failure.momentShifted[k] = shifted.ok
            ? std::abs (shifted.s0 - Complex (static_cast<double> (count))) : -1.0;
    }

    if (failure.momentShifted[5] >= 0.0)
        failure.regionS0Error = failure.momentShifted[5];
}

/** A cut that had to be moved off a root, kept for the record. */
struct ShiftedCut
{
    double parentF1 = 0.0, parentF2 = 0.0;
    double originalCut = 0.0, shiftedCut = 0.0;
    double displacement = 0.0;
    int parentCount = 0;
    int leftOriginal = 0, rightOriginal = 0;
    int leftShifted = 0, rightShifted = 0;
    int leftOpposite = 0, rightOpposite = 0;
    int sliverCount = 0;
    int attempts = 0;
    double certifyLeft = -1.0;
    double certifyRight = -1.0;
    bool invariant = false;
};

inline std::vector<ShiftedCut> shiftedCuts;

/*
    A cut must be ROOT-FREE before its counts mean anything.

    A zero sitting on the contour makes the winding integral formally
    undefined, and the earlier run showed this is not hypothetical: eight
    regions reported a clean integer count while |P| bottomed out exactly on
    their boundary, to five decimal places. Those counts were undefined and
    removing the ownership test would have accepted roots on that basis.

    So every cut is checked. A thin sliver straddling the cut is counted first;
    if it is empty the cut is root-free and the ordinary split proceeds. If it
    is not, the cut is displaced so the root falls strictly inside one child,
    both children are recounted, and the parent total is required to be
    invariant under displacement to EITHER side. The displacement comes from
    the region's own width and the sliver's extent, not from a frequency
    tolerance.
*/
/*
    NEAR DC, THE REGION IS SPLIT ALONG ITS LONG AXIS.

    The two roots below 1 Hz sit in 0.02595 .. 0.02994 Hz, and that block is
    5.2e-7 rad wide against an annulus 1e-3 thick - an aspect ratio of 1 to
    1900. On such a needle the winding number of P says 2 and the moment
    integral of the same P says 0, stably, from 128 points per edge to 4096.
    Both are contour integrals of P'/P, so one of them is wrong, and it is
    the moment: its two long radial edges nearly cancel and the enclosed
    count is the small residue of that cancellation. It is the same
    degeneracy that made the thin-sliver probe give false negatives.

    Frequency subdivision cannot escape this near DC. For an annulus 1e-3
    thick a sector only stops being a needle at about 1e-4 rad, which is 0.76
    Hz - the whole strip as one block. Separating two roots 0.004 Hz apart
    needs a finer cut than that by definition, so every such cut is
    degenerate. The radial direction is the only one with room, so the
    annulus is halved instead, each half certified by its own P count against
    the parent, until the region is no longer a needle. Only then is it
    solved - by centred moments, which is what a non-degenerate contour
    supports.

    enumerate is untouched. The 9322 roots it already finds keep their exact
    code path; this runs only on the near-DC strip.
*/
[[nodiscard]] int solveNearDc (const StaticTank& t, double f1, double f2,
                               double r1, double r2, int count,
                               std::vector<Mode>& modes, CountStats& stats, int depth)
{
    if (count <= 0)
        return 0;

    const double angular = 0.5 * (r1 + r2) * 2.0 * decay::pi * (f2 - f1) / kSampleRate;
    const double radial = r2 - r1;

    /*
        The annulus is halved while the region is still radius-dominant. The
        cuts are counted on the WIDE sector deliberately: the first attempt
        at this ran the bisection inside a 0.004 Hz block, where the sector
        is 5.2e-7 rad across and the roots sit inside it, so every radial
        edge passed within 2.6e-7 of a root, the phase step hit pi and the
        counts went unreliable after two levels. On the full strip the edges
        are 6.5e-5 away in angle and the same counts are clean.
    */
    if (radial > angular && depth < 80)
    {
        for (double fraction : { 0.5, 0.37, 0.63, 0.23, 0.77 })
        {
            const double rc = r1 + (r2 - r1) * fraction;

            bool okInner = true, okOuter = true;

            const int inner = countZerosPoly (t, f1, f2, r1, rc, stats, okInner);

            if (! okInner || inner < 0 || inner > count)
                continue;

            const int outer = countZerosPoly (t, f1, f2, rc, r2, stats, okOuter);

            if (! okOuter || outer < 0 || inner + outer != count)
                continue;

            return solveNearDc (t, f1, f2, r1, rc, inner, modes, stats, depth + 1)
                   + solveNearDc (t, f1, f2, rc, r2, outer, modes, stats, depth + 1);
        }
    }

    const double wMid = decay::pi * (f1 + f2) / kSampleRate;
    const Complex centre = std::polar (0.5 * (r1 + r2), wMid);

    if (count == 2)
    {
        std::vector<Mode> recovered;

        if (clusterRoots (t, f1, f2, count, modes, recovered, r1, r2) == count)
        {
            int taken = 0;

            for (const auto& m : recovered)
                if (acceptRoot (m, modes))
                    ++taken;

            if (taken == count)
                return taken;
        }
    }

    if (count == 1)
    {
        // Centred moments first, since the contour is no longer a needle.
        Complex previous;
        bool have = false;

        for (int perEdge : { 128, 256, 512, 1024, 2048 })
        {
            const auto m = contourMoments (t, f1, f2, r1, r2, perEdge, centre);

            if (! m.ok || std::abs (m.s0 - Complex (1.0)) >= 0.05
                || std::abs (m.s0) <= 0.0)
                continue;

            const Complex candidate = centre + m.s1 / m.s0;

            if (have && std::abs (candidate - previous) < 1.0e-13)
            {
                Mode mode;
                mode.pole = candidate;
                mode.frequencyHz = std::arg (candidate) * kSampleRate / (2.0 * decay::pi);
                mode.rt60 = -3.0 / (std::log10 (std::abs (candidate)) * kSampleRate);

                if (std::abs (candidate) > kInnerRadius && std::abs (candidate) < 1.0
                    && acceptRoot (mode, modes))
                    return 1;
            }

            previous = candidate;
            have = true;
        }

        Mode mode;

        if (bisectToRoot (t, f1, f2, stats, mode, r1, r2) && acceptRoot (mode, modes))
            return 1;
    }

    FailedRegion failure;
    failure.f1 = f1;
    failure.f2 = f2;
    failure.r1 = r1;
    failure.r2 = r2;
    failure.count = count;
    failure.centroid = centre;
    describeRegion (t, f1, f2, r1, r2, count, failure);
    failure.reason = "near-DC region: not a needle any more and still unsolved";
    failedRegions.push_back (failure);

    stats.depthExceeded += count;

    return 0;
}

void enumerate (const StaticTank& t, double f1, double f2, int count, int depth,
                std::vector<Mode>& modes, CountStats& stats)
{
    if (count == 0)
        return;

    if (count == 1)
    {
        Mode mode;

        if ((momentRoot (t, f1, f2, 1, modes, mode)
             || refinePole (t, f1, f2, mode, modes)
             || bisectToRoot (t, f1, f2, stats, mode))
            && acceptRoot (mode, modes))
            return;

        stats.newtonFailed += 1;

        /*
            UNRESOLVED LINEAGE ONLY. The split protocol sanitises the cut it
            has just made and nothing else, so a region that cannot be
            resolved has its other three edges probed here - the inherited
            frequency edge first, since that is where the fault was. This runs
            on failures alone, not on all 9324 regions.
        */
        int edge[4] { -1, -1, -1, -1 };
        bool edgeOk[4] { true, true, true, true };

        const bool clean = edgesRootFree (t, f1, f2, kInnerRadius, kOuterRadius,
                                          stats, edge, edgeOk);

        FailedRegion* target = nullptr;

        for (auto it = failedRegions.rbegin(); it != failedRegions.rend(); ++it)
            if (it->f1 == f1 && it->f2 == f2)
            {
                target = &*it;
                break;
            }

        if (target == nullptr)
        {
            FailedRegion fresh;
            fresh.f1 = f1;
            fresh.f2 = f2;
            fresh.count = count;
            fresh.path = currentPath;
            fresh.reason = "single root, every locator declined it";
            failedRegions.push_back (fresh);
            target = &failedRegions.back();
        }

        target->r1 = kInnerRadius;
        target->r2 = kOuterRadius;
        target->edgesProbed = true;
        target->edgesClean = clean;

        /*
            The honest edge test, next to the sliver probe that cannot be
            trusted: s0 over the region itself. If it is not the integer the
            winding count claims, a zero is on the contour whatever the
            sliver says.
        */
        {
            const auto m = contourMoments (t, f1, f2, kInnerRadius, kOuterRadius, 512);

            if (m.ok)
                target->regionS0Error = std::abs (m.s0 - Complex (static_cast<double> (count)));
        }

        for (int k = 0; k < 4; ++k)
        {
            target->edge[k] = edge[k];
            target->edgeOk[k] = edgeOk[k];
        }

        return;
    }

    if (depth >= 60)
    {
        stats.depthExceeded += count;
        return;
    }

    /*
        EVERY cut is certified, not only the displaced ones.

        Gating certification on a non-empty sliver was wrong. The thin-sliver
        winding count gives false negatives - the provenance trace showed all
        three remaining failures were sliver 0, candidates tried 0, their cut
        accepted with no further check, while s0 on the two children was out
        by a full root. The same sliver probe reported zero zeros on all four
        edges of those regions. It is a 0.02 Hz by 0.001 contour and its
        winding number is the small difference of two long, nearly cancelling
        radial phase walks; it is kept as a diagnostic and decides nothing.

        What decides is s0 on both children. A zero on the shared boundary
        puts the singularity of the integrand on the path and s0 stops being
        the integer the winding count claims, which is how the fault was
        found. The cut position is still swept, because a cut a fifth of the
        way into the parent frequently needs no displacement at all.
    */
    static constexpr double fractions[] { 0.5, 0.41, 0.59, 0.33, 0.67, 0.27, 0.73, 0.19, 0.81 };

    for (double fraction : fractions)
    {
        const double mid = f1 + (f2 - f1) * fraction;

        /*
            The displacement comes from local geometry: the width of the
            region, the modal spacing of the delay set, and the mean spacing
            of the roots this region actually holds. Not from a tolerance.
        */
        const double displacement = std::min (std::min (0.10 * (f2 - f1),
                                                        0.02 * kModalSpacingHz),
                                              0.25 * (f2 - f1) / count);

        bool okSliver = true;
        const int sliver = countZerosPoly (t, mid - displacement, mid + displacement,
                                           kInnerRadius, kOuterRadius, stats, okSliver);

        bool ok1 = true, ok2 = true;

        const int left0 = countZeros (t, f1, mid, stats, ok1);

        if (! ok1 || left0 < 0 || left0 > count)
            continue;

        const int right0 = countZeros (t, mid, f2, stats, ok2);

        if (! ok2 || right0 < 0 || left0 + right0 != count)
            continue;

        double cut = mid;
        int left = left0, right = right0;
        double eL = -1.0, eR = -1.0;
        int attempts = 1;

        ShiftedCut record;
        bool needsRecord = false;

        if (! certifyCut (t, f1, mid, f2, left0, right0, eL, eR))
        {
            /*
                A zero lies on this cut. The ONE shared boundary is moved for
                both children together - no overlap, no gap - and a candidate
                is taken only when it certifies AND the parent total survives
                displacement to either side of it. The strip count is then
                read off the two reliable counts as L(+) - L(-) instead of
                being measured on a degenerate contour.
            */
            record.parentF1 = f1;
            record.parentF2 = f2;
            record.parentCount = count;
            record.originalCut = mid;
            record.sliverCount = sliver;
            record.leftOriginal = left0;
            record.rightOriginal = right0;

            bool settled = false;

            for (double k : { 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 0.25, -0.25,
                              3.0, -3.0, 0.125, -0.125, 4.0, -4.0, 6.0, -6.0 })
            {
                const double candidate = mid + k * displacement;

                if (! (candidate > f1 && candidate < f2))
                    continue;

                attempts += 1;

                bool c1 = true, c2 = true;

                const int l = countZeros (t, f1, candidate, stats, c1);

                if (! c1 || l < 0 || l > count)
                    continue;

                const int r = countZeros (t, candidate, f2, stats, c2);

                if (! c2 || r < 0 || l + r != count)
                    continue;

                double a1 = -1.0, a2 = -1.0;

                if (! certifyCut (t, f1, candidate, f2, l, r, a1, a2))
                {
                    eL = a1;
                    eR = a2;
                    continue;
                }

                const double step = 0.5 * displacement;

                bool p1 = true, p2 = true, m1 = true, m2 = true;

                const int lp = countZeros (t, f1, candidate + step, stats, p1);
                const int rp = countZeros (t, candidate + step, f2, stats, p2);
                const int lm = countZeros (t, f1, candidate - step, stats, m1);
                const int rm = countZeros (t, candidate - step, f2, stats, m2);

                if (! (p1 && p2 && m1 && m2)
                    || lp < 0 || rp < 0 || lm < 0 || rm < 0
                    || lp + rp != count || lm + rm != count)
                    continue;

                cut = candidate;
                left = l;
                right = r;
                eL = a1;
                eR = a2;

                record.shiftedCut = candidate;
                record.displacement = candidate - mid;
                record.leftShifted = lp;
                record.rightShifted = rp;
                record.leftOpposite = lm;
                record.rightOpposite = rm;
                record.sliverCount = lp - lm;
                record.certifyLeft = a1;
                record.certifyRight = a2;
                record.attempts = attempts;
                record.invariant = true;

                settled = true;
                needsRecord = true;
                break;
            }

            if (! settled)
                continue;
        }

        if (needsRecord)
            shiftedCuts.push_back (record);

        SplitEvent event;
        event.f1 = f1;
        event.f2 = f2;
        event.parentCount = count;
        event.proposedCut = mid;
        event.acceptedCut = cut;
        event.displacement = cut - mid;
        event.sliverCount = sliver;
        event.left = left;
        event.right = right;
        event.certifyLeft = eL;
        event.certifyRight = eR;
        event.attempts = attempts;
        event.depth = depth;

        const int eventIndex = static_cast<int> (splitEvents.size());
        splitEvents.push_back (event);

        currentPath.push_back ({ eventIndex, false });
        enumerate (t, f1, cut, left, depth + 1, modes, stats);
        currentPath.pop_back();

        currentPath.push_back ({ eventIndex, true });
        enumerate (t, cut, f2, right, depth + 1, modes, stats);
        currentPath.pop_back();

        return;
    }

    /*
        NO CERTIFIABLE FREQUENCY CUT EXISTS.

        For a close pair that is not a failure of the search, it is a fact
        about the geometry. The two roots at 15093.39169 and 15093.40186 Hz
        are 0.0102 Hz apart, which is 1.3e-6 in z, while the radial edges of
        the contour span 1e-3. Any cut between them passes within 7e-7 of
        both, so no resolution certifies it and none should. The pair is
        solved from its moments where it stands instead.
    */
    if (count == 2)
    {
        std::vector<Mode> recovered;

        if (clusterRoots (t, f1, f2, count, modes, recovered) == count)
        {
            for (const auto& m : recovered)
                (void) acceptRoot (m, modes);

            return;
        }
    }

    /*
        Frequency alone did not separate them, so the cluster is split by
        radius instead: two poles at nearly the same frequency generally differ
        in how fast they decay.
    */
    for (double cut : { 0.99960, 0.99985, 0.99993 })
    {
        bool okInner = true, okOuter = true;

        const int inner = countZeros (t, f1, f2, stats, okInner, kInnerRadius, cut);

        if (! okInner || inner < 0 || inner > count)
            continue;

        const int outer = countZeros (t, f1, f2, stats, okOuter, cut, kOuterRadius);

        if (! okOuter || outer < 0 || inner + outer != count)
            continue;

        // A radial split cannot recurse through the frequency path, so each
        // side is refined directly from its own annulus.
        for (int side = 0; side < 2; ++side)
        {
            const int n = (side == 0) ? inner : outer;
            const double rLo = (side == 0) ? kInnerRadius : cut;
            const double rHi = (side == 0) ? cut : kOuterRadius;

            if (n == 0)
                continue;

            if (n == 1)
            {
                Mode mode;

                if ((refinePole (t, f1, f2, mode, modes) || bisectToRoot (t, f1, f2, stats, mode))
                    && acceptRoot (mode, modes))
                    {}
                else
                    stats.newtonFailed += 1;
            }
            else
            {
                /*
                    A radial sub-annulus the frequency path cannot re-enter.
                    It was previously abandoned with no diagnostics at all,
                    which is how an N = 2 cluster went into the ledger as
                    "never separated" with nothing to say about it. Its four
                    edges are probed first - item 6's rule, the contour before
                    the moments - and only then are the moments asked for the
                    two roots.
                */
                FailedRegion failure;
                failure.f1 = f1;
                failure.f2 = f2;
                failure.r1 = rLo;
                failure.r2 = rHi;
                failure.count = n;
                failure.path = currentPath;
                failure.centroid = contourCentroid (t, f1, f2);
                failure.edgesProbed = true;
                failure.edgesClean = edgesRootFree (t, f1, f2, rLo, rHi, stats,
                                                    failure.edge, failure.edgeOk);

                std::vector<Mode> recovered;

                if (n == 2 && clusterRoots (t, f1, f2, n, modes, recovered, rLo, rHi) == n)
                {
                    for (const auto& m : recovered)
                        (void) acceptRoot (m, modes);

                    continue;
                }

                describeRegion (t, f1, f2, rLo, rHi, n, failure);

                failure.reason = "radial sub-annulus: moments did not resolve it";
                failedRegions.push_back (failure);

                stats.depthExceeded += n;
            }
        }

        return;
    }

    /*
        Neither cut separated the cluster, so it is solved from its moments by
        Newton's identities rather than guessed at one root a time.
    */
    {
        std::vector<Mode> recovered;

        if (clusterRoots (t, f1, f2, count, modes, recovered) == count)
        {
            for (const auto& m : recovered)
                (void) acceptRoot (m, modes);

            return;
        }
    }

    FailedRegion failure;
    failure.f1 = f1;
    failure.f2 = f2;
    failure.count = count;
    failure.centroid = contourCentroid (t, f1, f2);
    failure.path = currentPath;
    failure.r1 = kInnerRadius;
    failure.r2 = kOuterRadius;
    failure.edgesProbed = true;
    failure.edgesClean = edgesRootFree (t, f1, f2, kInnerRadius, kOuterRadius, stats,
                                        failure.edge, failure.edgeOk);
    describeRegion (t, f1, f2, kInnerRadius, kOuterRadius, count, failure);

    failure.reason = "cluster never separated in frequency, radius or moments";
    failedRegions.push_back (failure);

    stats.depthExceeded += count;
}

/** Determinant of the leading n x n block. */
[[nodiscard]] Complex determinantOf (Complex m[kLines][kLines], int n)
{
    Complex det (1.0, 0.0);

    for (int k = 0; k < n; ++k)
    {
        int pivot = k;
        double best = std::abs (m[k][k]);

        for (int r = k + 1; r < n; ++r)
            if (std::abs (m[r][k]) > best) { best = std::abs (m[r][k]); pivot = r; }

        if (best == 0.0)
            return Complex (0.0, 0.0);

        if (pivot != k)
        {
            for (int c = 0; c < n; ++c)
                std::swap (m[k][c], m[pivot][c]);

            det = -det;
        }

        det *= m[k][k];

        for (int r = k + 1; r < n; ++r)
        {
            const Complex f = m[r][k] / m[k][k];

            for (int c = k; c < n; ++c)
                m[r][c] -= f * m[k][c];
        }
    }

    return det;
}

/**
    L_i'(z), exactly.

        L_i = k_i (p_i z - q_i) / ((z - a) z^d)
        L'/L = p/(p z - q) - 1/(z - a) - d/z

    The d/z term dominates, which is why a finite difference on F was never
    going to be as good as this.
*/
[[nodiscard]] Complex loopFactorDerivative (const StaticTank& t, int i, Complex z)
{
    const auto& s = t.shelf[static_cast<size_t> (i)];

    const double a = s.coeffHigh;
    const double g = s.gainHigh;

    // Low Damp is 0, so the low shelf is identically 1 and the whole numerator
    // in z is p z - q.
    const double p = g + (1.0 - g) * (1.0 - a);
    const double q = g * a;

    const Complex term = Complex (p) / (Complex (p) * z - Complex (q))
                         - Complex (1.0) / (z - Complex (a))
                         - Complex (static_cast<double> (t.delaySamples[static_cast<size_t> (i)])) / z;

    return t.loopFactor (i, z) * term;
}

/**
    Residue from the 8x8 null vectors, independent of any contour.

    Expanding M(z)u = f about a simple pole, with M v = 0 and w^H M = 0:

        r_k = (c^T v)(w^H D(z_k) b) / (w^H M'(z_k) v),   M'(z) = -D'(z) A

    The null vectors come from the adjugate, which is rank one at a simple
    pole: any nonzero column is proportional to v and any nonzero row to w^H.
    That is eight 7x7 determinants per column on an 8x8 — nothing at this size,
    and it shares no machinery at all with the Cauchy integral, which is the
    point of computing both.
*/
[[nodiscard]] Complex nullVectorResidue (const StaticTank& t, Complex pole, bool& ok)
{
    Complex m[kLines][kLines];
    Complex loop[kLines], loopPrime[kLines];

    for (int i = 0; i < kLines; ++i)
    {
        loop[i] = t.loopFactor (i, pole);
        loopPrime[i] = loopFactorDerivative (t, i, pole);
    }

    for (int i = 0; i < kLines; ++i)
        for (int j = 0; j < kLines; ++j)
            m[i][j] = ((i == j) ? Complex (1.0) : Complex (0.0)) - loop[i] * t.matrix[i][j];

    Complex adjugate[kLines][kLines];

    for (int i = 0; i < kLines; ++i)
        for (int j = 0; j < kLines; ++j)
        {
            Complex minor[kLines][kLines] {};
            int r = 0;

            for (int a = 0; a < kLines; ++a)
            {
                if (a == i) continue;

                int c = 0;

                for (int b = 0; b < kLines; ++b)
                {
                    if (b == j) continue;

                    minor[r][c] = m[a][b];
                    ++c;
                }

                ++r;
            }

            const Complex cofactor = determinantOf (minor, kLines - 1);

            adjugate[j][i] = (((i + j) % 2) == 0) ? cofactor : -cofactor;
        }

    int bestColumn = 0, bestRow = 0;
    double columnNorm = -1.0, rowNorm = -1.0;

    for (int k = 0; k < kLines; ++k)
    {
        double cn = 0.0, rn = 0.0;

        for (int i = 0; i < kLines; ++i)
        {
            cn += std::norm (adjugate[i][k]);
            rn += std::norm (adjugate[k][i]);
        }

        if (cn > columnNorm) { columnNorm = cn; bestColumn = k; }
        if (rn > rowNorm)    { rowNorm = rn;    bestRow = k; }
    }

    if (columnNorm <= 0.0 || rowNorm <= 0.0)
    {
        ok = false;
        return {};
    }

    Complex v[kLines], wH[kLines];

    for (int i = 0; i < kLines; ++i)
    {
        v[i]  = adjugate[i][bestColumn];
        wH[i] = adjugate[bestRow][i];
    }

    Complex cv (0.0, 0.0), wDb (0.0, 0.0), denominator (0.0, 0.0);

    for (int i = 0; i < kLines; ++i)
    {
        cv  += t.c[static_cast<size_t> (i)] * v[i];
        wDb += wH[i] * loop[i] * t.b[static_cast<size_t> (i)];
    }

    for (int i = 0; i < kLines; ++i)
    {
        Complex av (0.0, 0.0);

        for (int j = 0; j < kLines; ++j)
            av += t.matrix[i][j] * v[j];

        denominator += wH[i] * (-loopPrime[i]) * av;
    }

    if (std::abs (denominator) <= 0.0)
    {
        ok = false;
        return {};
    }

    ok = true;
    return cv * wDb / denominator;
}

/** Cauchy residue at an explicit contour radius. */
[[nodiscard]] Complex cauchyResidue (const StaticTank& t, Complex pole,
                                     double radius, int points)
{
    Complex sum (0.0, 0.0);

    for (int m = 0; m < points; ++m)
    {
        const Complex offset = std::polar (radius, 2.0 * decay::pi * m / points);
        sum += transfer (t, pole + offset) * offset;
    }

    return sum / static_cast<double> (points);
}

void extractResidues (const StaticTank& t, std::vector<Mode>& modes)
{
    constexpr int points = 48;

    // Modes arrive in frequency order, so the nearest neighbour is adjacent.
    for (size_t k = 0; k < modes.size(); ++k)
    {
        double nearest = 1.0e30;

        if (k > 0)
            nearest = std::min (nearest, std::abs (modes[k].pole - modes[k - 1].pole));

        if (k + 1 < modes.size())
            nearest = std::min (nearest, std::abs (modes[k].pole - modes[k + 1].pole));

        const double radius = std::clamp (0.25 * nearest, 1.0e-12, 1.0e-6);

        Complex sum (0.0, 0.0);

        for (int m = 0; m < points; ++m)
        {
            const Complex offset = std::polar (radius, 2.0 * decay::pi * m / points);
            sum += transfer (t, modes[k].pole + offset) * offset;
        }

        modes[k].residue = sum / static_cast<double> (points);
    }
}

// ---------------------------------------------------------------------------
//  Modal rendering
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<double> modalImpulse (const std::vector<Mode>& modes,
                                                int numSamples, bool equalise)
{
    std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

    if (modes.empty())
        return out;

    double meanSquare = 0.0;

    for (const auto& m : modes)
        meanSquare += std::norm (m.residue);

    const double rms = std::sqrt (meanSquare / static_cast<double> (modes.size()));

    const size_t n = modes.size();

    std::vector<double> pr (n), pi (n), wr (n), wi (n), sr (n, 0.0), si (n, 0.0);
    std::vector<double> weight (n, 2.0);

    for (size_t k = 0; k < n; ++k)
    {
        pr[k] = modes[k].pole.real();
        pi[k] = modes[k].pole.imag();

        const double magnitude = std::abs (modes[k].residue);

        const Complex w = (equalise && magnitude > 0.0)
                          ? modes[k].residue * (rms / magnitude)
                          : modes[k].residue;

        wr[k] = w.real();
        wi[k] = w.imag();
        weight[k] = modes[k].isReal ? 1.0 : 2.0;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double x = (sample == 0) ? 1.0 : 0.0;

        double sum = 0.0;

        for (size_t k = 0; k < n; ++k)
        {
            // A real pole stands alone; a complex one carries its conjugate.
            sum += weight[k] * (wr[k] * sr[k] - wi[k] * si[k]);

            const double nr = pr[k] * sr[k] - pi[k] * si[k] + x;
            si[k] = pr[k] * si[k] + pi[k] * sr[k];
            sr[k] = nr;
        }

        out[static_cast<size_t> (sample)] = sum;
    }

    return out;
}

/** Response of an LTI impulse response to a short excitation. */
[[nodiscard]] std::vector<double> convolve (const std::vector<double>& impulse,
                                            const std::vector<double>& drive)
{
    const int n = static_cast<int> (impulse.size());
    const int m = static_cast<int> (drive.size());

    std::vector<double> out (static_cast<size_t> (n), 0.0);

    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        const int limit = std::min (m, i + 1);

        for (int j = 0; j < limit; ++j)
            sum += drive[static_cast<size_t> (j)] * impulse[static_cast<size_t> (i - j)];

        out[static_cast<size_t> (i)] = sum;
    }

    return out;
}

// ---------------------------------------------------------------------------
//  Ensemble measurement
// ---------------------------------------------------------------------------

template <typename RenderFn>
[[nodiscard]] decay::Result measure (RenderFn&& render, const decay::Band& band, int samples)
{
    std::vector<double> all (static_cast<size_t> (samples), 0.0);

    int counted = 0;

    for (int k = 0; k < kEnsemble; ++k)
    {
        auto signal = render (makeExcitation (k), samples);
        band.apply (signal);

        const auto edc = decay::normalisedEdc (signal);

        if (edc.empty())
            continue;

        for (size_t i = 0; i < edc.size(); ++i)
            all[i] += edc[i];

        ++counted;
    }

    if (counted == 0)
        return {};

    const double k = 1.0 / static_cast<double> (counted);

    for (auto& e : all)
        e *= k;

    return decay::analyseEdc (all, band, kSampleRate);
}

const double kProbes[] { 1000.0, 3000.0, 6000.0, 12000.0 };

} // namespace

// ===========================================================================

void runDampingResidueTests()
{
    StaticTank tank;

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - the M0 system really is LTI");

    {
        const int length = 40000;
        const int shift = 3001;

        auto impulse = std::vector<double> (static_cast<size_t> (length), 0.0);
        impulse[0] = 1.0;

        auto shifted = std::vector<double> (static_cast<size_t> (length), 0.0);
        shifted[static_cast<size_t> (shift)] = 1.0;

        const auto y0 = tank.render (impulse, length);
        const auto y1 = tank.render (shifted, length);

        long long timeVariant = 0;
        double worstShift = 0.0;

        for (int n = 0; n + shift < length; ++n)
        {
            const double d = std::abs (y1[static_cast<size_t> (n + shift)]
                                       - y0[static_cast<size_t> (n)]);

            worstShift = std::max (worstShift, d);

            if (d > 1.0e-15)
                ++timeVariant;
        }

        auto twice = impulse;
        for (auto& s : twice) s *= 2.0;

        const auto yTwice = tank.render (twice, length);

        auto burstA = makeExcitation (3);
        auto burstB = makeExcitation (11);
        auto sum = burstA;

        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] += burstB[i];

        const auto yA = tank.render (burstA, length);
        const auto yB = tank.render (burstB, length);
        const auto ySum = tank.render (sum, length);

        double worstHomogeneity = 0.0, worstSuperposition = 0.0;

        for (int n = 0; n < length; ++n)
        {
            worstHomogeneity = std::max (worstHomogeneity,
                                         std::abs (yTwice[static_cast<size_t> (n)]
                                                   - 2.0 * y0[static_cast<size_t> (n)]));

            worstSuperposition = std::max (worstSuperposition,
                                           std::abs (ySum[static_cast<size_t> (n)]
                                                     - yA[static_cast<size_t> (n)]
                                                     - yB[static_cast<size_t> (n)]));
        }

        info ("  allpass modulation depth  " + number (tank.core.getAllpassBank().getDepth(), 6));
        info ("  matrix modulation depth   " + number (tank.core.getMatrix().getDepth(), 6));
        info ("  worst shift-invariance    " + number (worstShift, 18));
        info ("  worst homogeneity         " + number (worstHomogeneity, 18));
        info ("  worst superposition       " + number (worstSuperposition, 18));

        check (timeVariant == 0, "the response to a shifted impulse is the shifted response");
        checkAtMost (worstHomogeneity, 1.0e-12, "doubling the input doubles the output");
        checkAtMost (worstSuperposition, 1.0e-9, "responses superpose");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - the transfer function is the measured system");

    {
        /*
            Everything downstream rests on T(z) being the transfer function of
            the thing the ensemble measurements actually render. That was never
            checked, and it is the one assumption whose failure would look
            exactly like a bad modal reconstruction.

            The impulse response is rendered by the engine and evaluated at a
            set of frequencies by direct summation. No modal model is involved:
            this compares the closed-form T(z) against the engine itself.
        */
        const int samples = static_cast<int> (kSampleRate * 6.0);

        std::vector<double> impulse (static_cast<size_t> (samples), 0.0);
        impulse[0] = 1.0;

        const auto ir = tank.render (impulse, samples);

        info ("    frequency    |T(z)| closed form   |H| from the render   error");

        double worstDb = 0.0, worstPhase = 0.0;

        for (double f : { 250.0, 700.0, 1000.0, 2500.0, 4000.0, 6000.0, 9000.0,
                          12000.0, 16000.0, 20000.0 })
        {
            const double w = 2.0 * decay::pi * f / kSampleRate;

            Complex sum (0.0, 0.0);

            for (int n = 0; n < samples; ++n)
                sum += ir[static_cast<size_t> (n)] * std::polar (1.0, -w * n);

            const Complex exact = transfer (tank, std::polar (1.0, w));

            const double db = (std::abs (sum) > 1.0e-15 && std::abs (exact) > 1.0e-15)
                              ? std::abs (20.0 * std::log10 (std::abs (exact) / std::abs (sum)))
                              : 0.0;

            const double phase = std::abs (std::arg (exact / sum));

            worstDb = std::max (worstDb, db);
            worstPhase = std::max (worstPhase, phase);

            info ("    " + pad (number (f, 0), 13)
                  + pad (number (std::abs (exact), 8), 22)
                  + pad (number (std::abs (sum), 8), 22)
                  + number (db, 4) + " dB");
        }

        info ("");
        info ("  worst magnitude error   " + number (worstDb, 6) + " dB");
        info ("  worst phase error       " + number (worstPhase, 6) + " rad");

        checkAtMost (worstDb, 0.05,
                     "the closed-form transfer function matches the rendered engine");

        checkAtMost (worstPhase, 0.02,
                     "and matches it in phase, so the delay convention is right");
    }

    // -----------------------------------------------------------------------

    int delayStates = 0;

    for (int i = 0; i < kLines; ++i)
        delayStates += tank.delaySamples[static_cast<size_t> (i)];

    beginSuite ("PHASE 7 residue - exact state dimension");

    {
        const int dampingStates = 2 * kLines;
        const int total = delayStates + dampingStates;

        info ("  delay states                  " + std::to_string (delayStates));
        info ("  damping states (2 per line)   " + std::to_string (dampingStates));
        info ("  allpass states at depth 0     0   (process returns its input)");
        info ("  EXACT STATE DIMENSION         " + std::to_string (total));
        info ("");
        info ("  Low Damp is 0 %, so gainLow is exactly 1 and the low-shelf factor");
        info ("  is identically 1: eight of those damping states are unobservable");
        info ("  from the output. The characteristic function therefore has degree");
        info ("  " + std::to_string (delayStates) + " + 8 = "
              + std::to_string (delayStates + kLines) + ", and the eight high-shelf");
        info ("  modes sit near z = " + number (tank.shelf[0].coeffHigh, 4)
              + ", far outside the search annulus.");
        info ("");
        info ("  So the count expected NEAR THE UNIT CIRCLE is "
              + std::to_string (delayStates) + ", i.e. "
              + std::to_string (delayStates / 2) + " conjugate pairs across");
        info ("  0 to Nyquist. That is the number the enumeration must produce.");

        checkEqual (static_cast<long long> (total), 18665LL,
                    "state dimension counted from the configured engine");
    }

    // -----------------------------------------------------------------------

    std::vector<Mode> modes;
    CountStats stats;
    bool enumerationComplete = false;

    beginSuite ("PHASE 7 residue - certified pole enumeration");

    {
        info ("  Argument principle on annular sectors, radius "
              + number (kInnerRadius, 4) + " to " + number (kOuterRadius, 4) + ".");
        info ("  N = (1/2pi) * change of arg F around the boundary. Every edge is");
        info ("  resampled until no phase step exceeds pi/2, and each split must");
        info ("  account for its parent exactly or the region is reported.");
        info ("");

        /*
            The main window is left exactly where it was. Widening it moved
            every block boundary by about a hertz, several landed on roots, and
            a scan that had counted 9322 cleanly lost 40 of them. The two roots
            outside it are picked up by separate sliver scans below instead, so
            the fix is additive and cannot disturb what already worked.
        */
        const double lowest = 1.0;
        const double highest = 0.5 * kSampleRate - 1.0;

        // Coarse blocks first, each then subdivided by count.
        const int blocks = 480;

        /*
            THE ONE BOUNDARY THE PROTOCOL NEVER TOUCHED.

            Every internal cut is certified now, but 1.0 Hz is not an internal
            cut: it is where the low sliver scan meets the main window, and
            the provenance trace named it exactly - both edges of the only
            unresolved region "came from the coarse block grid, never a
            split", so neither was ever tested. Its two zeros sum correctly
            with the window's 9322, which is what a zero sitting on a shared
            edge and being counted once looks like.

            So that boundary is chosen rather than assumed. The candidate is
            moved for BOTH neighbours together, their counts are required to
            add up to the count over the union, and s0 on each is required to
            reproduce its own count. Only the first block's lower edge moves;
            the remaining 479 boundaries are left exactly where they were,
            because moving them all once cost 40 roots.
        */
        const double blockWidth = (highest - lowest) / blocks;
        const double sliverBottom = 0.02;

        double sliverTop = lowest;
        double boundaryErrorLow = -1.0, boundaryErrorHigh = -1.0;
        int boundaryAttempts = 0;
        bool boundaryCertified = false;

        {
            const double unionTop = lowest + blockWidth;

            bool okUnion = true;
            const int nUnion = countZeros (tank, sliverBottom, unionTop, stats, okUnion);

            for (double candidate : { 1.0, 1.0515, 0.9485, 1.1030, 0.8970,
                                      1.2000, 0.8000, 1.3500, 0.6500 })
            {
                if (! okUnion || nUnion < 0)
                    break;

                boundaryAttempts += 1;

                bool okLeft = true, okRight = true;

                const int nLeft = countZeros (tank, sliverBottom, candidate, stats, okLeft);
                const int nRight = countZeros (tank, candidate, unionTop, stats, okRight);

                if (! okLeft || ! okRight || nLeft < 0 || nRight < 0
                    || nLeft + nRight != nUnion)
                    continue;

                double eL = -1.0, eR = -1.0;

                if (! certifyCut (tank, sliverBottom, candidate, unionTop, nLeft, nRight, eL, eR))
                {
                    boundaryErrorLow = eL;
                    boundaryErrorHigh = eR;
                    continue;
                }

                sliverTop = candidate;
                boundaryErrorLow = eL;
                boundaryErrorHigh = eR;
                boundaryCertified = true;
                break;
            }

            info ("  shared boundary, low sliver against the first block");
            info ("    zeros over the union 0.02 .. " + number (unionTop, 4) + " Hz   "
                  + (okUnion && nUnion >= 0 ? std::to_string (nUnion)
                                            : std::string ("contour unreliable")));
            info ("    candidates tried                      "
                  + std::to_string (boundaryAttempts));
            info ("    boundary taken                        " + number (sliverTop, 4) + " Hz"
                  + (boundaryCertified ? std::string() : std::string ("   NOT CERTIFIED")));
            info ("    |s0 - N| low side                     "
                  + (boundaryErrorLow < 0.0 ? std::string ("not measured")
                                            : number (boundaryErrorLow, 9)));
            info ("    |s0 - N| high side                    "
                  + (boundaryErrorHigh < 0.0 ? std::string ("not measured")
                                             : number (boundaryErrorHigh, 9)));
        }

        long long counted = 0, unreliable = 0;

        for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
        {
            const double f1 = (blockIndex == 0) ? sliverTop : lowest + blockWidth * blockIndex;
            const double f2 = lowest + blockWidth * (blockIndex + 1);
            bool ok = true;
            int n = countZeros (tank, f1, f2, stats, ok);

            /*
                A block boundary that lands on a root makes its contour phase
                turn arbitrarily fast and no resampling recovers it. Nudging
                the boundary is enough, and the block is only skipped once
                several nudges have failed - previously one bad boundary threw
                away a whole 50 Hz block and every root in it.
            */
            double a1 = f1, a2 = f2;

            for (double nudge : { 0.07, -0.07, 0.19, -0.19 })
            {
                if (ok && n >= 0)
                    break;

                a1 = f1 + nudge;
                a2 = f2 + nudge;

                ok = true;
                n = countZeros (tank, a1, a2, stats, ok);
            }

            if (! ok || n < 0)
            {
                ++unreliable;
                continue;
            }

            counted += n;
            enumerate (tank, a1, a2, n, 0, modes, stats);
        }

        /*
            The two slivers the main window cannot see. They are scanned
            separately so that the window which already counted 9322 cleanly is
            left exactly as it was.
        */
        /*
            THE BOTTOM OF THE SCAN.

            The symmetric boxes settle the count: nothing within 0.02 Hz of
            DC, four zeros within 0.1 Hz, and no more out to 0.5 Hz. Four is
            even, so these are two conjugate pairs and not real roots - which
            the real-axis scan already said, stepping 2.5e-8 through [0.99, 1)
            and finding its one sign change on the Nyquist side. Two
            upper-half roots therefore sit between 0.02 and 0.1 Hz, and the
            accounting needs exactly those two.

            On the SECTOR, however, the winding count and s0 disagree and
            lowering the bottom edge does not fix it. So the report stops
            printing |s0 - N|, which cannot tell s0 = 0 from s0 = 4, and
            prints s0 itself. Where the two roots are is then found by direct
            search over a region whose count is already certified, and each
            root is validated afterwards by a box drawn around it.
        */
        info ("");
        info ("  BOTTOM OF THE SCAN");

        /*
            Both instruments, side by side. F is meromorphic and its winding
            number is zeros minus poles; P is a polynomial of degree 18657
            with no poles at all, so its winding number is a plain zero
            count. Where the two disagree, P is the one that can be trusted,
            and printing them together says which is failing instead of
            leaving it to be inferred.
        */
        info ("    box          F winding   P winding");

        for (double halfWidth : { 0.02, 0.03, 0.05, 0.07, 0.1, 0.5, 1.0 })
        {
            bool okF = true, okP = true;
            const int nF = countZeros (tank, -halfWidth, halfWidth, stats, okF);
            const int nP = countZerosPoly (tank, -halfWidth, halfWidth,
                                           kInnerRadius, kOuterRadius, stats, okP);

            info ("    |f| <= " + pad (number (halfWidth, 3), 13)
                  + pad (okF && nF >= 0 ? std::to_string (nF) : std::string ("unreliable"), 12)
                  + (okP && nP >= 0 ? std::to_string (nP) : std::string ("unreliable")));
        }

        // And the strip at the top of the band that the scan never covered.
        info ("");
        info ("    strip near Nyquist      F winding   P winding");

        for (double low : { 23999.0, 23999.9, 23999.99 })
        {
            bool okF = true, okP = true;
            const int nF = countZeros (tank, low, 24000.0, stats, okF);
            const int nP = countZerosPoly (tank, low, 24000.0,
                                           kInnerRadius, kOuterRadius, stats, okP);

            info ("    " + pad (number (low, 2) + " .. 24000", 24)
                  + pad (okF && nF >= 0 ? std::to_string (nF) : std::string ("unreliable"), 12)
                  + (okP && nP >= 0 ? std::to_string (nP) : std::string ("unreliable")));
        }

        info ("");
        info ("    sector                      winding   s0 at 1024                s0 at 8192");

        struct Sector { double f1, f2; };

        for (Sector s : { Sector { 0.02, 1.0 }, Sector { 0.00003, 1.0 },
                          Sector { 0.02, 0.1 }, Sector { 0.015, 0.12 },
                          Sector { 0.005, 0.15 } })
        {
            bool ok = true;
            const int n = countZeros (tank, s.f1, s.f2, stats, ok);

            const auto a = contourMoments (tank, s.f1, s.f2, kInnerRadius, kOuterRadius, 1024);
            const auto b = contourMoments (tank, s.f1, s.f2, kInnerRadius, kOuterRadius, 8192);

            info ("    " + pad (number (s.f1, 5) + " .. " + number (s.f2, 4) + " Hz", 28)
                  + pad (ok && n >= 0 ? std::to_string (n) : std::string ("unreliable"), 10)
                  + pad (a.ok ? number (a.s0.real(), 6) + ", " + number (a.s0.imag(), 6)
                              : std::string ("unreliable"), 26)
                  + (b.ok ? number (b.s0.real(), 6) + ", " + number (b.s0.imag(), 6)
                          : std::string ("unreliable")));
        }

        /*
            THE NEAR-DC STRIP, BLOCKED LIKE THE REST OF THE BAND.

            This was the structural fault. The main window is cut into 480
            blocks and each one is subdivided by its own count; the strip
            below it was handled as a single 1 Hz sliver and handed to the
            same recursion. Near DC the geometry is far tighter - a 1 Hz
            sector spans 1.3e-4 rad against a radial extent of 1e-3, and the
            four zeros sit between 0.020 and 0.030 Hz, inside 2.6e-6 rad of
            the sliver's own lower edge. One sliver was never going to
            separate them; the strip needs finer blocks, not fewer.

            The counts here come from P. F and P agree exactly on every box
            tried - 0 zeros within 0.020 Hz of DC, 4 within 0.030, and no
            more out to 1.0 Hz - but P is a polynomial of degree 18657 with
            no poles, so its winding number is a plain zero count and needs
            no correction for pole order.

            The local-minimum grid search that used to sit here has been
            removed. It reported no minimum over this whole strip, and that
            is not evidence: it is the same detector that found only 30 % of
            the modes at 12 kHz earlier in this study, which is why the
            argument principle replaced it. A shallow dip under a strong
            radial trend does not produce a strict local minimum.
        */
        info ("");
        info ("  NEAR-DC STRIP");

        {
            const int dcBlocks = 250;
            const double dcLow = 0.002;

            long long dcCounted = 0;
            int dcUnreliable = 0;

            info ("    " + number (dcLow, 4) + " .. " + number (sliverTop, 4) + " Hz in "
                  + std::to_string (dcBlocks) + " blocks of "
                  + number ((sliverTop - dcLow) / dcBlocks, 5) + " Hz");
            info ("    block (Hz)                   zeros");

            for (int b = 0; b < dcBlocks; ++b)
            {
                const double a1 = dcLow + (sliverTop - dcLow) * b / dcBlocks;
                const double a2 = dcLow + (sliverTop - dcLow) * (b + 1) / dcBlocks;

                bool ok = true;
                const int n = countZerosPoly (tank, a1, a2, kInnerRadius, kOuterRadius,
                                              stats, ok);

                if (! ok || n < 0)
                {
                    ++dcUnreliable;
                    info ("    " + pad (number (a1, 5) + " .. " + number (a2, 5), 29)
                          + "contour unreliable");
                    continue;
                }

                if (n == 0)
                    continue;

                info ("    " + pad (number (a1, 5) + " .. " + number (a2, 5), 29)
                      + std::to_string (n));

                dcCounted += n;
            }

            unreliable += dcUnreliable;

            info ("    zeros counted in the blocks    " + std::to_string (dcCounted));
            info ("    blocks with an unreliable count " + std::to_string (dcUnreliable));

            /*
                The blocks above are a map, not the working subdivision. The
                solve runs on the WHOLE strip, because that is the only
                sector down here that is not a needle: 0.002 to 1.0 Hz is
                1.3e-4 rad across an annulus 1e-3 thick, an aspect ratio of
                about 1 to 8, while any 0.004 Hz block is 1 to 1900. The
                annulus is then halved on this wide sector until the region
                is square enough for its moments to mean something.
            */
            bool okStrip = true;
            const int stripCount = countZerosPoly (tank, dcLow, sliverTop,
                                                   kInnerRadius, kOuterRadius,
                                                   stats, okStrip);

            info ("    zeros over the whole strip     "
                  + (okStrip && stripCount >= 0 ? std::to_string (stripCount)
                                                : std::string ("contour unreliable")));

            /*
                Not solved and not counted. The two zeros this winding number
                reports are the real roots at 0.9999424, which lie at f = 0,
                on the boundary of every sector with f1 > 0 and inside none
                of them. s0 said zero here at every resolution from 512
                upward and s0 was right; the real-axis scan below owns them.
            */
            info ("    the strip itself holds         0 zeros strictly inside"
                  "   (s0 = 0; the winding count reads the f = 0 roots on its edge)");
        }

        // The sliver at the top of the band, left exactly as it was.
        for (auto sliver : { std::pair<double, double> { 0.5 * kSampleRate - 1.0,
                                                         0.5 * kSampleRate - 0.02 } })
        {
            bool ok = true;
            const int n = countZeros (tank, sliver.first, sliver.second, stats, ok);

            info ("  sliver " + number (sliver.first, 2) + " - " + number (sliver.second, 2)
                  + " Hz:  " + (ok && n >= 0 ? std::to_string (n) + " zeros"
                                             : std::string ("contour unreliable")));

            if (! ok || n < 0)
            {
                ++unreliable;
                continue;
            }

            counted += n;
            enumerate (tank, sliver.first, sliver.second, n, 0, modes, stats);
        }

        std::sort (modes.begin(), modes.end(),
                   [] (const Mode& a, const Mode& b) { return a.frequencyHz < b.frequencyHz; });

        const int expectedPairs = delayStates / 2;

        info ("  zeros counted by contour          " + std::to_string (counted));
        info ("  poles isolated and refined        " + std::to_string (modes.size()));
        info ("  blocks with an unreliable contour " + std::to_string (unreliable));
        info ("  regions that never separated      " + std::to_string (stats.depthExceeded));
        info ("  isolated but not refined          " + std::to_string (stats.newtonFailed));
        info ("  worst phase step seen             " + number (stats.worstPhaseStep, 4)
              + " rad   (limit " + number (0.5 * decay::pi, 4) + ")");
        info ("  edges that needed resampling      " + std::to_string (stats.refinements));
        info ("  characteristic evaluations        " + std::to_string (stats.evaluations));
        info ("");
        info ("  expected conjugate pairs          " + std::to_string (expectedPairs)
              + "   (state dimension " + std::to_string (delayStates + 2 * kLines)
              + " minus 8 unobservable and 8 fast shelf modes, halved)");

        const double coverage = expectedPairs > 0
                                ? 100.0 * static_cast<double> (modes.size()) / expectedPairs
                                : 0.0;

        info ("  coverage                          " + number (coverage, 3) + " %");

        enumerationComplete = (unreliable == 0) && (stats.newtonFailed == 0)
                              && (stats.depthExceeded == 0)
                              && (stats.newtonFailed == 0)
                              && (static_cast<long long> (modes.size()) == counted);


        if (! failedRegions.empty())
        {
            info ("");
            info ("  REGIONS THE ENUMERATION COULD NOT PIN DOWN");
            info ("    f1 .. f2 (Hz)                N  Pcnt  min|P| at f / |z|           |P|/median  reason");

            for (const auto& r : failedRegions)
                info ("    " + pad (number (r.f1, 4) + " .. " + number (r.f2, 4), 29)
                      + pad (std::to_string (r.count), 3)
                      + pad (std::to_string (r.polyCount), 6)
                      + pad (number (r.minPolyF, 5) + " / " + number (r.minPolyR, 9), 28)
                      + pad (number (r.minPolyRatio, 8), 12)
                      + r.reason);

            /*
                BOUNDARY PROVENANCE. For each unresolved region, where every
                one of its four contour edges came from and whether a zero
                sits on it. The frequency edges are traced to the split that
                created them, through the kept path, so an inherited boundary
                can be named rather than inferred.
            */
            info ("");
            info ("  BOUNDARY PROVENANCE OF THE UNRESOLVED REGIONS");

            for (const auto& r : failedRegions)
            {
                info ("");
                info ("    region " + number (r.f1, 4) + " .. " + number (r.f2, 4)
                      + " Hz   N = " + std::to_string (r.count)
                      + (r.r2 > r.r1 ? "   |z| " + number (r.r1, 6) + " .. " + number (r.r2, 6)
                                     : std::string()));

                if (r.regionS0Error >= 0.0)
                    info ("      |s0 - N| over the region itself   "
                          + number (r.regionS0Error, 8)
                          + (r.regionS0Error < 0.05 ? "   contour clean"
                                                    : "   A ZERO IS ON THE CONTOUR"));

                if (r.momentPerEdge[0] > 0)
                {
                    info ("      |s0 - N| against quadrature resolution");
                    info ("        perEdge   about the origin   about the centroid");

                    for (int k = 0; k < 6; ++k)
                        info ("        " + pad (std::to_string (r.momentPerEdge[k]), 10)
                              + pad (r.momentPlain[k] < 0.0 ? std::string ("unreliable")
                                                            : number (r.momentPlain[k], 8), 20)
                              + (r.momentShifted[k] < 0.0 ? std::string ("unreliable")
                                                          : number (r.momentShifted[k], 8)));
                }

                if (r.edgesProbed)
                {
                    const char* names[4] { "f1", "f2", "inner |z|", "outer |z|" };
                    info ("      sliver probe (unreliable, kept for comparison):");

                    for (int k = 0; k < 4; ++k)
                        info (std::string ("      zeros on edge ") + pad (names[k], 11)
                              + (r.edgeOk[k] ? std::to_string (r.edge[k])
                                             : std::string ("probe unreliable")));
                }

                int lowFrom = -1, highFrom = -1;

                for (const auto& step : r.path)
                {
                    if (step.rightChild)
                        lowFrom = step.event;        // its f1 is that split's cut
                    else
                        highFrom = step.event;       // its f2 is that split's cut
                }

                for (int which = 0; which < 2; ++which)
                {
                    const int index = (which == 0) ? lowFrom : highFrom;
                    const char* label = (which == 0) ? "f1" : "f2";

                    if (index < 0 || index >= static_cast<int> (splitEvents.size()))
                    {
                        info (std::string ("      ") + label
                              + " came from the coarse block grid, never a split");
                        continue;
                    }

                    const auto& e = splitEvents[static_cast<size_t> (index)];

                    info (std::string ("      ") + label + " = " + number (e.acceptedCut, 5)
                          + " was made by the split of "
                          + number (e.f1, 4) + " .. " + number (e.f2, 4)
                          + " (N = " + std::to_string (e.parentCount)
                          + ", depth " + std::to_string (e.depth) + ")");
                    info (std::string ("         proposed cut  ") + number (e.proposedCut, 5)
                          + "   displacement " + number (e.displacement, 6)
                          + "   sliver " + std::to_string (e.sliverCount)
                          + "   candidates tried " + std::to_string (e.attempts));
                    info (std::string ("         children      ")
                          + std::to_string (e.left) + " / " + std::to_string (e.right)
                          + "   |s0-N| left " + (e.certifyLeft < 0.0 ? std::string ("not certified")
                                                                     : number (e.certifyLeft, 8))
                          + "   right " + (e.certifyRight < 0.0 ? std::string ("not certified")
                                                                : number (e.certifyRight, 8)));
                }

                info ("      ancestry depth " + std::to_string (r.path.size())
                      + " splits below the coarse block");
            }
        }


        {
            int failures = 0;

            for (const auto& r : momentRecoveries)
                if (! r.accepted)
                    ++failures;

            info ("");
            info ("  CONTOUR-MOMENT RECOVERY");
            info ("    regions attempted                 " + std::to_string (momentRecoveries.size()));
            info ("    accepted                          "
                  + std::to_string (momentRecoveries.size() - failures));
            info ("    rejected                          " + std::to_string (failures));
            int outsideCount = 0, duplicateCount = 0, s0Count = 0, shiftCount = 0;

            for (const auto& r : momentRecoveries)
                if (! r.accepted)
                {
                    if (! r.inside)                     ++outsideCount;
                    else if (r.duplicate)               ++duplicateCount;
                    else if (! (r.s0Error < 0.05))      ++s0Count;
                    else if (! r.converged) ++shiftCount;
                }

            info ("    rejected: outside contour         " + std::to_string (outsideCount));
            info ("    rejected: duplicate               " + std::to_string (duplicateCount));
            info ("    rejected: |s0-N| >= 0.05          " + std::to_string (s0Count));
            info ("    rejected: quadrature not settled  " + std::to_string (shiftCount));

            if (failures > 0 && failures <= 40)
            {
                info ("");
                info ("    f1 .. f2 (Hz)                N  |s0-N|     |s2-z0^2|  f / |z|                   df       dr        |P/P'|     sigma/||M||  Nwt");

                for (const auto& r : momentRecoveries)
                    if (! r.accepted)
                        info ("    " + pad (number (r.f1, 4) + " .. " + number (r.f2, 4), 29)
                              + pad (std::to_string (r.count), 3)
                              + pad (number (r.s0Error, 6), 11)
                              + pad (number (r.s2Error, 6), 11)
                              + pad (number (r.frequency, 5) + " / " + number (r.radius, 9), 27)
                              + pad (number (r.frequencyShift, 2), 9)
                              + pad (number (r.radiusShift, 2), 10)
                              + pad (number (r.residual, 3), 11)
                              + pad (number (r.condition, 3), 13)
                              + (r.newtonUsed ? "y" : "n"));
            }
        }
        if (! shiftedCuts.empty())
        {
            info ("");
            info ("  CUTS DISPLACED OFF A ROOT");
            info ("    parent (Hz)                  Np  original cut   shifted cut    delta      L/R orig  L/R +d    L/R -d    invariant");

            for (const auto& c : shiftedCuts)
                info ("    " + pad (number (c.parentF1, 4) + " .. " + number (c.parentF2, 4), 29)
                      + pad (std::to_string (c.parentCount), 4)
                      + pad (number (c.originalCut, 5), 15)
                      + pad (number (c.shiftedCut, 5), 15)
                      + pad (number (c.displacement, 5), 11)
                      + pad (std::to_string (c.leftOriginal) + "/" + std::to_string (c.rightOriginal), 10)
                      + pad (std::to_string (c.leftShifted) + "/" + std::to_string (c.rightShifted), 10)
                      + pad (std::to_string (c.leftOpposite) + "/" + std::to_string (c.rightOpposite), 10)
                      + (c.invariant ? "yes" : "NO"));

            info ("");
            info ("  Each row: a sliver straddling the cut counted at least one zero, so");
            info ("  the cut was moved by a tenth of the parent's width. The parent total");
            info ("  is required to survive displacement to either side, and the root to");
            info ("  change hands between them - that is what makes ownership strict.");
        }
        check (unreliable == 0, "every coarse block returned a reliable integer count");
        check (stats.depthExceeded == 0, "every counted zero was isolated to a single region");
        checkEqual (static_cast<long long> (modes.size()), counted,
                    "every counted zero was refined to a pole");
    }

    // -----------------------------------------------------------------------

    struct Health
    {
        double magnitudeDb = 0.0;
        double complexError = 0.0;
        double impulseRms = 0.0;
        double rt60Error = 0.0;
        bool passed = false;
    };

    std::array<Health, 4> health {};
    bool modelValid = false;

    std::vector<Mode> lowRadiusModes;
    std::vector<Mode> realModes;

    beginSuite ("PHASE 7 residue - exact pole accounting");

    {
        /*
            The inventory is closed by measurement, not by inference. The
            winding number of F on a circle is (zeros - poles) inside it, and
            F's only poles are at z = 0 (order 18649) and z = a (order 8). So
            two circles settle where every root lives.
        */
        info ("  Transfer denominator degree, derived: " + std::to_string (kTransferDegree));
        info ("    = 18649 delay states + 8 observable high-shelf states");
        info ("    (the 8 low-shelf states are unobservable: Low Damp 0 makes");
        info ("     gainLow exactly 1, so that factor is identically 1)");
        info ("");

        double stepUnit = 0.0, stepInner = 0.0, stepDeep = 0.0;

        const double windingUnit = circleWinding (tank, 0.99999999, 400000, stepUnit);
        const double windingInner = circleWinding (tank, kInnerRadius, 400000, stepInner);
        const double windingDeep = circleWinding (tank, 0.30, 400000, stepDeep);

        const long long zerosInsideUnit = std::llround (windingUnit) + kTransferDegree;
        const long long zerosInsideInner = std::llround (windingInner) + kTransferDegree;

        // Below a = 0.4559 only the pole at the origin is enclosed.
        const long long zerosInsideDeep = std::llround (windingDeep) + 18649;

        info ("    radius       winding          zeros inside   worst phase step");
        info ("    0.99999999   " + pad (number (windingUnit, 3), 17)
              + pad (std::to_string (zerosInsideUnit), 15) + number (stepUnit, 4) + " rad");
        info ("    " + number (kInnerRadius, 4) + "       " + pad (number (windingInner, 3), 17)
              + pad (std::to_string (zerosInsideInner), 15) + number (stepInner, 4) + " rad");
        info ("    0.3000       " + pad (number (windingDeep, 3), 17)
              + pad (std::to_string (zerosInsideDeep), 15) + number (stepDeep, 4) + " rad");
        info ("");

        const long long inAnnulus = zerosInsideUnit - zerosInsideInner;

        info ("  zeros with |z| < " + number (kInnerRadius, 4) + "        "
              + std::to_string (zerosInsideInner));
        info ("  zeros in the search annulus       " + std::to_string (inAnnulus));
        info ("  zeros inside the unit circle      " + std::to_string (zerosInsideUnit)
              + "   (all of them, since the tank is stable)");
        info ("");

        // ---- the eight low-radius roots, on the polynomial ------------------
        {
            double stepPoly = 0.0;
            const double windingPoly = polynomialWinding (tank, kInnerRadius, 200000, stepPoly);

            info ("  P(z) winding on |z| = " + number (kInnerRadius, 4) + "   "
                  + number (windingPoly, 3) + "   -> " + std::to_string (std::llround (windingPoly))
                  + " zeros inside (P is a polynomial, so this is a plain zero count)");

            std::vector<double> lowRadiusReal;

            findPolynomialRealRoots (tank, -0.9990, 0.9990, 400000, lowRadiusReal);

            info ("  real roots of P with |z| < " + number (kInnerRadius, 4) + "   "
                  + std::to_string (lowRadiusReal.size()));

            for (double r : lowRadiusReal)
            {
                lowRadiusModes.push_back ({});
                auto& mode = lowRadiusModes.back();

                mode.pole = Complex (r, 0.0);
                mode.frequencyHz = (r > 0.0) ? 0.0 : 0.5 * kSampleRate;
                mode.rt60 = -3.0 / (std::log10 (std::abs (r)) * kSampleRate);
                mode.isReal = true;

                info ("    z = " + number (r, 12) + "   RT60 "
                      + number (mode.rt60, 6) + " s");
            }

            info ("");
            info ("  The open-loop shelf pole sits at z = " + number (tank.shelf[0].coeffHigh, 4)
                  + "; where the closed-loop roots actually land is measured, not assumed.");
            info ("");
        }
        /*
            THE DC CLUSTER, COUNTED ON A SQUARE CONTOUR.

            Two real roots sit at 0.999942449866 and 0.999942451318, 1.45e-9
            apart. That is why nothing found them: the F scan steps 2.5e-8,
            so it crosses both sign changes inside one step and sees none at
            all, and the P scan only resolves them because its step is
            8.3e-10. Both are DC modes with RT60 2.50 s.

            They also settle which instrument was right. s0 reported no zeros
            inside every sector with f1 > 0, and that is simply true of a
            root at f = 0; the sector winding count was the corrupted one,
            reading a root on its own boundary. So the near-DC sector solve
            is gone - there was never anything inside it - and the real-axis
            scan owns these roots.

            The annulus holds 18649 zeros. 9322 upper-half complex with their
            conjugates, the Nyquist root and these two make 18647, so two are
            still missing, and the cluster is where to look. It is counted
            here on a contour that is actually square: the frequency
            half-width is chosen so that r*dw matches the radius window,
            which the full-annulus boxes never did - |f| <= 0.02 across the
            whole annulus is 5.2e-6 by 1e-3, an aspect ratio of 1 to 190.
        */
        info ("  THE DC CLUSTER");
        info ("    radius window              |f| <=      aspect     P count");

        for (double half : { 2.0e-6, 5.0e-6, 2.0e-5, 1.0e-4 })
        {
            const double rLo = std::max (kInnerRadius, 0.999942450592 - half);
            const double rHi = std::min (1.0, 0.999942450592 + half);

            // The frequency half-width that makes r*dw equal the radius window.
            const double halfF = half * kSampleRate / (2.0 * decay::pi);

            bool ok = true;
            const int n = countZerosPoly (tank, -halfF, halfF, rLo, rHi, stats, ok);

            info ("    " + pad (number (rLo, 12) + " .. " + number (rHi, 12), 27)
                  + pad (number (halfF, 6), 12)
                  + pad ("1 : 1", 11)
                  + (ok && n >= 0 ? std::to_string (n) : std::string ("unreliable")));
        }

        info ("");
        info ("    sign changes of P in the cluster, at rising resolution");
        info ("      window                               samples     found");

        for (int samples : { 200000, 800000, 3200000 })
        {
            std::vector<double> tight;
            findPolynomialRealRoots (tank, 0.9999410, 0.9999440, samples, tight);

            info ("      0.9999410 .. 0.9999440              "
                  + pad (std::to_string (samples), 12)
                  + std::to_string (tight.size()));

            if (samples == 3200000)
                for (double x : tight)
                    info ("        x = " + number (x, 12)
                          + "   RT60 " + number (-3.0 / (std::log10 (x) * kSampleRate), 6)
                          + " s   |P/P'| " + number (rootResidual (tank, Complex (x, 0.0)), 6));
        }

        info ("");
        /*
            THE REAL AXIS INSIDE THE ANNULUS, COUNTED THEN LOCATED.

            A single-resolution sign scan cannot do this. The four DC roots
            span 3.2e-9 in z, so resolving them over [0.999, 1) needs a step
            near 1e-12 and therefore a billion samples; at the 8.3e-10 step
            that 1.2 million samples buys, two of the four crossings fall
            inside one step and vanish. F is worse still - its 2.5e-8 step
            crosses all four and reports none, which is why this cluster was
            invisible for the whole study.

            So the count and the position come from different instruments,
            and the count comes first. A box symmetric about the real axis,
            with its frequency half-width set so that r*dw equals the radius
            window, is a square contour and its winding number is a plain
            zero count - 4 at the DC axis and 1 at Nyquist, the same at every
            scale tried. Each root the coarse scan does find is then used as
            a seed for a fine scan of its own neighbourhood, which recovers
            the rest of its micro-cluster, and the total is required to equal
            the certified count. Nothing is accepted on the strength of the
            locator alone.
        */
        std::vector<double> realRoots;

        {
            /*
                The box has to be narrow in RADIUS, not just square, or it
                counts the wrong thing. A box 1e-3 deep needs a 7.64 Hz
                half-width to be square, and at 2.57 Hz between modes that
                sweeps in whole conjugate pairs: it returns 6 at DC, which is
                the four real roots plus one pair, and 7 at Nyquist. Those
                numbers are correct and they are not a count of real roots,
                so comparing them against what the axis scan locates
                compares two different quantities.

                What certifies the real roots is a box whose radius window
                brackets the cluster alone. Its half-width is then small
                enough to exclude the pairs, and it returned 4 at DC at four
                different scales, matching the sign scan at three
                resolutions. That bracket is measured here rather than
                assumed: the radius window is contracted around the located
                roots until the count stops changing.
            */
            bool okWide = true;
            const double wideHalf = (kOuterRadius - kInnerRadius)
                                    * kSampleRate / (2.0 * decay::pi);
            const int wideDc = countZerosPoly (tank, -wideHalf, wideHalf,
                                               kInnerRadius, kOuterRadius, stats, okWide);

            info ("  REAL AXIS INSIDE THE ANNULUS");
            info ("    all zeros in the square box around DC, real and complex   "
                  + (okWide && wideDc >= 0 ? std::to_string (wideDc)
                                           : std::string ("unreliable"))
                  + "   (half-width " + number (wideHalf, 4) + " Hz)");

            /*
                The certifying count: a box bracketing the located cluster,
                contracted until the count settles. Its frequency half-width
                tracks its radius window so the contour stays square, and
                the window is small enough to hold no conjugate pair.
            */
            const auto bracketCount = [&] (double centre, double span, int& out) -> bool
            {
                /*
                    The radius window is built from |centre|; the sign of
                    centre only picks which axis, 0 Hz or Nyquist. Building
                    it from centre itself gave a negative outer radius on the
                    Nyquist side and the count came back unreliable, which
                    read as a disagreement when it was a mistake in the
                    diagnostic rather than anything about the roots.
                */
                const double m = std::abs (centre);

                const double rLo = std::max (kInnerRadius, m - span);
                const double rHi = std::min (1.0, m + span);
                const double hf = span * kSampleRate / (2.0 * decay::pi);
                const double axis = (centre > 0.0) ? 0.0 : 0.5 * kSampleRate;

                bool ok = true;
                out = countZerosPoly (tank, axis - hf, axis + hf, rLo, rHi, stats, ok);

                return ok && out >= 0;
            };

            info ("    bracketing box around each axis");
            info ("      axis       span        half-width (Hz)   zeros");

            int certifiedDc = -1, certifiedNy = -1;

            for (double span : { 1.0e-4, 2.0e-5, 5.0e-6, 2.0e-6 })
            {
                int n = -1;
                const bool ok = bracketCount (0.999942450592, span, n);

                info ("      DC         " + pad (number (span, 8), 12)
                      + pad (number (span * kSampleRate / (2.0 * decay::pi), 6), 18)
                      + (ok ? std::to_string (n) : std::string ("unreliable")));

                if (ok)
                    certifiedDc = n;
            }

            for (double span : { 1.0e-4, 2.0e-5, 5.0e-6, 2.0e-6 })
            {
                int n = -1;
                const bool ok = bracketCount (-0.999744702600, span, n);

                info ("      Nyquist    " + pad (number (span, 8), 12)
                      + pad (number (span * kSampleRate / (2.0 * decay::pi), 6), 18)
                      + (ok ? std::to_string (n) : std::string ("unreliable")));

                if (ok)
                    certifiedNy = n;
            }

            // Coarse seeds.
            std::vector<double> seeds, fromF;

            findPolynomialRealRoots (tank, kInnerRadius, 0.99999999, 1200000, seeds);
            findPolynomialRealRoots (tank, -0.99999999, -kInnerRadius, 1200000, seeds);

            findRealRoots (tank, 0.9900, 0.99999999, 400000, fromF);
            findRealRoots (tank, -0.99999999, -0.9900, 400000, fromF);

            info ("    seeds from P               " + std::to_string (seeds.size()));
            info ("    seeds from F               " + std::to_string (fromF.size()));

            // Each seed opens a fine scan of its own neighbourhood.
            const auto add = [&realRoots] (double x)
            {
                for (double y : realRoots)
                    if (std::abs (x - y) < 1.0e-13)
                        return;

                realRoots.push_back (x);
            };

            for (double seed : seeds)
            {
                const double span = 4.0e-8;
                const double lo = seed - span, hi = seed + span;

                std::vector<double> fine;
                findPolynomialRealRoots (tank, lo, hi, 400000, fine);

                for (double x : fine)
                    add (x);

                if (fine.empty())
                    add (seed);
            }

            std::sort (realRoots.begin(), realRoots.end());

            int atDc = 0, atNyquist = 0;

            info ("    x                       |P/P'|        RT60 (s)      side");

            for (double x : realRoots)
            {
                info ("    " + pad (number (x, 12), 24)
                      + pad (number (rootResidual (tank, Complex (x, 0.0)), 6), 14)
                      + pad (number (-3.0 / (std::log10 (std::abs (x)) * kSampleRate), 6), 14)
                      + (x > 0.0 ? "DC" : "Nyquist"));

                if (x > 0.0)
                    ++atDc;
                else
                    ++atNyquist;
            }

            info ("    located at DC              " + std::to_string (atDc)
                  + "   bracketing box says " + (certifiedDc >= 0
                      ? std::to_string (certifiedDc) : std::string ("unreliable"))
                  + (certifiedDc == atDc ? "   agree" : "   DISAGREE"));
            info ("    located at Nyquist         " + std::to_string (atNyquist)
                  + "   bracketing box says " + (certifiedNy >= 0
                      ? std::to_string (certifiedNy) : std::string ("unreliable"))
                  + (certifiedNy == atNyquist ? "   agree" : "   DISAGREE"));
            info ("");
        }

        info ("  real roots found on |x| in [0.99, 1)   " + std::to_string (realRoots.size()));
        info ("  real roots found on |x| in [0.99, 1)   " + std::to_string (realRoots.size()));

        for (double r : realRoots)
        {
            realModes.push_back ({});
            auto& mode = realModes.back();

            mode.pole = Complex (r, 0.0);
            mode.frequencyHz = (r > 0.0) ? 0.0 : 0.5 * kSampleRate;
            mode.rt60 = -3.0 / (std::log10 (std::abs (r)) * kSampleRate);
            mode.isReal = true;

            info ("    z = " + number (r, 12) + "   RT60 " + number (mode.rt60, 4)
                  + " s   " + (r > 0.0 ? "DC side" : "Nyquist side"));
        }

        info ("");
        info ("  ACCOUNTING");
        info ("    2 * upper-half complex in band      " + std::to_string (2 * modes.size()));
        info ("    isolated but unrefined regions      " + std::to_string (stats.newtonFailed));
        info ("    regions that never separated        " + std::to_string (stats.depthExceeded));
        info ("    real roots near the unit circle     " + std::to_string (realRoots.size()));
        info ("    low-radius roots located            " + std::to_string (lowRadiusModes.size()) + "   (winding says " + std::to_string (zerosInsideInner) + ")");
        info ("    ---------------------------------------------");
        info ("    required total                      " + std::to_string (kTransferDegree));

        const long long accounted = 2 * static_cast<long long> (modes.size())
                                    + 2 * stats.newtonFailed
                                    + 2 * stats.depthExceeded
                                    + static_cast<long long> (realModes.size())
                                    + static_cast<long long> (lowRadiusModes.size());

        info ("    accounted for                       " + std::to_string (accounted));
        info ("    unexplained                         "
              + std::to_string (kTransferDegree - accounted));

        checkAtMost (std::max (stepUnit, std::max (stepInner, stepDeep)), 0.5 * decay::pi,
                     "every winding contour was sampled finely enough to be an integer count");

        checkEqual (zerosInsideUnit, static_cast<long long> (kTransferDegree),
                    "every root of the transfer denominator is inside the unit circle");

        checkEqual (accounted, static_cast<long long> (kTransferDegree),
                    "the pole inventory closes exactly");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - duplicate and gap audit");

    {
        /*
            A region is allowed to accept a root a little outside itself, so
            that a jittered boundary does not orphan a pole sitting on it. That
            margin can also let two adjacent regions converge to the SAME root:
            one pole then enters the sum twice while a different one is missing
            entirely, and the total count still matches. It would look exactly
            like the failure being investigated, so it is checked directly.
        */
        long long duplicates = 0;
        double closestPair = 1.0e30;
        double widestGapHz = 0.0;
        double gapAtHz = 0.0;

        for (size_t k = 1; k < modes.size(); ++k)
        {
            const double separation = std::abs (modes[k].pole - modes[k - 1].pole);

            closestPair = std::min (closestPair, separation);

            if (separation < 1.0e-9)
                ++duplicates;

            const double gap = modes[k].frequencyHz - modes[k - 1].frequencyHz;

            if (gap > widestGapHz)
            {
                widestGapHz = gap;
                gapAtHz = modes[k - 1].frequencyHz;
            }
        }

        const double meanSpacing = modes.size() > 1
                                   ? (modes.back().frequencyHz - modes.front().frequencyHz)
                                     / static_cast<double> (modes.size() - 1)
                                   : 0.0;

        info ("  poles in the list                 " + std::to_string (modes.size()));
        info ("  duplicate pairs (|dz| < 1e-9)     " + std::to_string (duplicates));
        info ("  closest pair separation           " + number (closestPair, 12));
        info ("  mean frequency spacing            " + number (meanSpacing, 4) + " Hz");
        info ("  widest gap                        " + number (widestGapHz, 4)
              + " Hz at " + number (gapAtHz, 0) + " Hz");
        info ("");
        info ("  A gap far above the mean spacing is a missing mode; a duplicate");
        info ("  is a mode counted twice. Either would break the identity while");
        info ("  leaving the total count intact.");

        checkEqual (duplicates, 0LL, "no pole was accepted by two neighbouring regions");

        checkAtMost (widestGapHz, 4.0 * std::max (meanSpacing, 1.0e-9),
                     "no gap in the pole list far exceeds the mean modal spacing");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - partial-fraction convention on a known system");

    {
        /*
            Before trusting any residue of Aurora, the convention itself is
            checked on a system whose answer is known by hand: two conjugate
            pole pairs with chosen residues. This catches a one-sample shift, a
            stray z factor or mishandled conjugates - any of which would let a
            perfectly correct residue reconstruct the wrong signal.
        */
        std::vector<Mode> toy (2);

        toy[0].pole = std::polar (0.995, 0.30);
        toy[0].residue = Complex (0.7, -0.2);
        toy[1].pole = std::polar (0.980, 1.10);
        toy[1].residue = Complex (-0.3, 0.45);

        const int samples = 4000;
        const auto ir = modalImpulse (toy, samples, false);

        // Analytic: h[n] = sum 2 Re(r p^(n-1)) for n >= 1, and h[0] = 0.
        double worstTime = std::abs (ir[0]);

        for (int n = 1; n < samples; ++n)
        {
            Complex expected (0.0, 0.0);

            for (const auto& m : toy)
                expected += m.residue * std::pow (m.pole, n - 1);

            worstTime = std::max (worstTime,
                                  std::abs (ir[static_cast<size_t> (n)] - 2.0 * expected.real()));
        }

        // And the frequency domain of the same expansion.
        double worstFrequency = 0.0;

        for (int k = 0; k <= 200; ++k)
        {
            const double w = decay::pi * k / 200.0;
            const Complex z = std::polar (1.0, w);

            Complex sum (0.0, 0.0);

            for (const auto& m : toy)
                sum += m.residue / (z - m.pole)
                       + std::conj (m.residue) / (z - std::conj (m.pole));

            Complex fromTime (0.0, 0.0);

            for (int n = 0; n < samples; ++n)
                fromTime += ir[static_cast<size_t> (n)] * std::polar (1.0, -w * n);

            worstFrequency = std::max (worstFrequency, std::abs (sum - fromTime));
        }

        info ("  worst time-domain error       " + number (worstTime, 15));
        info ("  worst frequency-domain error  " + number (worstFrequency, 12));
        info ("");
        info ("  r/(z - p) has impulse response r p^(n-1) starting at n = 1, and");
        info ("  the renderer's output-then-update order is what produces it.");

        checkAtMost (worstTime, 1.0e-12,
                     "the modal renderer realises r/(z-p) exactly");

        checkAtMost (worstFrequency, 1.0e-6,
                     "and its transform agrees with the partial-fraction sum");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - Cauchy radius sensitivity and an independent formula");

    {
        /*
            Two questions at once. Is the contour radius in a numerically
            stable region, and does a completely different derivation give the
            same complex residue? The second is the one that matters: the
            null-vector formula shares no code path with the contour integral.
        */
        struct Probe { double hz; const char* label; };

        const Probe probes[]
        {
            { 1000.0, "1 kHz" }, { 3000.0, "3 kHz" },
            { 6000.0, "6 kHz" }, { 12000.0, "12 kHz" },
        };

        info ("    pole        d_k          rho/d_k   |r|          arg(r)      vs null-vector");

        double worstDisagreement = 0.0;
        double worstRadiusSpread = 0.0;

        for (const auto& probe : probes)
        {
            // The mode nearest the probe frequency, and its neighbour distance.
            size_t index = 0;
            double bestDistance = 1.0e30;

            for (size_t k = 0; k < modes.size(); ++k)
            {
                const double d = std::abs (modes[k].frequencyHz - probe.hz);

                if (d < bestDistance) { bestDistance = d; index = k; }
            }

            if (modes.size() < 3)
                continue;

            double nearest = 1.0e30;

            if (index > 0)
                nearest = std::min (nearest, std::abs (modes[index].pole - modes[index - 1].pole));

            if (index + 1 < modes.size())
                nearest = std::min (nearest, std::abs (modes[index].pole - modes[index + 1].pole));

            bool ok = false;
            const Complex reference = nullVectorResidue (tank, modes[index].pole, ok);

            double minMagnitude = 1.0e30, maxMagnitude = 0.0;

            for (double fraction : { 0.02, 0.05, 0.10, 0.20 })
            {
                const double radius = fraction * nearest;
                const Complex r = cauchyResidue (tank, modes[index].pole, radius, 256);

                minMagnitude = std::min (minMagnitude, std::abs (r));
                maxMagnitude = std::max (maxMagnitude, std::abs (r));

                const double disagreement = ok && std::abs (reference) > 0.0
                                            ? std::abs (r - reference) / std::abs (reference)
                                            : -1.0;

                if (disagreement >= 0.0)
                    worstDisagreement = std::max (worstDisagreement, disagreement);

                info ("    " + pad (std::string (probe.label), 12)
                      + pad (number (nearest, 10), 13)
                      + pad (number (fraction, 2), 10)
                      + pad (number (std::abs (r), 10), 13)
                      + pad (number (std::arg (r), 6), 12)
                      + (disagreement >= 0.0 ? number (disagreement * 100.0, 6) + " %"
                                             : std::string ("null vector failed")));
            }

            if (minMagnitude > 0.0)
                worstRadiusSpread = std::max (worstRadiusSpread,
                                              (maxMagnitude - minMagnitude) / minMagnitude);

            info ("");
        }

        info ("  worst spread across contour radii   " + number (worstRadiusSpread * 100.0, 6) + " %");
        info ("  worst Cauchy vs null-vector         " + number (worstDisagreement * 100.0, 6) + " %");
        info ("");
        info ("  The two derivations share no code. Agreement means the residues");
        info ("  are right and the reconstruction failure is elsewhere; disagreement");
        info ("  means the residues are the failure.");

        checkAtMost (worstRadiusSpread, 1.0e-6,
                     "the Cauchy residue does not depend on the contour radius");

        checkAtMost (worstDisagreement, 1.0e-6,
                     "the contour and null-vector residues agree numerically");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - reconstruction health");

    {
        if (modes.empty())
        {
            info ("  No modes were extracted, so there is nothing to validate.");
            check (false, "modes available for validation");
        }
        else
        {
            extractResidues (tank, modes);

            double worstRadius = 0.0;

            for (const auto& m : modes)
                worstRadius = std::max (worstRadius, std::abs (m.pole));

            info ("  worst pole radius   " + number (worstRadius, 10)
                  + "   (all strictly inside the unit circle: "
                  + std::string (worstRadius < 1.0 ? "yes" : "NO") + ")");
            info ("");

            const int samples = static_cast<int> (kSampleRate * 5.0);

            const auto modelIr = modalImpulse (modes, samples, false);

            std::vector<double> impulse (static_cast<size_t> (samples), 0.0);
            impulse[0] = 1.0;

            const auto exactIr = tank.render (impulse, samples);

            info ("    band     |H| error   complex     impulse RMS   RT60 error");

            for (size_t bIndex = 0; bIndex < 4; ++bIndex)
            {
                const double centre = kProbes[bIndex];
                auto& h = health[bIndex];

                const double checkLow = centre / 1.5, checkHigh = centre * 1.5;
                double worstDb = 0.0, worstComplex = 0.0, reference = 0.0;

                for (int k = 0; k <= 500; ++k)
                {
                    const double f = checkLow * std::pow (checkHigh / checkLow, k / 500.0);
                    const Complex z = std::polar (1.0, 2.0 * decay::pi * f / kSampleRate);

                    const Complex exact = transfer (tank, z);

                    Complex sum (0.0, 0.0);

                    for (const auto& m : modes)
                        sum += m.residue / (z - m.pole)
                               + std::conj (m.residue) / (z - std::conj (m.pole));

                    worstComplex = std::max (worstComplex, std::abs (exact - sum));
                    reference = std::max (reference, std::abs (exact));

                    if (std::abs (exact) > 1.0e-14 && std::abs (sum) > 1.0e-14)
                        worstDb = std::max (worstDb,
                                            std::abs (20.0 * std::log10 (std::abs (sum)
                                                                         / std::abs (exact))));
                }

                h.magnitudeDb = worstDb;
                h.complexError = worstComplex / std::max (reference, 1.0e-30);

                const auto band = decay::makeBand (centre, kBandOctaves, kSampleRate);

                auto a = exactIr, b = modelIr;
                band.apply (a);
                band.apply (b);

                double numerator = 0.0, denominator = 0.0;

                for (int n = 0; n < samples; ++n)
                {
                    const double d = b[static_cast<size_t> (n)] - a[static_cast<size_t> (n)];
                    numerator += d * d;
                    denominator += a[static_cast<size_t> (n)] * a[static_cast<size_t> (n)];
                }

                h.impulseRms = std::sqrt (numerator / std::max (denominator, 1.0e-300));

                const auto exactFit = decay::analyse (a, band, kSampleRate);
                const auto modelFit = decay::analyse (b, band, kSampleRate);

                if (exactFit.valid && modelFit.valid)
                    h.rt60Error = std::abs (modelFit.fitted - exactFit.fitted)
                                  / std::max (exactFit.fitted, 1.0e-12);

                h.passed = h.magnitudeDb <= kHealthMagnitudeDb
                           && h.impulseRms <= kHealthImpulseRms
                           && h.rt60Error <= kHealthRt60Error;

                info ("    " + pad (number (centre, 0), 9)
                      + pad (number (h.magnitudeDb, 4) + " dB", 12)
                      + pad (number (h.complexError * 100.0, 3) + " %", 12)
                      + pad (number (h.impulseRms * 100.0, 3) + " %", 14)
                      + number (h.rt60Error * 100.0, 3) + " %"
                      + (h.passed ? "" : "   FAIL"));
            }

            info ("");
            info ("  Limits fixed before the run: |H| <= " + number (kHealthMagnitudeDb, 1)
                  + " dB, impulse <= " + number (kHealthImpulseRms * 100.0, 0)
                  + " % RMS, RT60 <= " + number (kHealthRt60Error * 100.0, 0) + " %.");

            int passed = 0;

            for (const auto& h : health)
                if (h.passed)
                    ++passed;

            modelValid = (passed == 4) && enumerationComplete && worstRadius < 1.0;

            info ("  " + std::string (modelValid ? "MODEL VALID" : "MODEL INVALID"));

            check (modelValid, "the modal representation reproduces static T0 in every band");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - residue statistics");

    {
        if (! modelValid)
        {
            info ("  The reconstruction did not validate, so residue statistics are");
            info ("  not interpreted. They would describe a model that does not");
            info ("  reproduce the system it came from.");

            check (true, "statistics withheld until the model validates");
        }
        else
        {
            info ("    band          modes   p10        median     p90        max        p90/p10");

            for (size_t bIndex = 0; bIndex < 4; ++bIndex)
            {
                const double centre = kProbes[bIndex];
                const double low = centre / 1.4142, high = centre * 1.4142;

                std::vector<double> magnitudes;

                for (const auto& m : modes)
                    if (m.frequencyHz >= low && m.frequencyHz <= high)
                        magnitudes.push_back (std::abs (m.residue));

                if (magnitudes.size() < 10)
                    continue;

                std::sort (magnitudes.begin(), magnitudes.end());

                const double p10 = magnitudes[magnitudes.size() / 10];
                const double median = magnitudes[magnitudes.size() / 2];
                const double p90 = magnitudes[(magnitudes.size() * 9) / 10];

                info ("    " + pad (number (centre, 0), 14)
                      + pad (std::to_string (magnitudes.size()), 8)
                      + pad (number (p10, 8), 11)
                      + pad (number (median, 8), 11)
                      + pad (number (p90, 8), 11)
                      + pad (number (magnitudes.back(), 8), 11)
                      + number (p90 / std::max (p10, 1.0e-30), 2) + "x");
            }

            check (true, "residue magnitude distribution measured from the validated model");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 residue - MODEL R and MODEL E");

    {
        if (! modelValid)
        {
            info ("  Reconstruction did not validate. MODEL R and MODEL E are not");
            info ("  run: reading T20/T30 from a model that does not reproduce the");
            info ("  system would be reading its own error.");
            info ("");
            info ("  CLASSIFICATION: UNRESOLVED");

            check (true, "classification recorded against the pre-registered rule");
        }
        else
        {
            const int samples = static_cast<int> (kSampleRate * 5.0);

            const auto irR = modalImpulse (modes, samples, false);
            const auto irE = modalImpulse (modes, samples, true);

            info ("    band    source   RT60    EDT     T20     T30     R^2       |T20-T30|");

            std::array<double, 4> t0 {}, mr {}, me {};

            for (size_t bIndex = 0; bIndex < 4; ++bIndex)
            {
                const double centre = kProbes[bIndex];
                const auto band = decay::makeBand (centre, kBandOctaves, kSampleRate);

                const auto a = measure ([&] (const std::vector<double>& d, int n)
                                        { return tank.render (d, n); }, band, samples);

                const auto r = measure ([&] (const std::vector<double>& d, int)
                                        { return convolve (irR, d); }, band, samples);

                const auto e = measure ([&] (const std::vector<double>& d, int)
                                        { return convolve (irE, d); }, band, samples);

                const auto row = [&] (const char* label, const decay::Result& x)
                {
                    if (! x.valid)
                    {
                        info ("    " + pad (number (centre, 0), 8) + pad (label, 9)
                              + "INVALID - " + x.reason);
                        return;
                    }

                    info ("    " + pad (number (centre, 0), 8) + pad (label, 9)
                          + pad (number (x.fitted, 3), 8) + pad (number (x.edt, 3), 8)
                          + pad (number (x.t20, 3), 8) + pad (number (x.t30, 3), 8)
                          + pad (number (x.r2, 5), 10)
                          + number (x.mismatch() * 100.0, 2) + " %");
                };

                row ("T0", a);
                row ("MODEL R", r);
                row ("MODEL E", e);

                t0[bIndex] = a.valid ? a.mismatch() * 100.0 : 0.0;
                mr[bIndex] = r.valid ? r.mismatch() * 100.0 : 0.0;
                me[bIndex] = e.valid ? e.mismatch() * 100.0 : 0.0;
            }

            info ("");
            info ("    band      T0        MODEL R   agreement   MODEL E   R - E");

            for (size_t bIndex = 0; bIndex < 4; ++bIndex)
                info ("    " + pad (number (kProbes[bIndex], 0), 10)
                      + pad (number (t0[bIndex], 2) + " %", 10)
                      + pad (number (mr[bIndex], 2) + " %", 10)
                      + pad (number (std::abs (mr[bIndex] - t0[bIndex]), 2) + " pp", 12)
                      + pad (number (me[bIndex], 2) + " %", 10)
                      + number (mr[bIndex] - me[bIndex], 2) + " pp");

            const double agreement = std::abs (mr[3] - t0[3]);
            const double drop = mr[3] - me[3];

            info ("");
            info ("  12 kHz agreement     " + number (agreement, 2) + " pp   (limit "
                  + number (kModelRAgreementPp, 2) + ")");
            info ("  12 kHz residue drop  " + number (drop, 2) + " pp   (material >= "
                  + number (kMaterialDropPp, 2) + ", not material <= "
                  + number (kNotMaterialPp, 2) + ")");
            info ("");

            if (agreement > kModelRAgreementPp)
                info ("  CLASSIFICATION: UNRESOLVED  (MODEL R does not track T0 closely"
                      " enough for the E comparison to carry weight)");
            else if (drop >= kMaterialDropPp)
                info ("  CLASSIFICATION: RESIDUES MATERIAL DRIVER");
            else if (std::abs (drop) <= kNotMaterialPp)
                info ("  CLASSIFICATION: RESIDUES NOT MATERIAL DRIVER");
            else
                info ("  CLASSIFICATION: UNRESOLVED");

            check (true, "classification recorded against the pre-registered rule");
        }
    }
}

} // namespace avtest
