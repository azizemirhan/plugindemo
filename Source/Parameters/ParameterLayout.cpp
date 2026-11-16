#include "ParameterLayout.h"

#include <string>

namespace av::params
{

namespace
{
    using APF      = juce::AudioParameterFloat;
    using APC      = juce::AudioParameterChoice;
    using FloatAtt = juce::AudioParameterFloatAttributes;

    /*
        The nineteen shipped parameters. Their hint is frozen at generation 1
        and must stay there: changing it reorders the AU parameter list for
        hosts that have cached it. See the note in ParameterIDs.h.
    */
    juce::ParameterID pid (const char* name) { return { name, versionHintV1 }; }

    /**
        A parameter appended after the first release.

        Separate from pid() on purpose. A single shared helper would give an
        appended parameter the same hint as the originals, which places it
        among them in the AU identifier sort rather than after them - and that
        is the one thing the hint exists to control.

        First used by PHASE 10's earlyLate.
    */
    juce::ParameterID appendedPid (const char* name)
    {
        return { name, versionHintLatest };
    }

    /** Linear range. */
    juce::NormalisableRange<float> lin (float min, float max, float interval = 0.0f)
    {
        return { min, max, interval };
    }

    /** Range skewed so that `centre` sits at the middle of the control's travel. */
    juce::NormalisableRange<float> skewed (float min, float max, float centre, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r { min, max, interval };
        r.setSkewForCentre (centre);
        return r;
    }

    template <typename Container>
    juce::StringArray toStringArray (const Container& names)
    {
        juce::StringArray a;

        for (const auto& n : names)
            a.add (juce::String (std::string (n)));

        return a;
    }

    /** "12.3 Hz" / "1.24 kHz" — one formatter for every frequency parameter. */
    juce::String freqToText (float hz, int)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";

        return juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz";
    }

    /** Seconds below 1 s are shown in milliseconds. */
    juce::String secondsToText (float s, int)
    {
        if (s < 1.0f)
            return juce::String (s * 1000.0f, 0) + " ms";

        return juce::String (s, s < 10.0f ? 2 : 1) + " s";
    }

    juce::String percentToText (float v, int) { return juce::String (v, 1) + " %"; }

    std::unique_ptr<APF> percentParam (const char* paramId, const juce::String& name,
                                       float defaultValue, float max = 100.0f)
    {
        return std::make_unique<APF> (pid (paramId), name, lin (0.0f, max, 0.1f), defaultValue,
                                      FloatAtt().withLabel ("%")
                                               .withStringFromValueFunction (percentToText));
    }

    /** As percentParam, but for a parameter added after the first release. */
    std::unique_ptr<APF> appendedPercentParam (const char* paramId, const juce::String& name,
                                               float defaultValue, float max = 100.0f)
    {
        return std::make_unique<APF> (appendedPid (paramId), name, lin (0.0f, max, 0.1f),
                                      defaultValue,
                                      FloatAtt().withLabel ("%")
                                               .withStringFromValueFunction (percentToText));
    }

