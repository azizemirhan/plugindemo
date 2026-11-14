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
                              * (0.55 * std::sin (2.0 * pi * f0 * t)
                                 + 0.22 * std::sin (2.0 * pi * 2.0 * f0 * t)
                                 + 0.09 * std::sin (2.0 * pi * 3.0 * f0 * t)
                                 + 0.04 * std::sin (2.0 * pi * 5.0 * f0 * t));

            // A kick-like thump on every beat: a short low sine burst.
            const double thump = std::exp (-since / 0.055)
                                 * 0.35 * std::sin (2.0 * pi * 48.0 * since);

            // Mid content, so the file is a bass part in a track and not a
            // test tone. Quiet, and well above the cut frequencies, so it
            // stays in the reverb at every Low Cut setting and gives the ear
            // something unchanged to judge the low end against.
            const double midSince = std::fmod (t, beat * 0.5);
            const double mid = std::exp (-midSince / 0.040) * 0.10
                               * (std::sin (2.0 * pi * 880.0 * t)
                                  + 0.6 * next());

            const double x = 0.42 * (bass + thump + mid);

            // Slightly decorrelated, as the other sources are: the low end is
            // deliberately near-mono, which is how bass is actually recorded,
            // and the mid carries the width.
            out[n].l = x;
            out[n].r = 0.42 * (bass + thump + mid * 0.7
                               + 0.04 * std::exp (-midSince / 0.040)
                                 * std::sin (2.0 * pi * 880.0 * t + 0.8));
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  Measurement, for the manifest and for the case-4 match
    // -----------------------------------------------------------------------
    /**
        Energy above the split over energy below it, in dB.

        The same measure PHASE 7's listening set used, at the same 6 kHz
        split, so a number here and a number there describe the same thing.
        Not a loudness model and not a verdict: only differences between
        renders measured identically are used.
    */
    [[nodiscard]] double highToLowDb (const std::vector<StereoFrame>& frames,
                                      double sampleRate, double splitHz = 6000.0)
    {
        const double a = std::exp (-2.0 * pi * splitHz / sampleRate);

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

    /**
        How fast the high band decays relative to the whole, as a time.

        This exists for case 4 and it is the measure that separates the two
        controls. A high cut removes high-frequency energy at the input, so
        the high band is quieter throughout and its decay TIME is unchanged.
        High Damp shortens the high band's decay, so the high band's energy
        falls away faster than the rest of the tail.

        Measured as the centre of gravity in time of the energy above the
        split, in seconds from the start. Crude on purpose: it is a single
        number that moves for one control and not the other, and it is in the
        manifest so a listener knows what the two files are expected to differ
        in rather than hunting for a difference blind.
    */
    [[nodiscard]] double highBandCentroidSeconds (const std::vector<StereoFrame>& frames,
                                                  double sampleRate,
                                                  double splitHz = 6000.0)
    {
        const double a = std::exp (-2.0 * pi * splitHz / sampleRate);

        double stateL = 0.0, stateR = 0.0;
        double weighted = 0.0, total = 0.0;

        for (size_t n = 0; n < frames.size(); ++n)
        {
            stateL = a * stateL + (1.0 - a) * frames[n].l;
            stateR = a * stateR + (1.0 - a) * frames[n].r;

            const double hiL = frames[n].l - stateL;
            const double hiR = frames[n].r - stateR;

            const double energy = hiL * hiL + hiR * hiR;

            weighted += energy * static_cast<double> (n);
            total    += energy;
        }

        if (total <= 0.0)
            return 0.0;

        return weighted / total / sampleRate;
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

    const Target case2Targets[]
    {
        { "bass",  "mix30", kShippingMix },
        { "bass",  "wet",   1.0 },
        { "vocal", "mix30", kShippingMix },
    };

    manifest << "  " << pad ("file", 30) << pad ("peak", 11)
             << pad ("loudness", 13) << "vs OFF\n";

    for (const auto& t : case2Targets)
    {
        double loudnessAtOff = 0.0;

        for (const auto& v : case2)
        {
            const auto frames = render (sourceNamed (t.source), sampleRate, v.point, t.wet);

            const std::string name = std::string ("case2-") + t.source + "-" + t.mix
                                   + "-" + v.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            const double loudness = integratedLoudnessLufs (frames, sampleRate);

            if (std::string (v.tag) == "off")
                loudnessAtOff = loudness;

            double filePeak = 0.0;

            for (const auto& f : frames)
                filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

            manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                     << pad (fixed (loudness, 2) + " LUFS", 13)
                     << fixed (loudness - loudnessAtOff, 2) << " dB\n";
        }
    }

    manifest << "\n  The level drop is the effect, not a flaw in the set. It is NOT\n"
             << "  matched away: a low cut that removes low-frequency energy is\n"
             << "  quieter, and hiding that would hide half of what the control does.\n\n"
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

    const Target case3Targets[]
    {
        { "hats",  "mix30", kShippingMix },
        { "hats",  "wet",   1.0 },
        { "vocal", "mix30", kShippingMix },
    };

    manifest << "  " << pad ("file", 30) << pad ("peak", 11)
             << pad ("HF/LF", 13) << "vs OFF\n";

    for (const auto& t : case3Targets)
    {
        double balanceAtOff = 0.0;

        for (const auto& v : case3)
        {
            const auto frames = render (sourceNamed (t.source), sampleRate, v.point, t.wet);

            const std::string name = std::string ("case3-") + t.source + "-" + t.mix
                                   + "-" + v.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            const double balance = highToLowDb (frames, sampleRate);

            if (std::string (v.tag) == "off")
                balanceAtOff = balance;

            double filePeak = 0.0;

            for (const auto& f : frames)
                filePeak = std::max (filePeak, std::max (std::abs (f.l), std::abs (f.r)));

            manifest << "  " << pad (name, 30) << pad (fixed (filePeak, 4), 11)
                     << pad (fixed (balance, 2) + " dB", 13)
                     << fixed (balance - balanceAtOff, 2) << " dB\n";
        }
    }

    manifest << "\n  HF/LF is the energy above 6 kHz over the energy below it. It is\n"
             << "  not a loudness model; only the difference between renders measured\n"
             << "  identically is used. Note that the 6 kHz High Cut and the 6 kHz\n"
             << "  measurement split coincide, so that row understates the change.\n\n"
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
        Choosing C's High Damp by measurement rather than by guess.

        The comparison is only about the ROLE of the two controls if they are
        set to produce a comparable AMOUNT of darkening. Pick C's High Damp by
        hand and the listener is partly judging which one was set harder. So
        it is searched: render B, measure its HF/LF balance, then find the
        High Damp value whose balance comes closest to B's with no high cut at
        all.

        The search is reported, including how close the match got, because a
        match that failed would change how the answers should be read.
    */
    const auto& case4Source = sourceNamed ("hats");

    const Point case4A { 0.0, lowCutOffHz, highCutOffHz, kHighDamp };
    const Point case4B { 0.0, lowCutOffHz, 8000.0,       kHighDamp };

    const auto renderA = render (case4Source, sampleRate, case4A, 1.0);
    const auto renderB = render (case4Source, sampleRate, case4B, 1.0);

    const double balanceA = highToLowDb (renderA, sampleRate);
    const double balanceB = highToLowDb (renderB, sampleRate);

    manifest << "  C's High Damp is chosen by measurement, not by guess: the\n"
             << "  comparison is about the two controls' ROLE, and that only works if\n"
             << "  they are set to produce a comparable amount of darkening.\n"
             << "  Otherwise part of the answer is which one was turned up further.\n\n"
             << "  " << pad ("High Damp", 12) << pad ("HF/LF", 12) << "vs B\n";

    double bestHighDamp = 0.0;
    double bestError = 1.0e30;
    double bestBalance = 0.0;

    for (int step = 0; step <= 9; ++step)
    {
        const double damp = 0.50 + 0.05 * step;

        const Point candidate { 0.0, lowCutOffHz, highCutOffHz, damp };

        const auto frames = render (case4Source, sampleRate, candidate, 1.0);
        const double balance = highToLowDb (frames, sampleRate);
        const double error = std::abs (balance - balanceB);

        manifest << "  " << pad (fixed (damp * 100.0, 0) + " %", 12)
                 << pad (fixed (balance, 2) + " dB", 12)
                 << fixed (balance - balanceB, 2) << " dB\n";

        if (error < bestError)
        {
            bestError = error;
            bestHighDamp = damp;
            bestBalance = balance;
        }
    }

    manifest << "\n  chosen: High Damp " << fixed (bestHighDamp * 100.0, 0)
             << " %, which lands within " << fixed (bestError, 2)
             << " dB of B's HF/LF balance\n\n";

    const Point case4C { 0.0, lowCutOffHz, highCutOffHz, bestHighDamp };

    const auto renderC = render (case4Source, sampleRate, case4C, 1.0);

    // What the two controls are expected to differ in, stated up front.
    manifest << "  What the measurement expects, so that listening confirms or\n"
             << "  contradicts something specific rather than hunting blind:\n\n"
             << "  " << pad ("version", 10) << pad ("HF/LF", 12)
             << pad ("HF decay centroid", 20) << "\n"
             << "  " << pad ("A", 10) << pad (fixed (balanceA, 2) + " dB", 12)
             << pad (fixed (highBandCentroidSeconds (renderA, sampleRate), 3) + " s", 20) << "\n"
             << "  " << pad ("B", 10) << pad (fixed (balanceB, 2) + " dB", 12)
             << pad (fixed (highBandCentroidSeconds (renderB, sampleRate), 3) + " s", 20) << "\n"
             << "  " << pad ("C", 10) << pad (fixed (bestBalance, 2) + " dB", 12)
             << pad (fixed (highBandCentroidSeconds (renderC, sampleRate), 3) + " s", 20) << "\n\n"
             << "  The HF decay centroid is the centre of gravity in time of the\n"
             << "  energy above 6 kHz. B and C are matched on HF/LF balance by\n"
             << "  construction, so if the two controls really do different things\n"
             << "  the centroid is where it shows: a high CUT scales the high band\n"
             << "  down without changing when it arrives or how long it lasts, while\n"
             << "  high DAMP makes it leave earlier. A smaller centroid on C than on\n"
             << "  B is the whole distinction, in one number.\n\n";

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
        << " %   (chosen to match B's HF/LF balance to "
        << fixed (bestError, 2) << " dB)\n\n"
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
