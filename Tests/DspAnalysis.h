#pragma once

#include <complex>
#include <cstddef>
#include <vector>

/**
    Measurement utilities for the offline DSP suites.

    These exist so that claims about the reverb can be numbers rather than
    adjectives. Nothing here is used by the plugin; it is analysis only, and it
    is deliberately simple and readable rather than fast.
*/
namespace avtest
{

// ===========================================================================
//  Spectra
// ===========================================================================

/** In-place radix-2 FFT. `data.size()` must be a power of two. */
void fft (std::vector<std::complex<double>>& data);

/** In-place inverse FFT, normalised so ifft(fft(x)) == x. */
void ifft (std::vector<std::complex<double>>& data);

/**
    Zero-phase band limiting by FFT masking.

    Used to split an impulse response into bands before measuring decay, so that
    "does the tail decay at the same rate everywhere" becomes a number. Zero
    phase matters: an IIR bandpass would add its own decay to the measurement.
*/
std::vector<double> bandLimited (const std::vector<double>& signal, double sampleRate,
                                 double lowHz, double highHz);

/**
    Magnitude spectrum of `length` samples, zero-padded to `fftSize`.
    Returns fftSize/2 + 1 bins. Applies a Hann window when `windowed` is true,
    which is what a segment of an impulse response needs; a complete impulse
    response is analysed unwindowed.
*/
std::vector<double> magnitudeSpectrum (const double* signal, size_t length,
                                       size_t fftSize, bool windowed);

std::vector<double> magnitudeSpectrum (const std::vector<double>& signal,
                                       size_t fftSize, bool windowed);

/** Bin index for a frequency, given the FFT size and sample rate. */
size_t binForFrequency (double hz, size_t fftSize, double sampleRate);

/**
    Spectral flatness (Wiener entropy) over [lowHz, highHz]: the geometric mean
    of the power spectrum divided by its arithmetic mean.

    1.0 means a perfectly flat spectrum. A narrow resonance pulls it towards 0,
    which is what makes it a usable metallic-ringing detector.
*/
double spectralFlatness (const std::vector<double>& magnitude, double sampleRate,
                         size_t fftSize, double lowHz, double highHz);

/**
    The tallest narrow peak in the spectrum, in dB above the median bin, over
    [lowHz, highHz]. A diffuse response sits within a few dB of its median; a
    ringing resonance towers over it.
*/
double peakAboveMedianDb (const std::vector<double>& magnitude, double sampleRate,
                          size_t fftSize, double lowHz, double highHz);

/** Largest and smallest deviation from the mean level, in dB, over a band. */
struct BandDeviation
{
    double maxAboveDb = 0.0;
    double maxBelowDb = 0.0;
    double meanDb     = 0.0;
};

BandDeviation bandDeviation (const std::vector<double>& magnitude, double sampleRate,
                             size_t fftSize, double lowHz, double highHz);

/**
    Energy per third-octave band, in dB, from 25 Hz upwards.

    Mono compatibility is judged here rather than on raw bins: two decorrelated
    signals always produce arbitrarily deep nulls in individual bins, but a
    listener hears the band average.
*/
struct ThirdOctaveBands
{
    std::vector<double> centreHz;
    std::vector<double> energyDb;
};

ThirdOctaveBands thirdOctaveEnergy (const std::vector<double>& magnitude, double sampleRate,
                                    size_t fftSize, double lowHz, double highHz);

// ===========================================================================
//  Time-domain measures
// ===========================================================================

double rms (const double* x, size_t n);
double rms (const std::vector<double>& x);

double peakAbsolute (const double* x, size_t n);
double peakAbsolute (const std::vector<double>& x);

double energy (const double* x, size_t n);
double energy (const std::vector<double>& x);

/** Peak divided by RMS. Falls towards 1 as a signal becomes noise-like. */
double crestFactor (const double* x, size_t n);

/** Excess kurtosis. 0 for a Gaussian, large and positive for sparse impulses. */
double excessKurtosis (const double* x, size_t n);

/**
    Normalised echo density (Abel & Huang).

    The fraction of samples in a window whose magnitude exceeds the window's
    standard deviation, divided by the 0.3173 that a Gaussian process would
    give. It approaches 1.0 as the response becomes indistinguishable from
    noise, and sits near 0 while the response is still a handful of discrete
    echoes — which makes it the right way to prove that a Diffusion control
    actually increases density rather than merely sounding like it does.
*/
double normalisedEchoDensity (const double* impulseResponse, size_t length,
                              size_t windowStart, size_t windowLength);

/**
    Strongest periodicity in an impulse response's energy envelope.

    Returns the tallest autocorrelation lag divided by the median lag, over
    [minLag, maxLag], after the envelope has been detrended so the decay itself
    does not dominate. A response whose echoes repeat at a fixed interval — the
    definition of metallic ringing — has one lag towering over the rest and
    scores high; a genuinely diffuse one has no preferred lag and scores near 1.

    This catches what a spectrum cannot. An allpass cascade is magnitude-flat by
    construction, so spectral flatness is always 1.0 no matter how periodic the
    echo pattern is. Periodicity in an allpass network lives in the time
    structure, and that is where it has to be measured.
*/
double maxEnvelopePeriodicity (const double* impulseResponse, size_t length,
                               size_t minLag, size_t maxLag);

/**
    Divides a signal by a smoothed copy of its own envelope.

    Every shape statistic — kurtosis, crest factor, echo density — is computed
    over a window, and over any window of a decaying response the shape is
    dominated by the decay rather than by the structure being measured. A tail
    that falls 15 dB across the window scores as "concentrated" no matter how
    diffuse it is. Flattening the envelope first removes the decay and leaves
    the structure, which is what a comparison between delay sets is about.
*/
std::vector<double> flattenDecay (const double* signal, size_t length,
                                  double envelopeCoefficient = 0.002);

/** Pearson correlation between two equal-length signals. */
double correlation (const double* a, const double* b, size_t n);
double correlation (const std::vector<double>& a, const std::vector<double>& b);

/** Mean of a signal — used to prove the denormal guard leaves no DC behind. */
double mean (const double* x, size_t n);
double mean (const std::vector<double>& x);

/** True if every sample is finite. */
bool allFinite (const double* x, size_t n);
bool allFinite (const std::vector<double>& x);

/** True if any sample is subnormal. */
bool anySubnormal (const double* x, size_t n);

// ===========================================================================
//  Signals
// ===========================================================================

/** A unit impulse followed by silence. */
std::vector<double> impulse (size_t length);

/** Deterministic white noise in [-1, 1]. */
std::vector<double> whiteNoise (size_t length, unsigned int seed);

/** A Hann-windowed sine burst. */
std::vector<double> sineBurst (size_t length, size_t burstLength,
                               double frequencyHz, double sampleRate);

/** A fast-decaying click with a bit of body — a stand-in for a drum transient. */
std::vector<double> transientBurst (size_t length, double sampleRate, unsigned int seed);

/** Impulse response of anything exposing processSample(). */
template <typename Processor>
std::vector<double> impulseResponseOf (Processor& processor, size_t length)
{
    std::vector<double> ir (length, 0.0);

    for (size_t i = 0; i < length; ++i)
        ir[i] = static_cast<double> (processor.processSample (i == 0 ? 1.0 : 0.0));

    return ir;
}

/** Runs a signal through anything exposing processSample(). */
template <typename Processor>
std::vector<double> processThrough (Processor& processor, const std::vector<double>& input)
{
    std::vector<double> out (input.size(), 0.0);

    for (size_t i = 0; i < input.size(); ++i)
        out[i] = static_cast<double> (processor.processSample (input[i]));

    return out;
}

} // namespace avtest
