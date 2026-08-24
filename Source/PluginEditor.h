#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace av
{

/**
    Development editor.

    PHASE 1-19 use a generic control surface: every parameter is exposed as a
    plain slider so the DSP can be driven and measured in a host without any
    GUI work blocking it. The designed interface arrives in PHASE 20 and
    replaces the body of this class only — the processor never depends on it.
*/
class AuroraVerbEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AuroraVerbEditor (AuroraVerbProcessor&);
    ~AuroraVerbEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuroraVerbProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label phaseLabel;
    juce::GenericAudioProcessorEditor genericControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraVerbEditor)
};

} // namespace av
