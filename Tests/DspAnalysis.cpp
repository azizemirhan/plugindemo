#include "DspAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** Fraction of a Gaussian process lying more than one sigma from the mean. */
    constexpr double gaussianOutsideOneSigma = 0.3173105078629141;

    double toDb (double magnitude)
    {
        return 20.0 * std::log10 (std::max (magnitude, 1.0e-300));
    }
}

// ===========================================================================
//  Spectra
// ===========================================================================

void fft (std::vector<std::complex<double>>& data)
{
    const size_t n = data.size();

    if (n < 2)
        return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * pi / static_cast<double> (len);
        const std::complex<double> step { std::cos (angle), std::sin (angle) };

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w { 1.0, 0.0 };

            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * w;

                data[i + k]           = u + v;
                data[i + k + len / 2] = u - v;

                w *= step;
            }
        }
    }
}

void ifft (std::vector<std::complex<double>>& data)
{
    for (auto& v : data)
        v = std::conj (v);

    fft (data);

    const double scale = 1.0 / static_cast<double> (data.size());

    for (auto& v : data)
        v = std::conj (v) * scale;
}

std::vector<double> bandLimited (const std::vector<double>& signal, double sampleRate,
                                 double lowHz, double highHz)
{
    size_t fftSize = 1;
    while (fftSize < signal.size())
        fftSize <<= 1;

    std::vector<std::complex<double>> buffer (fftSize, { 0.0, 0.0 });

    for (size_t i = 0; i < signal.size(); ++i)
        buffer[i] = { signal[i], 0.0 };

    fft (buffer);

    const double binHz = sampleRate / static_cast<double> (fftSize);

    for (size_t k = 0; k <= fftSize / 2; ++k)
    {
        const double hz = static_cast<double> (k) * binHz;

        if (hz < lowHz || hz > highHz)
        {
            buffer[k] = { 0.0, 0.0 };

            if (k > 0 && k < fftSize / 2)
                buffer[fftSize - k] = { 0.0, 0.0 };
        }
    }

    ifft (buffer);

    std::vector<double> out (signal.size());

    for (size_t i = 0; i < out.size(); ++i)
        out[i] = buffer[i].real();

    return out;
}

std::vector<double> magnitudeSpectrum (const double* signal, size_t length,
                                       size_t fftSize, bool windowed)
{
    std::vector<std::complex<double>> buffer (fftSize, { 0.0, 0.0 });
    const size_t count = std::min (length, fftSize);

    for (size_t i = 0; i < count; ++i)
    {
        double w = 1.0;

        if (windowed && count > 1)
            w = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i)
                                      / static_cast<double> (count - 1));

        buffer[i] = { signal[i] * w, 0.0 };
    }

    fft (buffer);

    std::vector<double> magnitude (fftSize / 2 + 1);

    for (size_t k = 0; k < magnitude.size(); ++k)
        magnitude[k] = std::abs (buffer[k]);

    return magnitude;
}

std::vector<double> magnitudeSpectrum (const std::vector<double>& signal,
                                       size_t fftSize, bool windowed)
{
    return magnitudeSpectrum (signal.data(), signal.size(), fftSize, windowed);
}

size_t binForFrequency (double hz, size_t fftSize, double sampleRate)
{
    const double bin = hz * static_cast<double> (fftSize) / sampleRate;
    return static_cast<size_t> (std::max (0.0, std::round (bin)));
}

double spectralFlatness (const std::vector<double>& magnitude, double sampleRate,
                         size_t fftSize, double lowHz, double highHz)
{
    const size_t lo = std::max<size_t> (1, binForFrequency (lowHz, fftSize, sampleRate));
    const size_t hi = std::min (magnitude.size() - 1, binForFrequency (highHz, fftSize, sampleRate));

    if (hi <= lo)
        return 0.0;

    double logSum = 0.0, linearSum = 0.0;
    size_t count = 0;

    for (size_t k = lo; k <= hi; ++k)
    {
        const double power = std::max (magnitude[k] * magnitude[k], 1.0e-300);
        logSum    += std::log (power);
        linearSum += power;
        ++count;
    }

    const double geometric = std::exp (logSum / static_cast<double> (count));
    const double arithmetic = linearSum / static_cast<double> (count);

    return arithmetic > 0.0 ? geometric / arithmetic : 0.0;
}

