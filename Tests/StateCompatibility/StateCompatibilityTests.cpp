/*
    PHASE 10. State-compatibility suite.

    WHY THIS IS A SEPARATE TARGET
    -----------------------------
    Tests/CMakeLists.txt states the rule and enforces it by compilation:
    nothing in Source/DSP may depend on JUCE or on the processor, and the DSP
    suites deliberately do not link JUCE. That rule is worth keeping.

    But a state-compatibility test is ABOUT the processor and about APVTS, so
    it cannot live there. Reasoning about defaults is exactly what missed the
    defect this suite exists to catch, and the PHASE 10 Stage 2 report said so:
    "Do not rely only on source-code reasoning." So this is a small second
    executable that does link JUCE and the plugin's shared code, and it holds
    the real serialised fixture.

    WHAT IT CATCHES
    ---------------
    AudioProcessorValueTreeState::replaceState reconnects only the parameters
    that have a child in the loaded tree. One that is absent is never visited,
    so it keeps whatever value it currently holds rather than returning to its
    default. On a fresh instance that is the default and everything looks
    fine; load an older preset into a live instance where a newer control has
    been moved, and the preset does not reproduce the sound it was saved from.

    The fixture is generated here rather than checked in as a binary, and
    generated the only way that proves anything: by asking the processor to
    serialise a state that OMITS a parameter, which is what a genuinely older
    build would have written.
*/

