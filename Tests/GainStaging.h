#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

/**
    Gain staging measurement, and the renders that let a listener check it.

    After the first FL Studio session the report was "no serious problems, but a
    slight overall level reduction". That is one sentence and at least three
    possible causes, each of which would be fixed differently:

      - the decay-aware preview trim, which normalises the tank's build-up to a
        reference setting and is deliberately not unity anywhere else
      - the Size level compensation surface, which corrects the level change
        Size causes and can be up to +/-12 dB
      - the wet/dry mix law, which at the default 30% wet passes 70% of the dry
        signal and 30% of a wet signal that is quieter than the dry to begin with

    Nothing here changes a gain. It measures what the current chain does, splits
    the result into those three components, and renders the files a listener
    needs in order to say whether the measurement matches what they heard. What
    to do about it is a decision for after that, not before it.

    Loudness is ITU-R BS.1770-4: K-weighting, 400 ms blocks at 75% overlap,
    absolute gate at -70 LUFS and a relative gate 10 LU below the ungated mean.
    Peak and RMS are reported alongside it because they answer different
    questions - a reverb tail can lower the peak and raise the loudness at once.
*/
namespace avtest
{

/** One stereo signal and everything measured about its level. */
struct LevelMeasurement
{
    std::string name;

    double peak = 0.0;          /**< Sample peak, linear, the larger channel. */
    double peakDb = 0.0;
    double rms = 0.0;           /**< Over the whole signal, both channels. */
    double rmsDb = 0.0;
    double loudnessLufs = 0.0;  /**< BS.1770-4 gated integrated. */

    size_t frames = 0;
};

/** Stereo frame, matching the offline tank's interface. */
struct StereoFrame { double l = 0.0, r = 0.0; };

/**
    The plugin's shipping defaults, as the parameter layout declares them.

    Duplicated here as plain numbers on purpose: the point of this measurement
    is to say what a user hears at the settings the plugin opens with, and
    reading them back through the layout would need a JUCE processor. The test
    suite checks these against ParameterLayout so the duplication cannot drift.
*/
namespace defaults
{
    inline constexpr double decaySeconds     = 2.5;
    inline constexpr double sizeControl      = 0.60;
    inline constexpr double modDepthControl  = 0.30;
    inline constexpr double modRateControlHz = 0.5;
    inline constexpr double width            = 1.0;
    inline constexpr double mix              = 0.30;
}

/**
    The source the renders are made from.

    Transients and sustained tone in one signal, because the two answer
    different halves of the question: a transient shows what the mix law does to
    the dry signal, and a sustained passage lets the tank reach the steady state
    the trim is defined against.
*/
[[nodiscard]] std::vector<StereoFrame> makeGainStagingSource (double sampleRate);

/**
    Runs the dry signal through the chain the plugin runs today.

    Tank, then the decay-aware preview trim, then width, then the wet/dry
    crossfade - in that order, because the order is what decides where a level
    change comes from. `wetMix` is the only thing that varies between the
    current-settings render and the 100% wet one.
*/
[[nodiscard]] std::vector<StereoFrame> renderPreviewChain (const std::vector<StereoFrame>& dry,
                                                           double sampleRate,
                                                           double wetMix);

/** Where a level difference comes from, in dB, at one setting. */
struct GainStagingBreakdown
{
    double previewTrimDb = 0.0;        /**< 20 log10 of the decay-aware trim. */
    double sizeCompensationDb = 0.0;   /**< What the Size surface applies. */
    double mixLawWetDb = 0.0;          /**< 20 log10 of the wet coefficient. */
    double mixLawDryDb = 0.0;          /**< 20 log10 of the dry coefficient. */

