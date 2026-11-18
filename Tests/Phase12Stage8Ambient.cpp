#include "Phase12Stage8Ambient.h"
#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DampingListening.h"
#include "DspAnalysis.h"
#include "SignalExport.h"
#include "WetGainStaging.h"

#include <algorithm>
#include <cmath>
#include <memory>

/*
    PHASE 12 - STAGE 8. Ambient: the diagnosis.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    DA0 - DA6 of Docs/Phase12-Stage8-Ambient-Repair-Preregistration.md,
    section 4. The reading rule is that document's and is applied here as
    written. The variants in DA4 are probes, not candidates.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace stage8
{
    namespace
    {
        constexpr double kTwoPi = 6.283185307179586;
        constexpr double kBands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
        constexpr int kSeedCount = 12;

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

        /** Stage 7's piano phrase, parameter for parameter. */
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

        // --- Stage 7's linear-phase control filter, as it was -------------------------
        constexpr int kTaps = 4097;

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

        [[nodiscard]] Stereo filtered (const Stereo& x, const std::vector<double>& h)
        {
            const auto centre = static_cast<std::ptrdiff_t> ((h.size() - 1) / 2);
            const auto length = static_cast<std::ptrdiff_t> (x.l.size());
            const auto taps   = static_cast<std::ptrdiff_t> (h.size());

            Stereo out;
            out.l.assign (x.l.size(), 0.0);
            out.r.assign (x.r.size(), 0.0);

            runParallel (16, [&] (size_t part)
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

        [[nodiscard]] std::vector<double> fitFilter (const Stereo& ir, const Bands& reference, Bands& achieved)
        {
            Bands design {};
            const Bands start = dbBetween (sharesOf (ir), reference);

            for (size_t b = 0; b < 7; ++b)
                design[b] = -start[b];

            std::vector<double> h;

            for (int pass = 0; pass < 10; ++pass)
            {
                h = designFilter (design);
                achieved = dbBetween (sharesOf (filtered (ir, h)), reference);

                double worst = 0.0;

                for (size_t b = 0; b < 7; ++b)
                {
                    design[b] -= achieved[b];
                    worst = std::max (worst, std::abs (achieved[b]));
                }

                if (worst < 0.05)
                    break;
            }

            return h;
        }

        [[nodiscard]] std::string signedText (double v, int digits = 2)
        {
            return (v >= 0.0 ? "+" : "") + fx (v, digits);
        }

        [[nodiscard]] std::string bandRow (const Bands& db)
        {
            std::string row;

            for (double v : db)
                row += pad (signedText (v), 8);

            return row;
        }

        const std::string kBandHead = pad ("125", 8) + pad ("250", 8) + pad ("500", 8) + pad ("1k", 8)
                                    + pad ("2k", 8) + pad ("4k", 8) + pad ("8k", 8);

        struct Moments
        {
            Bands mean {}, sd {};
        };

        [[nodiscard]] Moments momentsOf (const std::vector<Bands>& rows)
        {
            Moments m;
            const double n = static_cast<double> (rows.size());

            for (size_t b = 0; b < 7; ++b)
            {
                double sum = 0.0;

                for (const auto& r : rows)
                    sum += r[b];

                m.mean[b] = sum / n;

                double sq = 0.0;

                for (const auto& r : rows)
                    sq += (r[b] - m.mean[b]) * (r[b] - m.mean[b]);

                m.sd[b] = std::sqrt (sq / std::max (1.0, n - 1.0));
            }

            return m;
        }

        /** Octave deviation from unperturbed Hall, over the perturbation seeds. */
        [[nodiscard]] Moments perturbedMoments (const Maker& make, const User& u)
        {
            const auto hall = stage6::octaveShares (Structure {}, u);

            std::vector<Bands> rows (kSeedCount);

            runParallel (rows.size(), [&] (size_t k)
            {
                (void) stage6::octaveDeviation (stage6::octaveShares (perturbed (make (u), static_cast<unsigned int> (k + 1)), u),
                                                hall, &rows[k]);
            });

            return momentsOf (rows);
        }

        [[nodiscard]] double meanOf (const Bands& b)
        {
            double sum = 0.0;

            for (double v : b)
                sum += v;

            return sum / 7.0;
        }

        /** Third-octave shares, 100 Hz - 10 kHz: 21 bands. */
        [[nodiscard]] std::array<double, 21> thirdOctaveShares (const Stereo& ir)
        {
            std::array<double, 21> share {};
            double total = 0.0;

            for (size_t b = 0; b < 21; ++b)
            {
                const double hz = 100.0 * std::pow (2.0, static_cast<double> (b) / 3.0);
                const double w0 = kTwoPi * hz / kRate;
                const double q = 4.318;                     // one third of an octave
                const double alpha = std::sin (w0) / (2.0 * q);

                const double b0 = alpha / (1.0 + alpha), b2 = -b0;
                const double a1 = -2.0 * std::cos (w0) / (1.0 + alpha), a2 = (1.0 - alpha) / (1.0 + alpha);

                double energy = 0.0;

                for (const auto* channel : { &ir.l, &ir.r })
                {
                    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

                    for (double x : *channel)
                    {
                        const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;

                        x2 = x1; x1 = x;
                        y2 = y1; y1 = y;

                        energy += y * y;
                    }
                }

                share[b] = energy;
                total += energy;
            }

            for (auto& v : share)
                v /= std::max (1.0e-300, total);

            return share;
        }

        /** Standard deviation, over the 21 bands, of dB against a reference. */
        [[nodiscard]] double roughness (const std::array<double, 21>& a, const std::array<double, 21>& reference)
        {
            double db[21] {}, mean = 0.0;

            for (size_t b = 0; b < 21; ++b)
            {
                db[b] = 10.0 * std::log10 (std::max (1.0e-300, a[b]) / std::max (1.0e-300, reference[b]));
                mean += db[b] / 21.0;
            }

            double sq = 0.0;

            for (double v : db)
                sq += (v - mean) * (v - mean);

            return std::sqrt (sq / 20.0);
        }
    }

    Structure perturbed (Structure s, unsigned int seed)
    {
        if (seed == 0)
            return s;

        unsigned int state = 0x9E3779B9u * seed + 0x7F4A7C15u;

        for (auto& ms : s.delaysMs)
        {
            state = state * 1664525u + 1013904223u;

            const double u = static_cast<double> (state >> 8) / static_cast<double> (1u << 24) * 2.0 - 1.0;

            ms *= 1.0 + 0.04 * u;
        }

        return s;
    }

    const std::vector<Source>& sources()
    {
        static const std::vector<Source> list = []
        {
            std::vector<Source> s;

            s.push_back ({ "vocal", monoOf (makeVocalReference (kRate, 2.2), 2.2), true });

            for (const auto& f : makeFu2Sources (kRate))
                if (f.name == "snare")
                    s.push_back ({ f.name, monoOf (f.frames, 2.74), true });

            s.push_back ({ "percussion", monoOf (makeBrightPercussion (kRate, 2.0), 2.0), true });
            s.push_back ({ "piano phrase", pianoPhrase(), false });

            return s;
        }();

        return list;
    }

    std::vector<double> sourceGains (const Structure& s, const User& u, double* lawDb)
    {
        const auto& list = sources();
        const double seconds = irSecondsFor (u.decay);

        const double hallIr = energyOf (impulseResponse (Structure {}, u, seconds));
        const double ownIr  = energyOf (impulseResponse (s, u, seconds));

        const double law = 10.0 * std::log10 (hallIr / std::max (1.0e-300, ownIr));

        if (lawDb != nullptr)
            *lawDb = law;

        std::vector<double> gains (list.size(), 0.0);

        runParallel (list.size(), [&] (size_t i)
        {
            const auto& m = list[i].m;
            const size_t length = m.size() + static_cast<size_t> ((u.decay * 0.9 + 0.8) * kRate);

            const double hall = energyOf (std::make_unique<Rig> (Structure {}, u)->render (m, m, length));
            const double own  = energyOf (std::make_unique<Rig> (s, u)->render (m, m, length));

            gains[i] = 10.0 * std::log10 (hall / std::max (1.0e-300, own)) - law;
        });

        return gains;
    }

    double spreadOf (const std::vector<double>& gains)
    {
        const auto& list = sources();

        double low = 1.0e30, high = -1.0e30;

        for (size_t i = 0; i < gains.size() && i < list.size(); ++i)
            if (list[i].gates)
            {
                low = std::min (low, gains[i]);
                high = std::max (high, gains[i]);
            }

        return high - low;
    }
}