#include "PluginProcessor.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;
    int checks = 0;

    void check (bool passed, const std::string& what)
    {
        ++checks;

        if (passed)
        {
            std::cout << "  PASS  " << what << "\n";
        }
        else
        {
            ++failures;
            std::cout << "  FAIL  " << what << "\n";
        }
    }

    void info (const std::string& line) { std::cout << line << "\n"; }

    [[nodiscard]] juce::AudioProcessorParameter* findParameter (av::AuroraVerbProcessor& p,
                                                                const juce::String& id)
    {
        for (auto* parameter : p.getParameters())
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter))
                if (hosted->getParameterID() == id)
                    return parameter;

        return nullptr;
    }

    /*
        A state blob that omits one parameter, which is what a build predating
        that parameter would have saved.

        Built by taking a real serialised state and removing the child, so the
        structure, the property names and the XML wrapping are all exactly
        what the plugin itself writes - not a hand-made approximation that
        might differ in a way that masks the defect.
    */
    [[nodiscard]] juce::MemoryBlock stateOmitting (av::AuroraVerbProcessor& source,
                                                   const juce::String& idToOmit)
    {
        juce::MemoryBlock full;
        source.getStateInformation (full);

        auto xml = juce::AudioProcessor::getXmlFromBinary (full.getData(),
                                                           static_cast<int> (full.getSize()));

        if (xml == nullptr)
            return {};

        for (auto* child : xml->getChildIterator())
            if (child->getStringAttribute ("id") == idToOmit)
            {
                xml->removeChildElement (child, true);
                break;
            }

        juce::MemoryBlock trimmed;
        juce::AudioProcessor::copyXmlToBinary (*xml, trimmed);

        return trimmed;
    }

    [[nodiscard]] int countChildren (const juce::MemoryBlock& state)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(),
                                                           static_cast<int> (state.getSize()));

        return xml != nullptr ? xml->getNumChildElements() : -1;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=================================================================\n"
              << "AuroraVerb state-compatibility suite\n"
              << "=================================================================\n\n";

    // -----------------------------------------------------------------------
    info ("Parameter inventory and version hints");
    info ("-------------------------------------");
    // -----------------------------------------------------------------------
    {
        av::AuroraVerbProcessor processor;

        int withZeroHint = 0;
        int hintV1 = 0, hintOther = 0;

        for (auto* parameter : processor.getParameters())
        {
            auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

            if (hosted == nullptr)
                continue;

            const auto hint = hosted->getVersionHint();

            if (hint == 0)
                ++withZeroHint;
            else if (hint == av::params::versionHintV1)
                ++hintV1;
            else
                ++hintOther;
        }

        info ("  parameters                " + std::to_string (processor.getParameters().size()));
        info ("  at generation-1 hint      " + std::to_string (hintV1));
        info ("  at a later hint           " + std::to_string (hintOther));
        info ("  with hint 0               " + std::to_string (withZeroHint));
        info ("");
        info ("  A hint of 0 trips an AU-only debug assertion in JUCE, so it is");
        info ("  checked here rather than discovered when an AU build is first");
        info ("  attempted.");

        check (withZeroHint == 0, "no parameter carries version hint 0");

        check (hintV1 == static_cast<int> (processor.getParameters().size()),
               "every shipped parameter is still at generation-1 hint");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("A state that omits a parameter");
    info ("------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            The defect, and the fix, on a fresh instance.

            `attack` is used as the stand-in for a future appended parameter:
            it is the most recently added control, its default is 0, and it is
            audible. A state omitting it is exactly the shape of a PHASE 8
            session loaded by a PHASE 9 build.
        */
        const juce::String omitted { "attack" };

        juce::MemoryBlock trimmed;

        {
            av::AuroraVerbProcessor source;

            if (auto* parameter = findParameter (source, omitted))
                parameter->setValueNotifyingHost (0.80f);

            trimmed = stateOmitting (source, omitted);
        }

        const auto children = countChildren (trimmed);

        info ("  children in the trimmed state   " + std::to_string (children));
        info ("  omitted parameter               " + omitted.toStdString());
        info ("");

        check (children > 0, "the fixture serialised");

        // --- fresh instance ------------------------------------------------
        {
            av::AuroraVerbProcessor fresh;

            auto* parameter = findParameter (fresh, omitted);

            const float before = parameter != nullptr ? parameter->getValue() : -1.0f;

            fresh.setStateInformation (trimmed.getData(),
                                       static_cast<int> (trimmed.getSize()));

            const float after = parameter != nullptr ? parameter->getValue() : -1.0f;

            info ("  fresh instance: before " + std::to_string (before)
                  + ", after " + std::to_string (after)
                  + ", default " + std::to_string (parameter != nullptr
                                                   ? parameter->getDefaultValue() : -1.0f));

            check (parameter != nullptr
                   && after == parameter->getDefaultValue(),
                   "a fresh instance leaves the omitted parameter at its default");
        }

        // --- live instance, the case that used to fail ----------------------
        {
            av::AuroraVerbProcessor live;

            auto* parameter = findParameter (live, omitted);

            // The user moves the control, then loads an older preset.
            if (parameter != nullptr)
                parameter->setValueNotifyingHost (0.65f);

            const float moved = parameter != nullptr ? parameter->getValue() : -1.0f;

            live.setStateInformation (trimmed.getData(),
                                      static_cast<int> (trimmed.getSize()));

            const float after = parameter != nullptr ? parameter->getValue() : -1.0f;

            info ("  live instance:  moved to " + std::to_string (moved)
                  + ", after loading " + std::to_string (after));
            info ("");
            info ("  This is the case APVTS gets wrong on its own: the omitted");
            info ("  parameter is never visited by replaceState, so without the");
            info ("  reset it would still read 0.65 here and the older preset");
            info ("  would not reproduce the sound it was saved from.");

            check (parameter != nullptr
                   && after == parameter->getDefaultValue(),
                   "a LIVE instance also returns the omitted parameter to its default");
        }
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("Round trip, and the parameters the state does mention");
    info ("-----------------------------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            The reset must not overreach. Resetting absent parameters is
            correct; resetting present ones would throw away the state being
            loaded, which is a far worse defect than the one being fixed.
        */
        av::AuroraVerbProcessor source;

        struct Moved { const char* id; float value; };

        const Moved moved[]
        {
            { "decay", 0.72f }, { "size", 0.33f }, { "mix", 0.91f },
            { "lowCut", 0.44f }, { "attack", 0.55f }, { "modDepth", 0.12f },
        };

        for (const auto& m : moved)
            if (auto* parameter = findParameter (source, m.id))
                parameter->setValueNotifyingHost (m.value);

        juce::MemoryBlock state;
        source.getStateInformation (state);

        av::AuroraVerbProcessor target;
        target.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        bool allRestored = true;

        /*
            COMPARED IN THE PARAMETER'S OWN UNITS, against its own step.

            A fixed normalised tolerance does not work here, and a first
            version of this suite used 1e-5 and failed on `decay` and
            `lowCut` by 1.2e-5 and 1.7e-5. Both are SKEWED parameters: APVTS
            stores the denormalised value as a float, and converting
            normalised -> denormalised -> float -> normalised back through an
            inverse skew amplifies the float's relative error far more than a
            linear range does.

            Those failures were not restore failures. 1.7e-5 normalised over
            lowCut's 980 Hz range is 0.017 Hz, and the parameter's own step is
            0.1 Hz - so the discrepancy is smaller than the smallest value the
            control can represent. The honest comparison is therefore in Hz or
            seconds against the step, which is a property of each parameter
            rather than a number chosen to make the test pass.
        */
        info ("  " + std::string ("parameter        saved       restored    error       step"));

        for (const auto& m : moved)
        {
            auto* parameter = findParameter (target, m.id);
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

            if (ranged == nullptr)
            {
                allRestored = false;
                continue;
            }

            const auto& range = ranged->getNormalisableRange();

            const float savedValue    = range.convertFrom0to1 (m.value);
            const float restoredValue = range.convertFrom0to1 (parameter->getValue());

            const float error = std::abs (restoredValue - savedValue);

            // Where a parameter declares no step, fall back to a millionth of
            // its span - still far below anything a host can send.
            const float step = range.interval > 0.0f
                             ? range.interval
                             : (range.end - range.start) * 1.0e-6f;

            if (error > step)
                allRestored = false;

            info ("  " + std::string (m.id) + std::string (17 - strlen (m.id), ' ')
                  + std::to_string (savedValue) + "   " + std::to_string (restoredValue)
                  + "   " + std::to_string (error) + "   " + std::to_string (step));
        }

        check (allRestored,
               "every parameter the state mentions is restored within its own step");

        // And a full round trip must be stable.
        juce::MemoryBlock again;
        target.getStateInformation (again);

        av::AuroraVerbProcessor third;
        third.setStateInformation (again.getData(), static_cast<int> (again.getSize()));

        bool stable = true;

        for (auto* parameter : target.getParameters())
        {
            auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

            if (hosted == nullptr)
                continue;

            auto* other = findParameter (third, hosted->getParameterID());

            if (other == nullptr || std::abs (other->getValue() - parameter->getValue()) > 1.0e-6f)
                stable = false;
        }

        check (stable, "save, load, save, load is stable across every parameter");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("Garbage and empty input");
    info ("-----------------------");
    // -----------------------------------------------------------------------
    {
        /*
            A host can hand over anything. The reset path runs on a tree that
            may be invalid, so it is worth proving it does not then wipe the
            parameters - an empty tree mentions nothing, and treating that as
            "reset everything" would destroy a session on a malformed load.
        */
        av::AuroraVerbProcessor processor;

        if (auto* parameter = findParameter (processor, "decay"))
            parameter->setValueNotifyingHost (0.42f);

        const float before = findParameter (processor, "decay")->getValue();

        const char garbage[] { 'n', 'o', 't', ' ', 'x', 'm', 'l', 0x01, 0x02 };
        processor.setStateInformation (garbage, static_cast<int> (sizeof garbage));

        const float afterGarbage = findParameter (processor, "decay")->getValue();

        processor.setStateInformation (nullptr, 0);

        const float afterEmpty = findParameter (processor, "decay")->getValue();

        info ("  decay before " + std::to_string (before)
              + ", after garbage " + std::to_string (afterGarbage)
              + ", after empty " + std::to_string (afterEmpty));
        info ("");
        info ("  Unparseable input leaves the state alone, because the reset is");
        info ("  inside the branch that only runs on a tree with the right tag -");
        info ("  an empty tree mentions no parameters, and treating that as");
        info ("  'reset everything' would destroy a session on a malformed load.");

        check (afterGarbage == before && afterEmpty == before,
               "unparseable or empty state leaves every parameter untouched");
    }

    // -----------------------------------------------------------------------
    std::cout << "\n=================================================================\n"
              << "  " << (checks - failures) << " / " << checks << " checks passed.  "
              << (failures == 0 ? "ALL TESTS PASSED." : std::to_string (failures) + " FAILURE(S).")
              << "\n=================================================================\n";

    return failures == 0 ? 0 : 1;
}
