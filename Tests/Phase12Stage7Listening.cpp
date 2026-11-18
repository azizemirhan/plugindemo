#include "Phase12Stage7Listening.h"
#include "Phase12Stage6.h"
#include "Phase12Structural.h"

#include "DampingListening.h"
#include "DspAnalysis.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>

/*
    PHASE 12 - STAGE 7, track A. The Ambient blind listening pack.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    A1b and AT5a are Stage 6's structures, parameter for parameter, built by
    Stage 6's builders and calibrated as Stage 6 calibrated them. Nothing is
    tuned here. See Phase12Stage7Listening.h.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kBands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

    using Bands = std::array<double, 7>;

    // --- sources ---------------------------------------------------------------------
    [[nodiscard]] std::vector<double> monoOf (const std::vector<StereoFrame>& frames, double seconds)
    {
        const size_t n = std::min (frames.size(), static_cast<size_t> (seconds * kRate));

        std::vector<double> m (n);

        for (size_t i = 0; i < n; ++i)
            m[i] = 0.5 * (frames[i].l + frames[i].r);

        const size_t fade = std::min (n, static_cast<size_t> (0.030 * kRate));

        for (size_t i = 0; i < fade; ++i)
            m[n - 1 - i] *= static_cast<double> (i) / static_cast<double> (fade);

        return m;
    }

    /** A piano-like phrase: seven struck notes and chords from G2 to A4,
        each a dozen slightly inharmonic partials with a 3 ms attack, the
        upper partials dying sooner. The four-sine chord the earlier stages
        call 'piano' is a held tone, and a reverb's level on four sines is
        mostly luck (see the level table in KEY.txt); this is not. */
    [[nodiscard]] std::vector<double> pianoPhrase()
    {
        struct Note { double at, hz, level; };

        const Note notes[]
        {
            { 0.00, 110.00, 1.0 }, { 0.00, 164.81, 0.7 }, { 0.00, 220.00, 0.7 },
            { 0.45, 277.18, 0.9 },
            { 0.80, 329.63, 0.9 },
            { 1.15, 440.00, 0.9 },
            { 1.50,  98.00, 1.0 }, { 1.50, 146.83, 0.7 }, { 1.50, 246.94, 0.7 },
            { 1.95, 293.66, 0.9 },
            { 2.30, 392.00, 0.9 }
        };

        std::vector<double> m (static_cast<size_t> (2.9 * kRate), 0.0);

        for (const auto& note : notes)
            for (int k = 1; k <= 12; ++k)
            {
                const double hz = note.hz * k * std::sqrt (1.0 + 0.0004 * k * k);

                if (hz > 9000.0)
                    break;

                const double amplitude = note.level / std::pow (static_cast<double> (k), 1.2);
                const double tau = 1.1 / (1.0 + 0.3 * (k - 1)) * std::pow (220.0 / note.hz, 0.3);
                const auto from = static_cast<size_t> (note.at * kRate);

                for (size_t n = from; n < m.size(); ++n)
                {
                    const double t = static_cast<double> (n - from) / kRate;
                    const double attack = t < 0.003 ? 0.5 - 0.5 * std::cos (kTwoPi * t / 0.006) : 1.0;

                    m[n] += amplitude * attack * std::exp (-t / tau) * std::sin (kTwoPi * hz * t);
                }
            }

        double peak = 0.0;

        for (double v : m)
            peak = std::max (peak, std::abs (v));

        const size_t fade = static_cast<size_t> (0.030 * kRate);

        for (size_t n = 0; n < m.size(); ++n)
            m[n] *= 0.45 / peak * (n + fade >= m.size() ? static_cast<double> (m.size() - 1 - n)
                                                           / static_cast<double> (fade) : 1.0);

        return m;
    }

    // --- octave shares of a response ------------------------------------------------
    [[nodiscard]] Bands sharesOf (const Stereo& ir)
    {
        Bands share {};
        double total = 0.0;

        for (size_t b = 0; b < 7; ++b)
        {
            for (double e : detail::bandEnergy (ir, kBands[b], ir.l.size()))
                share[b] += e;

            total += share[b];
        }

        for (auto& v : share)
            v /= std::max (1.0e-300, total);

        return share;
    }

    [[nodiscard]] Bands dbBetween (const Bands& a, const Bands& reference)
    {
        Bands db {};

        for (size_t b = 0; b < 7; ++b)
            db[b] = 10.0 * std::log10 (std::max (1.0e-300, a[b]) / std::max (1.0e-300, reference[b]));

        return db;
    }

    [[nodiscard]] double worstOf (const Bands& db)
    {
        double worst = 0.0;

        for (double v : db)
            worst = std::max (worst, std::abs (v));

        return worst;
    }

    // --- a linear-phase filter with a given gain at the seven octave centres --------
    constexpr int kTaps = 4097;

    /** Gain in dB at the octave centres, joined by straight lines on a
        logarithmic frequency axis and held flat beyond the ends. Frequency
        sampled on an 8192-point grid, Hann-windowed. */
    [[nodiscard]] std::vector<double> designFilter (const Bands& db)
    {
        constexpr int grid = 8192;

        std::vector<double> amplitude (grid / 2 + 1);

        for (int k = 0; k <= grid / 2; ++k)
        {
            const double hz = std::max (1.0, k * kRate / grid);
            const double position = std::clamp (std::log2 (hz / kBands[0]), 0.0, 6.0);
            const int lower = std::min (5, static_cast<int> (position));
            const double t = position - lower;

            const double gain = db[static_cast<size_t> (lower)] * (1.0 - t) + db[static_cast<size_t> (lower) + 1] * t;

            amplitude[static_cast<size_t> (k)] = std::pow (10.0, gain / 20.0);
        }

        std::vector<double> h (kTaps);
        const int centre = (kTaps - 1) / 2;

        for (int n = 0; n < kTaps; ++n)
        {
            const int d = n - centre;

            double sum = amplitude[0] + amplitude[grid / 2] * ((d & 1) != 0 ? -1.0 : 1.0);

            for (int k = 1; k < grid / 2; ++k)
                sum += 2.0 * amplitude[static_cast<size_t> (k)] * std::cos (kTwoPi * k * d / grid);

            const double window = 0.5 - 0.5 * std::cos (kTwoPi * n / (kTaps - 1));

            h[static_cast<size_t> (n)] = window * sum / grid;
        }

        return h;
    }

    /** Filtered, with the filter's own delay taken out: same length, aligned. */
    [[nodiscard]] Stereo filtered (const Stereo& x, const std::vector<double>& h)
    {
        const auto centre = static_cast<std::ptrdiff_t> ((h.size() - 1) / 2);
        const auto length = static_cast<std::ptrdiff_t> (x.l.size());
        const auto taps   = static_cast<std::ptrdiff_t> (h.size());

        Stereo out;
        out.l.assign (x.l.size(), 0.0);
        out.r.assign (x.r.size(), 0.0);

        structural::runParallel (16, [&] (size_t part)
        {
            const auto from = length * static_cast<std::ptrdiff_t> (part) / 16;
            const auto to   = length * static_cast<std::ptrdiff_t> (part + 1) / 16;

            for (auto n = from; n < to; ++n)
            {
                const auto first = std::max<std::ptrdiff_t> (0, n + centre - length + 1);
                const auto last  = std::min<std::ptrdiff_t> (taps - 1, n + centre);

                double l = 0.0, r = 0.0;

                for (auto k = first; k <= last; ++k)
                {
                    const auto i = static_cast<size_t> (n + centre - k);

                    l += h[static_cast<size_t> (k)] * x.l[i];
                    r += h[static_cast<size_t> (k)] * x.r[i];
                }

                out.l[static_cast<size_t> (n)] = l;
                out.r[static_cast<size_t> (n)] = r;
            }
        });

        return out;
    }

    /** The filter that moves `ir`'s octave shares to `wantDb` against
        `reference`, as the octave measure itself reads them. The measure's
        bands overlap and the filter has finite resolution, so the gains at
        the centres are corrected until the reading agrees. */
    [[nodiscard]] std::vector<double> fitFilter (const Stereo& ir, const Bands& reference, const Bands& wantDb,
                                                 Bands& achieved)
    {
        Bands design {};
        const Bands start = dbBetween (sharesOf (ir), reference);

        for (size_t b = 0; b < 7; ++b)
            design[b] = wantDb[b] - start[b];

        std::vector<double> h;

        for (int pass = 0; pass < 10; ++pass)
        {
            h = designFilter (design);
            achieved = dbBetween (sharesOf (filtered (ir, h)), reference);

            double worst = 0.0;

            for (size_t b = 0; b < 7; ++b)
            {
                const double error = wantDb[b] - achieved[b];

                design[b] += error;
                worst = std::max (worst, std::abs (error));
            }

            if (worst < 0.05)
                break;
        }

        return h;
    }

    [[nodiscard]] std::string bandRow (const Bands& db)
    {
        std::string row;

        for (double v : db)
        {
            std::string cell = (v >= 0.0 ? "+" : "") + fx (v, 2);
            cell.resize (8, ' ');
            row += cell;
        }

        return row;
    }

    enum Kind { kHall, kA1b, kAT5a, kHallTinted, kA1bFlat };

    constexpr const char* kKindNames[]
    {
        "Hall",
        "Ambient A1b",
        "Ambient AT5a (8 % direct; tone repaired, REJECTED by Hall's search)",
        "Hall + A1b's octave fingerprint (linear-phase filter; a control, not a candidate)",
        "A1b with its octave fingerprint filtered off (a control, not a candidate)"
    };

    constexpr const char* kShortNames[] { "Hall", "A1b", "AT5a", "Hall + fingerprint", "A1b, fingerprint off" };
}