// ===========================================================================
//  The diagnosis
// ===========================================================================
void runPhase12Stage8AmbientDiagnosis()
{
    using namespace stage8;
    using stage6::AmbientSpec;

    const char* const pointName[3] { "medium", "short", "long" };

    auto familyFor = [] (const AmbientSpec& spec)
    {
        return stage6::familyOf ("Ambient " + spec.name, stage6::ambientRaw (spec), true).target;
    };

    const AmbientSpec a1bSpec;
    const Candidate a1b = familyFor (a1bSpec);

    AmbientSpec at5aSpec;
    at5aSpec.name = "AT5a";
    at5aSpec.direct = 0.08;

    const Candidate at5a = familyFor (at5aSpec);

    const Structure sibling = withDelays ("sibling", mode12::hallSibling().delaysMs);

    // =====================================================================
    beginSuite ("PHASE 12.8.0 - DA0: is A1b with Hall's tone still A1b to the metric?");
    // =====================================================================

    info ("  Stage 7's control: A1b's own response through a linear-phase filter that moves its");
    info ("  octave shares onto Hall's. Structure untouched. Frozen metric, same knobs, against");
    info ("  A1b. Requirement 2 asks a repair for <= 1.5; if this control is over 1.5, no tonal");
    info ("  repair can meet it. 'filter alone' is the same process with a flat filter.");
    info ("");

    {
        double flat[3] {}, plain[3] {}, residual[3] {};
        int family[3] {};

        for (int p = 0; p < 3; ++p)
        {
            const User u = userAt (kPoints[p]);
            const Structure s = a1b.make (u);

            const auto centre = impulseResponse (s, u, irSecondsFor (u.decay));
            const auto left   = impulseResponse (s, u, kLeftOnlySeconds, true, false);

            const Features own = analyse (centre, left);

            Bands achieved {};
            const auto h = fitFilter (centre, sharesOf (impulseResponse (Structure {}, u, irSecondsFor (u.decay))), achieved);
            const auto unity = designFilter (Bands {});

            for (double v : achieved)
                residual[p] = std::max (residual[p], std::abs (v));

            flat[p]  = stage5::centredWorst (stage5::sameKnobs (own, analyse (filtered (centre, h), filtered (left, h))), &family[p]);

            const auto unityCentre = filtered (centre, unity), unityLeft = filtered (left, unity);

            plain[p] = stage5::centredWorst (stage5::sameKnobs (own, analyse (unityCentre, unityLeft)));

            // How far the flat filter is from doing nothing, and what the
            // metric makes of exactly nothing and of a tiny change.
            double worst = 0.0, peak = 0.0;
            size_t firstNonZero = centre.l.size();

            for (size_t n = 0; n < centre.l.size(); ++n)
            {
                worst = std::max (worst, std::abs (unityCentre.l[n] - centre.l[n]));
                peak = std::max (peak, std::abs (centre.l[n]));

                if (firstNonZero == centre.l.size() && centre.l[n] != 0.0)
                    firstNonZero = n;
            }

            const auto score = stage5::sameKnobs (own, analyse (unityCentre, unityLeft));
            std::string families;

            for (int f = 0; f < kFamilies; ++f)
                if (score.scaled[f] > 0.05)
                    families += std::string (kFamilyNames[f]) + " " + fx (score.scaled[f]) + "; ";

            info ("    [" + std::string (pointName[p]) + "] flat filter: worst |difference| / peak " + fx (worst / peak * 1.0e15, 2)
                  + " e-15; first non-zero sample " + std::to_string (firstNonZero)
                  + "; identical input scores " + fx (stage5::centredWorst (stage5::sameKnobs (own, analyse (centre, left))))
                  + "; families: " + families);
        }

        info ("    " + pad ("", 28) + pad ("medium", 10) + pad ("short", 10) + "long");
        info ("    " + pad ("filter alone", 28) + pad (fx (plain[0]), 10) + pad (fx (plain[1]), 10) + fx (plain[2]));
        info ("    " + pad ("A1b, fingerprint off", 28) + pad (fx (flat[0]), 10) + pad (fx (flat[1]), 10) + fx (flat[2]));
        info ("    " + pad ("  separating family", 28) + pad (kFamilyNames[family[0]], 10) + " / " + kFamilyNames[family[1]]
              + " / " + kFamilyNames[family[2]]);
        info ("    " + pad ("  tone left after the filter", 28) + pad (fx (residual[0]), 10) + pad (fx (residual[1]), 10) + fx (residual[2]) + "  dB");
        info ("");

        const bool reachable = flat[0] <= kReproduced && flat[1] <= kReproduced && flat[2] <= kReproduced;

        info (std::string ("  READING: requirement 2 is ") + (reachable ? "REACHABLE by a tonal repair."
                                                                          : "NOT REACHABLE by a tonal repair as the metric reads it."));
        check (reachable, "FINDING: A1b with Hall's tone reads as A1b (<= 1.5) at all three points");
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.1 - DA1, DA2: systematic or variance?");
    // =====================================================================

    info ("  Octave deviation from Hall, dB, with every delay perturbed by up to +/- 4 %: twelve");
    info ("  seeds. MEAN is what does not depend on where the modes fall (S). SD is what does (V).");
    info ("  Hall perturbed the same way, against itself, is the floor.");
    info ("  Rule: S present if |mean| >= 0.5 and >= 2 standard errors; V present if the SD is");
    info ("  >= 0.5 and >= twice Hall's.");
    info ("");

    Moments damped[3], undampedM[3], hallM[3];

    auto hallMaker = [] (const User&) { return Structure {}; };

    for (int p = 0; p < 3; ++p)
    {
        User u = userAt (kPoints[p]);

        damped[p] = perturbedMoments (a1b.make, u);
        hallM[p]  = perturbedMoments (hallMaker, u);

        u.lowDamp = 0.0;
        u.highDamp = 0.0;

        undampedM[p] = perturbedMoments (a1b.make, u);
    }

    int systematic = 0, variance = 0;

    for (int p = 0; p < 3; ++p)
    {
        info ("    " + pad (pointName[p], 30) + kBandHead);
        info ("    " + pad ("  A1b mean (S)", 30) + bandRow (damped[p].mean));
        info ("    " + pad ("  A1b SD (V)", 30) + bandRow (damped[p].sd));
        info ("    " + pad ("  Hall SD (floor)", 30) + bandRow (hallM[p].sd));
        info ("    " + pad ("  A1b mean, damping off", 30) + bandRow (undampedM[p].mean));
        info ("    " + pad ("  A1b SD, damping off", 30) + bandRow (undampedM[p].sd));

        std::string s = "    " + pad ("  present", 30);

        for (size_t b = 0; b < 7; ++b)
        {
            const double se = damped[p].sd[b] / std::sqrt (static_cast<double> (kSeedCount));

            const bool isS = std::abs (damped[p].mean[b]) >= 0.5 && std::abs (damped[p].mean[b]) >= 2.0 * se;
            const bool isV = damped[p].sd[b] >= 0.5 && damped[p].sd[b] >= 2.0 * hallM[p].sd[b];

            systematic += isS ? 1 : 0;
            variance += isV ? 1 : 0;

            s += pad (std::string (isS ? "S" : "-") + (isV ? "V" : "-"), 8);
        }

        info (s);
        info ("");
    }

    info ("  READING: S present in " + std::to_string (systematic) + " of 21 band-points, V in "
          + std::to_string (variance) + " of 21.");
    info ("  Mean of the SD over the seven bands, A1b / Hall: medium " + fx (meanOf (damped[0].sd)) + " / " + fx (meanOf (hallM[0].sd))
          + ", short " + fx (meanOf (damped[1].sd)) + " / " + fx (meanOf (hallM[1].sd))
          + ", long " + fx (meanOf (damped[2].sd)) + " / " + fx (meanOf (hallM[2].sd)));

    check (systematic + variance > 0, "FINDING: the diagnosis finds something - S or V is present somewhere");

    // =====================================================================
    beginSuite ("PHASE 12.8.2 - DA3: roughness at third-octave scale");
    // =====================================================================

    info ("  Standard deviation over 21 third-octave bands (100 Hz - 10 kHz) of the share against");
    info ("  Hall's, dB. What a source's spectrum samples.");
    info ("");
    info ("    " + pad ("", 22) + pad ("medium", 10) + pad ("short", 10) + "long");

    {
        double rough[3][3] {};

        for (int p = 0; p < 3; ++p)
        {
            const User u = userAt (kPoints[p]);
            const double seconds = irSecondsFor (u.decay);
            const auto hall = thirdOctaveShares (impulseResponse (Structure {}, u, seconds));

            rough[0][p] = roughness (thirdOctaveShares (impulseResponse (sibling, u, seconds)), hall);
            rough[1][p] = roughness (thirdOctaveShares (impulseResponse (a1b.make (u), u, seconds)), hall);
            rough[2][p] = roughness (thirdOctaveShares (impulseResponse (at5a.make (u), u, seconds)), hall);
        }

        const char* const names[3] { "Hall's sibling", "A1b", "AT5a" };

        for (int k = 0; k < 3; ++k)
            info ("    " + pad (names[k], 22) + pad (fx (rough[k][0]), 10) + pad (fx (rough[k][1]), 10) + fx (rough[k][2]));
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.3 - DA4: which sparseness does the variance come from?");
    // =====================================================================

    info ("  PROBES, NOT CANDIDATES. Mean over the seven bands of the SD across twelve seeds, dB,");
    info ("  and the mean deviation's worst band. Only how many lines are heard or fed changes.");
    info ("");

    {
        struct Probe { std::string name; Maker make; };
        std::vector<Probe> probes;

        probes.push_back ({ "A1b: fed 1st, heard last", a1b.make });

        { AmbientSpec s; s.name = "heard last two"; s.hear = { 0.0, 0.0, 0.5, 1.0 }; probes.push_back ({ "heard last two", familyFor (s).make }); }
        { AmbientSpec s; s.name = "heard all";      s.hear = { 0.25, 0.5, 0.75, 1.0 }; probes.push_back ({ "heard all, weighted", familyFor (s).make }); }

        // Fed into the first two stages: the first stage's entries copied onto the second's.
        probes.push_back ({ "fed first two", [make = a1b.make] (const User& u)
        {
            Structure s = make (u);

            for (auto* v : { &s.customInL, &s.customInR })
            {
                (*v)[2] = (*v)[0];
                (*v)[3] = (*v)[1];

                for (auto& x : *v)
                    x /= std::sqrt (2.0);
            }

            return s;
        } });

        probes.push_back ({ "AT5a: 8 % direct", at5a.make });

        info ("    " + pad ("probe", 28) + pad ("SD medium", 11) + pad ("SD short", 11) + pad ("SD long", 11)
              + "worst |mean| medium / short / long");

        for (const auto& probe : probes)
        {
            double sd[3] {}, worst[3] {};

            for (int p = 0; p < 3; ++p)
            {
                const auto m = perturbedMoments (probe.make, userAt (kPoints[p]));

                sd[p] = meanOf (m.sd);

                for (double v : m.mean)
                    worst[p] = std::max (worst[p], std::abs (v));
            }

            info ("    " + pad (probe.name, 28) + pad (fx (sd[0]), 11) + pad (fx (sd[1]), 11) + pad (fx (sd[2]), 11)
                  + fx (worst[0]) + " / " + fx (worst[1]) + " / " + fx (worst[2]));
        }

        info ("    " + pad ("Hall (floor)", 28) + pad (fx (meanOf (hallM[0].sd)), 11) + pad (fx (meanOf (hallM[1].sd)), 11)
              + fx (meanOf (hallM[2].sd)));
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.4 - DA5: source-to-source consistency, the baselines");
    // =====================================================================

    info ("  Requirement 4. Each structure is level-matched to Hall on its impulse response AT");
    info ("  THE SETTING; the gain still needed to match Hall on each source, dB. Spread: largest");
    info ("  minus smallest over vocal, snare, percussion. The piano phrase does not gate.");
    info ("  'law' is the level match itself: what a static level law over Size and Decay removes.");
    info ("");

    {
        struct Row { std::string name; Maker make; };

        const std::vector<Row> rows
        {
            { "Hall's sibling", [sibling] (const User&) { return sibling; } },
            { "A1b", a1b.make },
            { "AT5a", at5a.make }
        };

        std::string head = "    " + pad ("structure", 18) + pad ("setting", 9);

        for (const auto& s : sources())
            head += pad (s.name, 14);

        info (head + pad ("spread", 9) + "law");

        for (const auto& row : rows)
            for (int p = 0; p < 3; ++p)
            {
                const User u = userAt (kPoints[p]);

                double law = 0.0;
                const auto gains = sourceGains (row.make (u), u, &law);

                std::string line = "    " + pad (p == 0 ? row.name : "", 18) + pad (pointName[p], 9);

                for (double g : gains)
                    line += pad (signedText (g), 14);

                info (line + pad (fx (spreadOf (gains)), 9) + signedText (law));
            }

        info ("");
        info ("    The level law A1b would need, dB, Size 25 / 60 / 85 (rows) x Decay 0.8 / 2.5 / 5.0:");

        constexpr double sizes[3] { 0.25, 0.60, 0.85 }, decays[3] { 0.8, 2.5, 5.0 };

        double law[9] {};

        runParallel (9, [&] (size_t i)
        {
            User u;
            u.size = sizes[i / 3];
            u.decay = decays[i % 3];

            const double seconds = irSecondsFor (u.decay);

            law[i] = 10.0 * std::log10 (energyOf (impulseResponse (Structure {}, u, seconds))
                                        / energyOf (impulseResponse (a1b.make (u), u, seconds)));
        });

        for (int r = 0; r < 3; ++r)
            info ("      " + pad (signedText (law[r * 3]), 9) + pad (signedText (law[r * 3 + 1]), 9) + signedText (law[r * 3 + 2]));
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.5 - DA6: residence time by band");
    // =====================================================================

    info ("  Energy-weighted mean arrival time of each octave, ms, and A1b / Hall.");
    info ("");

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);
        const double seconds = irSecondsFor (u.decay);

        const auto hall = impulseResponse (Structure {}, u, seconds);
        const auto own  = impulseResponse (a1b.make (u), u, seconds);

        auto centroid = [] (const Stereo& ir, double hz)
        {
            const auto e = detail::bandEnergy (ir, hz, ir.l.size());

            double total = 0.0, moment = 0.0;

            for (size_t n = 0; n < e.size(); ++n)
            {
                total += e[n];
                moment += e[n] * static_cast<double> (n);
            }

            return 1000.0 * moment / std::max (1.0e-300, total) / kRate;
        };

        std::string a = "    " + pad (std::string (pointName[p]) + "  Hall", 22);
        std::string b = "    " + pad ("        A1b", 22);
        std::string c = "    " + pad ("        ratio", 22);

        for (double hz : kBands)
        {
            const double th = centroid (hall, hz), ta = centroid (own, hz);

            a += pad (fx (th, 0), 8);
            b += pad (fx (ta, 0), 8);
            c += pad (fx (ta / th, 2), 8);
        }

        if (p == 0)
            info ("    " + pad ("", 22) + kBandHead);

        info (a);
        info (b);
        info (c);
    }
}

// ===========================================================================
//  Addendum 1: DA7 - DA10
// ===========================================================================
namespace stage8
{
    namespace
    {
        constexpr size_t kBin = 240;        // 5 ms at 48 kHz, the metric's bin

        [[nodiscard]] std::vector<double> envelopeOf (const Stereo& x)
        {
            const size_t bins = x.l.size() / kBin;

            std::vector<double> env (bins, 0.0);

            for (size_t b = 0; b < bins; ++b)
                for (size_t i = 0; i < kBin; ++i)
                    env[b] += x.l[b * kBin + i] * x.l[b * kBin + i] + x.r[b * kBin + i] * x.r[b * kBin + i];

            return env;
        }

        /** White noise added to both channels, `levelDb` below the loudest
            5 ms bin's RMS. The same seed gives the same noise whatever the
            response. levelDb <= 0: nothing is added. */
        [[nodiscard]] Stereo withFloor (Stereo x, double levelDb, unsigned int seed)
        {
            if (levelDb <= 0.0)
                return x;

            const auto env = envelopeOf (x);
            const double peak = std::sqrt (*std::max_element (env.begin(), env.end()) / static_cast<double> (2 * kBin));

            auto add = [&] (std::vector<double>& channel, unsigned int s)
            {
                const auto noise = whiteNoise (channel.size(), s);

                double sq = 0.0;

                for (double v : noise)
                    sq += v * v;

                const double gain = peak * std::pow (10.0, -levelDb / 20.0)
                                  / std::sqrt (std::max (1.0e-300, sq / static_cast<double> (noise.size())));

                for (size_t n = 0; n < channel.size(); ++n)
                    channel[n] += gain * noise[n];
            };

            add (x.l, seed);
            add (x.r, seed + 0x51EDu);

            return x;
        }

        [[nodiscard]] Features flooredFeatures (const Structure& s, const User& u, double levelDb, unsigned int seed)
        {
            return analyse (withFloor (impulseResponse (s, u, irSecondsFor (u.decay)), levelDb, seed),
                            withFloor (impulseResponse (s, u, kLeftOnlySeconds, true, false), levelDb, seed + 7u));
        }
    }
}

void runPhase12Stage8AmbientDiagnosis2()
{
    using namespace stage8;

    const char* const pointName[3] { "medium", "short", "long" };

    const Candidate a1b = stage6::familyOf ("Ambient A1b", stage6::ambientRaw (stage6::AmbientSpec {}), true).target;

    // =====================================================================
    beginSuite ("PHASE 12.8.6 - DA7: are the late-texture bins in numerical dust?");
    // =====================================================================

    info ("  The 5 ms energy envelope against each response's loudest bin: the time, ms, at which");
    info ("  it first passes -200, -120 and -60 dB. The late-texture family reads bins from 30 ms.");
    info ("  Then the family's two figures (dB of spread, 50-200 ms and 200-500 ms).");
    info ("");
    info ("    " + pad ("", 26) + pad ("first", 8) + pad ("-200 dB", 10) + pad ("-120 dB", 10) + pad ("-60 dB", 10)
          + pad ("loudest at", 12) + pad ("fluct 50-200", 14) + "fluct 200-500");

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);

        struct Row { std::string name; Stereo centre, left; };

        std::vector<Row> rows;

        rows.push_back ({ std::string (pointName[p]) + "  Hall", impulseResponse (Structure {}, u, irSecondsFor (u.decay)),
                          impulseResponse (Structure {}, u, kLeftOnlySeconds, true, false) });

        const Structure s = a1b.make (u);

        rows.push_back ({ "        A1b", impulseResponse (s, u, irSecondsFor (u.decay)),
                          impulseResponse (s, u, kLeftOnlySeconds, true, false) });

        const auto unity = designFilter (Bands {});

        rows.push_back ({ "        A1b, flat filter", filtered (rows[1].centre, unity), filtered (rows[1].left, unity) });

        for (const auto& row : rows)
        {
            const auto env = envelopeOf (row.centre);
            const double peak = *std::max_element (env.begin(), env.end());
            const auto loudest = static_cast<size_t> (std::max_element (env.begin(), env.end()) - env.begin());

            auto firstAbove = [&] (double db)
            {
                for (size_t b = 0; b < env.size(); ++b)
                    if (env[b] > peak * std::pow (10.0, db / 10.0))
                        return fx (5.0 * static_cast<double> (b), 0);

                return std::string ("-");
            };

            size_t first = row.centre.l.size();

            for (size_t n = 0; n < row.centre.l.size(); ++n)
                if (row.centre.l[n] != 0.0 || row.centre.r[n] != 0.0)
                {
                    first = n;
                    break;
                }

            const Features f = analyse (row.centre, row.left);

            info ("    " + pad (row.name, 26) + pad (fx (1000.0 * static_cast<double> (first) / kRate, 1), 8)
                  + pad (firstAbove (-200.0), 10) + pad (firstAbove (-120.0), 10) + pad (firstAbove (-60.0), 10)
                  + pad (fx (5.0 * static_cast<double> (loudest), 0), 12) + pad (fx (f.fluctuation[0], 2), 14)
                  + fx (f.fluctuation[1], 2));
        }
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.7 - DA8: the separation with everything below a level masked");
    // =====================================================================

    info ("  A1b against Hall at the same knobs, frozen metric, with the same white noise added to");
    info ("  both responses L dB below each one's loudest 5 ms bin. Worst centred family, its name,");
    info ("  and the late-texture figure. -120 dB is 24 dB under the floor of 16-bit audio.");
    info ("");

    constexpr double levels[5] { 0.0, 200.0, 160.0, 120.0, 100.0 };

    double lateAt[3][5] {};

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);
        const Structure s = a1b.make (u);

        info ("    " + std::string (pointName[p]));

        for (int k = 0; k < 5; ++k)
        {
            const Features fh = flooredFeatures (Structure {}, u, levels[k], 0xF100Du);
            const Features fa = flooredFeatures (s, u, levels[k], 0xF100Du);

            const auto sc = stage5::sameKnobs (fh, fa);

            int family = 0;
            const double worst = stage5::centredWorst (sc, &family);

            lateAt[p][k] = sc.scaled[6];

            std::string others;

            for (int f = 0; f < kFamilies; ++f)
                if (f != 6 && f != stage5::kLateralFamily && sc.scaled[f] >= 1.5)
                    others += std::string (kFamilyNames[f]) + " " + fx (sc.scaled[f]) + "; ";

            info ("      " + pad (levels[k] <= 0.0 ? std::string ("no floor") : "floor -" + fx (levels[k], 0) + " dB", 18)
                  + pad ("worst " + fx (worst) + " (" + kFamilyNames[family] + ")", 34)
                  + pad ("late texture " + fx (sc.scaled[6]), 24) + "others >= 1.5: " + (others.empty() ? "none" : others));
        }

        // Controls: the floor is not itself seen.
        const double hallFloor = stage5::centredWorst (stage5::sameKnobs (featuresOf (Structure {}, u),
                                                                          flooredFeatures (Structure {}, u, 120.0, 0xF100Du)));
        const double seedFloor = stage5::centredWorst (stage5::sameKnobs (flooredFeatures (s, u, 120.0, 0xF100Du),
                                                                          flooredFeatures (s, u, 120.0, 0xBEEFu)));

        info ("      controls at -120 dB: Hall against Hall + floor " + fx (hallFloor)
              + "; A1b + floor against A1b + a different floor " + fx (seedFloor));
        info ("");
    }

    {
        bool below = true;

        for (int p = 0; p < 3; ++p)
            if (lateAt[p][0] >= 3.0 && ! (lateAt[p][3] < 0.5 * lateAt[p][0]))
                below = false;

        info (std::string ("  READING: A1b's unfloored late-texture distance from Hall is ")
              + (below ? "PREDOMINANTLY BELOW -120 dB wherever it exceeds 3.0." : "NOT predominantly below -120 dB."));
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.8 - DA10: is the low-frequency deficit missing phase scrambling?");
    // =====================================================================

    info ("  DA1's mean deviation, damping off, at three modulation depths. If modulation is what");
    info ("  lets the high bands migrate, then with it off every band migrates alike and the gap");
    info ("  between 125 Hz and 1-4 kHz closes. Holds if the gap at depth 0 is under half the");
    info ("  gap at the default.");
    info ("");
    info ("    " + pad ("", 22) + kBandHead + "125 minus 1-4k");

    {
        constexpr double depths[3] { 0.0, 0.30, 0.60 };

        double gap[2][3] {};

        for (int p = 0; p < 2; ++p)
            for (int d = 0; d < 3; ++d)
            {
                User u = userAt (kPoints[p]);
                u.lowDamp = 0.0;
                u.highDamp = 0.0;
                u.modDepth = depths[d];

                const auto m = perturbedMoments (a1b.make, u);

                gap[p][d] = m.mean[0] - (m.mean[3] + m.mean[4] + m.mean[5]) / 3.0;

                info ("    " + pad (std::string (pointName[p]) + ", depth " + fx (depths[d], 2), 22) + bandRow (m.mean)
                      + signedText (gap[p][d]));
            }

        const bool holds = std::abs (gap[0][0]) < 0.5 * std::abs (gap[0][1]) && std::abs (gap[1][0]) < 0.5 * std::abs (gap[1][1]);

        info ("");
        info (std::string ("  READING: the hypothesis ") + (holds ? "HOLDS." : "DOES NOT HOLD."));
        check (holds, "FINDING: with modulation off the low-frequency deficit closes by at least half");
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.9 - DA9: Hall's search against A1b read with a -120 dB floor");
    // =====================================================================

    info ("  The frozen search, five seeds, three points. ONLY THE TARGET IS FLOORED: the search");
    info ("  reads Hall without a floor, as the frozen code does. A diagnostic reading, not G2; it");
    info ("  changes no recorded verdict. Unfloored, Stage 5 and 6: 9.77 / 4.00 / 15.33, lowest 3.56.");
    info ("");

    {
        std::vector<stage5::Search> searches;

        for (int p = 0; p < kPointCount; ++p)
        {
            const User u = userAt (kPoints[p]);
            const Features target = flooredFeatures (a1b.make (u), u, 120.0, 0xF100Du);

            for (int k = 0; k < stage5::kSeeds; ++k)
            {
                stage5::Search s;
                s.label = "A1b, floor -120 dB";
                s.point = p;
                s.target = target;
                s.impostor = Structure {};
                s.start = u;
                s.seed = stage5::seedFor (k);
                searches.push_back (s);
            }
        }

        stage5::runSearches (searches);

        bool survives = true, reproduced = false;
        double lowest = 1.0e30;

        for (int p = 0; p < kPointCount; ++p)
        {
            std::vector<double> v;
            std::string runs;
            const stage5::Search* best = nullptr;

            for (int k = 0; k < stage5::kSeeds; ++k)
            {
                const auto& s = searches[static_cast<size_t> (p * stage5::kSeeds + k)];

                v.push_back (s.residual);
                runs += fx (s.residual) + " ";

                if (best == nullptr || s.residual < best->residual)
                    best = &s;
            }

            std::sort (v.begin(), v.end());

            const double median = v[v.size() / 2];

            lowest = std::min (lowest, v.front());

            if (median < kDistinct)
                survives = false;

            if (v.front() <= kReproduced)
                reproduced = true;

            info ("    " + pad (pointName[p], 9) + "median " + pad (fx (median), 8) + "runs " + pad (runs, 34)
                  + "stops on " + kFamilyNames[best->family]);
            info ("    " + pad ("", 9) + "Hall's knobs in the lowest run: " + best->knobs);
        }

        info ("");
        info ("  A1b, floor -120 dB: " + std::string (reproduced ? "REJECTED" : survives ? "SURVIVES" : "NOT REFUTED / WEAK")
              + ", lowest run " + fx (lowest));
    }
}

// ===========================================================================
//  Addendum 2: DA11 - A1b with its arrival removed
// ===========================================================================
namespace stage8
{
    namespace
    {
        /** Everything before the first 5 ms bin above -200 dB (against the
            loudest bin) set to exactly zero; then moved earlier by
            `advanceMs`, the length kept. Nothing above -200 dB is changed. */
        [[nodiscard]] Stereo arrivalRemoved (Stereo x, double advanceMs)
        {
            const auto env = envelopeOf (x);
            const double peak = *std::max_element (env.begin(), env.end());

            size_t first = 0;

            while (first < env.size() && ! (env[first] > peak * 1.0e-20))
                ++first;

            for (size_t n = 0; n < std::min (x.l.size(), first * kBin); ++n)
                x.l[n] = x.r[n] = 0.0;

            const auto shift = std::min (x.l.size(), static_cast<size_t> (std::lround (advanceMs * 0.001 * kRate)));

            for (auto* channel : { &x.l, &x.r })
            {
                channel->erase (channel->begin(), channel->begin() + static_cast<std::ptrdiff_t> (shift));
                channel->resize (channel->size() + shift, 0.0);
            }

            return x;
        }

        [[nodiscard]] Features arrivalRemovedFeatures (const Structure& s, const User& u, double advanceMs)
        {
            return analyse (arrivalRemoved (impulseResponse (s, u, irSecondsFor (u.decay)), advanceMs),
                            arrivalRemoved (impulseResponse (s, u, kLeftOnlySeconds, true, false), advanceMs));
        }

        /** Hall's frozen search, five seeds, against one target per point. */
        void hallSearch (const std::string& label, const Features target[3])
        {
            const char* const pointName[3] { "medium", "short", "long" };

            std::vector<stage5::Search> searches;

            for (int p = 0; p < kPointCount; ++p)
                for (int k = 0; k < stage5::kSeeds; ++k)
                {
                    stage5::Search s;
                    s.label = label;
                    s.point = p;
                    s.target = target[p];
                    s.impostor = Structure {};
                    s.start = userAt (kPoints[p]);
                    s.seed = stage5::seedFor (k);
                    searches.push_back (s);
                }

            stage5::runSearches (searches);

            for (int p = 0; p < kPointCount; ++p)
            {
                std::vector<double> v;
                std::string runs;
                const stage5::Search* best = nullptr;

                for (int k = 0; k < stage5::kSeeds; ++k)
                {
                    const auto& s = searches[static_cast<size_t> (p * stage5::kSeeds + k)];

                    v.push_back (s.residual);
                    runs += fx (s.residual) + " ";

                    if (best == nullptr || s.residual < best->residual)
                        best = &s;
                }

                std::sort (v.begin(), v.end());

                const double median = v[v.size() / 2];

                const std::string reading = v.front() <= kReproduced ? "REPRODUCED in at least one run"
                                          : median >= kDistinct      ? "distinct"
                                                                     : "WEAK";

                info ("    " + pad (pointName[p], 9) + "median " + pad (fx (median), 8) + "lowest " + pad (fx (v.front()), 8)
                      + "runs " + pad (runs, 34) + pad (reading, 32) + "stops on " + kFamilyNames[best->family]);
                info ("    " + pad ("", 9) + "Hall's knobs in the lowest run: " + best->knobs);
            }
        }
    }
}

void runPhase12Stage8AmbientDiagnosis3()
{
    using namespace stage8;

    const char* const pointName[3] { "medium", "short", "long" };
    constexpr double advance[3] { 35.0, 15.0, 65.0 };      // D*, from DA7

    const Candidate a1b = stage6::familyOf ("Ambient A1b", stage6::ambientRaw (stage6::AmbientSpec {}), true).target;

    // =====================================================================
    beginSuite ("PHASE 12.8.10 - DA11a: A1b against Hall at the same knobs, arrival removed");
    // =====================================================================

    info ("  Target only. Dust zeroed: every sample before the first 5 ms bin above -200 dB is set");
    info ("  to exactly zero. Advanced by D*: moved earlier by A1b's arrival minus Hall's (35 / 15 /");
    info ("  65 ms) - which is Hall with that much Pre-Delay. Every centred family at or above 1.5.");
    info ("");

    Features zeroed[3], advanced[3];

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);
        const Structure s = a1b.make (u);

        const Features hall = featuresOf (Structure {}, u);

        zeroed[p]   = arrivalRemovedFeatures (s, u, 0.0);
        advanced[p] = arrivalRemovedFeatures (s, u, advance[p]);

        const double control = stage5::centredWorst (stage5::sameKnobs (hall, arrivalRemovedFeatures (Structure {}, u, 0.0)));

        info ("    " + std::string (pointName[p]) + "   (control: Hall dust-zeroed against Hall " + fx (control) + ")");

        struct Row { const char* name; Features f; };

        for (const auto& row : { Row { "as it is", featuresOf (s, u) }, Row { "dust zeroed", zeroed[p] },
                                 Row { "zeroed, advanced by D*", advanced[p] } })
        {
            const auto sc = stage5::sameKnobs (hall, row.f);

            int family = 0;
            const double worst = stage5::centredWorst (sc, &family);

            std::string all;

            for (int f = 0; f < kFamilies; ++f)
                if (f != stage5::kLateralFamily && sc.scaled[f] >= 1.5)
                    all += std::string (kFamilyNames[f]) + " " + fx (sc.scaled[f]) + "; ";

            info ("      " + pad (row.name, 26) + pad ("worst " + fx (worst) + " (" + kFamilyNames[family] + ")", 32) + all);
        }

        info ("      fluctuation 50-200 ms: Hall " + fx (hall.fluctuation[0]) + ", A1b zeroed " + fx (zeroed[p].fluctuation[0])
              + ", A1b zeroed and advanced " + fx (advanced[p].fluctuation[0]) + " dB");
        info ("");
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.11 - DA11b: Hall's search against A1b, dust zeroed");
    // =====================================================================

    info ("  The frozen search, five seeds. The gate's reading with A1b's pre-arrival bins exact");
    info ("  zeros, as Hall's are. Diagnostic; not G2. As recorded: 9.77 / 4.00 / 15.33.");
    info ("");

    hallSearch ("A1b, dust zeroed", zeroed);

    // =====================================================================
    beginSuite ("PHASE 12.8.12 - DA11c: Hall with Pre-Delay against A1b");
    // =====================================================================

    info ("  The same search against A1b dust-zeroed AND advanced by D* (35 / 15 / 65 ms). The");
    info ("  product has a Pre-Delay control; the frozen search does not. Label: A1b, arrival");
    info ("  removed. Diagnostic; reclassifies nothing. D* is one value per point, so this is, if");
    info ("  anything, optimistic for A1b.");
    info ("");

    hallSearch ("A1b, arrival removed", advanced);
}