    std::unique_ptr<APF> freqParam (const char* paramId, const juce::String& name,
                                    float min, float max, float centre, float defaultValue)
    {
        return std::make_unique<APF> (pid (paramId), name, skewed (min, max, centre, 0.1f),
                                      defaultValue,
                                      FloatAtt().withLabel ("Hz")
                                               .withStringFromValueFunction (freqToText));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Algorithm & character -------------------------------------------

    layout.add (std::make_unique<APC> (pid (id::mode), "Mode",
                                       toStringArray (modeNames),
                                       static_cast<int> (Mode::largeHall)));

    layout.add (std::make_unique<APC> (pid (id::color), "Color",
                                       toStringArray (colorNames),
                                       static_cast<int> (Color::classic)));

    // --- Space ------------------------------------------------------------

    layout.add (std::make_unique<APF> (pid (id::preDelay), "Pre-Delay",
                                       skewed (0.0f, 500.0f, 60.0f, 0.1f), 0.0f,
                                       FloatAtt().withLabel ("ms")
                                                .withStringFromValueFunction ([] (float v, int)
                                                    { return juce::String (v, 1) + " ms"; })));

    // Decay is skewed so the musically busy 1-4 s region occupies the middle of
    // the control's travel rather than its first tenth.
    layout.add (std::make_unique<APF> (pid (id::decay), "Decay",
                                       skewed (0.1f, 60.0f, 2.5f, 0.01f), 2.5f,
                                       FloatAtt().withLabel ("s")
                                                .withStringFromValueFunction (secondsToText)));

    layout.add (percentParam (id::size, "Size", 60.0f));

    // --- Diffusion / density ---------------------------------------------

    layout.add (percentParam (id::earlyDiffusion, "Early Diffusion", 70.0f));
    layout.add (percentParam (id::lateDiffusion,  "Late Diffusion",  70.0f));
    layout.add (percentParam (id::density,        "Density",         70.0f));

    // Envelope shaping: 0 % = instant onset, 100 % = fully swelled attack.
    // The Nonlinear algorithm reinterprets this as gate / reverse length.
    layout.add (percentParam (id::attack, "Attack", 0.0f));

    // --- Modulation -------------------------------------------------------

    layout.add (std::make_unique<APF> (pid (id::modRate), "Mod Rate",
                                       skewed (0.01f, 10.0f, 0.5f, 0.001f), 0.5f,
                                       FloatAtt().withLabel ("Hz")
                                                .withStringFromValueFunction ([] (float v, int)
                                                    { return juce::String (v, 2) + " Hz"; })));

    layout.add (percentParam (id::modDepth, "Mod Depth", 30.0f));

    // --- Frequency-dependent decay ---------------------------------------

    /*
        PHASE 7 DEFAULT CHANGE - defaults only.

        High Damp 50 -> 45 % and High Damp Freq 6 -> 10 kHz. Parameter ids,
        ranges, the skew centres and every mapping are untouched; these are
        two default VALUES and nothing else.

        Why, from the default-space study: at the old 6 kHz / 50 % the worst
        500 Hz - 2 kHz deviation from the requested Decay was 7.90 %, almost
        all of it at 2 kHz. Every default axis was then swept to an interior
        optimum - Low Damp Freq and Low Damp amount both landed on what we
        already shipped, the high corner wanted 10 kHz, and the high amount
        wanted 45 % - and the combination gives 2.32 %.

        The approximate 2 % target is NOT strictly met. It is not reachable
        by any default: the 500 Hz and 2 kHz ends of the window cross over at
        about 2.3 % because Option A normalises each line by a single scalar,
        so the two shelves are coupled and neither end moves without the
        other. A second-order shelf was worse overall, and the overlap
        architecture had no measurable effect, so no further complexity is
        justified for the last 0.3 pp.

        The cost is 9.4 % of the high-frequency damping function: 45 % retains
        90.6 % of what 50 % delivered, still a 30.7 % RT60 reduction at
        16 kHz. See Docs/Phase7-HighAmountSweep.md.
    */
    layout.add (percentParam (id::lowDamp,  "Low Damp",  20.0f));
    layout.add (percentParam (id::highDamp, "High Damp", 45.0f));

    layout.add (freqParam (id::lowDampFreq,  "Low Damp Freq",   20.0f,  1000.0f,  200.0f,   200.0f));
    layout.add (freqParam (id::highDampFreq, "High Damp Freq", 500.0f, 20000.0f, 4000.0f, 10000.0f));

    // --- Output stage -----------------------------------------------------

    // 0 % = mono tail, 100 % = as generated, 200 % = mid/side expanded. Values
    // above 100 % are checked for mono compatibility by the stereo engine.
    layout.add (percentParam (id::width, "Width", 100.0f, 200.0f));

    layout.add (freqParam (id::lowCut,  "Low Cut",   20.0f,  1000.0f,  200.0f,   20.0f));
    layout.add (freqParam (id::highCut, "High Cut", 500.0f, 20000.0f, 4000.0f, 20000.0f));

    layout.add (percentParam (id::mix, "Mix", 30.0f));

    // --- PHASE 10: Early / Late ------------------------------------------

    /*
        One control for the whole early-reflection layer, and the ONLY public
        parameter PHASE 10 adds.

        A COMPOSITION CONTROL, NOT A WET LEVEL. 0 % is the late network alone,
        100 % is the early layer alone, and the energy-calibrated constant-
        power law between them holds the total wet energy to 0.01 dB across
        the travel - so moving it changes what the reverb is made of rather
        than how loud it is. Stage 2 measured the alternatives and both failed
        on exactly that point: a linear law moves the total by 3.02 dB and an
        uncalibrated equal-power law by 2.15 dB.

        DEFAULT 0, WHICH IS A COMPATIBILITY DECISION RATHER THAN A TASTE. At 0
        the processor takes an explicit bypass branch and reproduces the
        PHASE 9 signal path bit for bit, so every session saved before this
        parameter existed restores to exactly the sound it was saved with. Any
        nonzero default would quietly change all of them.

        Appended with versionHintLatest, not versionHintV1. The hint orders
        the AU parameter list, and an appended parameter sharing the
        originals' hint would sort among them instead of after them.
    */
    layout.add (appendedPercentParam (id::earlyLate, "Early / Late", 0.0f));

    return layout;
}

void RawHandles::attach (juce::AudioProcessorValueTreeState& state)
{
    auto get = [&state] (const char* paramId)
    {
        auto* p = state.getRawParameterValue (paramId);
        jassert (p != nullptr); // ID mismatch between the layout and this struct
        return p;
    };

    mode           = get (id::mode);
    color          = get (id::color);
    preDelay       = get (id::preDelay);
    decay          = get (id::decay);
    size           = get (id::size);
    earlyDiffusion = get (id::earlyDiffusion);
    lateDiffusion  = get (id::lateDiffusion);
    attack         = get (id::attack);
    modRate        = get (id::modRate);
    modDepth       = get (id::modDepth);
    density        = get (id::density);
    lowDamp        = get (id::lowDamp);
    highDamp       = get (id::highDamp);
    lowDampFreq    = get (id::lowDampFreq);
    highDampFreq   = get (id::highDampFreq);
    width          = get (id::width);
    lowCut         = get (id::lowCut);
    highCut        = get (id::highCut);
    mix            = get (id::mix);
    earlyLate      = get (id::earlyLate);
}

} // namespace av::params