int exportPhase12Stage7AmbientPack (const std::string& directory, double)
{
    // structural::kPoints: medium, small / short, large / long.
    const char* const pointLabels[] { "medium (Size 60, Decay 2.5 s)",
                                      "short (Size 25, Decay 0.8 s)",
                                      "long (Size 85, Decay 5.0 s)" };

    enum { M = 0, S = 1, L = 2 };

    struct Trial
    {
        const char* group;
        Kind first, second;     // `first` is the reference side
        const char* source;
        int point;
        bool hardLeft, monoFold;
        int file;
        bool referenceFirst;
    };

    // File numbers and orders are fixed, not random, and follow no pattern a
    // listener could learn.
    const Trial trials[]
    {
        { "Hall vs A1b, centred",            kHall, kA1b, "vocal",      S, false, false, 14, true  },
        { "Hall vs A1b, centred",            kHall, kA1b, "vocal",      M, false, false,  3, false },
        { "Hall vs A1b, centred",            kHall, kA1b, "vocal",      L, false, false, 22, true  },
        { "Hall vs A1b, centred",            kHall, kA1b, "piano",      S, false, false,  9, false },
        { "Hall vs A1b, centred",            kHall, kA1b, "piano",      M, false, false, 17, true  },
        { "Hall vs A1b, centred",            kHall, kA1b, "piano",      L, false, false,  1, false },
        { "Hall vs A1b, centred",            kHall, kA1b, "snare",      S, false, false, 25, true  },
        { "Hall vs A1b, centred",            kHall, kA1b, "snare",      M, false, false, 11, false },
        { "Hall vs A1b, centred",            kHall, kA1b, "snare",      L, false, false,  6, true  },
        { "Hall vs A1b, centred",            kHall, kA1b, "percussion", S, false, false, 19, false },

        { "Hall vs A1b, folded to mono",     kHall, kA1b, "vocal",      M, false, true,   8, true  },
        { "Hall vs A1b, folded to mono",     kHall, kA1b, "snare",      S, false, true,  21, false },
        { "Hall vs A1b, folded to mono",     kHall, kA1b, "piano",      L, false, true,  13, false },

        { "Hall vs A1b, hard-left source",   kHall, kA1b, "snare",      M, true,  false, 27, true  },
        { "Hall vs A1b, hard-left source",   kHall, kA1b, "vocal",      S, true,  false,  5, false },

        { "A1b vs AT5a",                     kA1b, kAT5a, "vocal",      S, false, false, 16, false },
        { "A1b vs AT5a",                     kA1b, kAT5a, "snare",      S, false, false,  2, true  },
        { "A1b vs AT5a",                     kA1b, kAT5a, "piano",      M, false, false, 24, true  },
        { "A1b vs AT5a",                     kA1b, kAT5a, "piano",      L, false, false, 10, false },

        { "TONE ONLY: Hall vs Hall + fingerprint",        kHall, kHallTinted, "piano", M, false, false, 20, false },
        { "TONE ONLY: Hall vs Hall + fingerprint",        kHall, kHallTinted, "vocal", M, false, false,  7, true  },
        { "TONE ONLY: Hall vs Hall + fingerprint",        kHall, kHallTinted, "piano", S, false, false, 26, true  },

        { "STRUCTURE ONLY: Hall vs A1b, fingerprint off", kHall, kA1bFlat,    "piano", M, false, false, 12, true  },
        { "STRUCTURE ONLY: Hall vs A1b, fingerprint off", kHall, kA1bFlat,    "vocal", S, false, false, 18, false },
        { "STRUCTURE ONLY: Hall vs A1b, fingerprint off", kHall, kA1bFlat,    "snare", S, false, false,  4, false },

        { "CATCH: identical halves",         kHall, kHall, "piano",     M, false, false, 15, true  },
        { "CATCH: identical halves",         kA1b,  kA1b,  "vocal",     M, false, false, 23, true  }
    };

    constexpr int kFiles = 27;

    // --- sources -------------------------------------------------------------------
    struct Source { std::string name; std::vector<double> m; };
    std::vector<Source> sources;

    sources.push_back ({ "vocal", monoOf (makeVocalReference (kRate, 2.2), 2.2) });
    sources.push_back ({ "piano", pianoPhrase() });

    std::vector<double> chord;

    for (const auto& f : makeFu2Sources (kRate))
    {
        if (f.name == "snare")
            sources.push_back ({ f.name, monoOf (f.frames, 2.74) });

        if (f.name == "piano")
            chord = monoOf (f.frames, 2.74);    // four sines; for the level table only
    }

    sources.push_back ({ "percussion", monoOf (makeBrightPercussion (kRate, 2.0), 2.0) });

    if (sources.size() != 4 || chord.empty())
        return -1;

    for (const auto& s : sources)
        if (s.m.empty())
            return -1;

    // --- structures, as Stage 6 built and calibrated them ------------------------------
    stage6::AmbientSpec a1bSpec;
    stage6::AmbientSpec at5aSpec;
    at5aSpec.name = "AT5a 8 % direct";
    at5aSpec.direct = 0.08;

    const auto a1bFamily  = stage6::familyOf ("Ambient A1b",  stage6::ambientRaw (a1bSpec),  true);
    const auto at5aFamily = stage6::familyOf ("Ambient AT5a", stage6::ambientRaw (at5aSpec), true);

    User users[3];
    Structure hallS[3], a1bS[3], at5aS[3];
    Bands fingerprint[3], at5aDb[3], tintedDb[3], flatDb[3];
    std::vector<double> tint[3], untint[3];
    double sameKnob[3][3] {};
    int sameKnobFamily[3][3] {};

    for (int p = 0; p < 3; ++p)
    {
        users[p] = userAt (kPoints[p]);
        hallS[p] = Structure {};
        a1bS[p]  = a1bFamily.target.make (users[p]);
        at5aS[p] = at5aFamily.target.make (users[p]);

        const double seconds = irSecondsFor (users[p].decay);

        const auto hallIr = impulseResponse (hallS[p], users[p], seconds);
        const auto a1bIr  = impulseResponse (a1bS[p],  users[p], seconds);
        const auto at5aIr = impulseResponse (at5aS[p], users[p], seconds);

        const Bands hallShares = sharesOf (hallIr);

        fingerprint[p] = dbBetween (sharesOf (a1bIr), hallShares);
        at5aDb[p]      = dbBetween (sharesOf (at5aIr), hallShares);

        // Hall moved to A1b's shares; A1b moved to Hall's. `tintedDb` and
        // `flatDb` are what the filters achieve, measured the same way.
        tint[p]   = fitFilter (hallIr, hallShares, fingerprint[p], tintedDb[p]);
        untint[p] = fitFilter (a1bIr,  hallShares, Bands {},       flatDb[p]);

        const Features fh = featuresOf (hallS[p], users[p]);
        const Features fa = featuresOf (a1bS[p],  users[p]);
        const Features ft = featuresOf (at5aS[p], users[p]);

        sameKnob[p][0] = stage5::centredWorst (stage5::sameKnobs (fh, fa), &sameKnobFamily[p][0]);
        sameKnob[p][1] = stage5::centredWorst (stage5::sameKnobs (fh, ft), &sameKnobFamily[p][1]);
        sameKnob[p][2] = stage5::centredWorst (stage5::sameKnobs (fa, ft), &sameKnobFamily[p][2]);
    }

    std::ofstream key (directory + "/KEY.txt", std::ios::binary);
    std::ofstream notes (directory + "/LISTENING.md", std::ios::binary);

    if (! key || ! notes)
        return -1;

    // --- the listener's sheet ------------------------------------------------------------
    notes << "# Phase 12 Stage 7 - twenty-seven blind comparisons\n\n"
          << "Each file is ONE comparison. You hear a sound through one reverb,\n"
          << "half a second of silence, then the SAME sound through another reverb.\n"
          << "Call them FIRST and SECOND.\n\n"
          << "What each file is, is in KEY.txt. Do not open it until you are done.\n\n"
          << "- Headphones or your normal monitors, a comfortable level, and leave\n"
          << "  the level alone once set. If you can, do one pass on each.\n"
          << "- Play a file as often as you like. Any order. Take breaks.\n"
          << "- It is all reverb and no dry signal, on purpose.\n"
          << "- Some files are mono, and in two the sound sits on the left. Intended.\n"
          << "- There are no right answers. 'Same' is a useful answer, and at least\n"
          << "  two files really are the same thing twice.\n\n"
          << "## For every file, six questions\n\n"
          << "1. **Same or different?**  same / slightly different / clearly different\n"
          << "2. **If different, which is it?**\n"
          << "   - two different KINDS of reverb\n"
          << "   - the same kind, with a different tone (as if one had an EQ on it)\n"
          << "   - the same kind, set slightly differently\n"
          << "   - cannot say\n"
          << "3. **Which words fit, and for which half?** (FIRST / SECOND / neither)\n"
          << "   - thinner, less low body\n"
          << "   - fuller, warmer\n"
          << "   - sounds filtered or equalised, not natural\n"
          << "   - blooms later; more around the sound than behind it\n"
          << "   - smoother, more even tail\n"
          << "   - boxy, coloured or ringing\n"
          << "4. **Is anything about the TONE of either half objectionable?**\n"
          << "   no / FIRST / SECOND - and what\n"
          << "5. **Which would you rather use?**  FIRST / SECOND / no preference\n"
          << "6. **Would you want BOTH as separate choices in a plugin?**  yes / no\n\n"
          << "A line or two in your own words is worth more than the boxes.\n\n"
          << "## Answer sheet\n\n"
          << "| file | 1 same / different | 2 which | 3 words | 4 tone objectionable | 5 prefer | 6 both? | notes |\n"
          << "|---|---|---|---|---|---|---|---|\n";

    for (int f = 1; f <= kFiles; ++f)
        notes << "| s7-" << (f < 10 ? "0" : "") << f << " | | | | | | | |\n";

    notes << "\n## After all the files, and after opening KEY.txt\n\n"
          << "Four questions about Ambient A1b as a whole, in your own words:\n\n"
          << "1. Does it sound like a genuinely different reverb family from Hall?\n"
          << "2. Is its low-frequency difference from Hall objectionable?\n"
          << "3. Does it sound like unintended EQ, or like the way this reverb is?\n"
          << "4. Would that tone get in the way of a Modern / Classic / Vintage\n"
          << "   Character choice laid over it later?\n";

    // --- the key ---------------------------------------------------------------------------
    key << "Phase 12 Stage 7 - key. Open only after listening.\n\n"
        << "All files: 100 % wet, 48 kHz, Early/Late 0, every other control at its\n"
        << "default and equal for both halves. The second-named reverb is level-\n"
        << "matched to the first by integrated energy (gain shown). 'mono' files\n"
        << "are (L + R) / 2 on both channels, matched after folding.\n\n"
        << "OBJECTIVE NOTES. Octave-band energy share against Hall's, dB, measured on\n"
        << "the impulse response at the same knobs. The gate is 1.0 dB at the\n"
        << "defaults and over a 27-point grid; A1b FAILS it (1.82 dB at the defaults,\n"
        << "3.02 over the grid). Nothing heard here changes that.\n\n";

    for (int p = 0; p < 3; ++p)
    {
        key << "  " << pointLabels[p] << "\n"
            << "                                   125     250     500     1k      2k      4k      8k      worst\n"
            << "    A1b                            " << bandRow (fingerprint[p]) << fx (worstOf (fingerprint[p]), 2) << "\n"
            << "    AT5a                           " << bandRow (at5aDb[p])      << fx (worstOf (at5aDb[p]), 2) << "\n"
            << "    Hall + fingerprint (control)   " << bandRow (tintedDb[p])    << fx (worstOf (tintedDb[p]), 2) << "\n"
            << "    A1b, fingerprint off (control) " << bandRow (flatDb[p])      << fx (worstOf (flatDb[p]), 2) << "\n"
            << "    structural metric at the same knobs, centred (>= 3.0 reads as a different structure):\n"
            << "      Hall vs A1b   " << fx (sameKnob[p][0], 2) << "  (" << kFamilyNames[sameKnobFamily[p][0]] << ")\n"
            << "      Hall vs AT5a  " << fx (sameKnob[p][1], 2) << "  (" << kFamilyNames[sameKnobFamily[p][1]] << ")\n"
            << "      A1b vs AT5a   " << fx (sameKnob[p][2], 2) << "  (" << kFamilyNames[sameKnobFamily[p][2]] << ")\n\n";
    }

    key << "The same-knob figures are not the gate. The gate is Hall's SEARCH (its\n"
        << "controls free to move): A1b 9.77 / 4.00 / 15.33, SURVIVES; AT5a 3.97 /\n"
        << "2.06 / 4.63, REJECTED at short.\n\n"
        << "The two filtered structures are listening controls. An output equaliser\n"
        << "is not an Ambient candidate.\n\n";

    // --- how much the level of each structure depends on the source ---------------------------
    {
        const Structure sibling = withDelays ("sibling", mode12::hallSibling().delaysMs);

        std::vector<Source> all = sources;
        all.push_back ({ "four-sine chord", chord });

        key << "LEVEL BY SOURCE. Every structure is set once, on its impulse response at\n"
            << "the defaults, to Hall's energy. This is the gain that would then still be\n"
            << "needed to match Hall on each source, dB (centred, stereo). The sibling is\n"
            << "Hall with a different delay set: what 'the same reverb' needs.\n\n"
            << "    source            setting   Hall sibling   A1b       AT5a\n";

        for (const auto& source : all)
            for (int p = 0; p < 3; ++p)
            {
                const User& u = users[p];
                const size_t length = source.m.size() + static_cast<size_t> ((u.decay * 0.9 + 0.8) * kRate);

                const Structure* list[] { &hallS[p], &sibling, &a1bS[p], &at5aS[p] };
                double energy[4] {};

                structural::runParallel (4, [&] (size_t i)
                {
                    energy[i] = energyOf (std::make_unique<Rig> (*list[i], u)->render (source.m, source.m, length));
                });

                std::string name = source.name;
                name.resize (18, ' ');

                std::string row = "    " + name + (p == 0 ? "medium    " : p == 1 ? "short     " : "long      ");

                for (int i = 1; i < 4; ++i)
                {
                    const double db = 10.0 * std::log10 (energy[0] / std::max (1.0e-300, energy[i]));

                    std::string cell = (db >= 0.0 ? "+" : "") + fx (db, 2);
                    cell.resize (i == 1 ? 15 : 10, ' ');
                    row += cell;
                }

                key << row << "\n";
            }

        key << "\n";
    }

    // --- the files ---------------------------------------------------------------------------
    int written = 0;

    for (int f = 1; f <= kFiles; ++f)
        for (const auto& t : trials)
        {
            if (t.file != f)
                continue;

            const Source* source = nullptr;

            for (const auto& s : sources)
                if (s.name == t.source)
                    source = &s;

            if (source == nullptr)
                return -1;

            const int p = t.point;
            const User& u = users[p];
            const auto& m = source->m;
            const size_t tail = static_cast<size_t> ((u.decay * 0.9 + 0.8) * kRate);

            double foldDb[2] {};
            int rendered = 0;

            auto render = [&] (Kind kind)
            {
                const Structure& s = kind == kHall || kind == kHallTinted ? hallS[p]
                                   : kind == kAT5a ? at5aS[p] : a1bS[p];

                const std::vector<double> silent (m.size(), 0.0);

                Stereo x = std::make_unique<Rig> (s, u)->render (m, t.hardLeft ? silent : m, m.size() + tail);

                if (kind == kHallTinted) x = filtered (x, tint[p]);
                if (kind == kA1bFlat)    x = filtered (x, untint[p]);

                if (t.monoFold)
                {
                    const double stereo = energyOf (x);

                    for (size_t n = 0; n < x.l.size(); ++n)
                        x.l[n] = x.r[n] = 0.5 * (x.l[n] + x.r[n]);

                    // Mono power against the mean power of the two channels.
                    foldDb[rendered] = 10.0 * std::log10 (std::max (1.0e-300, energyOf (x))
                                                          / std::max (1.0e-300, stereo));
                }

                ++rendered;

                return x;
            };

            auto a = render (t.first);
            auto b = render (t.second);

            const double gain = std::sqrt (energyOf (a) / std::max (1.0e-300, energyOf (b)));

            for (auto& v : b.l) v *= gain;
            for (auto& v : b.r) v *= gain;

            const Stereo& one = t.referenceFirst ? a : b;
            const Stereo& two = t.referenceFirst ? b : a;

            // One level for the whole file, so the match between the halves
            // survives: the louder half peaks at -6 dBFS.
            double peak = 0.0;

            for (const Stereo* x : { &one, &two })
                for (size_t n = 0; n < x->l.size(); ++n)
                    peak = std::max ({ peak, std::abs (x->l[n]), std::abs (x->r[n]) });

            const double level = peak > 0.0 ? 0.5 / peak : 1.0;

            const size_t gap  = static_cast<size_t> (0.5 * kRate);
            const size_t fade = static_cast<size_t> (0.020 * kRate);

            std::vector<StereoFrame> frames;
            frames.reserve (one.l.size() + two.l.size() + gap);

            for (const Stereo* x : { &one, &two })
            {
                const size_t n = x->l.size();

                for (size_t i = 0; i < n; ++i)
                {
                    const double g = level * (i + fade >= n ? static_cast<double> (n - 1 - i)
                                                              / static_cast<double> (fade) : 1.0);

                    frames.push_back ({ x->l[i] * g, x->r[i] * g });
                }

                if (x == &one)
                    frames.insert (frames.end(), gap, StereoFrame {});
            }

            const std::string name = std::string ("s7-") + (f < 10 ? "0" : "") + std::to_string (f);

            if (! writeStereoWav (directory + "/" + name + ".wav", frames, kRate))
                return -1;

            key << name << "  " << t.group << "\n"
                << "        source " << t.source
                << (t.hardLeft ? ", hard left" : ", centred") << (t.monoFold ? ", output folded to mono" : "")
                << "; " << pointLabels[p] << "\n"
                << "        FIRST  = " << kKindNames[t.referenceFirst ? t.first : t.second] << "\n"
                << "        SECOND = " << kKindNames[t.referenceFirst ? t.second : t.first] << "\n"
                << "        level match applied to " << kShortNames[t.second] << ": "
                << fx (20.0 * std::log10 (gain), 2) << " dB\n";

            if (t.monoFold)
                key << "        fold-down, mono against stereo, before matching: " << kShortNames[t.first] << " "
                    << fx (foldDb[0], 2) << " dB, " << kShortNames[t.second] << " " << fx (foldDb[1], 2) << " dB\n";

            key << "\n";

            ++written;
        }

    return written;
}

} // namespace avtest
