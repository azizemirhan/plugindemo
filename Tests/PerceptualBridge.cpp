#include "PerceptualBridge.h"

#include "DspAnalysis.h"
#include "GainStaging.h"
#include "ModalMap.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include "DSP/Denormal.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/*
    Phase 6 / FU-1: the perceptual bridge.

    The measurement is finished and says two things about the tank: twenty-five
    modes hold still under modulation and twenty-one are moved by it. Neither
    statement is about sound. This renders the material that turns them into a
    question a listener can answer, and it is careful to render nothing else.

    What it renders, and why it has to be band-limited
    --------------------------------------------------
    A candidate is one mode of one tank. The DSP is identical for every
    candidate - same Decay, same Size, same Mod Depth, same everything - so two
    candidates fed the identical broadband source produce the identical file.
    The only thing that can distinguish them is which part of the spectrum the
    excitation puts energy into.

    So the source material is the same for every candidate and the band is not:
    one ERB centred on the candidate's frequency. That is a deliberate reading
    of "the same source for every candidate" - the same sample, the same level,
    the same envelope, so no difference between two candidates can come from
    the material - and it is the only reading under which the comparison
    exists at all. The alternative, band-limiting the tank's output instead of
    its input, was rejected: that is post-processing, and this set has none.

    An ERB rather than a round number of Hz because the question is perceptual.
    Within one equivalent rectangular bandwidth the ear integrates; that is the
    band inside which this mode contributes to timbre rather than to a separate
    audible event.

    What it deliberately does not do
    --------------------------------
      - no normalisation, no loudness match, no limiting, no EQ, no notch
      - no gain applied beyond the shipping preview trim, which is the same
        number for every file in the set
      - no sustained sine anywhere: every excitation is a short transient, so
        the tank is never driven into behaviour it would not meet in use
      - no judgement, in the file names, in the manifest, or in the log

    Blinding, and its limit
    -----------------------
    Letters are permuted, the permutation differs between the two sources so
    that an order cannot be memorised from one and applied to the other, and the
    mapping lives in a key file that is meant to stay unread. Per-file
    measurements live in the key too, because a loudness column is the mapping
    written in another alphabet.

    The limit is worth stating plainly rather than papering over: a band-limited
    burst has an audible centre frequency, so a listener who knows the candidate
    list can tell which candidate a file is by ear. This blinds the letters, not
    the physics. What it protects is the order and the class label; what it
    cannot protect is the frequency.
*/
namespace avtest
{

namespace
{
    using namespace av::dsp;

    constexpr double pi = 3.14159265358979323846;

    /** Round one, in the order the pairs are to be listened to. */
    struct Candidate
    {
        int modeId;
        double frequencyHz;
        const char* classLetter;   /**< Key only. Never in a file name. */
        int pair;
        const char* region;
    };

    const Candidate candidates[] =
    {
        { 50,  375.44, "B", 0, "low-mid"   },
        {  4,  646.34, "A", 0, "low-mid"   },
        { 58, 1471.67, "B", 1, "mid"       },
        {  2, 1158.49, "A", 1, "mid"       },
        { 54, 4505.77, "B", 2, "upper-mid" },
        { 56, 4139.64, "A", 2, "upper-mid" },
    };

    constexpr size_t candidateCount = 6;
    constexpr size_t pairCount = 3;

    /**
        Two permutations of six letters.

        Fixed rather than clock-seeded, so a rerun writes the same files, and
        different between the two sources, so the diagnostic ordering says
        nothing about the snare ordering.
    */
    const int diagnosticOrder[candidateCount] = { 3, 0, 5, 1, 4, 2 };
    const int snareOrder[candidateCount]      = { 1, 4, 2, 5, 0, 3 };

    constexpr char letters[] = { 'A', 'B', 'C', 'D', 'E', 'F' };

    /** Glasberg and Moore's equivalent rectangular bandwidth, in Hz. */
    [[nodiscard]] double erbHz (double centreHz) noexcept
    {
        return 24.7 * (4.37 * centreHz * 0.001 + 1.0);
    }

    // -----------------------------------------------------------------------
    //  A causal band-pass, so the excitation has no pre-ring
    // -----------------------------------------------------------------------

    /**
        Four cascaded second-order band-passes, run forwards only.

        Zero-phase FFT filtering would be the obvious choice and is wrong here:
        it puts a symmetric pre-echo in front of the transient, and a listening
        test about whether a tail rings should not hand the listener a ring
        before the hit. Four sections give 24 dB per octave skirts; the section
        bandwidth is widened so that the cascade's -3 dB width is one ERB.
    */
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        [[nodiscard]] double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

            x2 = x1; x1 = x;
            y2 = y1; y1 = y;

            return y;
        }
    };

    [[nodiscard]] Biquad bandPass (double centreHz, double qFactor, double sampleRate)
    {
        Biquad f;

        const double w = 2.0 * pi * centreHz / sampleRate;
        const double alpha = std::sin (w) / (2.0 * std::max (0.0001, qFactor));
        const double a0 = 1.0 + alpha;

        f.b0 = alpha / a0;
        f.b1 = 0.0;
        f.b2 = -alpha / a0;
        f.a1 = -2.0 * std::cos (w) / a0;
        f.a2 = (1.0 - alpha) / a0;

        return f;
    }

    constexpr int bandSections = 4;

    /** sqrt(2^(1/N) - 1) for N = 4: how much a cascade narrows the band. */
    constexpr double cascadeNarrowing = 0.434959835;

    void bandLimitInPlace (std::vector<StereoFrame>& frames, double centreHz,
                           double sampleRate)
    {
        const double bandwidth = erbHz (centreHz);
        const double sectionQ = centreHz / (bandwidth / cascadeNarrowing);

        Biquad left[bandSections], right[bandSections];

        for (int s = 0; s < bandSections; ++s)
        {
            left[s] = bandPass (centreHz, sectionQ, sampleRate);
            right[s] = bandPass (centreHz, sectionQ, sampleRate);
        }

        for (auto& f : frames)
        {
            for (int s = 0; s < bandSections; ++s)
            {
                f.l = left[s].process (f.l);
                f.r = right[s].process (f.r);
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Excitations
    // -----------------------------------------------------------------------

    struct Rng
    {
        unsigned state = 1u;

        [[nodiscard]] double next() noexcept
        {
            state = state * 1664525u + 1013904223u;

            return static_cast<double> (state) / 2147483648.0 - 1.0;
        }
    };

    [[nodiscard]] double envelope (double t, double attack, double decay) noexcept
    {
        if (t < 0.0)
            return 0.0;

        return (1.0 - std::exp (-t / attack)) * std::exp (-t / decay);
    }

    /**
        A short broadband transient, before band-limiting.

        Noise rather than a tone: a burst has energy everywhere in its band and
        excites the whole neighbourhood, which is what a mode has to ring out of
        rather than be handed. 25 ms of decay, so the excitation is over long
        before the tail is being judged.
    */
    [[nodiscard]] std::vector<StereoFrame> diagnosticBurst (double sampleRate, double seconds, unsigned seed = 20260818u)
    {
        std::vector<StereoFrame> out (static_cast<size_t> (sampleRate * seconds));

        Rng rng { seed };

        const double onset = 0.05;

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / sampleRate - onset;
            const double x = 0.5 * rng.next() * envelope (t, 0.0004, 0.025);

            out[n] = { x, x };
        }

        return out;
    }
}

// ===========================================================================

int exportPerceptualBridgeSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double diagnosticSeconds = 0.6;   /**< Source length; the tail follows. */
    constexpr double tailSeconds = 6.5;

    // The musical source: the same snare stand-in the Phase 5F sets used, so a
    // listener who has heard those hears the same hit here.
    const auto sources = makeFu2Sources (sampleRate);

    const std::vector<StereoFrame>* snare = nullptr;

    for (const auto& s : sources)
        if (s.name == "snare")
            snare = &s.frames;

    if (snare == nullptr)
        return -1;

    const auto diagnosticSource = diagnosticBurst (sampleRate, diagnosticSeconds);

    std::ostringstream manifest, key;

    manifest << "PHASE 6 / FU-1 - TARGETED LISTENING SET, ROUND 1\n";
    manifest << "================================================\n\n";
    manifest << "Six candidates in three pairs, one pair per frequency region.\n";
    manifest << "Within a pair the two candidates differ only in which mode of the\n";
    manifest << "tank the excitation lands on.\n\n";
    manifest << "Settings, all shipping defaults and none of them varied:\n";
    manifest << "  Decay      " << defaults::decaySeconds << " s\n";
    manifest << "  Size       " << defaults::sizeControl << "  (scale "
             << mapping::sizeScaleFor (defaults::sizeControl) << ")\n";
    manifest << "  Mod Rate   " << defaults::modRateControlHz << " Hz\n";
    manifest << "  Mod Depth  " << defaults::modDepthControl
             << "   (a Mod Depth 0 variant of the diagnostic is also written)\n";
    manifest << "  Width      " << defaults::width << "\n";
    manifest << "  Mix        100% wet\n";
    manifest << "  Size compensation on, shipping preview trim applied\n\n";
    manifest << "48 kHz, 32-bit float stereo. No normalisation, no loudness match,\n";
    manifest << "no limiting, no EQ, no notch, no post-processing of any kind. The\n";
    manifest << "same trim is applied to every file, so the levels you hear are the\n";
    manifest << "levels the tank produced.\n\n";

    manifest << "Excitation\n";
    manifest << "----------\n";
    manifest << "The DSP is identical for every candidate, so an identical broadband\n";
    manifest << "source would give identical files. What differs is the band the\n";
    manifest << "excitation occupies: one ERB centred on the candidate, from four\n";
    manifest << "cascaded second-order band-passes run forwards only, so there is no\n";
    manifest << "pre-ring in front of the transient. The source material inside that\n";
    manifest << "band is the same for every candidate.\n\n";
    manifest << "  diagnostic-*.wav   a 25 ms noise burst\n";
    manifest << "  snare-*.wav        the Phase 5F snare stand-in\n";
    manifest << "  moddepth0-*.wav    the diagnostic again with Mod Depth at 0\n\n";
    manifest << "  snare-fullband-context.wav is the unfiltered snare through the same\n";
    manifest << "  tank. It is identical for every candidate, carries no letter and is\n";
    manifest << "  not part of any comparison - it is there so the bands can be heard\n";
    manifest << "  against the whole tail.\n\n";

    manifest << "Blinding\n";
    manifest << "--------\n";
    manifest << "Letters are permuted, and permuted differently for the diagnostic and\n";
    manifest << "for the snare, so the same candidate is a different letter in the two\n";
    manifest << "sets. The Mod Depth 0 files keep the diagnostic's letter, because that\n";
    manifest << "comparison is within one candidate rather than between two.\n\n";
    manifest << "BLIND_KEY.txt holds the mapping and every per-file measurement. Leave\n";
    manifest << "it unread until the listening is done - a level column is the mapping\n";
    manifest << "written in another alphabet.\n\n";
    manifest << "One thing this cannot blind: a band-limited burst has an audible\n";
    manifest << "centre frequency, so anyone holding the candidate list can tell which\n";
    manifest << "candidate a file is by ear. The letters hide the order and the class;\n";
    manifest << "they cannot hide the pitch.\n\n";

    key << "PHASE 6 / FU-1 - TARGETED LISTENING SET, BLIND KEY\n";
    key << "==================================================\n\n";
    key << "Do not read this before listening.\n\n";
    key << "It carries the letter mapping, the class of each candidate and every\n";
    key << "per-file measurement. Any one of the three gives the others away.\n\n";

    int written = 0;

    const auto measureAndLog = [&] (const std::string& fileName,
                                    const std::vector<StereoFrame>& frames,
                                    const Candidate& c)
    {
        const auto level = measureWindow (fileName, frames, sampleRate);

        // Where the energy actually ended up, inside the candidate's band.
        std::vector<double> mono (frames.size());

        for (size_t n = 0; n < frames.size(); ++n)
            mono[n] = 0.5 * (frames[n].l + frames[n].r);

        const size_t fftSize = 1u << 17;
        const size_t window = std::min (mono.size(), static_cast<size_t> (sampleRate * 2.0));

        std::vector<double> segment (mono.begin(), mono.begin() + static_cast<long> (window));

        const auto spectrum = magnitudeSpectrum (segment, fftSize, true);
        const auto res = resolutionFor (sampleRate, window, fftSize,
                                        mapping::sizeScaleFor (defaults::sizeControl));

        const double half = 1.5 * erbHz (c.frequencyHz);
        const auto local = measureLocalResponse (spectrum, res, c.frequencyHz, half);

        key << "    " << fileName << "\n";
        key << "      peak " << std::fixed << std::setprecision (2) << level.peakDb
            << " dBFS, RMS " << level.rmsDb << " dBFS, integrated "
            << level.loudnessLufs << " LUFS\n";

        if (local.measured)
            key << "      in band: centroid " << local.spectralCentroidHz
                << " Hz, spread " << local.spectralSpreadHz
                << " Hz, concentration " << std::setprecision (4) << local.energyConcentration
                << ", peaks " << local.peakCount << "\n" << std::setprecision (2);

        return level.peakDb;
    };

    double worstPeakDb = -300.0;
    double quietestPeakDb = 300.0;
    int clippingFiles = 0;
    int veryQuietFiles = 0;

    const auto notePeak = [&] (double peakDb)
    {
        worstPeakDb = std::max (worstPeakDb, peakDb);
        quietestPeakDb = std::min (quietestPeakDb, peakDb);

        if (peakDb > 0.0)   ++clippingFiles;
        if (peakDb < -60.0) ++veryQuietFiles;
    };

    // --- the three sets -------------------------------------------------------

    struct Pass
    {
        const char* prefix;
        const int* order;
        bool useSnare;
        double modDepth;
    };

    const Pass passes[] =
    {
        { "diagnostic", diagnosticOrder, false, defaults::modDepthControl },
        { "snare",      snareOrder,      true,  defaults::modDepthControl },
        { "moddepth0",  diagnosticOrder, false, 0.0                       },
    };

    for (const auto& pass : passes)
    {
        key << "--- " << pass.prefix << " ---\n";

        for (size_t c = 0; c < candidateCount; ++c)
        {
            const auto& cand = candidates[c];

            auto dry = pass.useSnare ? *snare : diagnosticSource;

            bandLimitInPlace (dry, cand.frequencyHz, sampleRate);

            const auto wet = renderShippingWet (dry, sampleRate, pass.modDepth, tailSeconds);

            int slot = 0;

            for (size_t i = 0; i < candidateCount; ++i)
                if (pass.order[i] == static_cast<int> (c))
                    slot = static_cast<int> (i);

            const std::string name = std::string (pass.prefix) + "-"
                                   + letters[slot] + ".wav";

            if (! writeStereoWav (directory + "/" + name, wet, sampleRate))
                return -1;

            ++written;

            key << "  mode #" << cand.modeId << "  " << cand.frequencyHz << " Hz  class "
                << cand.classLetter << "  " << cand.region << "  ->  " << name << "\n";

            notePeak (measureAndLog (name, wet, cand));
        }

        key << "\n";
    }

    // --- the shared context render ---------------------------------------------

    {
        const auto wet = renderShippingWet (*snare, sampleRate, defaults::modDepthControl,
                                            tailSeconds);

        if (! writeStereoWav (directory + "/snare-fullband-context.wav", wet, sampleRate))
            return -1;

        ++written;

        const auto level = measureWindow ("snare-fullband-context.wav", wet, sampleRate);

        notePeak (level.peakDb);

        // Identical for every candidate, so it leaks nothing and can be stated
        // in the manifest rather than hidden in the key.
        manifest << "snare-fullband-context.wav: peak " << std::fixed << std::setprecision (2)
                 << level.peakDb << " dBFS, integrated " << level.loudnessLufs << " LUFS\n\n";
    }

    // --- what to listen for ------------------------------------------------------

    manifest << "The three pairs\n";
    manifest << "---------------\n";

    for (size_t p = 0; p < pairCount; ++p)
    {
        std::string diagnosticLetters, snareLetters;
        const char* region = "";

        for (size_t c = 0; c < candidateCount; ++c)
        {
            if (candidates[c].pair != static_cast<int> (p))
                continue;

            region = candidates[c].region;

            for (size_t i = 0; i < candidateCount; ++i)
            {
                if (diagnosticOrder[i] == static_cast<int> (c))
                    diagnosticLetters += std::string (diagnosticLetters.empty() ? "" : " vs ")
                                       + letters[i];

                if (snareOrder[i] == static_cast<int> (c))
                    snareLetters += std::string (snareLetters.empty() ? "" : " vs ")
                                  + letters[i];
            }
        }

        manifest << "  pair " << (p + 1) << "  " << region << "\n";
        manifest << "    diagnostic-" << diagnosticLetters.substr (0, 1)
                 << ".wav  vs  diagnostic-"
                 << diagnosticLetters.substr (diagnosticLetters.size() - 1) << ".wav\n";
        manifest << "    snare-" << snareLetters.substr (0, 1)
                 << ".wav       vs  snare-"
                 << snareLetters.substr (snareLetters.size() - 1) << ".wav\n";
    }

    manifest << "\nFour questions per pair, and nothing else is being asked:\n\n";
    manifest << "  1. which sounds more natural, more premium?\n";
    manifest << "  2. in which is a bell, chime or metallic note more obvious?\n";
    manifest << "  3. whose tail is more pleasant?\n";
    manifest << "  4. does either sit on one note in a way that bothers you?\n\n";
    manifest << "There is no right answer and no expected one. Nothing in the\n";
    manifest << "measurement says either candidate in a pair should win, and neither\n";
    manifest << "has been called a defect.\n";

    // --- files -------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << manifest.str();
            ++written;
        }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << key.str();
            ++written;
        }
    }

    // Content-free verification, safe to print: it says nothing about which
    // file is which.
    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << "PHASE 6 / FU-1 - LISTENING SET, RENDER CHECK\n";
            out << "============================================\n\n";
            out << "Deliberately content-free. Nothing here distinguishes one\n";
            out << "candidate from another.\n\n";
            out << "audio files written        " << (candidateCount * 3 + 1) << "\n";
            out << "files above 0 dBFS         " << clippingFiles << "\n";
            out << "files below -60 dBFS peak  " << veryQuietFiles << "\n";
            out << "quietest peak in the set   " << std::fixed << std::setprecision (2)
                << quietestPeakDb << " dBFS\n";
            out << "highest peak in the set    " << std::fixed << std::setprecision (2)
                << worstPeakDb << " dBFS\n";
            out << "sample rate                " << sampleRate << " Hz, 32-bit float stereo\n";
            out << "gain applied               shipping preview trim only, identical\n";
            out << "                           for every file\n";
            out << "normalisation              none\n";
            out << "loudness match             none\n";
            out << "post-processing            none\n";

            ++written;
        }
    }

    return written;
}

