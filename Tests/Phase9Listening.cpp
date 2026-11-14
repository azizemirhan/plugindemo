#include "Phase9Listening.h"

#include "DampingListening.h"
#include "GainStaging.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
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

    // The shipping defaults, from ParameterLayout.cpp.
    constexpr double kDecaySeconds    = 2.5;
    constexpr double kSizeControl     = 0.60;
    constexpr double kEarlyDiffusion  = 0.70;
    constexpr double kModRateHz       = 0.5;
    constexpr double kLowDamp         = 0.20;
    constexpr double kLowDampHz       = 200.0;
    constexpr double kHighDamp        = 0.45;
    constexpr double kHighDampHz      = 10000.0;
    constexpr double kWidth           = 1.0;
    constexpr double kShippingMix     = 0.30;
    constexpr double kShippingModDepth = 0.30;

    struct Point
    {
        double attack     = 0.0;    ///< 0..1, the Attack control
        double modDepth   = kShippingModDepth;
        double preDelayMs = 0.0;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate, double modDepth)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (kDecaySeconds), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (kSizeControl));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (modDepth),
                                      mapping::modRateScaleFor (kModRateHz), true);

        // Settle the Size morph on silence, then clear, so a render holds the
        // tank AT its size rather than moving into it.
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

    /**
        processInternal, offline, in its own order.

            dry                                            untouched
            wet = input -> [Low Cut] -> [High Cut]          neutral, bypassed
                        -> Pre-Delay -> Attack -> tank
                        -> preview trim -> width -> mix

        Low Cut and High Cut are at their neutral endpoints throughout this
        set, where PHASE 8 proved them a bit-exact bypass, so they are not
        instantiated. Pre-Delay and Attack are the production classes.
    */
    [[nodiscard]] std::vector<StereoFrame> render (const std::vector<StereoFrame>& dry,
                                                   double sampleRate,
                                                   const Point& point,
                                                   double wetMix)
    {
        ScopedFtz ftz;

        auto tank = makeTank (sampleRate, point.modDepth);

        PreDelay<double> preDelay;
        preDelay.prepare (sampleRate);
        preDelay.snapTo (point.preDelayMs);

        AttackInjection attack;
        attack.prepare (sampleRate);
        attack.snapTo (point.attack);

        const double trim = mapping::previewWetTrim (mapping::rt60For (kDecaySeconds),
                                                      mapping::sizeScaleFor (kSizeControl));

        // Room for the tail, the pre-delay and the Attack spread.
        const auto tail = static_cast<size_t> (
            sampleRate * (kDecaySeconds + 1.0)
            + (point.preDelayMs + AttackInjection::defaultMaxSpreadMs) * sampleRate / 1000.0);

        std::vector<StereoFrame> out (dry.size() + tail);

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

            preDelay.process (wetL.data(), wetR.data(), count);
            attack  .process (wetL.data(), wetR.data(), count);

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

    [[nodiscard]] double peakOf (const std::vector<StereoFrame>& f)
    {
        double p = 0.0;

        for (const auto& s : f)
            p = std::max (p, std::max (std::abs (s.l), std::abs (s.r)));

        return p;
    }
}