double peakAboveMedianDb (const std::vector<double>& magnitude, double sampleRate,
                          size_t fftSize, double lowHz, double highHz)
{
    const size_t lo = std::max<size_t> (1, binForFrequency (lowHz, fftSize, sampleRate));
    const size_t hi = std::min (magnitude.size() - 1, binForFrequency (highHz, fftSize, sampleRate));

    if (hi <= lo)
        return 0.0;

    std::vector<double> band (magnitude.begin() + static_cast<ptrdiff_t> (lo),
                              magnitude.begin() + static_cast<ptrdiff_t> (hi) + 1);

    const double highest = *std::max_element (band.begin(), band.end());

    std::nth_element (band.begin(), band.begin() + static_cast<ptrdiff_t> (band.size() / 2), band.end());
    const double median = band[band.size() / 2];

    return toDb (highest) - toDb (median);
}

BandDeviation bandDeviation (const std::vector<double>& magnitude, double sampleRate,
                             size_t fftSize, double lowHz, double highHz)
{
    BandDeviation result;

    const size_t lo = std::max<size_t> (1, binForFrequency (lowHz, fftSize, sampleRate));
    const size_t hi = std::min (magnitude.size() - 1, binForFrequency (highHz, fftSize, sampleRate));

    if (hi <= lo)
        return result;

    double sumDb = 0.0;
    size_t count = 0;

    for (size_t k = lo; k <= hi; ++k)
    {
        sumDb += toDb (magnitude[k]);
        ++count;
    }

    result.meanDb = sumDb / static_cast<double> (count);

    for (size_t k = lo; k <= hi; ++k)
    {
        const double deviation = toDb (magnitude[k]) - result.meanDb;

        result.maxAboveDb = std::max (result.maxAboveDb, deviation);
        result.maxBelowDb = std::min (result.maxBelowDb, deviation);
    }

    result.maxBelowDb = -result.maxBelowDb;
    return result;
}

ThirdOctaveBands thirdOctaveEnergy (const std::vector<double>& magnitude, double sampleRate,
                                    size_t fftSize, double lowHz, double highHz)
{
    ThirdOctaveBands bands;

    const double ratio = std::pow (2.0, 1.0 / 3.0);
    const double edgeRatio = std::pow (2.0, 1.0 / 6.0);

    for (double centre = 25.0; centre <= highHz; centre *= ratio)
    {
        if (centre < lowHz)
            continue;

        const double lowEdge  = centre / edgeRatio;
        const double highEdge = centre * edgeRatio;

        const size_t lo = std::max<size_t> (1, binForFrequency (lowEdge, fftSize, sampleRate));
        const size_t hi = std::min (magnitude.size() - 1, binForFrequency (highEdge, fftSize, sampleRate));

        if (hi < lo)
            continue;

        double sum = 0.0;
        for (size_t k = lo; k <= hi; ++k)
            sum += magnitude[k] * magnitude[k];

        const double bandCount = static_cast<double> (hi - lo + 1);

        bands.centreHz.push_back (centre);
        bands.energyDb.push_back (10.0 * std::log10 (std::max (sum / bandCount, 1.0e-300)));
    }

    return bands;
}

// ===========================================================================
//  Time-domain measures
// ===========================================================================

double energy (const double* x, size_t n)
{
    double sum = 0.0;

    for (size_t i = 0; i < n; ++i)
        sum += x[i] * x[i];

    return sum;
}

double energy (const std::vector<double>& x) { return energy (x.data(), x.size()); }

double rms (const double* x, size_t n)
{
    return n == 0 ? 0.0 : std::sqrt (energy (x, n) / static_cast<double> (n));
}

double rms (const std::vector<double>& x) { return rms (x.data(), x.size()); }

double peakAbsolute (const double* x, size_t n)
{
    double worst = 0.0;

    for (size_t i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (x[i]));

    return worst;
}

double peakAbsolute (const std::vector<double>& x) { return peakAbsolute (x.data(), x.size()); }

double crestFactor (const double* x, size_t n)
{
    const double r = rms (x, n);
    return r > 0.0 ? peakAbsolute (x, n) / r : 0.0;
}