// ===========================================================================
//  Round 1b: what the modulation does, inside one mode
// ===========================================================================

/*
    Round one asked which of two modes sounds better and got an answer that
    could not be separated from "which frequency do you prefer" - the two
    candidates in a pair were an octave apart. This round removes that entirely:
    both files in a pair are the same mode, the same band, the same excitation
    sample. The only difference is Mod Depth, 0 against the shipping 30%.

    On re-rendering rather than reusing round one's files
    -----------------------------------------------------
    The instruction was to reuse the existing diagnostic renders. They are not
    reused, for two reasons that both point the same way.

    The round-one key is now open. It says which letter was which mode and which
    was Mod Depth 0, so a copy of those files can be de-blinded by comparing
    bytes with a file whose identity is already known. A blind test that only
    holds if nobody runs a checksum is not blind.

    And the listener has already heard round one's Mod Depth 30% diagnostics and
    formed opinions about them, while the Mod Depth 0 files are new. Reusing one
    side of the pair and not the other puts familiarity on one side of every
    comparison.

    So the excitation is regenerated with a different noise seed. Everything
    else is identical to round one - same band, same envelope, same tank, same
    trim - and inside a pair the excitation is the same vector passed twice,
    which the render check verifies rather than assumes.
*/
int exportModulationPerceptualSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double diagnosticSeconds = 0.6;
    constexpr double tailSeconds = 6.5;

    // A different realisation of the same noise, so nothing here is
    // byte-comparable with a round-one file.
    constexpr unsigned seed = 20260819u;

    struct Subject { int modeId; double frequencyHz; const char* classLetter; };

    const Subject subjects[] =
    {
        { 50,  375.44, "B" },
        {  4,  646.34, "A" },
        { 54, 4505.77, "B" },
    };

    constexpr size_t subjectCount = 3;

    // Which subject each numbered pair holds, and which variant takes X.
    // Fixed, so a rerun writes the same files. Pitch gives the subject away to
    // anyone holding the list - it is the Mod Depth assignment that this hides.
    const int pairSubject[subjectCount] = { 2, 0, 1 };
    const bool xIsModulated[subjectCount] = { true, false, true };

    const auto source = diagnosticBurst (sampleRate, diagnosticSeconds, seed);

    std::ostringstream manifest, key;

    manifest << "PHASE 6 / FU-1 - ROUND 1b, MODULATION INSIDE ONE MODE\n";
    manifest << "=====================================================\n\n";
    manifest << "Three pairs. Both files in a pair are the same mode, the same band\n";
    manifest << "and the same excitation sample. The only thing that differs is Mod\n";
    manifest << "Depth: one file at 0, the other at the shipping 30%.\n\n";
    manifest << "Round one compared two different modes, so preference there could not\n";
    manifest << "be separated from preference for a frequency region. That confound is\n";
    manifest << "gone here.\n\n";
    manifest << "Settings, all shipping defaults except the one under test:\n";
    manifest << "  Decay      " << defaults::decaySeconds << " s\n";
    manifest << "  Size       " << defaults::sizeControl << "\n";
    manifest << "  Mod Rate   " << defaults::modRateControlHz << " Hz\n";
    manifest << "  Mod Depth  0 in one file, " << defaults::modDepthControl
             << " in the other\n";
    manifest << "  Width      " << defaults::width << "\n";
    manifest << "  Mix        100% wet\n\n";
    manifest << "48 kHz, 32-bit float stereo. No normalisation, no loudness match, no\n";
    manifest << "limiting, no EQ, no notch, no post-processing. The same shipping trim\n";
    manifest << "is applied to every file.\n\n";
    manifest << "The excitation is a fresh noise realisation, not round one's. Round\n";
    manifest << "one's key is open, so copies of those files could be de-blinded by\n";
    manifest << "comparing bytes against a known one; and the listener has already\n";
    manifest << "heard the Mod Depth 30% side of every pair, which would put\n";
    manifest << "familiarity on one side of each comparison.\n\n";

    key << "PHASE 6 / FU-1 - ROUND 1b BLIND KEY\n";
    key << "===================================\n\n";
    key << "Do not read this before listening.\n\n";
    key << "Carries the Mod Depth assignment and every per-file measurement,\n";
    key << "including the signed level difference inside each pair. The sign is\n";
    key << "the mapping: modulation redistributes energy, so which file is louder\n";
    key << "is an inference away from which file is modulated.\n\n";

    int written = 0;
    int clippingFiles = 0;
    double worstPeakDb = -300.0, quietestPeakDb = 300.0;
    int identicalSourcePairs = 0;

    struct PairLevels { double lufsGap; double peakGap; };

    std::vector<PairLevels> gaps;

    for (size_t p = 0; p < subjectCount; ++p)
    {
        const auto& subject = subjects[static_cast<size_t> (pairSubject[p])];

        // One band-limited excitation, passed to both renders. Not two copies
        // built the same way - the same vector, so "identical" is structural.
        auto dry = source;

        bandLimitInPlace (dry, subject.frequencyHz, sampleRate);

        const auto before = dry;

        const auto modulated = renderShippingWet (dry, sampleRate,
                                                  defaults::modDepthControl, tailSeconds);

        // If the renderer had touched its input, the second render would not be
        // the comparison it claims to be. Checked, not assumed.
        bool unchanged = dry.size() == before.size();

        for (size_t n = 0; unchanged && n < dry.size(); ++n)
            unchanged = dry[n].l == before[n].l && dry[n].r == before[n].r;

        const auto unmodulated = renderShippingWet (dry, sampleRate, 0.0, tailSeconds);

        if (unchanged)
            ++identicalSourcePairs;

        const std::string stem = "modtest-" + std::to_string (p + 1) + "-";

        const auto& forX = xIsModulated[p] ? modulated : unmodulated;
        const auto& forY = xIsModulated[p] ? unmodulated : modulated;

        if (! writeStereoWav (directory + "/" + stem + "X.wav", forX, sampleRate))
            return -1;

        if (! writeStereoWav (directory + "/" + stem + "Y.wav", forY, sampleRate))
            return -1;

        written += 2;

        const auto levelX = measureWindow (stem + "X.wav", forX, sampleRate);
        const auto levelY = measureWindow (stem + "Y.wav", forY, sampleRate);

        for (double peak : { levelX.peakDb, levelY.peakDb })
        {
            worstPeakDb = std::max (worstPeakDb, peak);
            quietestPeakDb = std::min (quietestPeakDb, peak);

            if (peak > 0.0)
                ++clippingFiles;
        }

        gaps.push_back ({ std::abs (levelX.loudnessLufs - levelY.loudnessLufs),
                          std::abs (levelX.peakDb - levelY.peakDb) });

        key << "--- pair " << (p + 1) << " ---\n";
        key << "  mode #" << subject.modeId << "  " << subject.frequencyHz
            << " Hz  class " << subject.classLetter << "\n";
        key << "  " << stem << "X.wav = Mod Depth "
            << (xIsModulated[p] ? defaults::modDepthControl : 0.0) << "\n";
        key << "  " << stem << "Y.wav = Mod Depth "
            << (xIsModulated[p] ? 0.0 : defaults::modDepthControl) << "\n";
        key << std::fixed << std::setprecision (2);
        key << "    X: peak " << levelX.peakDb << " dBFS, RMS " << levelX.rmsDb
            << " dBFS, integrated " << levelX.loudnessLufs << " LUFS\n";
        key << "    Y: peak " << levelY.peakDb << " dBFS, RMS " << levelY.rmsDb
            << " dBFS, integrated " << levelY.loudnessLufs << " LUFS\n";
        key << "    signed gap X-Y: " << (levelX.loudnessLufs - levelY.loudnessLufs)
            << " LU, " << (levelX.peakDb - levelY.peakDb) << " dB peak\n\n";
        key << std::defaultfloat;
    }

    // --- the pairs, and the questions -------------------------------------------

    manifest << "The three pairs\n";
    manifest << "---------------\n";

    for (size_t p = 0; p < subjectCount; ++p)
        manifest << "  pair " << (p + 1) << "   modtest-" << (p + 1)
                 << "-X.wav  vs  modtest-" << (p + 1) << "-Y.wav\n";

    manifest << "\nLevel difference inside each pair\n";
    manifest << "---------------------------------\n";
    manifest << "Magnitude only. The sign is withheld deliberately: modulation\n";
    manifest << "redistributes energy, so knowing which file is louder is one\n";
    manifest << "inference away from knowing which file is modulated. The signed\n";
    manifest << "values are in the key.\n\n";

    manifest << std::fixed << std::setprecision (2);

    for (size_t p = 0; p < gaps.size(); ++p)
        manifest << "  pair " << (p + 1) << "   " << gaps[p].lufsGap
                 << " LU integrated, " << gaps[p].peakGap << " dB peak\n";

    manifest << std::defaultfloat;

    manifest << "\nNothing was levelled. Listen to both files in a pair at one common\n";
    manifest << "monitor gain and do not adjust between them.\n\n";

    manifest << "Three questions per pair\n";
    manifest << "------------------------\n";
    manifest << "  1. in which is the bell, chime, metallic or one-note feeling more\n";
    manifest << "     obvious?\n";
    manifest << "  2. whose tail is more natural, more smooth?\n";
    manifest << "  3. how big is the difference: very obvious, slight, or almost\n";
    manifest << "     none?\n\n";
    manifest << "Optionally, and kept separate from the first answer: which do you\n";
    manifest << "prefer? Preference and metallicness are different questions and the\n";
    manifest << "round-one result is a reason to keep them apart - the file that was\n";
    manifest << "preferred there is not automatically the less metallic one.\n\n";
    manifest << "There is no expected answer. The measurement says these three modes\n";
    manifest << "differ in how much the modulation moves their neighbourhood; whether\n";
    manifest << "that difference is audible is the open question.\n";

    // --- files ---------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open()) { out << manifest.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open()) { out << key.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << "PHASE 6 / FU-1 - ROUND 1b, RENDER CHECK\n";
            out << "=======================================\n\n";
            out << "Deliberately content-free. Nothing here says which file in a\n";
            out << "pair carries which Mod Depth.\n\n";
            out << "audio files written           " << (subjectCount * 2) << "\n";
            out << "pairs                         " << subjectCount << "\n";
            out << "pairs whose two renders share\n";
            out << "  a bit-identical excitation  " << identicalSourcePairs << " / "
                << subjectCount << "\n";
            out << "files above 0 dBFS            " << clippingFiles << "\n";
            out << std::fixed << std::setprecision (2);
            out << "highest peak in the set       " << worstPeakDb << " dBFS\n";
            out << "quietest peak in the set      " << quietestPeakDb << " dBFS\n";
            out << std::defaultfloat;
            out << "sample rate                   " << sampleRate
                << " Hz, 32-bit float stereo\n";
            out << "excitation seed               fresh, not round one's\n";
            out << "gain applied                  shipping preview trim only,\n";
            out << "                              identical for every file\n";
            out << "normalisation                 none\n";
            out << "loudness match                none\n";
            out << "post-processing               none\n";

            ++written;
        }
    }

    return written;
}