int exportPhase9ListeningSet (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    // --- the three sources ------------------------------------------------
    //
    // Sparse transients, because that is where a periodic or discrete-repeat
    // character is easiest to hear. `snare` and `piano` are the established
    // FU-2 material; `hats` is PHASE 7's noise-based hi-hat stand-in, which
    // has no resonator in it and so cannot answer a question about metallic
    // character on the reverb's behalf.

    const auto fu2 = makeFu2Sources (sampleRate);

    std::vector<NamedSource> sources;

    for (const auto& s : fu2)
        if (s.name == "snare" || s.name == "piano")
            sources.push_back (s);

    if (sources.size() != 2)
        return -1;

    sources.push_back ({ "hats", makeBrightPercussion (sampleRate, 6.0) });

    int written = 0;

    for (const auto& s : sources)
    {
        if (! writeStereoWav (directory + "/" + s.name + "-dry.wav", s.frames, sampleRate))
            return -1;

        ++written;
    }

    std::ostringstream manifest, key;

    manifest << "AuroraVerb - PHASE 9 Stage 4 targeted Attack listening set\n"
             << "==========================================================\n\n"
             << "Small and pointed. PHASE 9 is ATTACK ONLY - Density, Bloom and\n"
             << "Late Diffusion were all rejected on measurement and are not in this\n"
             << "set.\n\n"
             << "Stage 3 accepted the Attack production candidate with exactly one\n"
             << "open risk, and this set exists to answer it:\n\n"
             << "  At Mod Depth 0 and high Attack, late-field envelope periodicity\n"
             << "  rises from 16.72 to 37.52 - about 6.9 times the 3.03 measurement\n"
             << "  floor. At the shipping Mod Depth 30 % the effect is much smaller\n"
             << "  and non-monotone.\n\n"
             << "  Attack is 32 irregular taps imprinting their own autocorrelation\n"
             << "  on the excitation. At Mod Depth 0 there is nothing to smear it.\n"
             << "  Whether that is audible as a defect is not a measurement\n"
             << "  question, which is why this set exists.\n\n"
             << "THE PRIMARY QUESTION\n"
             << "  Does high Attack create an audibly periodic, metallic, hollow,\n"
             << "  rhythmic, fluttering or discrete-repeat character when Mod Depth\n"
             << "  is zero?\n\n"
             << "THE SECONDARY QUESTION\n"
             << "  Does Attack stay musically useful at the shipping Mod Depth 30 %?\n\n"
             << "Held fixed in EVERY render, at the product's own defaults:\n"
             << "  Decay            " << fixed (kDecaySeconds, 1) << " s\n"
             << "  Size             " << fixed (kSizeControl * 100.0, 0) << " %\n"
             << "  Early Diffusion  " << fixed (kEarlyDiffusion * 100.0, 0) << " %\n"
             << "  Low Damp         " << fixed (kLowDamp * 100.0, 0) << " %  at "
             << fixed (kLowDampHz, 0) << " Hz\n"
             << "  High Damp        " << fixed (kHighDamp * 100.0, 0) << " %  at "
             << fixed (kHighDampHz, 0) << " Hz\n"
             << "  Width            " << fixed (kWidth * 100.0, 0) << " %\n"
             << "  Mod Rate         " << fixed (kModRateHz, 2) << " Hz\n"
             << "  Low Cut          20 Hz    (OFF, a bit-exact bypass)\n"
             << "  High Cut         20 kHz   (OFF, a bit-exact bypass)\n"
             << "  input and output gain unity\n\n"
             << "Only Attack, Mod Depth and - in case C - Pre-Delay vary.\n\n"
             << "LEVEL: nothing is normalised. Stage 3 measured the Attack sweep's\n"
             << "integrated wet energy variation at 0.096 dB, so there is nothing\n"
             << "worth matching away and matching would only hide whatever is really\n"
             << "there. Identical source gain and identical downstream gain in every\n"
             << "render. The peak does fall as Attack rises - the same energy spread\n"
             << "over more time has a lower peak - and that is the effect, not a\n"
             << "flaw.\n\n"
             << "Sample rate " << fixed (sampleRate, 0) << " Hz, 32-bit float stereo.\n\n"
             << "SOURCES: snare and piano are the established FU-2 material; hats is\n"
             << "PHASE 7's noise-based hi-hat stand-in, which has no resonator in it\n"
             << "and so cannot answer a question about metallic character on the\n"
             << "reverb's behalf. Each is also written dry, as *-dry.wav.\n\n";

    // =======================================================================
    //  CASE A - the primary risk test
    // =======================================================================

    manifest << "CASE A - THE PRIMARY RISK   (100 % wet)\n"
             << "---------------------------------------\n"
             << "Wet only, because the question is about the tail and Mix 30 % would\n"
             << "bury it under 70 % of something unchanged.\n\n"
             << "  caseA-<source>-md00-a000.wav   Mod Depth 0 %,  Attack 0 %\n"
             << "  caseA-<source>-md00-a050.wav   Mod Depth 0 %,  Attack 50 %\n"
             << "  caseA-<source>-md30-a000.wav   Mod Depth 30 %, Attack 0 %\n"
             << "  caseA-<source>-md30-a050.wav   Mod Depth 30 %, Attack 50 %\n\n"
             << "  caseA-<source>-highattack-1.wav   BLIND\n"
             << "  caseA-<source>-highattack-2.wav   BLIND\n\n"
             << "  The two blind files are Attack 100 % at the two Mod Depths, one\n"
             << "  each, in an order that differs per source. They are blind for a\n"
             << "  specific reason: a listener who knows which render the\n"
             << "  periodicity measurement complained about will hear what the\n"
             << "  measurement predicted. The mapping is in\n"
             << "  KEY-do-not-open-first.txt.\n\n"
             << "  " << pad ("file", 36) << pad ("peak", 10) << "\n";

    key << "AuroraVerb - PHASE 9 Stage 4 listening set: KEY\n"
        << "===============================================\n\n"
        << "Only case A's high-Attack pair is blind. Do not open this until the\n"
        << "case-A answers are written down.\n\n";

    /*
        The blind order per source. Written down rather than generated, so the
        set is reproducible: the same build produces the same bytes, which is
        a property every listening set in this project has. Two of the three
        put Mod Depth 0 first, which is deliberate rather than alternating -
        a strict alternation is itself a pattern a listener can follow.
    */
    const bool modZeroFirst[] { true, false, true };

    for (size_t s = 0; s < sources.size(); ++s)
    {
        const auto& source = sources[s];

        struct Labelled { const char* tag; Point point; };

        const Labelled labelled[]
        {
            { "md00-a000", Point { 0.00, 0.0 } },
            { "md00-a050", Point { 0.50, 0.0 } },
            { "md30-a000", Point { 0.00, kShippingModDepth } },
            { "md30-a050", Point { 0.50, kShippingModDepth } },
        };

        for (const auto& l : labelled)
        {
            const auto frames = render (source.frames, sampleRate, l.point, 1.0);

            const std::string name = "caseA-" + source.name + "-" + l.tag + ".wav";

            if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
                return -1;

            ++written;

            manifest << "  " << pad (name, 36) << pad (fixed (peakOf (frames), 4), 10) << "\n";
        }

        // --- the blind high-Attack pair -----------------------------------
        const Point modZero { 1.0, 0.0 };
        const Point modShipping { 1.0, kShippingModDepth };

        const bool zeroFirst = modZeroFirst[s];

        const Point first  = zeroFirst ? modZero : modShipping;
        const Point second = zeroFirst ? modShipping : modZero;

        const auto firstFrames  = render (source.frames, sampleRate, first,  1.0);
        const auto secondFrames = render (source.frames, sampleRate, second, 1.0);

        const std::string name1 = "caseA-" + source.name + "-highattack-1.wav";
        const std::string name2 = "caseA-" + source.name + "-highattack-2.wav";

        if (! writeStereoWav (directory + "/" + name1, firstFrames, sampleRate))
            return -1;

        ++written;

        if (! writeStereoWav (directory + "/" + name2, secondFrames, sampleRate))
            return -1;

        ++written;

        manifest << "  " << pad (name1, 36) << pad (fixed (peakOf (firstFrames), 4), 10)
                 << "  Attack 100 %, Mod Depth withheld\n";
        manifest << "  " << pad (name2, 36) << pad (fixed (peakOf (secondFrames), 4), 10)
                 << "  Attack 100 %, Mod Depth withheld\n";

        key << "  " << pad (name1, 38) << "Attack 100 %, Mod Depth "
            << (zeroFirst ? "0 %" : "30 %") << "\n";
        key << "  " << pad (name2, 38) << "Attack 100 %, Mod Depth "
            << (zeroFirst ? "30 %" : "0 %") << "\n";
    }

    // --- the secondary Mod Depth 100 reference ----------------------------
    {
        const auto frames = render (sources.front().frames, sampleRate,
                                    Point { 1.0, 1.0 }, 1.0);

        const std::string name = "caseA-" + sources.front().name + "-md100-a100.wav";

        if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
            return -1;

        ++written;

        manifest << "  " << pad (name, 36) << pad (fixed (peakOf (frames), 4), 10)
                 << "  secondary reference: Mod Depth 100 %, Attack 100 %\n";
    }

    manifest << "\n  WHAT TO LISTEN FOR, and the Mod Depth 0 files are the point\n"
             << "    - an obvious repeated pattern\n"
             << "    - rhythmic flutter\n"
             << "    - metallic ringing\n"
             << "    - comb-like hollowness\n"
             << "    - discrete echo taps\n"
             << "    - an unnatural pulse in the tail\n"
             << "    - static coloration\n"
             << "    - loss of smooth late-field behaviour\n\n"
             << "    DIFFERENT IS NOT DEFECTIVE. Mod Depth 0 removes the tank's\n"
             << "    modulation, so it will sound more static whatever Attack does -\n"
             << "    that is PHASE 5's accepted character showing through, not an\n"
             << "    Attack defect. The question is only whether the high-Attack\n"
             << "    files become OBJECTIONABLE or unprofessional.\n\n"
             << "    Judge the two blind files against each other and against the\n"
             << "    Attack 0 files at both Mod Depths, which are labelled.\n\n";

    // =======================================================================
    //  CASE B - normal product use
    // =======================================================================

    manifest << "CASE B - NORMAL PRODUCT USE   (Mod Depth 30 %, Mix 30 %)\n"
             << "--------------------------------------------------------\n"
             << "The shipping configuration. Only Attack moves.\n\n"
             << "  " << pad ("file", 36) << pad ("peak", 10) << "Attack\n";

    for (double u : { 0.0, 0.25, 0.50, 0.75, 1.0 })
    {
        const auto frames = render (sources.front().frames, sampleRate,
                                    Point { u, kShippingModDepth }, kShippingMix);

        // Three digits always, so the five files sort in control order in
        // any file browser.
        std::ostringstream tag;
        tag << std::setw (3) << std::setfill ('0') << static_cast<int> (u * 100.0 + 0.5);

        const std::string name = "caseB-" + sources.front().name + "-a" + tag.str() + ".wav";

        if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
            return -1;

        ++written;

        manifest << "  " << pad (name, 36) << pad (fixed (peakOf (frames), 4), 10)
                 << fixed (u * 100.0, 0) << " %\n";
    }

    manifest << "\n  WHAT TO LISTEN FOR\n"
             << "    - does the buildup get progressively slower across the sweep\n"
             << "    - is the range useful through most of its travel, not just at\n"
             << "      the top\n"
             << "    - is it SLOWER rather than merely QUIETER\n"
             << "    - does the onset stay where it is, i.e. it must not feel like\n"
             << "      Pre-Delay\n"
             << "    - any discrete multitap character at all\n\n"
             << "    The internal mapping is 250 ms times the control squared, so\n"
             << "    0-100 ms of spread occupies the first 63 % of the travel. The\n"
             << "    low settings are meant to be subtle; if 25 % sounds like\n"
             << "    nothing, that is worth reporting, because the mapping is the\n"
             << "    easiest thing to change.\n\n";

    // =======================================================================
    //  CASE C - Attack against Pre-Delay
    // =======================================================================

    manifest << "CASE C - ATTACK AGAINST PRE-DELAY   (Mod Depth 30 %, Mix 30 %)\n"
             << "--------------------------------------------------------------\n"
             << "Three files, one source. A perceptual confirmation only - the\n"
             << "distinction is already proved numerically: Pre-Delay 50 ms moves\n"
             << "the onset by exactly 50 ms at every Attack setting and leaves the\n"
             << "buildup at 108 ms, while Attack 100 % moves the buildup from 108 to\n"
             << "252 ms at every Pre-Delay setting and leaves the onset at 21 ms.\n\n"
             << "  " << pad ("file", 36) << pad ("peak", 10) << "setting\n";

    struct CaseC { const char* tag; Point point; const char* label; };

    const CaseC caseC[]
    {
        { "a-reference", Point { 0.0, kShippingModDepth, 0.0 },  "Pre-Delay 0, Attack 0" },
        { "b-predelay",  Point { 0.0, kShippingModDepth, 80.0 }, "Pre-Delay 80 ms, Attack 0" },
        { "c-attack",    Point { 1.0, kShippingModDepth, 0.0 },  "Pre-Delay 0, Attack 100 %" },
    };

    for (const auto& c : caseC)
    {
        const auto frames = render (sources.front().frames, sampleRate, c.point,
                                    kShippingMix);

        const std::string name = std::string ("caseC-") + sources.front().name
                               + "-" + c.tag + ".wav";

        if (! writeStereoWav (directory + "/" + name, frames, sampleRate))
            return -1;

        ++written;

        manifest << "  " << pad (name, 36) << pad (fixed (peakOf (frames), 4), 10)
                 << c.label << "\n";
    }

    manifest << "\n  WHAT TO LISTEN FOR\n"
             << "    B should move the whole wet event later - a gap after the dry\n"
             << "      transient, then the reverb arrives intact.\n"
             << "    C should keep the reverb starting WITH the transient while the\n"
             << "      field takes longer to fill in.\n\n"
             << "    If B and C sound like the same thing, that is a finding and the\n"
             << "      most important one this case can produce.\n\n"
             << "    C is Attack at its maximum rather than at a setting matched to\n"
             << "    B's separation, because the two controls do not share a scale -\n"
             << "    there is no Attack value that is 'the same as 80 ms of\n"
             << "    Pre-Delay'. The maximum is simply where the distinction is\n"
             << "    easiest to hear.\n\n";

    // --- the answers ------------------------------------------------------

    manifest << "THE ANSWERS\n"
             << "-----------\n"
             << "Write these down. For case A, write them down BEFORE opening\n"
             << "KEY-do-not-open-first.txt.\n\n"
             << "CASE A - the primary risk\n"
             << "  1. Do either of the two blind high-Attack files have an obvious\n"
             << "     repeated pattern, flutter, metallic ring, hollow comb or\n"
             << "     discrete echo taps?\n\n"
             << "  2. If one does and the other does not, which number is it?\n\n"
             << "  3. Is whatever you hear OBJECTIONABLE, or merely different from\n"
             << "     the Attack 0 files?\n\n"
             << "  4. Same question for Attack 50 % at Mod Depth 0, which is\n"
             << "     labelled.\n\n"
             << "CASE B - normal use\n"
             << "  1. Does the buildup get progressively slower across the sweep?\n\n"
             << "  2. Is the range useful through most of its travel?\n\n"
             << "  3. Slower, or just quieter?\n\n"
             << "  4. Any discrete multitap character?\n\n"
             << "CASE C\n"
             << "  1. Does B move the whole wet event later?\n\n"
             << "  2. Does C keep the onset while slowing the fill-in?\n\n"
             << "  3. Do B and C sound like different things?\n\n"
             << "THE DECISION\n"
             << "------------\n"
             << "  PASS              high Attack is musically useful; Mod Depth 30 %\n"
             << "                    is clean; Mod Depth 0 may sound more static but\n"
             << "                    has no clearly objectionable repeating,\n"
             << "                    metallic or rhythmic artifact; Attack stays\n"
             << "                    perceptually distinct from Pre-Delay; no click,\n"
             << "                    pop, pitch glide or discrete-tap defect\n\n"
             << "  CONDITIONAL PASS  Mod Depth 0 at extreme Attack has a mild\n"
             << "                    texture difference that is not objectionable,\n"
             << "                    and normal-use settings are clean\n\n"
             << "  FAIL              high Attack at low or zero modulation produces\n"
             << "                    a clearly audible repeating pattern, metallic\n"
             << "                    tail, flutter, discrete echo sequence or\n"
             << "                    comb-like resonance\n\n"
             << "  verdict:\n\n"
             << "On FAIL, do not tune anything. Record the source, the Attack value,\n"
             << "the Mod Depth value and the exact perceived defect, and stop. The\n"
             << "things that would then be candidates for change are the tap count,\n"
             << "the tap timing distribution, the sign pattern and the maximum\n"
             << "Attack mapping - and which one depends on what the defect is.\n\n"
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
