#include "Phase10Listening.h"

#include "SignalExport.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/EarlyReflections.h"
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

    // The shipping defaults, from ParameterLayout.cpp. Every one of these is
    // identical in all eight renders.
    constexpr double kDecaySeconds   = 2.5;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModRateHz      = 0.5;
    constexpr double kModDepth       = 0.30;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;
    constexpr double kWidth          = 1.0;
    constexpr double kShippingMix    = 0.30;

    /** The two operating points under test in cases A and B. */
    constexpr double kShippedOccupancy   = 0.70;
    constexpr double kCandidateOccupancy = 0.20;

    struct Point
    {
        double size      = 0.80;   ///< the Size control, 0..1
        double balance   = 0.0;    ///< the Early/Late control, 0..1
        double occupancy = kShippedOccupancy;

        /** Sweeps Early/Late from 0 to 100 % across the render instead of
            holding `balance`. A static render cannot show level movement. */
        bool sweepBalance = false;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate, double sizeControl)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (kDecaySeconds), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (sizeControl));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (kModDepth),
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

            dry                                             untouched
            wet = input -> [Low Cut] -> [High Cut] -> Pre-Delay
                        -+-> Attack -> tank -> trim -+-> balance -> width -> mix
                         |                           |
                         +-> early reflections ------+

        Low Cut, High Cut, Pre-Delay and Attack are at their neutral endpoints
        throughout, where each is a bit-exact bypass. Pre-Delay and Attack are
        instantiated at zero rather than omitted: the point is that this is the
        production chain, not a convenient subset of it.

        The trim applies to the tank branch ONLY, exactly as the processor does
        it - it is a decay-aware compensation for the recurrence, and the early
        layer has no recurrence to compensate.

        `occupancy` is the only Pattern field a caller may vary, and only so
        that cases A and B can put the shipped value against the candidate.
        Everything else comes from er::defaultPattern, envelope decay 1.5
        included.
    */
    [[nodiscard]] std::vector<StereoFrame> render (const std::vector<StereoFrame>& dry,
                                                   double sampleRate,
                                                   const Point& point,
                                                   double wetMix)
    {
        ScopedFtz ftz;

        auto tank = makeTank (sampleRate, point.size);

        PreDelay<double> preDelay;
        preDelay.prepare (sampleRate);
        preDelay.snapTo (0.0);

        AttackInjection attack;
        attack.prepare (sampleRate);
        attack.snapTo (0.0);

        er::Pattern pattern;
        pattern.cellOccupancy = point.occupancy;

        const double sizeRatio = mapping::sizeScaleFor (point.size)
                               / mapping::sizeScaleFor (er::referenceSizeControl);

        EarlyReflections early;
        early.prepare (sampleRate, pattern);
        early.snapToSizeRatio (sizeRatio);

        /*
            The calibration comes from the production table, not from the
            reference constant - these renders have to be the product, and
            at Size 80 % the table gives a materially different K from the
            value at the reference Size.
        */
        const double calibration =
            er::calibrationFor (mapping::rt60For (kDecaySeconds),
                                mapping::sizeScaleFor (point.size));

        EarlyLateBalance balance;
        balance.prepare (sampleRate);
        balance.snapTo (point.sweepBalance ? 0.0 : point.balance, calibration);

        // A swept render is active throughout even though it starts at 0:
        // writeOnly would skip the convolution for the whole first block and
        // the layer would arrive a block late.
        const bool erActive = point.sweepBalance || ! balance.isBypassed();

        const double trim = mapping::previewWetTrim (mapping::rt60For (kDecaySeconds),
                                                      mapping::sizeScaleFor (point.size));

        const auto tail = static_cast<size_t> (sampleRate * (kDecaySeconds + 1.0));

        std::vector<StereoFrame> out (dry.size() + tail);

        constexpr int blockSize = 128;

        std::vector<double> wetL (blockSize, 0.0), wetR (blockSize, 0.0);
        std::vector<double> erL  (blockSize, 0.0), erR  (blockSize, 0.0);

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

            // Per block, exactly as the processor sets its own targets.
            if (point.sweepBalance)
                balance.setBalance (std::min (1.0, static_cast<double> (base)
                                                   / static_cast<double> (out.size())),
                                    calibration);

            preDelay.process (wetL.data(), wetR.data(), count);

            if (erActive)
                early.process (wetL.data(), wetR.data(), erL.data(), erR.data(), count);
            else
                early.writeOnly (wetL.data(), wetR.data(), count);

            attack.process (wetL.data(), wetR.data(), count);

            for (int i = 0; i < count; ++i)
            {
                double l = wetL[static_cast<size_t> (i)];
                double r = wetR[static_cast<size_t> (i)];

                tank->processSample (l, r);

                l *= trim;
                r *= trim;

                if (erActive)
                {
                    const double gl = balance.nextLate();
                    const double ge = balance.nextEarly();

                    l = gl * l + ge * erL[static_cast<size_t> (i)];
                    r = gl * r + ge * erR[static_cast<size_t> (i)];
                }

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

    /** The mono downmix, written to both channels so it plays at the same
        level in the same player and the comparison is not also a loudness
        comparison. */
    [[nodiscard]] std::vector<StereoFrame> monoSum (const std::vector<StereoFrame>& x)
    {
        std::vector<StereoFrame> out (x.size());

        for (size_t n = 0; n < x.size(); ++n)
        {
            const double m = 0.5 * (x[n].l + x[n].r);
            out[n].l = out[n].r = m;
        }

        return out;
    }

    [[nodiscard]] std::string fixed (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] double peakOf (const std::vector<StereoFrame>& f)
    {
        double p = 0.0;

        for (const auto& s : f)
            p = std::max (p, std::max (std::abs (s.l), std::abs (s.r)));

        return p;
    }

    /** Integrated level, which is what a listener would use as a loudness
        cue - the peak is not, and a blind pair has to be checked on the one
        that can actually give the answer away. */
    [[nodiscard]] double levelDb (const std::vector<StereoFrame>& f)
    {
        double sum = 0.0;

        for (const auto& s : f)
            sum += s.l * s.l + s.r * s.r;

        const double rms = std::sqrt (sum / std::max<size_t> (1, f.size() * 2));

        return rms > 0.0 ? 20.0 * std::log10 (rms) : -200.0;
    }
}

int exportPhase10ListeningSet (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    const auto fu2 = makeFu2Sources (sampleRate);

    const std::vector<StereoFrame>* snare = nullptr;

    for (const auto& s : fu2)
        if (s.name == "snare")
            snare = &s.frames;

    if (snare == nullptr)
        return -1;

    const auto vocal = makeVocalReference (sampleRate, 6.0);

    int written = 0;

    std::ostringstream key;

    key << "AuroraVerb - PHASE 10 Stage 3 listening key\n"
        << "===========================================\n\n"
        << "DO NOT READ until the case A and case B answers are written down.\n\n"
        << "Cases A and B are a blind A/B between two values of the early-\n"
        << "reflection pattern's cellOccupancy field. Everything else in the\n"
        << "two files of a pair is identical: same source, same gain, same\n"
        << "sample rate, same length, same Size, same Early/Late, same\n"
        << "envelope decay (1.5).\n\n"
        << "  shipped   cellOccupancy " << fixed (kShippedOccupancy, 2)
        << "   measured handoff valley 16.47 dB at Size 100 %, E/L 25 %\n"
        << "  candidate cellOccupancy " << fixed (kCandidateOccupancy, 2)
        << "   measured handoff valley  7.26 dB at the same point\n\n"
        << "Both are fully aperiodic: 0 % gap repetition at every period,\n"
        << "fifteen distinct arrival gaps out of fifteen.\n\n"
        << "The mapping differs between case A and case B, so an order learned\n"
        << "on one does not carry to the other.\n\n";

    /*
        THE BLIND MAPPING.

        Fixed so a run is reproducible, and deliberately different between the
        two A/B cases. It appears in no filename and in no manifest.
    */
    struct Pair
    {
        const char* caseName;
        const std::vector<StereoFrame>* source;
        const char* sourceName;
        double size, balance, mix;
        bool firstIsShipped;
    };

    const std::vector<Pair> pairs
    {
        { "caseA", snare,  "snare", 1.00, 0.25, 1.0,            false },
        { "caseB", &vocal, "vocal", 1.00, 0.50, 1.0,            true  },
    };

    double caseALevelGap = 0.0, caseBLevelGap = 0.0;

    for (const auto& p : pairs)
    {
        const double first  = p.firstIsShipped ? kShippedOccupancy : kCandidateOccupancy;
        const double second = p.firstIsShipped ? kCandidateOccupancy : kShippedOccupancy;

        const auto a = render (*p.source, sampleRate, { p.size, p.balance, first  }, p.mix);
        const auto b = render (*p.source, sampleRate, { p.size, p.balance, second }, p.mix);

        if (! writeStereoWav (directory + "/" + p.caseName + "-1.wav", a, sampleRate)
            || ! writeStereoWav (directory + "/" + p.caseName + "-2.wav", b, sampleRate))
            return -1;

        written += 2;

        const double gap = std::abs (levelDb (a) - levelDb (b));

        (std::string (p.caseName) == "caseA" ? caseALevelGap : caseBLevelGap) = gap;

        key << p.caseName << "-1  =  cellOccupancy " << fixed (first, 2)
            << "  (" << (p.firstIsShipped ? "shipped" : "candidate") << ")"
            << "   peak " << fixed (peakOf (a), 4)
            << "   level " << fixed (levelDb (a), 2) << " dB\n"
            << p.caseName << "-2  =  cellOccupancy " << fixed (second, 2)
            << "  (" << (p.firstIsShipped ? "candidate" : "shipped") << ")"
            << "   peak " << fixed (peakOf (b), 4)
            << "   level " << fixed (levelDb (b), 2) << " dB\n";
    }

    // --- CASE C: ER rhythm and discrete reflections ------------------------
    //
    // The shipped pattern only, at a balance that exposes the layer clearly
    // without making the test ER-only - at Early/Late 100 % the late field is
    // gone and any reverb would sound like a tap train, which would not be a
    // fair question. The Early/Late 0 file is the reference that lets a
    // listener attribute any rhythm to the ER layer rather than to the source
    // or the tank.
    {
        const auto withEr = render (*snare, sampleRate, { 0.80, 0.50, kShippedOccupancy }, 1.0);
        const auto without = render (*snare, sampleRate, { 0.80, 0.00, kShippedOccupancy }, 1.0);

        if (! writeStereoWav (directory + "/caseC-snare-el050.wav", withEr, sampleRate)
            || ! writeStereoWav (directory + "/caseC-snare-el000.wav", without, sampleRate))
            return -1;

        written += 2;

        key << "caseC-snare-el050  peak " << fixed (peakOf (withEr), 4) << "\n"
            << "caseC-snare-el000  peak " << fixed (peakOf (without), 4) << "\n";
    }

    // --- CASE D: stereo image and tonal quality ----------------------------
    //
    // At the shipping Mix, because this is the question about normal product
    // use, and on sustained harmonic material, because that is what reveals a
    // magnitude response as tone rather than as an event.
    {
        const auto stereo = render (vocal, sampleRate,
                                    { 0.80, 0.50, kShippedOccupancy }, kShippingMix);

        const auto mono = monoSum (stereo);

        if (! writeStereoWav (directory + "/caseD-vocal-stereo.wav", stereo, sampleRate)
            || ! writeStereoWav (directory + "/caseD-vocal-mono.wav", mono, sampleRate))
            return -1;

        written += 2;

        key << "caseD-vocal-stereo  peak " << fixed (peakOf (stereo), 4) << "\n"
            << "caseD-vocal-mono    peak " << fixed (peakOf (mono), 4) << "\n";
    }

    // --- LISTENING.md ------------------------------------------------------

    std::ostringstream md;

    md << "# AuroraVerb - PHASE 10 early reflections: listening\n\n"
       << "Eight files, four cases. Please answer in order and write each answer\n"
       << "down before moving on. Do not open `KEY.txt` until cases A and B are\n"
       << "answered.\n\n"
       << "Everything except Size, Early/Late and the one thing under test is at\n"
       << "the product's own defaults in every file. The same gain is applied\n"
       << "everywhere and nothing is loudness-matched afterwards.\n\n"
       << "**A note on level, so it does not become the answer.** The two files\n"
       << "in each blind pair are not exactly equal in loudness - case A differs\n"
       << "by " << fixed (caseALevelGap, 2) << " dB and case B by "
       << fixed (caseBLevelGap, 2) << " dB integrated. That\n"
       << "difference is part of the effect under test, not a gain error, and\n"
       << "matching it away would hide some of what you are being asked about.\n"
       << "Please judge how the reverb behaves in time, not which file is\n"
       << "louder - a small level difference is the easiest thing in the world\n"
       << "to mistake for a preference.\n\n"
       << "---\n\n"
       << "## CASE A - `caseA-1.wav` / `caseA-2.wav`\n\n"
       << "Snare, Size 100 %, Early/Late 25 %, 100 % wet. Blind A/B.\n\n"
       << "1. Does either version sound like the reverb becomes disconnected\n"
       << "   from the source, or develops an obvious hole between the attack\n"
       << "   and the tail?\n\n"
       << "2. Which version sounds more natural: 1, 2, or no meaningful\n"
       << "   preference?\n\n"
       << "---\n\n"
       << "## CASE B - `caseB-1.wav` / `caseB-2.wav`\n\n"
       << "Vocal, Size 100 %, Early/Late 50 %, 100 % wet. Blind A/B.\n\n"
       << "1. Does the reverb develop smoothly behind the musical source, or is\n"
       << "   there a gap before the tail arrives?\n\n"
       << "2. Which: 1, 2, or no meaningful preference?\n\n"
       << "---\n\n"
       << "## CASE C - `caseC-snare-el050.wav`, `caseC-snare-el000.wav`\n\n"
       << "Snare, Size 80 %, 100 % wet. `el050` has the early layer at\n"
       << "Early/Late 50 %; `el000` is the same render with no early layer at\n"
       << "all, as the reference.\n\n"
       << "1. In `el050`, can you hear any of these, and which:\n"
       << "   discrete slap; a repeating rhythm; metallic repetition;\n"
       << "   comb-like coloration; an artificial multi-tap character?\n\n"
       << "2. Is anything you hear present in `el000` too? If it is, it is not\n"
       << "   the early reflections.\n\n"
       << "---\n\n"
       << "## CASE D - `caseD-vocal-stereo.wav`, `caseD-vocal-mono.wav`\n\n"
       << "Vocal, Size 80 %, Early/Late 50 %, Mix "
       << fixed (kShippingMix * 100.0, 0) << " %. The second file is the\n"
       << "first one's own mono downmix, written to both channels.\n\n"
       << "1. In stereo: is the image stable, or does it pull to one side or\n"
       << "   refuse to sit still? Is the width natural or exaggerated?\n\n"
       << "2. Is there tonal coloration the dry vocal does not have?\n\n"
       << "3. In mono: do reflections disappear, does the tail thin out, or\n"
       << "   does a hollow colour appear that the stereo version does not\n"
       << "   have?\n\n"
       << "---\n\n"
       << "## What happens next\n\n"
       << "Cases A and B decide which of two candidate values of one pattern\n"
       << "constant becomes the shipping candidate. Neither has been accepted;\n"
       << "the production value is unchanged and will stay unchanged until\n"
       << "these answers exist.\n\n"
       << "Cases C and D are pass/fail observations on the shipped pattern. A\n"
       << "problem found there is a finding, not a tuning request.\n";

    {
        std::ofstream f (directory + "/LISTENING.md", std::ios::binary);

        if (! f)
            return -1;

        f << md.str();
        ++written;
    }

    {
        std::ofstream f (directory + "/KEY.txt", std::ios::binary);

        if (! f)
            return -1;

        f << key.str();
        ++written;
    }

    return written;
}

// ===========================================================================

int exportPhase10FinalSanity (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    const auto fu2 = makeFu2Sources (sampleRate);

    auto named = [&fu2] (const char* name) -> const std::vector<StereoFrame>*
    {
        for (const auto& s : fu2)
            if (s.name == name)
                return &s.frames;

        return nullptr;
    };

    const auto vocal = makeVocalReference (sampleRate, 6.0);

    struct Case { const char* name; const std::vector<StereoFrame>* source; };

    const std::vector<Case> cases
    {
        { "snare", named ("snare") },
        { "vocal", &vocal          },
        { "piano", named ("piano") },
        { "pad",   named ("pad")   },
    };

    int written = 0;

    std::ostringstream md;

    md << "# AuroraVerb - PHASE 10 final sanity renders\n\n"
       << "Four files, not blind, nothing to choose between. Everything is at\n"
       << "the production candidate:\n\n"
       << "    cellOccupancy   0.40\n"
       << "    envelopeDecay   1.5\n"
       << "    gain law        linear-in-power, late = sqrt(1-t), early = sqrt(t)*K\n"
       << "    calibration     the recalibrated BAL-4 table\n"
       << "    Size            80 %\n"
       << "    Mix             " << fixed (kShippingMix * 100.0, 0) << " %\n"
       << "    everything else at its default\n\n"
       << "**Each file sweeps Early/Late from 0 to 100 % across its length.**\n"
       << "That is deliberate. A static render cannot show unnatural level\n"
       << "movement, which is one of the six things these files exist to check;\n"
       << "a sweep shows it directly and walks through the handoff, the slap\n"
       << "region and the ER-heavy end in one pass, driving the real automation\n"
       << "path rather than a row of static settings.\n\n"
       << "So the reverb starts as the late network alone and ends as the early\n"
       << "layer alone. Listen once through each, for:\n\n"
       << "1. **Disconnected early/late behaviour** - does the reverb ever come\n"
       << "   apart into two separate sounds, or develop a hole as the balance\n"
       << "   moves?\n"
       << "2. **Discrete slap** - do individual reflections become audible as\n"
       << "   separate events?\n"
       << "3. **Metallic repetition** - any ringing or comb-like character?\n"
       << "4. **Tonal coloration** - does the reverb impose a pitch or a\n"
       << "   hollowness the source does not have?\n"
       << "5. **Unnatural level movement** - does the loudness lurch as the\n"
       << "   control travels? A gradual trend is expected and documented; a\n"
       << "   lurch or a dip in the middle is not.\n"
       << "6. **Unstable stereo image** - does the image pull, wander, or\n"
       << "   collapse?\n\n"
       << "## One thing is already known and measured, so it is not a surprise\n\n"
       << "On **sustained narrowband material** - the pad especially - the two\n"
       << "branches can become strongly correlated or anti-correlated at the\n"
       << "source's own frequency, and the sweep then shows a level trend of\n"
       << "several dB, up to 13 dB at small Size with a short Decay. It is a\n"
       << "property of two layers carrying the same narrow band, it is present\n"
       << "at every ER pattern measured, and no calibration removes it.\n\n"
       << "It is carried as an explicit product concern rather than hidden.\n"
       << "What the listening is for is whether it is objectionable in use, not\n"
       << "whether it exists.\n\n"
       << "## Files\n\n";

    for (const auto& c : cases)
    {
        if (c.source == nullptr)
            return -1;

        const auto out = render (*c.source, sampleRate,
                                 { 0.80, 0.0, er::defaultPattern.cellOccupancy, true },
                                 kShippingMix);

        const auto path = std::string ("final-") + c.name + "-sweep.wav";

        if (! writeStereoWav (directory + "/" + path, out, sampleRate))
            return -1;

        ++written;

        md << "- `" << path << "` - peak " << fixed (peakOf (out), 4)
           << ", integrated " << fixed (levelDb (out), 2) << " dB\n";
    }

    md << "\nSample rate " << fixed (sampleRate, 0) << " Hz, 32-bit float stereo.\n"
       << "Nothing is level-matched; the level trend is part of what is being\n"
       << "checked.\n";

    {
        std::ofstream f (directory + "/LISTENING.md", std::ios::binary);

        if (! f)
            return -1;

        f << md.str();
        ++written;
    }

    return written;
}

// ===========================================================================

int exportPhase10OccupancyAb (const std::string& directory, double sampleRate)
{
    if (sampleRate <= 0.0)
        return -1;

    constexpr double kLow  = 0.20;
    constexpr double kHigh = 0.40;

    const auto fu2 = makeFu2Sources (sampleRate);

    const std::vector<StereoFrame>* snare = nullptr;

    for (const auto& s : fu2)
        if (s.name == "snare")
            snare = &s.frames;

    if (snare == nullptr)
        return -1;

    const auto vocal = makeVocalReference (sampleRate, 6.0);

    int written = 0;

    std::ostringstream key, md;

    key << "AuroraVerb - PHASE 10 occupancy A/B key\n"
        << "=======================================\n\n"
        << "DO NOT READ until both answers are written down.\n\n"
        << "cellOccupancy " << fixed (kLow, 2)
        << "   handoff valley 7.26 dB at Size 100 %, 1/3-oct ripple 5.18 dB\n"
        << "cellOccupancy " << fixed (kHigh, 2)
        << "   handoff valley 8.86 dB at Size 100 %, 1/3-oct ripple 3.76 dB\n\n"
        << "Both are fully aperiodic (0 % gap repetition, 15/15 distinct\n"
        << "intervals) and indistinguishable on correlation and mono downmix.\n"
        << "The measurements disagree about which is better, which is why this\n"
        << "comparison exists.\n\n"
        << "The mapping is randomised independently per case.\n\n";

    struct Case
    {
        const char* name;
        const std::vector<StereoFrame>* source;
        double size, balance, mix;
        bool firstIsLow;
    };

    /*
        O2 uses 100 % wet rather than the shipping Mix, and that is a
        deliberate reading of "the most revealing sensible Mix value". The
        question is about the reverb's internal structure - a handoff and a
        tonal character - and at Mix 30 % seventy per cent of what reaches the
        listener is a dry vocal that is identical in both files. Burying the
        thing under test under unchanged material does not make the test more
        realistic, it makes it less sensitive.
    */
    const std::vector<Case> cases
    {
        { "caseO1", snare,  1.00, 0.25, 1.0, false },
        { "caseO2", &vocal, 1.00, 0.50, 1.0, true  },
    };

    std::vector<double> levelGaps;

    for (const auto& c : cases)
    {
        const double first  = c.firstIsLow ? kLow  : kHigh;
        const double second = c.firstIsLow ? kHigh : kLow;

        const auto a = render (*c.source, sampleRate, { c.size, c.balance, first  }, c.mix);
        const auto b = render (*c.source, sampleRate, { c.size, c.balance, second }, c.mix);

        if (! writeStereoWav (directory + "/" + c.name + "-1.wav", a, sampleRate)
            || ! writeStereoWav (directory + "/" + c.name + "-2.wav", b, sampleRate))
            return -1;

        written += 2;

        levelGaps.push_back (std::abs (levelDb (a) - levelDb (b)));

        key << c.name << "-1  =  cellOccupancy " << fixed (first, 2)
            << "   peak " << fixed (peakOf (a), 4)
            << "   level " << fixed (levelDb (a), 2) << " dB\n"
            << c.name << "-2  =  cellOccupancy " << fixed (second, 2)
            << "   peak " << fixed (peakOf (b), 4)
            << "   level " << fixed (levelDb (b), 2) << " dB\n";
    }

    md << "# AuroraVerb - PHASE 10 early reflections: the final occupancy A/B\n\n"
       << "Four files, two cases, both blind. Please answer case O1 completely\n"
       << "before listening to case O2, and do not open `KEY.txt` until both\n"
       << "are written down.\n\n"
       << "This replaces the earlier eight-file set. That one compared the\n"
       << "current production value against one alternative; the research has\n"
       << "since ruled the production value out on structural grounds, and the\n"
       << "two files in each pair here are the two remaining candidates.\n\n"
       << "Everything except the one pattern constant under test is identical\n"
       << "between the two files of a pair: same source, same gain, same sample\n"
       << "rate, same length, same Size, same Early/Late, same envelope decay.\n"
       << "**Level is not a cue here, and that was checked rather than\n"
       << "assumed.** Nothing is loudness-matched, but the pairs happen to\n"
       << "differ in integrated level by only "
       << fixed (levelGaps[0], 2) << " dB and "
       << fixed (levelGaps[1], 2) << " dB, so whatever you\n"
       << "hear is structure and not loudness. Peak level does differ a little\n"
       << "more, which is the effect itself - the same energy arriving in a\n"
       << "different pattern - and is not something to judge on.\n\n"
       << "---\n\n"
       << "## CASE O1 - `caseO1-1.wav` / `caseO1-2.wav`\n\n"
       << "Snare, Size 100 %, Early/Late 25 %, 100 % wet.\n\n"
       << "1. A, B, or no meaningful preference?\n"
       << "2. Any discrete slap?\n"
       << "3. Any rhythmic multitap impression?\n"
       << "4. Any obvious hole between the early reflections and the late tail?\n"
       << "5. Any tonal coloration, or any unnatural smoothness?\n\n"
       << "---\n\n"
       << "## CASE O2 - `caseO2-1.wav` / `caseO2-2.wav`\n\n"
       << "Vocal, Size 100 %, Early/Late 50 %, 100 % wet.\n\n"
       << "1. A, B, or no meaningful preference?\n"
       << "2. Any discrete slap?\n"
       << "3. Any rhythmic multitap impression?\n"
       << "4. Any obvious hole between the early reflections and the late tail?\n"
       << "5. Any tonal coloration, or any unnatural smoothness?\n\n"
       << "---\n\n"
       << "## What this decides\n\n"
       << "Which of two candidate values of one early-reflection constant\n"
       << "becomes the shipping candidate. Neither is accepted; the production\n"
       << "value is a third value that the measurements have ruled out, and it\n"
       << "stays in place until these answers exist.\n\n"
       << "The measurements disagree about which candidate is better - one has\n"
       << "the cleaner handoff, the other the flatter tone - so there is no\n"
       << "expected answer here, and question 5 matters as much as question 4.\n";

    {
        std::ofstream f (directory + "/LISTENING.md", std::ios::binary);

        if (! f)
            return -1;

        f << md.str();
        ++written;
    }

    {
        std::ofstream f (directory + "/KEY.txt", std::ios::binary);

        if (! f)
            return -1;

        f << key.str();
        ++written;
    }

    return written;
}

} // namespace avtest