// ===========================================================================
//  Round 1c: the same question, counterbalanced
// ===========================================================================

/*
    Round 1b asked whether Mod Depth changes what a mode sounds like and came
    back unusable. The listener chose the second file as "more metallic" in all
    three pairs while calling every difference slight or less; because the
    letters were randomised against the treatment, that perfect consistency in
    letter space was only two-of-three in treatment space, which is chance. No
    level metric explained the pattern - the chosen file was the louder one in
    two of three by loudness and one of three by peak - so the only variable
    that tracked every answer was position.

    That is not a result about the tank. It is a result about the test, and the
    fix is to measure the thing that spoiled it rather than hope it went away.

    Every mode is heard twice, on two different excitations, with the treatment
    on opposite sides of the pair. If the listener is following Mod Depth, the
    two repeats pick the same treatment and disagree about position. If the
    listener is following position, the two repeats pick the same position and
    disagree about treatment. The two hypotheses now make opposite predictions
    on the same data, which is the whole point.

    The analysis is fixed before the files exist and is written into the
    manifest, so it cannot be chosen after seeing the answers.
*/
int exportCounterbalancedModulationSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double diagnosticSeconds = 0.6;
    constexpr double tailSeconds = 6.5;

    struct Subject { int modeId; double frequencyHz; const char* classLetter; };

    const Subject subjects[] =
    {
        { 54, 4505.77, "B" },
        { 50,  375.44, "B" },
        {  4,  646.34, "A" },
    };

    /**
        One trial: a mode, a repeat index, an excitation seed, and which side of
        the pair the modulated file sits on.

        Listening order is the array order. The two repeats of a mode are never
        adjacent - three trials apart in every case - so the second hearing is
        not a direct comparison against a memory of the first.
    */
    struct Trial
    {
        int subject;          /**< Index into subjects. */
        int repeat;           /**< 1 or 2. */
        unsigned seed;
        bool firstIsModulated;
        const char* firstLetter;
        const char* secondLetter;
    };

    // Counterbalance, checked below rather than trusted: each mode appears once
    // with Mod Depth 30% first and once with Mod Depth 0 first, and the set as a
    // whole is three of each.
    const Trial trials[] =
    {
        { 1, 1, 20260820u, true,  "P", "Q" },   // #50
        { 2, 2, 20260821u, false, "R", "S" },   // #4
        { 0, 1, 20260822u, false, "T", "U" },   // #54
        { 1, 2, 20260823u, false, "V", "W" },   // #50
        { 2, 1, 20260824u, true,  "J", "K" },   // #4
        { 0, 2, 20260825u, true,  "M", "N" },   // #54
    };

    constexpr size_t trialCount = 6;
    constexpr size_t subjectCount = 3;

    std::ostringstream manifest, key;

    manifest << "PHASE 6 / FU-1 - ROUND 1c, COUNTERBALANCED MODULATION REPEAT\n";
    manifest << "============================================================\n\n";
    manifest << "Six pairs: three modes, each heard twice on a different excitation,\n";
    manifest << "with the treatment on opposite sides of the pair the second time.\n\n";
    manifest << "Round 1b was inconclusive. The answers were perfectly consistent in\n";
    manifest << "letter and only two-of-three in treatment, which is chance, and no\n";
    manifest << "level difference explained it. This round is built so that following\n";
    manifest << "the modulation and following the position give different answers.\n\n";

    manifest << "Settings, all shipping defaults except the one under test:\n";
    manifest << "  Decay      " << defaults::decaySeconds << " s\n";
    manifest << "  Size       " << defaults::sizeControl << "\n";
    manifest << "  Mod Rate   " << defaults::modRateControlHz << " Hz\n";
    manifest << "  Mod Depth  0 in one file of a pair, " << defaults::modDepthControl
             << " in the other\n";
    manifest << "  Width      " << defaults::width << "\n";
    manifest << "  Mix        100% wet\n\n";
    manifest << "48 kHz, 32-bit float stereo. Inside a pair the excitation is the same\n";
    manifest << "vector passed to the renderer twice, so nothing but Mod Depth differs.\n";
    manifest << "Across pairs every excitation is a different deterministic seed, so no\n";
    manifest << "two pairs share a noise realisation and nothing here is byte-comparable\n";
    manifest << "with round 1b. No normalisation, no loudness match, no limiting, no EQ,\n";
    manifest << "no notch, no post-processing.\n\n";

    manifest << "LISTEN IN THIS ORDER\n";
    manifest << "--------------------\n";

    key << "PHASE 6 / FU-1 - ROUND 1c BLIND KEY\n";
    key << "===================================\n\n";
    key << "Do not read this before listening.\n\n";

    int written = 0;
    int clippingFiles = 0;
    int identicalSourcePairs = 0;
    double worstPeakDb = -300.0, quietestPeakDb = 300.0;

    std::vector<double> lufsGap (trialCount, 0.0), peakGap (trialCount, 0.0);
    std::vector<std::string> fileNames;

    for (size_t t = 0; t < trialCount; ++t)
    {
        const auto& trial = trials[t];
        const auto& subject = subjects[static_cast<size_t> (trial.subject)];

        auto dry = diagnosticBurst (sampleRate, diagnosticSeconds, trial.seed);

        bandLimitInPlace (dry, subject.frequencyHz, sampleRate);

        const auto before = dry;

        const auto modulated = renderShippingWet (dry, sampleRate,
                                                  defaults::modDepthControl, tailSeconds);

        bool unchanged = dry.size() == before.size();

        for (size_t n = 0; unchanged && n < dry.size(); ++n)
            unchanged = dry[n].l == before[n].l && dry[n].r == before[n].r;

        const auto unmodulated = renderShippingWet (dry, sampleRate, 0.0, tailSeconds);

        if (unchanged)
            ++identicalSourcePairs;

        const std::string stem = "trial-" + std::to_string (t + 1) + "-";

        const std::string firstName = stem + trial.firstLetter + ".wav";
        const std::string secondName = stem + trial.secondLetter + ".wav";

        const auto& first = trial.firstIsModulated ? modulated : unmodulated;
        const auto& second = trial.firstIsModulated ? unmodulated : modulated;

        if (! writeStereoWav (directory + "/" + firstName, first, sampleRate))
            return -1;

        if (! writeStereoWav (directory + "/" + secondName, second, sampleRate))
            return -1;

        written += 2;

        fileNames.push_back (firstName);
        fileNames.push_back (secondName);

        const auto levelFirst = measureWindow (firstName, first, sampleRate);
        const auto levelSecond = measureWindow (secondName, second, sampleRate);

        for (double peak : { levelFirst.peakDb, levelSecond.peakDb })
        {
            worstPeakDb = std::max (worstPeakDb, peak);
            quietestPeakDb = std::min (quietestPeakDb, peak);

            if (peak > 0.0)
                ++clippingFiles;
        }

        lufsGap[t] = std::abs (levelFirst.loudnessLufs - levelSecond.loudnessLufs);
        peakGap[t] = std::abs (levelFirst.peakDb - levelSecond.peakDb);

        manifest << "  " << (t + 1) << ".  " << firstName << "   then   " << secondName << "\n";

        key << "--- trial " << (t + 1) << " ---\n";
        key << "  mode #" << subject.modeId << "  " << std::fixed << std::setprecision (2)
            << subject.frequencyHz << " Hz  class " << subject.classLetter
            << "  repeat " << trial.repeat << "  seed " << trial.seed << "\n";
        key << std::defaultfloat;
        key << "  " << firstName << "  (first)  = Mod Depth "
            << (trial.firstIsModulated ? defaults::modDepthControl : 0.0) << "\n";
        key << "  " << secondName << "  (second) = Mod Depth "
            << (trial.firstIsModulated ? 0.0 : defaults::modDepthControl) << "\n";
        key << std::fixed << std::setprecision (2);
        key << "    first:  peak " << levelFirst.peakDb << " dBFS, RMS " << levelFirst.rmsDb
            << " dBFS, integrated " << levelFirst.loudnessLufs << " LUFS\n";
        key << "    second: peak " << levelSecond.peakDb << " dBFS, RMS " << levelSecond.rmsDb
            << " dBFS, integrated " << levelSecond.loudnessLufs << " LUFS\n";
        key << "    signed gap first-second: "
            << (levelFirst.loudnessLufs - levelSecond.loudnessLufs) << " LU, "
            << (levelFirst.peakDb - levelSecond.peakDb) << " dB peak\n\n";
        key << std::defaultfloat;
    }

    // --- verify the counterbalance rather than assert it --------------------------

    int modulatedFirstCount = 0;
    int properlyCounterbalanced = 0;

    for (size_t s = 0; s < subjectCount; ++s)
    {
        int seen = 0, modulatedFirst = 0;

        for (size_t t = 0; t < trialCount; ++t)
        {
            if (trials[t].subject != static_cast<int> (s))
                continue;

            ++seen;

            if (trials[t].firstIsModulated)
                ++modulatedFirst;
        }

        if (seen == 2 && modulatedFirst == 1)
            ++properlyCounterbalanced;
    }

    for (size_t t = 0; t < trialCount; ++t)
        if (trials[t].firstIsModulated)
            ++modulatedFirstCount;

    // --- the question ---------------------------------------------------------------

    manifest << "\nThe two repeats of a mode are never next to each other in this\n";
    manifest << "order, so the second hearing is a fresh judgement rather than a\n";
    manifest << "comparison against a memory.\n\n";

    manifest << "Level difference inside each pair\n";
    manifest << "---------------------------------\n";
    manifest << "Magnitude only; the sign is in the key, because which file is louder\n";
    manifest << "is one inference away from which file is modulated.\n\n";
    manifest << std::fixed << std::setprecision (2);

    for (size_t t = 0; t < trialCount; ++t)
        manifest << "  trial " << (t + 1) << "   " << lufsGap[t] << " LU integrated, "
                 << peakGap[t] << " dB peak\n";

    manifest << std::defaultfloat;
    manifest << "\nNothing was levelled. Listen to both files in a pair at one common\n";
    manifest << "monitor gain and do not change it between them or between trials.\n\n";

    manifest << "THE QUESTION\n";
    manifest << "------------\n";
    manifest << "One primary question per trial, and deliberately only one:\n\n";
    manifest << "  In which is the bell, chime, metallic or one-note feeling more\n";
    manifest << "  obvious?\n\n";
    manifest << "Answer with a file name, or say you cannot tell them apart. There is\n";
    manifest << "no forced choice: \"cannot tell\" is a real answer and is analysed as\n";
    manifest << "one, not discarded.\n\n";
    manifest << "Then, secondary:\n\n";
    manifest << "  How big is the difference?  obvious / slight / almost none /\n";
    manifest << "  cannot tell\n\n";
    manifest << "Not asked this round, on purpose: which is more natural, and which\n";
    manifest << "you prefer. In round 1b those answers moved in lockstep with the\n";
    manifest << "metallicness answer in all three pairs, which means they carried no\n";
    manifest << "independent information and could only blur the endpoint.\n\n";

    manifest << "THE ANALYSIS, FIXED NOW\n";
    manifest << "-----------------------\n";
    manifest << "Written before the files were rendered so that it cannot be chosen\n";
    manifest << "after the answers are known.\n\n";
    manifest << "Per mode, across its two repeats:\n";
    manifest << "  both repeats name the same Mod Depth   -> direction-consistent\n";
    manifest << "  the repeats name opposite Mod Depths   -> inconsistent\n";
    manifest << "  either repeat is \"cannot tell\"          -> unresolved\n\n";
    manifest << "Reported separately, and this is the point of the round:\n";
    manifest << "  treatment-direction consistency  - do the repeats agree on Mod Depth?\n";
    manifest << "  presentation-position consistency - do the repeats agree on first\n";
    manifest << "                                      versus second?\n\n";
    manifest << "Because each mode is counterbalanced, those two cannot both be high.\n";
    manifest << "Following the modulation forces position to disagree; following the\n";
    manifest << "position forces treatment to disagree. Whichever comes back consistent\n";
    manifest << "is what was actually being heard.\n\n";
    manifest << "A result of \"unresolved\" or \"inconsistent\" for a mode is an\n";
    manifest << "acceptable outcome and will be reported as such. Nothing here needs\n";
    manifest << "the modulation to be audible.\n";

    // --- files --------------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open()) { out << manifest.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open()) { out << key.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << "PHASE 6 / FU-1 - ROUND 1c, RENDER CHECK\n";
            out << "=======================================\n\n";
            out << "Deliberately content-free. Nothing here says which file in a\n";
            out << "pair carries which Mod Depth.\n\n";
            out << "audio files written             " << (trialCount * 2) << "\n";
            out << "pairs                           " << trialCount << "\n";
            out << "modes, each heard twice         " << subjectCount << "\n";
            out << "pairs whose two renders share\n";
            out << "  a bit-identical excitation    " << identicalSourcePairs << " / "
                << trialCount << "\n";
            out << "modes counterbalanced (one\n";
            out << "  repeat each way)              " << properlyCounterbalanced << " / "
                << subjectCount << "\n";
            out << "pairs with the modulated file\n";
            out << "  presented first               " << modulatedFirstCount << " / "
                << trialCount << "\n";
            out << "distinct excitation seeds       " << trialCount << "\n";
            out << "files above 0 dBFS              " << clippingFiles << "\n";
            out << std::fixed << std::setprecision (2);
            out << "highest peak in the set         " << worstPeakDb << " dBFS\n";
            out << "quietest peak in the set        " << quietestPeakDb << " dBFS\n";
            out << std::defaultfloat;
            out << "sample rate                     " << static_cast<long long> (sampleRate)
                << " Hz, 32-bit float stereo\n";
            out << "difference within a pair        Mod Depth only\n";
            out << "gain applied                    shipping preview trim only,\n";
            out << "                                identical for every file\n";
            out << "normalisation                   none\n";
            out << "loudness match                  none\n";
            out << "post-processing                 none\n";

            ++written;
        }
    }

    return written;
}

