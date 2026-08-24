#include "FdnAnalysis.h"

#include "DspAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avtest
{

namespace
{
    /** First sample index at which the decay curve has fallen below `db`. */
    [[nodiscard]] double crossingSample (const std::vector<double>& curveDb, double db)
    {
        for (size_t i = 1; i < curveDb.size(); ++i)
        {
            if (curveDb[i] <= db)
            {
                // Linear interpolation between the bracketing samples, so the
                // answer does not quantise to the sample grid on short decays.
                const double previous = curveDb[i - 1];
                const double span = previous - curveDb[i];

                if (span <= 0.0)
                    return static_cast<double> (i);

                return static_cast<double> (i - 1) + (previous - db) / span;
            }
        }

        return -1.0;
    }

    /** R^2 of a straight-line fit to curveDb between two sample indices. */
    [[nodiscard]] double fitQuality (const std::vector<double>& curveDb, size_t from, size_t to)
    {
        if (to <= from + 2 || to > curveDb.size())
            return 0.0;

        const auto n = static_cast<double> (to - from);

        double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;

        for (size_t i = from; i < to; ++i)
        {
            const double x = static_cast<double> (i - from);
            const double y = curveDb[i];

            sumX += x; sumY += y; sumXY += x * y; sumXX += x * x;
        }

        const double meanX = sumX / n;
        const double meanY = sumY / n;
        const double slope = (sumXY - n * meanX * meanY) / std::max (1.0e-30, sumXX - n * meanX * meanX);
        const double intercept = meanY - slope * meanX;

        double residual = 0.0, total = 0.0;

        for (size_t i = from; i < to; ++i)
        {
            const double x = static_cast<double> (i - from);
            const double predicted = slope * x + intercept;

            residual += (curveDb[i] - predicted) * (curveDb[i] - predicted);
            total    += (curveDb[i] - meanY) * (curveDb[i] - meanY);
        }

        return total > 0.0 ? 1.0 - residual / total : 0.0;
    }

    [[nodiscard]] int gcdOf (int a, int b)
    {
        while (b != 0)
        {
            const int t = a % b;
            a = b;
            b = t;
        }

        return a < 0 ? -a : a;
    }
}

std::vector<double> energyDecayCurve (const std::vector<double>& impulseResponse)
{
    std::vector<double> curve (impulseResponse.size(), 0.0);

    if (impulseResponse.empty())
        return curve;

    // Backward cumulative sum of the squared response.
    double running = 0.0;

    for (size_t i = impulseResponse.size(); i-- > 0;)
    {
        running += impulseResponse[i] * impulseResponse[i];
        curve[i] = running;
    }

    const double total = curve[0];

    if (total <= 0.0)
        return std::vector<double> (impulseResponse.size(), -300.0);

    for (auto& v : curve)
        v = 10.0 * std::log10 (std::max (v / total, 1.0e-30));

    return curve;
}

DecayMeasurement measureDecay (const std::vector<double>& impulseResponse, double sampleRate)
{
    DecayMeasurement result;
    result.totalEnergy = energy (impulseResponse);

    if (impulseResponse.empty() || sampleRate <= 0.0 || result.totalEnergy <= 0.0)
        return result;

    const auto curve = energyDecayCurve (impulseResponse);

    const double at0  = crossingSample (curve, -0.1);
    const double at5  = crossingSample (curve, -5.0);
    const double at10 = crossingSample (curve, -10.0);
    const double at25 = crossingSample (curve, -25.0);
    const double at35 = crossingSample (curve, -35.0);

    if (at5 >= 0.0 && at35 > at5)
    {
        result.rt60FromT30 = 2.0 * (at35 - at5) / sampleRate;
        result.valid = true;
    }

    if (at5 >= 0.0 && at25 > at5)
        result.rt60FromT20 = 3.0 * (at25 - at5) / sampleRate;

    if (at0 >= 0.0 && at10 > at0)
        result.earlyDecayTime = 6.0 * (at10 - at0) / sampleRate;

    if (result.valid)
        result.linearity = fitQuality (curve, static_cast<size_t> (at5), static_cast<size_t> (at35));

    return result;
}

DecayMeasurement measureBandDecay (const std::vector<double>& impulseResponse, double sampleRate,
                                   double lowHz, double highHz)
{
    return measureDecay (bandLimited (impulseResponse, sampleRate, lowHz, highHz), sampleRate);
}

// ===========================================================================
//  Feedback operator analysis
// ===========================================================================

double largestSingularValue (const double* matrix, int n, int iterations)
{
    std::vector<double> v (static_cast<size_t> (n), 1.0 / std::sqrt (static_cast<double> (n)));
    double sigma = 0.0;

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        std::vector<double> mv (static_cast<size_t> (n), 0.0);

        for (int row = 0; row < n; ++row)
            for (int column = 0; column < n; ++column)
                mv[static_cast<size_t> (row)] += matrix[row * n + column] * v[static_cast<size_t> (column)];

        std::vector<double> mtmv (static_cast<size_t> (n), 0.0);

        for (int column = 0; column < n; ++column)
            for (int row = 0; row < n; ++row)
                mtmv[static_cast<size_t> (column)] += matrix[row * n + column] * mv[static_cast<size_t> (row)];

        double norm = 0.0;
        for (double x : mtmv)
            norm += x * x;

        norm = std::sqrt (norm);

        if (norm <= 0.0)
            return 0.0;

        for (int i = 0; i < n; ++i)
            v[static_cast<size_t> (i)] = mtmv[static_cast<size_t> (i)] / norm;

        sigma = std::sqrt (norm);
    }

    return sigma;
}

