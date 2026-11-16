#include "Phase11Listening.h"

#include "GainStaging.h"
#include "WetGainStaging.h"

#include "DSP/Denormal.h"
#include "DSP/FdnTuning.h"
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

    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;

    [[nodiscard]] std::string fixed (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate, double size,
                                                    double decay)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (decay), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (kModDepth),
                                      mapping::modRateScaleFor (kModRate), true);

        for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).getCore().resetModulation();

        tank->reset();

        return tank;
    }

    /** An electric-piano-like note: the source that measured worst. */
    [[nodiscard]] std::vector<double> pianoNote (double hz, double seconds,
                                                 double sampleRate)
    {
        const auto n = static_cast<size_t> (seconds * sampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;

            double v = std::sin (2.0 * 3.14159265358979323846 * hz * t);
            v += 0.5 * std::sin (2.0 * 3.14159265358979323846 * hz * 2.01 * t)
                     * std::exp (-t * 3.0);
            v += 0.25 * std::sin (2.0 * 3.14159265358979323846 * hz * 4.02 * t)
                      * std::exp (-t * 6.0);
            v += 0.15 * std::sin (2.0 * 3.14159265358979323846 * hz * 6.9 * t)
                      * std::exp (-t * 12.0);

            const double env = std::exp (-t * 1.6) * (1.0 - std::exp (-t * 500.0));

            x[i] = 0.34 * v * env;
        }

        return x;
    }

    /** Three notes in sequence: the control pitch, then the failing pitch,
        then the control again. A within-file A-B-A so the listener does not
        have to remember across files. */
    [[nodiscard]] std::vector<double> threeNotes (double controlHz, double failingHz,
                                                  double sampleRate)
    {
        const double noteSeconds = 1.6;
        const auto noteLength = static_cast<size_t> (noteSeconds * sampleRate);

        std::vector<double> out (noteLength * 3, 0.0);

        const double pitches[3] { controlHz, failingHz, controlHz };

        for (int k = 0; k < 3; ++k)
        {
            const auto note = pianoNote (pitches[k], noteSeconds, sampleRate);

            for (size_t n = 0; n < note.size() && n < noteLength; ++n)
                out[static_cast<size_t> (k) * noteLength + n] = note[n];
        }

        return out;
    }

    [[nodiscard]] std::vector<StereoFrame> render (const std::vector<double>& drive,
                                                    double sampleRate, double size,
                                                    double decay, double mix)
    {
        ScopedFtz ftz;

        auto tank = makeTank (sampleRate, size, decay);

        const double trim = mapping::previewWetTrim (mapping::rt60For (decay),
                                                     mapping::sizeScaleFor (size));

        const auto tail = static_cast<size_t> (sampleRate * (decay + 1.0));

        std::vector<StereoFrame> out (drive.size() + tail);

        for (size_t n = 0; n < out.size(); ++n)
        {
            double l = n < drive.size() ? drive[n] : 0.0;
            double r = l;

            const double dry = l;

            tank->processSample (l, r);

            l *= trim;
            r *= trim;

            // Width 100 % is the identity, so it is omitted rather than
            // written out as a no-op that invites someone to change it.
            out[n].l = dry * (1.0 - mix) + l * mix;
            out[n].r = dry * (1.0 - mix) + r * mix;
        }

        return out;
    }

    /** The honest downmix: sum, halve, both channels. NOT renormalised. */
    [[nodiscard]] std::vector<StereoFrame> foldToMono (const std::vector<StereoFrame>& x)
    {
        std::vector<StereoFrame> out (x.size());

        for (size_t n = 0; n < x.size(); ++n)
        {
            const double m = 0.5 * (x[n].l + x[n].r);
            out[n].l = out[n].r = m;
        }

        return out;
    }

    [[nodiscard]] double levelDb (const std::vector<StereoFrame>& f)
    {
        double sum = 0.0;

        for (const auto& s : f)
            sum += s.l * s.l + s.r * s.r;

        const double rms = std::sqrt (sum / std::max<size_t> (1, f.size() * 2));

        return rms > 0.0 ? 20.0 * std::log10 (rms) : -200.0;
    }
}