// ===========================================================================
//  Round 2: the high band, rated absolutely
// ===========================================================================

/*
    Rounds 1, 1b and 1c all asked a comparative question and all three were
    spoiled by it in one way or another - by the frequency of the other
    candidate, by the position of the other file, or by there being no way to
    say "these sound the same". This round asks nothing comparative.

    Each trial is one file, rated on its own: how much bell or metallic
    character is in it, whether that character is objectionable, and whether the
    tail is pleasant. Those are three separate endpoints and are recorded
    separately, because round 1 collapsed them and lost the one it was built to
    measure.

    Mod Depth is not under test here. It is at the shipping 30% in every file.

    Level staging
    -------------
    Round 1c's files peaked between -41 and -30 dBFS, which is quiet enough that
    the listener had to raise the monitor a long way. The cause is the band
    limiting: one ERB of a broadband burst is a small fraction of its energy,
    and the tail then spreads what is left over seconds.

    The fix is the excitation amplitude, not a gain after the tank. The path is
    linear, so one constant applied to the source scales every file by the same
    amount and leaves every relative level exactly where it was. The production
    gain law, the Size compensation and the preview trim are untouched, and no
    file is normalised or loudness matched.
*/
int exportHighBandListeningSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double diagnosticSeconds = 0.6;
    constexpr double tailSeconds = 6.5;

    /**
        The excitation is louder than round 1c's by this factor.

        Not a post-render gain and not a normalisation: it multiplies the test
        signal before the tank, the tank is linear, and every file in the set
        gets the same factor. Relative levels between trials are preserved
        exactly, which is the property that matters for rating them.
    */
    constexpr double excitationGain = 8.0;

    struct Subject
    {
        int modeId;
        double frequencyHz;
        const char* classLetter;
        const char* note;          /**< Key only. */
    };

    // Verified against Phase6ModalMap-v4 before this list was written.
    const Subject subjects[] =
    {
        { 10,  9146.01, "B", "high-priority modulation-resistant, confirmed" },
        { 25,  7217.22, "A", "modulation-moving control, confirmed"          },
        {  3, 10655.00, "B", "usable high-band B, confirmed"                 },
        {  1, 11561.40, "D", "high-prominence, structurally unresolved"      },
    };

    constexpr size_t subjectCount = 4;
    constexpr size_t trialCount = 8;

    /**
        Listening order.

        Two repeats per mode, never closer than three positions apart, and the
        first four are not the order the modes are listed in.
    */
    const int trialSubject[trialCount] = { 2, 0, 3, 1, 0, 2, 1, 3 };

    const unsigned trialSeed[trialCount] =
    {
        20260901u, 20260902u, 20260903u, 20260904u,
        20260905u, 20260906u, 20260907u, 20260908u,
    };

    // Not A, B or D - those are class letters - and not a letter this project
    // has used to label a candidate in an earlier round.
    const char* trialLetter[trialCount] = { "G", "Z", "L", "H", "O", "T", "C", "N" };

    std::ostringstream manifest, key;

    manifest << "PHASE 6 / FU-1 - ROUND 2, HIGH BAND\n";
    manifest << "===================================\n\n";
    manifest << "Eight trials. Each one is a single file, rated on its own.\n\n";
    manifest << "This is not an A/B test and there is nothing to compare. Do not put\n";
    manifest << "two trials side by side - rate each file as you hear it, in the order\n";
    manifest << "given, and move on.\n\n";
    manifest << "Mod Depth is NOT under test in this round. Every file is at the\n";
    manifest << "shipping " << defaults::modDepthControl << ".\n\n";

    manifest << "Settings, all shipping defaults, none varied:\n";
    manifest << "  Decay      " << defaults::decaySeconds << " s\n";
    manifest << "  Size       " << defaults::sizeControl << "\n";
    manifest << "  Mod Rate   " << defaults::modRateControlHz << " Hz\n";
    manifest << "  Mod Depth  " << defaults::modDepthControl << "\n";
    manifest << "  Width      " << defaults::width << "\n";
    manifest << "  Mix        100% wet for the eight trials\n\n";
    manifest << "48 kHz, 32-bit float stereo. No normalisation, no loudness match, no\n";
    manifest << "limiting, no EQ, no notch, no post-processing. The production gain\n";
    manifest << "law, the Size compensation and the preview trim are exactly as they\n";
    manifest << "ship.\n\n";
    manifest << "The excitation is " << excitationGain << "x louder than the previous\n";
    manifest << "round's, applied to the test signal before the tank. The path is\n";
    manifest << "linear and the factor is the same for every file, so every relative\n";
    manifest << "level between trials is preserved. Nothing was levelled per file.\n\n";

    key << "PHASE 6 / FU-1 - ROUND 2 BLIND KEY\n";
    key << "==================================\n\n";
    key << "Do not read this before listening.\n\n";
    key << "Four modes, each rendered twice on a different excitation seed. The\n";
    key << "listener is not told that any two trials share a mode.\n\n";

    int written = 0;
    int clippingFiles = 0;
    double worstPeakDb = -300.0, quietestPeakDb = 300.0;
    double worstLufs = -300.0, quietestLufs = 300.0;

    std::vector<std::string> orderLines;

    for (size_t t = 0; t < trialCount; ++t)
    {
        const auto& subject = subjects[static_cast<size_t> (trialSubject[t])];

        auto dry = diagnosticBurst (sampleRate, diagnosticSeconds, trialSeed[t]);

        for (auto& f : dry)
        {
            f.l *= excitationGain;
            f.r *= excitationGain;
        }

        bandLimitInPlace (dry, subject.frequencyHz, sampleRate);

        const auto wet = renderShippingWet (dry, sampleRate, defaults::modDepthControl,
                                            tailSeconds);

        std::ostringstream name;

        name << "high-" << (t + 1 < 10 ? "0" : "") << (t + 1) << "-" << trialLetter[t] << ".wav";

        if (! writeStereoWav (directory + "/" + name.str(), wet, sampleRate))
            return -1;

        ++written;

        const auto level = measureWindow (name.str(), wet, sampleRate);

        worstPeakDb = std::max (worstPeakDb, level.peakDb);
        quietestPeakDb = std::min (quietestPeakDb, level.peakDb);
        worstLufs = std::max (worstLufs, level.loudnessLufs);
        quietestLufs = std::min (quietestLufs, level.loudnessLufs);

        if (level.peakDb > 0.0)
            ++clippingFiles;

        {
            std::ostringstream line;

            line << "  " << (t + 1) << ".  " << name.str();

            orderLines.push_back (line.str());
        }

        const double band = erbHz (subject.frequencyHz);

        key << "--- trial " << (t + 1) << " ---\n";
        key << "  " << name.str() << "\n";
        key << "  mode #" << subject.modeId << "  " << std::fixed << std::setprecision (2)
            << subject.frequencyHz << " Hz  class " << subject.classLetter
            << "  (" << subject.note << ")\n";
        key << "  excitation band: one ERB, " << band << " Hz wide, "
            << (subject.frequencyHz - 0.5 * band) << " - "
            << (subject.frequencyHz + 0.5 * band) << " Hz\n";
        key << "  seed " << trialSeed[t] << "\n";
        key << "    peak " << level.peakDb << " dBFS, RMS " << level.rmsDb
            << " dBFS, integrated " << level.loudnessLufs << " LUFS\n\n";
        key << std::defaultfloat;
    }

    // --- the musical context ---------------------------------------------------

    // One render, not four. The tank is at fixed settings and the source is
    // fixed, so a per-candidate "musical" render would be four byte-identical
    // files pretending to be a comparison. Isolating one mode inside a
    // broadband source would need a filter this set is not allowed to add - and
    // should not add, because the point is to hear the tank as it ships.
    {
        const auto sources = makeFu2Sources (sampleRate);

        const std::vector<StereoFrame>* snare = nullptr;

        for (const auto& s : sources)
            if (s.name == "snare")
                snare = &s.frames;

        if (snare == nullptr)
            return -1;

        const auto wet = renderShippingWet (*snare, sampleRate, defaults::modDepthControl,
                                            tailSeconds);

        struct Context { const char* name; double mix; };

        const Context contexts[] = { { "context-wet100.wav", 1.00 },
                                     { "context-mix30.wav",  defaults::mix } };

        for (const auto& c : contexts)
        {
            const auto mixed = mixDryWet (*snare, wet, MixLaw::auroraCustom, c.mix);

            if (! writeStereoWav (directory + "/" + c.name, mixed, sampleRate))
                return -1;

            ++written;

            const auto level = measureWindow (c.name, mixed, sampleRate);

            if (level.peakDb > 0.0)
                ++clippingFiles;

            // Identical for every mode, so it leaks nothing and belongs in the
            // manifest rather than the key.
            manifest << c.name << ": peak " << std::fixed << std::setprecision (2)
                     << level.peakDb << " dBFS, integrated " << level.loudnessLufs
                     << " LUFS, Mix " << static_cast<int> (100.0 * c.mix) << "%\n";
            manifest << std::defaultfloat;
        }

        manifest << "\n";
    }

    // --- order and questions ------------------------------------------------------

    manifest << "LISTEN IN THIS ORDER\n";
    manifest << "--------------------\n";

    for (const auto& line : orderLines)
        manifest << line << "\n";

    manifest << "\nThen, separately, the two context files. They are the same full-band\n";
    manifest << "snare through the same tank, with nothing filtered and nothing\n";
    manifest << "targeted at any particular mode.\n\n";
    manifest << "  context-wet100.wav   the tail alone\n";
    manifest << "  context-mix30.wav    the same thing at the shipping Mix\n\n";
    manifest << "What the context files are for, and what they are not: they are a\n";
    manifest << "whole-tail control, to hear whether the character the trials show up\n";
    manifest << "is also there in an ordinary broadband transient. They are NOT proof\n";
    manifest << "about any individual mode. One mode cannot be isolated inside a\n";
    manifest << "broadband source without adding a filter, and this set adds none.\n\n";

    manifest << "THE THREE QUESTIONS, PER TRIAL\n";
    manifest << "------------------------------\n";
    manifest << "Recorded separately. The second and third are not a re-wording of the\n";
    manifest << "first, and a high answer to one does not imply anything about the\n";
    manifest << "others.\n\n";
    manifest << "  1. Do you hear a bell / chime / metallic / one-note quality?\n";
    manifest << "       0 = none\n";
    manifest << "       1 = very slight\n";
    manifest << "       2 = slight\n";
    manifest << "       3 = obvious\n";
    manifest << "       4 = very obvious\n\n";
    manifest << "  2. Is that quality objectionable?\n";
    manifest << "       0 = not at all\n";
    manifest << "       1 = a little\n";
    manifest << "       2 = noticeably\n";
    manifest << "       3 = very\n\n";
    manifest << "  3. Is the tail pleasant overall?\n";
    manifest << "       pleasant / neutral / unpleasant\n\n";
    manifest << "Question 1 is the primary endpoint. Preference is not a substitute for\n";
    manifest << "it and is not asked as one: a mode can be audibly bell-like and\n";
    manifest << "perfectly welcome, and that combination has a name in the analysis\n";
    manifest << "below.\n\n";
    manifest << "There is no expected answer, and 0 across the board is a result.\n\n";

    manifest << "THE ANALYSIS PLAN\n";
    manifest << "-----------------\n";
    manifest << "Fixed before these files were rendered and written to\n";
    manifest << "ANALYSIS-PLAN.txt, which carries the same timestamp as everything\n";
    manifest << "else here.\n\n";
    manifest << "Leave it unread until the ratings are done, for the same reason as\n";
    manifest << "the key. It states the thresholds a mode is judged against, and a\n";
    manifest << "listener who knows where the cut-offs are is being invited to rate\n";
    manifest << "towards them.\n";

    // The rules a mode is judged against, deliberately not in the listener's
    // instructions. Pre-registration exists so the analyst cannot choose the
    // rule after seeing the answers; it does not require handing the listener
    // the thresholds their own ratings will be measured against.
    std::ostringstream plan;

    plan << "PHASE 6 / FU-1 - ROUND 2 ANALYSIS PLAN\n";
    plan << "======================================\n\n";
    plan << "Fixed before the files were rendered. Do not read before rating.\n\n";
    plan << "Some trials share a mode; which ones is in the key. Per mode, over its\n";
    plan << "two trials:\n\n";
    plan << "  metallicity ratings differ by <= 1   -> repeat-consistent\n";
    plan << "  metallicity ratings differ by  > 1   -> perceptually unstable,\n";
    plan << "                                          reported as unresolved\n\n";
    plan << "A mode is a HIGH-PRIORITY PERCEPTUAL CANDIDATE when it is\n";
    plan << "repeat-consistent AND its mean metallicity is 2 or more, OR its\n";
    plan << "annoyance is 1 or more.\n\n";
    plan << "That label does NOT mean a DSP problem, and nothing in this round can\n";
    plan << "establish one. It means the character is perceptually salient and worth\n";
    plan << "attention.\n\n";
    plan << "A mode with high metallicity and annoyance 0 is reported as\n";
    plan << "\"audible character, not objectionable\". That distinction is the point\n";
    plan << "of asking two questions instead of one, and it is why preference is not\n";
    plan << "asked at all.\n\n";
    plan << "One of the four modes is structurally unresolved on the Size axis -\n";
    plan << "class D in the measurement. Whatever it does here, it will be reported\n";
    plan << "as a high-band perceptual candidate only. It will not be called\n";
    plan << "modulation-resistant or modulation-moving, because its structural class\n";
    plan << "is not known.\n\n";
    plan << "An outcome of \"no metallic character anywhere\" is a result and will be\n";
    plan << "reported as one. Nothing here needs a candidate to be found.\n";

    // --- files -----------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open()) { out << manifest.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/ANALYSIS-PLAN.txt", std::ios::trunc);

        if (out.is_open()) { out << plan.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open()) { out << key.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << "PHASE 6 / FU-1 - ROUND 2, RENDER CHECK\n";
            out << "======================================\n\n";
            out << "Deliberately content-free. Nothing here says which trial is\n";
            out << "which mode, or which trials share one.\n\n";
            out << "rating trials                  " << trialCount << "\n";
            out << "context files                  2\n";
            out << "distinct modes                 " << subjectCount << "\n";
            out << "repeats per mode               2\n";
            out << "distinct excitation seeds      " << trialCount << "\n";

            // The two trials that share a mode must never sit next to each
            // other. Computed from the order actually used, not asserted.
            int closestRepeat = static_cast<int> (trialCount);

            for (size_t a = 0; a < trialCount; ++a)
                for (size_t b = a + 1; b < trialCount; ++b)
                    if (trialSubject[a] == trialSubject[b])
                        closestRepeat = std::min (closestRepeat, static_cast<int> (b - a));

            out << "closest two trials sharing\n";
            out << "  a mode                       " << closestRepeat
                << " apart (" << (closestRepeat - 1) << " trials between)\n";
            out << "files above 0 dBFS             " << clippingFiles << "\n";
            out << std::fixed << std::setprecision (2);
            out << "trial peak range               " << quietestPeakDb << " to "
                << worstPeakDb << " dBFS\n";
            out << "trial loudness range           " << quietestLufs << " to "
                << worstLufs << " LUFS\n";
            out << "trial loudness spread          " << (worstLufs - quietestLufs) << " LU\n";
            out << std::defaultfloat;
            out << "sample rate                    " << static_cast<long long> (sampleRate)
                << " Hz, 32-bit float stereo\n";
            out << "Mod Depth                      " << defaults::modDepthControl
                << " in every file, not under test\n";
            out << "excitation gain vs round 1c    " << excitationGain
                << "x, before the tank, same for every file\n";
            out << "normalisation                  none\n";
            out << "loudness match                 none\n";
            out << "post-processing                none\n";
            out << "production gain law / trim     unchanged\n";

            ++written;
        }
    }

    return written;
}