namespace
{
    void multiply (const std::vector<double>& a, const std::vector<double>& b,
                   std::vector<double>& out, int n)
    {
        out.assign (static_cast<size_t> (n * n), 0.0);

        for (int i = 0; i < n; ++i)
            for (int k = 0; k < n; ++k)
            {
                const double aik = a[static_cast<size_t> (i * n + k)];

                if (aik == 0.0)
                    continue;

                for (int j = 0; j < n; ++j)
                    out[static_cast<size_t> (i * n + j)] += aik * b[static_cast<size_t> (k * n + j)];
            }
    }

    double frobenius (const std::vector<double>& m)
    {
        double sum = 0.0;

        for (double v : m)
            sum += v * v;

        return std::sqrt (sum);
    }
}

double spectralRadiusByGelfand (const double* matrix, int n)
{
    std::vector<double> p (matrix, matrix + n * n);

    double norm = frobenius (p);

    if (norm <= 0.0)
        return 0.0;

    for (auto& v : p)
        v /= norm;

    double logScale = std::log (norm);
    double power = 1.0;

    std::vector<double> squared;

    for (int step = 0; step < 30; ++step)
    {
        multiply (p, p, squared, n);

        const double squaredNorm = frobenius (squared);

        if (squaredNorm <= 0.0)
            return 0.0;

        for (auto& v : squared)
            v /= squaredNorm;

        p.swap (squared);

        logScale = 2.0 * logScale + std::log (squaredNorm);
        power *= 2.0;
    }

    return std::exp (logScale / power);
}

