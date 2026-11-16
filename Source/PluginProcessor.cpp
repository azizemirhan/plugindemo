#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <utility>

namespace av
{

// ===========================================================================
//  SmoothedParameters
// ===========================================================================

namespace
{
    /** Reads a parameter's current value without touching the ValueTree. */
    [[nodiscard]] Real load (const std::atomic<float>* p) noexcept
    {
        return static_cast<Real> (p->load (std::memory_order_relaxed));
    }

    /** Percentage parameter -> normalised amount. */
    [[nodiscard]] Real loadPercent (const std::atomic<float>* p) noexcept
    {
        return load (p) * Real (0.01);
    }
}

void SmoothedParameters::prepare (double sampleRate)
{
    // Long enough to be inaudible on fast automation, short enough that the
    // control still feels immediate.
    constexpr double levelRampSeconds     = 0.02;
    constexpr double ratioTimeConstant    = 0.03;

    for (auto* s : { &size, &earlyDiffusion, &lateDiffusion, &density,
                     &modDepth, &lowDamp, &highDamp, &width, &mix })
        s->prepare (sampleRate, levelRampSeconds);

    // Floors match each parameter's minimum, so the log domain is always valid.
    decaySeconds .prepare (sampleRate, ratioTimeConstant, Real (0.05));
    modRate      .prepare (sampleRate, ratioTimeConstant, Real (0.005));
    lowDampFreq  .prepare (sampleRate, ratioTimeConstant, Real (10.0));
    highDampFreq .prepare (sampleRate, ratioTimeConstant, Real (100.0));
    lowCut       .prepare (sampleRate, ratioTimeConstant, Real (10.0));
    highCut      .prepare (sampleRate, ratioTimeConstant, Real (100.0));
}

void SmoothedParameters::updateTargets (const params::RawHandles& raw)
{
    size          .setTarget (loadPercent (raw.size));
    earlyDiffusion.setTarget (loadPercent (raw.earlyDiffusion));
    lateDiffusion .setTarget (loadPercent (raw.lateDiffusion));
    density       .setTarget (loadPercent (raw.density));
    modDepth      .setTarget (loadPercent (raw.modDepth));
    lowDamp       .setTarget (loadPercent (raw.lowDamp));
    highDamp      .setTarget (loadPercent (raw.highDamp));
    width         .setTarget (loadPercent (raw.width));
    mix           .setTarget (loadPercent (raw.mix));

    decaySeconds  .setTarget (load (raw.decay));
    modRate       .setTarget (load (raw.modRate));
    lowDampFreq   .setTarget (load (raw.lowDampFreq));
    highDampFreq  .setTarget (load (raw.highDampFreq));
    lowCut        .setTarget (load (raw.lowCut));
    highCut       .setTarget (load (raw.highCut));
}

void SmoothedParameters::snapToTargets()
{
    for (auto* s : { &size, &earlyDiffusion, &lateDiffusion, &density,
                     &modDepth, &lowDamp, &highDamp, &width, &mix })
        s->snapToTarget();

    for (auto* s : { &decaySeconds, &modRate, &lowDampFreq, &highDampFreq,
                     &lowCut, &highCut })
        s->snapToTarget();
}

void SmoothedParameters::advanceUnconsumed (int numSamples)
{
    for (auto* s : { &size, &earlyDiffusion, &lateDiffusion, &density,
                     &modDepth, &lowDamp, &highDamp })
        s->skip (numSamples);

    /*
        PHASE 8 note, because this is the easiest place in the project to
        introduce a bug that looks like a feature.

        lowCut and highCut are now CONSUMED by the audio path - but through
        getCurrentValue(), which reads without advancing, exactly as
        lowDampFreq and highDampFreq have always been read. This function is
        how every control-rate Ratio smoother advances, so the two cuts stay
        in this list and are advanced here once per chunk.

        Calling advance(count) in the filter stage AND leaving them here would
        run every one of them at twice the rate: automation would simply feel
        quicker, nothing would fail, and no test that only looked at the audio
        would notice. A test asserts the once-per-sample property directly.

        preDelayMs is gone entirely - dsp::PreDelay owns its own transition.
    */
    for (auto* s : { &decaySeconds, &modRate, &lowDampFreq, &highDampFreq,
                     &lowCut, &highCut })
        s->skip (numSamples);
}

// ===========================================================================
//  AuroraVerbProcessor
// ===========================================================================

juce::AudioProcessor::BusesProperties AuroraVerbProcessor::defaultBuses()
{
    return BusesProperties()
             .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
             .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

AuroraVerbProcessor::AuroraVerbProcessor()
    : juce::AudioProcessor (defaultBuses()),
      apvts (*this, nullptr, "AuroraVerb", params::createLayout())
{
    raw.attach (apvts);
}

void AuroraVerbProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = juce::jmax (1, maximumExpectedSamplesPerBlock);

    // The only allocation point. Extra headroom is reserved because some hosts
    // hand over blocks larger than the size they announced; the render loop
    // splits anything bigger into chunks rather than reallocating.
    wetBuffer.setSize (2, juce::jmax (currentBlockSize, 512), false, true, true);

    // PHASE 10. The ER branch's output. Same length as the wet buffer, so the
    // chunk loop can never produce more ER samples than there is room for.
    erBuffer.setSize (2, wetBuffer.getNumSamples(), false, true, true);

    smoothed.prepare (sampleRate);
    needsSmootherReset = true;

    // PHASE 8. Wet path only, upstream of the tank. prepareToPlay is the
    // only allocation point, and the pre-delay's ring buffer is sized here
    // for its full 500 ms at this rate - at 192 kHz that is 96000 samples,
    // rounded up to a power of two by DelayLine, about 2 MB across the pair.
    cutFilters     .prepare (sampleRate);
    preDelay       .prepare (sampleRate);
    attackInjection.prepare (sampleRate);

    // PHASE 10. Sized for the longest span any pattern may ask for rather
    // than for the current Size, so changing Size never allocates.
    earlyReflections.prepare (sampleRate);
    earlyLateBalance.prepare (sampleRate);

    tank.prepare (sampleRate);
    tank.setRt60Seconds (2.0, true);
    tank.setModulationRateHz (dsp::modulation::defaultRateHz);
    tank.setModulationDepth (dsp::modulation::defaultDepth, true);

    // AV_DEV_ALLPASS_CANDIDATE is a development override for A/B work on the
    // PHASE 5B candidates. It defaults to 1, which is the architecture PHASE 5C
    // selected and the one every validated artefact was built with:
    //
    //   0  matrix modulation only (PHASE 5A behaviour)
    //   1  energy-preserving allpass only  <- shipped
    //   2  Thiran allpass only
    //   3  Schroeder reference allpass only  (research reference, known to pump)
    //
    // Only one engine is driven at a time on purpose. Anything other than 1 is
    // a comparison build and must not be shipped.
    tank.setAllpassRateHz (dsp::allpassMod::defaultRateHz);
    tank.setAllpassSectionsPerLine (dsp::allpassMod::defaultSectionsPerLine);
    tank.setAllpassActiveLines (dsp::allpassMod::defaultActiveLines);

   #if AV_DEV_ALLPASS_CANDIDATE == 1
    tank.setAllpassMode (dsp::TimeVaryingAllpass::Mode::energyPreserving);
   #elif AV_DEV_ALLPASS_CANDIDATE == 2
    tank.setAllpassMode (dsp::TimeVaryingAllpass::Mode::thiran);
   #elif AV_DEV_ALLPASS_CANDIDATE == 3
    tank.setAllpassMode (dsp::TimeVaryingAllpass::Mode::schroederReference);
   #endif

    tank.setAllpassDepth (0.0, true);
    tank.setCompensationEnabled (true, true);

   #if AV_DEV_DIAGNOSTICS
    // Every buffer the diagnostic layer will ever need is reserved here, so
    // nothing on the audio thread can allocate later — including the scratch
    // used to widen a float output block to double.
    diagnostics.prepare (sampleRate, currentBlockSize);

    // Ten milliseconds of playhead slack. Recomputed here so the figure means
    // the same amount of time at every rate.
    transportSeekToleranceSamples = static_cast<int64_t> (0.010 * sampleRate);

    lastTransportPlaying  = false;
    lastTransportSamples  = 0;
    lastTransportBlock    = 0;
    haveTransportPosition = false;

    diagnosticLeft.assign (static_cast<size_t> (currentBlockSize), 0.0);
    diagnosticRight.assign (static_cast<size_t> (currentBlockSize), 0.0);

    diagnostics.setLevel (diag::Level::lightweight);

   #if AV_DEV_AUDIO_CAPTURE
    diagnostics.setCaptureEnabled (true);
    diagnostics.setLevel (diag::Level::full);
   #endif

    updateDiagnosticHostState();

    diag::DiagnosticSessionInfo info;
    info.pluginVersion = JucePlugin_VersionString;
    info.buildConfiguration = "Release, diagnostics";
    info.architectureName = dsp::architecture::describe (dsp::architecture::defaultCandidate);
    info.hostName = juce::PluginHostType().getHostDescription();
    info.pluginFormat = juce::AudioProcessor::getWrapperTypeDescription (wrapperType);
    info.instanceId = juce::Uuid().toDashedString().substring (0, 8).toStdString();
    info.diagnosticsCompiledIn = true;

    // The engine is unconditional now, so this is a constant. It stays a
    // reported field rather than being dropped: a report that simply omits it
    // cannot distinguish "engine present" from "nobody checked", which is the
    // gap that let a development flag decide the shipping signal path.
    info.engineActive = true;

    info.captureCompiledIn = AV_DEV_AUDIO_CAPTURE != 0;

    // Named from the flags rather than from a build-system string, so a
    // mislabelled preset cannot make an artefact claim the wrong profile.
   #if AV_DEV_AUDIO_CAPTURE
    info.buildProfile = "development";
   #else
    info.buildProfile = "validation";
   #endif

    // A session that cannot be started is not a failure to render audio. The
    // plugin carries on and the reason is available for the editor to show.
    (void) diagnostics.startSession (info);

    diagnosticBlockCounter = 0;
    diagnosticRawPeak = Real (0);
    lastReportedDecay = -1.0f;
    lastReportedSize = -1.0f;
    lastReportedModRate = -1.0f;
    lastReportedModDepth = -1.0f;

    diagnostics.getCollector().pushEvent (diag::EventType::prepareSampleRate,
                                          diag::ParameterId::none,
                                          static_cast<float> (sampleRate), 0.0f,
                                          currentBlockSize);
   #endif

    setLatencySamples (0);
}

void AuroraVerbProcessor::releaseResources()
{
   #if AV_DEV_DIAGNOSTICS
    // Stopped before anything is torn down: the writer thread holds the
    // consumer end of the collector's queues, so it must be joined while the
    // collector is still alive.
    diagnostics.stopSession();
   #endif

    wetBuffer.setSize (0, 0);
    currentSampleRate = 0.0;
}

void AuroraVerbProcessor::reset()
{
    wetBuffer.clear();
    erBuffer .clear();
    needsSmootherReset = true;

    // PHASE 8. Without this the pre-delay buffer would replay audio from
    // before the stop, which is the classic stale-buffer bug. The ER layer
    // is a delay line too and has exactly the same exposure.
    cutFilters      .reset();
    preDelay        .reset();
    attackInjection .reset();
    earlyReflections.reset();

    tank.reset();

   #if AV_DEV_DIAGNOSTICS
    diagnostics.getCollector().pushEvent (diag::EventType::reset);
   #endif

    // The shipping reverb engine reset() goes here.
}

bool AuroraVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    // Stereo output only — the engine always renders a decorrelated stereo field.
    if (out != juce::AudioChannelSet::stereo())
        return false;