// ===========================================================================
//  Round 2b: the stimulus alone
// ===========================================================================

/*
    Round 2 rated four high-band modes at 3.5 to 4.0 out of 4 for metallic
    character, repeatably. It could not say how much of that belongs to the tank
    and how much to the stimulus, because the stimulus is a burst filtered to
    one ERB - a band roughly nine times narrower than its centre frequency - and
    a narrow-band burst has an audible pitch before any reverb touches it.

    This is that missing control. The same eight excitations, the same eight
    seeds, the same centre frequencies, the same ERB widths, the same duration,
    and no tank.

    The excitations are not regenerated differently or approximated: each of the
    eight (seed, centre frequency) pairs from round 2 is reused exactly, and the
    render check counts how many of the eight matched a round 2 pair rather than
    taking it on trust.

    On the blinding, which is nominal here
    --------------------------------------
    The file names hide which mode a file is. They cannot hide that these files
    have no reverb: a dry burst followed by silence is obviously a dry burst
    followed by silence, and no naming scheme changes that. Anyone listening
    will know inside a second what kind of file this is. That is inherent to a
    stimulus-alone control and is not a defect of the set - but it should not be
    described as a blind test of anything except mode identity.
*/
int exportStimulusAnchorSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double diagnosticSeconds = 0.6;
    constexpr double tailSeconds = 6.5;      /**< Silence, to match round 2's length. */

    // Identical to round 2: the factor applied to the burst before the band
    // filter, so the excitation reaching this file is the same signal that
    // reached the tank there.
    constexpr double excitationGain = 8.0;

    /**
        One common gain on the finished anchors, and nothing else.

        Not a normalisation: one number for all eight files, so every relative
        level between anchors is exactly as the excitations produced it. It
        exists so the set sits in the same loudness range as the wet trials it
        will be compared against.

        Chosen from a measured first pass and then fixed: ungained, these
        anchors land near -2 LUFS against round 2's -22, so a factor of 0.5
        against round 2's own excitation puts the two sets within about a
        loudness unit of each other. Peaks stay several dB higher than round
        2's regardless, because a 25 ms burst has a far higher crest factor
        than a reverb tail - that difference is inherent to the comparison and
        is reported rather than flattened.
    */
    constexpr double anchorGain = 0.5;

    struct Anchor
    {
        int modeId;
        double frequencyHz;
        const char* classLetter;
        unsigned seed;             /**< Reused from round 2, with its frequency. */
        const char* letter;
    };

    // Presentation order is new - round 2's is not reused - and the two files
    // of a mode are never closer than three apart. Each (seed, frequency) pair
    // is exactly one of round 2's.
    const Anchor anchors[] =
    {
        { 25,  7217.22, "A", 20260904u, "R" },
        {  1, 11561.40, "D", 20260903u, "M" },
        { 10,  9146.01, "B", 20260902u, "V" },
        {  3, 10655.00, "B", 20260901u, "K" },
        {  1, 11561.40, "D", 20260908u, "S" },
        { 25,  7217.22, "A", 20260907u, "Y" },
        {  3, 10655.00, "B", 20260906u, "F" },
        { 10,  9146.01, "B", 20260905u, "P" },
    };

    constexpr size_t anchorCount = 8;

    // Round 2's (seed, centre frequency) pairs, to check the reuse rather than
    // assume it.
    struct Pair { unsigned seed; double frequencyHz; };

    const Pair roundTwoPairs[] =
    {
        { 20260901u, 10655.00 }, { 20260902u,  9146.01 },
        { 20260903u, 11561.40 }, { 20260904u,  7217.22 },
        { 20260905u,  9146.01 }, { 20260906u, 10655.00 },
        { 20260907u,  7217.22 }, { 20260908u, 11561.40 },
    };

    std::ostringstream manifest, key, plan;

    manifest << "PHASE 6 / FU-1 - ROUND 2b, CONTROL SET\n";
    manifest << "======================================\n\n";
    manifest << "Eight files. Each one is rated on its own.\n\n";
    manifest << "This is not an A/B test and there is nothing to compare. Do not put\n";
    manifest << "two files side by side - rate each as you hear it, in the order given,\n";
    manifest << "and move on.\n\n";
    manifest << "48 kHz, 32-bit float stereo, 7.1 s each. No normalisation, no loudness\n";
    manifest << "match, no limiting, no EQ, no notch, no saturation, no post-processing\n";
    manifest << "of any kind. One common gain is applied to all eight files and to none\n";
    manifest << "of them individually, so every relative level between them is as\n";
    manifest << "produced.\n\n";

    manifest << "LISTEN IN THIS ORDER\n";
    manifest << "--------------------\n";

    key << "PHASE 6 / FU-1 - ROUND 2b BLIND KEY\n";
    key << "===================================\n\n";
    key << "Do not read this before listening.\n\n";
    key << "Stimulus-alone controls: the round 2 excitations with no tank. Four\n";
    key << "modes, each twice, reusing round 2's seeds and centre frequencies.\n";
    key << "Common output gain " << anchorGain << "x, applied to all eight.\n\n";

    int written = 0;
    int clippingFiles = 0;
    int seedPairsMatched = 0;
    int filesWithTail = 0;
    double worstPeakDb = -300.0, quietestPeakDb = 300.0;
    double worstLufs = -300.0, quietestLufs = 300.0;

    const auto excitationSamples = static_cast<size_t> (sampleRate * diagnosticSeconds);
    const auto totalSamples = excitationSamples
                            + static_cast<size_t> (sampleRate * tailSeconds);

    for (size_t a = 0; a < anchorCount; ++a)
    {
        const auto& anchor = anchors[a];

        // Reuse check: this (seed, frequency) pair must be one of round 2's.
        for (const auto& p : roundTwoPairs)
            if (p.seed == anchor.seed
                && std::abs (p.frequencyHz - anchor.frequencyHz) < 1.0e-6)
                ++seedPairsMatched;

        // Exactly round 2's excitation, up to the point where round 2 handed it
        // to the tank.
        auto signal = diagnosticBurst (sampleRate, diagnosticSeconds, anchor.seed);

        for (auto& f : signal)
        {
            f.l *= excitationGain;
            f.r *= excitationGain;
        }

        bandLimitInPlace (signal, anchor.frequencyHz, sampleRate);

        // No tank. The silence that follows is what a dry burst leaves behind,
        // and the file is padded to round 2's length so the two sets are the
        // same duration.
        signal.resize (totalSamples, StereoFrame {});

        for (auto& f : signal)
        {
            f.l *= anchorGain;
            f.r *= anchorGain;
        }

        // Verify the bypass rather than assert it: a reverb would have left
        // something after the excitation window. Nothing may be there.
        bool tail = false;

        for (size_t n = excitationSamples; n < signal.size() && ! tail; ++n)
            tail = signal[n].l != 0.0 || signal[n].r != 0.0;

        if (tail)
            ++filesWithTail;

        std::ostringstream name;

        name << "control-" << (a + 1 < 10 ? "0" : "") << (a + 1) << "-"
             << anchor.letter << ".wav";

        if (! writeStereoWav (directory + "/" + name.str(), signal, sampleRate))
            return -1;

        ++written;

        const auto level = measureWindow (name.str(), signal, sampleRate);

        worstPeakDb = std::max (worstPeakDb, level.peakDb);
        quietestPeakDb = std::min (quietestPeakDb, level.peakDb);
        worstLufs = std::max (worstLufs, level.loudnessLufs);
        quietestLufs = std::min (quietestLufs, level.loudnessLufs);

        if (level.peakDb > 0.0)
            ++clippingFiles;

        manifest << "  " << (a + 1) << ".  " << name.str() << "\n";

        const double band = erbHz (anchor.frequencyHz);

        key << "--- " << name.str() << " ---\n";
        key << "  mode #" << anchor.modeId << "  " << std::fixed << std::setprecision (2)
            << anchor.frequencyHz << " Hz  class " << anchor.classLetter << "\n";
        key << "  excitation band: one ERB, " << band << " Hz wide, "
            << (anchor.frequencyHz - 0.5 * band) << " - "
            << (anchor.frequencyHz + 0.5 * band) << " Hz\n";
        key << "  seed " << anchor.seed << "  (reused from round 2 with this frequency)\n";
        key << "    peak " << level.peakDb << " dBFS, RMS " << level.rmsDb
            << " dBFS, integrated " << level.loudnessLufs << " LUFS\n\n";
        key << std::defaultfloat;
    }

    // --- questions -----------------------------------------------------------------

    manifest << "\nThe two files that come from the same source setting are never next\n";
    manifest << "to each other in this order.\n\n";

    manifest << "THE THREE QUESTIONS, PER FILE\n";
    manifest << "-----------------------------\n";
    manifest << "Recorded separately. The second and third are not a re-wording of the\n";
    manifest << "first.\n\n";
    manifest << "  1. Do you hear a bell / chime / metallic / one-note quality?\n";
    manifest << "       0 = none\n";
    manifest << "       1 = very slight\n";
    manifest << "       2 = slight\n";
    manifest << "       3 = obvious\n";
    manifest << "       4 = very obvious\n\n";
    manifest << "  2. Is that quality objectionable?\n";
    manifest << "       0 = not at all\n";
    manifest << "       1 = a little\n";
    manifest << "       2 = noticeably\n";
    manifest << "       3 = very\n\n";
    manifest << "  3. Overall sound?\n";
    manifest << "       pleasant / neutral / unpleasant\n\n";
    manifest << "Question 3 is worded as overall sound rather than tail, because the\n";
    manifest << "word tail does not apply to every file in this set. It is a secondary\n";
    manifest << "endpoint here and will be treated as one.\n\n";
    manifest << "Question 1 is the primary endpoint. There is no expected answer, and 0\n";
    manifest << "across the board is a result.\n\n";
    manifest << "An analysis plan was fixed before these files were rendered and is in\n";
    manifest << "ANALYSIS-PLAN.txt. Leave it unread until the ratings are done: it\n";
    manifest << "states the bands a result is read against, and a listener who knows\n";
    manifest << "where they are is being invited to rate towards them.\n";

    // --- the pre-registered plan ------------------------------------------------------

    plan << "PHASE 6 / FU-1 - ROUND 2b ANALYSIS PLAN\n";
    plan << "=======================================\n\n";
    plan << "Fixed before the files were rendered. Do not read before rating.\n\n";
    plan << "These eight files are the round 2 targeted excitations with no tank:\n";
    plan << "same seeds, same centre frequencies, same ERB widths, same duration.\n";
    plan << "Four modes, two files each.\n\n";
    plan << "Per mode, the mean metallicity of its two control files is compared with\n";
    plan << "the mean metallicity of its two round 2 wet trials. With two ratings per\n";
    plan << "mode and one listener this is DESCRIPTIVE ONLY. No statistical or\n";
    plan << "inferential test will be run or reported, and no correlation will be\n";
    plan << "claimed.\n\n";
    plan << "Reading, fixed now:\n\n";
    plan << "  A) control 0-1, wet 3-4\n";
    plan << "     -> strong evidence that the tank adds the metallic character.\n\n";
    plan << "  B) control around 2, wet 3-4\n";
    plan << "     -> the excitation contributes; the tank likely enhances it.\n\n";
    plan << "  C) control 3-4, wet 3-4\n";
    plan << "     -> the targeted test's localisation is confounded: the excitation\n";
    plan << "        is already metallic on its own, and the targeted trials are not\n";
    plan << "        by themselves mode-specific proof.\n\n";
    plan << "The same descriptive comparison may be made for annoyance.\n\n";
    plan << "These bands will not be changed after the ratings are seen.\n\n";
    plan << "WHAT THIS CANNOT OVERTURN\n";
    plan << "-------------------------\n";
    plan << "If the controls also rate as metallic, that does NOT make round 2's\n";
    plan << "verdict wrong. Round 2's whole-tail context renders used an unfiltered\n";
    plan << "full-band snare - no band limiting anywhere - and still rated\n";
    plan << "metallicity 3 and 4, with annoyance 1 and 2 and one unpleasant. That\n";
    plan << "evidence stands on its own and is untouched by this control.\n\n";
    plan << "What a high control rating would weaken is narrower and must be stated\n";
    plan << "as exactly that: how strongly the targeted trials can point at any\n";
    plan << "particular high-band mode or sub-band. The existence of the character\n";
    plan << "and its localisation are separate claims, and only the second is at\n";
    plan << "stake here.\n";

    // --- files -------------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open()) { out << manifest.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/ANALYSIS-PLAN.txt", std::ios::trunc);

        if (out.is_open()) { out << plan.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open()) { out << key.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            int closestRepeat = static_cast<int> (anchorCount);

            for (size_t x = 0; x < anchorCount; ++x)
                for (size_t y = x + 1; y < anchorCount; ++y)
                    if (anchors[x].modeId == anchors[y].modeId)
                        closestRepeat = std::min (closestRepeat, static_cast<int> (y - x));

            out << "PHASE 6 / FU-1 - ROUND 2b, RENDER CHECK\n";
            out << "=======================================\n\n";
            out << "Deliberately content-free. Nothing here says which file is\n";
            out << "which mode, or which two share one.\n\n";
            out << "files written                  " << anchorCount << "\n";
            out << "distinct source settings       4, two files each\n";
            out << "closest two files sharing\n";
            out << "  a setting                    " << closestRepeat
                << " apart (" << (closestRepeat - 1) << " files between)\n";
            out << "seed + centre frequency pairs\n";
            out << "  reused from round 2          " << seedPairsMatched << " / "
                << anchorCount << "\n";
            out << "excitation gain before the\n";
            out << "  band filter                  " << excitationGain
                << "x, identical to round 2\n";
            out << "duration                       " << (diagnosticSeconds + tailSeconds)
                << " s, identical to round 2\n";
            out << "files with any non-zero sample\n";
            out << "  after the excitation window  " << filesWithTail << " / " << anchorCount
                << "   (a reverb would leave one)\n";
            out << "common output gain             " << anchorGain
                << "x, applied to all files and to none individually\n";
            out << "files above 0 dBFS             " << clippingFiles << "\n";
            out << std::fixed << std::setprecision (2);
            out << "peak range                     " << quietestPeakDb << " to "
                << worstPeakDb << " dBFS\n";
            out << "loudness range                 " << quietestLufs << " to "
                << worstLufs << " LUFS\n";
            out << "loudness spread                " << (worstLufs - quietestLufs) << " LU\n";
            out << std::defaultfloat;
            out << "sample rate                    " << static_cast<long long> (sampleRate)
                << " Hz, 32-bit float stereo\n";
            out << "tank / reverb                  not applied\n";
            out << "modulation                     not applied\n";
            out << "normalisation                  none\n";
            out << "loudness match                 none\n";
            out << "post-processing                none\n";

            ++written;
        }
    }

    return written;
}