double mean (const double* x, size_t n)
{
    if (n == 0)
        return 0.0;

    // Compensated summation: the guard signal being measured here is 25 orders
    // of magnitude below 1, so naive accumulation would lose it entirely.
    double sum = 0.0, compensation = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const double y = x[i] - compensation;
        const double t = sum + y;
        compensation = (t - sum) - y;
        sum = t;
    }

    return sum / static_cast<double> (n);
}

double mean (const std::vector<double>& x) { return mean (x.data(), x.size()); }

double excessKurtosis (const double* x, size_t n)
{
    if (n < 4)
        return 0.0;

    const double m = mean (x, n);

    double variance = 0.0, fourth = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const double d = x[i] - m;
        variance += d * d;
        fourth   += d * d * d * d;
    }

    variance /= static_cast<double> (n);
    fourth   /= static_cast<double> (n);

    return variance > 0.0 ? (fourth / (variance * variance)) - 3.0 : 0.0;
}

double normalisedEchoDensity (const double* impulseResponse, size_t length,
                              size_t windowStart, size_t windowLength)
{
    if (windowStart >= length)
        return 0.0;

    const size_t count = std::min (windowLength, length - windowStart);

    if (count < 8)
        return 0.0;

    const double* w = impulseResponse + windowStart;

    double sum = 0.0;
    for (size_t i = 0; i < count; ++i)
        sum += w[i];

    const double m = sum / static_cast<double> (count);

    double variance = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double d = w[i] - m;
        variance += d * d;
    }

    const double sigma = std::sqrt (variance / static_cast<double> (count));

    if (sigma <= 0.0)
        return 0.0;

    // A window that holds nothing but anti-denormal guard noise has a perfectly
    // well-defined standard deviation and would score as "dense". Gate on the
    // window carrying a real share of the response's peak, so an empty window
    // reports no density instead of noise-shaped nonsense.
    const double referencePeak = peakAbsolute (impulseResponse, length);

    if (referencePeak <= 0.0 || sigma < 1.0e-9 * referencePeak)
        return 0.0;

    size_t exceeding = 0;
    for (size_t i = 0; i < count; ++i)
        if (std::abs (w[i] - m) > sigma)
            ++exceeding;

    return static_cast<double> (exceeding)
         / (gaussianOutsideOneSigma * static_cast<double> (count));
}

double maxEnvelopePeriodicity (const double* impulseResponse, size_t length,
                               size_t minLag, size_t maxLag)
{
    if (length < 8 || minLag >= length)
        return 0.0;

    std::vector<double> envelope (length);

    for (size_t i = 0; i < length; ++i)
        envelope[i] = impulseResponse[i] * impulseResponse[i];

    // Detrend first. The envelope of any decaying response is dominated by the
    // decay itself, and autocorrelating that would score every reverb as highly
    // periodic regardless of its echo pattern. Dividing by a smoothed copy of
    // the envelope removes the decay and leaves only the structure on top of
    // it, which is the thing being measured.
    std::vector<double> trend (length);
    {
        constexpr double coefficient = 0.002; // ~500 sample time constant
        double state = envelope[0];

        for (size_t i = 0; i < length; ++i)
        {
            state += coefficient * (envelope[i] - state);
            trend[i] = state;
        }

        // A second pass backwards, so the trend is not delayed relative to the
        // envelope it is meant to follow.
        state = trend[length - 1];

        for (size_t i = length; i-- > 0;)
        {
            state += coefficient * (trend[i] - state);
            trend[i] = state;
        }
    }

    const double floorLevel = 1.0e-30 + 1.0e-6 * mean (envelope.data(), length);

    for (size_t i = 0; i < length; ++i)
        envelope[i] = envelope[i] / (trend[i] + floorLevel) - 1.0;

    const double m = mean (envelope.data(), length);

    for (auto& v : envelope)
        v -= m;

    const double zeroLag = energy (envelope.data(), length);

    if (zeroLag <= 0.0)
        return 0.0;

    const size_t highest = std::min (maxLag, length - 1);

    if (highest <= minLag)
        return 0.0;

    std::vector<double> autocorrelation;
    autocorrelation.reserve (highest - minLag + 1);

    for (size_t lag = minLag; lag <= highest; ++lag)
    {
        double sum = 0.0;

        for (size_t i = 0; i + lag < length; ++i)
            sum += envelope[i] * envelope[i + lag];

        autocorrelation.push_back (std::abs (sum) / zeroLag);
    }

    // The tallest lag relative to the typical lag, not relative to zero. Every
    // signal has some autocorrelation at every lag; what marks a periodic echo
    // pattern is one lag standing far above the rest.
    const double highestLag = *std::max_element (autocorrelation.begin(), autocorrelation.end());

    std::nth_element (autocorrelation.begin(),
                      autocorrelation.begin() + static_cast<ptrdiff_t> (autocorrelation.size() / 2),
                      autocorrelation.end());

    const double typical = autocorrelation[autocorrelation.size() / 2];

    return typical > 0.0 ? highestLag / typical : 0.0;
}