    // Mono or stereo input.
    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

double AuroraVerbProcessor::getTailLengthSeconds() const
{
    // Named preDelayMs rather than preDelay: since PHASE 8 there is a member
    // called preDelay, and a local that shadows it is a warning today and a
    // misreading later.
    const auto decay      = static_cast<double> (raw.decay    ->load (std::memory_order_relaxed));
    const auto preDelayMs = static_cast<double> (raw.preDelay ->load (std::memory_order_relaxed));

    // PHASE 9: Attack spreads the injection over up to its full range, so the
    // tail starts later by that much. Reported through the stage's own
    // mapping rather than re-deriving it here, so the two cannot drift.
    const auto attack = static_cast<double> (raw.attack->load (std::memory_order_relaxed)) * 0.01;

    const auto attackMs = dsp::AttackInjection::defaultMaxSpreadMs
                        * std::pow (std::clamp (attack, 0.0, 1.0),
                                    dsp::AttackInjection::mappingExponent);

    // PHASE 10: the ER layer is PARALLEL to the late one, so the tail is the
    // longer of the two branches and not their sum. It only exists at all
    // above Early/Late 0, and its span follows Size through the same law the
    // engine uses, rather than a second copy of it here.
    const auto earlyLate = static_cast<double> (raw.earlyLate->load (std::memory_order_relaxed));

    double earlyMs = 0.0;

    if (earlyLate > 0.0)
    {
        const auto size = static_cast<double> (raw.size->load (std::memory_order_relaxed)) * 0.01;

        earlyMs = dsp::er::spanFor (dsp::er::defaultPattern,
                                    dsp::mapping::sizeScaleFor (size)
                                      / dsp::mapping::sizeScaleFor (dsp::er::referenceSizeControl));
    }

    const auto lateSeconds  = decay + attackMs * 0.001;
    const auto earlySeconds = earlyMs * 0.001;

    return preDelayMs * 0.001 + std::max (lateSeconds, earlySeconds);
}

// --- Rendering -------------------------------------------------------------

void AuroraVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void AuroraVerbProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

/*
    Bypass, both directions.

    The host does not tell the plugin it has been bypassed; it stops calling
    processBlock and starts calling processBlockBypassed. Noting the change on
    both sides is the only way a session can say when it happened, and round
    two's test plan asks for exactly that. Both are one comparison and, on the
    edge only, one queue push.
*/
void AuroraVerbProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midi)
{
   #if AV_DEV_DIAGNOSTICS
    noteBypassState (true);
   #endif

    juce::AudioProcessor::processBlockBypassed (buffer, midi);
}

void AuroraVerbProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer,
                                                juce::MidiBuffer& midi)
{
   #if AV_DEV_DIAGNOSTICS
    noteBypassState (true);
   #endif

    juce::AudioProcessor::processBlockBypassed (buffer, midi);
}

template <typename FloatType>
void AuroraVerbProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

   #if AV_DEV_DIAGNOSTICS
    // Reading the clock is the first thing and stopping it is the last, so the
    // measured time is the whole callback rather than a convenient part of it.
    // When the level is off this costs one relaxed atomic load in endBlock.
    const auto diagnosticBlockStart = diagnostics.getCollector().beginBlock();

    // Reaching here at all means the host is not bypassing us.
    noteBypassState (false);
   #endif

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

   #if AV_DEV_DIAGNOSTICS
    // Read here, once per callback, and before anything that pushes an event.
    //
    // The first round-two session recorded transport_playing=false on all 1729
    // of its events and produced no transport events at all, through a whole
    // test pass that included play, stop and seeking. The reason was that the
    // only thing reading the playhead was updateDiagnosticHostState(), which is
    // called from prepareToPlay and nowhere else - so the flag was whatever it
    // had been at prepare, frozen, and the edge detector never ran.
    //
    // getPosition() reads the process context the host already handed the
    // wrapper. No allocation, no lock, no system call.
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            noteTransportState (*position, numSamples);
   #endif

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || wetBuffer.getNumSamples() == 0)
        return;

    smoothed.updateTargets (raw);

    const bool firstBlockAfterPrepare = std::exchange (needsSmootherReset, false);

    if (firstBlockAfterPrepare)
        smoothed.snapToTargets();

    const bool monoInput = (numIn < 2) || (buffer.getNumChannels() < 2);
    const int  maxChunk  = wetBuffer.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += maxChunk)
    {
        const int count = juce::jmin (maxChunk, numSamples - offset);

        // --- 1. Capture the input into the double-precision wet path -------

        {
            const auto* inL = buffer.getReadPointer (0, offset);
            const auto* inR = monoInput ? inL : buffer.getReadPointer (1, offset);

            auto* wetL = wetBuffer.getWritePointer (0);
            auto* wetR = wetBuffer.getWritePointer (1);

            for (int i = 0; i < count; ++i)
            {
                wetL[i] = static_cast<Real> (inL[i]);
                wetR[i] = static_cast<Real> (inR[i]);
            }

           #if AV_DEV_DIAGNOSTICS
            diagnostics.getCollector().observeInput (wetL, wetR, count);
           #endif
        }

        // --- 1b. PHASE 8: Low Cut, High Cut, Pre-Delay --------------------
        //
        //   input -> Low Cut -> High Cut -> Pre-Delay -> tank
        //
        // On the wet buffer only, so the dry signal and its timing are
        // untouched by where this sits rather than by care taken here.
        //
        // These filters are OUTSIDE the feedback loop. They change how much
        // energy enters a band; PHASE 7 damping, inside the loop, changes how
        // long a band takes to decay. Neither is a substitute for the other
        // and a test measures the difference rather than asserting it.
        //
        // PHASE 10 put the Early/Late split AFTER the pre-delay, not before
        // it. An earlier note here predicted the opposite, on the reasoning
        // that early reflections should start early; that reasoning was
        // wrong, and it is left visible rather than quietly deleted. A
        // pre-delay in front of a reverb delays the SOURCE, and a reflection
        // of a delayed source is delayed with it. Splitting ahead of the
        // pre-delay would have made Pre-Delay a hidden early/late timing
        // control - the one thing PHASE 10's brief rules out by name.

        bool earlyLateActive = false;

        {
            auto* wetL = wetBuffer.getWritePointer (0);
            auto* wetR = wetBuffer.getWritePointer (1);

            // Control rate, once per chunk. The corner frequency is read
            // without advancing - advanceUnconsumed() at the end of the chunk
            // is what advances it, exactly as for the damping corners. See the
            // comment there before changing either.
            //
            // Whether a cut is OFF is decided from the SETTLED TARGET and not
            // from the smoothed value: LogSmoother recovers its linear value
            // through exp(log(x)), which need not return x bit-exactly, so an
            // equality test on the smoothed value could miss the endpoint for
            // ever and leave a nominally-neutral filter running.
            cutFilters.setLowCut (smoothed.lowCut.getCurrentValue(),
                                  ! smoothed.lowCut.isSmoothing()
                                    && smoothed.lowCut.getTargetValue() <= dsp::lowCutOffHz);

            cutFilters.setHighCut (smoothed.highCut.getCurrentValue(),
                                   ! smoothed.highCut.isSmoothing()
                                     && smoothed.highCut.getTargetValue() >= dsp::highCutOffHz);

            cutFilters.process (wetL, wetR, count);

            // The pre-delay takes the raw target in milliseconds, not a
            // smoothed one. It crossfades between two fixed integer taps
            // instead of sliding a read position, because the derivative of a
            // delay time is the pitch shift applied to whatever is passing
            // through it.
            const auto preDelayTargetMs =
                static_cast<double> (raw.preDelay->load (std::memory_order_relaxed));

            if (firstBlockAfterPrepare && offset == 0)
                preDelay.snapTo (preDelayTargetMs);
            else
                preDelay.setDelayMilliseconds (preDelayTargetMs);

            preDelay.process (wetL, wetR, count);

            // --- PHASE 10: the Early/Late split ---------------------------
            //
            // Everything above this line conditions BOTH branches; everything
            // below it until the combine belongs to the late one. The ER
            // branch reads the pre-delayed signal here and writes its own
            // buffer, so Attack - which runs next, in place on the wet
            // buffer - cannot reach it.
            //
            // The span follows Size through the engine's own law. Rebuilding
            // the schedule crossfades, and two Size values that round to the
            // same integer span do not restart the fade, so a Size smoother
            // creeping by a fraction of a sample costs nothing.
            {
                const auto sizeRatio =
                    dsp::mapping::sizeScaleFor (smoothed.size.getCurrentValue())
                    / dsp::mapping::sizeScaleFor (dsp::er::referenceSizeControl);

                const auto balance =
                    static_cast<double> (raw.earlyLate->load (std::memory_order_relaxed)) * 0.01;

                if (firstBlockAfterPrepare && offset == 0)
                {
                    earlyReflections.snapToSizeRatio (sizeRatio);
                    earlyLateBalance.snapTo (balance);
                }
                else
                {
                    earlyReflections.setSizeScaleRatio (sizeRatio);
                    earlyLateBalance.setBalance (balance);
                }

                // Asked of the target and the smoothing state, never of the
                // current value: a gain that has arrived at zero still has to
                // be KNOWN to have arrived before the branch can be skipped.
                earlyLateActive = ! earlyLateBalance.isBypassed();

                if (earlyLateActive)
                    earlyReflections.process (wetL, wetR,
                                              erBuffer.getWritePointer (0),
                                              erBuffer.getWritePointer (1), count);
                else
                    // Silent, but still fed. A delay line that stops being
                    // written goes stale, and the first move away from 0
                    // would read whatever was passing through when the
                    // control was last touched.
                    earlyReflections.writeOnly (wetL, wetR, count);
            }

            // --- PHASE 9: Attack ------------------------------------------
            //
            // A fixed FIR: one immediate tap plus a spread of later taps,
            // L2-normalised, so the wet onset stays where it is while the
            // late field is established gradually. Feedforward and outside
            // the recurrence, so it cannot touch Decay, the damping shelves,
            // the feedback gains, the loop lengths or the modulation law.
            //
            // The raw target, not a smoothed one, for the same reason the
            // pre-delay takes a raw target: a smoothed Attack would move tap
            // positions continuously, and a moving delay tap is a pitch
            // shift. AttackInjection crossfades between fixed schedules.
            //
            // Attack 0 is an explicit bypass, bit-exact against PHASE 8 -
            // not a tap set that sums to unity, which it would not: the 32
            // weights sum to -0.73 while their squares sum to 1.
            const auto attackTarget =
                static_cast<double> (raw.attack->load (std::memory_order_relaxed)) * 0.01;

            if (firstBlockAfterPrepare && offset == 0)
                attackInjection.snapTo (attackTarget);
            else
                attackInjection.setAttack (attackTarget);

            attackInjection.process (wetL, wetR, count);
        }

        // --- 2. Reverb engine ---------------------------------------------

        {
            // The signal path, in functional order:
            //
            //   input -> short stereo input diffusion -> 8x8 stereo FDN
            //         -> selected modulation architecture -> stereo readout
            //         -> Size level compensation -> safe wet output
            //
            // The allpass modulation is not literally a stage after the FDN —
            // it lives inside the line-state feedback path, where PHASE 5B put
            // it. The chain above is the functional order, not the class order.
            //
            // A Size request that would not fit is refused by the tank and the
            // previous one stays in effect, which is the right behaviour on the
            // audio thread.
            tank.setRt60Seconds (
                dsp::mapping::rt60For (smoothed.decaySeconds.getCurrentValue()));

            tank.setInputDiffusion (smoothed.earlyDiffusion.getCurrentValue());

            tank.setSize (dsp::mapping::sizeScaleFor (smoothed.size.getCurrentValue()));

            // PHASE 7. The controls are decay ratios by the time the tank sees
            // them, not percentages: Low/High Damp say how long their band
            // decays relative to the mid band, and the mid band stays anchored
            // to Decay. Set after Size and Decay because the shelf gains are
            // derived from the loop delays and RT60 those two produce.
            //
            // Snapped on the first block after prepare: there is no previous
            // damping to move away from, and gliding the corners up from their
            // floor would put a filter sweep at the start of every playback.
            tank.setDamping (dsp::mapping::lowDampRatioFor  (smoothed.lowDamp .getCurrentValue()),
                             dsp::mapping::highDampRatioFor (smoothed.highDamp.getCurrentValue()),
                             smoothed.lowDampFreq .getCurrentValue(),
                             smoothed.highDampFreq.getCurrentValue(),
                             firstBlockAfterPrepare && offset == 0);

            // Mod Depth and Mod Rate drive whichever architecture this preview
            // build selected. AV_DEV_ALLPASS_CANDIDATE 0 keeps the PHASE 5A
            // matrix-only comparison reachable; anything else drives the
            // corresponding PHASE 5B engine. The default build of the preview
            // uses the architecture PHASE 5C selected.
            const double depth = dsp::mapping::modDepthFor (smoothed.modDepth.getCurrentValue());
            const double rateScale = dsp::mapping::modRateScaleFor (smoothed.modRate.getCurrentValue());

           #if AV_DEV_ALLPASS_CANDIDATE == 0
            tank.setAllpassDepth (0.0);
            tank.setModulationDepth (depth);
            tank.setModulationRateHz (dsp::modulation::defaultRateHz * rateScale);
           #elif AV_DEV_ALLPASS_CANDIDATE == 2
            tank.setModulationDepth (0.0);
            tank.setAllpassMode (dsp::TimeVaryingAllpass::Mode::thiran);
            tank.setAllpassRateHz (
                dsp::mapping::allpassRateFor (smoothed.modRate.getCurrentValue()));
            tank.setAllpassDepth (depth);
           #elif AV_DEV_ALLPASS_CANDIDATE == 3
            tank.setModulationDepth (0.0);
            tank.setAllpassMode (dsp::TimeVaryingAllpass::Mode::schroederReference);
            tank.setAllpassRateHz (
                dsp::mapping::allpassRateFor (smoothed.modRate.getCurrentValue()));
            tank.setAllpassDepth (depth);
           #else
            // The shipped architecture, scaled by the user's Depth control.
            dsp::architecture::applyWithDepth (tank, dsp::architecture::defaultCandidate,
                                               depth, rateScale);
           #endif

            auto* wetL = wetBuffer.getWritePointer (0);
            auto* wetR = wetBuffer.getWritePointer (1);

            tank.processBlock (wetL, wetR, count);

            // Decay-aware trim, so that Decay controls decay rather than
            // doubling as a volume control. See mapping::previewWetTrim.
            const auto trim = dsp::mapping::previewWetTrim (
                dsp::mapping::rt60For (smoothed.decaySeconds.getCurrentValue()),
                dsp::mapping::sizeScaleFor (smoothed.size.getCurrentValue()));

           #if AV_DEV_DIAGNOSTICS
            // The internal peak before the trim, so a gain-staging problem
            // cannot be hidden by the gain that comes after it.
            Real rawPeak = 0.0;

            for (int i = 0; i < count; ++i)
                rawPeak = juce::jmax (rawPeak, juce::jmax (std::abs (wetL[i]), std::abs (wetR[i])));

            diagnosticRawPeak = juce::jmax (diagnosticRawPeak, rawPeak);
           #endif

            for (int i = 0; i < count; ++i)
            {
                wetL[i] *= trim;
                wetR[i] *= trim;
            }
        }

        // --- 2b. PHASE 10: combine the two branches ------------------------
        //
        // AFTER the trim, and that placement is a decision rather than an
        // accident of ordering. The trim is a DECAY-AWARE compensation for
        // the TANK's level: it keeps Decay from doubling as a volume control
        // by undoing the level the recurrence builds. The ER layer has no
        // recurrence and nothing to undo, so passing it through the same trim
        // would make the early layer's loudness follow Decay - a late
        // property that the early one does not have, and an Early/Late
        // control whose meaning changed with the Decay knob.
        //
        // Before Width and Mix, so both act on the composed wet signal. An ER
        // layer outside Width would be a stereo image Width could not reach.
        //
        // Skipped entirely at Early/Late 0 - not multiplied by gains that
        // happen to be 1 and 0. PHASE 9 established the distinction and it is
        // the same one here: `x * 1.0 + y * 0.0` is arithmetic the compiler
        // must perform, and performing it on a denormal or an infinity is not
        // the identity. Taking the branch is what makes the legacy path bit
        // exact rather than merely very close.

        if (earlyLateActive)
        {
            auto* wetL = wetBuffer.getWritePointer (0);
            auto* wetR = wetBuffer.getWritePointer (1);

            const auto* erL = erBuffer.getReadPointer (0);
            const auto* erR = erBuffer.getReadPointer (1);

            for (int i = 0; i < count; ++i)
            {
                // Per sample, so a balance move is a ramp rather than a step
                // at the chunk boundary. The ramp is on the GAINS; nothing
                // here can move a reflection in time.
                const Real lateG  = static_cast<Real> (earlyLateBalance.nextLate());
                const Real earlyG = static_cast<Real> (earlyLateBalance.nextEarly());

                wetL[i] = lateG * wetL[i] + earlyG * erL[i];
                wetR[i] = lateG * wetR[i] + earlyG * erR[i];
            }
        }

        // --- 3. Stereo width on the wet signal only ------------------------

        {
            auto* wetL = wetBuffer.getWritePointer (0);
            auto* wetR = wetBuffer.getWritePointer (1);

            for (int i = 0; i < count; ++i)
            {
                const Real widthAmount = smoothed.width.getNextValue();

                const Real mid  = (wetL[i] + wetR[i]) * 0.5;
                const Real side = (wetL[i] - wetR[i]) * 0.5 * widthAmount;

                wetL[i] = mid + side;
                wetR[i] = mid - side;
            }
        }

        // --- 4. Dry / wet crossfade ----------------------------------------

        {
            const auto* wetL = wetBuffer.getReadPointer (0);
            const auto* wetR = wetBuffer.getReadPointer (1);

            auto* outL = buffer.getWritePointer (0, offset);
            auto* outR = (numOut >= 2) ? buffer.getWritePointer (1, offset) : nullptr;

            for (int i = 0; i < count; ++i)
            {
                // The smoother is advanced exactly as before, once per sample,
                // and the law is applied to what it produces. Changing where
                // the smoothing happens would change the automation behaviour
                // that PHASE 5E validated, so it is left alone; only the two
                // coefficients derived from it are new.
                const Real mixAmount = smoothed.mix.getNextValue();

                const auto g = dsp::mapping::mixCoefficientsFor (mixAmount);

                const Real dryAmount = static_cast<Real> (g.dry);
                const Real wetAmount = static_cast<Real> (g.wet);

                const Real dryL = static_cast<Real> (outL[i]);
                const Real dryR = (outR != nullptr && ! monoInput)
                                    ? static_cast<Real> (outR[i]) : dryL;

                outL[i] = static_cast<FloatType> (dryL * dryAmount + wetL[i] * wetAmount);

                if (outR != nullptr)
                    outR[i] = static_cast<FloatType> (dryR * dryAmount + wetR[i] * wetAmount);
            }
        }

        // --- 5. Keep the not-yet-consumed smoothers in step -----------------
        //
        // These are read by the engine from PHASE 3 onwards. Advancing them
        // here keeps every smoother on the same sample clock, so switching a
        // module on later does not introduce a one-block discontinuity. The
        // control-rate smoothers advance exactly once per chunk, which is the
        // grid the engine will run them on.

        smoothed.advanceUnconsumed (count);

       #if AV_DEV_DIAGNOSTICS
        {
            // Observed on the output the host will actually receive, after mix
            // and width, because that is what a listener hears and what a clip
            // would happen to.
            const auto* outL = buffer.getReadPointer (0, offset);
            const auto* outR = (numOut >= 2) ? buffer.getReadPointer (1, offset) : nullptr;

            observeOutputBlock (outL, outR, count);
        }
       #endif
    }

   #if AV_DEV_DIAGNOSTICS
    // Control rate, not per sample: the DSP snapshot is a dozen loads and a
    // struct copy, and doing it per block is already far more often than
    // anything reads it.
    if (++diagnosticBlockCounter >= 8)
    {
        diagnosticBlockCounter = 0;
        updateDiagnosticDspState();
    }

    diagnostics.getCollector().endBlock (diagnosticBlockStart, numSamples);
   #endif
}