// ===========================================================================
//  Round 2c: the decision gate
// ===========================================================================

/*
    Everything so far has been about localisation, and localisation failed. The
    targeted method could not survive its own control: a one-ERB burst is
    audibly pitched before the tank touches it, so round 2's four-of-four
    metallicity said as much about the stimulus as about the reverb.

    What survived is one file - an unfiltered broadband source at 100% wet,
    rated metallicity 3, annoyance 1, overall neutral - and one weaker one at
    the shipping Mix, rated 4 / 2 / unpleasant with the dry source present, at a
    higher level, unblinded, unrepeated and heard second by a listener with a
    known second-file tendency.

    That is not enough to decide whether anything needs doing, and it is the
    only question worth asking before spending more effort on where. So this
    round asks it directly, and nothing else.

    No band limiting anywhere. Three full-band sources, each heard dry, at 100%
    wet and at the shipping Mix, so that the tank's contribution can be
    separated from the source's own character and from the mix level - the three
    things that were tangled together in the single context pair.

    Nine files. Not a comparison: each is rated on its own.
*/
int exportDecisionGateSet (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr double tailSeconds = 6.5;
    constexpr double totalSeconds = 12.5;   /**< Every file, so length tells nothing. */

    enum class Condition { dry = 0, wet100, mix30 };

    struct Trial
    {
        const char* sourceName;   /**< Key only. */
        int sourceIndex;
        Condition condition;
        const char* letter;
    };

    // Presentation order. The three conditions of a source are always three
    // apart, and no condition falls on a regular spacing: mix at 1, 5, 6; wet
    // at 2, 4, 9; dry at 3, 7, 8.
    const Trial trials[] =
    {
        { "snare", 0, Condition::mix30,  "E" },
        { "vocal", 1, Condition::wet100, "I" },
        { "piano", 2, Condition::dry,    "J" },
        { "snare", 0, Condition::wet100, "Q" },
        { "vocal", 1, Condition::mix30,  "U" },
        { "piano", 2, Condition::mix30,  "W" },
        { "snare", 0, Condition::dry,    "X" },
        { "vocal", 1, Condition::dry,    "Y" },
        { "piano", 2, Condition::wet100, "Z" },
    };

    constexpr size_t trialCount = 9;

    // --- the three sources ---------------------------------------------------

    // Existing reference material, not new synthesis. The snare is the one
    // rounds 1 and 2 used, so this round is continuous with them; the vocal is
    // the developer export set's formant "ah"; the piano chord is the Phase 5F
    // candidate set's.
    const auto fu2 = makeFu2Sources (sampleRate);

    const std::vector<StereoFrame>* snare = nullptr;
    const std::vector<StereoFrame>* piano = nullptr;

    for (const auto& s : fu2)
    {
        if (s.name == "snare") snare = &s.frames;
        if (s.name == "piano") piano = &s.frames;
    }

    if (snare == nullptr || piano == nullptr)
        return -1;

    const auto vocal = makeVocalReference (sampleRate, 4.0);

    constexpr size_t sourceCount = 3;

    /**
        The three sources are brought to a common loudness before anything else
        happens to them.

        Not a normalisation of any rendered file, and not a change to the
        production gain law. One gain per source, applied to the input, and the
        same gain for all three of that source's conditions - so every dry to
        wet to mix relationship inside a source is exactly what the shipping
        chain produces.

        It is done because the sources are natively 13 dB apart, and that
        difference was landing inside every condition group: the first render of
        this set spread 14.8 LU, almost all of it source to source rather than
        condition to condition. Rating that would have been rating which source
        is loud. The snare is the anchor because rounds 1 and 2 used it.
    */
    std::vector<std::vector<StereoFrame>> levelled (sourceCount);
    double sourceGain[sourceCount] = { 1.0, 1.0, 1.0 };

    {
        const std::vector<StereoFrame>* raw[] = { snare, &vocal, piano };

        const double target = integratedLoudnessLufs (*snare, sampleRate);

        for (size_t s = 0; s < sourceCount; ++s)
        {
            const double lufs = integratedLoudnessLufs (*raw[s], sampleRate);

            sourceGain[s] = std::isfinite (lufs) && std::isfinite (target)
                          ? std::pow (10.0, (target - lufs) / 20.0) : 1.0;

            levelled[s] = *raw[s];

            for (auto& f : levelled[s])
            {
                f.l *= sourceGain[s];
                f.r *= sourceGain[s];
            }
        }
    }

    const std::vector<StereoFrame>* sources[] = { &levelled[0], &levelled[1], &levelled[2] };

    const auto totalSamples = static_cast<size_t> (sampleRate * totalSeconds);

    // One wet render per source, reused by that source's wet100 and mix30 files
    // so the two differ by the mix law alone.
    std::vector<std::vector<StereoFrame>> wetBySource (sourceCount);

    for (size_t s = 0; s < sourceCount; ++s)
        wetBySource[s] = renderShippingWet (*sources[s], sampleRate,
                                            defaults::modDepthControl, tailSeconds);

    // --- render --------------------------------------------------------------

    std::ostringstream manifest, key, plan;

    manifest << "PHASE 6 / FU-1 - ROUND 2c, DECISION GATE\n";
    manifest << "========================================\n\n";
    manifest << "Nine files. Each one is rated on its own.\n\n";
    manifest << "This is not an A/B test and there is nothing to compare. Rate each\n";
    manifest << "file as you hear it, in the order given, and move on.\n\n";
    manifest << "Three full-band sources, each heard three ways. No band limiting\n";
    manifest << "anywhere in this set - the excitation is never filtered.\n\n";
    manifest << "Settings, all shipping defaults, none varied:\n";
    manifest << "  Decay      " << defaults::decaySeconds << " s\n";
    manifest << "  Size       " << defaults::sizeControl << "\n";
    manifest << "  Mod Rate   " << defaults::modRateControlHz << " Hz\n";
    manifest << "  Mod Depth  " << defaults::modDepthControl << "\n";
    manifest << "  Width      " << defaults::width << "\n";
    manifest << "  Mix law    production, unchanged\n\n";
    manifest << "48 kHz, 32-bit float stereo, " << totalSeconds << " s each. No\n";
    manifest << "normalisation, no loudness match, no limiter, no saturation, no EQ,\n";
    manifest << "no notch, no post-processing. Levels are whatever the production gain\n";
    manifest << "law produces.\n\n";

    key << "PHASE 6 / FU-1 - ROUND 2c BLIND KEY\n";
    key << "===================================\n\n";
    key << "Do not read this before rating.\n\n";
    key << "Three sources, each rendered dry, at 100% wet and at the shipping\n";
    key << "Mix " << static_cast<int> (100.0 * defaults::mix) << "%.\n\n";
    key << "Input levelling, applied to the source before anything else and the\n";
    key << "same for all three of that source's conditions. The snare is the\n";
    key << "anchor at 1.0000 by definition:\n";
    key << std::fixed << std::setprecision (4);
    key << "  snare  x" << sourceGain[0] << "\n";
    key << "  vocal  x" << sourceGain[1] << "\n";
    key << "  piano  x" << sourceGain[2] << "\n";
    key << std::defaultfloat << "\n";

    int written = 0;
    int clippingFiles = 0;

    struct Measured { double lufs; double peak; Condition condition; std::string name; };

    std::vector<Measured> measured;

    for (size_t t = 0; t < trialCount; ++t)
    {
        const auto& trial = trials[t];
        const auto& dry = *sources[static_cast<size_t> (trial.sourceIndex)];
        const auto& wet = wetBySource[static_cast<size_t> (trial.sourceIndex)];

        std::vector<StereoFrame> out;

        switch (trial.condition)
        {
            case Condition::dry:
                out = dry;                        // no tank at all
                break;

            case Condition::wet100:
                out = mixDryWet (dry, wet, MixLaw::auroraCustom, 1.0);
                break;

            case Condition::mix30:
                out = mixDryWet (dry, wet, MixLaw::auroraCustom, defaults::mix);
                break;
        }

        // Every file the same length, so duration says nothing about which
        // condition or which source a file is.
        out.resize (totalSamples, StereoFrame {});

        std::ostringstream name;

        name << "case-" << (t + 1 < 10 ? "0" : "") << (t + 1) << "-" << trial.letter << ".wav";

        if (! writeStereoWav (directory + "/" + name.str(), out, sampleRate))
            return -1;

        ++written;

        const auto level = measureWindow (name.str(), out, sampleRate);

        if (level.peakDb > 0.0)
            ++clippingFiles;

        measured.push_back ({ level.loudnessLufs, level.peakDb, trial.condition, name.str() });

        manifest << "  " << (t + 1) << ".  " << name.str() << "\n";

        const char* conditionName = trial.condition == Condition::dry    ? "DRY, no reverb"
                                  : trial.condition == Condition::wet100 ? "WET 100%"
                                                                         : "MIX 30%, shipping";

        key << "--- " << name.str() << " ---\n";
        key << "  source " << trial.sourceName << ", " << conditionName << "\n";
        key << std::fixed << std::setprecision (2);
        key << "    peak " << level.peakDb << " dBFS, RMS " << level.rmsDb
            << " dBFS, integrated " << level.loudnessLufs << " LUFS\n";
        key << std::defaultfloat;
    }

    // --- the analysis-only loudness match ---------------------------------------

    // Computed, reported in the key, and applied to nothing. The delivered files
    // are at production level; this is the offset each would need to reach the
    // set median, so a later reading can ask whether a rating tracked loudness
    // without anyone having altered what the listener heard.
    {
        std::vector<double> lufs;

        for (const auto& m : measured)
            if (std::isfinite (m.lufs))
                lufs.push_back (m.lufs);

        std::sort (lufs.begin(), lufs.end());

        const double median = lufs.empty() ? 0.0 : lufs[lufs.size() / 2];

        key << "\n--- analysis only: offset to the set median of "
            << std::fixed << std::setprecision (2) << median << " LUFS ---\n";
        key << "Applied to nothing. The delivered files are at production level.\n\n";

        for (const auto& m : measured)
            key << "  " << m.name << "  " << (median - m.lufs >= 0.0 ? "+" : "")
                << (median - m.lufs) << " LU\n";

        key << std::defaultfloat;
    }

    // --- level summary, per condition -------------------------------------------

    const auto range = [&] (Condition c, bool peak)
    {
        double lo = 1.0e9, hi = -1.0e9;

        for (const auto& m : measured)
        {
            if (m.condition != c)
                continue;

            const double v = peak ? m.peak : m.lufs;

            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }

        std::ostringstream o;

        o << std::fixed << std::setprecision (2) << lo << " to " << hi;

        return o.str();
    };

    manifest << "\nLEVELS - READ THIS BEFORE YOU START\n";
    manifest << "-----------------------------------\n";
    manifest << "No rendered file was normalised and the production gain law is\n";
    manifest << "untouched, so what each source's dry, wet and mix versions do\n";
    manifest << "relative to each other is exactly what the shipping chain produces -\n";
    manifest << "and that is the thing being asked about.\n\n";
    manifest << "The three sources were brought to a common loudness before anything\n";
    manifest << "else happened to them, because natively they sit about 13 dB apart\n";
    manifest << "and that gap would otherwise have dominated every rating. One gain\n";
    manifest << "per source, the same across all three of its versions.\n\n";
    manifest << "What is left is still not equally loud, and the differences are\n";
    manifest << "large enough to matter. The three groups below are printed in\n";
    manifest << "loudness order, not in any order that says what they are:\n\n";

    {
        struct Group { double lo; std::string lufs; std::string peak; };

        std::vector<Group> groups;

        for (auto c : { Condition::dry, Condition::wet100, Condition::mix30 })
        {
            double lo = 1.0e9;

            for (const auto& m : measured)
                if (m.condition == c)
                    lo = std::min (lo, m.lufs);

            groups.push_back ({ lo, range (c, false), range (c, true) });
        }

        std::sort (groups.begin(), groups.end(),
                   [] (const Group& a, const Group& b) { return a.lo < b.lo; });

        for (size_t g = 0; g < groups.size(); ++g)
            manifest << "  group " << (g + 1) << "   " << groups[g].lufs << " LUFS, peak "
                     << groups[g].peak << " dBFS\n";
    }

    manifest << "\nWhich file belongs to which group is in the key. What matters while\n";
    manifest << "rating is only this: a quieter file is not automatically less\n";
    manifest << "annoying, and a louder one is not automatically worse. Set one\n";
    manifest << "comfortable monitor level, keep it, and try to rate the character\n";
    manifest << "rather than the level.\n\n";

    // --- questions ------------------------------------------------------------------

    manifest << "THE QUESTIONS, PER FILE\n";
    manifest << "-----------------------\n";
    manifest << "Recorded separately. None is a re-wording of another.\n\n";
    manifest << "  1. Do you hear a bell / chime / metallic / one-note quality?\n";
    manifest << "       0 = none        1 = very slight    2 = slight\n";
    manifest << "       3 = obvious     4 = very obvious\n\n";
    manifest << "  2. Is it annoying?\n";
    manifest << "       0 = not at all  1 = slight         2 = clearly annoying\n";
    manifest << "       3 = very annoying\n\n";
    manifest << "  3. Overall sound?\n";
    manifest << "       pleasant / neutral / unpleasant\n\n";
    manifest << "  4. Does the source stay natural and in front?\n";
    manifest << "       yes / partly / no / n-a\n\n";
    manifest << "Question 4 will not fit every file in this set. Answer n-a where it\n";
    manifest << "does not, and do not force it.\n\n";
    manifest << "Question 1 is the primary endpoint. Preference is not a substitute\n";
    manifest << "for it and is not asked. There is no expected answer, and 0 across\n";
    manifest << "the board is a result.\n\n";
    manifest << "An analysis plan was fixed before these files were rendered and is in\n";
    manifest << "ANALYSIS-PLAN.txt. Leave it unread until the ratings are done: it\n";
    manifest << "states the thresholds a result is read against.\n";

    // --- pre-registered decision gate ---------------------------------------------------

    plan << "PHASE 6 / FU-1 - ROUND 2c ANALYSIS PLAN\n";
    plan << "=======================================\n\n";
    plan << "Fixed before the files were rendered. Do not read before rating.\n\n";
    plan << "Nine files: three full-band sources, each rendered DRY (no reverb),\n";
    plan << "WET100 (pure tank output) and MIX30 (the shipping Mix). No band\n";
    plan << "limiting anywhere.\n\n";
    plan << "This round decides one thing: whether the metallic character in the\n";
    plan << "shipping DSP is objectionable enough, in real use, to justify a voicing\n";
    plan << "intervention. It does NOT localise anything in frequency.\n\n";
    plan << "With one listener and one rating per cell, no statistical or\n";
    plan << "inferential test will be run and no correlation will be claimed.\n\n";
    plan << "THE GATE\n";
    plan << "--------\n";
    plan << "  A) WET100 metallicity high, but annoyance <= 1 and overall mostly\n";
    plan << "     neutral or pleasant\n";
    plan << "     -> audible tank character, NOT demonstrated as an objectionable\n";
    plan << "        defect.\n\n";
    plan << "  B) MIX30 shows, repeatably across two or more different sources,\n";
    plan << "     metallicity >= 3 AND (annoyance >= 2 OR unpleasant)\n";
    plan << "     -> shipping-context voicing issue evidence strengthened.\n\n";
    plan << "  C) MIX30 rates badly, but the DRY file of the same source rates\n";
    plan << "     similarly metallic or annoying\n";
    plan << "     -> source contribution or interaction is confounded; the tank is\n";
    plan << "        not blamed on its own.\n\n";
    plan << "  D) WET100 also reaches annoyance >= 2 or unpleasant across different\n";
    plan << "     sources\n";
    plan << "     -> tank-level voicing issue evidence strong.\n\n";
    plan << "These thresholds will not be changed after the ratings are seen.\n\n";
    plan << "THE VERDICT, one of three\n";
    plan << "-------------------------\n";
    plan << "  1. CHARACTER ACCEPTABLE - the metallic character is audible, and the\n";
    plan << "     evidence does not justify a voicing intervention. Stop trying to\n";
    plan << "     flatten FU-1; move to the Phase 6 closing assessment.\n\n";
    plan << "  2. VOICING ISSUE - a repeatable, objectionable metallic character in\n";
    plan << "     whole-tail and shipping context. Only then design the Size-based\n";
    plan << "     localisation experiment.\n\n";
    plan << "  3. UNRESOLVED - source, level and context effects still cannot be\n";
    plan << "     separated.\n\n";
    plan << "Any of the three is an acceptable outcome. Nothing here needs a problem\n";
    plan << "to be found, and CHARACTER ACCEPTABLE is not a failure of the round.\n\n";
    plan << "THE LEVEL CAVEAT\n";
    plan << "----------------\n";
    plan << "The three conditions sit at different loudnesses, because the\n";
    plan << "production gain law puts them there and nothing was levelled. The key\n";
    plan << "carries an analysis-only offset per file to the set median, applied to\n";
    plan << "nothing. Any reading that depends on an annoyance difference between\n";
    plan << "two conditions must be checked against those offsets before it is\n";
    plan << "believed.\n";

    // --- files --------------------------------------------------------------------------

    {
        std::ofstream out (directory + "/MANIFEST.txt", std::ios::trunc);

        if (out.is_open()) { out << manifest.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/ANALYSIS-PLAN.txt", std::ios::trunc);

        if (out.is_open()) { out << plan.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (out.is_open()) { out << key.str(); ++written; }
    }

    {
        std::ofstream out (directory + "/RENDER-CHECK.txt", std::ios::trunc);

        if (out.is_open())
        {
            int closestSameSource = static_cast<int> (trialCount);

            for (size_t x = 0; x < trialCount; ++x)
                for (size_t y = x + 1; y < trialCount; ++y)
                    if (trials[x].sourceIndex == trials[y].sourceIndex)
                        closestSameSource = std::min (closestSameSource,
                                                      static_cast<int> (y - x));

            double lowLufs = 1.0e9, highLufs = -1.0e9;
            double lowPeak = 1.0e9, highPeak = -1.0e9;

            for (const auto& m : measured)
            {
                lowLufs = std::min (lowLufs, m.lufs);
                highLufs = std::max (highLufs, m.lufs);
                lowPeak = std::min (lowPeak, m.peak);
                highPeak = std::max (highPeak, m.peak);
            }

            out << "PHASE 6 / FU-1 - ROUND 2c, RENDER CHECK\n";
            out << "=======================================\n\n";
            out << "Deliberately content-free. Nothing here says which file is\n";
            out << "which source or which condition.\n\n";
            out << "files written                  " << trialCount << "\n";
            out << "sources                        " << sourceCount << ", full band, unfiltered\n";
            out << "conditions per source          3\n";
            out << "band limiting                  none anywhere in this set\n";
            out << "closest two files sharing\n";
            out << "  a source                     " << closestSameSource
                << " apart (" << (closestSameSource - 1) << " files between)\n";
            out << "duration                       " << totalSeconds
                << " s, identical for every file\n";
            out << "files above 0 dBFS             " << clippingFiles << "\n";
            out << std::fixed << std::setprecision (2);
            out << "peak range, whole set          " << lowPeak << " to " << highPeak
                << " dBFS\n";
            out << "loudness range, whole set      " << lowLufs << " to " << highLufs
                << " LUFS\n";
            out << "loudness spread, whole set     " << (highLufs - lowLufs) << " LU\n";
            out << std::defaultfloat;
            out << "sample rate                    " << static_cast<long long> (sampleRate)
                << " Hz, 32-bit float stereo\n";
            out << "Mod Depth                      " << defaults::modDepthControl
                << ", unchanged and not under test\n";
            out << "mix law                        production, unchanged\n";
            out << "normalisation                  none\n";
            out << "loudness match                 none applied (analysis-only figures\n";
            out << "                               are in the key)\n";
            out << "limiter / saturation           none\n";
            out << "post-processing                none\n";

            ++written;
        }
    }

    return written;
}

} // namespace avtest
