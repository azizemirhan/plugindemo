#include "Phase8Listening.h"

#include "DampingListening.h"
#include "GainStaging.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include "DSP/CutFilter.h"
#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

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
    //  Everything held fixed, at the shipping defaults
    // -----------------------------------------------------------------------
    /*
        These are the product's own defaults, read off ParameterLayout.cpp:
        Decay 2.5 s, Size 60 %, Early Diffusion 70 %, Mod Rate 0.5 Hz, Mod
        Depth 30 %, Low Damp 20 %, Low Damp Freq 200 Hz, High Damp 45 %, High
        Damp Freq 10 kHz, Width 100 %, Mix 30 %.

        They are duplicated here rather than read from the layout, because the
        test target does not link JUCE. That duplication is the test
        infrastructure debt recorded in Docs/Phase7-TestInfrastructureDebt.md,
        and this file is one more site the deferred cleanup will migrate to a
        single checked contract. It is not fixed now because Stage 3 is a
        listening stage and touching the shared defaults would change files
        earlier listening sets were made from.
    */
    constexpr double kDecaySeconds    = 2.5;
    constexpr double kSizeControl     = 0.60;
    constexpr double kEarlyDiffusion  = 0.70;
    constexpr double kModDepthControl = 0.30;
    constexpr double kModRateHz       = 0.5;
    constexpr double kLowDamp         = 0.20;
    constexpr double kLowDampHz       = 200.0;
    constexpr double kHighDamp        = 0.45;
    constexpr double kHighDampHz      = 10000.0;
    constexpr double kWidth           = 1.0;
    constexpr double kShippingMix     = 0.30;

    /** One complete Phase-8 operating point. */
    struct Point
    {
        double preDelayMs = 0.0;
        double lowCutHz   = lowCutOffHz;
        double highCutHz  = highCutOffHz;
        double highDamp   = kHighDamp;
    };

    // -----------------------------------------------------------------------
    //  The production chain, offline
    // -----------------------------------------------------------------------
    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate, double highDamp)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (kDecaySeconds), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (kSizeControl));

        // Damping after Size and Decay: the shelf gains are derived from the
        // loop delays and the RT60 those two produce. Same order as
        // PluginProcessor, and the same reason.
        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (highDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (kModDepthControl),
                                      mapping::modRateScaleFor (kModRateHz),
                                      true);

        // Settle the Size morph on silence, then clear, so a render holds the
        // tank AT its target size rather than a transition into it.
        for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        tank->reset();

        return tank;
    }

    /**
        processInternal, offline, in its own order.

            dry                                      untouched
            wet = input -> Low Cut -> High Cut -> Pre-Delay -> tank
                        -> preview trim -> width -> mix

        The cut filters and the pre-delay are the production classes, not
        reimplementations, so a render here and the same setting in a host are
        the same arithmetic. The dry path never passes through either, which
        is the structural property Stage 2 was built around.
    */
    [[nodiscard]] std::vector<StereoFrame> render (const std::vector<StereoFrame>& dry,
                                                   double sampleRate,
                                                   const Point& point,
                                                   double wetMix)
    {
        ScopedFtz ftz;

        auto tank = makeTank (sampleRate, point.highDamp);

        StereoCutFilters cut;
        cut.prepare (sampleRate);

        // settledAtEndpoint exactly as the processor computes it: from the
        // target, which here is static, and not from a smoothed value.
        cut.setLowCut  (point.lowCutHz,  point.lowCutHz  <= lowCutOffHz);
        cut.setHighCut (point.highCutHz, point.highCutHz >= highCutOffHz);

        PreDelay<double> preDelay;
        preDelay.prepare (sampleRate);
        preDelay.snapTo (point.preDelayMs);   // no crossfade at the start of a render

        const double trim = mapping::previewWetTrim (mapping::rt60For (kDecaySeconds),
                                                      mapping::sizeScaleFor (kSizeControl));

        // Room for the tail AND for the pre-delay that displaces it.
        const auto tail = static_cast<size_t> (sampleRate * (kDecaySeconds + 1.0)
                                               + point.preDelayMs * sampleRate / 1000.0);

        std::vector<StereoFrame> out (dry.size() + tail);

        // Block by block, so the cut filters and the pre-delay are driven the
        // way the processor drives them rather than one sample at a time.
        constexpr int blockSize = 128;

        std::vector<double> wetL (blockSize, 0.0), wetR (blockSize, 0.0);

        for (size_t base = 0; base < out.size(); base += blockSize)
        {
            const int count = static_cast<int> (std::min (static_cast<size_t> (blockSize),
                                                          out.size() - base));

            for (int i = 0; i < count; ++i)
            {
                const size_t n = base + static_cast<size_t> (i);

                wetL[static_cast<size_t> (i)] = n < dry.size() ? dry[n].l : 0.0;
                wetR[static_cast<size_t> (i)] = n < dry.size() ? dry[n].r : 0.0;
            }

            cut     .process (wetL.data(), wetR.data(), count);
            preDelay.process (wetL.data(), wetR.data(), count);

            for (int i = 0; i < count; ++i)
            {
                double l = wetL[static_cast<size_t> (i)];
                double r = wetR[static_cast<size_t> (i)];

                tank->processSample (l, r);

                l *= trim;
                r *= trim;

                const double mid  = (l + r) * 0.5;
                const double side = (l - r) * 0.5 * kWidth;

                l = mid + side;
                r = mid - side;

                const size_t n = base + static_cast<size_t> (i);

                const double dryL = n < dry.size() ? dry[n].l : 0.0;
                const double dryR = n < dry.size() ? dry[n].r : 0.0;

                out[n].l = dryL * (1.0 - wetMix) + l * wetMix;
                out[n].r = dryR * (1.0 - wetMix) + r * wetMix;
            }
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  The bass-heavy source
    // -----------------------------------------------------------------------
    /**
        A bass-heavy source, and why this set needs one.

        Low Cut runs 20 to 1000 Hz and this stage asks about 200 and 400 Hz.
        A filter at 200 Hz can only act on energy below 200 Hz, and the lowest
        content anywhere in the established material is the 220 Hz fundamental
        of `pad` and `mixed`. Handed only those, a listener would be asked
        whether a filter they cannot hear is musically useful.

        So: a sustained bass at 55 Hz with its first few harmonics, a kick-like
        thump on the beat, and enough mid content that removing everything
        below 400 Hz changes the reverb's weight rather than silencing it. No
        resonator and no filter sweep in the source - the question is what the
        TANK's low end sounds like when it is cut, and the source must not
        answer it.

        An addition, not part of the established material, and named as one in
        the manifest.
    */
    [[nodiscard]] std::vector<StereoFrame> makeBassHeavySource (double sampleRate, double seconds)
    {
        std::vector<StereoFrame> out (static_cast<size_t> (sampleRate * seconds));

        unsigned int seed = 20261114u;

        const auto next = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 8388608.0 - 1.0;
        };

        // A two-bar figure at 100 bpm: root, root, fifth, root.
        const double beat = 60.0 / 100.0;
        const double notes[] { 55.0, 55.0, 82.5, 55.0 };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / sampleRate;

            const int index = static_cast<int> (t / beat) % 4;
            const double since = std::fmod (t, beat);
            const double f0 = notes[index];

            // Plucked-bass envelope: fast attack, slow decay, so the tail of
            // one note overlaps the next - which is where a low cut in the
            // reverb becomes audible rather than theoretical.
            const double envelope = (1.0 - std::exp (-since / 0.004))
                                    * std::exp (-since / 0.45);

            const double bass = envelope
                              * (0.55 * std::sin (2.0 * damping::pi * f0 * t)
                                 + 0.22 * std::sin (2.0 * damping::pi * 2.0 * f0 * t)
                                 + 0.09 * std::sin (2.0 * damping::pi * 3.0 * f0 * t)
                                 + 0.04 * std::sin (2.0 * damping::pi * 5.0 * f0 * t));

            // A kick-like thump on every beat: a short low sine burst.
            const double thump = std::exp (-since / 0.055)
                                 * 0.35 * std::sin (2.0 * damping::pi * 48.0 * since);

            // Mid content, so the file is a bass part in a track and not a
            // test tone. Quiet, and well above the cut frequencies, so it
            // stays in the reverb at every Low Cut setting and gives the ear
            // something unchanged to judge the low end against.
            const double midSince = std::fmod (t, beat * 0.5);
            const double mid = std::exp (-midSince / 0.040) * 0.10
                               * (std::sin (2.0 * damping::pi * 880.0 * t)
                                  + 0.6 * next());

            const double x = 0.42 * (bass + thump + mid);

            // Slightly decorrelated, as the other sources are: the low end is
            // deliberately near-mono, which is how bass is actually recorded,
            // and the mid carries the width.
            out[n].l = x;
            out[n].r = 0.42 * (bass + thump + mid * 0.7
                               + 0.04 * std::exp (-midSince / 0.040)
                                 * std::sin (2.0 * damping::pi * 880.0 * t + 0.8));
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  Measurement, for the manifest and for the case-4 match
    // -----------------------------------------------------------------------
    /**
        Energy in one band over one time window, as a level in dB.

        This is case 4's measure, and the first version of this file got it
        wrong in a way worth recording. That version used the centre of
        gravity in time of the high-band energy, and it separated the two
        controls by 46 ms out of 3 s - 1.5 %, which is not a discriminator.
        The reason is that the source runs for six seconds and the tail for
        three and a half, so a centroid over the whole render is dominated by
        the source's own span and barely sees the decay at all.

        What distinguishes the two controls is not when the high band arrives
        but how it leaves, and that is only visible in the FREE TAIL, after
        the source has stopped. So the band level is measured over an explicit
        window:

            early tail   how dark the tail STARTS
            late  tail   how dark it has BECOME

        A high cut lowers the first and leaves the fall between them alone. A
        high damp leaves the first much closer to the default and steepens the
        fall. Those two numbers are the distinction, stated before anyone
        listens.
    */
    [[nodiscard]] double bandLevelDb (const std::vector<StereoFrame>& frames,
                                      double sampleRate,
                                      double fromSeconds, double toSeconds,
                                      double splitHz = 6000.0,
                                      bool above = true)
    {
        const double a = std::exp (-2.0 * damping::pi * splitHz / sampleRate);

        const auto from = static_cast<size_t> (fromSeconds * sampleRate);
        const auto to   = std::min (frames.size(),
                                    static_cast<size_t> (toSeconds * sampleRate));

        double stateL = 0.0, stateR = 0.0;
        double energy = 0.0;
        size_t counted = 0;

        // The filter runs from the start so that its state is settled by the
        // time the window opens; only the window contributes to the total.
        for (size_t n = 0; n < to; ++n)
        {
            stateL = a * stateL + (1.0 - a) * frames[n].l;
            stateR = a * stateR + (1.0 - a) * frames[n].r;

            if (n < from)
                continue;

            const double bandL = above ? frames[n].l - stateL : stateL;
            const double bandR = above ? frames[n].r - stateR : stateR;

            energy += bandL * bandL + bandR * bandR;
            ++counted;
        }

        if (counted == 0 || energy <= 0.0)
            return -200.0;

        return 10.0 * std::log10 (energy / static_cast<double> (counted));
    }

    /** Worst sample difference between two renders of the same source. */
    [[nodiscard]] double worstSampleDifference (const std::vector<StereoFrame>& a,
                                                const std::vector<StereoFrame>& b)
    {
        double worst = 0.0;

        for (size_t n = 0; n < std::min (a.size(), b.size()); ++n)
        {
            worst = std::max (worst, std::abs (a[n].l - b[n].l));
            worst = std::max (worst, std::abs (a[n].r - b[n].r));
        }

        return worst;
    }

    /*
        The line between "this pair can carry an audible difference" and "it
        cannot", carried over from PHASE 7's listening set unchanged.

        0.25 dB is roughly the smallest broadband level or tilt difference that
        survives an A/B on program material. It is a judgement about hearing,
        not a measurement, and it is written here so it can be argued with
        rather than discovered in the code.
    */
    constexpr double kAudibleDb = 0.25;

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
}

int exportPhase8ListeningSet (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    // --- the four sources -------------------------------------------------

    const auto fu2 = makeFu2Sources (sampleRate);

    std::vector<NamedSource> sources;

    for (const auto& s : fu2)
        if (s.name == "snare")
            sources.push_back (s);

    if (sources.size() != 1)
        return -1;   // the established snare went missing

    sources.push_back ({ "vocal", makeVocalReference (sampleRate, 4.0) });
    sources.push_back ({ "hats",  makeBrightPercussion (sampleRate, 6.0) });
    sources.push_back ({ "bass",  makeBassHeavySource (sampleRate, 6.0) });

    const auto sourceNamed = [&sources] (const char* name) -> const std::vector<StereoFrame>&
    {
        for (const auto& s : sources)
            if (s.name == name)
                return s.frames;

        return sources.front().frames;
    };

    std::ostringstream manifest;
    std::ostringstream key;

    int written = 0;

    manifest << "AuroraVerb - PHASE 8 Stage 3 listening set\n"
             << "==========================================\n\n"
             << "Pre-Delay, Low Cut and High Cut. Four cases, one question each.\n\n"
             << "This is NOT a preference ranking and a PASS does not require every\n"
             << "setting to sound preferable. It asks whether the three controls are\n"
             << "musically useful, clean and predictable.\n\n"
             << "Everything the engineering work already established, so that\n"
             << "listening is not asked to rediscover it:\n"
             << "  - the neutral path (Pre-Delay 0, Low Cut 20 Hz, High Cut 20 kHz)\n"
             << "    is bit-identical to PHASE 7: 0 of 48000 samples differ\n"
             << "  - pre-delay timing is exact at 44.1 / 48 / 96 kHz, 0 samples error\n"
             << "  - 0 ms is bit-exact, with no one-sample wet-path delay\n"
             << "  - endpoint automation adds no step the signal does not already have\n"
             << "  - nothing from before a bypass survives it (bit-zero)\n"
             << "  - the cuts reach no tank control value, and the signal entering the\n"
             << "    tank replays bit-identically into a tank that never saw them\n"
             << "  - focused regression 990/992, the two failures pre-existing PHASE 7\n\n"
             << "Held fixed in EVERY render, at the product's own defaults:\n"
             << "  Decay            " << fixed (kDecaySeconds, 1) << " s\n"
             << "  Size             " << fixed (kSizeControl * 100.0, 0) << " %\n"
             << "  Early Diffusion  " << fixed (kEarlyDiffusion * 100.0, 0) << " %\n"
             << "  Low Damp         " << fixed (kLowDamp * 100.0, 0) << " %   at "
             << fixed (kLowDampHz, 0) << " Hz\n"
             << "  High Damp        " << fixed (kHighDamp * 100.0, 0) << " %   at "
             << fixed (kHighDampHz, 0) << " Hz   (case 4 varies this, and only this)\n"
             << "  Width            " << fixed (kWidth * 100.0, 0) << " %\n"
             << "  Mod Depth        " << fixed (kModDepthControl * 100.0, 0) << " %\n"
             << "  Mod Rate         " << fixed (kModRateHz, 2) << " Hz\n"
             << "  input and output gain unity\n\n"
             << "Sample rate " << fixed (sampleRate, 0) << " Hz, 32-bit float stereo.\n\n"
             << "LEVEL: nothing in cases 1 to 3 is normalised. Low Cut and High Cut\n"
             << "legitimately change how much energy reaches the tank, and matching\n"
             << "that away would hide the effect instead of isolating it. Every render\n"
             << "uses the same source gain and the same downstream gain, so the level\n"
             << "differences you hear are real. Case 4 is the one exception and the\n"
             << "reason is stated there.\n\n"
             << "SOURCES\n"
             << "  snare  percussive          established FU-2 material\n"
             << "  vocal  vocal-like          established, the export set's reference\n"
             << "  hats   bright percussion   added in PHASE 7; noise-based, no resonator\n"
             << "  bass   bass-heavy          ADDED FOR THIS SET\n\n"
             << "  `bass` is new. Low Cut at 200 and 400 Hz can only act on energy\n"
             << "  below those frequencies, and the lowest content anywhere in the\n"
             << "  established material is a 220 Hz fundamental - so without it a\n"
             << "  listener would be asked whether a filter they cannot hear is\n"
             << "  useful. It is a 55 Hz plucked bass figure with a kick-like thump\n"
             << "  and quiet mid content, with no resonator and no filter in it.\n\n"
             << "  Each source is also written dry, as *-dry.wav, for reference.\n\n";

    // --- the dry references -----------------------------------------------

    for (const auto& s : sources)
    {
        if (! writeStereoWav (directory + "/" + s.name + "-dry.wav", s.frames, sampleRate))
            return -1;

        ++written;
    }

    // =======================================================================
    //  CASE 1 - Pre-Delay
    // =======================================================================

    manifest << "CASE 1 - PRE-DELAY\n"
             << "------------------\n"
             << "Low Cut OFF (20 Hz), High Cut OFF (20 kHz), everything else as above.\n"
             << "Only Pre-Delay moves.\n\n"
             << "  case1-<source>-<mix>-p0.wav    Pre-Delay  0 ms\n"
             << "  case1-<source>-<mix>-p20.wav   Pre-Delay 20 ms\n"
             << "  case1-<source>-<mix>-p80.wav   Pre-Delay 80 ms\n\n";

    struct Variant { const char* tag; Point point; };

    const Variant case1[]
    {
        { "p0",  Point { 0.0,  lowCutOffHz, highCutOffHz } },
        { "p20", Point { 20.0, lowCutOffHz, highCutOffHz } },
        { "p80", Point { 80.0, lowCutOffHz, highCutOffHz } },
    };

    struct Target { const char* source; const char* mix; double wet; };

    const Target case1Targets[]
    {
        { "snare", "mix30", kShippingMix },
        { "snare", "wet",   1.0 },
        { "vocal", "mix30", kShippingMix },
    };

    manifest << "  " << pad ("file", 30) << pad ("peak", 11) << "wet onset vs 0 ms\n";

    for (const auto& t : case1Targets)
    {
        double onsetAtZero = 0.0;

        for (const auto& v : case1)
        {
            const auto frames = render (sourceNamed (t.source), sampleRate, v.point, t.wet);

            const std::string name = std::string ("case1-") + t.source + "-" + t.mix
                                   + "-" + v.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            // Where the wet energy actually starts, so the manifest can state
            // the separation rather than asking a listener to confirm it. The
            // onset is taken from a wet-only render of the same point, because
            // at Mix 30 % the dry signal starts at sample 0 regardless.
            const auto wetOnly = render (sourceNamed (t.source), sampleRate, v.point, 1.0);

            double peak = 0.0;

            for (const auto& f : wetOnly)
                peak = std::max (peak, std::max (std::abs (f.l), std::abs (f.r)));

            size_t onset = 0;

            for (size_t n = 0; n < wetOnly.size(); ++n)
                if (std::max (std::abs (wetOnly[n].l), std::abs (wetOnly[n].r)) > peak * 0.02)
                {
                    onset = n;
                    break;
                }

            const double onsetMs = 1000.0 * static_cast<double> (onset) / sampleRate;

            if (std::string (v.tag) == "p0")
                onsetAtZero = onsetMs;

            double filePeak = 0.0;

            for (const auto& f : frames)
                filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

            manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                     << fixed (onsetMs - onsetAtZero, 2) << " ms\n";
        }
    }

    manifest << "\n  The last column is measured on a wet-only render at the same\n"
             << "  setting, as the time at which the wet signal first reaches 2 % of\n"
             << "  its peak, relative to the 0 ms render. It should read about 0, 20\n"
             << "  and 80. It is here so that if 20 ms sounds like nothing, the\n"
             << "  question is about perception and not about whether the control\n"
             << "  did anything.\n\n"
             << "  WHAT TO LISTEN FOR\n"
             << "    - 0 ms: reverb integrated with the source, no separation\n"
             << "    - 20 ms: the transient stands clear of the reverb, and it should\n"
             << "      sound like space, not like an effect\n"
             << "    - 80 ms: clearly longer, and still clean - a distinct onset, not\n"
             << "      a slap or an echo artefact\n"
             << "    - no pitch glide at any setting. The taps are integer samples and\n"
             << "      do not move during a render, so there is nothing to glide; if\n"
             << "      you hear one, that is a finding.\n"
             << "    - the DRY transient must arrive at the same instant in all three.\n"
             << "      Pre-Delay is on the wet path only. A shift in the dry hit is a\n"
             << "      defect.\n\n";

    // =======================================================================
    //  CASE 2 - Low Cut
    // =======================================================================

    manifest << "CASE 2 - LOW CUT\n"
             << "----------------\n"
             << "Pre-Delay 0 ms, High Cut OFF. Only Low Cut moves.\n\n"
             << "  case2-<source>-<mix>-off.wav   Low Cut  20 Hz  (OFF, exact bypass)\n"
             << "  case2-<source>-<mix>-200.wav   Low Cut 200 Hz\n"
             << "  case2-<source>-<mix>-400.wav   Low Cut 400 Hz\n\n";

    const Variant case2[]
    {
        { "off", Point { 0.0, lowCutOffHz, highCutOffHz } },
        { "200", Point { 0.0, 200.0,       highCutOffHz } },
        { "400", Point { 0.0, 400.0,       highCutOffHz } },
    };

    /*
        `vocal` is heard WET here rather than at Mix 30 %.

        The first render of this set put it at Mix 30 % and the pair came out
        0.04 dB apart - a null trial. At 30 % wet the output is 70 % dry
        signal, and the dry path never passes through the cut, so a change
        confined to the reverb's low end arrives at the output scaled by 0.3
        and buried under something unchanged. `bass` is kept at Mix 30 %
        because that question - does anything get worse in normal use - is
        worth asking; the question of whether the control works is asked on
        the wet renders, where it can be answered.
    */
    const Target case2Targets[]
    {
        { "bass",  "mix30", kShippingMix },
        { "bass",  "wet",   1.0 },
        { "vocal", "wet",   1.0 },
    };

    manifest << "  " << pad ("file", 30) << pad ("peak", 11)
             << pad ("loudness", 13) << pad ("vs OFF", 10)
             << pad ("LF level", 12) << pad ("vs OFF", 10)
             << "worst sample\n";

    int case2Nulls = 0, case2Pairs = 0;

    for (const auto& t : case2Targets)
    {
        double loudnessAtOff = 0.0, shareAtOff = 0.0;
        std::vector<StereoFrame> reference;

        for (const auto& v : case2)
        {
            const auto frames = render (sourceNamed (t.source), sampleRate, v.point, t.wet);

            const std::string name = std::string ("case2-") + t.source + "-" + t.mix
                                   + "-" + v.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            const double loudness = integratedLoudnessLufs (frames, sampleRate);

            // Energy below 200 Hz as a share of the total: what a low cut at
            // 200 or 400 Hz is for. Not a tilt ratio, which saturates.
            const double share = bandLevelDb (frames, sampleRate, 0.0, 1.0e9, 200.0, false);

            const bool isOff = std::string (v.tag) == "off";

            if (isOff)
            {
                loudnessAtOff = loudness;
                shareAtOff = share;
                reference = frames;
            }

            double filePeak = 0.0;

            for (const auto& f : frames)
                filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

            const double worst = isOff ? 0.0 : worstSampleDifference (reference, frames);

            manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                     << pad (fixed (loudness, 2) + " LUFS", 13)
                     << pad (fixed (loudness - loudnessAtOff, 2) + " dB", 10)
                     << pad (fixed (share, 2) + " dB", 12)
                     << pad (fixed (share - shareAtOff, 2) + " dB", 10);

            if (isOff)
            {
                manifest << "-   (reference)\n";
            }
            else
            {
                ++case2Pairs;

                const bool canReveal =
                    std::abs (loudness - loudnessAtOff) >= kAudibleDb
                    || std::abs (share - shareAtOff) >= kAudibleDb;

                if (! canReveal)
                    ++case2Nulls;

                manifest << fixed (worst, 6)
                         << (canReveal ? "" : "   near-identical: a null trial") << "\n";
            }
        }
    }

    manifest << "\n  " << case2Nulls << " of " << case2Pairs
             << " comparisons are near-identical.\n\n"
             << "  The level drop is the effect, not a flaw in the set. It is NOT\n"
             << "  matched away: a low cut that removes low-frequency energy is\n"
             << "  quieter, and hiding that would hide half of what the control does.\n\n"
             << "  Do not strain to hear a difference in a pair flagged as a null\n"
             << "  trial. If one sounds different, that is a reading of expectation.\n\n"
             << "  WHAT TO LISTEN FOR\n"
             << "    - OFF: the reverb keeps the full low end of the source\n"
             << "    - 200 Hz: the low end of the TAIL cleans up while the source's\n"
             << "      own low end is untouched - the dry path never passes through\n"
             << "      the filter\n"
             << "    - 400 Hz: stronger, and it should still be usable rather than\n"
             << "      thin or hollow\n"
             << "    - no hollow resonance and no ringing. The filter is a single\n"
             << "      first-order pole with no resonance to ring, so a ring would be\n"
             << "      a finding.\n"
             << "    - no strange transient at the start of any file\n\n";

    // =======================================================================
    //  CASE 3 - High Cut
    // =======================================================================

    manifest << "CASE 3 - HIGH CUT\n"
             << "-----------------\n"
             << "Pre-Delay 0 ms, Low Cut OFF. Only High Cut moves.\n\n"
             << "  case3-<source>-<mix>-off.wav   High Cut 20 kHz  (OFF, exact bypass)\n"
             << "  case3-<source>-<mix>-10k.wav   High Cut 10 kHz\n"
             << "  case3-<source>-<mix>-6k.wav    High Cut  6 kHz\n\n";

    const Variant case3[]
    {
        { "off", Point { 0.0, lowCutOffHz, highCutOffHz } },
        { "10k", Point { 0.0, lowCutOffHz, 10000.0 } },
        { "6k",  Point { 0.0, lowCutOffHz,  6000.0 } },
    };

    /*
        The second source here is `snare`, not `vocal`, and the measurement is
        why.

        The stage suggested bright percussion or a vocal. Rendered, the vocal
        turned out unable to carry the comparison at all: it holds 12 dB less
        energy above 3 kHz than below, so a 10 kHz High Cut moved it by 0.05 dB
        and a 6 kHz cut by 0.15 dB - both null trials by the 0.25 dB line used
        throughout this project. Handing a listener a pair that is physically
        near-identical and asking whether the darkening is gentle manufactures
        a false positive. `snare` is broadband noise hits and carries the
        change; the vocal's inability to is recorded here rather than silently
        worked around.

        The split is at 3 kHz rather than 6 kHz: a 6 kHz split and a 6 kHz High
        Cut coincide, so the measure would sit exactly on the corner it is
        trying to see.
    */
    const Target case3Targets[]
    {
        { "hats",  "mix30", kShippingMix },
        { "hats",  "wet",   1.0 },
        { "snare", "wet",   1.0 },
    };

    manifest << "  " << pad ("file", 30) << pad ("peak", 11)
             << pad ("loudness", 13) << pad ("vs OFF", 10)
             << pad ("HF level", 12) << pad ("vs OFF", 10)
             << "worst sample\n";

    int case3Nulls = 0, case3Pairs = 0;

    for (const auto& t : case3Targets)
    {
        double loudnessAtOff = 0.0, shareAtOff = 0.0;
        std::vector<StereoFrame> reference;

        for (const auto& v : case3)
        {
            const auto frames = render (sourceNamed (t.source), sampleRate, v.point, t.wet);

            const std::string name = std::string ("case3-") + t.source + "-" + t.mix
                                   + "-" + v.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            const double loudness = integratedLoudnessLufs (frames, sampleRate);
            const double share = bandLevelDb (frames, sampleRate, 0.0, 1.0e9, 3000.0, true);

            const bool isOff = std::string (v.tag) == "off";

            if (isOff)
            {
                loudnessAtOff = loudness;
                shareAtOff = share;
                reference = frames;
            }

            double filePeak = 0.0;

            for (const auto& f : frames)
                filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

            const double worst = isOff ? 0.0 : worstSampleDifference (reference, frames);

            manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                     << pad (fixed (loudness, 2) + " LUFS", 13)
                     << pad (fixed (loudness - loudnessAtOff, 2) + " dB", 10)
                     << pad (fixed (share, 2) + " dB", 12)
                     << pad (fixed (share - shareAtOff, 2) + " dB", 10);

            if (isOff)
            {
                manifest << "-   (reference)\n";
            }
            else
            {
                ++case3Pairs;

                const bool canReveal =
                    std::abs (loudness - loudnessAtOff) >= kAudibleDb
                    || std::abs (share - shareAtOff) >= kAudibleDb;

                if (! canReveal)
                    ++case3Nulls;

                manifest << fixed (worst, 6)
                         << (canReveal ? "" : "   near-identical: a null trial") << "\n";
            }
        }
    }

    manifest << "\n  " << case3Nulls << " of " << case3Pairs
             << " comparisons are near-identical.\n\n"
             << "  HF share is the energy above 3 kHz as a share of the total. 3 kHz\n"
             << "  rather than 6 kHz because a 6 kHz split and a 6 kHz High Cut would\n"
             << "  coincide, putting the measure on the corner it is trying to see.\n"
             << "  It is not a loudness model; only differences between renders\n"
             << "  measured identically are used.\n\n"
             << "  Do not strain to hear a difference in a pair flagged as a null\n"
             << "  trial. If one sounds different, that is a reading of expectation.\n\n"
             << "  WHAT TO LISTEN FOR\n"
             << "    - OFF: the tail stays open and present at the top\n"
             << "    - 10 kHz: gentle darkening, the kind that sits a reverb behind a\n"
             << "      source rather than removing air from it\n"
             << "    - 6 kHz: clearly darker, and it should be a usable darker rather\n"
             << "      than a muffled one\n"
             << "    - no brittle edge, no resonance, no unnatural HF collapse. A\n"
             << "      first-order pole cannot resonate, so a ring is a finding.\n\n";

    // =======================================================================
    //  CASE 4 - High Cut against High Damp
    // =======================================================================

    manifest << "CASE 4 - HIGH CUT AGAINST HIGH DAMP   (blind)\n"
             << "---------------------------------------------\n"
             << "The conceptually important one. Both controls make a reverb darker.\n"
             << "If they are perceptually redundant then the product has one control\n"
             << "too many, and this is the case that would show it.\n\n"
             << "  A   High Cut OFF,     High Damp 45 %, High Damp Freq 10 kHz\n"
             << "      the shipping default - the reference\n"
             << "  B   High Cut 8 kHz,   High Damp 45 %, High Damp Freq 10 kHz\n"
             << "      bandwidth removed at the INPUT, outside the loop\n"
             << "  C   High Cut OFF,     High Damp stronger, High Damp Freq 10 kHz\n"
             << "      decay shortened in the high band, INSIDE the loop\n\n";

    /*
        Choosing C's High Damp by measurement rather than by guess, and the
        first attempt getting it wrong.

        The comparison is only about the ROLE of the two controls if they end
        up comparably dark. Pick C by hand and the listener is partly judging
        which control was turned up further.

        THE FIRST ATTEMPT matched on HF/LF balance over the whole render, and
        it failed: the search ran to the top of the parameter range and High
        Damp 95 % was still 0.64 dB short of an 8 kHz High Cut, with the trend
        still improving. That is not a search that needs a wider grid. It is
        the finding the case exists to produce - High Damp cannot reproduce
        what a High Cut does, however hard it is driven, because a cut removes
        the high band from the excitation while damping only shortens how long
        it lasts once it is in the tank. The high band still ARRIVES at full
        level.

        So the match moved to the quantity that can be matched and that is the
        fair one: the high-band level LATE IN THE FREE TAIL, after the source
        has stopped. Both controls can make the tail end up equally dark. How
        they get there is the distinction, and it is then visible as the
        early-tail level, which the match deliberately leaves alone.
    */
    const auto& case4Source = sourceNamed ("hats");

    // The `hats` source is 6 s long, so the free tail starts there.
    constexpr double kSourceSeconds = 6.0;
    constexpr double kEarlyFrom = kSourceSeconds + 0.02;
    constexpr double kEarlyTo   = kSourceSeconds + 0.30;
    constexpr double kLateFrom  = kSourceSeconds + 0.60;
    constexpr double kLateTo    = kSourceSeconds + 1.20;

    const Point case4A { 0.0, lowCutOffHz, highCutOffHz, kHighDamp };
    const Point case4B { 0.0, lowCutOffHz, 8000.0,       kHighDamp };

    const auto renderA = render (case4Source, sampleRate, case4A, 1.0);
    const auto renderB = render (case4Source, sampleRate, case4B, 1.0);

    const auto earlyOf = [&] (const std::vector<StereoFrame>& f)
    {
        return bandLevelDb (f, sampleRate, kEarlyFrom, kEarlyTo);
    };

    const auto lateOf = [&] (const std::vector<StereoFrame>& f)
    {
        return bandLevelDb (f, sampleRate, kLateFrom, kLateTo);
    };

    const double lateB = lateOf (renderB);

    manifest << "  C's High Damp is chosen by measurement, not by guess: the\n"
             << "  comparison is about the two controls' ROLE, and that only works if\n"
             << "  they end up comparably dark. Otherwise part of the answer is which\n"
             << "  one was turned up further.\n\n"
             << "  The match is on the high-band level LATE in the free tail, 0.6 to\n"
             << "  1.2 s after the source stops - still part of the audible tail at a\n"
             << "  2.5 s Decay. Two earlier versions of this measurement were wrong\n"
             << "  and both are worth recording.\n\n"
             << "  The FIRST matched on whole-render HF/LF balance, and the search ran\n"
             << "  to the top of the parameter range still 0.64 dB short. That was not\n"
             << "  a grid too narrow - it is the point of the case: High Damp cannot\n"
             << "  reproduce a High Cut however hard it is driven, because a cut keeps\n"
             << "  the high band out of the tank while damping only shortens how long\n"
             << "  it survives once it is in. It still arrives at full level.\n\n"
             << "  The SECOND put the late window at 1.5 to 2.5 s after the source,\n"
             << "  where the high band sits around -95 dB. Matching there is matching\n"
             << "  on inaudible residue, and it chose a High Damp barely above the\n"
             << "  default. The window now sits inside the part of the tail a listener\n"
             << "  actually hears.\n\n"
             << "  " << pad ("High Damp", 12) << pad ("late tail HF", 15) << "vs B\n";

    double bestHighDamp = 0.0;
    double bestError = 1.0e30;

    for (int step = 0; step <= 10; ++step)
    {
        const double damp = 0.50 + 0.05 * step;

        const Point candidate { 0.0, lowCutOffHz, highCutOffHz, damp };

        const auto frames = render (case4Source, sampleRate, candidate, 1.0);
        const double late = lateOf (frames);
        const double error = std::abs (late - lateB);

        manifest << "  " << pad (fixed (damp * 100.0, 0) + " %", 12)
                 << pad (fixed (late, 2) + " dB", 15)
                 << fixed (late - lateB, 2) << " dB\n";

        if (error < bestError)
        {
            bestError = error;
            bestHighDamp = damp;
        }
    }

    manifest << "\n  chosen: High Damp " << fixed (bestHighDamp * 100.0, 0)
             << " %, within " << fixed (bestError, 2)
             << " dB of B's late-tail high-band level\n\n";

    const Point case4C { 0.0, lowCutOffHz, highCutOffHz, bestHighDamp };

    const auto renderC = render (case4Source, sampleRate, case4C, 1.0);

    // What the two controls are expected to differ in, stated up front.
    manifest << "  What the measurement expects, so that listening confirms or\n"
             << "  contradicts something specific rather than hunting blind.\n\n"
             << "  High-band level (above 6 kHz) in the free tail, after the source\n"
             << "  has stopped:\n\n"
             << "  " << pad ("version", 10) << pad ("early tail", 14)
             << pad ("late tail", 14) << "fall\n"
             << "  " << pad ("A", 10)
             << pad (fixed (earlyOf (renderA), 2) + " dB", 14)
             << pad (fixed (lateOf  (renderA), 2) + " dB", 14)
             << fixed (lateOf (renderA) - earlyOf (renderA), 2) << " dB\n"
             << "  " << pad ("B", 10)
             << pad (fixed (earlyOf (renderB), 2) + " dB", 14)
             << pad (fixed (lateOf  (renderB), 2) + " dB", 14)
             << fixed (lateOf (renderB) - earlyOf (renderB), 2) << " dB\n"
             << "  " << pad ("C", 10)
             << pad (fixed (earlyOf (renderC), 2) + " dB", 14)
             << pad (fixed (lateOf  (renderC), 2) + " dB", 14)
             << fixed (lateOf (renderC) - earlyOf (renderC), 2) << " dB\n\n"
             << "  B and C are matched on the LATE column by construction, so the\n"
             << "  distinction lives in the other two. A high CUT lowers the tail's\n"
             << "  high band from its first instant and leaves the fall alone: B\n"
             << "  should start dark and fall like A. A high DAMP leaves the first\n"
             << "  instant much closer to the default and steepens the fall: C should\n"
             << "  start near A and fall further. That is the whole claim, and it is\n"
             << "  what a listener should be able to hear as uniformly darker against\n"
             << "  brighter-then-darkening.\n\n"
             << "  One number needs a caveat before it is over-read. B's fall is not\n"
             << "  exactly A's, and it is not supposed to be: the measured band is\n"
             << "  everything above 6 kHz while the cut sits at 8 kHz, so the cut\n"
             << "  reweights the band towards 6-8 kHz, which decays a little\n"
             << "  differently under a 10 kHz damping corner. That is a property of\n"
             << "  measuring a wide band with one number, not evidence that the cut\n"
             << "  reached the decay law. It cannot: STAGE 2.5 proved the cut\n"
             << "  parameters enter no tank control value, on the complete production\n"
             << "  tank, and that the signal entering the tank replays bit-identically\n"
             << "  into a tank that never saw them.\n\n";

    /*
        Blinding, for this case only.

        The other three cases are labelled, because the question there is
        functional - did the control do its job - and knowing which file is
        which does not bias that. Case 4 asks whether two things sound
        different in kind, which is exactly the question expectation answers
        on a listener's behalf. So the three versions are written as 1, 2 and
        3 with a fixed permutation that lives in the key.

        The permutation is C, A, B. Written down rather than generated, so the
        set is reproducible: the same build produces the same bytes, which is
        the property every listening set in this project has.
    */
    struct Blind { const char* file; const char* version; const std::vector<StereoFrame>* frames; };

    const Blind blind[]
    {
        { "1", "C", &renderC },
        { "2", "A", &renderA },
        { "3", "B", &renderB },
    };

    key << "AuroraVerb - PHASE 8 Stage 3 listening set: KEY\n"
        << "===============================================\n\n"
        << "Only case 4 is blind. Do not open this until the case-4 answers are\n"
        << "written down.\n\n"
        << "  A = High Cut OFF,   High Damp " << fixed (kHighDamp * 100.0, 0)
        << " %   (the shipping default)\n"
        << "  B = High Cut 8 kHz, High Damp " << fixed (kHighDamp * 100.0, 0) << " %\n"
        << "  C = High Cut OFF,   High Damp " << fixed (bestHighDamp * 100.0, 0)
        << " %   (chosen so its late-tail high-band level\n"
        << "                                        lands within "
        << fixed (bestError, 2) << " dB of B's)\n\n"
        << "  case4-hats-wet-1.wav = C\n"
        << "  case4-hats-wet-2.wav = A\n"
        << "  case4-hats-wet-3.wav = B\n\n";

    manifest << "  Files, wet only - the tail is what is being judged and Mix 30 %\n"
             << "  would bury it under the dry signal:\n\n"
             << "  " << pad ("file", 30) << pad ("peak", 11) << pad ("loudness", 13)
             << "matched gain\n";

    // Loudness-match the three against the quietest, so that "darker" cannot
    // be heard as "quieter". The quietest is left exactly as rendered and the
    // others are brought DOWN to meet it, so the match never adds gain and
    // cannot push a file over full scale.
    double quietest = 1.0e30;

    for (const auto& b : blind)
        quietest = std::min (quietest, integratedLoudnessLufs (*b.frames, sampleRate));

    for (const auto& b : blind)
    {
        const std::string name = std::string ("case4-hats-wet-") + b.file + ".wav";

        if (! writeStereoWav (directory + "/" + name, *b.frames, sampleRate))
            return -1;

        ++written;

        const double loudness = integratedLoudnessLufs (*b.frames, sampleRate);
        const double gainDb = quietest - loudness;
        const double gain = std::pow (10.0, gainDb / 20.0);

        auto matched = *b.frames;

        for (auto& f : matched) { f.l *= gain; f.r *= gain; }

        const std::string matchedName = std::string ("case4-hats-wet-") + b.file
                                      + "-matched.wav";

        if (! writeStereoWav (directory + "/" + matchedName, matched, sampleRate))
            return -1;

        ++written;

        double filePeak = 0.0;

        for (const auto& f : *b.frames)
            filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

        manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                 << pad (fixed (loudness, 2) + " LUFS", 13)
                 << fixed (gainDb, 2) << " dB\n";

        key << "  " << pad (matchedName, 34) << "version " << b.version
            << ", gain " << fixed (gainDb, 2) << " dB\n";
    }

    manifest << "\n  LEVEL, and this is the one place in the set where matching is\n"
             << "  used. Case 4 asks whether two controls do different KINDS of\n"
             << "  thing, and a level difference is the fastest way to mistake\n"
             << "  \"quieter\" for \"different in kind\". So the three are written twice:\n"
             << "  as rendered, and loudness-matched to the quietest of them by gain\n"
             << "  only, never upward, with the amount in the column above. Judge the\n"
             << "  roles on the *-matched files. Cases 1 to 3 are NOT matched, for the\n"
             << "  opposite reason: there the level change is part of the effect.\n\n"
             << "  WHAT TO LISTEN FOR\n"
             << "    One of these three removes high frequencies from what goes INTO\n"
             << "    the reverb. Another shortens how long the high frequencies last\n"
             << "    once they are in it. The third is the default and does neither.\n\n"
             << "    They are not expected to sound radically different, and they are\n"
             << "    not expected to be equally preferable. The question is only\n"
             << "    whether they feel like two different things or like two copies of\n"
             << "    the same control.\n\n"
             << "    The giveaway, if you want one: a reverb whose high band is cut at\n"
             << "    the input is uniformly darker from the first instant of the tail\n"
             << "    to the last. A reverb whose high band is damped starts brighter\n"
             << "    and darkens as it decays.\n\n";

    // --- the questions ----------------------------------------------------

    manifest << "THE ANSWERS\n"
             << "-----------\n"
             << "Write these down. For case 4, write them down BEFORE opening\n"
             << "KEY-do-not-open-first.txt.\n\n"
             << "CASE 1 - Pre-Delay\n"
             << "  1. Does 0 ms sound integrated with the source?\n\n"
             << "  2. Does 20 ms create useful separation without sounding\n"
             << "     artificial?\n\n"
             << "  3. Is 80 ms clearly longer and still clean?\n\n"
             << "  4. Any echo-like glitch beyond the intended pre-delay?\n\n"
             << "  5. Any phase or transient corruption? Any pitch glide?\n\n"
             << "  6. Does the DRY transient arrive at the same instant in all three?\n\n"
             << "CASE 2 - Low Cut\n"
             << "  1. Does OFF remain full?\n\n"
             << "  2. Does 200 Hz clean the low end of the reverb naturally?\n\n"
             << "  3. Is 400 Hz stronger but still musically usable?\n\n"
             << "  4. Any hollow resonance, filter ringing or strange transient?\n\n"
             << "CASE 3 - High Cut\n"
             << "  1. Does OFF remain open?\n\n"
             << "  2. Does 10 kHz give a gentle darkening?\n\n"
             << "  3. Is 6 kHz clearly darker and still usable?\n\n"
             << "  4. Any brittle edge, resonance or unnatural HF collapse?\n\n"
             << "CASE 4 - High Cut against High Damp\n"
             << "  1. Which of 1, 2 and 3 is the brightest, i.e. the untreated one?\n\n"
             << "  2. Of the other two, which darkens uniformly across the whole tail,\n"
             << "     and which starts brighter and darkens as it decays?\n\n"
             << "  3. Do those two feel like two different controls, or like two\n"
             << "     copies of the same one?\n\n"
             << "THE DECISION\n"
             << "------------\n"
             << "  PASS              useful control range, predictable response, no\n"
             << "                    click, no stale burst, no pitch glide, no metallic\n"
             << "                    or ringing filter artefact, no dry-path timing\n"
             << "                    change; Low Cut reads as low-end cleanup, High Cut\n"
             << "                    as tonal darkening, Pre-Delay as clean separation,\n"
             << "                    and High Cut and High Damp remain distinguishable\n"
             << "                    in role\n\n"
             << "  CONDITIONAL PASS  all of the above, with one setting that is\n"
             << "                    aggressive or obviously filtered at an extreme.\n"
             << "                    An extreme setting sounding extreme is not a\n"
             << "                    defect.\n\n"
             << "  FAIL              a reproducible artefact, or behaviour inconsistent\n"
             << "                    with the control's intended function: a click, a\n"
             << "                    ring, a pitch glide, a dry-path timing shift, or\n"
             << "                    two controls that are perceptually redundant\n\n"
             << "  verdict:\n\n"
             << "On FAIL, do not tune anything. Record which case, which setting and\n"
             << "which perceptual dimension failed, and stop. In particular, PHASE 7\n"
             << "damping is closed and is not to be retuned on the strength of a\n"
             << "PHASE 8 listening result.\n\n"
             << "Nothing in this set is a measurement and nothing in it is a\n"
             << "recommendation. It renders the settings and asks.\n";

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