#if AV_DEV_DIAGNOSTICS

template <typename FloatType>
void AuroraVerbProcessor::observeOutputBlock (const FloatType* left, const FloatType* right,
                                              int count)
{
    if (diagnostics.getCollector().isOff())
        return;

    // The collector takes doubles. When the host is already running in double
    // precision this is a straight pass; when it is not, the conversion goes
    // into a buffer prepareToPlay reserved, never into a fresh allocation.
    if constexpr (std::is_same_v<FloatType, double>)
    {
        diagnostics.getCollector().observeOutput (left, right, count,
                                                  static_cast<double> (diagnosticRawPeak));
    }
    else
    {
        if (static_cast<int> (diagnosticLeft.size()) < count)
            return;   // Cannot happen after prepareToPlay; refuse rather than grow.

        for (int i = 0; i < count; ++i)
        {
            diagnosticLeft[static_cast<size_t> (i)] = static_cast<double> (left[i]);
            diagnosticRight[static_cast<size_t> (i)] =
                static_cast<double> (right != nullptr ? right[i] : left[i]);
        }

        diagnostics.getCollector().observeOutput (diagnosticLeft.data(), diagnosticRight.data(),
                                                  count, static_cast<double> (diagnosticRawPeak));
    }

    diagnosticRawPeak = Real (0);
}

