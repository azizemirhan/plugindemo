#include "Phase12Stage6.h"
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
    PHASE 12 - STAGE 6. Shared machinery; Room; Ambient; Room against Ambient.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    The metric and the search are Stage 3's, frozen at c18ecce. Gates,
    candidates and predictions are in Docs/Phase12-Stage6-Preregistration.md,
    committed before this file measured anything.

    AV_S6_PART selects parts: R Room, A Ambient, X Room against Ambient,
    P Plate, C Chamber, M the cross-mode matrix, E cost and switching.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace stage6
{
    using stage5::fixed;
    using stage5::levelled;
    using stage5::decayCalibrated;
    using stage5::seedFor;
    using stage5::kLateralFamily;
    using stage5::kSeeds;

    // =======================================================================
    //  The search with a knob-dependent impostor
    // =======================================================================
    /*
        structural::mimic with one difference: `impostor (toUser (c))` where
        the original has a fixed structure. Everything else - the scatter,
        the three refined starts, the step schedule, the oblique moves, the
        random stream - is the original's, in the original's order.
    */
    SearchResult mimicWith (const Features& target, const Maker& impostor, const Floor& floor,
                            const Controls& startAt, unsigned int seed, int ignoreFamily)
    {
        constexpr double kDecayLow = 0.1, kDecayHigh = 20.0;

        SearchResult result;

        auto evaluate = [&] (const Controls& c)
        {
            ++result.evaluations;

            const User u = toUser (c);
            auto d = distance (target, featuresOf (impostor (u), u));

            if (ignoreFamily >= 0)
                d.family[ignoreFamily] = 0.0;

            return score (d, floor);
        };

        struct Scored { Controls c; Score s; };

        std::vector<Scored> starts;
        starts.push_back ({ startAt, evaluate (startAt) });

        result.start = starts[0].s;

        unsigned int state = seed | 1u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        for (int i = 0; i < 24; ++i)
        {
            Controls c;

            for (auto& v : c.v)
                v = next01();

            if (i % 2 == 0) c.v[4] = 0.0;
            if (i % 4 == 0) c.v[2] = 0.0;
            if (i % 3 != 0) c.v[6] = 0.5;

            if (i % 2 == 1 && target.base.rt60 > 0.0)
                c.v[1] = std::clamp (std::log (target.base.rt60 / kDecayLow)
                                     / std::log (kDecayHigh / kDecayLow), 0.0, 1.0);

            starts.push_back ({ c, evaluate (c) });
        }

        std::sort (starts.begin(), starts.end(), [] (const Scored& a, const Scored& b)
        {
            return a.s.objective() < b.s.objective();
        });

        result.best.worst = 1.0e9;

        for (int s = 0; s < 3; ++s)
        {
            Controls current = starts[static_cast<size_t> (s)].c;
            Score currentS   = starts[static_cast<size_t> (s)].s;

            double step = 0.16;
            int budget = 260;

            while (step >= 0.0049 && budget > 0)
            {
                bool improved = false;

                for (int k = 0; k < kControls && budget > 0; ++k)
                    for (double direction : { 1.0, -1.0 })
                    {
                        Controls trial = current;
                        trial.v[k] = std::clamp (trial.v[k] + direction * step, 0.0, 1.0);

                        if (trial.v[k] == current.v[k])
                            continue;

                        const auto sc = evaluate (trial);
                        --budget;

                        if (sc.objective() < currentS.objective())
                        {
                            current = trial;
                            currentS = sc;
                            improved = true;
                        }
                    }

                if (! improved)
                {
                    for (int attempt = 0; attempt < 8 && budget > 0 && ! improved; ++attempt)
                    {
                        Controls trial = current;

                        for (auto& v : trial.v)
                            v = std::clamp (v + step * (2.0 * next01() - 1.0), 0.0, 1.0);

                        const auto sc = evaluate (trial);
                        --budget;

                        if (sc.objective() < currentS.objective())
                        {
                            current = trial;
                            currentS = sc;
                            improved = true;
                        }
                    }
                }

                if (! improved)
                    step *= 0.5;
            }

            if (currentS.objective() < result.best.objective())
            {
                result.best = currentS;
                result.controls = current;
            }
        }

        return result;
    }

    void runProbes (std::vector<Probe>& probes)
    {
        runParallel (probes.size(), [&] (size_t i)
        {
            auto& p = probes[i];

            const auto found = mimicWith (p.target, p.impostor, floorOf(), toControls (p.start),
                                          p.seed, kLateralFamily);

            p.residual = found.best.worst;
            p.family   = found.best.worstFamily;
            p.knobs    = describe (found.controls);
            p.best     = found.best;
        });
    }

    std::vector<Gate> gateWith (const std::vector<Candidate>& targets, const Maker& impostor, int seeds,
                                std::vector<std::array<Score, kPointCount>>* best)
    {
        const size_t count = targets.size();

        std::vector<Features> features (count * kPointCount);

        runParallel (features.size(), [&] (size_t i)
        {
            const User u = userAt (kPoints[i % kPointCount]);
            features[i] = featuresOf (targets[i / kPointCount].make (u), u);
        });

        std::vector<Probe> probes;

        for (size_t t = 0; t < count; ++t)
            for (int p = 0; p < kPointCount; ++p)
                for (int k = 0; k < seeds; ++k)
                {
                    Probe probe;
                    probe.target = features[t * kPointCount + static_cast<size_t> (p)];
                    probe.impostor = impostor;
                    probe.start = userAt (kPoints[p]);
                    probe.seed = seedFor (k);
                    probes.push_back (probe);
                }

        runProbes (probes);

        std::vector<Gate> gates (count);

        if (best != nullptr)
            best->assign (count, {});

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
                    const auto& found = probes[(t * kPointCount + static_cast<size_t> (p))
                                               * static_cast<size_t> (seeds) + static_cast<size_t> (k)];
                    const double r = found.residual;

                    if (r < lowest)
                    {
                        lowest = r;
                        g.family[p] = found.family;
                        g.knobs[p] = found.knobs;

                        if (best != nullptr)
                            (*best)[t][static_cast<size_t> (p)] = found.best;
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

    // =======================================================================
    //  Families
    // =======================================================================
    Maker lawful (const Maker& raw)
    {
        constexpr double sizes[4]  { 0.0, 0.33, 0.67, 1.0 };
        constexpr double decays[4] { 0.3, 1.0, 3.5, 12.0 };

        auto table = std::make_shared<std::array<double, 16>>();

        runParallel (16, [&] (size_t i)
        {
            User u;
            u.size  = sizes[i / 4];
            u.decay = decays[i % 4];

            const double seconds = irSecondsFor (u.decay) + 1.0;
            const double reference = mode12::rt60FromEnergy (detail::energyOfBoth (
                impulseResponse (Structure {}, u, seconds)));

            Structure s = raw (u);

            for (int pass = 0; pass < 3; ++pass)
            {
                const double own = mode12::rt60FromEnergy (detail::energyOfBoth (impulseResponse (s, u, seconds)));

                if (own > 0.0 && reference > 0.0)
                    s.decayScale *= reference / own;
            }

            (*table)[i] = s.decayScale;
        });

        return [raw, table] (const User& u)
        {
            constexpr double gridSize[4]  { 0.0, 0.33, 0.67, 1.0 };
            constexpr double gridDecay[4] { 0.3, 1.0, 3.5, 12.0 };

            auto locate = [] (const double* grid, double x, int& index, double& t)
            {
                index = 0;

                while (index < 2 && x > grid[index + 1])
                    ++index;

                t = std::clamp ((x - grid[index]) / (grid[index + 1] - grid[index]), 0.0, 1.0);
            };

            double logGrid[4];

            for (int i = 0; i < 4; ++i)
                logGrid[i] = std::log (gridDecay[i]);

            int a = 0, b = 0;
            double ta = 0.0, tb = 0.0;

            locate (gridSize, u.size, a, ta);
            locate (logGrid, std::log (std::max (0.05, u.decay)), b, tb);

            const auto at = [&] (int i, int j) { return (*table)[static_cast<size_t> (i * 4 + j)]; };

            Structure s = raw (u);

            s.decayScale = (1.0 - ta) * ((1.0 - tb) * at (a, b)     + tb * at (a, b + 1))
                         +        ta  * ((1.0 - tb) * at (a + 1, b) + tb * at (a + 1, b + 1));

            return s;
        };
    }

    Family familyOf (const std::string& name, const Maker& raw, bool calibrateDecay)
    {
        Family f;

        f.name = name;
        f.raw = raw;
        f.target   = calibrateDecay ? levelled (decayCalibrated ({ name, raw })) : levelled ({ name, raw });
        f.impostor = levelled ({ name, calibrateDecay ? lawful (raw) : raw });

        return f;
    }

    Family hallFamily()
    {
        Family f;

        f.name = "Hall";
        f.raw = [] (const User&) { return Structure {}; };
        f.target = f.impostor = fixed ("Hall", Structure {});
        f.standing = f.structural = true;
        f.status = "PROVEN - the frozen production engine";

        return f;
    }

    // =======================================================================
    //  Measurements
    // =======================================================================
    namespace
    {
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
    }

    Safety safetyOf (const Structure& s, const User& u)
    {
        Safety row;

        const auto centre = impulseResponse (s, u, 3.0);
        const auto left   = impulseResponse (s, u, 3.0, true, false);
        const auto right  = impulseResponse (s, u, 3.0, false, true);

        row.corr = analyse (centre, left).base.corr[2];
        row.fold = foldDb (centre, 0);

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

        return row;
    }

    bool safetyDefect (const Safety& s, const Safety& hall)
    {
        return s.toneWorst < hall.toneWorst - 3.0
            || std::abs (s.balance) > 1.0 || std::abs (s.mirror) > 1.0
            || s.fold < hall.fold - 1.5;
    }

    std::array<double, 7> octaveShares (const Structure& s, const User& u, double fromSeconds, double toSeconds)
    {
        constexpr double bands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

        const auto ir = impulseResponse (s, u, irSecondsFor (u.decay));

        const auto from = std::min (ir.l.size(), static_cast<size_t> (fromSeconds * kRate));
        const auto to   = toSeconds > 1.0e8 ? ir.l.size()
                                            : std::min (ir.l.size(), static_cast<size_t> (toSeconds * kRate));

        std::array<double, 7> share {};
        double total = 0.0;

        for (size_t b = 0; b < 7; ++b)
        {
            const auto e = detail::bandEnergy (ir, bands[b], ir.l.size());

            for (size_t n = from; n < to; ++n)
                share[b] += e[n];

            total += share[b];
        }

        for (auto& v : share)
            v /= std::max (1.0e-300, total);

        return share;
    }

    double octaveDeviation (const std::array<double, 7>& a, const std::array<double, 7>& reference,
                            std::array<double, 7>* signedDb)
    {
        double worst = 0.0;

        for (size_t b = 0; b < 7; ++b)
        {
            const double db = 10.0 * std::log10 (std::max (1.0e-300, a[b]) / std::max (1.0e-300, reference[b]));

            if (signedDb != nullptr)
                (*signedDb)[b] = db;

            worst = std::max (worst, std::abs (db));
        }

        return worst;
    }

    double modalWorst (const Candidate& c)
    {
        double worst = 0.0;
        std::mutex guard;

        runParallel (kPointCount, [&] (size_t p)
        {
            const User u = userAt (kPoints[p]);
            const double v = featuresOf (c.make (u), u).modalDb;

            std::lock_guard<std::mutex> lock (guard);
            worst = std::max (worst, v);
        });

        return worst;
    }

    // =======================================================================
    //  Room
    // =======================================================================
    namespace
    {
        [[nodiscard]] std::vector<double> hadamard (int n)
        {
            std::vector<double> m (static_cast<size_t> (n * n), 0.0);

            m[0] = 1.0;

            for (int size = 1; size < n; size *= 2)
                for (int r = 0; r < size; ++r)
                    for (int c = 0; c < size; ++c)
                    {
                        const double v = m[static_cast<size_t> (r * n + c)];

                        m[static_cast<size_t> (r * n + c + size)]          =  v;
                        m[static_cast<size_t> ((r + size) * n + c)]        =  v;
                        m[static_cast<size_t> ((r + size) * n + c + size)] = -v;
                    }

            for (auto& v : m)
                v /= std::sqrt (static_cast<double> (n));

            return m;
        }

        [[nodiscard]] std::array<double, 8> sortedHall()
        {
            auto base = Structure {}.delaysMs;
            std::sort (base.begin(), base.end());
            return base;
        }

        /** RR6: eight local lines and Hall's eight as the field, in the open tank. */
        [[nodiscard]] Structure roomSixteen (const RoomSpec& spec, const User& u)
        {
            const auto base = sortedHall();
            const double s  = mapping::sizeScaleFor (u.size);
            const double s0 = mapping::sizeScaleFor (0.60);

            const double localScale = spec.localScale * std::pow (s / s0, spec.localPower);

            double mean = 0.0;

            for (int i = 0; i < 4; ++i)
                mean += base[static_cast<size_t> (i)];

            mean = mean / 4.0 * localScale;

            // RR6: eight local lines. RR7: with two allpasses in each. RR8: sixteen, with them.
            const int  local = spec.ringing == 8 ? 16 : 8;
            const bool allpasses = spec.ringing >= 7;

            std::vector<double> ratio (static_cast<size_t> (local));
            double ratioSum = 0.0;

            for (int i = 0; i < local; ++i)
            {
                // 1 : 2.24, geometrically; for eight lines this is 1, 1.12, 1.26 ... 2.24.
                ratio[static_cast<size_t> (i)] = local == 8 ? std::round (100.0 * std::pow (2.24, i / 7.0)) / 100.0
                                                            : std::pow (2.24, i / static_cast<double> (local - 1));
                ratioSum += ratio[static_cast<size_t> (i)];
            }

            auto open = std::make_shared<OpenSpec>();

            open->lines = local + 8;

            for (int i = 0; i < local; ++i)
            {
                open->delayMs.push_back (mean * local * ratio[static_cast<size_t> (i)] / ratioSum);

                if (allpasses)
                    open->allpass.push_back ({ { 0.37, 0.5, 0.0, 0.0 }, { 0.23, 0.5, 0.0, 0.0 } });
            }

            for (int i = 0; i < 8; ++i)
                open->delayMs.push_back (base[static_cast<size_t> (i)]);

            const double loop = mean * (allpasses ? 1.0 + 0.37 + 0.23 : 1.0);
            const double tau = spec.k * mapping::rt60For (u.decay) * std::pow (s / s0, spec.tauPower);
            const double theta = std::asin (std::sqrt (std::min (0.5, loop * s * 0.001 / tau)));
            const double c = std::cos (theta), sn = std::sin (theta);

            const auto hLocal = hadamard (local);
            const auto hField = hadamard (8);

            // blockdiag (H local, H8), then local row i rotated against field row i, i < 8.
            for (int r = 0; r < local; ++r)
                for (int col = 0; col < local; ++col)
                {
                    const double v = hLocal[static_cast<size_t> (r * local + col)];

                    open->matrix.push_back ({ r, col, r < 8 ? c * v : v });

                    if (r < 8)
                        open->matrix.push_back ({ local + r, col, -sn * v });
                }

            for (int r = 0; r < 8; ++r)
                for (int col = 0; col < 8; ++col)
                {
                    const double v = hField[static_cast<size_t> (r * 8 + col)];

                    open->matrix.push_back ({ r,         local + col, sn * v });
                    open->matrix.push_back ({ local + r, local + col, c * v });
                }

            // Stage 5's eight-line sign vectors: none of them a Hadamard row.
            constexpr double inL8[8]  { 1, 1, 1, -1,  1,  1,  1, -1 };
            constexpr double inR8[8]  { 1, 1, -1, 1, -1, -1,  1, -1 };
            constexpr double outL8[8] { 1, -1, 1, 1,  1, -1,  1,  1 };
            constexpr double outR8[8] { -1, 1, 1, 1,  1, -1, -1, -1 };

            const double norm = 1.0 / std::sqrt (static_cast<double> (local));

            for (int i = 0; i < local; ++i)
            {
                double inL = 0.0, inR = 0.0, outL = 0.0, outR = 0.0;

                if (spec.vectors == 3)
                {
                    // A four-line pattern tiled over the groups of four, the
                    // right channel's sign alternating from group to group.
                    const auto k = static_cast<size_t> (i % 4);
                    const double flip = (i / 4) % 2 == 1 ? -1.0 : 1.0;

                    inL = spec.inL[k];   inR = flip * spec.inR[k];
                    outL = spec.outL[k]; outR = flip * spec.outR[k];
                }
                else
                {
                    const double flip = (i / 8) % 2 == 1 ? -1.0 : 1.0;

                    inL = inL8[i % 8];   inR = flip * inR8[i % 8];
                    outL = outL8[i % 8]; outR = flip * outR8[i % 8];
                }

                open->feeds.push_back ({ i, 0.0, inL * norm, inR * norm });
                open->reads.push_back ({ i, 1.0, 0.0, outL * norm, outR * norm });
            }

            Structure out;
            out.name = spec.name;
            out.open = open;
            return out;
        }
    }

    Maker roomRaw (const RoomSpec& spec)
    {
        return [spec] (const User& u)
        {
            using stage5::Blocks;
            using stage5::Mix;

            if (spec.ringing >= 6)
                return roomSixteen (spec, u);

            const auto base = sortedHall();

            const double s  = mapping::sizeScaleFor (u.size);
            const double s0 = mapping::sizeScaleFor (0.60);

            const double localScale = spec.localScale * std::pow (s / s0, spec.localPower);
            const double tau = spec.k * mapping::rt60For (u.decay) * std::pow (s / s0, spec.tauPower);

            auto angle = [&] (double meanMs, double rate)
            {
                return std::asin (std::sqrt (std::min (0.5, meanMs * s * 0.001 * rate / tau)));
            };

            if (spec.ringing == 3)
            {
                // RR3: two local clusters either side of the field.
                Blocks b;

                b.blocks.push_back ({ { 0, 1 }, localScale * 0.18 / 0.25, Mix::rotation, 1.0 });
                b.blocks.push_back ({ { 4, 5, 6, 7 }, 1.0, Mix::hadamard, 1.0 });
                b.blocks.push_back ({ { 2, 3 }, localScale * 0.36 / 0.25, Mix::rotation, 1.0 });

                const double meanA = 0.5 * (base[0] + base[1]) * localScale * 0.18 / 0.25;
                const double meanB = 0.5 * (base[2] + base[3]) * localScale * 0.36 / 0.25;

                b.theta = { angle (meanA, 1.0), angle (meanB, 1.0) };
                b.in  = { 0, 2 };
                b.out = { 0, 2 };

                return stage5::build (spec.name, b);
            }

            Blocks b;

            b.blocks.push_back ({ { 0, 1, 2, 3 }, localScale, spec.ringing == 4 ? Mix::householder : Mix::hadamard, 1.0 });
            b.blocks.push_back ({ { 4, 5, 6, 7 }, 1.0, Mix::hadamard, 1.0 });
            b.theta = { 0.0 };
            b.in  = { 0 };
            b.out = { 0 };

            Structure out = stage5::build (spec.name, b);

            // --- the local block's delays -----------------------------------------
            double mean = 0.0;

            for (int i = 0; i < 4; ++i)
                mean += out.delaysMs[static_cast<size_t> (i)];

            mean /= 4.0;

            if (spec.ringing == 2)
            {
                constexpr double ratio[4] { 1.0, 1.31, 1.73, 2.27 };
                constexpr double ratioSum = 1.0 + 1.31 + 1.73 + 2.27;

                for (int i = 0; i < 4; ++i)
                    out.delaysMs[static_cast<size_t> (i)] = mean * 4.0 * ratio[i] / ratioSum;
            }

            double loop = mean;

            if (spec.ringing == 1)
            {
                for (int i = 0; i < 4; ++i)
                    out.lineAllpass[static_cast<size_t> (i)] = { { 0.37, 0.5 }, { 0.23, 0.5 } };

                loop = mean * (1.0 + 0.37 + 0.23);
            }

            // --- the exchange: row i against row i + 4 ---------------------------
            constexpr double equal[4]   { 1.0, 1.0, 1.0, 1.0 };
            constexpr double unequal[4] { 0.5, 0.8, 1.25, 2.0 };

            const double* rate = spec.ringing == 5 ? unequal : equal;
            const double rateMean = spec.ringing == 5 ? (0.5 + 0.8 + 1.25 + 2.0) / 4.0 : 1.0;

            for (int i = 0; i < 4; ++i)
            {
                const double theta = angle (loop, rate[i] / rateMean);
                const double c = std::cos (theta), sn = std::sin (theta);

                for (int col = 0; col < 8; ++col)
                {
                    const double ma = out.customMatrix[static_cast<size_t> (i * 8 + col)];
                    const double mb = out.customMatrix[static_cast<size_t> ((i + 4) * 8 + col)];

                    out.customMatrix[static_cast<size_t> (i * 8 + col)]       =  c * ma + sn * mb;
                    out.customMatrix[static_cast<size_t> ((i + 4) * 8 + col)] = -sn * ma + c * mb;
                }
            }

            // --- the vectors ---------------------------------------------------------
            if (spec.vectors != 0)
            {
                std::array<double, 4> inL {}, inR {}, outL {}, outR {};

                if (spec.vectors == 1)
                {
                    constexpr double mid[4]  { 1, 1, 1, -1 }, side[4]  { 1, -1, 1, 1 };
                    constexpr double midO[4] { 1, 1, -1, 1 }, sideO[4] { -1, 1, 1, 1 };

                    const double norm = 0.5 / std::sqrt (2.0);

                    for (size_t i = 0; i < 4; ++i)
                    {
                        inL[i]  = norm * (mid[i]  + side[i]);
                        inR[i]  = norm * (mid[i]  - side[i]);
                        outL[i] = norm * (midO[i] + sideO[i]);
                        outR[i] = norm * (midO[i] - sideO[i]);
                    }
                }
                else if (spec.vectors == 2)
                {
                    const auto q = patterns::genericQuad();

                    inL = q[0]; inR = q[1]; outL = q[2]; outR = q[3];
                }
                else
                {
                    for (size_t i = 0; i < 4; ++i)
                    {
                        inL[i]  = 0.5 * spec.inL[i];
                        inR[i]  = 0.5 * spec.inR[i];
                        outL[i] = 0.5 * spec.outL[i];
                        outR[i] = 0.5 * spec.outR[i];
                    }
                }

                out.customInL.fill (0.0);  out.customInR.fill (0.0);
                out.customOutL.fill (0.0); out.customOutR.fill (0.0);

                for (size_t i = 0; i < 4; ++i)
                {
                    out.customInL[i]  = inL[i];
                    out.customInR[i]  = inR[i];
                    out.customOutL[i] = outL[i];
                    out.customOutR[i] = outR[i];
                }
            }

            return out;
        };
    }

    // =======================================================================
    //  Ambient
    // =======================================================================
    Maker ambientRaw (const AmbientSpec& spec)
    {
        return [spec] (const User& u)
        {
            using stage5::Blocks;
            using stage5::Mix;

            const auto base = sortedHall();
            const auto count = spec.stages.size();

            const double scale = mapping::sizeScaleFor (u.size);
            const double total = spec.k * mapping::rt60For (u.decay);
            const double tau = total / static_cast<double> (count - 1);

            Blocks b;

            for (const auto& lines : spec.stages)
                b.blocks.push_back ({ lines, 1.0, lines.size() == 2 ? Mix::rotation : Mix::hadamard, 1.0 });

            for (size_t s = 0; s + 1 < count; ++s)
            {
                double mean = 0.0;

                for (int line : spec.stages[s])
                    mean += base[static_cast<size_t> (line)];

                mean = mean / static_cast<double> (spec.stages[s].size()) * scale * 0.001;

                b.theta.push_back (std::asin (std::sqrt (std::min (0.5, mean / tau))));
            }

            const int last = static_cast<int> (count) - 1;

            b.in = { 0 };
            b.out = { last };

            if (spec.direct > 0.0)
                b.in = { 0, last };

            if (! spec.hear.empty())
            {
                b.out.clear();

                for (int s = 0; s <= last; ++s)
                    b.out.push_back (s);
            }

            Structure out = stage5::build (spec.name, b);

            // Where each stage's lines sit, in block order.
            std::vector<size_t> first (count, 0);

            for (size_t s = 1; s < count; ++s)
                first[s] = first[s - 1] + spec.stages[s - 1].size();

            auto weigh = [&] (std::array<double, 8>& v, const std::vector<double>& weight)
            {
                double norm = 0.0;

                for (size_t s = 0; s < count; ++s)
                    for (size_t i = 0; i < spec.stages[s].size(); ++i)
                    {
                        auto& x = v[first[s] + i];

                        // Back to a unit entry, then the stage's weight.
                        x = (x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 0.0) * weight[s];
                        norm += x * x;
                    }

                // A stage of n lines carried n entries of 1 / sqrt (n).
                for (auto& x : v)
                    x /= std::sqrt (std::max (1.0e-30, norm));
            };

            if (! spec.hear.empty())
            {
                weigh (out.customOutL, spec.hear);
                weigh (out.customOutR, spec.hear);
            }

            if (spec.direct > 0.0)
            {
                std::vector<double> weight (count, 0.0);

                weight[0] = std::sqrt (1.0 - spec.direct);
                weight[static_cast<size_t> (last)] = std::sqrt (spec.direct);

                weigh (out.customInL, weight);
                weigh (out.customInR, weight);
            }

            out.dampLowPower  = spec.gammaLow;
            out.dampHighPower = spec.gammaHigh;

            return out;
        };
    }

    // =======================================================================
    //  Shared printing
    // =======================================================================
    namespace
    {
        struct Shape { double t10 = 0, t50 = 0, centroid = 0, early = 0; };

        [[nodiscard]] Shape shapeOf (const Structure& s, const User& u)
        {
            const auto ir = impulseResponse (s, u, irSecondsFor (u.decay));
            const auto e = detail::energyOfBoth (ir);

            double total = 0.0, moment = 0.0, early = 0.0;

            for (size_t n = 0; n < e.size(); ++n)
            {
                total += e[n];
                moment += e[n] * static_cast<double> (n);

                if (n < static_cast<size_t> (0.080 * kRate))
                    early += e[n];
            }

            Shape shape;

            shape.centroid = 1000.0 * moment / std::max (1.0e-300, total) / kRate;
            shape.early = early / std::max (1.0e-300, total);

            double running = 0.0;
            bool got10 = false, got50 = false;

            for (size_t n = 0; n < e.size(); ++n)
            {
                running += e[n];

                if (! got10 && running >= 0.10 * total) { shape.t10 = n * 1000.0 / kRate; got10 = true; }
                if (! got50 && running >= 0.50 * total) { shape.t50 = n * 1000.0 / kRate; got50 = true; break; }
            }

            return shape;
        }

        constexpr double kSizes[5] { 0.10, 0.30, 0.50, 0.70, 0.90 };

        struct SizeRow
        {
            Shape shape[5];
            bool monotone = false, meaningful = false, centroidRises = false;

            [[nodiscard]] bool repaired() const { return monotone && meaningful && centroidRises; }
        };

        [[nodiscard]] std::vector<SizeRow> sizeRows (const std::vector<Candidate>& list)
        {
            std::vector<SizeRow> rows (list.size());

            runParallel (list.size() * 5, [&] (size_t i)
            {
                User u;
                u.size = kSizes[i % 5];
                rows[i / 5].shape[i % 5] = shapeOf (list[i / 5].make (u), u);
            });

            for (auto& r : rows)
            {
                r.monotone = r.centroidRises = true;

                for (int i = 1; i < 5; ++i)
                {
                    if (r.shape[i].t10 < r.shape[i - 1].t10 - 0.05)      r.monotone = false;
                    if (r.shape[i].centroid <= r.shape[i - 1].centroid)  r.centroidRises = false;
                }

                r.meaningful = r.shape[4].t10 >= 1.25 * r.shape[0].t10;
            }

            return rows;
        }

        void printSizeRows (const std::vector<Candidate>& list, const std::vector<SizeRow>& rows)
        {
            info ("    Size 10 / 30 / 50 / 70 / 90.  t10 and t50: ms to 10 % and 50 % of the energy.");
            info ("    centroid: ms.  early: share of the energy in the first 80 ms, %.");
            info ("");
            info ("    " + pad ("structure", 26) + pad ("t10", 32) + pad ("t50", 34) + pad ("centroid", 34) + "early");

            for (size_t k = 0; k < list.size(); ++k)
            {
                std::string a, b, c, d;

                for (int i = 0; i < 5; ++i)
                {
                    a += pad (fx (rows[k].shape[i].t10, 1), 6);
                    b += pad (fx (rows[k].shape[i].t50, 0), 6);
                    c += pad (fx (rows[k].shape[i].centroid, 0), 6);
                    d += pad (fx (100.0 * rows[k].shape[i].early, 0), 5);
                }

                info ("    " + pad (list[k].name, 26) + pad (a, 32) + pad (b, 34) + pad (c, 34) + d);
            }

            info ("");

            for (size_t k = 0; k < list.size(); ++k)
                info ("    " + pad (list[k].name, 26)
                      + "t10 x " + fx (rows[k].shape[4].t10 / std::max (1.0e-9, rows[k].shape[0].t10))
                      + (rows[k].monotone ? "   not falling" : "   FALLS")
                      + (rows[k].meaningful ? "   >= 1.25" : "   UNDER 1.25")
                      + (rows[k].centroidRises ? "   centroid rises" : "   CENTROID DOES NOT RISE"));
        }

        /** Hall's search against a candidate at five Sizes, Decay 2.5 s. */
        [[nodiscard]] std::vector<Probe> sizeProbes (const Candidate& c)
        {
            std::vector<Features> features (5);

            runParallel (5, [&] (size_t i)
            {
                User u;
                u.size = kSizes[i];
                features[i] = featuresOf (c.make (u), u);
            });

            std::vector<Probe> probes;

            for (int i = 0; i < 5; ++i)
            {
                Probe p;
                p.target = features[static_cast<size_t> (i)];
                p.impostor = [] (const User&) { return Structure {}; };
                p.start = User {};
                p.start.size = kSizes[i];
                p.seed = seedFor (0);
                probes.push_back (p);
            }

            runProbes (probes);

            return probes;
        }

        [[nodiscard]] double printSizeProbes (const std::string& name, const std::vector<Probe>& probes)
        {
            std::string row = "    " + pad (name, 26);
            double lowest = 1.0e30;

            for (const auto& p : probes)
            {
                row += pad (fx (p.residual), 7);
                lowest = std::min (lowest, p.residual);
            }

            info (row + (lowest <= kReproduced ? "  HALL AGAIN at some Size" : "  not Hall at any Size"));

            return lowest;
        }

        [[nodiscard]] double lowestMedian (const Gate& g)
        {
            return *std::min_element (g.median, g.median + kPointCount);
        }

        void printSafetyHeader()
        {
            info ("    " + pad ("structure", 26) + pad ("corr", 7) + pad ("fold", 7) + pad ("L/R", 7) + pad ("mirror", 8)
                  + "mono tone worst / median, dB");
        }

        void printSafety (const std::string& name, const Safety& s, const Safety* hall)
        {
            info ("    " + pad (name, 26) + pad (fx (s.corr), 7) + pad (fx (s.fold, 1), 7) + pad (fx (s.balance, 1), 7)
                  + pad (fx (s.mirror, 1), 8) + pad (fx (s.toneWorst, 1) + " / " + fx (s.toneMedian, 1), 16)
                  + (hall != nullptr && safetyDefect (s, *hall) ? "  WORSE THAN HALL" : ""));
        }
    }

    RoomSpec standingRoomSpec()       { return RoomSpec {}; }
    AmbientSpec standingAmbientSpec() { return AmbientSpec {}; }

    // =======================================================================
    //  Part 1 - Room
    // =======================================================================
    Family runRoom()
    {
        const Candidate hallC = fixed ("Hall", Structure {});

        auto candidateOf = [] (const RoomSpec& spec)
        {
            return levelled (decayCalibrated ({ spec.name, roomRaw (spec) }));
        };

        const RoomSpec r4c;

        // ===================================================================
        beginSuite ("PHASE 12.6.2 - Room: Size");
        // ===================================================================

        info ("  R4c's exchange time is 0.12 x Decay and has no Size in it. Three laws");
        info ("  that put Size in, and R4c itself. A law repairs Size if t10 does not");
        info ("  fall, t10 at Size 90 is >= 1.25 x t10 at Size 10 (Hall's is 1.79),");
        info ("  and the temporal centroid rises at every step.");
        info ("");

        std::vector<RoomSpec> sizeSpecs;

        { RoomSpec s = r4c; s.name = "RS1 exchange x s";       s.tauPower = 1.0;   sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS2 exchange x s^2";     s.tauPower = 2.0;   sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS3 local block x s";    s.localPower = 1.0; sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS5 exch s, block s";    s.tauPower = 1.0; s.localPower = 1.0; sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS6 exch s^2, block s";  s.tauPower = 2.0; s.localPower = 1.0; sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS7 exch s, block s^2";  s.tauPower = 1.0; s.localPower = 2.0; sizeSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RS8 exch s, block s^1.5"; s.tauPower = 1.0; s.localPower = 1.5; sizeSpecs.push_back (s); }

        auto listOf = [&] (const std::vector<RoomSpec>& specs, bool withBase)
        {
            std::vector<Candidate> list;
            list.push_back (hallC);

            if (withBase)
                list.push_back (candidateOf (r4c));

            for (const auto& s : specs)
                list.push_back (candidateOf (s));

            return list;
        };

        auto sizeList = listOf (sizeSpecs, true);
        auto rows = sizeRows (sizeList);

        // RS4 (both levers, built only if each repaired Size alone) was not built
        // in the first run: neither did. Addendum 1 gives the reason to build
        // the combinations after all - the two-rate model - and names three.
        info ("  RS1-RS3 are the first run's laws; none passed. RS5-RS7 are addendum 1's, RS8 addendum 2's:");
        info ("  exchange time and block scale together.");
        info ("");

        printSizeRows (sizeList, rows);

        check (rows[0].repaired(), "Hall itself meets the Size criterion (a check on the criterion, not on a candidate)");

        // Those that repair Size: Hall's search at five Sizes, then G1.
        std::vector<RoomSpec> sizeRepaired;

        for (size_t k = 0; k < sizeSpecs.size(); ++k)
            if (rows[k + 2].repaired())
                sizeRepaired.push_back (sizeSpecs[k]);

        info ("");
        info ("  " + std::to_string (sizeRepaired.size()) + " of " + std::to_string (sizeSpecs.size())
              + " laws repair Size. Hall's search against each at the five Sizes (Decay 2.5 s,");
        info ("  one search each, centred); <= 1.5 anywhere is Hall again:");
        info ("");

        int bestSize = -1;
        double bestSizeLowest = 0.0;

        if (! sizeRepaired.empty())
        {
            std::vector<Candidate> repaired;

            for (const auto& s : sizeRepaired)
                repaired.push_back (candidateOf (s));

            std::vector<double> lowestAtSizes;

            for (const auto& c : repaired)
                lowestAtSizes.push_back (printSizeProbes (c.name, sizeProbes (c)));

            info ("");
            info ("  G1 at the three operating points:");
            info ("");

            const auto quick = stage5::gate (repaired, hallC, 1);
            stage5::printGate (repaired, quick);

            for (size_t k = 0; k < repaired.size(); ++k)
            {
                const double lowest = std::min (lowestAtSizes[k], quick[k].minimum);

                if (lowest > kReproduced && lowest > bestSizeLowest)
                {
                    bestSizeLowest = lowest;
                    bestSize = static_cast<int> (k);
                }
            }
        }

        info ("");
        info (bestSize >= 0 ? "  SIZE LAW CHOSEN: " + sizeRepaired[static_cast<size_t> (bestSize)].name
                              + "  (lowest run anywhere " + fx (bestSizeLowest) + ")"
                            : "  NO SIZE LAW repairs Size and stays distinct from Hall.");

        check (bestSize >= 0, "FINDING: a Size law makes Size meaningful and is not Hall at any Size");

        // ===================================================================
        beginSuite ("PHASE 12.6.3 - Room: ringing");
        // ===================================================================

        info ("  Modal prominence, worst of the three points, dB. Clean: <= Hall + 3.");
        info ("  Materially reduced: <= Hall + 14 (half of R4c's excess). Tone and damping");
        info ("  are untouched in every candidate.");
        info ("");

        std::vector<RoomSpec> ringSpecs;

        { RoomSpec s = r4c; s.name = "RR1 allpasses in block";   s.ringing = 1; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR2 delays respaced";      s.ringing = 2; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR3 two clusters";         s.ringing = 3; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR4 Householder block";    s.ringing = 4; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR5 unequal exchange";     s.ringing = 5; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR6 8 local, open tank";   s.ringing = 6; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR7 8 local + allpasses";  s.ringing = 7; ringSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RR8 16 local + allpasses"; s.ringing = 8; ringSpecs.push_back (s); }

        auto measureRinging = [&] (const std::vector<RoomSpec>& specs, std::vector<double>& modal)
        {
            const auto list = listOf (specs, true);

            modal.assign (list.size(), 0.0);

            for (size_t k = 0; k < list.size(); ++k)
                modal[k] = modalWorst (list[k]);

            for (size_t k = 0; k < list.size(); ++k)
                info ("    " + pad (list[k].name, 26) + pad (fx (modal[k], 1), 8)
                      + (k == 0 ? "" : modal[k] <= modal[0] + 3.0  ? "clean"
                                     : modal[k] <= modal[0] + 14.0 ? "materially reduced"
                                                                   : "NOT reduced enough"));
        };

        std::vector<double> modal;
        measureRinging (ringSpecs, modal);

        const double hallModal = modal[0];

        std::vector<RoomSpec> ringReduced;
        std::vector<double> ringModal;

        for (size_t k = 0; k < ringSpecs.size(); ++k)
            if (modal[k + 2] <= hallModal + 14.0)
            {
                ringReduced.push_back (ringSpecs[k]);
                ringModal.push_back (modal[k + 2]);
            }

        int bestRing = -1;

        if (! ringReduced.empty())
        {
            std::vector<Candidate> reduced;

            for (const auto& s : ringReduced)
                reduced.push_back (candidateOf (s));

            info ("");
            info ("  G1 for those materially reduced:");
            info ("");

            const auto quick = stage5::gate (reduced, hallC, 1);
            stage5::printGate (reduced, quick);

            for (size_t k = 0; k < reduced.size(); ++k)
                if (quick[k].minimum > kReproduced
                    && (bestRing < 0 || ringModal[k] < ringModal[static_cast<size_t> (bestRing)]))
                    bestRing = static_cast<int> (k);
        }

        // Addendum 2: not candidates (they are not materially reduced and cannot
        // be chosen) - the two partial reductions through G1, for the record.
        {
            std::vector<Candidate> partial;

            for (const auto& s : ringSpecs)
                if (s.ringing == 1 || s.ringing == 6)
                    partial.push_back (candidateOf (s));

            info ("");
            info ("  FOR THE RECORD, not candidates: the two partial reductions through G1, to show");
            info ("  where between 42 dB and 23 dB the identity goes.");
            info ("");

            stage5::printGate (partial, stage5::gate (partial, hallC, 1));
        }

        info ("");
        info (bestRing >= 0 ? "  RINGING REPAIR CHOSEN: "
 + ringReduced[static_cast<size_t> (bestRing)].name
                              + "  (" + fx (ringModal[static_cast<size_t> (bestRing)], 1) + " dB; Hall "
                              + fx (hallModal, 1) + ")"
                            : "  NO RINGING REPAIR is materially reduced and passes G1.");

        check (bestRing >= 0, "FINDING: a ringing repair is materially reduced and is not reproduced by Hall");

        // ===================================================================
        beginSuite ("PHASE 12.6.4 - Room: mono and stereo");
        // ===================================================================

        info ("  Only the block's injection and readout vectors change. Limits are Stage 3's,");
        info ("  against Hall: worst mono tone <= 3 dB deeper, balance and mirror within 1 dB,");
        info ("  fold-down within 1.5 dB.");
        info ("");

        std::vector<RoomSpec> vectorSpecs;

        { RoomSpec s = r4c; s.name = "RM1 mid +/- side";    s.vectors = 1; vectorSpecs.push_back (s); }
        { RoomSpec s = r4c; s.name = "RM2 generic vectors"; s.vectors = 2; vectorSpecs.push_back (s); }

        // RM3 / RM4 / RM5: every orthogonal pair of +/-1 vectors, in and out,
        // chosen on safety (and, from addendum 1, tone) alone. The
        // distinctness metric plays no part in the choice.
        auto chooseVectors = [&] (RoomSpec base, const std::string& name, bool withTone)
        {
            struct Pair { std::array<double, 4> l, r; };

            std::vector<Pair> pairs;

            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 16; ++b)
                {
                    Pair p;
                    double dot = 0.0;

                    for (size_t i = 0; i < 4; ++i)
                    {
                        p.l[i] = i == 0 ? 1.0 : ((a >> (i - 1)) & 1) != 0 ? -1.0 : 1.0;
                        p.r[i] = ((b >> i) & 1) != 0 ? -1.0 : 1.0;
                        dot += p.l[i] * p.r[i];
                    }

                    if (dot == 0.0)
                        pairs.push_back (p);
                }

            auto specAt = [&] (size_t i)
            {
                RoomSpec s = base;
                s.name = name;
                s.vectors = 3;
                s.inL = pairs[i / pairs.size()].l;   s.inR = pairs[i / pairs.size()].r;
                s.outL = pairs[i % pairs.size()].l;  s.outR = pairs[i % pairs.size()].r;
                return s;
            };

            const size_t count = pairs.size() * pairs.size();
            const auto hallShares = octaveShares (Structure {}, User {});

            std::vector<Safety> safety (count);
            std::vector<double> tone (count, 0.0);

            runParallel (count, [&] (size_t i)
            {
                const Structure s = roomRaw (specAt (i)) (User {});

                safety[i] = safetyOf (s);

                if (withTone)
                    tone[i] = octaveDeviation (octaveShares (s, User {}), hallShares);
            });

            auto inside = [&] (size_t i)
            {
                return std::abs (safety[i].balance) <= 0.5 && std::abs (safety[i].mirror) <= 0.5
                    && (! withTone || tone[i] <= 0.75);
            };

            int chosen = -1;
            size_t insideCount = 0;

            for (size_t i = 0; i < count; ++i)
                if (inside (i))
                {
                    ++insideCount;

                    if (chosen < 0 || safety[i].toneWorst > safety[static_cast<size_t> (chosen)].toneWorst)
                        chosen = static_cast<int> (i);
                }

            if (chosen < 0)
            {
                // Nothing is inside. The nearest, by the largest of the three excesses.
                auto excess = [&] (size_t i)
                {
                    return std::max ({ std::abs (safety[i].balance) - 0.5, std::abs (safety[i].mirror) - 0.5,
                                       withTone ? tone[i] - 0.75 : 0.0 });
                };

                chosen = 0;

                for (size_t i = 1; i < count; ++i)
                    if (excess (i) < excess (static_cast<size_t> (chosen)))
                        chosen = static_cast<int> (i);
            }

            const RoomSpec s = specAt (static_cast<size_t> (chosen));

            auto show = [] (const std::array<double, 4>& v)
            {
                std::string t;

                for (double x : v)
                    t += x > 0.0 ? "+" : "-";

                return t;
            };

            info ("  " + name + ": " + std::to_string (count) + " combinations; " + std::to_string (insideCount)
                  + " have balance and mirror within 0.5 dB" + (withTone ? " and octave bands within 0.75 dB." : "."));
            info ("    chosen" + std::string (insideCount > 0 ? " (shallowest worst mono tone among those)" : " (NONE inside; the nearest)")
                  + ": in L " + show (s.inL) + " R " + show (s.inR) + ", out L " + show (s.outL) + " R " + show (s.outR)
                  + (withTone ? ", octave " + fx (tone[static_cast<size_t> (chosen)]) + " dB uncalibrated" : ""));
            info ("");

            return s;
        };

        vectorSpecs.push_back (chooseVectors (r4c, "RM3 safety only", false));
        vectorSpecs.push_back (chooseVectors (r4c, "RM4 safety and tone", true));

        const auto vectorList = listOf (vectorSpecs, true);
        std::vector<Safety> vectorSafety (vectorList.size());
        std::vector<double> vectorTone (vectorList.size(), 0.0);

        runParallel (vectorList.size(), [&] (size_t k)
        {
            const User u;

            vectorSafety[k] = safetyOf (vectorList[k].make (u));
            vectorTone[k] = octaveDeviation (octaveShares (vectorList[k].make (u), u), octaveShares (Structure {}, u));
        });

        printSafetyHeader();

        for (size_t k = 0; k < vectorList.size(); ++k)
        {
            printSafety (vectorList[k].name, vectorSafety[k], k == 0 ? nullptr : &vectorSafety[0]);
            info ("    " + pad ("", 26) + "octave bands " + fx (vectorTone[k]) + " dB" + (vectorTone[k] > 1.0 ? "  OVER 1.0" : ""));
        }

        std::vector<RoomSpec> vectorSafe;
        std::vector<Safety> vectorSafeRow;
        std::vector<double> vectorSafeTone;

        for (size_t k = 0; k < vectorSpecs.size(); ++k)
            if (! safetyDefect (vectorSafety[k + 2], vectorSafety[0]))
            {
                vectorSafe.push_back (vectorSpecs[k]);
                vectorSafeRow.push_back (vectorSafety[k + 2]);
                vectorSafeTone.push_back (vectorTone[k + 2]);
            }

        int bestVector = -1;

        if (! vectorSafe.empty())
        {
            std::vector<Candidate> safe;

            for (const auto& s : vectorSafe)
                safe.push_back (candidateOf (s));

            info ("");
            info ("  G1 for those inside the stereo and mono limits:");
            info ("");

            const auto quick = stage5::gate (safe, hallC, 1);
            stage5::printGate (safe, quick);

            // Inside the octave limit first; then the shallowest worst mono tone.
            for (size_t k = 0; k < safe.size(); ++k)
            {
                if (quick[k].minimum <= kReproduced)
                    continue;

                const bool toned = vectorSafeTone[k] <= 1.0;
                const bool bestToned = bestVector >= 0 && vectorSafeTone[static_cast<size_t> (bestVector)] <= 1.0;

                if (bestVector < 0 || (toned && ! bestToned)
                    || (toned == bestToned
                        && vectorSafeRow[k].toneWorst > vectorSafeRow[static_cast<size_t> (bestVector)].toneWorst))
                    bestVector = static_cast<int> (k);
            }
        }

        info ("");
        info (bestVector >= 0 ? "  VECTORS CHOSEN (four-line block): " + vectorSafe[static_cast<size_t> (bestVector)].name
                                + (vectorSafeTone[static_cast<size_t> (bestVector)] <= 1.0 ? "" : "  - but OVER the octave limit")
                              : "  NO VECTOR SET is inside the limits and passes G1.");

        check (bestVector >= 0 && vectorSafeTone[static_cast<size_t> (bestVector)] <= 1.0,
               "FINDING: a vector set is inside the stereo, mono and octave limits and is not reproduced by Hall");

        // ===================================================================
        beginSuite ("PHASE 12.6.5 - Room: the repaired structure");
        // ===================================================================

        // Addendum 2: built both ways. R5b is R4c with the chosen vectors; R6 is
        // R5b with the chosen Size law. (No ringing repair can be chosen: the
        // ones that reduce the measure were REJECTED at G1.)
        const Candidate r4cC = candidateOf (r4c);

        info ("  R4c itself, for reference - G2, five seeds per point, centred view:");
        info ("");

        const std::vector<Candidate> r4cOnly { r4cC };
        const auto r4cGate = stage5::gate (r4cOnly, hallC, kSeeds);

        stage5::printGate (r4cOnly, r4cGate);

        const double r4cModal = modalWorst (r4cC);

        auto judge = [&] (const RoomSpec& repaired, const std::string& made)
        {
            info ("");
            info ("  ---------------------------------------------------------------------------");
            info ("  " + repaired.name + " = R4c with: " + made);
            info ("  ---------------------------------------------------------------------------");
            info ("");

            Family family = familyOf (repaired.name, roomRaw (repaired), true);

            {
                std::vector<Candidate> list { hallC, r4cC, family.target };
                printSizeRows (list, sizeRows (list));

                info ("");
                info ("  Hall's search at the five Sizes:");
                info ("");
            }

            const double lowestAtSizes = printSizeProbes (family.target.name, sizeProbes (family.target));

            info ("");
            info ("  G2 - five seeds per point, centred view:");
            info ("");

            const std::vector<Candidate> one { family.target };
            const auto serious = stage5::gate (one, hallC, kSeeds);

            stage5::printGate (one, serious);

            const double modal = modalWorst (family.target);

            info ("");
            info ("  Modal prominence: " + fx (modal, 1) + " dB  (R4c " + fx (r4cModal, 1) + ", Hall " + fx (hallModal, 1) + ")");

            const std::vector<Candidate> both { r4cC, family.target };

            stage5::qualityRisks (both);
            stage5::centredSources (both);

            info ("");
            info ("  G3, CPU limit 1.5 x Hall:");

            const auto g3 = stage5::semantics (both, 0.10, 1.5);

            info ("");
            info ("  Stereo and mono at the three operating points (G3 reads the defaults only):");
            info ("");

            printSafetyHeader();

            bool safeEverywhere = true;

            for (const auto& p : kPoints)
            {
                const User u = userAt (p);
                const Safety hallRow = safetyOf (Structure {}, u);
                const Safety own = safetyOf (family.target.make (u), u);

                printSafety (std::string ("Hall, ") + p.label, hallRow, nullptr);
                printSafety (repaired.name + ", " + p.label, own, &hallRow);

                if (safetyDefect (own, hallRow))
                    safeEverywhere = false;
            }

            const auto sizeFinal = sizeRows ({ family.target });

            const bool distinct   = serious[0].verdict == "SURVIVES" && lowestAtSizes > kReproduced;
            const bool sizeOk     = sizeFinal[0].repaired() && g3[1].sizeMonotone;
            const bool decayOk    = g3[1].decayLow >= 0.90 && g3[1].decayHigh <= 1.10;
            const bool ringingOk  = modal <= hallModal + 14.0;
            const bool stereoOk   = ! g3[1].stereoDefect;
            const bool toneOk     = g3[1].toneDb <= 1.0;
            const bool cpuOk      = g3[1].cpu <= 1.5;

            info ("");
            info ("  THE PASS CONDITION (pre-registration 2.4), " + repaired.name + ":");
            info ("    centred distinct at every point, and not Hall at any Size   " + std::string (distinct ? "yes" : "NO")
                  + "   (" + serious[0].verdict + ", lowest median " + fx (lowestMedian (serious[0]))
                  + "; lowest at the five Sizes " + fx (lowestAtSizes) + ")");
            info ("    Size monotonic and meaningful                               " + std::string (sizeOk ? "yes" : "NO"));
            info ("    Decay within 10 %                                           " + std::string (decayOk ? "yes" : "NO"));
            info ("    modal prominence materially reduced                         " + std::string (ringingOk ? "yes" : "NO")
                  + "   (" + fx (modal, 1) + " dB" + (modal <= hallModal + 3.0 ? "; clean" : "; not clean") + ")");
            info ("    stereo and mono inside Stage 3's limits                     " + std::string (stereoOk ? "yes" : "NO")
                  + (safeEverywhere ? "" : "   (NOT at every operating point)"));
            info ("    octave bands within 1 dB                                    " + std::string (toneOk ? "yes" : "NO")
                  + "   (" + fx (g3[1].toneDb) + ")");
            info ("    CPU <= 1.5 x Hall                                           " + std::string (cpuOk ? "yes" : "NO")
                  + "   (" + fx (g3[1].cpu) + ")");

            family.structural = serious[0].verdict != "REJECTED";
            family.standing = distinct && sizeOk && decayOk && ringingOk && stereoOk && toneOk && cpuOk;

            std::string fails;

            if (! distinct)  fails += " distinctness;";
            if (! sizeOk)    fails += " Size;";
            if (! decayOk)   fails += " Decay;";
            if (! ringingOk) fails += " ringing;";
            if (! stereoOk)  fails += " stereo/mono;";
            if (! toneOk)    fails += " tone;";
            if (! cpuOk)     fails += " CPU;";

            family.status = (family.standing ? "PASSES - " : family.structural ? "BLOCKED - " : "REJECTED - ")
                          + repaired.name + " (R4c + " + made + "): " + serious[0].verdict + ", lowest median "
                          + fx (lowestMedian (serious[0])) + ", x " + fx (g3[1].cpu) + " Hall"
                          + (fails.empty() ? "" : "; fails" + fails);

            return family;
        };

        RoomSpec vectorsOnly = r4c;
        vectorsOnly.name = "Room R5b";

        std::string madeVectors = "no vector set was chosen";

        if (bestVector >= 0)
        {
            const auto& v = vectorSafe[static_cast<size_t> (bestVector)];

            vectorsOnly.vectors = v.vectors;
            vectorsOnly.inL = v.inL;   vectorsOnly.inR = v.inR;
            vectorsOnly.outL = v.outL; vectorsOnly.outR = v.outR;
            madeVectors = v.name;
        }

        Family chosen = judge (vectorsOnly, madeVectors);

        if (bestSize >= 0)
        {
            RoomSpec sized = vectorsOnly;

            sized.name = "Room R6";
            sized.tauPower   = sizeRepaired[static_cast<size_t> (bestSize)].tauPower;
            sized.localPower = sizeRepaired[static_cast<size_t> (bestSize)].localPower;

            const Family withSize = judge (sized, madeVectors + "; " + sizeRepaired[static_cast<size_t> (bestSize)].name);

            info ("");

            if (withSize.structural)
            {
                info ("  R6 is not rejected by Hall's search; it is the Room that goes on.");
                chosen = withSize;
            }
            else
            {
                info ("  R6 is REJECTED by Hall's search. R5b is the Room that goes on.");
                info ("    R6:  " + withSize.status);
            }
        }

        if (! chosen.structural)
        {
            info ("");
            info ("  R5b is REJECTED too. R4c remains the structural reference.");

            chosen = familyOf ("Room R4c", roomRaw (r4c), true);
            chosen.structural = r4cGate[0].verdict != "REJECTED";
            chosen.status = "R4c stands structurally (" + r4cGate[0].verdict + "); every repaired composite was REJECTED";
        }

        info ("");
        info ("  ROOM: " + chosen.status);

        check (chosen.standing, "FINDING: the repaired Room passes every condition of 2.4");

        return chosen;
    }

    // =======================================================================
    //  Part 2 - Ambient
    // =======================================================================
    namespace
    {
        constexpr const char* kBandNames = "125    250    500    1k     2k     4k     8k";

        [[nodiscard]] std::string bandsText (const std::array<double, 7>& db)
        {
            std::string t;

            for (double v : db)
                t += pad ((v >= 0.0 ? "+" : "") + fx (v), 7);

            return t;
        }

        /** Largest octave deviation from Hall over Size x Decay x three dampings. */
        [[nodiscard]] double toneGrid (const Candidate& c, double* atDefaults = nullptr)
        {
            constexpr double sizes[3]  { 0.25, 0.60, 0.85 };
            constexpr double decays[3] { 0.8, 2.5, 5.0 };

            std::vector<double> deviation (27, 0.0);

            runParallel (27, [&] (size_t i)
            {
                User u;
                u.size  = sizes[(i / 3) % 3];
                u.decay = decays[i % 3];

                switch (i / 9)
                {
                    case 1:  u.lowDamp = 0.0; u.highDamp = 0.0; break;
                    case 2:  u.highDamp = 0.80; break;
                    default: break;
                }

                deviation[i] = octaveDeviation (octaveShares (c.make (u), u), octaveShares (Structure {}, u));
            });

            if (atDefaults != nullptr)
                *atDefaults = deviation[4];

            return *std::max_element (deviation.begin(), deviation.end());
        }
    }

    Family runAmbient()
    {
        const Candidate hallC = fixed ("Hall", Structure {});

        auto candidateOf = [] (const AmbientSpec& spec)
        {
            return levelled (decayCalibrated ({ spec.name, ambientRaw (spec) }));
        };

        const AmbientSpec a1b;
        const Candidate base = candidateOf (a1b);

        // ===================================================================
        beginSuite ("PHASE 12.6.6 - Ambient: why A1b is off in tone");
        // ===================================================================

        info ("  Octave shares of A1b against Hall's, dB, with one thing changed at a time in");
        info ("  BOTH. The hypothesis written first: loss accumulation - the damping acts on");
        info ("  energy that is still migrating and has not been heard yet.");
        info ("");

        struct Row { std::string name; std::array<double, 7> db {}; double worst = 0.0; };

        std::vector<Row> rowsOut;

        auto compare = [&] (const std::string& name, const Candidate& c, const User& u, bool diffuserOff,
                            double from = 0.0, double to = 1.0e9)
        {
            Structure own = c.make (u);
            Structure hall;

            if (diffuserOff)
            {
                own.diffuser = DiffuserKind::none;
                hall.diffuser = DiffuserKind::none;
            }

            Row row;
            row.name = name;
            row.worst = octaveDeviation (octaveShares (own, u, from, to), octaveShares (hall, u, from, to), &row.db);

            rowsOut.push_back (row);
            return row.worst;
        };

        User plain, undamped, still;

        undamped.lowDamp = 0.0;
        undamped.highDamp = 0.0;
        still.modDepth = 0.0;

        User stillUndamped = undamped;
        stillUndamped.modDepth = 0.0;

        AmbientSpec ascending = a1b, folded = a1b;

        ascending.name = "stages ascending";
        ascending.stages = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 } };
        folded.name = "stages folded";
        folded.stages = { { 0, 7 }, { 1, 6 }, { 2, 5 }, { 3, 4 } };

        const Candidate ascendingC = candidateOf (ascending), foldedC = candidateOf (folded);

        const double asIs       = compare ("A1b as it is",                       base, plain, false);
        const double d1         = compare ("D1  damping off",                    base, undamped, false);
        compare ("D2  first 0.3 s",                    base, plain, false, 0.0, 0.3);
        compare ("D2  0.3 - 1.2 s",                    base, plain, false, 0.3, 1.2);
        compare ("D2  after 1.2 s",                    base, plain, false, 1.2);
        const double d3         = compare ("D3  diffuser off",                   base, plain, true);
        const double d3undamped = compare ("D3  diffuser off, damping off",      base, undamped, true);
        const double d5a        = compare ("D5  stages ascending",               ascendingC, plain, false);
        const double d5aU       = compare ("D5  ascending, damping off",         ascendingC, undamped, false);
        const double d5b        = compare ("D5  stages folded",                  foldedC, plain, false);
        const double d5bU       = compare ("D5  folded, damping off",            foldedC, undamped, false);
        const double d6         = compare ("D6  modulation off",                 base, still, false);
        const double d6undamped = compare ("D6  modulation off, damping off",    base, stillUndamped, false);

        info ("    " + pad ("", 34) + pad ("worst", 8) + kBandNames);

        for (const auto& r : rowsOut)
            info ("    " + pad (r.name, 34) + pad (fx (r.worst), 8) + bandsText (r.db));

        // D2: decay time per octave.
        {
            constexpr double bands[7] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

            const User u;
            const auto own  = impulseResponse (base.make (u), u, irSecondsFor (u.decay) + 1.0);
            const auto hall = impulseResponse (Structure {}, u, irSecondsFor (u.decay) + 1.0);

            std::string a = "    " + pad ("D2  RT60 per octave, A1b / Hall", 42);

            for (double hz : bands)
                a += pad (fx (mode12::rt60FromEnergy (detail::bandEnergy (own, hz, own.l.size()))
                              / mode12::rt60FromEnergy (detail::bandEnergy (hall, hz, hall.l.size())), 3), 7);

            info ("");
            info (a);
        }

        // D4: where the energy is held, by stage and band.
        {
            constexpr double bands[3] { 250.0, 1000.0, 8000.0 };

            const User u;
            const Structure s = base.make (u);
            const auto length = static_cast<size_t> (irSecondsFor (u.decay) * kRate);

            auto rig = std::make_unique<Rig> (s, u);

            std::vector<Stereo> held (4);

            for (auto& h : held)
            {
                h.l.assign (length, 0.0);
                h.r.assign (length, 0.0);
            }

            {
                ScopedFtz ftz;

                for (size_t n = 0; n < length; ++n)
                {
                    double l = n == 0 ? 1.0 : 0.0, r = l;
                    rig->processSample (l, r);

                    const double* state = rig->getLastState();

                    for (size_t stage = 0; stage < 4; ++stage)
                    {
                        held[stage].l[n] = state[2 * stage];
                        held[stage].r[n] = state[2 * stage + 1];
                    }
                }
            }

            info ("");
            info ("    D4  energy held in each stage's lines over the whole response, % of the band:");
            info ("        " + pad ("", 10) + pad ("stage 1", 10) + pad ("stage 2", 10) + pad ("stage 3", 10) + "stage 4 (heard)");

            for (double hz : bands)
            {
                double e[4] {}, total = 0.0;

                for (size_t stage = 0; stage < 4; ++stage)
                {
                    for (double v : detail::bandEnergy (held[stage], hz, length))
                        e[stage] += v;

                    total += e[stage];
                }

                std::string row = "        " + pad (fx (hz, 0) + " Hz", 10);

                for (double v : e)
                    row += pad (fx (100.0 * v / total, 1), 10);

                info (row);
            }
        }

        // D7 (addendum 2): the 125 Hz band against Size, damping off.
        {
            constexpr double sizes[5] { 0.10, 0.30, 0.50, 0.70, 0.90 };

            std::vector<std::array<double, 7>> db (5);

            runParallel (5, [&] (size_t i)
            {
                User u = undamped;
                u.size = sizes[i];

                (void) octaveDeviation (octaveShares (base.make (u), u), octaveShares (Structure {}, u), &db[i]);
            });

            std::string low = "    D7  125 Hz, damping off, Size 10 / 30 / 50 / 70 / 90:   ";
            std::string next = "        250 Hz                                            ";

            for (const auto& row : db)
            {
                low += pad ((row[0] >= 0.0 ? "+" : "") + fx (row[0]), 8);
                next += pad ((row[1] >= 0.0 ? "+" : "") + fx (row[1]), 8);
            }

            info ("");
            info (low);
            info (next);
            info ("        (the new hypothesis says the deficit shrinks as Size grows: "
                  + std::string (std::abs (db[4][0]) < std::abs (db[0][0]) - 0.3 ? "it does)" : "IT DOES NOT)"));
        }

        const bool lossAccumulation = d1 <= 0.5 * asIs;
        const bool delaysMatter = std::min (d5a, d5b) <= asIs - 0.3;

        info ("");
        info ("  READING.");
        info ("    damping off removes " + fx (100.0 * (1.0 - d1 / asIs), 0) + " % of the deviation ("
              + fx (asIs) + " -> " + fx (d1) + " dB): "
              + (lossAccumulation ? "LOSS ACCUMULATION is the larger part." : "loss accumulation is NOT the larger part."));
        info ("    diffuser off: " + fx (d3) + " dB (damping off too: " + fx (d3undamped) + ")");
        info ("    stage assignment: ascending " + fx (d5a) + " (undamped " + fx (d5aU) + "), folded " + fx (d5b)
              + " (undamped " + fx (d5bU) + "): " + (delaysMatter ? "the delay distribution MATTERS." : "little effect."));
        info ("    modulation off: " + fx (d6) + " dB (damping off too: " + fx (d6undamped) + ")");

        check (lossAccumulation, "FINDING: the hypothesis holds - damping off removes at least half of the deviation");

        // ===================================================================
        beginSuite ("PHASE 12.6.7 - Ambient: repairs");
        // ===================================================================

        info ("  Pass: octave bands within 1.0 dB of Hall (0.75 preferred) at the defaults and");
        info ("  over Size 25 / 60 / 85 x Decay 0.8 / 2.5 / 5.0 x three damping settings;");
        info ("  G2 SURVIVES; RT60 within 5 %; G3 otherwise. No output equaliser is a candidate.");
        info ("");

        std::vector<AmbientSpec> specs;

        // AT1: a power on each damping ratio, fitted at the defaults.
        if (lossAccumulation)
        {
            const User u;
            const auto hallShares = octaveShares (Structure {}, u);
            const double scale = base.make (u).decayScale;

            auto deviationFor = [&] (double low, double high, std::array<double, 7>* db = nullptr)
            {
                AmbientSpec s = a1b;
                s.gammaLow = low;
                s.gammaHigh = high;

                Structure built = ambientRaw (s) (u);
                built.decayScale = scale;

                return octaveDeviation (octaveShares (built, u), hallShares, db);
            };

            double low = 1.0, high = 1.0;

            for (int round = 0; round < 2; ++round)
            {
                for (int which = 0; which < 2; ++which)
                {
                    std::vector<double> error (41, 0.0);

                    runParallel (41, [&] (size_t i)
                    {
                        const double g = 0.05 * static_cast<double> (i);
                        error[i] = which == 0 ? deviationFor (low, g) : deviationFor (g, high);
                    });

                    const auto best = static_cast<double> (std::min_element (error.begin(), error.end()) - error.begin());

                    (which == 0 ? high : low) = 0.05 * best;
                }
            }

            std::array<double, 7> db {};
            const double fitted = deviationFor (low, high, &db);

            info ("  AT1 fitted at the defaults: low shelf power " + fx (low) + ", high shelf power " + fx (high)
                  + "  ->  " + fx (fitted) + " dB   " + bandsText (db));
            info ("  (a power below 1 damps less than the control says; above 1, more)");
            info ("");

            AmbientSpec s = a1b;
            s.name = "AT1 damping law";
            s.gammaLow = low;
            s.gammaHigh = high;
            specs.push_back (s);
        }
        else
        {
            info ("  AT1 is not built: the diagnosis does not point to loss accumulation.");
            info ("");
        }

        { AmbientSpec s = a1b; s.name = "AT2 every stage heard"; s.hear = { 0.25, 0.5, 0.75, 1.0 }; specs.push_back (s); }
        { AmbientSpec s = a1b; s.name = "AT3 25 % direct";       s.direct = 0.25;                   specs.push_back (s); }

        // Addendum 2.
        { AmbientSpec s = a1b; s.name = "AT5a 8 % direct";      s.direct = 0.08;                 specs.push_back (s); }
        { AmbientSpec s = a1b; s.name = "AT5b 12 % direct";     s.direct = 0.12;                 specs.push_back (s); }
        { AmbientSpec s = a1b; s.name = "AT5c 16 % direct";     s.direct = 0.16;                 specs.push_back (s); }
        { AmbientSpec s = a1b; s.name = "AT6 last two heard";   s.hear = { 0.0, 0.0, 0.5, 1.0 }; specs.push_back (s); }
        { AmbientSpec s = a1b; s.name = "AT7 heard stage of 4"; s.stages = { { 0, 4 }, { 1, 5 }, { 2, 3, 6, 7 } }; specs.push_back (s); }

        if (delaysMatter)
        {
            AmbientSpec s = d5a <= d5b ? ascending : folded;
            s.name = d5a <= d5b ? "AT4 stages ascending" : "AT4 stages folded";
            specs.push_back (s);
        }
        else
        {
            info ("  AT4 is not built: reassigning the stages does not lower the deviation by 0.3 dB.");
            info ("");
        }

        std::vector<Candidate> list;
        list.push_back (base);

        for (const auto& s : specs)
            list.push_back (candidateOf (s));

        std::vector<double> atDefaults (list.size(), 0.0), overGrid (list.size(), 0.0);

        for (size_t k = 0; k < list.size(); ++k)
            overGrid[k] = toneGrid (list[k], &atDefaults[k]);

        info ("    " + pad ("structure", 26) + pad ("defaults", 10) + pad ("worst of 27", 13));

        for (size_t k = 0; k < list.size(); ++k)
            info ("    " + pad (list[k].name, 26) + pad (fx (atDefaults[k]), 10) + pad (fx (overGrid[k]), 13)
                  + (overGrid[k] <= 0.75 ? "within 0.75" : overGrid[k] <= 1.0 ? "within 1.0" : "OVER 1.0"));

        std::vector<Candidate> toned;
        std::vector<AmbientSpec> tonedSpec;
        std::vector<double> tonedGrid;

        for (size_t k = 0; k < specs.size(); ++k)
            if (overGrid[k + 1] <= 1.0)
            {
                toned.push_back (list[k + 1]);
                tonedSpec.push_back (specs[k]);
                tonedGrid.push_back (overGrid[k + 1]);
            }

        Family family = familyOf ("Ambient A1b", ambientRaw (a1b), true);
        family.structural = true;
        family.status = "BLOCKED - A1b SURVIVES (Stage 5) and no repair brings its tone within 1 dB";

        if (toned.empty())
        {
            info ("");
            info ("  No repair is within 1.0 dB over the grid.");

            // The nearest miss is still worth a gate, to know what it would cost.
            size_t nearest = 1;

            for (size_t k = 2; k < list.size(); ++k)
                if (overGrid[k] < overGrid[nearest])
                    nearest = k;

            if (list.size() > 1)
            {
                info ("  The nearest, " + list[nearest].name + " (" + fx (overGrid[nearest]) + " dB), through G2 for the record:");
                info ("");

                const std::vector<Candidate> one { list[nearest] };
                stage5::printGate (one, stage5::gate (one, hallC, kSeeds));
            }

            info ("");
            info ("  AMBIENT: " + family.status);
            check (false, "FINDING: an Ambient repair is within 1 dB over the grid and passes G2 and G3");

            return family;
        }

        info ("");
        info ("  G1, then G2, for those within 1.0 dB:");
        info ("");

        const auto quick = stage5::gate (toned, hallC, 1);
        stage5::printGate (toned, quick);

        std::vector<Candidate> passed;
        std::vector<AmbientSpec> passedSpec;
        std::vector<double> passedGrid;

        for (size_t k = 0; k < toned.size(); ++k)
            if (quick[k].minimum > kReproduced)
            {
                passed.push_back (toned[k]);
                passedSpec.push_back (tonedSpec[k]);
                passedGrid.push_back (tonedGrid[k]);
            }

        int best = -1;

        if (! passed.empty())
        {
            info ("");

            const auto serious = stage5::gate (passed, hallC, kSeeds);
            stage5::printGate (passed, serious);

            stage5::qualityRisks (passed);

            info ("");
            info ("  G3, Decay limit 5 %, CPU limit 1.5 x Hall:");

            const auto g3 = stage5::semantics (passed, 0.05, 1.5);

            for (size_t k = 0; k < passed.size(); ++k)
                if (serious[k].verdict == "SURVIVES" && g3[k].pass
                    && (best < 0 || passedGrid[k] < passedGrid[static_cast<size_t> (best)]))
                    best = static_cast<int> (k);

            if (best >= 0)
            {
                const auto b = static_cast<size_t> (best);

                family = familyOf ("Ambient " + passedSpec[b].name.substr (0, passedSpec[b].name.find (' ')), ambientRaw (passedSpec[b]), true);
                family.structural = family.standing = true;
                family.status = "PASSES - A1b with " + passedSpec[b].name + ": SURVIVES, lowest median "
                              + fx (lowestMedian (serious[b])) + ", tone " + fx (passedGrid[b]) + " dB over the grid, x "
                              + fx (g3[b].cpu) + " Hall";
            }
            else
            {
                // Structurally alive but short of a gate: say which.
                for (size_t k = 0; k < passed.size(); ++k)
                    info ("    " + pad (passed[k].name, 26) + serious[k].verdict + (g3[k].pass ? ", G3 pass" : ", G3 FAIL"));
            }
        }

        info ("");
        info ("  AMBIENT: " + family.status);

        check (best >= 0, "FINDING: an Ambient repair is within 1 dB over the grid and passes G2 and G3");

        return family;
    }

    // =======================================================================
    //  Part 3 - Room against Ambient
    // =======================================================================
    bool runRoomAgainstAmbient (const Family& room, const Family& ambient)
    {
        // ===================================================================
        beginSuite ("PHASE 12.6.8 - Room against Ambient, both directions");
        // ===================================================================

        info ("  Both exist by the same rule, an exchange timed from Decay. Each is searched");
        info ("  by the other's seven controls; the impostor follows its own knobs. Three");
        info ("  points, five seeds, centred. A run <= 1.5 in either direction: not two Modes.");
        info ("");
        info ("  Room:    " + room.target.name);
        info ("  Ambient: " + ambient.target.name);
        info ("");

        bool collapsed = false, distinct = true;

        struct Direction { const Family* target; const Family* impostor; };

        for (const auto& d : { Direction { &room, &ambient }, Direction { &ambient, &room } })
        {
            info ("  TARGET " + d.target->target.name + ", searched by " + d.impostor->impostor.name + "'s controls:");
            info ("");

            std::vector<std::array<Score, kPointCount>> best;

            const std::vector<Candidate> target { d.target->target };
            const auto g = gateWith (target, d.impostor->impostor.make, kSeeds, &best);

            stage5::printGate (target, g);

            info ("");
            info ("    every family at the lowest run of each point (what is left after the search):");
            printFamilyHeader();

            for (int p = 0; p < kPointCount; ++p)
                printFamilies (kPoints[p].label, best[0][static_cast<size_t> (p)]);

            info ("");

            if (g[0].minimum <= kReproduced) collapsed = true;
            if (g[0].verdict != "SURVIVES")  distinct = false;
        }

        info (collapsed ? "  ONE REPRODUCES THE OTHER. They are not justified as two Modes."
              : distinct ? "  DISTINCT in both directions: no run <= 1.5 and every median >= 3.0."
                         : "  NOT REFUTED: no run <= 1.5, but a median is under 3.0. Not claimed as distinct.");

        check (! collapsed, "FINDING: neither of Room and Ambient reproduces the other");
        check (distinct, "FINDING: Room and Ambient are DISTINCT in both directions");

        return distinct;
    }
}

} // namespace avtest
