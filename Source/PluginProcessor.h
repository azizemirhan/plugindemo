#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

#include "DSP/DspConfig.h"
#include "DSP/ScratchArena.h"
#include "DSP/SmoothedParameter.h"
#include "Parameters/ParameterLayout.h"

// The wet/dry crossfade law lives here, and the crossfade is part of every
// build - not just the preview one. It used to be included only under
// AV_DEV_DIFFUSION_PREVIEW, which was fine while the mix stage was two lines
// of arithmetic and stopped being fine the moment it called a mapping
// function: the default configuration failed to compile and the preview one
// did not, so only building all three caught it.
#include "DSP/ParameterMapping.h"

#include "DSP/AttackInjection.h"
#include "DSP/CutFilter.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/PreDelay.h"

#if AV_DEV_DIAGNOSTICS
 #include "Diagnostics/DiagnosticSession.h"
#endif

namespace av
{

/**
    Every continuously-varying parameter, each smoothed with the law that suits
    its kind. See DSP/SmoothedParameter.h for why one smoother does not fit all.

      Level  linear ramp, audio rate    — normalised 0..1 amounts and gains
      Glide  one-pole, audio rate       — times in milliseconds
      Ratio  log-domain, control rate   — frequencies and durations

    PHASE 8 removed the `Glide preDelayMs` member rather than leaving it
    unused. A one-pole on a delay TIME produces Doppler — the derivative of a
    delay time is the pitch shift applied to the signal passing through it —
    so dsp::PreDelay owns its own transition, a crossfade between two fixed
    taps, and takes the raw target in milliseconds once per chunk. Leaving the
    smoother in place would have left a loaded gun: the obvious thing to do
    with a `preDelayMs` smoother is to read it per sample, which is exactly
    the thing that must not happen. No `Glide` is currently used; the class
    stays in SmoothedParameter.h because it is the right tool for a modulation
    depth or a feedback coefficient, neither of which is smoothed here yet.

    Percentage parameters are normalised to 0..1 here (Width reaches 2.0) so the
    render loop never carries a stray 0.01.

    Discrete parameters (Mode, Color) are deliberately not smoothed — they select
    a topology, and are handled by the engine's crossfade logic instead
    (PHASE 9 onwards).
*/
struct SmoothedParameters
{
    using Level = dsp::LinearSmoother<Real>;
    using Glide = dsp::OnePoleSmoother<Real>;
    using Ratio = dsp::LogSmoother<Real>;

    void prepare (double sampleRate);
    void updateTargets (const params::RawHandles& raw);
    void snapToTargets();

    /** Advances the smoothers the audio path has not consumed this block, so
        every smoother stays on the same sample clock. Width and Mix are
        excluded: they are read per sample by the render loop. */
    void advanceUnconsumed (int numSamples);

    /*
        PHASE 9: `attack` is gone from here, for the same reason `preDelayMs`
        went in PHASE 8. A smoothed Attack time read per sample would mean
        continuously moving tap positions, and the derivative of a delay time
        is a pitch shift applied to whatever is passing through it.
        dsp::AttackInjection owns its own transition: it crossfades between
        two fixed tap schedules and coalesces a pending target.

        `lateDiffusion` and `density` stay, unconsumed. PHASE 9 rejected both
        as controls - see Docs/Phase9-Stage1-Proposal.md and -Stage2 - but
        their parameter IDs are part of the serialised state contract and
        removing them would change what existing sessions restore.
    */
    Level size, earlyDiffusion, lateDiffusion, density;
    Level modDepth, lowDamp, highDamp;
    Level width, mix;

    Ratio decaySeconds, modRate, lowDampFreq, highDampFreq, lowCut, highCut;
};

/**
    AuroraVerb — professional algorithmic reverb.

    PHASE 1 scope: plugin skeleton only. The wet path is an identity
    placeholder; the reverb engine is built from PHASE 2 onwards. Everything
    around it — parameter layout, smoothing, denormal protection, bus handling,
    state persistence, sample-rate handling — is production shaped from the
    start, because retrofitting those is what makes reverb projects fail.
*/
class AuroraVerbProcessor final : public juce::AudioProcessor
{
public:
    AuroraVerbProcessor();
    ~AuroraVerbProcessor() override = default;