// ===========================================================================
//  Addendum 3: the scan of earlier Ambient structures, and step A
// ===========================================================================
namespace stage8
{
    namespace
    {
        /** Section 2 of addendum 3. Everything is measured from the onset:
            the time at which the running energy first reaches 0.1 % of the
            total. */
        struct Shape
        {
            size_t onsetSample = 0;
            double onset = 0, t10 = 0, t50 = 0, ratio = 0, centroid = 0, early = 0;
            std::vector<double> cumulative;     // one value per millisecond after the onset
        };

        [[nodiscard]] Shape shapeOf (const Stereo& ir)
        {
            const auto e = detail::energyOfBoth (ir);

            double total = 0.0;

            for (double v : e)
                total += v;

            Shape s;

            double running = 0.0;

            for (size_t n = 0; n < e.size(); ++n)
            {
                running += e[n];

                if (running >= 1.0e-3 * total)
                {
                    s.onsetSample = n;
                    break;
                }
            }

            s.onset = 1000.0 * static_cast<double> (s.onsetSample) / kRate;

            double after = 0.0;

            for (size_t n = s.onsetSample; n < e.size(); ++n)
                after += e[n];

            after = std::max (1.0e-300, after);

            const size_t perMs = static_cast<size_t> (kRate / 1000.0);
            const size_t early = static_cast<size_t> (0.050 * kRate);

            double sum = 0.0, moment = 0.0;
            bool got10 = false, got50 = false;

            for (size_t n = s.onsetSample; n < e.size(); ++n)
            {
                const size_t i = n - s.onsetSample;

                sum += e[n];
                moment += e[n] * static_cast<double> (i);

                if (i == early)
                    s.early = sum / after;

                if (! got10 && sum >= 0.10 * after) { s.t10 = 1000.0 * static_cast<double> (i) / kRate; got10 = true; }
                if (! got50 && sum >= 0.50 * after) { s.t50 = 1000.0 * static_cast<double> (i) / kRate; got50 = true; }

                if (i % perMs == perMs - 1)
                    s.cumulative.push_back (sum / after);
            }

            s.centroid = 1000.0 * moment / after / kRate;
            s.ratio = s.t10 / std::max (1.0e-9, s.t50);

            return s;
        }