int exportPhase11MonoPack (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    struct Case
    {
        const char* name;
        double size, decay, failingHz, controlHz, mix;
        const char* label;
    };

    /*
        TWO CASES, CHOSEN BY MEASUREMENT RATHER THAN BY SEVERITY ALONE.

        The first is at the PRODUCT DEFAULT, which is the one that decides
        whether this is a shipping problem: D4 measured -7.76 dB on a piano
        note against C4's -0.77 dB, two semitones apart.

        The second is the small-room setting where Stage 1 originally found
        the condition: A3 at -12.60 dB against B3 at -0.62 dB.

        Each file plays control, failing, control - so the listener compares
        within the file and does not have to hold a memory across files.
    */
    const std::vector<Case> cases
    {
        { "default", 0.60, 2.5, 293.7, 261.6, 1.0, "Size 60 %, Decay 2.5 s - the product default" },
        { "small",   0.15, 0.6, 220.0, 123.5, 1.0, "Size 15 %, Decay 0.6 s - a small, short room" },
    };

    int written = 0;

    std::ostringstream md;

    md << "# AuroraVerb - PHASE 11: is the narrow-band mono null audible?\n\n"
       << "Six files, two cases. **This is not a preference test.** The question\n"
       << "is whether a measured stereo-to-mono cancellation in the late reverb\n"
       << "is audible as a product problem.\n\n"
       << "## What you are listening to\n\n"
       << "Each file plays the same electric-piano-like note three times:\n"
       << "**a control pitch, then the failing pitch, then the control again.**\n"
       << "The comparison is inside the file, so you do not have to remember\n"
       << "across files.\n\n"
       << "For each case there is a **stereo** render and its own **mono**\n"
       << "fold-down. The mono file is the honest downmix of that exact stereo\n"
       << "file - sum, halve, both channels - and it is **deliberately not\n"
       << "level-matched back up**. Normalising it would hide the entire effect.\n\n"
       << "Early/Late is 0 and Mix is 100 % for the two main cases, because the\n"
       << "condition lives in the late reverb and both the early layer and the\n"
       << "dry signal would mask it. One case is also provided at 60 % Mix so it\n"
       << "can be judged in context.\n\n"
       << "## What the measurements say\n\n"
       << "Summing two decorrelated channels costs 3.01 dB. That is normal and\n"
       << "is what a healthy reverb does. These notes do worse:\n\n"
       << "| case | failing note | measured mono loss | control note | control loss |\n"
       << "|---|---|---|---|---|\n"
       << "| product default | D4, 293.7 Hz | **-7.76 dB** | C4, 261.6 Hz | -0.77 dB |\n"
       << "| small room | A3, 220.0 Hz | **-12.60 dB** | B3, 123.5 Hz | -0.62 dB |\n\n"
       << "Adjacent semitones differ by around 4 dB, so in mono a chromatic line\n"
       << "would have some notes keep their reverb and others lose much of it.\n\n"
       << "## Files\n\n";

    for (const auto& c : cases)
    {
        const auto drive = threeNotes (c.controlHz, c.failingHz, sampleRate);

        const auto stereo = render (drive, sampleRate, c.size, c.decay, c.mix);
        const auto mono   = foldToMono (stereo);

        const std::string stereoPath = std::string ("case-") + c.name + "-stereo.wav";
        const std::string monoPath   = std::string ("case-") + c.name + "-mono.wav";

        if (! writeStereoWav (directory + "/" + stereoPath, stereo, sampleRate)
            || ! writeStereoWav (directory + "/" + monoPath, mono, sampleRate))
            return -1;

        written += 2;

        md << "- `" << stereoPath << "` - " << c.label
           << ", stereo. Integrated level " << fixed (levelDb (stereo), 2) << " dB\n"
           << "- `" << monoPath << "` - the same render folded to mono. "
           << fixed (levelDb (mono), 2) << " dB, a difference of "
           << fixed (levelDb (stereo) - levelDb (mono), 2) << " dB\n";
    }

    // The default case again at a musical Mix, for context.
    {
        const auto& c = cases.front();
        const auto drive = threeNotes (c.controlHz, c.failingHz, sampleRate);

        const auto stereo = render (drive, sampleRate, c.size, c.decay, 0.60);
        const auto mono   = foldToMono (stereo);

        if (! writeStereoWav (directory + "/case-default-mix60-stereo.wav", stereo, sampleRate)
            || ! writeStereoWav (directory + "/case-default-mix60-mono.wav", mono, sampleRate))
            return -1;

        written += 2;

        md << "- `case-default-mix60-stereo.wav` - the default case at 60 % Mix, "
           << "stereo. " << fixed (levelDb (stereo), 2) << " dB\n"
           << "- `case-default-mix60-mono.wav` - its mono fold-down. "
           << fixed (levelDb (mono), 2) << " dB, a difference of "
           << fixed (levelDb (stereo) - levelDb (mono), 2) << " dB\n";
    }

    md << "\n## The four questions\n\n"
       << "Please answer for each of the two main cases, and once more for the\n"
       << "60 % Mix pair.\n\n"
       << "1. **Does the mono version sound dramatically thinner, or does the\n"
       << "   reverb nearly disappear?**\n\n"
       << "2. **Is the difference greater than normal stereo-to-mono\n"
       << "   narrowing?** Every reverb loses some width and about 3 dB of level\n"
       << "   in mono. The question is whether this is more than that.\n\n"
       << "3. **Does a specific note or resonance vanish unnaturally?** The\n"
       << "   middle note of the three is the measured failing pitch. Does it\n"
       << "   behave differently from the two outer notes when you switch to\n"
       << "   mono?\n\n"
       << "4. **Would this be unacceptable in a real mix played in mono?**\n\n"
       << "## What your answer decides\n\n"
       << "If realistic musical material produces an obvious destructive mono\n"
       << "failure, the condition stays a **shipping blocker** and the next step\n"
       << "is new DSP - three stages of research have shown no data change\n"
       << "fixes it.\n\n"
       << "If what you hear is only normal or moderate narrowing, it becomes a\n"
       << "**known measured non-blocking characteristic**, documented and\n"
       << "carried with its diagnostic tests.\n\n"
       << "The measurements lean toward the first answer. They cannot settle it.\n\n"
       << "Sample rate " << fixed (sampleRate, 0) << " Hz, 32-bit float stereo.\n";

    {
        std::ofstream f (directory + "/LISTENING.md", std::ios::binary);

        if (! f)
            return -1;

        f << md.str();
        ++written;
    }

    return written;
}

} // namespace avtest