void AuroraVerbProcessor::updateDiagnosticDspState()
{
    if (diagnostics.getCollector().isOff())
        return;

    diag::DiagnosticDspState state;

    const double decay = dsp::mapping::rt60For (smoothed.decaySeconds.getCurrentValue());
    const double sizeScale = dsp::mapping::sizeScaleFor (smoothed.size.getCurrentValue());
    const double depth = dsp::mapping::modDepthFor (smoothed.modDepth.getCurrentValue());

    state.decayTargetSeconds = decay;
    state.decayInsideCalibration = dsp::sizeComp::isCalibrated (decay)
                                   && ! dsp::sizeComp::correctionIsClamped (sizeScale, decay, depth > 0.0);
    state.sizeTarget = sizeScale;
    state.modDepth = depth;
    state.modRateHz = dsp::mapping::allpassRateFor (smoothed.modRate.getCurrentValue());
    state.architecture = static_cast<int32_t> (dsp::architecture::defaultCandidate);

    // Two reads of a smoother's current value. No allocation, no lock, and the
    // whole function is behind the isOff() check above, so a build with
    // diagnostics compiled out or switched off pays nothing for them.
    state.mix = smoothed.mix.getCurrentValue();
    state.width = smoothed.width.getCurrentValue();

    const auto& bank = tank.getActiveBank();
    const auto& core = bank.getCore();

    state.sizeActive = bank.getSizeScale();
    state.activeBank = tank.getActiveBankIndex();
    state.morphActive = tank.isMorphing();
    state.morphProgress = tank.getMorphPosition();
    state.compensationEnabled = tank.isCompensationEnabled();

    const double gain = tank.getCompensationGain (tank.getActiveBankIndex());
    state.compensationGainDb = gain > 0.0 ? 20.0 * std::log10 (gain) : 0.0;

    state.allpassEngaged = ! core.getAllpassBank().isBypassed();
    state.matrixEngaged = core.getMatrix().getDepth() > 0.0;
    state.inputDiffuserStages = bank.isInputDiffusionEnabled() ? 4 : 0;

    double minGain = 1.0, maxGain = 0.0, addedDelay = 0.0;

    for (int i = 0; i < dsp::fdn::order; ++i)
    {
        const double g = core.getGain (i);
        minGain = juce::jmin (minGain, g);
        maxGain = juce::jmax (maxGain, g);
        addedDelay += core.getEffectiveLoopDelaySamples (i) - core.getDelaySamples (i);
    }

    state.minFeedbackGain = minGain;
    state.maxFeedbackGain = maxGain;
    state.effectiveLoopDelaySamples = addedDelay / dsp::fdn::order;

    state.lastMorphReason = [reason = tank.getLastPrimeReason()]
    {
        using Prime = dsp::MorphingStereoFdn<>::PrimeReason;

        switch (reason)
        {
            case Prime::levelSettled: return diag::MorphReason::levelSettled;
            case Prime::silence:      return diag::MorphReason::silence;
            case Prime::timedOut:     return diag::MorphReason::timedOut;
        }

        return diag::MorphReason::none;
    }();

    auto& collector = diagnostics.getCollector();

    /*
        Morph and bank-swap counters, kept here rather than in the tank.

        DiagnosticDspState has carried morphStartCount, morphTimeoutCount and
        bankSwapCount since the layer was written, and every report reads them -
        but nothing ever wrote them, so summary.txt and session.json reported
        zero morphs through a session that recorded 31. Worse, the morph-timeout
        warning is guarded by `morphStartCount > 0`, so a rule meant to catch a
        real fault could never fire.

        MorphingStereoFdn does not count these, and adding counters to it would
        mean touching DSP that is already validated. The edge is already
        detected here, from state this function already reads, so this is where
        the counting belongs: three integers on the audio thread, incremented on
        an edge, and no change to any DSP behaviour.
    */
    const bool morphStarted = state.morphActive && ! lastReportedMorphActive;
    const bool morphEnded   = ! state.morphActive && lastReportedMorphActive;

    if (morphStarted)
        ++diagnosticMorphStarts;

    // A morph that ended because it ran out of time, rather than because the
    // level settled or the tank went quiet. That is the condition the warning
    // rule exists for.
    if (morphEnded && state.lastMorphReason == diag::MorphReason::timedOut)
        ++diagnosticMorphTimeouts;

    if (state.activeBank != lastReportedBank)
    {
        if (lastReportedBank >= 0)
            ++diagnosticBankSwaps;

        lastReportedBank = state.activeBank;
    }

    state.morphStartCount = diagnosticMorphStarts;
    state.morphTimeoutCount = diagnosticMorphTimeouts;
    state.bankSwapCount = diagnosticBankSwaps;

    collector.observeDsp (state);

    // Parameter events, coalesced: pushed only when a control has actually
    // moved by more than a threshold, never once per automation sample.
    const auto report = [&collector] (float& last, float current, diag::ParameterId id,
                                      float threshold)
    {
        if (last < 0.0f || std::abs (current - last) > threshold)
        {
            collector.pushEvent (diag::EventType::parameterChange, id, current, last);
            last = current;
        }
    };

    report (lastReportedDecay, static_cast<float> (decay), diag::ParameterId::decay, 0.01f);
    report (lastReportedSize, static_cast<float> (sizeScale), diag::ParameterId::size, 0.005f);
    report (lastReportedModRate, static_cast<float> (state.modRateHz),
            diag::ParameterId::modRate, 0.005f);
    report (lastReportedModDepth, static_cast<float> (depth), diag::ParameterId::modDepth, 0.005f);
    report (lastReportedMix, static_cast<float> (state.mix), diag::ParameterId::mix, 0.005f);
    report (lastReportedWidth, static_cast<float> (state.width), diag::ParameterId::width, 0.005f);

    if (state.morphActive != lastReportedMorphActive)
    {
        collector.pushEvent (state.morphActive ? diag::EventType::morphStart
                                               : diag::EventType::morphComplete,
                             diag::ParameterId::size,
                             static_cast<float> (state.sizeActive));
        lastReportedMorphActive = state.morphActive;
    }

    if (! state.decayInsideCalibration)
        collector.pushEvent (diag::EventType::uncalibratedRegion, diag::ParameterId::decay,
                             static_cast<float> (decay));
}