        [[nodiscard]] double trajectoryDistance (const Shape& a, const Shape& b)
        {
            const size_t n = std::max (a.cumulative.size(), b.cumulative.size());

            double worst = 0.0;

            for (size_t i = 0; i < n; ++i)
                worst = std::max (worst, std::abs ((i < a.cumulative.size() ? a.cumulative[i] : 1.0)
                                                   - (i < b.cumulative.size() ? b.cumulative[i] : 1.0)));

            return worst;
        }

        /** Everything before `own` set to zero, and the response moved so
            that `own` falls on `target`. Length kept. */
        [[nodiscard]] Stereo onsetAligned (Stereo x, size_t own, size_t target)
        {
            for (size_t n = 0; n < std::min (own, x.l.size()); ++n)
                x.l[n] = x.r[n] = 0.0;

            for (auto* channel : { &x.l, &x.r })
            {
                const size_t length = channel->size();

                if (own > target)
                {
                    channel->erase (channel->begin(), channel->begin() + static_cast<std::ptrdiff_t> (std::min (length, own - target)));
                    channel->resize (length, 0.0);
                }
                else if (target > own)
                {
                    channel->insert (channel->begin(), target - own, 0.0);
                    channel->resize (length);
                }
            }

            return x;
        }

        [[nodiscard]] double spearman (const std::vector<double>& a, const std::vector<double>& b)
        {
            const size_t n = a.size();

            if (n < 3)
                return 0.0;

            auto ranks = [n] (const std::vector<double>& v)
            {
                std::vector<size_t> order (n);

                for (size_t i = 0; i < n; ++i)
                    order[i] = i;

                std::sort (order.begin(), order.end(), [&] (size_t x, size_t y) { return v[x] < v[y]; });

                std::vector<double> r (n);

                for (size_t i = 0; i < n; ++i)
                    r[order[i]] = static_cast<double> (i);

                return r;
            };

            const auto ra = ranks (a), rb = ranks (b);
            const double mean = 0.5 * static_cast<double> (n - 1);

            double num = 0.0, da = 0.0, db = 0.0;

            for (size_t i = 0; i < n; ++i)
            {
                num += (ra[i] - mean) * (rb[i] - mean);
                da += (ra[i] - mean) * (ra[i] - mean);
                db += (rb[i] - mean) * (rb[i] - mean);
            }

            return num / std::sqrt (std::max (1.0e-300, da * db));
        }