    // --- AudioProcessor -------------------------------------------------

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    /** Overridden only so that being bypassed is recorded, then handed on. */
    void processBlockBypassed (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override      { return 1; }
    int getCurrentProgram() override   { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /**
        Resets every parameter the loaded state does not mention.

        PHASE 10. APVTS reconnects only the parameters that have a child in the
        loaded tree, so one that is absent keeps its CURRENT value rather than
        returning to its default - which makes an older preset loaded into a
        live instance sound wrong. See the implementation comment.

        Public so that the state-compatibility suite can drive it directly
        against a serialised fixture rather than inferring it from audio.
    */
    void resetParametersAbsentFrom (const juce::ValueTree& tree);

    // --- Project API ----------------------------------------------------

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

private:
    /** Mono-or-stereo in, stereo out. Must be a member: BusesProperties is a
        protected nested type of AudioProcessor. */
    static BusesProperties defaultBuses();

    /** The single templated implementation behind both processBlock overloads. */
    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    juce::AudioProcessorValueTreeState apvts;
    params::RawHandles raw;
    SmoothedParameters smoothed;

    double currentSampleRate = 0.0;
    int    currentBlockSize  = 0;

    /** Scratch buffer for the wet path. Allocated in prepareToPlay only. */
    juce::AudioBuffer<Real> wetBuffer;

    /** The reverb engine: input diffusion into the 8x8 stereo FDN, the PHASE 5C
        modulation architecture, and Size level compensation.

        Part of every build. It used to be compiled in only under
        AV_DEV_DIFFUSION_PREVIEW, which meant the default configuration produced
        a plugin whose wet path was a copy of its input — silently, because
        nothing failed to build. Every artefact that was ever validated in a
        host had that flag ON; no default build was ever listened to. The flag
        no longer decides whether the engine exists.

        The output trim it carries is decay-aware: a tank driven by sustained
        full-scale noise legitimately builds well past unity — the core tests
        measure a peak near 40 into a 30 s decay — so the trim keeps a
        full-scale input from clipping. See dsp::mapping::previewWetTrim, whose
        name predates this change and whose numeric behaviour is unchanged. */
    dsp::MorphingStereoFdn<> tank;

    /** PHASE 8, wet path only, upstream of the tank.

        input -> Low Cut -> High Cut -> Pre-Delay -> tank

        Both live on the wet buffer between the capture step and the engine,
        so the dry signal is untouched structurally rather than by care, and
        "no change to dry timing" is a property of where they sit rather than
        a thing to remember.

        At the neutral settings — Pre-Delay 0 ms, Low Cut 20 Hz, High Cut
        20 kHz — both stages are skipped on a flag and the PHASE 7 path is
        reproduced bit for bit. */
    dsp::StereoCutFilters cutFilters;
    dsp::PreDelay<Real>   preDelay;
    dsp::AttackInjection  attackInjection;

   #if AV_DEV_DIAGNOSTICS
    /** Telemetry. Compiled out entirely unless -DAV_DEV_DIAGNOSTICS=ON: in the
        default build this member does not exist, so there is no thread, no
        queue, no ring buffer and no branch anywhere in the audio path. */
    diag::DiagnosticController diagnostics;

    /** Fills in what the host will tell us. Message thread, per block. */
    void updateDiagnosticHostState();

    /** Fills in the DSP snapshot. Control rate, from the audio thread. */
    void updateDiagnosticDspState();

    /** Templated so both float and double process paths can feed it. */
    template <typename FloatType>
    void observeOutputBlock (const FloatType* left, const FloatType* right, int count);

    /** Pushes a bypass event on the edge only. Audio thread; never allocates. */
    void noteBypassState (bool bypassed) noexcept;

    /**
        Records the host's transport, once per callback.

        Audio thread. Reads the position the wrapper already holds, writes two
        booleans, and pushes an event only on a play/stop edge or a jump. No
        allocation, no lock, no system call.
    */
    void noteTransportState (const juce::AudioPlayHead::PositionInfo& position,
                             int numSamples) noexcept;

    /** Tells a loop wrap from a user seek, or says it cannot. Audio thread. */
    [[nodiscard]] diag::SeekReason
    classifySeek (const juce::AudioPlayHead::PositionInfo& position,
                  int64_t drift) const noexcept;

    /** Highest internal peak seen before the preview trim, this block. */
    Real diagnosticRawPeak = Real (0);

    /** Scratch for converting a float output block to double. Preallocated. */
    std::vector<double> diagnosticLeft, diagnosticRight;

    /** Last values seen, so a parameter event is only pushed on a real change. */
    float lastReportedDecay = -1.0f;
    float lastReportedSize = -1.0f;
    float lastReportedModRate = -1.0f;
    float lastReportedModDepth = -1.0f;
    float lastReportedMix = -1.0f;
    float lastReportedWidth = -1.0f;

    int64_t lastReportedBankSwaps = 0;
    bool    lastReportedMorphActive = false;

    /** Morph and bank-swap totals. Written on the audio thread, on edges only. */
    int64_t diagnosticMorphStarts = 0;
    int64_t diagnosticMorphTimeouts = 0;
    int64_t diagnosticBankSwaps = 0;

    /** -1 until the first snapshot, so the opening bank is not a swap. */
    int32_t lastReportedBank = -1;

    /** Transport and bypass, so each one produces an event on the edge only. */
    bool lastTransportPlaying = false;
    bool lastBypassed = false;

    /** Playhead continuity, for telling a seek from ordinary advance. */
    int64_t lastTransportSamples = 0;
    int     lastTransportBlock = 0;
    bool    haveTransportPosition = false;

    /**
        How far the playhead may miss its expected position before it counts as
        a seek. Ten milliseconds: far above the jitter of a host that delivers
        blocks of one to a few hundred samples, far below any musical jump.
    */
    int64_t transportSeekToleranceSamples = 480;

    /** Control-rate divider, so telemetry is not gathered per sample. */
    int diagnosticBlockCounter = 0;
   #endif

    /** Set by prepareToPlay, cleared once the first block has been rendered, so
        the first block snaps to its parameter targets instead of ramping. */
    bool needsSmootherReset = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraVerbProcessor)
};

} // namespace av