std::vector<std::complex<double>> eigenvalues (const double* matrix, int n)
{
    std::vector<double> h (matrix, matrix + n * n);

    auto at = [&h, n] (int i, int j) -> double& { return h[static_cast<size_t> (i * n + j)]; };

    // --- Householder reduction to upper Hessenberg form -------------------

    for (int k = 0; k < n - 2; ++k)
    {
        double normBelow = 0.0;

        for (int i = k + 1; i < n; ++i)
            normBelow += at (i, k) * at (i, k);

        normBelow = std::sqrt (normBelow);

        if (normBelow < 1.0e-300)
            continue;

        if (at (k + 1, k) > 0.0)
            normBelow = -normBelow;

        std::vector<double> v (static_cast<size_t> (n), 0.0);

        for (int i = k + 1; i < n; ++i)
            v[static_cast<size_t> (i)] = at (i, k);

        v[static_cast<size_t> (k + 1)] -= normBelow;

        double vNorm = 0.0;
        for (int i = k + 1; i < n; ++i)
            vNorm += v[static_cast<size_t> (i)] * v[static_cast<size_t> (i)];

        if (vNorm < 1.0e-300)
            continue;

        // H := (I - 2vv^T/v^Tv) H (I - 2vv^T/v^Tv)
        for (int j = 0; j < n; ++j)
        {
            double dot = 0.0;

            for (int i = k + 1; i < n; ++i)
                dot += v[static_cast<size_t> (i)] * at (i, j);

            const double factor = 2.0 * dot / vNorm;

            for (int i = k + 1; i < n; ++i)
                at (i, j) -= factor * v[static_cast<size_t> (i)];
        }

        for (int i = 0; i < n; ++i)
        {
            double dot = 0.0;

            for (int j = k + 1; j < n; ++j)
                dot += at (i, j) * v[static_cast<size_t> (j)];

            const double factor = 2.0 * dot / vNorm;

            for (int j = k + 1; j < n; ++j)
                at (i, j) -= factor * v[static_cast<size_t> (j)];
        }
    }

    // --- Shifted QR with deflation ----------------------------------------

    std::vector<std::complex<double>> result;
    result.reserve (static_cast<size_t> (n));

    int high = n - 1;
    int iterations = 0;

    while (high >= 0 && iterations < 4000)
    {
        // Find the top of the active block.
        int low = high;

        while (low > 0)
        {
            const double scale = std::abs (at (low - 1, low - 1)) + std::abs (at (low, low));

            if (std::abs (at (low, low - 1)) <= 1.0e-14 * std::max (scale, 1.0e-300))
            {
                at (low, low - 1) = 0.0;
                break;
            }

            --low;
        }

        if (low == high)
        {
            result.emplace_back (at (high, high), 0.0);
            --high;
            iterations = 0;
            continue;
        }

        if (low == high - 1)
        {
            // 2x2 block: solve its characteristic quadratic directly.
            const double a = at (high - 1, high - 1);
            const double b = at (high - 1, high);
            const double c = at (high, high - 1);
            const double d = at (high, high);

            const double trace = a + d;
            const double det   = a * d - b * c;
            const double disc  = trace * trace * 0.25 - det;

            if (disc >= 0.0)
            {
                const double root = std::sqrt (disc);
                result.emplace_back (trace * 0.5 + root, 0.0);
                result.emplace_back (trace * 0.5 - root, 0.0);
            }
            else
            {
                const double imaginary = std::sqrt (-disc);
                result.emplace_back (trace * 0.5,  imaginary);
                result.emplace_back (trace * 0.5, -imaginary);
            }

            high -= 2;
            iterations = 0;
            continue;
        }

        // Wilkinson shift, with an exceptional shift if it stalls.
        double shift = at (high, high);

        if (iterations > 0 && iterations % 12 == 0)
            shift = std::abs (at (high, high - 1)) + std::abs (at (high - 1, high - 2));

        for (int i = low; i <= high; ++i)
            at (i, i) -= shift;

        // QR step by Givens rotations on the Hessenberg block.
        std::vector<double> cosines (static_cast<size_t> (n), 1.0);
        std::vector<double> sines (static_cast<size_t> (n), 0.0);

        for (int k = low; k < high; ++k)
        {
            const double x = at (k, k);
            const double y = at (k + 1, k);
            const double norm = std::hypot (x, y);

            const double c = norm > 0.0 ? x / norm : 1.0;
            const double s = norm > 0.0 ? y / norm : 0.0;

            cosines[static_cast<size_t> (k)] = c;
            sines[static_cast<size_t> (k)] = s;

            for (int j = k; j < n; ++j)
            {
                const double t1 = at (k, j);
                const double t2 = at (k + 1, j);

                at (k, j)     =  c * t1 + s * t2;
                at (k + 1, j) = -s * t1 + c * t2;
            }
        }

        for (int k = low; k < high; ++k)
        {
            const double c = cosines[static_cast<size_t> (k)];
            const double s = sines[static_cast<size_t> (k)];

            for (int i = 0; i <= std::min (high, k + 2); ++i)
            {
                const double t1 = at (i, k);
                const double t2 = at (i, k + 1);

                at (i, k)     =  c * t1 + s * t2;
                at (i, k + 1) = -s * t1 + c * t2;
            }
        }

        for (int i = low; i <= high; ++i)
            at (i, i) += shift;

        ++iterations;
    }

    // Anything left undeflated after the iteration cap is reported as-is, so a
    // failure shows up as a wrong answer rather than as a missing one.
    while (static_cast<int> (result.size()) < n)
    {
        result.emplace_back (at (high, high), 0.0);
        --high;
    }

    return result;
}

