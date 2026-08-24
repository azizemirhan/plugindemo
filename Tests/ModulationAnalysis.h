#pragma once

#include <cstddef>
#include <vector>

/**
    Measurement for modulated reverb tails.

    Matrix modulation does not move any delay length, so it does not produce the
    pitch shift a modulated delay does. What it moves is how energy is shared
    between the delay lines, which moves where the network's modes sit. That
    calls for different instruments: how long a spectral peak persists, how far
    it drifts, how much the spectrum changes from frame to frame, and — because
    time-varying mixing coefficients are an amplitude modulation whether or not
    anyone intended one — how much the output level wobbles.
*/
namespace avtest
{

/** Magnitude spectra of successive overlapping frames. */
struct Spectrogram
{
    std::vector<std::vector<double>> frames;  /**< [frame][bin] magnitudes. */
    size_t fftSize = 0;
    double sampleRate = 0.0;
    double hopSeconds = 0.0;
};

Spectrogram spectrogramOf (const std::vector<double>& signal, double sampleRate,
                           size_t fftSize, size_t hop);

/**
    Mean positive spectral change per frame, normalised by frame energy.

    A static tail settles into a fixed set of modes and its flux falls towards
    zero; a moving one keeps changing. Higher is more movement.
*/
double spectralFlux (const Spectrogram& s, double lowHz, double highHz);

/** Where the strongest peaks sit in one frame, in Hz. */
std::vector<double> peakFrequencies (const std::vector<double>& magnitude, double sampleRate,
                                     size_t fftSize, double lowHz, double highHz, int count);

/**
    How much of the peak structure survives from one frame to the next.

    For every peak in a frame, a peak within `toleranceHz` in the following
    frame counts as the same one. Returns the fraction that survive: 1.0 means
    the modes are standing still, which is exactly the static ringing this
    modulation exists to break up.
*/
double modalPeakPersistence (const Spectrogram& s, double lowHz, double highHz,
                             int peaksPerFrame, double toleranceHz);

/** Mean distance a surviving peak moves between frames, in Hz. */
double modalPeakMovementHz (const Spectrogram& s, double lowHz, double highHz,
                            int peaksPerFrame, double toleranceHz);

/**
    Peak-to-trough variation of a signal's envelope, in dB.

    Measured on a smoothed rectified envelope, over a span well clear of the
    onset. Time-varying mixing coefficients show up here even though they never
    touch a delay length.
*/
double amplitudeModulationDepthDb (const double* signal, size_t length, double sampleRate,
                                   double envelopeSeconds = 0.03);

/**
    Amplitude modulation split by how fast it is.

    amplitudeModulationDepthDb reports one peak-to-trough figure for the whole
    tail, which treats a slow swell and a fast wobble as the same defect. They
    are not: a level that drifts over several seconds is a breathing tail, while
    the same depth at a few Hertz is a tremolo. Ranking two architectures on the
    combined figure penalises whichever one happens to modulate more slowly, for
    a reason that has nothing to do with how it sounds.

    The rectified envelope is detrended, windowed and transformed, and its energy
    is split at `splitHz`. Both halves are reported as an equivalent modulation
    depth in dB so they can be compared with the combined figure directly.
*/
struct AmBands
{
    double slowDepthDb = 0.0;   /**< Modulation below splitHz. */
    double fastDepthDb = 0.0;   /**< Modulation above splitHz. */
    double dominantHz  = 0.0;   /**< Where the envelope's strongest component sits. */
};

AmBands amplitudeModulationBands (const double* signal, size_t length, double sampleRate,
                                  double splitHz = 1.0, double envelopeSeconds = 0.03);

/**
    Ratio between the energy close to a tone and the energy in the skirt around
    it, in dB.

    Higher means the output is more concentrated on the input's frequency.
    Modulation broadens the skirt; how much it broadens it is the number that
    separates "movement" from "chorus".
*/
double spectralConcentrationDb (const std::vector<double>& signal, double sampleRate,
                                double toneHz, size_t fftSize,
                                double coreHz, double skirtHz);

/**
    Apparent pitch movement of a tone passing through the tank.

    The spectral centroid inside a narrow band around the tone is tracked frame
    by frame and its excursion reported in cents. Robust where zero-crossing
    counting is not: the output is a sum of many delayed copies, so its zero
    crossings are noisy even when its pitch is not moving.
*/
struct PitchExcursion
{
    double peakCents = 0.0;
    double meanCents = 0.0;
};

PitchExcursion pitchCentroidExcursion (const std::vector<double>& signal, double sampleRate,
                                       double toneHz, size_t frameSize, size_t hop,
                                       double bandHz);

/**
    How long a spectral peak survives before it drifts away or dies.

    modalPeakPersistence answers "what fraction survive one frame"; this answers
    "how long do they last". A peak found in one frame is followed forward while
    a peak stays within `toleranceHz` of it, and the run length is recorded. Two
    tails can share a persistence figure and still differ here: one where every
    mode survives three frames and one where a third of them ring forever.

    Reported in seconds. Lower is more movement.
*/
struct PeakLifetime
{
    double meanSeconds = 0.0;
    double maxSeconds  = 0.0;
    int    tracked     = 0;
};

PeakLifetime modalPeakLifetime (const Spectrogram& s, double lowHz, double highHz,
                                int peaksPerFrame, double toleranceHz);

/**
    Energy around a modulated tone, split into carrier and sideband orders.

    A tone through a phase-modulated network comes out as a carrier plus
    sidebands spaced by the modulation rate. How the energy divides between them
    is what separates "movement" from "chorus": a little first-order sideband is
    motion, a lot of higher-order sideband is a warble.

    Because the modulation rates here are a fraction of a Hertz, resolving
    +/- rateHz needs a very long window — the caller is expected to pass a signal
    of at least a few tens of seconds and an `fftSize` fine enough that one
    sideband spacing spans several bins. `binsPerSideband` sets how many bins
    around each theoretical sideband position are counted.

    Fractions are of the total energy in the analysed neighbourhood, so
    fundamental + firstOrder + higherOrder + residual = 1.
*/
struct SidebandEnergy
{
    double fundamental = 0.0;   /**< Fraction at the carrier. */
    double firstOrder  = 0.0;   /**< Fraction at carrier +/- rate. */
    double higherOrder = 0.0;   /**< Fraction at carrier +/- k*rate, k >= 2. */
    double residual    = 0.0;   /**< Everything else in the neighbourhood. */
    double resolutionHz = 0.0;  /**< Bin spacing actually achieved. */
    bool   resolved    = false; /**< False if a sideband is under two bins away. */
};

SidebandEnergy sidebandEnergy (const std::vector<double>& signal, double sampleRate,
                               double toneHz, double rateHz, size_t fftSize,
                               int maxOrder, int binsPerSideband);

/**
    Splits the modulation a narrowband signal carries into amplitude and phase.

    The analytic signal is formed by FFT (negative frequencies zeroed, positive
    ones doubled) after band-limiting to a neighbourhood of the tone. Its
    magnitude is the instantaneous amplitude and the derivative of its unwrapped
    argument is the instantaneous frequency, so the two kinds of modulation can
    be reported separately instead of being conflated into one "wobble" figure.

    Reported over the steady portion only; the caller passes an already-trimmed
    signal.
*/
struct AmFmSplit
{
    double amDepthDb     = 0.0; /**< Peak-to-trough of instantaneous amplitude. */
    double amRmsDb       = 0.0; /**< RMS deviation of instantaneous amplitude. */
    double fmPeakCents   = 0.0; /**< Peak instantaneous-frequency excursion. */
    double fmRmsCents    = 0.0; /**< RMS instantaneous-frequency excursion. */
    double centreHz      = 0.0; /**< Mean instantaneous frequency. */
};

AmFmSplit amFmSeparation (const std::vector<double>& signal, double sampleRate,
                          double toneHz, double bandHz);

/**
    Time until a set of periodic lanes returns close to its starting phase.

    Reported in seconds, or a negative value if it does not recur inside the
    horizon searched. A short common period would make the modulation audibly
    cyclic no matter how irregular each lane is on its own.
*/
double commonRepeatPeriodSeconds (const double* rates, int count,
                                  double horizonSeconds, double toleranceCycles);

} // namespace avtest