void AuroraVerbProcessor::updateDiagnosticHostState()
{
    diag::DiagnosticHostState host;

    host.sampleRate = currentSampleRate;
    host.prepareBlockSize = currentBlockSize;
    host.inputChannels = getTotalNumInputChannels();
    host.outputChannels = getTotalNumOutputChannels();
    host.doublePrecision = isUsingDoublePrecision();

    // Only what the host actually provides. A value that is not available is
    // reported as unavailable rather than guessed at.
    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            host.transportAvailable = true;
            host.transportPlaying = position->getIsPlaying();
            host.offlineRendering = isNonRealtime();

            if (auto bpm = position->getBpm())
            {
                host.bpmAvailable = true;
                host.bpm = *bpm;
            }

            if (auto signature = position->getTimeSignature())
            {
                host.timeSignatureAvailable = true;
                host.timeSignatureNumerator = signature->numerator;
                host.timeSignatureDenominator = signature->denominator;
            }
        }
    }

    auto& collector = diagnostics.getCollector();

    collector.observeHost (host);

    // Edge-triggered, not level-reported.
    //
    // The first session recorded the transport state in every snapshot and no
    // event anywhere, so "the user pressed play at this point" was not in the
    // session at all - only a column that happened to change value between two
    // rows ten milliseconds apart, which is not the same thing and is not
    // findable. The round-two test plan asks for every transport move and every
    // bypass to be visible, and this is what makes them so.
    if (host.transportAvailable && host.transportPlaying != lastTransportPlaying)
    {
        lastTransportPlaying = host.transportPlaying;

        collector.pushEvent (diag::EventType::transportChanged, diag::ParameterId::none,
                             host.transportPlaying ? 1.0f : 0.0f,
                             host.transportPlaying ? 0.0f : 1.0f);
    }

    // Transport is deliberately NOT read here.
    //
    // This runs once, from prepareToPlay. Anything time-varying read here is
    // frozen for the life of the session, which is exactly how the transport
    // flag came to be false on every event of a session that spent minutes
    // playing. Live transport state belongs to noteTransportState, on the audio
    // thread, once per callback.
    //
    // Bypass is not read here either: this processor declares no bypass
    // parameter, so getBypassParameter() returns null and polling it would be
    // dead code that looks like coverage.
}

