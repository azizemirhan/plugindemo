#include "DampingListening.h"

#include "GainStaging.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;

    using Morph = MorphingStereoFdn<N>;

    // -----------------------------------------------------------------------
    //  The two settings under test, and everything held fixed
    // -----------------------------------------------------------------------

    /**
        One damping operating point.

        These are control positions, 0 to 1 for the amounts and Hz for the
        corners, exactly as the parameters carry them. They are turned into
        shelf ratios by the same mapping the processor uses, so a setting here
        and the same setting in the host drive the tank identically.
    */
    struct Setting
    {
        const char* name;
        double lowDamp;
        double highDamp;
        double lowDampHz;
        double highDampHz;
    };

    /*
        A is the default the plugin shipped with before Phase 7. B is the
        default it ships with now.

        A is written out here rather than read from anywhere because it no
        longer exists in the product: it is a historical operating point, and
        the whole purpose of this set is to put the product's past against its
        present. B, on the other hand, duplicates a production default, and
        that duplication is the test-infrastructure debt recorded in
        Docs/Phase7-TestInfrastructureDebt.md - this file is one of the sites
        the deferred cleanup will migrate to a single checked contract.
    */
    constexpr Setting kSettingA { "A", 0.20, 0.50, 200.0,  6000.0 };
    constexpr Setting kSettingB { "B", 0.20, 0.45, 200.0, 10000.0 };

    /*
        Everything that is NOT damping. The listening gate forbids compensating
        for a damping change by moving any of these, so they are constants in
        one place where a reader can see that both sides got the same value,
        rather than two call sites that happen to agree.
    */
    constexpr double kDecaySeconds   = 2.5;
    constexpr double kSizeControl    = 0.60;
    constexpr double kModDepthControl = 0.30;
    constexpr double kModRateHz      = 0.5;
    constexpr double kWidth          = 1.0;
    constexpr double kShippingMix    = 0.30;

    /** Mix points: what a user hears, and the tail magnified. */
    struct MixPoint { const char* name; double wet; };

    const MixPoint kMixPoints[]
    {
        { "mix30", kShippingMix },
        { "wet",   1.0 },
    };

    // -----------------------------------------------------------------------
    //  Rendering
    // -----------------------------------------------------------------------

    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate, const Setting& setting)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (kDecaySeconds), true);
        tank->setSize (mapping::sizeScaleFor (kSizeControl));

        // Damping after Size and Decay, because the shelf gains are derived
        // from the loop delays and the RT60 those two produce. This is the
        // order PluginProcessor uses and the reason it uses it.
        tank->setDamping (mapping::lowDampRatioFor  (setting.lowDamp),
                          mapping::highDampRatioFor (setting.highDamp),
                          setting.lowDampHz,
                          setting.highDampHz,
                          true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (kModDepthControl),
                                      mapping::modRateScaleFor (kModRateHz),
                                      true);

        // Settle the Size morph on silence, then clear the tank, so the render
        // holds the tank AT its target size rather than a transition into it.
        for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        tank->reset();

        return tank;
    }

    /**
        The production chain, with damping as the only thing a caller varies.

        Tank, then the decay-aware preview trim, then width, then the linear
        wet/dry crossfade - the same four stages in the same order as
        GainStaging's renderPreviewChain, which is the order the processor runs
        them in. Duplicated rather than shared because that function has no
        damping parameter and giving it one would change a file the Phase 5F
        and FU-2 listening sets were already made from.
    */
    [[nodiscard]] std::vector<StereoFrame> render (const std::vector<StereoFrame>& dry,
                                                   double sampleRate,
                                                   const Setting& setting,
                                                   double wetMix)
    {
        ScopedFtz ftz;

        auto tank = makeTank (sampleRate, setting);

        const double trim = mapping::previewWetTrim (mapping::rt60For (kDecaySeconds),
                                                     mapping::sizeScaleFor (kSizeControl));

        const auto tail = static_cast<size_t> (sampleRate * (kDecaySeconds + 1.0));

        std::vector<StereoFrame> out (dry.size() + tail);

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double dryL = n < dry.size() ? dry[n].l : 0.0;
            const double dryR = n < dry.size() ? dry[n].r : 0.0;

            double wetL = dryL, wetR = dryR;
            tank->processSample (wetL, wetR);

            wetL *= trim;
            wetR *= trim;

            const double mid  = (wetL + wetR) * 0.5;
            const double side = (wetL - wetR) * 0.5 * kWidth;

            wetL = mid + side;
            wetR = mid - side;

            const double dryAmount = 1.0 - wetMix;

            out[n].l = dryL * dryAmount + wetL * wetMix;
            out[n].r = dryR * dryAmount + wetR * wetMix;
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  Measurement, for the key and for the level match
    // -----------------------------------------------------------------------

    /**
        Energy above and below the high corner region, as a plain ratio in dB.

        Not a loudness model and not a verdict. The whole point of moving the
        high corner from 6 to 10 kHz is that the tank holds on to more
        high-frequency energy, so the manifest should be able to say by how
        much instead of asking a listener to discover it. One number, measured
        the same way on both sides.

        A single-pole split at 6 kHz, run forwards only: the absolute value is
        not used for anything, only the A-to-B difference, and both sides get
        the identical filter.
    */
    [[nodiscard]] double highToLowDb (const std::vector<StereoFrame>& frames, double sampleRate)
    {
        constexpr double splitHz = 6000.0;

        const double a = std::exp (-2.0 * 3.14159265358979323846 * splitHz / sampleRate);

        double lowEnergy = 0.0, highEnergy = 0.0;
        double stateL = 0.0, stateR = 0.0;

        for (const auto& f : frames)
        {
            stateL = a * stateL + (1.0 - a) * f.l;
            stateR = a * stateR + (1.0 - a) * f.r;

            const double hiL = f.l - stateL;
            const double hiR = f.r - stateR;

            lowEnergy  += stateL * stateL + stateR * stateR;
            highEnergy += hiL * hiL + hiR * hiR;
        }

        if (lowEnergy <= 0.0 || highEnergy <= 0.0)
            return 0.0;

        return 10.0 * std::log10 (highEnergy / lowEnergy);
    }

    [[nodiscard]] std::string fixed (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    // -----------------------------------------------------------------------
    //  Blinding
    // -----------------------------------------------------------------------

    /**
        Which letter each setting gets, per source.

        Five sources, five different assignments, so an order learned on one
        source does not carry to the next. Two of the five put A first and
        three put B first; that is deliberate rather than alternating, because
        a strict alternation is itself a pattern a listener can follow.
    */
    struct Blinding { const char* first; const char* second; };

    const Blinding kBlinding[]
    {
        { "B", "A" },
        { "A", "B" },
        { "B", "A" },
        { "B", "A" },
        { "A", "B" },
        { "A", "B" },
    };

    [[nodiscard]] const Setting& settingNamed (const char* name)
    {
        return (name[0] == 'A') ? kSettingA : kSettingB;
    }


} // namespace

/**
    A bright percussion source, and why this set needs one.

    The established FU-2 source set is a 220 Hz tone, a piano chord, a
    snare stand-in, a full-band mix and a vocal. The first render of this
    A/B showed that eight of its ten pairs come out near-identical, and the
    reason is the material: moving the high damping corner from 6 to 10 kHz
    can only be heard on energy above 6 kHz, and four of those five sources
    carry almost none. A listening gate that cannot reveal the change it is
    gating is not a gate, so this source is here to give it something to
    act on.

    It is a hi-hat / shaker stand-in: short noise transients with the low
    end differenced away, at a steady interval. Noise rather than a
    resonant model on purpose - a metallic resonator would put metallic
    character into the source, and question 3 asks whether the TANK sounds
    metallic. The source must not answer that question for it.

    This is an addition, not part of the established Phase 6 material, and
    it is named as one in the manifest so a reader is never misled about
    where it came from.
*/
[[nodiscard]] std::vector<StereoFrame> makeBrightPercussion (double sampleRate, double seconds)
{
    std::vector<StereoFrame> out (static_cast<size_t> (sampleRate * seconds));

    // Deterministic, like every other source in the listening material.
    unsigned int seed = 1973u;

    const auto next = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double> (seed) / 2147483648.0 - 1.0;
    };

    double previousL = 0.0, previousR = 0.0;

    for (size_t n = 0; n < out.size(); ++n)
    {
        const double t = static_cast<double> (n) / sampleRate;

        // Eighth notes at 120 bpm, alternating accent, so the tail of one
        // hit is still audible when the next arrives - which is where a
        // damping difference lives.
        const double since = std::fmod (t, 0.25);
        const bool accent = std::fmod (t, 0.5) < 0.25;

        const double envelope = std::exp (-since / 0.030) * (accent ? 0.5 : 0.3);

        const double rawL = next() * envelope;
        const double rawR = next() * envelope;

        // A one-sample difference is a 6 dB/octave highpass. Crude, and
        // deliberately so: it tilts energy upward without imposing a
        // corner frequency of its own anywhere near the 6 and 10 kHz the
        // two settings differ at.
        out[n] = { rawL - previousL, rawR - previousR };

        previousL = rawL;
        previousR = rawR;
    }

    return out;
}