    /** trim + compensation + wet mix: the whole wet path, ignoring the tank. */
    [[nodiscard]] double wetPathTotalDb() const noexcept
    {
        return previewTrimDb + sizeCompensationDb + mixLawWetDb;
    }
};

/** The breakdown at a given Decay, Size and Mix. */
[[nodiscard]] GainStagingBreakdown breakdownFor (double decaySeconds,
                                                 double sizeControl,
                                                 double wetMix);

// ===========================================================================
//  Mix law comparison
// ===========================================================================
/*
    Candidate crossfades, compared offline. Nothing here is wired into the
    plugin: the production path still uses the linear law, and this exists so
    the choice between them is made against measurements rather than by
    reasoning about which one "should" be right.

    The reverb is rendered ONCE per source. Every law and every mix position is
    then a weighted sum of the same dry and wet signals, which is exactly what
    the plugin does at the end of its chain. That makes the comparison exact
    rather than approximate, and it removes any chance that two candidates
    differ because the tank was in a different state.
*/
enum class MixLaw
{
    linear = 0,     /**< dry = 1 - m,          wet = m          - what ships today. */
    equalPower,     /**< dry = cos(m pi/2),    wet = sin(m pi/2). */
    auroraCustom    /**< See gainsFor for what it is and why. */
};

[[nodiscard]] const char* describe (MixLaw law) noexcept;

struct MixGains { double dry = 1.0; double wet = 0.0; };

/** The two coefficients a law applies at a given mix position. */
[[nodiscard]] MixGains gainsFor (MixLaw law, double mix) noexcept;

/**
    Renders the wet path once: tank, preview trim, width. No crossfade.

    Everything up to but not including the mix stage, so one render serves every
    law and every mix position.
*/
[[nodiscard]] std::vector<StereoFrame> renderWetOnly (const std::vector<StereoFrame>& dry,
                                                      double sampleRate);

/** Combines a pre-rendered dry and wet pair under one law at one position. */
[[nodiscard]] std::vector<StereoFrame> mixDryWet (const std::vector<StereoFrame>& dry,
                                                  const std::vector<StereoFrame>& wet,
                                                  MixLaw law, double mix);

/** Peak, true peak, RMS and loudness of one candidate at one position. */
struct MixLawMeasurement
{
    MixLaw law = MixLaw::linear;
    double mix = 0.0;

    double dryGain = 1.0;
    double wetGain = 0.0;

    double peak = 0.0;
    double truePeak = 0.0;
    double rms = 0.0;
    double loudnessLufs = 0.0;

    double loudnessDeltaVsDry = 0.0;
};

[[nodiscard]] MixLawMeasurement measureMixLaw (const std::vector<StereoFrame>& dry,
                                               const std::vector<StereoFrame>& wet,
                                               MixLaw law, double mix,
                                               double dryLoudnessLufs,
                                               double sampleRate);

/**
    The two K-weighting biquads, as designed for a given rate.

    Exposed so the test suite can check them against the coefficients BS.1770-4
    publishes for 48 kHz. The first version of this passed every self-consistent
    check while being 0.25 dB wrong at every rate, because nothing compared it
    to the standard - only to itself.
*/
struct KWeightingCoefficients
{
    std::array<double, 3> shelfB {};
    std::array<double, 3> shelfA {};   /**< a0 normalised to 1. */
    std::array<double, 3> highPassB {};
    std::array<double, 3> highPassA {};
};

[[nodiscard]] KWeightingCoefficients kWeightingCoefficients (double sampleRate);

/** BS.1770-4 gated integrated loudness, in LUFS. */
[[nodiscard]] double integratedLoudnessLufs (const std::vector<StereoFrame>& frames,
                                            double sampleRate);

/** Peak, RMS and loudness of one signal. */
[[nodiscard]] LevelMeasurement measureLevel (const std::string& name,
                                             const std::vector<StereoFrame>& frames,
                                             double sampleRate);

/**
    Writes the four gain-staging renders into `directory`.

      dry-reference.wav        the input, untouched
      preview-current.wav      the chain exactly as it stands today
      preview-matched.wav      the same, scaled to the dry signal's loudness
      preview-wet-100.wav      the same chain with Mix at 100%

    Returns the number of files written, or a negative value on failure. Like
    the rest of the export tooling this never runs as part of a test pass.
*/
int exportGainStagingRenders (const std::string& directory, double sampleRate = 48000.0);

/**
    Renders the Phase 5F candidate listening set into `directory`.

    Four sources, each rendered ONCE through the reverb, then crossfaded at
    Mix 30% by each of the three laws. The dry and wet pair is shared, so the
    only difference between three candidate files is two multiplications - no
    normalisation, no loudness matching, no limiting, and the tank never sees a
    different state.

    Also writes a blind set (candidate-A/B/C per source, order shuffled per
    source), BLIND_KEY.txt mapping them back, and LISTENING_MANIFEST.txt with
    the gains and measurements for every file.

    Returns the number of audio files written, or a negative value on failure.
*/
int exportMixLawCandidates (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