double spectralRadius (const double* matrix, int n)
{
    double worst = 0.0;

    for (const auto& lambda : eigenvalues (matrix, n))
        worst = std::max (worst, std::abs (lambda));

    return worst;
}

// ===========================================================================
//  Delay-set analysis
// ===========================================================================

int worstPairwiseGcd (const int* delaySamples, int count)
{
    int worst = 1;

    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            worst = std::max (worst, gcdOf (delaySamples[i], delaySamples[j]));

    return worst;
}

double meanModalSpacingHz (const int* delaySamples, int count, double sampleRate)
{
    double total = 0.0;

    for (int i = 0; i < count; ++i)
        total += delaySamples[i];

    return total > 0.0 ? sampleRate / total : 0.0;
}

double modalSpacingSpread (const int* delaySamples, int count,
                           double sampleRate, double upToHz)
{
    std::vector<double> modes;
    modes.reserve (4096);

    for (int i = 0; i < count; ++i)
    {
        if (delaySamples[i] <= 0)
            continue;

        const double spacing = sampleRate / static_cast<double> (delaySamples[i]);

        for (double f = spacing; f <= upToHz; f += spacing)
            modes.push_back (f);
    }

    if (modes.size() < 8)
        return 0.0;

    std::sort (modes.begin(), modes.end());

    std::vector<double> gaps;
    gaps.reserve (modes.size());

    for (size_t i = 1; i < modes.size(); ++i)
        gaps.push_back (modes[i] - modes[i - 1]);

    const double meanGap = std::accumulate (gaps.begin(), gaps.end(), 0.0)
                         / static_cast<double> (gaps.size());

    if (meanGap <= 0.0)
        return 0.0;

    double variance = 0.0;

    for (double g : gaps)
        variance += (g - meanGap) * (g - meanGap);

    variance /= static_cast<double> (gaps.size());

    return std::sqrt (variance) / meanGap;
}

double worstSubsetSumCollision (const double* delays, int count)
{
    double worst = 1.0e9;

    for (int mask = 1; mask < (1 << count); ++mask)
    {
        int bits = 0;
        double sum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            if (mask & (1 << i))
            {
                sum += delays[i];
                ++bits;
            }
        }

        if (bits < 2)
            continue;

        for (int j = 0; j < count; ++j)
            if ((mask & (1 << j)) == 0)
                worst = std::min (worst, std::abs (sum - delays[j]) / delays[j]);
    }

    return worst;
}

double worstSimpleRatioProximity (const double* delays, int count)
{
    constexpr int maxNumerator   = 24;
    constexpr int maxDenominator = 3;

    double worst = 1.0e9;

    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            const double ratio = delays[j] / delays[i];

            for (int q = 1; q <= maxDenominator; ++q)
                for (int p = 1; p <= maxNumerator; ++p)
                {
                    const double simple = static_cast<double> (p) / static_cast<double> (q);
                    worst = std::min (worst, std::abs (ratio - simple) / simple);
                }
        }
    }

    return worst;
}

} // namespace avtest
