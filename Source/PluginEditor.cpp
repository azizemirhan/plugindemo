#include "PluginEditor.h"

namespace av
{

AuroraVerbEditor::AuroraVerbEditor (AuroraVerbProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      genericControls (p)
{
    titleLabel.setText (JucePlugin_Name, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    phaseLabel.setText ("v" JucePlugin_VersionString "  -  PHASE 1: skeleton (wet path is a passthrough placeholder)",
                        juce::dontSendNotification);
    phaseLabel.setFont (juce::FontOptions (13.0f));
    phaseLabel.setJustificationType (juce::Justification::centredLeft);
    phaseLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible (phaseLabel);

    addAndMakeVisible (genericControls);

    juce::ignoreUnused (processorRef); // used from PHASE 20 onwards

    setResizable (true, true);
    setResizeLimits (480, 400, 1200, 1400);
    setSize (620, 760);
}

void AuroraVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void AuroraVerbEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds (area.removeFromTop (30));
    phaseLabel.setBounds (area.removeFromTop (20));

    area.removeFromTop (8);
    genericControls.setBounds (area);
}

} // namespace av
