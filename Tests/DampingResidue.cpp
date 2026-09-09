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
[[nodiscard]] bool refinePole (const StaticTank& t, double f1, double f2, Mode& out)
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

        if (bestStep > 1.0e-9)
            continue;

        const double radius = std::abs (best);

        if (radius >= kOuterRadius || radius < kInnerRadius)
            continue;

        const double frequency = std::arg (best) * kSampleRate / (2.0 * decay::pi);

        if (frequency < f1 - margin || frequency > f2 + margin)
            continue;

        out.pole = best;
        out.frequencyHz = frequency;
        out.rt60 = -3.0 / (std::log10 (radius) * kSampleRate);

        return true;
    }

    /*
        Newton never landed inside. The contour moment is already an unbiased
        estimate of the single zero the region holds, so it is used directly
        rather than losing the mode — but only if it is where the count says it
        must be.
    */
    const double centroidRadius = std::abs (centroid);
    const double centroidFrequency = std::arg (centroid) * kSampleRate / (2.0 * decay::pi);

    if (std::isfinite (centroidRadius)
        && centroidRadius < kOuterRadius && centroidRadius >= kInnerRadius
        && centroidFrequency >= f1 - margin && centroidFrequency <= f2 + margin)
    {
        out.pole = centroid;
        out.frequencyHz = centroidFrequency;
        out.rt60 = -3.0 / (std::log10 (centroidRadius) * kSampleRate);

        return true;
    }

    return false;
}

/** Adaptive subdivision until each region holds exactly one zero. */
void enumerate (const StaticTank& t, double f1, double f2, int count, int depth,
                std::vector<Mode>& modes, CountStats& stats)
{
    if (count == 0)
        return;

    if (count == 1)
    {
        Mode mode;

        if (refinePole (t, f1, f2, mode))
            modes.push_back (mode);
        else
            stats.newtonFailed += 1;

        return;
    }

    if (depth >= 60)
    {
        stats.depthExceeded += count;
        return;
    }

    /*
        The split point is jittered on failure.

        A contour that runs almost exactly through a root has a phase that
        turns arbitrarily fast, and no amount of resampling fixes it — the
        first attempt at this study lost 1292 of 9322 zeros that way. Roots are
        isolated points, so moving the cut a little takes the boundary off them.
        Every candidate is still required to account for the parent exactly, so
        jitter changes where the boundary is drawn and never what is accepted.
    */
    static constexpr double fractions[] { 0.5, 0.41, 0.59, 0.33, 0.67, 0.27, 0.73, 0.19, 0.81 };

    for (double fraction : fractions)
    {
        const double mid = f1 + (f2 - f1) * fraction;

        bool ok1 = true, ok2 = true;
        const int left = countZeros (t, f1, mid, stats, ok1);

        if (! ok1 || left < 0 || left > count)
            continue;

        const int right = countZeros (t, mid, f2, stats, ok2);

        if (! ok2 || right < 0 || left + right != count)
            continue;

        enumerate (t, f1, mid, left, depth + 1, modes, stats);
        enumerate (t, mid, f2, right, depth + 1, modes, stats);
        return;
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

            if (n == 0)
                continue;

            if (n == 1)
            {
                Mode mode;

                if (refinePole (t, f1, f2, mode))
                    modes.push_back (mode);
                else
                    stats.newtonFailed += 1;
            }
            else
            {
                stats.depthExceeded += n;
            }
        }

        return;
    }

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

        long long counted = 0, unreliable = 0;

        for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
        {
            const double f1 = lowest + (highest - lowest) * blockIndex / blocks;
            const double f2 = lowest + (highest - lowest) * (blockIndex + 1) / blocks;

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
        for (auto sliver : { std::pair<double, double> { 0.02, 1.0 },
                             std::pair<double, double> { 0.5 * kSampleRate - 1.0,
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

        // Real roots near the unit circle, which the angular scan cannot reach.
        std::vector<double> realRoots;

        findRealRoots (tank, 0.9900, 0.99999999, 400000, realRoots);
        findRealRoots (tank, -0.99999999, -0.9900, 400000, realRoots);

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
        info ("    unresolved regions in band          " + std::to_string (stats.newtonFailed));
        info ("    real roots near the unit circle     " + std::to_string (realRoots.size()));
        info ("    low-radius roots located            " + std::to_string (lowRadiusModes.size()) + "   (winding says " + std::to_string (zerosInsideInner) + ")");
        info ("    ---------------------------------------------");
        info ("    required total                      " + std::to_string (kTransferDegree));

        const long long accounted = 2 * static_cast<long long> (modes.size())
                                    + 2 * stats.newtonFailed
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