diag::SeekReason
AuroraVerbProcessor::classifySeek (const juce::AudioPlayHead::PositionInfo& position,
                                   int64_t drift) const noexcept
{
    /*
        Only claimed when the host gave enough to claim it.

        Without loop points there is no basis at all, so the answer is unknown -
        not "probably a seek". With loop points and looping off, a jump cannot
        be a wrap. With looping on, a wrap goes backwards and lands on the loop
        start; anything else while looping is the user having moved the
        playhead.

        One ambiguity remains and is not resolvable from the API: seeking by
        hand to exactly the loop start, while looping, looks like a wrap. It is
        reported as loopWrap. Saying so here is better than pretending the
        classification is exact.
    */
    const auto loop = position.getLoopPoints();

    if (! loop.hasValue())
        return diag::SeekReason::unknown;

    if (! position.getIsLooping())
        return diag::SeekReason::userSeek;

    const auto ppq = position.getPpqPosition();

    if (! ppq.hasValue())
        return diag::SeekReason::unknown;

    // A twentieth of a beat: wide enough for the block the wrap landed in,
    // far narrower than any deliberate move.
    constexpr double toleranceBeats = 0.05;

    if (drift < 0 && std::abs (*ppq - loop->ppqStart) <= toleranceBeats)
        return diag::SeekReason::loopWrap;

    return diag::SeekReason::userSeek;
}