std::vector<double> flattenDecay (const double* signal, size_t length, double envelopeCoefficient)
{
    std::vector<double> out (length, 0.0);

    if (length == 0)
        return out;

    // Forward then backward one-pole over the rectified signal, so the envelope
    // follows the decay without lagging behind it.
    std::vector<double> envelope (length);
    double state = std::abs (signal[0]);

    for (size_t i = 0; i < length; ++i)
    {
        state += envelopeCoefficient * (std::abs (signal[i]) - state);
        envelope[i] = state;
    }

    state = envelope[length - 1];

    for (size_t i = length; i-- > 0;)
    {
        state += envelopeCoefficient * (envelope[i] - state);
        envelope[i] = state;
    }

    double mean = 0.0;
    for (double v : envelope)
        mean += v;

    mean /= static_cast<double> (length);

    const double floorLevel = 1.0e-30 + 1.0e-4 * mean;

    for (size_t i = 0; i < length; ++i)
        out[i] = signal[i] / (envelope[i] + floorLevel);

    return out;
}

double correlation (const double* a, const double* b, size_t n)
{
    if (n == 0)
        return 0.0;

    const double meanA = mean (a, n);
    const double meanB = mean (b, n);

    double covariance = 0.0, varA = 0.0, varB = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;

        covariance += da * db;
        varA += da * da;
        varB += db * db;
    }

    const double denominator = std::sqrt (varA * varB);

    return denominator > 0.0 ? covariance / denominator : 0.0;
}

double correlation (const std::vector<double>& a, const std::vector<double>& b)
{
    return correlation (a.data(), b.data(), std::min (a.size(), b.size()));
}

bool allFinite (const double* x, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (! std::isfinite (x[i]))
            return false;

    return true;
}

bool allFinite (const std::vector<double>& x) { return allFinite (x.data(), x.size()); }

bool anySubnormal (const double* x, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (std::fpclassify (x[i]) == FP_SUBNORMAL)
            return true;

    return false;
}

// ===========================================================================
//  Signals
// ===========================================================================

std::vector<double> impulse (size_t length)
{
    std::vector<double> x (length, 0.0);

    if (length > 0)
        x[0] = 1.0;

    return x;
}

std::vector<double> whiteNoise (size_t length, unsigned int seed)
{
    std::vector<double> x (length);
    unsigned int state = seed != 0 ? seed : 0x9E3779B9u;

    for (size_t i = 0; i < length; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        x[i] = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;
    }

    return x;
}

std::vector<double> sineBurst (size_t length, size_t burstLength,
                               double frequencyHz, double sampleRate)
{
    std::vector<double> x (length, 0.0);
    const size_t count = std::min (burstLength, length);

    for (size_t i = 0; i < count; ++i)
    {
        const double w = count > 1
            ? 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (count - 1))
            : 1.0;

        x[i] = w * std::sin (2.0 * pi * frequencyHz * static_cast<double> (i) / sampleRate);
    }

    return x;
}

std::vector<double> transientBurst (size_t length, double sampleRate, unsigned int seed)
{
    // A short noise burst under a fast exponential envelope: broadband, with a
    // sharp attack, which is what makes smearing audible.
    std::vector<double> x = whiteNoise (length, seed);

    const double decaySamples = 0.004 * sampleRate;

    for (size_t i = 0; i < length; ++i)
        x[i] *= std::exp (-static_cast<double> (i) / decaySamples);

    return x;
}

} // namespace avtest
