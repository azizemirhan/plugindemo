#pragma once

#include <string>
#include <vector>

/**
    Offline analysis of a captured diagnostic session.

    Everything expensive happens here, after the session, on a file — never in
    an audio callback. RT60, FFTs, modal tracking, sideband analysis and true
    peak are all far too heavy for a real-time thread, and a live figure that
    was cheap enough to compute there would not be worth having.

    Every measurement is the one PHASE 3 to PHASE 5C already built. This file
    calls energyDecayCurve, measureDecay, measureBandDecay, modalPeakPersistence,
    maxEnvelopePeriodicity, spectralFlux, thirdOctaveEnergy and the rest rather
    than reimplementing any of them. Two copies of the same formula would drift,
    and only one of them would be the one the DSP phases validated.

    Lives in the test target because that is where the analysis library lives
    and because it is developer tooling: the shipped plugin gains nothing from
    it and should not carry it.
*/
namespace avtest
{

/** Whether a measurement could be made at all, and if not, why not. */
enum class Measurability
{
    measured,
    sourceNotImpulsive,   /**< RT60 from a pad or a loop is not RT60. */
    tooShort,
    silent,
    fileMissing
};

[[nodiscard]] const char* describe (Measurability m) noexcept;

struct DiagnosticAnalysis
{
    bool valid = false;
    std::string error;

    // --- File ---
    double sampleRate = 0.0;
    int    channels = 0;
    size_t frames = 0;
    double durationSeconds = 0.0;

    // --- Levels ---
    double peakLeft = 0.0;
    double peakRight = 0.0;
    double truePeak = 0.0;          /**< 4x oversampled, offline only. */
    double rmsLeft = 0.0;
    double rmsRight = 0.0;
    double dcLeft = 0.0;
    double dcRight = 0.0;
    int64_t clippedSamples = 0;

    // --- Stereo ---
    double correlation = 0.0;
    double monoDownmixDb = 0.0;
    double worstThirdOctaveDb = 0.0;

    // --- Decay ---
    Measurability decayMeasurability = Measurability::measured;
    double rt60Broadband = 0.0;
    double rt60Low = 0.0;
    double rt60Mid = 0.0;
    double rt60High = 0.0;
    double t20 = 0.0;
    double t30 = 0.0;

    /*
        R-squared of the straight-line fit the decay figure came from, one per
        band. The band figures were previously computed and thrown away, which
        meant a band whose fit was poor produced an RT60 that looked exactly as
        trustworthy as one whose fit was perfect. An RT60 without its fit
        quality is a number, not a measurement.

        All four belong to the T30 fit, which is the one the RT60 figures come
        from. There is no separate T20 R-squared: T20 is reported so that its
        disagreement with T30 can be seen, and that disagreement is the signal.
    */
    double decayLinearity = 0.0;
    double decayLinearityLow = 0.0;
    double decayLinearityMid = 0.0;
    double decayLinearityHigh = 0.0;

    double earlyDecayTime = 0.0;

    // --- Movement ---
    Measurability movementMeasurability = Measurability::measured;
    /*
        How much of the peak structure survives from frame to frame, and how far
        the survivors move.

        The two belong together. modalPeakPersistence counts a peak as surviving
        if the next frame has one within +/- 4 Hz, so a mode that is being moved
        by a few Hz still counts as persistent - which is why the Mod Depth A/B
        showed HIGHER persistence with modulation on (0.7556) than off (0.7083).
        The number was right; reading it as "modulation is not breaking up the
        modes" would not have been. modalPeakMovementHz is the other half: it
        says how far the surviving peaks actually travelled.
    */
    double modalPersistence = 0.0;
    double modalMovementHz = 0.0;
    double latePeriodicity = 0.0;
    double peakLifetimeSeconds = 0.0;
    double spectralFlux = 0.0;

    // --- Tail ---
    double silenceTailSeconds = 0.0;
    bool   tailDecaysToSilence = false;
};

/**
    True peak of one channel, 4x oversampled by spectral interpolation.

    Exposed so the gain-staging comparison can measure headroom with the same
    calculation the session analyser uses, rather than a second implementation
    of the same idea that could disagree with it.
*/
[[nodiscard]] double truePeakOfSignal (const std::vector<double>& signal);

/** Reads a 32-bit float WAV written by WavWriter. */
[[nodiscard]] bool readWav (const std::string& path, std::vector<double>& left,
                            std::vector<double>& right, double& sampleRate, std::string& error);

/**
    Analyses a captured output file.

    `impulsive` says whether the source was an impulse or something equally
    suitable for a decay measurement. When it is false the decay fields are left
    at zero and marked sourceNotImpulsive rather than being filled with a number
    that would look like an RT60 and mean nothing — measuring the decay of a
    sustained pad produces a confident, wrong answer, which is worse than no
    answer.
*/
[[nodiscard]] DiagnosticAnalysis analyseCapture (const std::string& path, bool impulsive);

/**
    Analyses one WAV and writes analysis.json / analysis.txt beside it.

    Unlike analyseSessionFolder, which assumes the source was not impulsive,
    this takes the caller's word for it. Needed for a session recorded as a
    single impulse into silence, where the decay figures are the whole point.
*/
int analyseCaptureToFolder (const std::string& wavPath, const std::string& outputFolder,
                            bool impulsive);

/**
    Decides whether a capture looks impulsive enough to measure a decay from.

    Uses the crest factor and where the energy sits: an impulse response has
    almost all of its energy in the first fraction of its length and a very high
    peak-to-RMS ratio, and neither is true of music or of a sustained tone.
*/
[[nodiscard]] bool looksImpulsive (const std::vector<double>& signal, double sampleRate);

/** Human-readable analysis.txt. */
[[nodiscard]] std::string buildAnalysisText (const DiagnosticAnalysis& analysis,
                                             const std::string& sourceName);

/** Machine-readable analysis.json. */
[[nodiscard]] std::string buildAnalysisJson (const DiagnosticAnalysis& analysis,
                                             const std::string& sourceName);

/**
    Runs the analysis over a session folder and writes analysis.json and
    analysis.txt into it.

    Returns the exit code the analysis script reports: 0 pass, 1 pass with
    warnings, 2 fail, 3 missing or invalid session.
*/
[[nodiscard]] int analyseSessionFolder (const std::string& folder);

} // namespace avtest