void AuroraVerbProcessor::noteTransportState (const juce::AudioPlayHead::PositionInfo& position,
                                              int numSamples) noexcept
{
    auto& collector = diagnostics.getCollector();

    const bool playing = position.getIsPlaying();

    // The flag every other event carries. Written every callback, so a deadline
    // overrun's "transport_playing" describes the moment it happened.
    collector.observeTransport (playing, isNonRealtime());

    if (playing != lastTransportPlaying)
    {
        lastTransportPlaying = playing;

        collector.pushEvent (diag::EventType::transportChanged, diag::ParameterId::none,
                             playing ? 1.0f : 0.0f,
                             playing ? 0.0f : 1.0f);

        // Starting and stopping both move the playhead in ways that are not
        // seeks, so the continuity tracking restarts rather than reporting one.
        haveTransportPosition = false;
    }

    const auto samples = position.getTimeInSamples();

    if (! playing || ! samples.hasValue())
    {
        // A stopped playhead does not advance, so every callback would look
        // like a backward jump. Seeks are only meaningful while playing.
        haveTransportPosition = false;
        return;
    }

    const int64_t now = static_cast<int64_t> (*samples);

    if (haveTransportPosition)
    {
        // While playing, the playhead advances by exactly the block that was
        // just delivered. Anything else is the user having moved it - a click
        // in the timeline, a loop wrap, a jump to a marker.
        const int64_t expected = lastTransportSamples + lastTransportBlock;
        const int64_t drift    = now - expected;

        if (std::abs (drift) > transportSeekToleranceSamples)
        {
            const double rate = currentSampleRate > 0.0 ? currentSampleRate : 48000.0;

            collector.pushEventWithFlags (
                diag::EventType::transportSeek, diag::ParameterId::none,
                static_cast<float> (static_cast<double> (now) / rate),
                static_cast<float> (static_cast<double> (expected) / rate),
                drift > 0 ? 1 : -1,
                diag::eventFlags::packSeekReason (classifySeek (position, drift)));
        }
    }

    lastTransportSamples  = now;
    lastTransportBlock    = numSamples;
    haveTransportPosition = true;
}

void AuroraVerbProcessor::noteBypassState (bool bypassed) noexcept
{
    if (bypassed == lastBypassed)
        return;

    lastBypassed = bypassed;

    diagnostics.getCollector().pushEvent (diag::EventType::bypassChanged,
                                          diag::ParameterId::none,
                                          bypassed ? 1.0f : 0.0f,
                                          bypassed ? 0.0f : 1.0f);
}

#endif // AV_DEV_DIAGNOSTICS

// --- State -----------------------------------------------------------------

void AuroraVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

/*
    PHASE 10. Parameters the loaded state does not mention are reset to their
    defaults, and that is not a tidiness measure - without it the plugin loads
    older states wrongly.

    AudioProcessorValueTreeState::replaceState assigns the tree and then
    reconnects only the parameters that HAVE a child in it:
    updateParameterConnectionsToChildTrees walks the new tree's children, and
    setNewState is called once per child found. A parameter with no child is
    never visited, so it keeps whatever value it currently holds.

    On a freshly constructed instance that value is the default, so loading an
    older session appears to work. But load an older PRESET into a live
    instance where the user has already moved a newer control, and that
    control keeps its moved value - so the preset does not reproduce the sound
    it was saved from. The older the state, the more controls are affected.

    This matters from PHASE 10 onwards specifically. PHASES 8 and 9 added no
    parameters: they consumed IDs that already existed, so every saved state
    mentioned every parameter and nothing was ever missing. PHASE 10 is the
    first phase to append one.

    Resetting the absent ones is the whole fix, and it is deliberately applied
    to ALL parameters rather than to a known list of recent additions - a list
    would have to be maintained, and forgetting to extend it would reintroduce
    exactly this defect silently.
*/
void AuroraVerbProcessor::resetParametersAbsentFrom (const juce::ValueTree& tree)
{
    for (auto* parameter : getParameters())
    {
        auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

        if (hosted == nullptr)
            continue;

        const auto id = hosted->getParameterID();

        bool present = false;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto child = tree.getChild (i);

            if (child.getProperty ("id").toString() == id)
            {
                present = true;
                break;
            }
        }

        if (! present)
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
    }
}

void AuroraVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            apvts.replaceState (tree);

            // After, not before: replaceState is what reconnects the
            // parameters the tree does mention, and this must not be undone
            // by it.
            resetParametersAbsentFrom (tree);
        }
    }

    needsSmootherReset = true;

   #if AV_DEV_DIAGNOSTICS
    diagnostics.getCollector().pushEvent (diag::EventType::stateRestore);
   #endif
}

juce::AudioProcessorEditor* AuroraVerbProcessor::createEditor()
{
    return new AuroraVerbEditor (*this);
}

} // namespace av

// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new av::AuroraVerbProcessor();
}