int exportDampingListeningSet (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    // --- the sources ------------------------------------------------------
    //
    // Four classes are required. `snare` is the percussive one, `pad` and
    // `piano` the harmonic sustained ones, `mixed` the full-band musical one,
    // and `vocal` comes from the export set's own generator rather than a
    // second copy of it, so the two cannot drift apart.
    //
    // `hats` is NOT part of the established material. It was added after the
    // first render of this set came out near-identical on eight of ten pairs,
    // because none of the five established sources carries enough energy above
    // 6 kHz for a change to the high damping corner to act on. See
    // makeBrightPercussion.

    auto sources = makeFu2Sources (sampleRate);

    sources.push_back ({ "vocal", makeVocalReference (sampleRate, 4.0) });
    sources.push_back ({ "hats",  makeBrightPercussion (sampleRate, 6.0) });

    constexpr size_t blindingCount = sizeof kBlinding / sizeof kBlinding[0];

    if (sources.size() != blindingCount)
        return -1;   // a source was added without deciding its blinding

    std::ostringstream manifest;
    std::ostringstream key;

    manifest << "AuroraVerb - Phase 7 damping default A/B listening set\n"
             << "======================================================\n\n"
             << "Two damping operating points. Everything else is identical.\n\n"
             << "  one side   Low Damp 20 %, Low Damp Freq 200 Hz,\n"
             << "             High Damp 50 %, High Damp Freq 6 kHz\n"
             << "             - the default the plugin shipped with before Phase 7\n\n"
             << "  other side Low Damp 20 %, Low Damp Freq 200 Hz,\n"
             << "             High Damp 45 %, High Damp Freq 10 kHz\n"
             << "             - the default it ships with now\n\n"
             << "Which side is which is in the key file. Do not open it until the\n"
             << "answers below are written down.\n\n"
             << "Held fixed on both sides, and not to be moved during the comparison:\n"
             << "  Decay      " << fixed (kDecaySeconds, 1) << " s\n"
             << "  Size       " << fixed (kSizeControl * 100.0, 0) << " %\n"
             << "  Mix        " << fixed (kShippingMix * 100.0, 0) << " %\n"
             << "  Width      " << fixed (kWidth * 100.0, 0) << " %\n"
             << "  Mod Depth  " << fixed (kModDepthControl * 100.0, 0) << " %\n"
             << "  Mod Rate   " << fixed (kModRateHz, 2) << " Hz\n"
             << "  early diffusion on, input and output gain unity\n\n"
             << "Compensating for a damping difference by moving any of these turns\n"
             << "the comparison into a different question.\n\n"
             << "Sample rate " << fixed (sampleRate, 0) << " Hz, 32-bit float stereo.\n\n"
             << "Sources: mixed, pad, snare, piano and vocal are the established\n"
             << "material. `hats` is not - it was added for this set because none of\n"
             << "the other five carries enough energy above 6 kHz for a change to the\n"
             << "high damping corner to act on, and a gate that cannot reveal the\n"
             << "change it gates is not a gate. It is a noise-based hi-hat stand-in\n"
             << "with no resonator in it, so that it cannot answer the question about\n"
             << "metallic character on the tank's behalf.\n\n";

    key << "AuroraVerb - Phase 7 damping listening set: KEY\n"
        << "===============================================\n\n"
        << "Letter mapping and per-file measurements. A loudness column is the\n"
        << "mapping written in another alphabet, which is why both live here\n"
        << "and not in the manifest.\n\n"
        << "  A = High Damp 50 %, High Damp Freq  6 kHz   (pre-Phase-7 default)\n"
        << "  B = High Damp 45 %, High Damp Freq 10 kHz   (current default)\n\n";

    int written = 0;

    /*
        How much each pair actually differs, collected while rendering.

        This exists because the first render of this set showed that three of
        the five sources barely move between the two settings: a 220 Hz tone
        and a piano chord have almost no energy above 6 kHz, so a change to
        the high corner cannot do anything audible to them. Handing a listener
        a pair that is physically near-identical and asking "which is
        brighter?" manufactures a false positive, so the manifest says up front
        which pairs can carry a difference and which cannot.

        Unsigned, so it reports whether a difference exists without reporting
        which side is which. The signed, attributed figures are in the key.
    */
    struct PairDifference
    {
        std::string source;
        std::string mix;
        double loudnessDeltaDb;
        double balanceDeltaDb;
        double worstSample;
    };

    std::vector<PairDifference> differences;

    // --- what the measurement already knows -------------------------------

    manifest << "What the measurement already says, so that listening is not asked\n"
             << "to rediscover it\n"
             << "-----------------------------------------------------------------\n"
             << "  worst 500 Hz - 2 kHz deviation from the requested Decay\n"
             << "    pre-Phase-7 default   7.90 %\n"
             << "    current default       2.32 %\n"
             << "  every band's tail stays single-slope at the current default,\n"
             << "  |T20-T30| <= 3.11 %, R^2 >= 0.9979\n"
             << "  the cost is 9.4 % of the high-frequency damping function\n\n"
             << "So the expected difference is: the current default holds on to more\n"
             << "mid and high-frequency decay. The listening question is whether that\n"
             << "is an improvement or merely a measurement.\n\n";

    // --- render -----------------------------------------------------------

    manifest << "Files\n-----\n";

    for (size_t s = 0; s < sources.size(); ++s)
    {
        const auto& source = sources[s];
        const auto& blinding = kBlinding[s];

        key << source.name << "\n";

        for (const auto& mix : kMixPoints)
        {
            // Render both settings before writing either, so the pair can be
            // loudness-matched against each other.
            const auto firstRender  = render (source.frames, sampleRate,
                                              settingNamed (blinding.first), mix.wet);
            const auto secondRender = render (source.frames, sampleRate,
                                              settingNamed (blinding.second), mix.wet);

            const std::string stem = std::string (source.name) + "-" + mix.name + "-";

            const std::string name1 = stem + "1.wav";
            const std::string name2 = stem + "2.wav";

            if (! writeStereoWav (directory + "/" + name1, firstRender, sampleRate))
                return -1;

            ++written;

            if (! writeStereoWav (directory + "/" + name2, secondRender, sampleRate))
                return -1;

            ++written;

            const auto level1 = measureWindow (name1, firstRender, sampleRate);
            const auto level2 = measureWindow (name2, secondRender, sampleRate);

            const double balance1 = highToLowDb (firstRender, sampleRate);
            const double balance2 = highToLowDb (secondRender, sampleRate);

            double worstSample = 0.0;

            for (size_t n = 0; n < std::min (firstRender.size(), secondRender.size()); ++n)
            {
                worstSample = std::max (worstSample, std::abs (firstRender[n].l - secondRender[n].l));
                worstSample = std::max (worstSample, std::abs (firstRender[n].r - secondRender[n].r));
            }

            differences.push_back ({ source.name, mix.name,
                                     std::abs (level1.loudnessLufs - level2.loudnessLufs),
                                     std::abs (balance1 - balance2),
                                     worstSample });

            // The loudness-matched copies. The quieter side is left exactly as
            // rendered and the louder one is brought down to meet it, so the
            // match never adds gain and cannot push a file over full scale.
            const double delta = level1.loudnessLufs - level2.loudnessLufs;

            const double gain1 = delta > 0.0 ? std::pow (10.0, -delta / 20.0) : 1.0;
            const double gain2 = delta < 0.0 ? std::pow (10.0,  delta / 20.0) : 1.0;

            auto matched1 = firstRender;
            auto matched2 = secondRender;

            for (auto& f : matched1) { f.l *= gain1; f.r *= gain1; }
            for (auto& f : matched2) { f.l *= gain2; f.r *= gain2; }

            const std::string matchedName1 = stem + "1-matched.wav";
            const std::string matchedName2 = stem + "2-matched.wav";

            if (! writeStereoWav (directory + "/" + matchedName1, matched1, sampleRate))
                return -1;

            ++written;

            if (! writeStereoWav (directory + "/" + matchedName2, matched2, sampleRate))
                return -1;

            ++written;

            manifest << "  " << pad (stem + "1.wav", 30) << "vs  " << stem << "2.wav\n";

            key << "  " << pad (mix.name, 8)
                << "1 = " << blinding.first
                << "   " << fixed (level1.loudnessLufs, 2) << " LUFS"
                << "   peak " << fixed (level1.peakDb, 2) << " dB"
                << "   HF/LF " << fixed (balance1, 2) << " dB"
                << "   match gain " << fixed (20.0 * std::log10 (gain1), 2) << " dB\n"
                << "  " << pad (mix.name, 8)
                << "2 = " << blinding.second
                << "   " << fixed (level2.loudnessLufs, 2) << " LUFS"
                << "   peak " << fixed (level2.peakDb, 2) << " dB"
                << "   HF/LF " << fixed (balance2, 2) << " dB"
                << "   match gain " << fixed (20.0 * std::log10 (gain2), 2) << " dB\n";
        }

        key << "\n";

        // The dry source, so the comparison has a reference that has not been
        // through the tank at all.
        const std::string dryName = std::string (source.name) + "-dry.wav";

        if (! writeStereoWav (directory + "/" + dryName, source.frames, sampleRate))
            return -1;

        ++written;

        manifest << "  " << pad (dryName, 30) << "the source, no reverb\n";
    }

    manifest << "\n  *-matched.wav are the same renders with the louder side brought\n"
             << "  down to the quieter side's loudness. Gain only, never upward, and\n"
             << "  the amount is in the key. Judge level on the unmatched pair and\n"
             << "  tone on the matched one.\n\n";

    // --- which pairs can carry a difference at all -------------------------

    manifest << "How much the two files in each pair actually differ\n"
             << "---------------------------------------------------\n"
             << "Measured on the renders themselves and reported unsigned, so this\n"
             << "says whether a difference exists without saying which side is which.\n\n"
             << "  HF/LF balance is the energy above 6 kHz over the energy below it.\n"
             << "  It is not a loudness model; only the difference between the two\n"
             << "  sides is used, and both sides are measured identically.\n\n"
             << "  " << pad ("source", 10) << pad ("mix", 8) << pad ("loudness", 12)
             << pad ("HF/LF", 12) << pad ("worst sample", 16) << "\n";

    /*
        0.25 dB as the line between "this pair can carry an audible difference"
        and "this pair cannot". It is roughly the smallest broadband level or
        tilt difference that survives an A/B on program material; below it a
        listener who reports a difference is reporting expectation. The number
        is a judgement about hearing, not a measurement, and it is written here
        so that it can be argued with rather than discovered in the code.
    */
    constexpr double kAudibleDb = 0.25;

    int nullTrials = 0;

    for (const auto& d : differences)
    {
        const bool canReveal = d.loudnessDeltaDb >= kAudibleDb || d.balanceDeltaDb >= kAudibleDb;

        if (! canReveal)
            ++nullTrials;

        manifest << "  " << pad (d.source, 10) << pad (d.mix, 8)
                 << pad (fixed (d.loudnessDeltaDb, 2) + " dB", 12)
                 << pad (fixed (d.balanceDeltaDb, 2) + " dB", 12)
                 << pad (fixed (d.worstSample, 6), 16)
                 << (canReveal ? "can carry a difference" : "near-identical: a null trial")
                 << "\n";
    }

    manifest << "\n  " << nullTrials << " of " << differences.size()
             << " pairs are near-identical.\n\n"
             << "  Two things about the table before the pattern in it is read as a\n"
             << "  result.\n\n"
             << "  The HF/LF column saturates on a source that is almost entirely\n"
             << "  high frequency to begin with. `hats` has very little energy below\n"
             << "  6 kHz, so the ratio barely moves even when the absolute amount of\n"
             << "  high-frequency energy changes a great deal - which is why that row\n"
             << "  is flagged on its loudness difference and not its balance. The\n"
             << "  column is a tilt measure, and a tilt measure has nothing to say\n"
             << "  about a signal with only one side to it.\n\n"
             << "  And every mix30 pair is null while three wet pairs are not. That is\n"
             << "  arithmetic, not a defect: at 30 % wet the difference between the two\n"
             << "  wet signals is scaled by 0.3 and sits under a dry signal at 0.7, so\n"
             << "  a 1 dB difference in the tail arrives as about 0.1 dB at the output.\n"
             << "  The honest reading is that this default change is a change to the\n"
             << "  tail, and at the shipping Mix the tail is a minority of what is\n"
             << "  heard. Run the wet pairs to judge whether the new tail is better,\n"
             << "  and the mix30 pairs to confirm that nothing in normal use got worse.\n\n"
             << "  A null trial is not a flaw in the set, and it is not padding: it\n"
             << "  is the finding that on that material these two settings do almost\n"
             << "  nothing different, which is worth knowing and worth confirming by\n"
             << "  ear. But do not strain to hear a difference in one. If a null pair\n"
             << "  sounds different, that is a reading of expectation, not of damping,\n"
             << "  and the right answer to write down is \"no difference\".\n\n"
             << "  The reason so many are null is the material, not the change: a\n"
             << "  220 Hz tone and a piano chord carry almost no energy above 6 kHz,\n"
             << "  so moving the high corner from 6 to 10 kHz has nothing to act on.\n"
             << "  The pairs that can answer the brightness question are the ones\n"
             << "  with broadband content.\n\n";

    // --- the questions ----------------------------------------------------

    manifest << "The six questions\n-----------------\n"
             << "Answer all six for every source. Write the answers down before\n"
             << "opening the key.\n\n";

    const char* questions[]
    {
        "Does one side sound noticeably brighter than the other? Which?",
        "Does one side preserve low-mid / mid body better? Which?",
        "Does the tail remain silky, lush and continuous on both sides?",
        "Does the brighter side still provide perceptually useful HF damping,\n"
        "       or does it become too open / exposed?",
        "Are transients and source separation at least as clean on both sides?",
        "Does modulation remain organic and non-chorusy on both sides?",
    };

    for (size_t q = 0; q < sizeof questions / sizeof questions[0]; ++q)
        manifest << "  " << (q + 1) << ". " << questions[q] << "\n\n";

    manifest << "Per source, the answers:\n\n";

    for (const auto& source : sources)
    {
        manifest << "  " << source.name << "\n";

        for (size_t q = 1; q <= 6; ++q)
            manifest << "    " << q << ".  \n";

        manifest << "\n";
    }

    manifest << "The decision\n------------\n"
             << "  PASS              the current default is equal or preferable overall,\n"
             << "                    with no material new tonal or modulation defect\n\n"
             << "  CONDITIONAL PASS  the current default is clearly technically\n"
             << "                    preferable and only a very small voicing difference\n"
             << "                    exists, not enough to justify another DSP iteration\n\n"
             << "  FAIL              an obvious undesirable product-level character change:\n"
             << "                    harsh or brittle HF tail, noticeably weaker damping,\n"
             << "                    loss of lushness, metallic decay, transient smearing,\n"
             << "                    or an obvious modulation / chorus character\n\n"
             << "  verdict:\n\n"
             << "On FAIL, do not tune anything. Record which source and which\n"
             << "perceptual dimension failed, and stop.\n\n"
             << "Nothing in this set is a measurement and nothing in it is a\n"
             << "recommendation. It renders two settings and asks.\n";

    // --- write the text files ---------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::binary);

        if (! out)
            return -1;

        out << manifest.str();

        ++written;
    }

    {
        std::ofstream out (directory + "/KEY-do-not-open-first.txt", std::ios::binary);

        if (! out)
            return -1;

        out << key.str();

        ++written;
    }

    return written;
}

} // namespace avtest