        struct Entry
        {
            std::string name;
            Maker make;
            std::string recorded;       // Hall's search as the stage reports give it
            bool staged = true;         // an Ambient structure (enters the correlations)
            int shapeOnly = 0;          // 1: Hall with Attack at maximum; 2: Hall, pure shift
        };

        struct Cell
        {
            Shape shape;
            double distance = 0, buildup = 0, profile = 0, echo = 0, late = 0, fluct[2] {};
            double mean125 = 0, sd125 = 0, worstMean = 0, rough = 0, spread = 0, residence = 0;
        };
    }
}

void runPhase12Stage8AmbientScan()
{
    using namespace stage8;
    using stage6::AmbientSpec;

    const char* const pointName[3] { "medium", "short", "long" };

    auto chain = [] (const AmbientSpec& spec)
    {
        return stage6::familyOf ("Ambient " + spec.name, stage6::ambientRaw (spec), true).target.make;
    };

    auto through = [] (double angle, int stages, double lossy, bool generic)
    {
        Structure s;
        s.matrix = MatrixKind::coupledBlocks;
        s.coupling = angle;
        s.injection = InjectionKind::blockA;
        s.readout = ReadoutKind::blockB;
        s.diffuserStages = stages;
        s.blockDecayRatio = lossy;
        s.genericVectors = generic;

        return Maker ([s] (const User&) { return s; });
    };

    std::vector<Entry> entries;

    entries.push_back ({ "S3 Ambient A", through (0.15, 4, 1.0, false), "12.32 / 3.51 / 58.58" });
    entries.push_back ({ "S3 Ambient B", through (0.30, 8, 1.0, false), "19.87 / 3.09 / 22.85" });
    entries.push_back ({ "S3 Ambient C", through (0.15, 4, 0.35, false), "13.99 / 4.81 / 61.45" });
    entries.push_back ({ "S3 Ambient D", through (0.15, 4, 1.0, true), "10.74 / 2.02 / 14.07" });
    entries.push_back ({ "S3 Ambient E", through (0.30, 4, 1.0, true), "9.02 / 1.21 / 18.46" });

    { AmbientSpec s; s.name = "A1a"; s.k = 0.25; entries.push_back ({ "S5 A1a k 0.25", chain (s), "7.77 / 2.90 / 16.73" }); }
    { AmbientSpec s; entries.push_back ({ "S5 A1b (REFERENCE)", chain (s), "9.77 / 4.00 / 15.33" }); }
    { AmbientSpec s; s.name = "A3"; s.k = 0.30; s.stages = { { 0, 4 }, { 1, 5 }, { 2, 3, 6, 7 } };
      entries.push_back ({ "S5 A3 2-2-4 k 0.30", chain (s), "7.76 / 4.12 / 35.93" }); }

    { AmbientSpec s; s.name = "AT2"; s.hear = { 0.25, 0.5, 0.75, 1.0 }; entries.push_back ({ "S6 AT2 all heard", chain (s), "not searched" }); }
    { AmbientSpec s; s.name = "AT3"; s.direct = 0.25; entries.push_back ({ "S6 AT3 25 % direct", chain (s), "4.03 / 1.98 / 5.97" }); }
    { AmbientSpec s; s.name = "AT5a"; s.direct = 0.08; entries.push_back ({ "S6 AT5a 8 % direct", chain (s), "3.97 / 2.06 / 4.63" }); }
    { AmbientSpec s; s.name = "AT6"; s.hear = { 0.0, 0.0, 0.5, 1.0 }; entries.push_back ({ "S6 AT6 last two heard", chain (s), "not searched" }); }
    { AmbientSpec s; s.name = "AT7"; s.stages = { { 0, 4 }, { 1, 5 }, { 2, 3, 6, 7 } }; entries.push_back ({ "S6 AT7 2-2-4 k 0.40", chain (s), "not searched" }); }
    { AmbientSpec s; s.name = "asc"; s.stages = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 } }; entries.push_back ({ "S6 D5 ascending", chain (s), "not searched" }); }
    { AmbientSpec s; s.name = "fold"; s.stages = { { 0, 7 }, { 1, 6 }, { 2, 5 }, { 3, 4 } }; entries.push_back ({ "S6 D5 folded", chain (s), "not searched" }); }

    const Structure sibling = withDelays ("sibling", mode12::hallSibling().delaysMs);

    entries.push_back ({ "control: Hall's sibling", [sibling] (const User&) { return sibling; }, "-", false, 0 });
    entries.push_back ({ "control: Hall, Attack max", [] (const User&) { return Structure {}; }, "-", false, 1 });
    entries.push_back ({ "control: Hall, pure shift", [] (const User&) { return Structure {}; }, "-", false, 2 });

    const size_t referenceIndex = 6;

    std::vector<std::vector<Cell>> cells (entries.size(), std::vector<Cell> (3));
    Shape hallShape[3];

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);
        const double seconds = irSecondsFor (u.decay);

        const auto hallCentre = impulseResponse (Structure {}, u, seconds);
        const Features hallFeatures = featuresOf (Structure {}, u);
        const auto hallThird = thirdOctaveShares (hallCentre);

        hallShape[p] = shapeOf (hallCentre);

        // A1b's onset, for the pure-shift control.
        const size_t a1bOnset = shapeOf (impulseResponse (entries[referenceIndex].make (u), u, seconds)).onsetSample;

        for (size_t k = 0; k < entries.size(); ++k)
        {
            const auto& entry = entries[k];
            auto& cell = cells[k][static_cast<size_t> (p)];

            User own = u;

            if (entry.shapeOnly == 1)
                own.attack = 1.0;

            const Structure s = entry.make (own);

            auto centre = impulseResponse (s, own, seconds);
            auto left   = impulseResponse (s, own, kLeftOnlySeconds, true, false);

            if (entry.shapeOnly == 2)
            {
                const size_t shift = a1bOnset > hallShape[p].onsetSample ? a1bOnset - hallShape[p].onsetSample : 0;

                for (auto* x : { &centre, &left })
                    for (auto* channel : { &x->l, &x->r })
                    {
                        channel->insert (channel->begin(), shift, 0.0);
                        channel->resize (channel->size() - shift);
                    }
            }

            cell.shape = shapeOf (centre);
            cell.distance = trajectoryDistance (cell.shape, hallShape[p]);
            cell.residence = (cell.shape.onset + cell.shape.centroid) / (hallShape[p].onset + hallShape[p].centroid);

            const size_t leftOnset = shapeOf (left).onsetSample;
            const size_t hallLeftOnset = shapeOf (impulseResponse (Structure {}, u, kLeftOnlySeconds, true, false)).onsetSample;

            const Features aligned = analyse (onsetAligned (centre, cell.shape.onsetSample, hallShape[p].onsetSample),
                                              onsetAligned (left, leftOnset, hallLeftOnset));
            const auto sc = stage5::sameKnobs (hallFeatures, aligned);

            cell.buildup = sc.scaled[0];
            cell.profile = sc.scaled[1];
            cell.echo = sc.scaled[2];
            cell.late = sc.scaled[6];
            cell.fluct[0] = aligned.fluctuation[0];
            cell.fluct[1] = aligned.fluctuation[1];

            if (entry.shapeOnly != 0)
                continue;

            const auto m = perturbedMoments (entry.make, u);

            cell.mean125 = m.mean[0];
            cell.sd125 = m.sd[0];

            for (double v : m.mean)
                if (std::abs (v) > std::abs (cell.worstMean))
                    cell.worstMean = v;

            cell.rough = roughness (thirdOctaveShares (centre), hallThird);
            cell.spread = spreadOf (sourceGains (s, u));
        }
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.13 - the scan: buildup shape, measured from the onset");
    // =====================================================================

    info ("  Onset: the running energy first reaches 0.1 % of the total. Everything after is");
    info ("  measured from it. t10 / t50 has no time scale: 0.152 for an instant rise and an");
    info ("  exponential decay whatever the decay time; more for a staged build. 'traj' is the");
    info ("  largest gap between the cumulative-energy curve and Hall's. The four family columns");
    info ("  are the frozen metric against Hall at the same knobs with the onsets aligned.");
    info ("  'recorded' is Hall's seven-control search as the stage reports give it (medium / small");
    info ("  / large); it was not re-run.");

    for (int p = 0; p < 3; ++p)
    {
        info ("");
        info ("    " + std::string (pointName[p]) + "   Hall: onset " + fx (hallShape[p].onset, 0) + " ms, t10 " + fx (hallShape[p].t10, 0)
              + ", t50 " + fx (hallShape[p].t50, 0) + ", t10/t50 " + fx (hallShape[p].ratio, 3) + ", centroid "
              + fx (hallShape[p].centroid, 0) + ", early " + fx (hallShape[p].early, 2));
        info ("    " + pad ("structure", 28) + pad ("onset", 7) + pad ("t10", 6) + pad ("t50", 6) + pad ("t10/t50", 9) + pad ("centr", 7)
              + pad ("early", 7) + pad ("traj", 7) + pad ("buildup", 9) + pad ("profile", 9) + pad ("echo", 7) + pad ("late", 7)
              + pad ("fl 50-200", 11) + pad ("fl 200-500", 12) + "recorded");

        for (size_t k = 0; k < entries.size(); ++k)
        {
            const auto& c = cells[k][static_cast<size_t> (p)];

            info ("    " + pad (entries[k].name, 28) + pad (fx (c.shape.onset, 0), 7) + pad (fx (c.shape.t10, 0), 6)
                  + pad (fx (c.shape.t50, 0), 6) + pad (fx (c.shape.ratio, 3), 9) + pad (fx (c.shape.centroid, 0), 7)
                  + pad (fx (c.shape.early, 2), 7) + pad (fx (c.distance, 2), 7) + pad (fx (c.buildup), 9) + pad (fx (c.profile), 9)
                  + pad (fx (c.echo), 7) + pad (fx (c.late), 7) + pad (fx (c.fluct[0]), 11) + pad (fx (c.fluct[1]), 12)
                  + entries[k].recorded);
        }
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.14 - the scan: tone, variance, level, residence");
    // =====================================================================

    info ("  125 Hz mean and standard deviation over twelve delay perturbations; the worst band's");
    info ("  mean; third-octave roughness; the spread over vocal, snare and percussion; and the");
    info ("  whole response's mean time against Hall's.");

    for (int p = 0; p < 3; ++p)
    {
        info ("");
        info ("    " + pad (pointName[p], 28) + pad ("125 mean", 10) + pad ("125 SD", 9) + pad ("worst mean", 12) + pad ("rough", 8)
              + pad ("spread", 8) + "residence");

        for (size_t k = 0; k < entries.size(); ++k)
        {
            if (entries[k].shapeOnly != 0)
                continue;

            const auto& c = cells[k][static_cast<size_t> (p)];

            info ("    " + pad (entries[k].name, 28) + pad (signedText (c.mean125), 10) + pad (fx (c.sd125), 9)
                  + pad (signedText (c.worstMean), 12) + pad (fx (c.rough), 8) + pad (fx (c.spread), 8) + fx (c.residence));
        }
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.15 - the scan: relationships");
    // =====================================================================

    info ("  Spearman rank correlation across the fifteen Ambient structures, each point apart.");
    info ("  Descriptive: fifteen is few, and a relationship is not a cause.");
    info ("");

    {
        const char* const names[8] { "t10/t50", "traj", "residence", "125 mean", "125 SD", "rough", "spread", "buildup" };

        for (int p = 0; p < 3; ++p)
        {
            std::vector<std::vector<double>> v (8);

            for (size_t k = 0; k < entries.size(); ++k)
            {
                if (! entries[k].staged)
                    continue;

                const auto& c = cells[k][static_cast<size_t> (p)];

                v[0].push_back (c.shape.ratio);
                v[1].push_back (c.distance);
                v[2].push_back (c.residence);
                v[3].push_back (c.mean125);
                v[4].push_back (c.sd125);
                v[5].push_back (c.rough);
                v[6].push_back (c.spread);
                v[7].push_back (c.buildup);
            }

            std::string head = "    " + pad (pointName[p], 12);

            for (const char* n : names)
                head += pad (n, 11);

            info (head);

            for (int a = 0; a < 8; ++a)
            {
                std::string row = "    " + pad (names[a], 12);

                for (int b = 0; b < 8; ++b)
                    row += pad (b < a ? std::string ("") : signedText (spearman (v[static_cast<size_t> (a)], v[static_cast<size_t> (b)])), 11);

                info (row);
            }

            info ("");
        }
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.16 - Hall's reach: a baseline for thresholds");
    // =====================================================================

    info ("  300 random settings of Hall's controls (Size 0-100, Decay 0.3-8 s, Early/Late,");
    info ("  Early Diffusion, Attack, modulation; fixed seed). Pre-Delay is irrelevant: everything");
    info ("  is measured from the onset. Then A1b at the nine grid settings. No threshold is set");
    info ("  here.");
    info ("");

    {
        constexpr size_t count = 300;

        std::vector<Shape> reach (count);
        std::vector<User> users (count);

        unsigned int state = 0x5EED1234u;

        auto uniform = [&state]
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<double> (state >> 8) / static_cast<double> (1u << 24);
        };

        for (auto& u : users)
        {
            u.size = uniform();
            u.decay = 0.3 * std::pow (8.0 / 0.3, uniform());
            u.earlyLate = uniform();
            u.earlyDiff = uniform();
            u.attack = uniform();
            u.modDepth = uniform();
        }

        runParallel (count, [&] (size_t i)
        {
            reach[i] = shapeOf (impulseResponse (Structure {}, users[i], irSecondsFor (users[i].decay)));
        });

        auto summarise = [&] (const std::string& name, const std::function<double (const Shape&)>& of)
        {
            std::vector<double> v;
            size_t top = 0;

            for (size_t i = 0; i < count; ++i)
            {
                v.push_back (of (reach[i]));

                if (v[i] > v[top])
                    top = i;
            }

            auto sorted = v;
            std::sort (sorted.begin(), sorted.end());

            info ("    " + pad (name, 16) + "min " + pad (fx (sorted.front(), 3), 8) + "5 % " + pad (fx (sorted[count / 20], 3), 8)
                  + "median " + pad (fx (sorted[count / 2], 3), 8) + "95 % " + pad (fx (sorted[count - count / 20], 3), 8)
                  + "max " + pad (fx (sorted.back(), 3), 8) + "(max at Size " + fx (100.0 * users[top].size, 0) + ", Decay "
                  + fx (users[top].decay) + ", E/L " + fx (100.0 * users[top].earlyLate, 0) + ", EDiff "
                  + fx (100.0 * users[top].earlyDiff, 0) + ", Atk " + fx (100.0 * users[top].attack, 0) + ")");
        };

        info ("    Hall, 300 settings:");
        summarise ("t10 / t50",      [] (const Shape& s) { return s.ratio; });
        summarise ("centroid / t50", [] (const Shape& s) { return s.centroid / std::max (1.0e-9, s.t50); });
        summarise ("t10, ms",        [] (const Shape& s) { return s.t10; });
        summarise ("early fraction", [] (const Shape& s) { return s.early; });

        info ("");
        info ("    A1b and Hall at the same knobs, Size 25 / 60 / 85 (rows) x Decay 0.8 / 2.5 / 5.0:");
        info ("    " + pad ("", 22) + pad ("t10/t50  A1b / Hall", 26) + pad ("t10 ms  A1b / Hall", 26) + "early  A1b / Hall");

        constexpr double sizes[3] { 0.25, 0.60, 0.85 }, decays[3] { 0.8, 2.5, 5.0 };

        const Maker a1b = entries[referenceIndex].make;

        for (double size : sizes)
            for (double decay : decays)
            {
                User u;
                u.size = size;
                u.decay = decay;

                const double seconds = irSecondsFor (u.decay);
                const Shape a = shapeOf (impulseResponse (a1b (u), u, seconds));
                const Shape h = shapeOf (impulseResponse (Structure {}, u, seconds));

                info ("    " + pad ("Size " + fx (100.0 * size, 0) + ", Decay " + fx (decay, 1), 22)
                      + pad (fx (a.ratio, 3) + " / " + fx (h.ratio, 3), 26) + pad (fx (a.t10, 0) + " / " + fx (h.t10, 0), 26)
                      + fx (a.early, 3) + " / " + fx (h.early, 3));
            }
    }
}

// ===========================================================================
//  Step A: root cause, stage by stage
// ===========================================================================
void runPhase12Stage8AmbientRootCause()
{
    using namespace stage8;

    const char* const pointName[3] { "medium", "short", "long" };

    const Maker a1b = stage6::familyOf ("Ambient A1b", stage6::ambientRaw (stage6::AmbientSpec {}), true).target.make;

    struct Held
    {
        double stage[4][7] {};      // energy held, by stage and octave
        double moment[4][7] {};     // its first moment, samples
        double out[7] {};           // the output's energy by octave
    };

    /** One response with the tank's state read line by line. `pairs` lines
        per stage: 2 for A1b; Hall is read as four pairs and summed later. */
    auto hold = [] (const Structure& s, const User& u)
    {
        const auto length = static_cast<size_t> (irSecondsFor (u.decay) * kRate);

        auto rig = std::make_unique<Rig> (s, u);

        std::vector<Stereo> lines (4);
        Stereo out;

        for (auto& l : lines)
        {
            l.l.assign (length, 0.0);
            l.r.assign (length, 0.0);
        }

        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        {
            ScopedFtz ftz;

            for (size_t n = 0; n < length; ++n)
            {
                double l = n == 0 ? 1.0 : 0.0, r = l;
                rig->processSample (l, r);

                out.l[n] = l;
                out.r[n] = r;

                const double* state = rig->getLastState();

                for (size_t k = 0; k < 4; ++k)
                {
                    lines[k].l[n] = state[2 * k];
                    lines[k].r[n] = state[2 * k + 1];
                }
            }
        }

        Held h;

        for (size_t b = 0; b < 7; ++b)
        {
            for (size_t k = 0; k < 4; ++k)
            {
                const auto e = detail::bandEnergy (lines[k], kBands[b], length);

                for (size_t n = 0; n < e.size(); ++n)
                {
                    h.stage[k][b] += e[n];
                    h.moment[k][b] += e[n] * static_cast<double> (n);
                }
            }

            for (double v : detail::bandEnergy (out, kBands[b], length))
                h.out[b] += v;
        }

        return h;
    };

    auto balance = [] (const double* band)
    {
        Bands share {};
        double total = 0.0;

        for (size_t b = 0; b < 7; ++b)
            total += band[b];

        for (size_t b = 0; b < 7; ++b)
            share[b] = band[b] / std::max (1.0e-300, total);

        return share;
    };

    // =====================================================================
    beginSuite ("PHASE 12.8.17 - step A: the coupling law as built (RC7)");
    // =====================================================================

    info ("  A1b: four stages of two lines; neighbouring stages exchange by a rotation with");
    info ("  sin^2(theta) = stage loop time / tau, tau = 0.40 x RT60 / 3.");
    info ("");

    for (int p = 0; p < 3; ++p)
    {
        const User u = userAt (kPoints[p]);

        auto base = Structure {}.delaysMs;
        std::sort (base.begin(), base.end());

        const double scale = mapping::sizeScaleFor (u.size);
        const double tau = 0.40 * mapping::rt60For (u.decay) / 3.0;

        std::string row = "    " + pad (pointName[p], 9) + "tau " + pad (fx (1000.0 * tau, 0) + " ms", 10) + "decayScale "
                        + pad (fx (a1b (u).decayScale, 3), 8);

        for (size_t s = 0; s < 4; ++s)
        {
            const double loop = 0.5 * (base[s] + base[s + 4]) * scale;

            row += "stage " + std::to_string (s + 1) + ": " + fx (loop, 1) + " ms"
                 + (s < 3 ? ", sin2 " + fx (std::min (0.5, 0.001 * loop / tau), 3) + "   " : "");
        }

        info (row);
    }

    // =====================================================================
    beginSuite ("PHASE 12.8.18 - step A: where the fingerprint and the variance are created (RC1-RC5, RC8)");
    // =====================================================================

    info ("  The tank's state read line by line over the whole response, twelve delay");
    info ("  perturbations. Each stage's octave balance is compared with Hall's (all eight lines,");
    info ("  unperturbed, read the same way). MEAN is systematic; SD is chance.");

    for (int p = 0; p < 2; ++p)
        for (int damping = 0; damping < 2; ++damping)
        {
            User u = userAt (kPoints[p]);

            if (damping == 1)
            {
                u.lowDamp = 0.0;
                u.highDamp = 0.0;
            }

            const Held hall = hold (Structure {}, u);

            double hallAll[7] {};

            for (size_t b = 0; b < 7; ++b)
                for (size_t k = 0; k < 4; ++k)
                    hallAll[b] += hall.stage[k][b];

            const Bands hallBalance = balance (hallAll), hallOut = balance (hall.out);

            constexpr size_t seeds = 12;

            std::vector<Held> held (seeds);

            runParallel (seeds, [&] (size_t k)
            {
                held[k] = hold (perturbed (a1b (u), static_cast<unsigned int> (k + 1)), u);
            });

            std::vector<Bands> stageDb[4], outDb (seeds), joinDb[3];
            double share[4][7] {}, centroid[4][7] {};

            for (auto& v : stageDb) v.resize (seeds);
            for (auto& v : joinDb)  v.resize (seeds);

            for (size_t k = 0; k < seeds; ++k)
            {
                for (size_t s = 0; s < 4; ++s)
                {
                    stageDb[s][k] = dbBetween (balance (held[k].stage[s]), hallBalance);

                    for (size_t b = 0; b < 7; ++b)
                    {
                        double total = 0.0;

                        for (size_t t = 0; t < 4; ++t)
                            total += held[k].stage[t][b];

                        share[s][b] += 100.0 * held[k].stage[s][b] / total / seeds;
                        centroid[s][b] += 1000.0 * held[k].moment[s][b] / held[k].stage[s][b] / kRate / seeds;
                    }
                }

                for (size_t s = 0; s < 3; ++s)
                    for (size_t b = 0; b < 7; ++b)
                        joinDb[s][k][b] = 10.0 * std::log10 (held[k].stage[s + 1][b] / held[k].stage[s][b]);

                outDb[k] = dbBetween (balance (held[k].out), hallOut);
            }

            info ("");
            info ("    " + std::string (pointName[p]) + (damping == 1 ? ", DAMPING OFF" : ", damping as the defaults"));
            info ("    " + pad ("", 34) + kBandHead);

            const auto outM = momentsOf (outDb);

            Moments stageM[4];

            for (size_t s = 0; s < 4; ++s)
                stageM[s] = momentsOf (stageDb[s]);

            for (size_t s = 0; s < 4; ++s)
                info ("    " + pad ("RC2 stage " + std::to_string (s + 1) + " balance, MEAN", 34) + bandRow (stageM[s].mean));

            info ("    " + pad ("    the output, MEAN", 34) + bandRow (outM.mean));

            for (size_t s = 0; s < 4; ++s)
                info ("    " + pad ("RC3 stage " + std::to_string (s + 1) + " balance, SD", 34) + bandRow (stageM[s].sd));

            info ("    " + pad ("    the output, SD", 34) + bandRow (outM.sd));

            {
                Bands diff {};

                for (size_t b = 0; b < 7; ++b)
                    diff[b] = outM.mean[b] - stageM[3].mean[b];

                info ("    " + pad ("RC8 output minus stage 4, MEAN", 34) + bandRow (diff));
            }

            for (size_t s = 0; s < 3; ++s)
            {
                const auto m = momentsOf (joinDb[s]);

                Bands relative {};

                for (size_t b = 0; b < 7; ++b)
                    relative[b] = m.mean[b] - (m.mean[3] + m.mean[4] + m.mean[5]) / 3.0;

                info ("    " + pad ("RC4 held in stage " + std::to_string (s + 2) + " / stage " + std::to_string (s + 1) + ", dB", 34)
                      + bandRow (m.mean) + "  against 1-4k: " + signedText (relative[0]) + " at 125, " + signedText (relative[1]) + " at 250");
            }

            for (size_t s = 0; s < 4; ++s)
            {
                std::string row = "    " + pad ("RC1 stage " + std::to_string (s + 1) + " share of the band, %", 34);

                for (size_t b = 0; b < 7; ++b)
                    row += pad (fx (share[s][b], 1), 8);

                info (row);
            }

            for (size_t s = 0; s < 4; ++s)
            {
                std::string row = "    " + pad ("RC5 stage " + std::to_string (s + 1) + " mean time, ms", 34);

                for (size_t b = 0; b < 7; ++b)
                    row += pad (fx (centroid[s][b], 0), 8);

                info (row);
            }

            // The registered reading: the first stage at which the 125 Hz mean
            // (and SD) reaches half of the output's.
            int meanAt = 0, sdAt = 0;

            for (int s = 3; s >= 0; --s)
            {
                if (std::abs (stageM[static_cast<size_t> (s)].mean[0]) >= 0.5 * std::abs (outM.mean[0]))
                    meanAt = s + 1;

                if (stageM[static_cast<size_t> (s)].sd[0] >= 0.5 * outM.sd[0])
                    sdAt = s + 1;
            }

            info ("    READING: at 125 Hz the systematic deviation reaches half of the output's at stage "
                  + (meanAt > 0 ? std::to_string (meanAt) : std::string ("- (none)")) + "; the variance at stage "
                  + (sdAt > 0 ? std::to_string (sdAt) : std::string ("- (none)")) + ".");
        }

    // =====================================================================
    beginSuite ("PHASE 12.8.19 - step A: conservation (RC6)");
    // =====================================================================

    info ("  Damping off, broadband. The decay rate of the total energy held in the tank, dB per");
    info ("  second, fitted from 0.3 x Decay to 1.0 x Decay, against the rate the control asks");
    info ("  for (60 / RT60) and against Hall measured the same way.");
    info ("");

    for (int p = 0; p < 3; ++p)
    {
        User u = userAt (kPoints[p]);
        u.lowDamp = 0.0;
        u.highDamp = 0.0;

        auto slopeOf = [&] (const Structure& s)
        {
            const auto length = static_cast<size_t> (irSecondsFor (u.decay) * kRate);
            const size_t bin = static_cast<size_t> (0.050 * kRate);

            auto rig = std::make_unique<Rig> (s, u);

            std::vector<double> e (length / bin, 0.0);

            {
                ScopedFtz ftz;

                for (size_t n = 0; n < e.size() * bin; ++n)
                {
                    double l = n == 0 ? 1.0 : 0.0, r = l;
                    rig->processSample (l, r);

                    const double* state = rig->getLastState();

                    for (int i = 0; i < 8; ++i)
                        e[n / bin] += state[i] * state[i];
                }
            }

            const auto from = static_cast<size_t> (0.3 * u.decay / 0.050), to = std::min (e.size(), static_cast<size_t> (1.0 * u.decay / 0.050));

            double sx = 0, sy = 0, sxx = 0, sxy = 0, count = 0;

            for (size_t b = from; b < to; ++b)
            {
                const double x = 0.050 * static_cast<double> (b), y = 10.0 * std::log10 (std::max (1.0e-300, e[b]));

                sx += x; sy += y; sxx += x * x; sxy += x * y; count += 1.0;
            }

            return (count * sxy - sx * sy) / std::max (1.0e-300, count * sxx - sx * sx);
        };

        const double asked = -60.0 / mapping::rt60For (u.decay);

        info ("    " + pad (pointName[p], 9) + "asked " + pad (fx (asked, 2), 9) + "Hall " + pad (fx (slopeOf (Structure {}), 2), 9)
              + "A1b " + pad (fx (slopeOf (a1b (u)), 2), 9) + "(A1b's tank is asked for RT60 x " + fx (a1b (u).decayScale, 3) + ")");
    }
}

} // namespace avtest
