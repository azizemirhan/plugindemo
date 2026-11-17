#include "Phase12Stage5.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DspAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

/*
    PHASE 12 - STAGE 5. The five-Mode structural completion programme:
    shared machinery, and the order the parts run in.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    The metric and the search are Stage 3's, frozen at c18ecce. The gates are
    in Docs/Phase12-Stage5-Preregistration.md, committed before this file
    measured anything.

    AV_S5_PART=A|B|C|D runs one part (C needs B's and A's survivors and
    re-derives them from their fixed definitions).
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace stage5
{
    // =======================================================================
    //  Blocks
    // =======================================================================
    Candidate fixed (const std::string& name, Structure s)
    {
        s.name = name;
        return { name, [s] (const User&) { return s; } };
    }

    namespace
    {
        void fillHadamard (double* m, int stride, int n)
        {
            // Sylvester construction, normalised.
            m[0] = 1.0;

            for (int size = 1; size < n; size *= 2)
                for (int r = 0; r < size; ++r)
                    for (int c = 0; c < size; ++c)
                    {
                        const double v = m[r * stride + c];

                        m[r * stride + c + size]            =  v;
                        m[(r + size) * stride + c]          =  v;
                        m[(r + size) * stride + c + size]   = -v;
                    }

            const double norm = 1.0 / std::sqrt (static_cast<double> (n));

            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c)
                    m[r * stride + c] *= norm;
        }

        // Equal-magnitude sign vectors, left and right orthogonal, none of
        // them a row of a Hadamard block (an odd number of minus signs in
        // every group of four) - the alignment that spoiled Stage 3's
        // Ambient A-C.
        void signs (int n, bool readout, double* l, double* r)
        {
            constexpr double in2L[] { 1, 1 },             in2R[] { 1, -1 };
            constexpr double in4L[] { 1, 1, 1, -1 },      in4R[] { 1, 1, -1, 1 };
            constexpr double in6L[] { 1, 1, 1, 1, 1, -1 }, in6R[] { 1, -1, 1, -1, 1, 1 };

            constexpr double out2L[] { 1, 1 },             out2R[] { -1, 1 };
            constexpr double out4L[] { 1, -1, 1, 1 },      out4R[] { -1, 1, 1, 1 };
            constexpr double out6L[] { 1, 1, -1, 1, 1, 1 }, out6R[] { 1, -1, 1, 1, -1, 1 };

            if (n == 8)
            {
                signs (4, readout, l, r);
                signs (4, readout, l + 4, r + 4);

                for (int i = 4; i < 8; ++i)
                    r[i] = -r[i];

                return;
            }

            const double* sl = n == 2 ? (readout ? out2L : in2L)
                             : n == 4 ? (readout ? out4L : in4L)
                                      : (readout ? out6L : in6L);
            const double* sr = n == 2 ? (readout ? out2R : in2R)
                             : n == 4 ? (readout ? out4R : in4R)
                                      : (readout ? out6R : in6R);

            for (int i = 0; i < n; ++i)
            {
                l[i] = sl[i];
                r[i] = sr[i];
            }
        }
    }

    Structure build (const std::string& name, const Blocks& b)
    {
        Structure s;
        s.name = name;

        auto base = Structure {}.delaysMs;
        std::sort (base.begin(), base.end());

        const size_t count = b.blocks.size();

        std::vector<int> first (count, 0), size (count, 0);

        double d[N * N] {};
        int p = 0;

        for (size_t k = 0; k < count; ++k)
        {
            const auto& block = b.blocks[k];
            const int n = static_cast<int> (block.lines.size());

            first[k] = p;
            size[k] = n;

            for (int i = 0; i < n; ++i)
            {
                s.delaysMs[static_cast<size_t> (p + i)] = base[static_cast<size_t> (block.lines[static_cast<size_t> (i)])]
                                                        * block.delayScale;
                s.lineDecayRatio[static_cast<size_t> (p + i)] = block.decayRatio;
            }

            double* corner = d + p * N + p;

            switch (block.mix)
            {
                case Mix::hadamard:
                    fillHadamard (corner, N, n);
                    break;

                case Mix::householder:
                    for (int r = 0; r < n; ++r)
                        for (int c = 0; c < n; ++c)
                            corner[r * N + c] = (r == c ? 1.0 : 0.0) - 2.0 / n;
                    break;

                case Mix::identity:
                    for (int r = 0; r < n; ++r)
                        corner[r * N + r] = 1.0;
                    break;

                case Mix::ring:
                    for (int r = 0; r < n; ++r)
                        corner[r * N + (r + 1) % n] = 1.0;
                    break;

                case Mix::rotation:
                {
                    // Two lines, 0.9 rad: mixes, and is aligned with no sign vector.
                    const double c = std::cos (0.9), sn = std::sin (0.9);

                    corner[0] = c;      corner[1] = sn;
                    corner[N] = -sn;    corner[N + 1] = c;
                    break;
                }
            }

            p += n;
        }

        // Join adjacent blocks: rotate line i of one against line i of the next.
        for (size_t k = 0; k + 1 < count; ++k)
        {
            const double c = std::cos (b.theta[k]), sn = std::sin (b.theta[k]);
            const int pairs = std::min (size[k], size[k + 1]);

            for (int i = 0; i < pairs; ++i)
            {
                const int ra = first[k] + i, rb = first[k + 1] + i;

                for (int col = 0; col < N; ++col)
                {
                    const double ma = d[ra * N + col], mb = d[rb * N + col];

                    d[ra * N + col] =  c * ma + sn * mb;
                    d[rb * N + col] = -sn * ma + c * mb;
                }
            }
        }

        s.matrix = MatrixKind::custom;

        for (int i = 0; i < N * N; ++i)
            s.customMatrix[static_cast<size_t> (i)] = d[i];

        auto vectors = [&] (const std::vector<int>& which, bool readout, std::array<double, 8>& l,
                            std::array<double, 8>& r)
        {
            l.fill (0.0);
            r.fill (0.0);

            double norm = 0.0;

            for (int k : which)
            {
                signs (size[static_cast<size_t> (k)], readout, l.data() + first[static_cast<size_t> (k)],
                       r.data() + first[static_cast<size_t> (k)]);
                norm += size[static_cast<size_t> (k)];
            }

            norm = 1.0 / std::sqrt (norm);

            for (auto& v : l) v *= norm;
            for (auto& v : r) v *= norm;
        };

        vectors (b.in,  false, s.customInL,  s.customInR);
        vectors (b.out, true,  s.customOutL, s.customOutR);

        s.injection = InjectionKind::custom;
        s.readout   = ReadoutKind::custom;

        return s;
    }

    double orthogonalityError (const Structure& s)
    {
        double m[N * N];
        effectiveMatrix (s, m);

        double worst = 0.0;

        for (int a = 0; a < N; ++a)
            for (int b = 0; b < N; ++b)
            {
                double dot = 0.0;

                for (int c = 0; c < N; ++c)
                    dot += m[a * N + c] * m[b * N + c];

                worst = std::max (worst, std::abs (dot - (a == b ? 1.0 : 0.0)));
            }

        return worst;
    }

    // =======================================================================
    //  Scoring
    // =======================================================================
    double centredWorst (const Score& s, int* family)
    {
        double worst = 0.0;
        int which = 0;

        for (int k = 0; k < kFamilies; ++k)
            if (k != kLateralFamily && s.scaled[k] > worst)
            {
                worst = s.scaled[k];
                which = k;
            }

        if (family != nullptr)
            *family = which;

        return worst;
    }

    Score sameKnobs (const Features& a, const Features& b)
    {
        return score (distance (a, b), floorOf());
    }

    unsigned int seedFor (int k)
    {
        // None of these is a seed Stage 3 or Stage 4 used.
        return 0x9E3779B9u ^ static_cast<unsigned int> ((static_cast<unsigned int> (k) + 201u) * 2654435761u);
    }

    void runSearches (std::vector<Search>& searches)
    {
        runParallel (searches.size(), [&] (size_t i)
        {
            auto& s = searches[i];

            const auto found = mimic (s.target, s.impostor, floorOf(), toControls (s.start),
                                      s.seed, kLateralFamily);

            s.residual = found.best.worst;
            s.family   = found.best.worstFamily;
            s.knobs    = describe (found.controls);
        });
    }

    std::vector<Gate> gate (const std::vector<Candidate>& targets, const Candidate& impostor, int seeds)
    {
        const size_t count = targets.size();

        std::vector<Features> features (count * kPointCount);

        runParallel (features.size(), [&] (size_t i)
        {
            const User u = userAt (kPoints[i % kPointCount]);
            features[i] = featuresOf (targets[i / kPointCount].make (u), u);
        });

        const Structure with = impostor.make (User {});

        std::vector<Search> searches;

        for (size_t t = 0; t < count; ++t)
            for (int p = 0; p < kPointCount; ++p)
                for (int k = 0; k < seeds; ++k)
                {
                    Search s;
                    s.label = targets[t].name;
                    s.point = p;
                    s.target = features[t * kPointCount + static_cast<size_t> (p)];
                    s.impostor = with;
                    s.start = userAt (kPoints[p]);
                    s.seed = seedFor (k);
                    searches.push_back (s);
                }

        runSearches (searches);

        std::vector<Gate> gates (count);

        for (size_t t = 0; t < count; ++t)
        {
            auto& g = gates[t];

            g.seeds = seeds;
            g.minimum = 1.0e30;

            bool reproduced = false, survives = true, notRefuted = true;

            for (int p = 0; p < kPointCount; ++p)
            {
                std::vector<double> v;
                double lowest = 1.0e30;

                for (int k = 0; k < seeds; ++k)
                {
                    const auto& found = searches[(t * kPointCount + static_cast<size_t> (p))
                                                 * static_cast<size_t> (seeds) + static_cast<size_t> (k)];
                    const double r = found.residual;

                    if (r < lowest)
                    {
                        lowest = r;
                        g.family[p] = found.family;
                        g.knobs[p] = found.knobs;
                    }

                    g.residual[p][k] = r;
                    g.minimum = std::min (g.minimum, r);
                    v.push_back (r);

                    if (r <= kReproduced)
                        reproduced = true;
                }

                std::sort (v.begin(), v.end());

                g.median[p] = v.size() % 2 == 1 ? v[v.size() / 2]
                                                : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);

                if (g.median[p] < kDistinct) survives = false;
                if (g.median[p] < 2.5)       notRefuted = false;
            }

            if (seeds == 1)
                g.verdict = reproduced ? "REJECTED at G1" : "passes G1";
            else
                g.verdict = reproduced ? "REJECTED"
                          : survives   ? "SURVIVES"
                          : notRefuted ? "NOT REFUTED"
                                       : "REJECTED";
        }

        return gates;
    }

    void printGate (const std::vector<Candidate>& targets, const std::vector<Gate>& gates)
    {
        info ("    " + pad ("target", 30) + pad ("medium", 9) + pad ("small", 9) + pad ("large", 9)
              + pad ("lowest run", 12) + "verdict");

        for (size_t t = 0; t < targets.size(); ++t)
        {
            const auto& g = gates[t];

            info ("    " + pad (targets[t].name, 30) + pad (fx (g.median[0]), 9) + pad (fx (g.median[1]), 9)
                  + pad (fx (g.median[2]), 9) + pad (fx (g.minimum), 12) + g.verdict);

            if (g.seeds > 1)
                for (int p = 0; p < kPointCount; ++p)
                {
                    std::string row = "        " + pad (kPoints[p].label, 18);

                    for (int k = 0; k < g.seeds; ++k)
                        row += pad (fx (g.residual[p][k]), 7);

                    info (row + "  " + pad (kFamilyNames[g.family[p]], 18) + g.knobs[p]);
                }
        }
    }

    // =======================================================================
    //  Level and decay
    // =======================================================================
    Candidate levelled (const Candidate& c)
    {
        auto correction = std::make_shared<double> (0.0);
        auto guard = std::make_shared<std::mutex>();
        const Maker make = c.make;

        return { c.name, [make, correction, guard] (const User& u)
        {
            {
                std::lock_guard<std::mutex> lock (*guard);

                if (*correction == 0.0)
                    *correction = calibrate (make (User {})).lateCorrection;
            }

            Structure s = make (u);
            s.lateCorrection = *correction;
            return s;
        } };
    }

    Candidate decayCalibrated (const Candidate& c)
    {
        auto table = std::make_shared<std::map<std::pair<double, double>, double>>();
        auto guard = std::make_shared<std::mutex>();
        const Maker make = c.make;

        return { c.name, [make, table, guard] (const User& u)
        {
            const auto key = std::make_pair (u.size, u.decay);

            {
                std::lock_guard<std::mutex> lock (*guard);
                const auto found = table->find (key);

                if (found != table->end())
                {
                    Structure s = make (u);
                    s.decayScale = found->second;
                    return s;
                }
            }

            // A law of Size and Decay only: fitted at the other controls' defaults.
            User plain;
            plain.size = u.size;
            plain.decay = u.decay;

            const double seconds = irSecondsFor (u.decay) + 1.0;
            const double reference = mode12::rt60FromEnergy (detail::energyOfBoth (
                impulseResponse (Structure {}, plain, seconds)));

            Structure s = make (plain);

            for (int pass = 0; pass < 3; ++pass)
            {
                const double own = mode12::rt60FromEnergy (detail::energyOfBoth (
                    impulseResponse (s, plain, seconds)));

                if (own > 0.0 && reference > 0.0)
                    s.decayScale *= reference / own;
            }

            {
                std::lock_guard<std::mutex> lock (*guard);
                (*table)[key] = s.decayScale;
            }

            Structure out = make (u);
            out.decayScale = s.decayScale;
            return out;
        } };
    }

    // =======================================================================
    //  G3
    // =======================================================================
    namespace
    {
        volatile double gSink = 0.0;

        [[nodiscard]] double foldDb (const Stereo& x, size_t from)
        {
            double total = 0.0, mono = 0.0;

            for (size_t n = from; n < x.l.size(); ++n)
            {
                total += x.l[n] * x.l[n] + x.r[n] * x.r[n];

                const double m = 0.5 * (x.l[n] + x.r[n]);
                mono += 2.0 * m * m;
            }

            return total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-30, mono / total)) : 0.0;
        }

        struct StereoRow
        {
            double corr = 0, fold = 0, balance = 0, mirror = 0, hard = 0, toneWorst = 0, toneMedian = 0;
            double width[3] {};
        };

        [[nodiscard]] StereoRow stereoOf (const Structure& s)
        {
            const User u;
            StereoRow row;

            const auto centre = impulseResponse (s, u, 3.0);
            const auto left   = impulseResponse (s, u, 3.0, true, false);
            const auto right  = impulseResponse (s, u, 3.0, false, true);

            const auto f = analyse (centre, left);

            row.corr = f.base.corr[2];
            row.fold = foldDb (centre, 0);
            row.hard = f.placementHard;

            double ll = 0.0, rr = 0.0;

            for (size_t n = 0; n < centre.l.size(); ++n)
            {
                ll += centre.l[n] * centre.l[n];
                rr += centre.r[n] * centre.r[n];
            }

            row.balance = 10.0 * std::log10 (ll / rr);
            row.mirror  = 10.0 * std::log10 (energyOf (left) / energyOf (right));

            std::vector<double> folds;

            for (int t = 0; t < 24; ++t)
            {
                const double hz = 180.0 * std::pow (2200.0 / 180.0, t / 23.0);
                const auto length = static_cast<size_t> (1.6 * kRate);

                std::vector<double> drive (length);

                for (size_t n = 0; n < length; ++n)
                    drive[n] = 0.25 * std::min (1.0, n / (0.02 * kRate))
                             * std::sin (2.0 * detail::kPi * hz * static_cast<double> (n) / kRate);

                folds.push_back (foldDb (std::make_unique<Rig> (s, u)->render (drive, drive, length), length / 2));
            }

            std::sort (folds.begin(), folds.end());

            row.toneWorst  = folds.front();
            row.toneMedian = folds[folds.size() / 2];

            constexpr double widths[3] { 0.5, 1.0, 1.5 };

            for (int w = 0; w < 3; ++w)
            {
                User wide;
                wide.width = widths[w];

                const auto ir = impulseResponse (s, wide, 2.0);

                double mid = 0.0, side = 0.0;

                for (size_t n = 0; n < ir.l.size(); ++n)
                {
                    mid  += (ir.l[n] + ir.r[n]) * (ir.l[n] + ir.r[n]);
                    side += (ir.l[n] - ir.r[n]) * (ir.l[n] - ir.r[n]);
                }

                row.width[w] = 10.0 * std::log10 (std::max (1.0e-30, side) / std::max (1.0e-30, mid));
            }

            return row;
        }

        void octaves (const Structure& s, double* share)
        {
            constexpr double bands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

            const User u;
            const auto ir = impulseResponse (s, u, irSecondsFor (u.decay));

            double total = 0.0;

            for (int b = 0; b < 7; ++b)
            {
                share[b] = 0.0;

                for (double v : detail::bandEnergy (ir, bands[b], ir.l.size()))
                    share[b] += v;

                total += share[b];
            }

            for (int b = 0; b < 7; ++b)
                share[b] /= total;
        }
    }

    double wetPathNs (const Structure& s)
    {
        const auto noise = whiteNoise (static_cast<size_t> (1.0 * kRate), 0xC0FFEEu);
        const User u;

        double best = 1.0e30;

        for (int attempt = 0; attempt < 5; ++attempt)
        {
            Rig rig (s, u);

            const auto t0 = std::chrono::steady_clock::now();
            const auto out = rig.render (noise, noise, noise.size());
            const auto t1 = std::chrono::steady_clock::now();

            gSink = gSink + out.l.back();

            best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count()
                                   / static_cast<double> (noise.size()));
        }

        return best;
    }

    double wetPathRatio (const Structure& s)
    {
        // Hall and the structure alternate, nine rounds, and the ratio is of
        // the two minima. A single best-of-five per structure (the first
        // version of this) was measured minutes apart from Hall's and came
        // out below 1.0 for chains that add work: the machine's clock speed
        // had moved in between.
        const auto noise = whiteNoise (static_cast<size_t> (1.0 * kRate), 0xC0FFEEu);
        const User u;

        double best[2] { 1.0e30, 1.0e30 };

        for (int round = 0; round < 9; ++round)
            for (int which = 0; which < 2; ++which)
            {
                Rig rig (which == 0 ? Structure {} : s, u);

                const auto t0 = std::chrono::steady_clock::now();
                const auto out = rig.render (noise, noise, noise.size());
                const auto t1 = std::chrono::steady_clock::now();

                gSink = gSink + out.l.back();

                best[which] = std::min (best[which], std::chrono::duration<double, std::nano> (t1 - t0).count());
            }

        return best[1] / best[0];
    }

    std::vector<Semantics> semantics (const std::vector<Candidate>& list, double decayLimit, double cpuLimit)
    {
        // Hall first, as the row everything is read against.
        std::vector<Candidate> all;
        all.push_back (fixed ("Hall", Structure {}));

        for (const auto& c : list)
            all.push_back (c);

        const size_t count = all.size();

        constexpr double sizes[5] { 0.10, 0.30, 0.50, 0.70, 0.90 };
        constexpr double gridSize[3]  { 0.25, 0.60, 0.85 };
        constexpr double gridDecay[3] { 0.8, 2.5, 5.0 };

        std::vector<double> t10 (count * 5), rt (count * 9), share (count * 7);
        std::vector<StereoRow> stereo (count);

        runParallel (count * 5, [&] (size_t i)
        {
            User u;
            u.size = sizes[i % 5];
            t10[i] = featuresOf (all[i / 5].make (u), u).base.t10;
        });

        runParallel (count * 9, [&] (size_t i)
        {
            User u;
            u.size  = gridSize[(i % 9) / 3];
            u.decay = gridDecay[(i % 9) % 3];

            rt[i] = mode12::rt60FromEnergy (detail::energyOfBoth (
                impulseResponse (all[i / 9].make (u), u, irSecondsFor (u.decay) + 1.0)));
        });

        runParallel (count, [&] (size_t k)
        {
            const Structure s = all[k].make (User {});

            stereo[k] = stereoOf (s);
            octaves (s, share.data() + k * 7);
        });

        // CPU alone on the machine: nothing else is running in this process now.
        std::vector<double> ns (count, 1.0);

        for (size_t k = 1; k < count; ++k)
            ns[k] = wetPathRatio (all[k].make (User {}));

        std::vector<Semantics> out (list.size());

        info ("");
        info ("    SIZE (time to 10 % of the energy, ms, Size 10 / 30 / 50 / 70 / 90) and DECAY");
        info ("    (measured RT60 / Hall's over nine settings, min .. max; limit +/- "
              + fx (decayLimit * 100.0, 0) + " %):");

        for (size_t k = 0; k < count; ++k)
        {
            std::string row = "      " + pad (all[k].name, 30);
            bool monotone = true;

            for (int i = 0; i < 5; ++i)
            {
                row += pad (fx (t10[k * 5 + static_cast<size_t> (i)], 1), 8);

                if (i > 0 && t10[k * 5 + static_cast<size_t> (i)] < t10[k * 5 + static_cast<size_t> (i) - 1] - 0.05)
                    monotone = false;
            }

            double low = 1.0e30, high = 0.0;

            for (size_t i = 0; i < 9; ++i)
            {
                const double ratio = rt[k * 9 + i] / rt[i];

                low = std::min (low, ratio);
                high = std::max (high, ratio);
            }

            info (row + pad (monotone ? "" : "NOT MONOTONE", 14) + fx (low, 3) + " .. " + fx (high, 3));

            if (k > 0)
            {
                out[k - 1].sizeMonotone = monotone;
                out[k - 1].decayLow = low;
                out[k - 1].decayHigh = high;
            }
        }

        info ("");
        info ("    STEREO AND MONO at the defaults (limits as in Stage 3, section 12.3.9), TONE (largest");
        info ("    octave-band deviation from Hall, dB; limit 1) and CPU (wet path, x Hall):");
        info ("      " + pad ("structure", 30) + pad ("corr", 7) + pad ("fold", 7) + pad ("L/R", 7) + pad ("mirror", 8)
              + pad ("hard", 7) + pad ("tone worst/med", 16) + pad ("width 50/100/150", 18)
              + pad ("octave", 8) + "CPU");

        for (size_t k = 0; k < count; ++k)
        {
            const auto& r = stereo[k];

            const bool defect = k > 0 && (r.toneWorst < stereo[0].toneWorst - 3.0
                                          || r.hard > stereo[0].hard + 0.05
                                          || std::abs (r.balance) > 1.0 || std::abs (r.mirror) > 1.0
                                          || r.fold < stereo[0].fold - 1.5);

            double deviation = 0.0;

            for (size_t b = 0; b < 7; ++b)
                deviation = std::max (deviation, std::abs (10.0 * std::log10 (share[k * 7 + b] / share[b])));

            const double cpu = ns[k] / ns[0];

            info ("      " + pad (all[k].name, 30) + pad (fx (r.corr), 7) + pad (fx (r.fold, 1), 7)
                  + pad (fx (r.balance, 1), 7) + pad (fx (r.mirror, 1), 8) + pad (fx (r.hard, 3), 7)
                  + pad (fx (r.toneWorst, 1) + " / " + fx (r.toneMedian, 1), 16)
                  + pad (fx (r.width[0], 1) + " " + fx (r.width[1], 1) + " " + fx (r.width[2], 1), 18)
                  + pad (fx (deviation), 8) + fx (cpu)
                  + (defect ? "   STEREO WORSE THAN HALL" : ""));

            if (k > 0)
            {
                auto& o = out[k - 1];

                o.stereoDefect = defect;
                o.toneDb = deviation;
                o.cpu = cpu;
                o.pass = o.sizeMonotone && o.decayLow >= 1.0 - decayLimit && o.decayHigh <= 1.0 + decayLimit
                      && ! defect && deviation <= 1.0 && cpu <= cpuLimit;
            }
        }

        info ("");

        for (size_t k = 0; k < list.size(); ++k)
        {
            const auto& o = out[k];
            std::string why;

            if (! o.sizeMonotone) why += " Size;";
            if (o.decayLow < 1.0 - decayLimit || o.decayHigh > 1.0 + decayLimit) why += " Decay;";
            if (o.stereoDefect) why += " stereo/mono;";
            if (o.toneDb > 1.0) why += " tone;";
            if (o.cpu > cpuLimit) why += " CPU;";

            info ("      " + pad (list[k].name, 30) + (o.pass ? "G3 PASS" : "G3 FAIL:" + why));
        }

        return out;
    }
}

// ===========================================================================
//  The order of work
// ===========================================================================
void runPhase12Stage5Tests()
{
    using namespace stage5;

    const auto started = std::chrono::steady_clock::now();

    ensureFloor();

    const char* only = std::getenv ("AV_S5_PART");
    const std::string part = only != nullptr ? only : "ABCD";

    auto wants = [&] (char c) { return part.find (c) != std::string::npos; };

    Outcome plate, room, chamber, ambient;

    // Part C needs a Room and a Plate to be attacked by. In a run of every part
    // they are the ones just measured; in a run of C alone they are the
    // standing definitions, so an hour is not spent re-deriving them.
    if (wants ('A')) plate = runPlate(); else if (wants ('C')) plate = standingPlate();
    if (wants ('B')) room = runRoom();   else if (wants ('C')) room = standingRoom();
    if (wants ('C'))                chamber = runChamber (room, plate);
    if (wants ('D'))                ambient = runAmbient();

    if (part == "ABCD")
    {
        beginSuite ("PHASE 12.5.9 - five-Mode status after Stage 5");

        info ("    " + pad ("HALL", 10) + "PROVEN - the frozen production engine");
        info ("    " + pad ("PLATE", 10) + plate.status);
        info ("    " + pad ("ROOM", 10) + room.status);
        info ("    " + pad ("CHAMBER", 10) + chamber.status);
        info ("    " + pad ("AMBIENT", 10) + ambient.status);

        check (plate.found,   "FINDING: a Plate mechanism survived preliminary structural rejection");
        check (room.found,    "FINDING: a Room mechanism survived preliminary structural rejection");
        check (chamber.found, "FINDING: a Chamber mechanism survived preliminary structural rejection");
        check (ambient.found, "FINDING: an Ambient mechanism survived preliminary structural rejection");
    }

    info ("");
    info ("  Stage 5 suite wall time: "
          + fx (std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count(), 0)
          + " s");
}

} // namespace avtest
