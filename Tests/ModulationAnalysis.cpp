#include "ModulationAnalysis.h"

#include "DspAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avtest
{

namespace
{
    size_t binOf (double hz, size_t fftSize, double sampleRate)
    {
        return static_cast<size_t> (std::max (0.0, std::round (hz * static_cast<double> (fftSize) / sampleRate)));
    }

    double frequencyOf (size_t bin, size_t fftSize, double sampleRate)
    {
        return static_cast<double> (bin) * sampleRate / static_cast<double> (fftSize);
    }
}

Spectrogram spectrogramOf (const std::vector<double>& signal, double sampleRate,
                           size_t fftSize, size_t hop)
{
    Spectrogram s;
    s.fftSize = fftSize;
    s.sampleRate = sampleRate;
    s.hopSeconds = static_cast<double> (hop) / sampleRate;

    if (signal.size() < fftSize || hop == 0)
        return s;

    for (size_t start = 0; start + fftSize <= signal.size(); start += hop)
        s.frames.push_back (magnitudeSpectrum (signal.data() + start, fftSize, fftSize, true));

    return s;
}

double spectralFlux (const Spectrogram& s, double lowHz, double highHz)
{
    if (s.frames.size() < 2)
        return 0.0;

    const size_t lo = std::max<size_t> (1, binOf (lowHz, s.fftSize, s.sampleRate));
    const size_t hi = std::min (s.frames[0].size() - 1, binOf (highHz, s.fftSize, s.sampleRate));

    if (hi <= lo)
        return 0.0;

    double total = 0.0;

    for (size_t f = 1; f < s.frames.size(); ++f)
    {
        double rise = 0.0, energy = 0.0;

        for (size_t k = lo; k <= hi; ++k)
        {
            const double difference = s.frames[f][k] - s.frames[f - 1][k];

            if (difference > 0.0)
                rise += difference;

            energy += s.frames[f][k];
        }

        if (energy > 0.0)
            total += rise / energy;
    }

    return total / static_cast<double> (s.frames.size() - 1);
}

std::vector<double> peakFrequencies (const std::vector<double>& magnitude, double sampleRate,
                                     size_t fftSize, double lowHz, double highHz, int count)
{
    const size_t lo = std::max<size_t> (2, binOf (lowHz, fftSize, sampleRate));
    const size_t hi = std::min (magnitude.size() - 2, binOf (highHz, fftSize, sampleRate));

    std::vector<std::pair<double, size_t>> peaks;

    for (size_t k = lo; k <= hi; ++k)
        if (magnitude[k] > magnitude[k - 1] && magnitude[k] >= magnitude[k + 1])
            peaks.emplace_back (magnitude[k], k);

    std::sort (peaks.begin(), peaks.end(),
               [] (const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<double> result;
    result.reserve (static_cast<size_t> (count));

    for (size_t i = 0; i < peaks.size() && static_cast<int> (i) < count; ++i)
    {
        // Parabolic interpolation, so a peak that drifts by a fraction of a bin
        // is not reported as standing still.
        const size_t k = peaks[i].second;
        const double a = magnitude[k - 1], b = magnitude[k], c = magnitude[k + 1];
        const double denominator = a - 2.0 * b + c;
        const double offset = std::abs (denominator) > 1.0e-30 ? 0.5 * (a - c) / denominator : 0.0;

        result.push_back (frequencyOf (k, fftSize, sampleRate)
                          + offset * sampleRate / static_cast<double> (fftSize));
    }

    return result;
}

namespace
{
    struct PeakTracking
    {
        double persistence = 0.0;
        double movementHz = 0.0;
    };

    PeakTracking trackPeaks (const Spectrogram& s, double lowHz, double highHz,
                             int peaksPerFrame, double toleranceHz)
    {
        PeakTracking result;

        if (s.frames.size() < 2)
            return result;

        size_t considered = 0, survived = 0;
        double movement = 0.0;

        for (size_t f = 1; f < s.frames.size(); ++f)
        {
            const auto previous = peakFrequencies (s.frames[f - 1], s.sampleRate, s.fftSize,
                                                   lowHz, highHz, peaksPerFrame);
            const auto current  = peakFrequencies (s.frames[f], s.sampleRate, s.fftSize,
                                                   lowHz, highHz, peaksPerFrame);

            for (double p : previous)
            {
                ++considered;

                double closest = 1.0e9;

                for (double c : current)
                    closest = std::min (closest, std::abs (c - p));

                if (closest <= toleranceHz)
                {
                    ++survived;
                    movement += closest;
                }
            }
        }

        if (considered > 0)
            result.persistence = static_cast<double> (survived) / static_cast<double> (considered);

        if (survived > 0)
            result.movementHz = movement / static_cast<double> (survived);

        return result;
    }
}

double modalPeakPersistence (const Spectrogram& s, double lowHz, double highHz,
                             int peaksPerFrame, double toleranceHz)
{
    return trackPeaks (s, lowHz, highHz, peaksPerFrame, toleranceHz).persistence;
}

double modalPeakMovementHz (const Spectrogram& s, double lowHz, double highHz,
                            int peaksPerFrame, double toleranceHz)
{
    return trackPeaks (s, lowHz, highHz, peaksPerFrame, toleranceHz).movementHz;
}

AmBands amplitudeModulationBands (const double* signal, size_t length, double sampleRate,
                                  double splitHz, double envelopeSeconds)
{
    AmBands result;

    if (length < 1024 || sampleRate <= 0.0)
        return result;

    // Rectified one-pole envelope, matching amplitudeModulationDepthDb so the
    // two figures describe the same envelope.
    const double coefficient = 1.0 - std::exp (-1.0 / (envelopeSeconds * sampleRate));

    std::vector<double> envelope;
    envelope.reserve (length);

    double state = std::abs (signal[0]);

    for (size_t i = 0; i < length; ++i)
    {
        state += coefficient * (std::abs (signal[i]) - state);
        envelope.push_back (state);
    }

    // The follower's own settling is not modulation.
    const size_t skip = length / 10;

    if (envelope.size() <= skip + 1024)
        return result;

    envelope.erase (envelope.begin(), envelope.begin() + static_cast<ptrdiff_t> (skip));

    const double meanLevel = std::accumulate (envelope.begin(), envelope.end(), 0.0)
                           / static_cast<double> (envelope.size());

    if (meanLevel <= 0.0)
        return result;

    size_t n = 1;
    while (n < envelope.size())
        n <<= 1;

    std::vector<std::complex<double>> spectrum (n, {0.0, 0.0});

    // Relative deviation from the mean, Hann windowed. Removing the mean is what
    // makes this a measure of modulation rather than of level.
    for (size_t i = 0; i < envelope.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                               * static_cast<double> (i)
                                               / static_cast<double> (envelope.size() - 1));

        spectrum[i] = { (envelope[i] / meanLevel - 1.0) * w, 0.0 };
    }

    fft (spectrum);

    const double binHz = sampleRate / static_cast<double> (n);
    const size_t half = n / 2;

    double slow = 0.0, fast = 0.0, strongest = 0.0;

    for (size_t k = 1; k < half; ++k)
    {
        const double hz = static_cast<double> (k) * binHz;

        // Above a few Hz there is no modulation left, only the envelope
        // follower's own residual ripple.
        if (hz > 50.0)
            break;

        const double power = std::norm (spectrum[k]);

        if (hz < splitHz) slow += power;
        else              fast += power;

        if (power > strongest)
        {
            strongest = power;
            result.dominantHz = hz;
        }
    }

    // Hann loses half the amplitude; the factor restores a sinusoidal
    // modulation of relative depth a to a reported depth of a.
    const double scale = 4.0 / static_cast<double> (n);

    const auto toDepthDb = [] (double amplitude)
    {
        const double a = std::min (0.999, std::abs (amplitude));
        return 20.0 * std::log10 ((1.0 + a) / (1.0 - a));
    };

    result.slowDepthDb = toDepthDb (std::sqrt (2.0 * slow) * scale);
    result.fastDepthDb = toDepthDb (std::sqrt (2.0 * fast) * scale);

    return result;
}

PeakLifetime modalPeakLifetime (const Spectrogram& s, double lowHz, double highHz,
                                int peaksPerFrame, double toleranceHz)
{
    PeakLifetime result;

    if (s.frames.size() < 2)
        return result;

    // Peaks per frame, computed once. Recomputing them inside the tracking loop
    // would make this quadratic in the frame count for no benefit.
    std::vector<std::vector<double>> perFrame;
    perFrame.reserve (s.frames.size());

    for (const auto& frame : s.frames)
        perFrame.push_back (peakFrequencies (frame, s.sampleRate, s.fftSize,
                                             lowHz, highHz, peaksPerFrame));

    // A peak is only used as the start of a track if it was not itself the
    // continuation of a peak in the previous frame. Otherwise one long-lived
    // mode would be counted once per frame it passes through, and the mean
    // would be pulled towards 1 by its own tail.
    std::vector<char> continuation (perFrame.size(), 0);

    double totalFrames = 0.0;
    long   tracks = 0;
    double longest = 0.0;

    for (size_t f = 0; f + 1 < perFrame.size(); ++f)
    {
        for (size_t i = 0; i < perFrame[f].size(); ++i)
        {
            if (f > 0)
            {
                bool wasContinuation = false;

                for (double p : perFrame[f - 1])
                    if (std::abs (p - perFrame[f][i]) <= toleranceHz)
                        wasContinuation = true;

                if (wasContinuation)
                    continue;
            }

            double current = perFrame[f][i];
            double frames = 1.0;

            for (size_t g = f + 1; g < perFrame.size(); ++g)
            {
                double closest = 1.0e9, closestFrequency = current;

                for (double c : perFrame[g])
                {
                    const double distance = std::abs (c - current);

                    if (distance < closest)
                    {
                        closest = distance;
                        closestFrequency = c;
                    }
                }

                if (closest > toleranceHz)
                    break;

                // Follow the peak rather than the starting frequency, so a mode
                // that drifts steadily is one long track and not a series of
                // short ones ending at the tolerance boundary.
                current = closestFrequency;
                frames += 1.0;
            }

            totalFrames += frames;
            longest = std::max (longest, frames);
            ++tracks;
        }
    }

    if (tracks > 0)
    {
        result.meanSeconds = (totalFrames / static_cast<double> (tracks)) * s.hopSeconds;
        result.maxSeconds  = longest * s.hopSeconds;
        result.tracked     = static_cast<int> (tracks);
    }

    return result;
}

SidebandEnergy sidebandEnergy (const std::vector<double>& signal, double sampleRate,
                               double toneHz, double rateHz, size_t fftSize,
                               int maxOrder, int binsPerSideband)
{
    SidebandEnergy result;

    if (signal.size() < fftSize || sampleRate <= 0.0 || rateHz <= 0.0 || maxOrder < 1)
        return result;

    const double binHz = sampleRate / static_cast<double> (fftSize);
    result.resolutionHz = binHz;

    // Below two bins of separation the carrier and its first sideband are the
    // same measurement, and reporting a split would be inventing one.
    result.resolved = rateHz >= 2.0 * binHz;

    // Taken from the middle of the signal so neither onset nor any trailing
    // decay is inside the window.
    const size_t start = (signal.size() - fftSize) / 2;
    const auto magnitude = magnitudeSpectrum (signal.data() + start, fftSize, fftSize, true);

    const auto energyAround = [&] (double centreHz)
    {
        const double exact = centreHz * static_cast<double> (fftSize) / sampleRate;
        const long   centre = static_cast<long> (std::llround (exact));

        double total = 0.0;

        for (long k = centre - binsPerSideband; k <= centre + binsPerSideband; ++k)
            if (k >= 0 && static_cast<size_t> (k) < magnitude.size())
                total += magnitude[static_cast<size_t> (k)] * magnitude[static_cast<size_t> (k)];

        return total;
    };

    result.fundamental = energyAround (toneHz);

    for (int order = 1; order <= maxOrder; ++order)
    {
        const double side = energyAround (toneHz - order * rateHz)
                          + energyAround (toneHz + order * rateHz);

        if (order == 1) result.firstOrder  += side;
        else            result.higherOrder += side;
    }

    // The neighbourhood is everything the sidebands could occupy, plus a margin.
    const double span = (maxOrder + 2) * rateHz;
    double neighbourhood = 0.0;

    const long lo = std::max (0L, static_cast<long> (std::llround ((toneHz - span) / binHz)));
    const long hi = std::min (static_cast<long> (magnitude.size()) - 1,
                              static_cast<long> (std::llround ((toneHz + span) / binHz)));

    for (long k = lo; k <= hi; ++k)
        neighbourhood += magnitude[static_cast<size_t> (k)] * magnitude[static_cast<size_t> (k)];

    if (neighbourhood <= 0.0)
        return result;

    result.fundamental /= neighbourhood;
    result.firstOrder  /= neighbourhood;
    result.higherOrder /= neighbourhood;
    result.residual = std::max (0.0, 1.0 - result.fundamental - result.firstOrder - result.higherOrder);

    return result;
}

AmFmSplit amFmSeparation (const std::vector<double>& signal, double sampleRate,
                          double toneHz, double bandHz)
{
    AmFmSplit result;

    if (signal.size() < 64 || sampleRate <= 0.0 || toneHz <= 0.0)
        return result;

    size_t n = 1;
    while (n < signal.size())
        n <<= 1;

    std::vector<std::complex<double>> spectrum (n, {0.0, 0.0});

    for (size_t i = 0; i < signal.size(); ++i)
        spectrum[i] = { signal[i], 0.0 };

    fft (spectrum);

    // Analytic signal, band-limited to a neighbourhood of the tone in one step:
    // keep only the positive frequencies inside the band and double them. What
    // comes back is a complex signal whose magnitude is the instantaneous
    // amplitude and whose argument advances at the instantaneous frequency.
    const double binHz = sampleRate / static_cast<double> (n);
    const long   lo = std::max (1L, static_cast<long> (std::llround ((toneHz - bandHz) / binHz)));
    const long   hi = std::min (static_cast<long> (n / 2),
                                static_cast<long> (std::llround ((toneHz + bandHz) / binHz)));

    for (size_t k = 0; k < n; ++k)
    {
        const long index = static_cast<long> (k);

        if (index >= lo && index <= hi)
            spectrum[k] *= 2.0;
        else
            spectrum[k] = { 0.0, 0.0 };
    }

    ifft (spectrum);

    // Edges are dropped: the band limiting is a circular convolution, so the
    // first and last few thousand samples carry wrap-around from the other end.
    const size_t margin = std::min<size_t> (signal.size() / 8, static_cast<size_t> (0.05 * sampleRate));
    const size_t first  = margin;
    const size_t last   = signal.size() > margin ? signal.size() - margin : signal.size();

    if (last <= first + 8)
        return result;

    double amSum = 0.0, amLow = 1.0e300, amHigh = 0.0;
    size_t amCount = 0;

    for (size_t i = first; i < last; ++i)
    {
        const double a = std::abs (spectrum[i]);

        amSum += a;
        amLow  = std::min (amLow, a);
        amHigh = std::max (amHigh, a);
        ++amCount;
    }

    const double amMean = amSum / static_cast<double> (amCount);

    if (amMean <= 0.0)
        return result;

    result.amDepthDb = 20.0 * std::log10 (std::max (amHigh, 1.0e-300)
                                          / std::max (amLow, 1.0e-300));

    double amVariance = 0.0;

    for (size_t i = first; i < last; ++i)
    {
        const double d = std::abs (spectrum[i]) / amMean - 1.0;
        amVariance += d * d;
    }

    result.amRmsDb = 20.0 * std::log10 (1.0 + std::sqrt (amVariance / static_cast<double> (amCount)));

    // Instantaneous frequency from the phase advance between neighbours. Taking
    // the argument of the ratio wraps correctly by construction, so no separate
    // unwrapping pass is needed.
    std::vector<double> instantaneous;
    instantaneous.reserve (last - first);

    for (size_t i = first + 1; i < last; ++i)
    {
        if (std::abs (spectrum[i - 1]) <= 0.0)
            continue;

        const double advance = std::arg (spectrum[i] / spectrum[i - 1]);
        instantaneous.push_back (advance * sampleRate / (2.0 * 3.14159265358979323846));
    }

    if (instantaneous.size() < 8)
        return result;

    const double centre = std::accumulate (instantaneous.begin(), instantaneous.end(), 0.0)
                        / static_cast<double> (instantaneous.size());

    if (centre <= 0.0)
        return result;

    result.centreHz = centre;

    double peak = 0.0, squared = 0.0;

    for (double f : instantaneous)
    {
        if (f <= 0.0)
            continue;

        const double cents = 1200.0 * std::log2 (f / centre);

        peak = std::max (peak, std::abs (cents));
        squared += cents * cents;
    }

    result.fmPeakCents = peak;
    result.fmRmsCents  = std::sqrt (squared / static_cast<double> (instantaneous.size()));

    return result;
}

double amplitudeModulationDepthDb (const double* signal, size_t length, double sampleRate,
                                   double envelopeSeconds)
{
    if (length < 16 || sampleRate <= 0.0)
        return 0.0;

    const double coefficient = 1.0 - std::exp (-1.0 / (envelopeSeconds * sampleRate));

    double state = std::abs (signal[0]);
    double lowest = 1.0e9, highest = 0.0;

    // The first tenth is skipped so the follower's own settling is not measured.
    const size_t skip = length / 10;

    for (size_t i = 0; i < length; ++i)
    {
        state += coefficient * (std::abs (signal[i]) - state);

        if (i >= skip)
        {
            lowest  = std::min (lowest, state);
            highest = std::max (highest, state);
        }
    }

    if (lowest <= 0.0 || highest <= 0.0)
        return 0.0;

    return 20.0 * std::log10 (highest / lowest);
}

double spectralConcentrationDb (const std::vector<double>& signal, double sampleRate,
                                double toneHz, size_t fftSize,
                                double coreHz, double skirtHz)
{
    const auto magnitude = magnitudeSpectrum (signal, fftSize, true);

    const size_t centre    = binOf (toneHz, fftSize, sampleRate);
    const size_t coreWidth = std::max<size_t> (1, binOf (coreHz, fftSize, sampleRate));
    const size_t skirtWidth = std::max (coreWidth + 1, binOf (skirtHz, fftSize, sampleRate));

    double core = 0.0, skirt = 0.0;

    for (size_t k = 0; k < magnitude.size(); ++k)
    {
        const size_t distance = k > centre ? k - centre : centre - k;
        const double power = magnitude[k] * magnitude[k];

        if (distance <= coreWidth)
            core += power;
        else if (distance <= skirtWidth)
            skirt += power;
    }

    return 10.0 * std::log10 (std::max (core, 1.0e-300) / std::max (skirt, 1.0e-300));
}

PitchExcursion pitchCentroidExcursion (const std::vector<double>& signal, double sampleRate,
                                       double toneHz, size_t frameSize, size_t hop,
                                       double bandHz)
{
    PitchExcursion result;

    const auto s = spectrogramOf (signal, sampleRate, frameSize, hop);

    if (s.frames.size() < 2)
        return result;

    const size_t centre = binOf (toneHz, frameSize, sampleRate);
    const size_t width  = std::max<size_t> (2, binOf (bandHz, frameSize, sampleRate));

    const size_t lo = centre > width ? centre - width : 1;
    const size_t hi = std::min (s.frames[0].size() - 1, centre + width);

    std::vector<double> centroids;
    centroids.reserve (s.frames.size());

    for (const auto& frame : s.frames)
    {
        double weighted = 0.0, total = 0.0;

        for (size_t k = lo; k <= hi; ++k)
        {
            const double power = frame[k] * frame[k];
            weighted += power * frequencyOf (k, frameSize, sampleRate);
            total += power;
        }

        if (total > 0.0)
            centroids.push_back (weighted / total);
    }

    if (centroids.size() < 2)
        return result;

    const double reference = std::accumulate (centroids.begin(), centroids.end(), 0.0)
                           / static_cast<double> (centroids.size());

    if (reference <= 0.0)
        return result;

    double peak = 0.0, sum = 0.0;

    for (double c : centroids)
    {
        const double cents = std::abs (1200.0 * std::log2 (c / reference));
        peak = std::max (peak, cents);
        sum += cents;
    }

    result.peakCents = peak;
    result.meanCents = sum / static_cast<double> (centroids.size());

    return result;
}

double commonRepeatPeriodSeconds (const double* rates, int count,
                                  double horizonSeconds, double toleranceCycles)
{
    if (count <= 0)
        return -1.0;

    double slowest = 1.0e9;

    for (int i = 0; i < count; ++i)
        if (rates[i] > 0.0)
            slowest = std::min (slowest, rates[i]);

    if (slowest >= 1.0e9)
        return -1.0;

    // The search starts after the slowest lane has completed a full cycle. Near
    // t = 0 every lane is a fraction of a cycle from zero, so any tolerance at
    // all reports an immediate "recurrence" — which is what the first version
    // of this did, reporting 0.0 s for every set of rates.
    const double start = 1.0 / slowest;
    const double step = std::min (0.01, start * 0.001);

    for (double t = start; t <= horizonSeconds; t += step)
    {
        bool aligned = true;

        for (int i = 0; i < count && aligned; ++i)
        {
            const double cycles = rates[i] * t;
            const double nearest = std::round (cycles);

            // A lane that has not completed a cycle cannot be said to have
            // returned to its starting phase.
            aligned = nearest >= 1.0 && std::abs (cycles - nearest) <= toleranceCycles;
        }

        if (aligned)
            return t;
    }

    return -1.0;
}

} // namespace avtest
