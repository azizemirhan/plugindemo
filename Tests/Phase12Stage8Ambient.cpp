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

} // namespace avtest
