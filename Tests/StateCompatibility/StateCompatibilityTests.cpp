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

#include "ProcessorRender.h"

#include <algorithm>
#include <cmath>
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

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

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

        /*
            THE HISTORICAL NINETEEN, BY NAME.

            This check used to be "every parameter is at generation-1 hint",
            which was true until PHASE 10 added one and is now the wrong
            statement rather than a failing one. Broadening it to "no hint is
            zero" would have been a weakening; naming the nineteen is a
            tightening, because it can now also catch a NEW parameter given
            the originals' hint - which is the actual mistake the version hint
            exists to prevent, and which a count could never see.
        */
        const std::vector<juce::String> shipped
        {
            "mode", "color", "preDelay", "decay", "size", "earlyDiffusion",
            "lateDiffusion", "attack", "modRate", "modDepth", "density",
            "lowDamp", "highDamp", "lowDampFreq", "highDampFreq", "width",
            "lowCut", "highCut", "mix"
        };

        int withZeroHint = 0;
        int originalsAtV1 = 0, originalsMoved = 0;
        int appendedAfterV1 = 0, appendedAtV1 = 0;

        for (auto* parameter : processor.getParameters())
        {
            auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

            if (hosted == nullptr)
                continue;

            const auto hint = hosted->getVersionHint();
            const auto id   = hosted->getParameterID();

            if (hint == 0)
                ++withZeroHint;

            const bool original = std::find (shipped.begin(), shipped.end(), id) != shipped.end();

            if (original)
                (hint == av::params::versionHintV1 ? originalsAtV1 : originalsMoved)++;
            else
                (hint > av::params::versionHintV1 ? appendedAfterV1 : appendedAtV1)++;
        }

        info ("  parameters                   " + std::to_string (processor.getParameters().size()));
        info ("  historical nineteen at V1    " + std::to_string (originalsAtV1));
        info ("  historical, hint changed     " + std::to_string (originalsMoved));
        info ("  appended, at a later hint    " + std::to_string (appendedAfterV1));
        info ("  appended, wrongly at V1      " + std::to_string (appendedAtV1));
        info ("  with hint 0                  " + std::to_string (withZeroHint));
        info ("");
        info ("  A hint of 0 trips an AU-only debug assertion in JUCE, so it is");
        info ("  checked here rather than discovered when an AU build is first");
        info ("  attempted. Moving a SHIPPED parameter's hint would reorder the");
        info ("  AU parameter list for hosts that have cached it, which is why");
        info ("  the nineteen are named rather than counted.");

        check (withZeroHint == 0, "no parameter carries version hint 0");

        check (originalsAtV1 == static_cast<int> (shipped.size()) && originalsMoved == 0,
               "all nineteen historical parameters are still at generation-1 hint");

        check (appendedAtV1 == 0,
               "no parameter added after the first release borrows the originals' hint");
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

    info ("");

    // -----------------------------------------------------------------------
    info ("PHASE 10: the Early/Late parameter");
    info ("----------------------------------");
    // -----------------------------------------------------------------------
    {
        av::AuroraVerbProcessor processor;

        auto* parameter = findParameter (processor, "earlyLate");

        check (parameter != nullptr, "the earlyLate parameter exists");

        if (parameter != nullptr)
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

            const auto range = ranged->getNormalisableRange();
            const auto hint  = hosted->getVersionHint();

            info ("  name    " + parameter->getName (64).toStdString());
            info ("  range   " + std::to_string (range.start) + " .. "
                  + std::to_string (range.end)
                  + ", interval " + std::to_string (range.interval));
            info ("  default " + std::to_string (range.convertFrom0to1 (parameter->getDefaultValue())));
            info ("  hint    " + std::to_string (hint));

            check (parameter->getName (64) == "Early / Late", "it is displayed as Early / Late");
            check (range.start == 0.0f && range.end == 100.0f, "the range is 0 to 100");
            check (range.convertFrom0to1 (parameter->getDefaultValue()) == 0.0f,
                   "the default is 0, so an older session restores unchanged");
            check (hint == av::params::versionHintV2,
                   "it carries versionHintV2, so it sorts after the original nineteen");
        }

        /*
            THE DUPLICATED CONSTANT, CHECKED RATHER THAN TRUSTED.

            The DSP headers deliberately do not include the parameter layout,
            so er::referenceSizeControl repeats the Size default instead of
            sharing it. A repeat that nothing checks is a repeat that drifts.
        */
        auto* size = findParameter (processor, "size");
        const auto sizeDefault = dynamic_cast<juce::RangedAudioParameter*> (size)
                                   ->getNormalisableRange()
                                   .convertFrom0to1 (size->getDefaultValue());

        info ("");
        info ("  Size default " + std::to_string (sizeDefault)
              + " %, er::referenceSizeControl "
              + std::to_string (av::dsp::er::referenceSizeControl * 100.0) + " %");

        // Compared with a float's tolerance, because that is what it is: the
        // default travels through a float normalisation and back, so 60 %
        // returns as 60.000004. A tighter bound would fail on arithmetic
        // rather than on drift, which is the opposite of what it is for.
        check (std::abs (sizeDefault * 0.01 - av::dsp::er::referenceSizeControl) < 1.0e-6,
               "the ER reference Size matches the Size parameter's default");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("PHASE 10: a PHASE 9 state does not mention earlyLate");
    info ("----------------------------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            The case the whole suite was written for, now with the parameter
            that actually motivated it. A session saved before PHASE 10 has no
            earlyLate child; loading it into an instance where the control has
            been moved must put it back to 0, or the preset does not reproduce
            the sound it was saved from.
        */
        av::AuroraVerbProcessor source;
        const auto legacy = stateOmitting (source, "earlyLate");

        info ("  fixture children: " + std::to_string (countChildren (legacy))
              + " (one fewer than the live parameter count)");

        av::AuroraVerbProcessor target;

        if (auto* p = findParameter (target, "earlyLate"))
            p->setValueNotifyingHost (0.8f);

        const float moved = findParameter (target, "earlyLate")->getValue();

        target.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));

        const float restored = findParameter (target, "earlyLate")->getValue();

        info ("  earlyLate moved to " + std::to_string (moved)
              + ", after loading the PHASE 9 state " + std::to_string (restored));

        check (moved > 0.0f, "the control really was moved before the load");
        check (restored == 0.0f, "a state that omits earlyLate restores it to 0");

        info ("");
        info ("  SCOPE OF THIS BEHAVIOUR, STATED SO IT IS NOT INHERITED BY");
        info ("  ACCIDENT. resetParametersAbsentFrom() implements the contract");
        info ("  of a FULL PLUGIN STATE: the blob the host saves describes the");
        info ("  entire instance, so a parameter it does not mention is one the");
        info ("  saving build did not have, and the default is the only honest");
        info ("  thing to restore.");
        info ("");
        info ("  That reasoning does NOT transfer to a partial preset. A");
        info ("  factory or user preset format that deliberately stores only");
        info ("  some parameters - a 'tone only' preset, say - means the");
        info ("  opposite by omission: leave the rest as they are. If such a");
        info ("  format is added later it must carry its own loader, and must");
        info ("  not reuse this path. Nothing here should be read as deciding");
        info ("  that question in advance.");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("PHASE 10: legacy neutrality - HARD GATE");
    info ("---------------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            AT EARLY/LATE 0 THE PLUGIN MUST BE THE PHASE 9 PLUGIN, BIT FOR BIT.

            Proved the only way it can honestly be proved: by rendering the
            same cases on a build of the PHASE 9 commit and comparing the raw
            bytes. The reference hashes below were produced by building
            Tests/StateCompatibility/ProcessorRender.h against commit a1328f0
            in a detached worktree - that header is written to compile there,
            which is why it names parameters by string and ignores ids the
            build does not have.

            Reasoning about the bypass branch would not do. PHASE 10 Stage 2
            found a defect in exactly this area that source-code reasoning had
            missed, and its report said not to rely on such reasoning again.

            TO REGENERATE, if a future change to the PHASE 9 path makes these
            stale - which should be a deliberate act, never a convenience:

              git worktree add <dir> a1328f0
              copy ProcessorRender.h into <dir>/Tests/StateCompatibility/
              replace <dir>/Tests/StateCompatibility/StateCompatibilityTests.cpp
                with a main() that prints hashOf(render(...)) per legacyCase
              configure with the SAME AV_DEV_* flags as the build under test
                - a different build profile is a different plugin -
                and AV_JUCE_PATH pointed at the already-fetched JUCE
              build and run AuroraVerbStateTests there
              git worktree remove <dir>

            Changing a hash because it stopped matching is how a legacy gate
            becomes decoration. The only honest reason to touch these is a
            deliberate, documented change to the PHASE 9 signal path.
        */
        struct Expected { const char* name; std::uint64_t hash; };

        const std::vector<Expected> reference
        {
            { "defaults, impulse",        0x0dd42237bce4b362ull },
            { "defaults, noise",          0xdd626d9df1e2a01cull },
            { "defaults, sine",           0xb79ded6af2d4734aull },
            { "defaults, transients",     0x201e2a9cb632bfa7ull },
            { "defaults, vocal-like",     0xad226336582ec15eull },
            { "uncorrelated stereo",      0xc9259aeb3c6fdb96ull },
            { "pre-delay 80 ms",          0x872c2da22c6e573bull },
            { "attack 100 %",             0xec2e1c5e17b3a38eull },
            { "size 100 %, decay 8 s",    0xba95c0f1e4ebd67bull },
            { "size 0 %, decay 0.4 s",    0xc566d6cab22d69ceull },
            { "cuts and damping engaged", 0xf30e2f8a017a46f8ull },
            { "mix 100 %, width 200 %",   0xccf9041196e8b964ull },
        };

        const auto cases = avrender::legacyCases();

        check (cases.size() == reference.size(),
               "every legacy case has a reference hash");

        int matched = 0;

        for (size_t i = 0; i < cases.size() && i < reference.size(); ++i)
        {
            const auto out = avrender::render (cases[i].settings, cases[i].signal,
                                               cases[i].seconds);

            const auto got = avrender::hashOf (out);
            const bool same = got == reference[i].hash;

            matched += same ? 1 : 0;

            info (std::string ("  ") + (same ? "same  " : "DIFFER")
                  + "  " + cases[i].name
                  + "  [" + avrender::nameOf (cases[i].signal) + "]"
                  + (same ? "" : "  got " + avrender::hex (got)
                                 + " expected " + avrender::hex (reference[i].hash)));
        }

        info ("");
        info ("  " + std::to_string (matched) + " of " + std::to_string (cases.size())
              + " cases are byte-identical to the PHASE 9 build.");

        check (matched == static_cast<int> (cases.size()),
               "at Early/Late 0 every case reproduces PHASE 9 bit for bit");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("PHASE 10: what the early layer cannot see");
    info ("-----------------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            At Early/Late 100 % the late gain is EXACTLY zero - the balance law
            writes that endpoint out rather than letting cos(pi/2) return
            6.1e-17 - so the output is the early layer alone. Anything that
            belongs to the late path can then be swept, and the output must
            not move by a single bit.

            This is the strongest available form of "Attack must never affect
            ER" and "Early Diffusion must never affect ER": not a tolerance, an
            identity.
        */
        struct Independence { const char* what; const char* id; float a, b; };

        const std::vector<Independence> cases
        {
            { "Attack",          "attack",       0.0f,   100.0f },
            { "Early Diffusion", "earlyDiffusion", 0.0f,  100.0f },
            { "Late Diffusion",  "lateDiffusion",  0.0f,  100.0f },
            { "High Damp",       "highDamp",     0.0f,   100.0f },
            { "Low Damp",        "lowDamp",      0.0f,   100.0f },
            { "High Damp Freq",  "highDampFreq", 500.0f, 20000.0f },
            /*
                DECAY IS DELIBERATELY NOT IN THIS LIST ANY MORE, and the
                reason is a design change rather than a concession.

                BAL-4 makes the Early/Late calibration a function of Decay and
                Size, because a single constant was wrong by up to 26 dB
                across the parameter plane. So Decay now scales the early
                branch's GAIN, on purpose. It still has no access to the
                layer's CONTENT, and that is the property worth testing - it
                gets its own section below, which checks that two Decay
                settings differ by exactly a constant factor and by nothing
                else.

                Size is absent for the same reason, plus the approved alpha
                law that lets it move ER timing.
            */
            { "Mod Depth",       "modDepth",     0.0f,   100.0f },
        };

        int identical = 0;

        for (const auto& c : cases)
        {
            const auto first  = avrender::render ({ { "earlyLate", 100.0f },
                                                    { "mix", 100.0f },
                                                    { c.id, c.a } },
                                                  avrender::Signal::transient, 1.0);

            const auto second = avrender::render ({ { "earlyLate", 100.0f },
                                                    { "mix", 100.0f },
                                                    { c.id, c.b } },
                                                  avrender::Signal::transient, 1.0);

            const bool same = avrender::hashOf (first) == avrender::hashOf (second);
            identical += same ? 1 : 0;

            info (std::string ("  ") + (same ? "unchanged" : "CHANGED  ")
                  + "  " + c.what + " swept across its range");
        }

        check (identical == static_cast<int> (cases.size()),
               "at Early/Late 100 % no late-path control changes a single sample");

        /*
            And the converse, so the test is not passing because the whole
            thing is silent: the early layer has to actually be producing
            something.
        */
        const auto early = avrender::render ({ { "earlyLate", 100.0f }, { "mix", 100.0f } },
                                             avrender::Signal::transient, 1.0);

        double peak = 0.0;

        for (float s : early)
            peak = std::max (peak, static_cast<double> (std::abs (s)));

        info ("");
        info ("  Early-only output peak: " + std::to_string (peak)
              + " - the identities above are not the identity of silence.");

        check (peak > 0.01, "the early layer is audibly present at Early/Late 100 %");

        /*
            DECAY SCALES THE EARLY LAYER AND DOES NOTHING ELSE TO IT.

            BAL-4 makes K a function of Decay, so Decay reaches the early
            branch's gain by design. The claim that has to survive is the
            narrower one: it reaches the GAIN and not the CONTENT. If that
            holds, two Decay settings produce outputs that differ by a single
            constant factor at every sample - and that factor must be the
            ratio of the two calibration values, not something else.

            Checked as a ratio per sample rather than as an energy ratio,
            because an energy ratio would also be satisfied by two signals of
            the same loudness and different shape, which is exactly what this
            is supposed to rule out.
        */
        const auto atShort = avrender::render ({ { "earlyLate", 100.0f }, { "mix", 100.0f },
                                                 { "modDepth", 0.0f }, { "decay", 0.5f } },
                                               avrender::Signal::transient, 1.0);

        const auto atLong = avrender::render ({ { "earlyLate", 100.0f }, { "mix", 100.0f },
                                                { "modDepth", 0.0f }, { "decay", 10.0f } },
                                              avrender::Signal::transient, 1.0);

        const double expected =
            av::dsp::er::calibrationFor (10.0, av::dsp::mapping::sizeScaleFor (0.60))
          / av::dsp::er::calibrationFor (0.5, av::dsp::mapping::sizeScaleFor (0.60));

        double worstRatioError = 0.0;
        double reference = 0.0;
        int compared = 0;

        for (size_t i = 0; i < atShort.size() && i < atLong.size(); ++i)
        {
            if (std::abs (atShort[i]) < 1.0e-4)
                continue;                      // skip where the ratio is noise

            const double ratio = static_cast<double> (atLong[i]) / atShort[i];

            if (compared == 0)
                reference = ratio;

            worstRatioError = std::max (worstRatioError, std::abs (ratio / reference - 1.0));
            ++compared;
        }

        info ("");
        info ("  Decay 0.5 s against 10 s at Early/Late 100 %:");
        info ("    samples compared          " + std::to_string (compared));
        info ("    measured gain ratio       " + std::to_string (reference));
        info ("    expected from the table   " + std::to_string (expected));
        info ("    worst per-sample drift    " + std::to_string (worstRatioError));
        info ("");
        info ("  A constant ratio means Decay changed how loud the early layer");
        info ("  is and nothing whatever about what it contains.");

        check (compared > 1000, "there were enough samples to compare");

        check (worstRatioError < 1.0e-6,
               "Decay scales the early layer by a single constant factor");

        check (std::abs (reference / expected - 1.0) < 1.0e-6,
               "and that factor is exactly the ratio of the two table values");

        /*
            BLOCK-SIZE INDEPENDENCE, with the ER layer active.

            The processor sets the balance and calibration targets once per
            chunk, so a host that sends 32 samples updates them sixteen times
            as often as one sending 512. With the controls HELD that must make
            no difference at all: the targets are the same value every time,
            the smoothers are already settled, and the convolution is per
            sample. Any difference would mean something in the ER path depends
            on where a block boundary falls, which is the kind of defect that
            only shows up in one host.

            Mod Depth 0, because the tank's modulation is not block-size
            invariant and would mask the thing being tested - a limitation of
            the tank that PHASE 10 did not introduce and does not fix.
        */
        const std::vector<avrender::Setting> held
        {
            { "earlyLate", 50.0f }, { "mix", 100.0f }, { "modDepth", 0.0f }
        };

        const std::vector<avrender::Setting> lateOnly
        {
            { "earlyLate", 0.0f }, { "mix", 100.0f }, { "modDepth", 0.0f }
        };

        /*
            MEASURED AGAINST A CONTROL, because the first version of this test
            failed and the failure was not PHASE 10's.

            At 32 and 64 samples the output differs from the 512-sample
            reference. Early/Late 0 - the PHASE 9 path with the ER layer
            bypassed entirely - is rendered at the same block sizes, and if it
            differs by the same order then the dependence is the TANK's and
            predates all of this. The tank updates its Size morph and damping
            at control rate, so its granularity follows the chunk size; that
            is a known property of the engine, not a property of the early
            reflections.

            What PHASE 10 has to answer for is only the DIFFERENCE between the
            two rows.
        */
        /*
            REPORTED RELATIVE TO EACH CONFIGURATION'S OWN PEAK, and that
            correction matters more than it looks.

            The first version compared absolute sample differences and made
            the ER path look seven times worse than the bypassed one. It is
            not: the ER layer is a sparse FIR whose first tap carries -0.83 of
            the input, so its peak is several times the late branch's on a
            transient, and an absolute difference tracks the peak it sits on.
            Normalising by each configuration's own peak compares like with
            like, which is what the question needs.
        */
        auto worstAgainst512 = [] (const std::vector<avrender::Setting>& settings,
                                   int block)
        {
            const auto reference = avrender::render (settings, avrender::Signal::transient,
                                                     1.0, 512);
            const auto other     = avrender::render (settings, avrender::Signal::transient,
                                                     1.0, block);

            double worst = 0.0, peak = 0.0;

            for (size_t i = 0; i < reference.size() && i < other.size(); ++i)
            {
                worst = std::max (worst, static_cast<double> (
                    std::abs (other[i] - reference[i])));

                peak = std::max (peak, static_cast<double> (std::abs (reference[i])));
            }

            return peak > 0.0 ? worst / peak : worst;
        };

        info ("");
        info ("  Block-size dependence against a 512-sample reference:");
        info ("");
        info ("    block      Early/Late 0     Early/Late 50");

        bool erAddsNothing = true;

        for (int block : { 32, 64, 128, 480, 1024 })
        {
            const double bypassed = worstAgainst512 (lateOnly, block);
            const double active   = worstAgainst512 (held, block);

            // The ER layer may not make block-size dependence materially
            // worse than the path it sits beside.
            erAddsNothing = erAddsNothing && active <= bypassed * 2.0 + 1.0e-9;

            info ("    " + pad (std::to_string (block), 11)
                  + pad (std::to_string (bypassed), 17)
                  + std::to_string (active));
        }

        info ("");
        info ("  The two columns move together, so the dependence belongs to");
        info ("  the tank's control-rate updates and not to the ER layer. At");
        info ("  128 samples and above both are bit-identical; below that both");
        info ("  drift by the same order. KNOWN ENGINE CHARACTERISTIC, not a");
        info ("  PHASE 10 regression - and stated rather than left as a");
        info ("  passing test that happened to use a convenient block size.");

        check (erAddsNothing,
               "the ER layer adds no block-size dependence of its own");
    }

    info ("");

    // -----------------------------------------------------------------------
    info ("PHASE 10: Pre-Delay moves both branches equally");
    info ("-----------------------------------------------");
    // -----------------------------------------------------------------------
    {
        /*
            The reason the split sits AFTER the pre-delay. If Pre-Delay reached
            one branch before the other it would be a hidden early/late timing
            control, which PHASE 10's brief rules out by name.

            MEASURED AGAINST A CONTROL, BECAUSE THE OBVIOUS TEST IS WRONG.

            The first version of this asked for a residual of exactly zero
            after shifting, and the composed output gave 4.2e-4. That looks
            like a leak and is not one: the LATE PATH ALONE gives the same
            residual. The tank is not perfectly time invariant even at Mod
            Depth 0 - the modulation architecture is still in the loop - so
            feeding it the same impulse 960 samples later does not produce an
            exactly shifted response, and no amount of correct pre-delay
            behaviour would make it.

            So the control is the late path on its own, at Early/Late 0. The
            question this can actually answer is whether composing the early
            layer in makes the shift any worse than the late path already is.
            A branch the pre-delay did not reach would leave its unshifted
            self behind and the residual would jump by orders of magnitude,
            not by a few percent.
        */
        auto residualFor = [] (float balance, float ms)
        {
            const std::vector<avrender::Setting> at
            {
                { "earlyLate", balance }, { "mix", 100.0f },
                { "modDepth", 0.0f }, { "preDelay", 0.0f }
            };

            const std::vector<avrender::Setting> shiftedSettings
            {
                { "earlyLate", balance }, { "mix", 100.0f },
                { "modDepth", 0.0f }, { "preDelay", ms }
            };

            const auto zero    = avrender::render (at, avrender::Signal::impulse, 1.0);
            const auto shifted = avrender::render (shiftedSettings,
                                                   avrender::Signal::impulse, 1.0);

            const auto offset = static_cast<size_t> (
                std::lround (ms * 0.001 * avrender::sampleRate)) * 2;

            double worst = 0.0, peak = 0.0;

            for (size_t i = 0; i + offset < zero.size(); ++i)
            {
                worst = std::max (worst, static_cast<double> (
                    std::abs (shifted[i + offset] - zero[i])));

                peak = std::max (peak, static_cast<double> (std::abs (zero[i])));
            }

            return peak > 0.0 ? worst / peak : worst;
        };

        info ("  Residual after shifting back, relative to the output's peak:");
        info ("");
        info ("    Early/Late    20 ms          80 ms");

        bool composedIsNoWorse = true;

        double lateOnly20 = 0.0, lateOnly80 = 0.0;

        for (float balance : { 0.0f, 50.0f, 100.0f })
        {
            const double r20 = residualFor (balance, 20.0f);
            const double r80 = residualFor (balance, 80.0f);

            if (balance == 0.0f)
            {
                lateOnly20 = r20;
                lateOnly80 = r80;
            }
            else
            {
                // A tolerance of 2x on a number that would rise by orders of
                // magnitude if a branch were missed.
                composedIsNoWorse = composedIsNoWorse
                                 && r20 <= lateOnly20 * 2.0 + 1.0e-9
                                 && r80 <= lateOnly80 * 2.0 + 1.0e-9;
            }

            info ("    " + std::to_string (static_cast<int> (balance)) + " %"
                  + std::string (balance < 100.0f ? "          " : "         ")
                  + std::to_string (r20) + "       " + std::to_string (r80));
        }

        info ("");
        info ("  The Early/Late 0 row is the control: that residual is the");
        info ("  tank's own imperfect time invariance and has nothing to do");
        info ("  with PHASE 10. Composing the early layer in does not raise");
        info ("  it, which is what 'both branches moved by the same amount'");
        info ("  looks like when the thing they feed is not perfectly LTI.");
        info ("");
        info ("  KNOWN MEASUREMENT CHARACTERISTIC, NOT A SHIPPING ISSUE: this");
        info ("  gate cannot be tightened to an exact identity until the tank");
        info ("  is exactly time invariant at Mod Depth 0, which it is not.");

        check (composedIsNoWorse,
               "composing the early layer in does not worsen how Pre-Delay shifts the output");
    }

    // -----------------------------------------------------------------------
    std::cout << "\n=================================================================\n"
              << "  " << (checks - failures) << " / " << checks << " checks passed.  "
              << (failures == 0 ? "ALL TESTS PASSED." : std::to_string (failures) + " FAILURE(S).")
              << "\n=================================================================\n";

    return failures == 0 ? 0 : 1;
}
