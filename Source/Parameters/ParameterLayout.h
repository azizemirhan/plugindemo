#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIDs.h"

namespace av::params
{

/** Builds the complete APVTS parameter layout for the plugin. */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/**
    Raw atomic pointers to every parameter, resolved once in the processor
    constructor. The audio thread reads through these — it never performs a
    string lookup and never touches the APVTS ValueTree.
*/
struct RawHandles
{
    void attach (juce::AudioProcessorValueTreeState& state);

    std::atomic<float>* mode           = nullptr;
    std::atomic<float>* color          = nullptr;
    std::atomic<float>* preDelay       = nullptr;
    std::atomic<float>* decay          = nullptr;
    std::atomic<float>* size           = nullptr;
    std::atomic<float>* earlyDiffusion = nullptr;
    std::atomic<float>* lateDiffusion  = nullptr;
    std::atomic<float>* attack         = nullptr;
    std::atomic<float>* modRate        = nullptr;
    std::atomic<float>* modDepth       = nullptr;
    std::atomic<float>* density        = nullptr;
    std::atomic<float>* lowDamp        = nullptr;
    std::atomic<float>* highDamp       = nullptr;
    std::atomic<float>* lowDampFreq    = nullptr;
    std::atomic<float>* highDampFreq   = nullptr;
    std::atomic<float>* width          = nullptr;
    std::atomic<float>* lowCut         = nullptr;
    std::atomic<float>* highCut        = nullptr;
    std::atomic<float>* mix            = nullptr;
};

} // namespace av::params
