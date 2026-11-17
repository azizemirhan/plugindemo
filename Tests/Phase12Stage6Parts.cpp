#include "Phase12Stage6.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DspAnalysis.h"
#include "DSP/FeedbackMatrix.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>

/*
    PHASE 12 - STAGE 6. Plate's engineering family, three Chamber concepts,
    the cross-mode matrix, cost and switching; and the order of work.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.
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
    using stage5::centredWorst;
    using stage5::sameKnobs;
    using stage5::wetPathRatio;
    using stage5::kSeeds;

    namespace
    {
        volatile double gSink = 0.0;

        constexpr double kTwoPi = 6.283185307179586;

        [[nodiscard]] double lowestMedian (const Gate& g)
        {
            return *std::min_element (g.median, g.median + kPointCount);
        }

        [[nodiscard]] double hallMeanMs()
        {
            double sum = 0.0;

            for (double d : Structure {}.delaysMs)
                sum += d;

            return sum / 8.0;
        }

        // ===================================================================
        //  Plate: the chain, on its own
        // ===================================================================
        using Chain = std::function<void (double&, double&)>;

        /** ns per stereo sample, the two chains timed alternately; best of nine. */
        void timePair (const Chain& a, const Chain& b, double& nsA, double& nsB)
        {
            const auto noise = whiteNoise (static_cast<size_t> (0.5 * kRate), 0xC0FFEEu);

            nsA = nsB = 1.0e30;

            for (int round = 0; round < 9; ++round)
                for (int which = 0; which < 2; ++which)
                {
                    const auto& chain = which == 0 ? a : b;
                    double sum = 0.0;

                    ScopedFtz ftz;

                    const auto t0 = std::chrono::steady_clock::now();

                    for (double x : noise)
                    {
                        double l = x, r = -x;
                        chain (l, r);
                        sum += l + r;
                    }

                    const auto t1 = std::chrono::steady_clock::now();

                    gSink = gSink + sum;

                    const double ns = std::chrono::duration<double, std::nano> (t1 - t0).count()
                                    / static_cast<double> (noise.size());

                    (which == 0 ? nsA : nsB) = std::min (which == 0 ? nsA : nsB, ns);
                }
        }

        [[nodiscard]] std::vector<double> through (const Chain& chain, const std::vector<double>& x)
        {
            std::vector<double> y (x.size());

            ScopedFtz ftz;

            for (size_t n = 0; n < x.size(); ++n)
            {
                double l = x[n], r = x[n];
                chain (l, r);
                y[n] = l;
            }

            return y;
        }

        [[nodiscard]] Stereo both (const std::vector<double>& x)
        {
            Stereo s;
            s.l = x;
            s.r = x;
            return s;
        }

        /** Energy centroid, ms, of a response seen through one band. */
        [[nodiscard]] double bandCentroidMs (const std::vector<double>& ir, double hz)
        {
            const auto e = detail::bandEnergy (both (ir), hz, ir.size());

            double total = 0.0, moment = 0.0;

            for (size_t n = 0; n < e.size(); ++n)
            {
                total += e[n];
                moment += e[n] * static_cast<double> (n);
            }

            return 1000.0 * moment / std::max (1.0e-300, total) / kRate;
        }

        struct Click { double peakDb = 0.0, spanMs = 0.0; };

        [[nodiscard]] Click clickOf (const std::vector<double>& ir)
        {
            double peak = 0.0, total = 0.0;

            for (double v : ir)
            {
                peak = std::max (peak, std::abs (v));
                total += v * v;
            }

            double running = 0.0;
            size_t from = 0, to = ir.size() - 1;
            bool gotFrom = false;

            for (size_t n = 0; n < ir.size(); ++n)
            {
                running += ir[n] * ir[n];

                if (! gotFrom && running >= 0.05 * total) { from = n; gotFrom = true; }
                if (running >= 0.95 * total) { to = n; break; }
            }

            return { 20.0 * std::log10 (std::max (1.0e-30, peak)), 1000.0 * static_cast<double> (to - from) / kRate };
        }

        /** What is left of a sine after the sine itself is taken out, dB. */
        [[nodiscard]] double aliasDb (const Chain& chain, double hz)
        {
            const auto length = static_cast<size_t> (0.5 * kRate);
            std::vector<double> x (length);

            for (size_t n = 0; n < length; ++n)
                x[n] = std::sin (kTwoPi * hz * static_cast<double> (n) / kRate);

            const auto y = through (chain, x);

            const auto from = static_cast<size_t> (0.2 * kRate);

            double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0, yy = 0.0;

            for (size_t n = from; n < length; ++n)
            {
                const double s = std::sin (kTwoPi * hz * static_cast<double> (n) / kRate);
                const double c = std::cos (kTwoPi * hz * static_cast<double> (n) / kRate);

                ss += s * s; cc += c * c; sc += s * c;
                ys += y[n] * s; yc += y[n] * c; yy += y[n] * y[n];
            }

            const double det = ss * cc - sc * sc;
            const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;

            double residual = 0.0;

            for (size_t n = from; n < length; ++n)
            {
                const double fit = a * std::sin (kTwoPi * hz * static_cast<double> (n) / kRate)
                                 + b * std::cos (kTwoPi * hz * static_cast<double> (n) / kRate);

                residual += (y[n] - fit) * (y[n] - fit);
            }

            return 10.0 * std::log10 (std::max (1.0e-30, residual) / std::max (1.0e-30, yy));
        }

        /** Largest minus smallest of 24 band gains through a chain, dB. */
        [[nodiscard]] double rippleDb (const Chain& chain)
        {
            const auto x = whiteNoise (static_cast<size_t> (2.0 * kRate), 0x5EEDu);
            const auto y = through (chain, x);

            double low = 1.0e30, high = -1.0e30;

            for (int b = 0; b < 24; ++b)
            {
                const double hz = 80.0 * std::pow (16000.0 / 80.0, b / 23.0);

                double in = 0.0, out = 0.0;

                for (double v : detail::bandEnergy (both (x), hz, x.size())) in += v;
                for (double v : detail::bandEnergy (both (y), hz, y.size())) out += v;

                const double db = 10.0 * std::log10 (out / in);

                low = std::min (low, db);
                high = std::max (high, db);
            }

            return high - low;
        }

        [[nodiscard]] Structure plateReference()
        {
            Structure s;
            s.name = "Plate A (reference)";
            s.preDispersion = 240;
            return s;
        }

        [[nodiscard]] Structure platePipelined (const std::string& name, int split, int form)
        {
            Structure s;
            s.name = name;
            s.preChain.assign (240, ChainSection { -0.7, 1 });
            s.preChainSplit = split;
            s.preChainVector = form == 1;
            s.preChainRegisters = form == 2;
            return s;
        }
    }

    std::shared_ptr<const MultirateSpec> multirateDesign (int bands)
    {
        auto target = [] (double hz) { return 240.0 * sectionDelay (-0.7, kTwoPi * hz / kRate); };

        // Addendum 3: the fewest sections within 0.25 ms rms of the reference's curve.
        const double tolerance = 0.25e-3 * kRate;

        if (bands == 2)
        {
            auto spec = std::make_shared<MultirateSpec>();

            spec->divide = 4;
            spec->low  = fitBand (target, kRate, 4, 20.0, 4500.0, tolerance);
            spec->high = fitBand (target, kRate, 1, 3000.0, 22000.0, tolerance);

            return spec;
        }

        auto inner = std::make_shared<MultirateSpec>();

        inner->divide = 4;
        inner->low  = fitBand (target, kRate, 8, 20.0, 2250.0, tolerance);
        inner->high = fitBand (target, kRate, 2, 1500.0, 9000.0, tolerance);

        auto outer = std::make_shared<MultirateSpec>();

        outer->divide = 2;
        outer->high = fitBand (target, kRate, 1, 6000.0, 22000.0, tolerance);
        outer->inner = inner;

        return outer;
    }

    Structure standingPlateStructure() { return plateReference(); }

    // =======================================================================
    //  Part 4 - Plate
    // =======================================================================
    Family runPlate()
    {
        const Candidate hallC = fixed ("Hall", Structure {});
        const Structure reference = plateReference();

        // ===================================================================
        beginSuite ("PHASE 12.6.9 - Plate: the dispersion chain on its own");
        // ===================================================================

        info ("  The reference is 240 first-order sections at -0.7 per channel. The");
        info ("  hypothesis: it is bound by latency (each section waits for the one before),");
        info ("  not by arithmetic. Timed alternately with the reference, stereo, best of nine.");
        info ("");

        const std::vector<double> coefficients (240, -0.7);

        auto plain = [] ()
        {
            auto l = std::make_shared<Dispersion>(), r = std::make_shared<Dispersion>();

            l->prepare (240, -0.7);
            r->prepare (240, -0.7);

            return Chain ([l, r] (double& a, double& b) { a = l->process (a); b = r->process (b); });
        };

        // form: 0 scalar, 1 two-wide through memory, 2 two-wide in registers (PV3)
        auto pipelined = [&] (int split, int form)
        {
            auto p = std::make_shared<PipelinedChain>();
            p->prepare (coefficients, split, form == 1, form == 2);

            return Chain ([p] (double& a, double& b) { p->process (a, b); });
        };

        auto multirate = [] (int bands, bool bypass)
        {
            auto l = std::make_shared<Multirate>(), r = std::make_shared<Multirate>();
            const auto spec = multirateDesign (bands);

            l->prepare (*spec);
            r->prepare (*spec);
            l->setBypass (bypass);
            r->setBypass (bypass);

            return Chain ([l, r] (double& a, double& b) { a = l->process (a); b = r->process (b); });
        };

        struct Timed { std::string name; Chain chain; int split, form; };

        std::vector<Timed> timed;

        constexpr const char* formName[3] { "PV1 scalar", "PV2 two-wide", "PV3 in registers" };

        for (int form = 0; form < 3; ++form)
            for (int split : { 1, 4, 8, 16 })
                if (form < 2 || split > 1)
                    timed.push_back ({ std::string (formName[form]) + ", " + std::to_string (split) + " sub-chains",
                                       pipelined (split, form), split, form });

        const size_t pipelinedCount = timed.size();

        timed.push_back ({ "PM1 multirate, two bands",   multirate (2, false), 0, 0 });
        timed.push_back ({ "PM2 multirate, three bands", multirate (3, false), 0, 0 });

        info ("    " + pad ("chain", 34) + pad ("ns / sample", 13) + pad ("reference", 11) + "reference / chain");

        double bestSpeed = 0.0;
        int bestSplit = 1, bestForm = 0;

        for (size_t k = 0; k < timed.size(); ++k)
        {
            double nsReference = 0.0, ns = 0.0;
            timePair (plain(), timed[k].chain, nsReference, ns);

            info ("    " + pad (timed[k].name, 34) + pad (fx (ns, 1), 13) + pad (fx (nsReference, 1), 11)
                  + fx (nsReference / ns) + " x");

            if (k < pipelinedCount && nsReference / ns > bestSpeed)
            {
                bestSpeed = nsReference / ns;
                bestSplit = timed[k].split;
                bestForm = timed[k].form;
            }
        }

        const std::string bestName = std::string (formName[bestForm]) + ", " + std::to_string (bestSplit);

        info ("");
        info ("  Fastest: " + bestName + " sub-chains, " + fx (bestSpeed) + " x the reference chain.");

        check (bestSpeed >= 3.0, "the prediction: rearranged, the same arithmetic is at least 3 x cheaper");
        check (bestSpeed >= 6.0, "addendum 3's prediction: kept in registers, at least 6 x cheaper");

        // --- the transfer function ------------------------------------------------
        {
            const auto noise = whiteNoise (8192, 0xABCDu);
            const auto ref = through (plain(), noise);

            info ("");
            info ("  The same transfer function? Largest difference from the reference on noise,");
            info ("  the pipelined output read (sub-chains - 1) samples later:");

            double worstAll = 0.0;

            for (int vector = 0; vector < 3; ++vector)
                for (int split : { 1, 4, 8, 16 })
                {
                    const auto y = through (pipelined (split, vector), noise);

                    double worst = 0.0;

                    for (size_t n = 0; n + static_cast<size_t> (split - 1) < noise.size(); ++n)
                        worst = std::max (worst, std::abs (y[n + static_cast<size_t> (split - 1)] - ref[n]));

                    worstAll = std::max (worstAll, worst);

                    info ("    " + pad (std::string (formName[vector]) + ", " + std::to_string (split), 24)
                          + fx (worst * 1.0e12, 3) + " e-12");
                }

            check (worstAll < 1.0e-9, "the pipelined chain is the reference chain, delayed");
        }

        // --- multirate ---------------------------------------------------------------
        info ("");
        info ("  MULTIRATE. Each band's sections run at the band's rate and are fitted to the");
        info ("  reference's delay curve there. 'equivalents' weighs a section by its rate.");
        info ("");

        for (int bands : { 2, 3 })
        {
            const auto spec = multirateDesign (bands);

            Multirate probe;
            probe.prepare (*spec);

            auto line = [] (const std::string& what, const BandFit& f, int divide)
            {
                info ("      " + pad (what, 26) + pad (std::to_string (f.sections) + " sections @ " + fx (f.a), 24)
                      + pad ("rate / " + std::to_string (divide), 12) + "fit error " + fx (f.rmsError * 1000.0 / kRate, 2) + " ms rms");
            };

            info ("    PM" + std::to_string (bands - 1) + ":");

            if (bands == 2)
            {
                line ("below 3 kHz", spec->low, 4);
                line ("above 3 kHz", spec->high, 1);
            }
            else
            {
                line ("below 1.5 kHz", spec->inner->low, 8);
                line ("1.5 - 6 kHz", spec->inner->high, 2);
                line ("above 6 kHz", spec->high, 1);
            }

            info ("      section equivalents " + fx (probe.sectionEquivalents(), 1) + " (reference 240), filter multiplies "
                  + fx (probe.filterMultiplies(), 1) + " per sample, latency beyond the curve "
                  + fx (probe.latency * 1000.0 / kRate, 2) + " ms");

            // Reconstruction with the chains out.
            {
                const auto noise = whiteNoise (16384, 0x1234u);
                const auto y = through (multirate (bands, true), noise);

                // The filter bank's own delay, found as the lag of best match.
                size_t lag = 0;
                double bestError = 1.0e30;

                for (size_t d = 0; d < 400; ++d)
                {
                    double e = 0.0;

                    for (size_t n = 2000; n < 6000; ++n)
                        e += (y[n + d] - noise[n]) * (y[n + d] - noise[n]);

                    if (e < bestError) { bestError = e; lag = d; }
                }

                double energy = 0.0;

                for (size_t n = 2000; n < 6000; ++n)
                    energy += noise[n] * noise[n];

                info ("      reconstruction with the chains out: " + fx (10.0 * std::log10 (std::max (1.0e-30, bestError) / energy), 0)
                      + " dB, at " + std::to_string (lag) + " samples of delay");
            }

            info ("      magnitude ripple with the chains in: " + fx (rippleDb (multirate (bands, false))) + " dB over 24 bands"
                  + "  (the reference: " + fx (rippleDb (plain())) + ")");

            std::string alias = "      alias and image level on a sine, dB:";

            for (double hz : { 1000.0, 2500.0, 4000.0, 5500.0, 7000.0, 10000.0 })
                alias += "  " + fx (hz / 1000.0, 1) + "k " + fx (aliasDb (multirate (bands, false), hz), 0);

            info (alias);
            info ("");
        }

        // --- delay curve and click ---------------------------------------------------
        {
            std::vector<double> impulse (16384, 0.0);
            impulse[0] = 1.0;

            struct Row { std::string name; std::vector<double> ir; };

            std::vector<Row> rows;

            rows.push_back ({ "reference", through (plain(), impulse) });
            rows.push_back ({ bestName, through (pipelined (bestSplit, bestForm), impulse) });
            rows.push_back ({ "PM1", through (multirate (2, false), impulse) });
            rows.push_back ({ "PM2", through (multirate (3, false), impulse) });

            info ("  Delay by band (energy centroid of the chain's own response, ms) and the click:");
            info ("    " + pad ("", 22) + pad ("125", 8) + pad ("250", 8) + pad ("500", 8) + pad ("1k", 8) + pad ("2k", 8)
                  + pad ("4k", 8) + pad ("8k", 8) + pad ("12k", 8) + pad ("peak dB", 9) + "5-95 % span, ms");

            for (const auto& r : rows)
            {
                std::string row = "    " + pad (r.name, 22);

                for (double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0 })
                    row += pad (fx (bandCentroidMs (r.ir, hz), 1), 8);

                const auto c = clickOf (r.ir);

                info (row + pad (fx (c.peakDb, 1), 9) + fx (c.spanMs, 1));
            }
        }

        // ===================================================================
        beginSuite ("PHASE 12.6.10 - Plate: every candidate against the reference and against Hall");
        // ===================================================================

        info ("  At the same knobs, three points, centred: the score against the reference");
        info ("  (near is good - within 1.5 is PRESERVED) and against Hall (far is good).");
        info ("  Cost is the whole wet path against Hall, measured alternately.");
        info ("");

        struct Entry { std::string name; Structure structure; double vsRef[3] {}, vsHall[3] {}; double cpu = 0.0; bool preserved = false; };

        std::vector<Entry> entries;

        auto add = [&] (const Structure& s) { Entry e; e.name = s.name; e.structure = s; entries.push_back (e); };

        add (platePipelined (bestName, bestSplit, bestForm));

        // PV1 as the first run measured it, beside whatever is fastest now.
        if (bestForm != 0 || bestSplit != 8)
            add (platePipelined ("PV1 scalar, 8", 8, 0));

        { Structure s; s.name = "PM1 multirate 2 bands"; s.multirate = multirateDesign (2); add (s); }
        { Structure s; s.name = "PM2 multirate 3 bands"; s.multirate = multirateDesign (3); add (s); }

        for (int n : { 2, 4, 8 })
        {
            Structure s;
            s.name = "PT1 loop " + std::to_string (n);
            s.loopDispersion = n;
            s.dispersionCoeff = -0.7;
            add (s);
        }

        constexpr std::pair<int, int> hybrids[3] { { 96, 2 }, { 64, 2 }, { 48, 4 } };

        for (const auto& hybrid : hybrids)
        {
            Structure s;
            s.name = "PT2 " + std::to_string (hybrid.first) + " ahead + loop " + std::to_string (hybrid.second);
            s.preChain.assign (static_cast<size_t> (hybrid.first), ChainSection { -0.7, 1 });
            s.preChainSplit = 8;
            s.loopDispersion = hybrid.second;
            s.dispersionCoeff = -0.7;
            add (s);
        }

        const size_t rows = entries.size() + 2;
        std::vector<Features> features (rows * kPointCount);

        runParallel (features.size(), [&] (size_t i)
        {
            const size_t k = i / kPointCount;
            const User u = userAt (kPoints[i % kPointCount]);

            features[i] = featuresOf (k == 0 ? Structure {} : k == 1 ? reference : entries[k - 2].structure, u);
        });

        double referenceVsHall[3] {};

        for (int p = 0; p < kPointCount; ++p)
            referenceVsHall[p] = centredWorst (sameKnobs (features[static_cast<size_t> (p)],
                                                          features[kPointCount + static_cast<size_t> (p)]));

        const double referenceCpu = wetPathRatio (reference);

        for (size_t k = 0; k < entries.size(); ++k)
        {
            auto& e = entries[k];

            e.preserved = true;

            for (int p = 0; p < kPointCount; ++p)
            {
                const auto& f = features[(k + 2) * kPointCount + static_cast<size_t> (p)];

                e.vsHall[p] = centredWorst (sameKnobs (features[static_cast<size_t> (p)], f));
                e.vsRef[p]  = centredWorst (sameKnobs (features[kPointCount + static_cast<size_t> (p)], f));

                if (e.vsRef[p] > kReproduced)
                    e.preserved = false;
            }

            e.cpu = wetPathRatio (e.structure);
        }

        info ("    " + pad ("candidate", 30) + pad ("vs reference  med sma lar", 28) + pad ("vs Hall  med sma lar", 24)
              + pad ("x Hall", 8));
        info ("    " + pad ("Plate A (reference)", 30) + pad ("0.00 0.00 0.00", 28)
              + pad (fx (referenceVsHall[0]) + " " + fx (referenceVsHall[1]) + " " + fx (referenceVsHall[2]), 24)
              + fx (referenceCpu));

        for (const auto& e : entries)
            info ("    " + pad (e.name, 30)
                  + pad (fx (e.vsRef[0]) + " " + fx (e.vsRef[1]) + " " + fx (e.vsRef[2]), 28)
                  + pad (fx (e.vsHall[0]) + " " + fx (e.vsHall[1]) + " " + fx (e.vsHall[2]), 24)
                  + pad (fx (e.cpu), 8) + (e.preserved ? "PRESERVED" : "a different response")
                  + (e.cpu <= 1.5 ? ", <= 1.5" : e.cpu <= 1.75 ? ", <= 1.75" : e.cpu <= 2.0 ? ", <= 2.0" : ", OVER 2.0"));

        // --- Hall's search ----------------------------------------------------------------
        info ("");
        info ("  Hall's search. PRESERVED candidates go straight to G2; the others are a");
        info ("  different Plate and must pass G1 first. Reference-equivalent is Stage 5's rule:");
        info ("  PRESERVED, no run <= 1.5, and no median below the reference's own ten-seed");
        info ("  minimum at that point (2.95, 2.54, 2.78).");
        info ("");

        std::vector<size_t> toG2;
        std::vector<Candidate> different;
        std::vector<size_t> differentIndex;

        for (size_t k = 0; k < entries.size(); ++k)
        {
            if (entries[k].cpu > 2.0)
                continue;

            if (entries[k].preserved)
                toG2.push_back (k);
            else
            {
                different.push_back (fixed (entries[k].name, entries[k].structure));
                differentIndex.push_back (k);
            }
        }

        if (! different.empty())
        {
            const auto quick = stage5::gate (different, hallC, 1);
            stage5::printGate (different, quick);
            info ("");

            for (size_t k = 0; k < different.size(); ++k)
                if (quick[k].minimum > kReproduced)
                    toG2.push_back (differentIndex[k]);
        }

        std::sort (toG2.begin(), toG2.end());

        constexpr double referenceMinimum[kPointCount] { 2.95, 2.54, 2.78 };

        int best = -1, bestDifferent = -1;
        std::vector<Gate> serious;

        if (! toG2.empty())
        {
            std::vector<Candidate> targets;

            for (size_t k : toG2)
                targets.push_back (fixed (entries[k].name, entries[k].structure));

            serious = stage5::gate (targets, hallC, kSeeds);
            stage5::printGate (targets, serious);

            info ("");

            for (size_t i = 0; i < toG2.size(); ++i)
            {
                const auto& e = entries[toG2[i]];

                bool equivalent = e.preserved && serious[i].minimum > kReproduced;

                for (int p = 0; p < kPointCount; ++p)
                    if (serious[i].median[p] < referenceMinimum[p])
                        equivalent = false;

                info ("    " + pad (e.name, 30) + (equivalent ? "REFERENCE-EQUIVALENT" : e.preserved ? "preserved, but NOT reference-equivalent"
                                                                                            : "a different Plate: " + serious[i].verdict)
                      + ", x " + fx (e.cpu) + " Hall");

                if (equivalent && (best < 0 || e.cpu < entries[toG2[static_cast<size_t> (best)]].cpu))
                    best = static_cast<int> (i);

                if (! e.preserved && serious[i].verdict != "REJECTED"
                    && (bestDifferent < 0 || e.cpu < entries[toG2[static_cast<size_t> (bestDifferent)]].cpu))
                    bestDifferent = static_cast<int> (i);
            }
        }

        Family family;

        family.name = "Plate";
        family.raw = [reference] (const User&) { return reference; };
        family.target = family.impostor = fixed ("Plate A (reference)", reference);
        family.structural = true;
        family.status = "NO ENGINEERING CANDIDATE - the 240-section reference stands at x " + fx (referenceCpu) + " Hall";

        const int chosen = best >= 0 ? best : bestDifferent;

        if (chosen >= 0)
        {
            const auto& e = entries[toG2[static_cast<size_t> (chosen)]];
            const Structure s = e.structure;

            info ("");
            info ("  G3 for " + e.name + " and the reference (CPU limit 1.75 x Hall):");

            const auto g3 = stage5::semantics ({ fixed ("Plate A (reference)", reference), fixed (e.name, s) }, 0.10, 1.75);

            stage5::centredSources ({ fixed ("Plate A (reference)", reference), fixed (e.name, s) });

            family.raw = [s] (const User&) { return s; };
            family.target = family.impostor = fixed (e.name, s);
            family.standing = g3[1].pass;

            family.status = std::string (best >= 0 ? "REFERENCE-EQUIVALENT - " : "A DIFFERENT PLATE - ") + e.name + ": "
                          + serious[static_cast<size_t> (chosen)].verdict + ", lowest median "
                          + fx (lowestMedian (serious[static_cast<size_t> (chosen)])) + ", x " + fx (g3[1].cpu) + " Hall"
                          + (g3[1].pass ? ", G3 pass" : ", G3 FAIL");
        }

        info ("");
        info ("  PLATE: " + family.status);

        check (best >= 0, "FINDING: a Plate candidate is reference-equivalent");
        check (family.standing, "FINDING: a Plate candidate passes G3 at <= 1.75 x Hall");

        return family;
    }

    // =======================================================================
    //  Part 5 - Chamber
    // =======================================================================
    std::shared_ptr<const OpenSpec> openHallSpec()
    {
        auto spec = std::make_shared<OpenSpec>();

        spec->lines = 8;

        for (double d : Structure {}.delaysMs)
            spec->delayMs.push_back (d);

        FeedbackMatrix<8> hadamard;
        double m[64];
        hadamard.getCoefficients (m);

        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                spec->matrix.push_back ({ r, c, m[r * 8 + c] });

        for (int i = 0; i < 8; ++i)
        {
            spec->feeds.push_back ({ i, 0.0, fdn::injectionLeft[i] * fdn::signVectorNorm,
                                     fdn::injectionRight[i] * fdn::signVectorNorm });
            spec->reads.push_back ({ i, 1.0, 0.0, fdn::readoutLeft[i] * fdn::signVectorNorm,
                                     fdn::readoutRight[i] * fdn::signVectorNorm });
        }

        return spec;
    }

    namespace
    {
        /** K1: wall nodes, a directed line for every ordered pair of walls. */
        [[nodiscard]] std::shared_ptr<const OpenSpec> wallNetwork (int nodes)
        {
            const double mean = hallMeanMs();

            constexpr double pair4[6]  { 0.55, 0.70, 0.82, 0.97, 1.13, 1.31 };
            constexpr double pair5[10] { 0.48, 0.57, 0.66, 0.74, 0.83, 0.93, 1.04, 1.16, 1.29, 1.43 };

            constexpr double sourceL[5] { 0.21, 0.34, 0.47, 0.62, 0.55 };
            constexpr double sourceR[5] { 0.29, 0.52, 0.18, 0.41, 0.37 };
            constexpr double hearL[5]   { 0.58, 0.26, 0.44, 0.31, 0.39 };
            constexpr double hearR[5]   { 0.29, 0.52, 0.35, 0.49, 0.61 };
            constexpr double signL[5]   { 1, -1, 1, -1, 1 };
            constexpr double signR[5]   { 1, 1, -1, -1, 1 };

            auto spec = std::make_shared<OpenSpec>();

            auto line = [nodes] (int from, int to) { return from * (nodes - 1) + (to < from ? to : to - 1); };

            spec->lines = nodes * (nodes - 1);
            spec->delayMs.assign (static_cast<size_t> (spec->lines), 0.0);

            int pair = 0;

            for (int a = 0; a < nodes; ++a)
                for (int b = a + 1; b < nodes; ++b)
                {
                    const double ms = mean * (nodes == 4 ? pair4[pair] : pair5[pair]);

                    spec->delayMs[static_cast<size_t> (line (a, b))] = ms;
                    spec->delayMs[static_cast<size_t> (line (b, a))] = ms;
                    ++pair;
                }

            // What arrives at b from a leaves b toward c.
            for (int b = 0; b < nodes; ++b)
                for (int a = 0; a < nodes; ++a)
                    for (int c = 0; c < nodes; ++c)
                        if (a != b && c != b)
                            spec->matrix.push_back ({ line (b, c), line (a, b),
                                                      2.0 / (nodes - 1) - (a == c ? 1.0 : 0.0) });

            const double norm = 1.0 / std::sqrt (static_cast<double> (spec->lines));

            for (int b = 0; b < nodes; ++b)
                for (int c = 0; c < nodes; ++c)
                    if (c != b)
                    {
                        // Each source reaches wall b after its own delay, and
                        // is sent toward every other wall.
                        spec->feeds.push_back ({ line (b, c), mean * sourceL[b], norm, 0.0 });
                        spec->feeds.push_back ({ line (b, c), mean * sourceR[b], 0.0, norm });

                        // Each ear hears what arrives at wall b, after its own delay.
                        spec->reads.push_back ({ line (c, b), 1.0, mean * hearL[b], signL[b] * norm, 0.0 });
                        spec->reads.push_back ({ line (c, b), 1.0, mean * hearR[b], 0.0, signR[b] * norm });
                    }

            return spec;
        }

        /** K2: one ring of segments, each behind a nested allpass. */
        [[nodiscard]] std::shared_ptr<const OpenSpec> nestedRing (int segments)
        {
            const double mean = hallMeanMs();

            constexpr double length[6] { 0.83, 1.09, 0.71, 1.27, 0.93, 1.17 };

            auto spec = std::make_shared<OpenSpec>();

            spec->lines = segments;

            for (int i = 0; i < segments; ++i)
            {
                spec->delayMs.push_back (mean * length[i]);
                spec->matrix.push_back ({ (i + 1) % segments, i, 1.0 });
                spec->allpass.push_back ({ { 0.31, 0.55, 0.11, 0.45 } });
            }

            spec->feeds.push_back ({ 0, 0.0, 1.0, 0.0 });
            spec->feeds.push_back ({ segments / 2, 0.0, 0.0, 1.0 });

            if (segments == 4)
            {
                const double w = 1.0 / std::sqrt (3.0);

                spec->reads.push_back ({ 1, 0.29, 0.0,  w, 0.0 });
                spec->reads.push_back ({ 2, 0.63, 0.0, -w, 0.0 });
                spec->reads.push_back ({ 3, 0.41, 0.0,  w, 0.0 });
                spec->reads.push_back ({ 3, 0.27, 0.0, 0.0,  w });
                spec->reads.push_back ({ 0, 0.59, 0.0, 0.0, -w });
                spec->reads.push_back ({ 1, 0.43, 0.0, 0.0,  w });
            }
            else
            {
                const double w = 0.5;

                spec->reads.push_back ({ 1, 0.29, 0.0,  w, 0.0 });
                spec->reads.push_back ({ 2, 0.63, 0.0, -w, 0.0 });
                spec->reads.push_back ({ 4, 0.41, 0.0,  w, 0.0 });
                spec->reads.push_back ({ 5, 0.52, 0.0, -w, 0.0 });
                spec->reads.push_back ({ 4, 0.27, 0.0, 0.0,  w });
                spec->reads.push_back ({ 5, 0.59, 0.0, 0.0, -w });
                spec->reads.push_back ({ 1, 0.43, 0.0, 0.0,  w });
                spec->reads.push_back ({ 2, 0.36, 0.0, 0.0, -w });
            }

            return spec;
        }

        /** K3: 3 x 3 junctions, links between neighbours. */
        [[nodiscard]] std::shared_ptr<const OpenSpec> lattice (bool variantB)
        {
            const double mean = hallMeanMs();

            constexpr double length[12] { 0.38, 0.44, 0.49, 0.53, 0.58, 0.63, 0.41, 0.47, 0.52, 0.56, 0.61, 0.67 };

            struct Link { int a, b; };

            std::vector<Link> links;

            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 2; ++c)
                    links.push_back ({ 3 * r + c, 3 * r + c + 1 });

            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 3; ++c)
                    links.push_back ({ 3 * r + c, 3 * (r + 1) + c });

            auto spec = std::make_shared<OpenSpec>();

            spec->lines = 24;

            // Line 2 k runs a -> b along link k; line 2 k + 1 runs b -> a.
            struct Directed { int from, to; };

            std::vector<Directed> directed;

            for (size_t k = 0; k < links.size(); ++k)
            {
                directed.push_back ({ links[k].a, links[k].b });
                directed.push_back ({ links[k].b, links[k].a });

                spec->delayMs.push_back (mean * length[k]);
                spec->delayMs.push_back (mean * length[k]);
            }

            int degree[9] {};

            for (const auto& d : directed)
                ++degree[d.from];

            for (int in = 0; in < 24; ++in)
                for (int out = 0; out < 24; ++out)
                    if (directed[static_cast<size_t> (in)].to == directed[static_cast<size_t> (out)].from)
                    {
                        const int junction = directed[static_cast<size_t> (in)].to;
                        const bool back = directed[static_cast<size_t> (out)].to == directed[static_cast<size_t> (in)].from;

                        spec->matrix.push_back ({ out, in, 2.0 / degree[junction] - (back ? 1.0 : 0.0) });
                    }

            auto feed = [&] (int junction, bool left)
            {
                const double g = 1.0 / std::sqrt (static_cast<double> (degree[junction]));

                for (int k = 0; k < 24; ++k)
                    if (directed[static_cast<size_t> (k)].from == junction)
                        spec->feeds.push_back ({ k, 0.0, left ? g : 0.0, left ? 0.0 : g });
            };

            // Hears what arrives at a junction, with alternating signs.
            auto hear = [&] (int junction, bool left, double weight, double firstSign)
            {
                double sign = firstSign;

                for (int k = 0; k < 24; ++k)
                    if (directed[static_cast<size_t> (k)].to == junction)
                    {
                        spec->reads.push_back ({ k, 1.0, 0.0, left ? sign * weight : 0.0, left ? 0.0 : sign * weight });
                        sign = -sign;
                    }
            };

            if (! variantB)
            {
                // In at two corners of the top edge, heard along the bottom edge.
                feed (0, true);
                feed (2, false);

                const double w = 1.0 / std::sqrt (5.0);

                hear (6, true,  w,  1.0);
                hear (7, true,  w,  1.0);
                hear (8, false, w,  1.0);
                hear (7, false, w, -1.0);
            }
            else
            {
                // In at the middle of the left and right edges, heard at the corners.
                feed (3, true);
                feed (5, false);

                const double w = 0.5;

                hear (0, true,  w,  1.0);
                hear (8, true,  w,  1.0);
                hear (2, false, w,  1.0);
                hear (6, false, w,  1.0);
            }

            return spec;
        }

        [[nodiscard]] Candidate openCandidate (const std::string& name, const std::shared_ptr<const OpenSpec>& spec,
                                               bool calibrated)
        {
            Structure s;
            s.name = name;
            s.open = spec;

            return calibrated ? levelled (decayCalibrated (fixed (name, s))) : fixed (name, s);
        }
    }

    std::vector<std::pair<std::string, std::shared_ptr<const OpenSpec>>> chamberSpecs()
    {
        return
        {
            { "K1a wall network, 4 walls",  wallNetwork (4) },
            { "K1b wall network, 5 walls",  wallNetwork (5) },
            { "K2a nested ring, 4",         nestedRing (4) },
            { "K2b nested ring, 6",         nestedRing (6) },
            { "K3a lattice, edge to edge",  lattice (false) },
            { "K3b lattice, to corners",    lattice (true) }
        };
    }

    Family runChamber()
    {
        const Candidate hallC = fixed ("Hall", Structure {});

        // ===================================================================
        beginSuite ("PHASE 12.6.11 - Chamber: the open tank's control");
        // ===================================================================

        info ("  Hall's own lines, matrix and vectors, built in the open tank. Hall's search");
        info ("  must REPRODUCE it (median <= 1.5 at every point); otherwise the open tank");
        info ("  differs from the research core in something that is not structure.");
        info ("");

        const Candidate control = openCandidate ("Hall in the open tank", openHallSpec(), false);

        {
            const User u;
            const auto a = impulseResponse (Structure {}, u, 1.0);
            const auto b = impulseResponse (control.make (u), u, 1.0);

            double worst = 0.0, peak = 0.0;

            for (size_t n = 0; n < a.l.size(); ++n)
            {
                worst = std::max (worst, std::max (std::abs (a.l[n] - b.l[n]), std::abs (a.r[n] - b.r[n])));
                peak = std::max (peak, std::max (std::abs (a.l[n]), std::abs (a.r[n])));
            }

            info ("  Sample by sample against Hall, first second: largest difference "
                  + fx (20.0 * std::log10 (std::max (1.0e-30, worst) / peak), 0) + " dB re peak.");
            info ("");
        }

        const std::vector<Candidate> controlList { control };
        const auto controlGate = stage5::gate (controlList, hallC, kSeeds);

        stage5::printGate (controlList, controlGate);

        bool controlHolds = true;

        for (int p = 0; p < kPointCount; ++p)
            if (controlGate[0].median[p] > kReproduced)
                controlHolds = false;

        info ("");
        info (controlHolds ? "  The control holds: the open tank, built as Hall, is Hall to the metric."
                           : "  THE CONTROL FAILS. Results from the open tank are CONFOUNDED.");

        check (controlHolds, "the open tank built as Hall is reproduced by Hall");

        // ===================================================================
        beginSuite ("PHASE 12.6.12 - Chamber: three concepts");
        // ===================================================================

        info ("  K1 a scattering delay network; K2 nested recursive diffusion in one loop;");
        info ("  K3 a scattering lattice. Two variants each, fixed in the pre-registration.");
        info ("");

        const auto specs = chamberSpecs();

        std::vector<Candidate> candidates;

        for (const auto& s : specs)
        {
            info ("    " + pad (s.first, 30) + pad (std::to_string (s.second->lines) + " lines", 10)
                  + "orthogonality error " + fx (s.second->orthogonalityError() * 1.0e12, 3) + " e-12");

            candidates.push_back (openCandidate (s.first, s.second, true));
        }

        info ("");
        info ("  G1 - one search per point, centred view:");
        info ("");

        const auto quick = stage5::gate (candidates, hallC, 1);
        stage5::printGate (candidates, quick);

        std::vector<Candidate> passed;

        for (size_t k = 0; k < candidates.size(); ++k)
            if (quick[k].minimum > kReproduced)
                passed.push_back (candidates[k]);

        info ("");
        info ("  " + std::to_string (passed.size()) + " of " + std::to_string (candidates.size()) + " pass G1.");

        Family family;

        family.name = "Chamber";
        family.status = "NO MECHANISM - every Chamber concept was reproduced by Hall";

        if (passed.empty())
        {
            info ("");
            info ("  CHAMBER: " + family.status);
            check (false, "FINDING: a Chamber concept passes G1, G2 and G3");
            return family;
        }

        info ("");
        info ("  G2 - five seeds per point, centred view:");
        info ("");

        const auto serious = stage5::gate (passed, hallC, kSeeds);
        stage5::printGate (passed, serious);

        std::vector<Candidate> alive;
        std::vector<Gate> aliveGate;

        for (size_t k = 0; k < passed.size(); ++k)
            if (serious[k].verdict != "REJECTED")
            {
                alive.push_back (passed[k]);
                aliveGate.push_back (serious[k]);
            }

        stage5::qualityRisks (passed);
        stage5::centredSources (passed);

        if (alive.empty())
        {
            family.status = "NO MECHANISM - every Chamber concept that passed G1 was REJECTED at G2";

            info ("");
            info ("  CHAMBER: " + family.status);
            check (false, "FINDING: a Chamber concept passes G1, G2 and G3");
            return family;
        }

        info ("");
        info ("  G3, CPU limit 1.5 x Hall:");

        const auto g3 = stage5::semantics (alive, 0.10, 1.5);

        // The strongest structurally; among those passing G3 if any does.
        int best = -1;
        bool bestPasses = false;

        for (size_t k = 0; k < alive.size(); ++k)
        {
            const bool better = best < 0
                || (g3[k].pass && ! bestPasses)
                || (g3[k].pass == bestPasses
                    && lowestMedian (aliveGate[k]) > lowestMedian (aliveGate[static_cast<size_t> (best)]));

            if (better)
            {
                best = static_cast<int> (k);
                bestPasses = g3[k].pass;
            }
        }

        const auto b = static_cast<size_t> (best);

        // Rebuild the chosen one as a family, from its spec.
        for (const auto& s : specs)
            if (s.first == alive[b].name)
            {
                Structure structure;
                structure.name = s.first;
                structure.open = s.second;

                family = familyOf (s.first, [structure] (const User&) { return structure; }, true);
            }

        family.structural = true;
        family.standing = bestPasses && controlHolds && aliveGate[b].verdict == "SURVIVES";

        family.status = std::string (! controlHolds ? "CONFOUNDED - " : family.standing ? "PASSES Hall's gates - "
                                                                      : "BLOCKED - ")
                      + alive[b].name + ": " + aliveGate[b].verdict + ", lowest median " + fx (lowestMedian (aliveGate[b]))
                      + ", x " + fx (g3[b].cpu) + " Hall" + (g3[b].pass ? ", G3 pass" : ", G3 FAIL")
                      + "; the other families' searches are in the matrix";

        info ("");
        info ("  CHAMBER: " + family.status);

        check (family.standing, "FINDING: a Chamber concept passes G1, G2 and G3");

        return family;
    }

    // =======================================================================
    //  Part 6 - the cross-mode matrix
    // =======================================================================
    std::vector<std::string> runMatrix (const std::vector<Family>& families)
    {
        std::vector<std::string> reproducedNames;
        // ===================================================================
        beginSuite ("PHASE 12.6.13 - the cross-mode matrix");
        // ===================================================================

        info ("  Every family that stands structurally, as target (rows) and as impostor");
        info ("  (columns). Three points, three seeds, centred; the impostor follows its own");
        info ("  knobs. Each cell: lowest run / lowest median. A run <= 1.5 means the row is");
        info ("  not a separate Mode from the column. (Hall as a target is reported, not held");
        info ("  against the column.)");
        info ("");

        const size_t count = families.size();

        for (const auto& f : families)
            info ("    " + pad (f.name, 10) + f.target.name);

        info ("");

        std::vector<std::string> cell (count * count, "-");
        bool collapse = false;
        std::string collapses;

        for (size_t i = 0; i < count; ++i)
        {
            std::vector<Candidate> targets;
            std::vector<size_t> index;

            for (size_t t = 0; t < count; ++t)
                if (t != i)
                {
                    targets.push_back (families[t].target);
                    index.push_back (t);
                }

            const auto gates = gateWith (targets, families[i].impostor.make, 3);

            info ("  IMPOSTOR " + families[i].name + ":");
            stage5::printGate (targets, gates);
            info ("");

            for (size_t k = 0; k < targets.size(); ++k)
            {
                const bool reproduced = gates[k].minimum <= kReproduced;

                cell[index[k] * count + i] = fx (gates[k].minimum) + " / " + fx (lowestMedian (gates[k]))
                                           + (reproduced ? " !" : "");

                if (reproduced && index[k] != 0)
                {
                    collapse = true;
                    reproducedNames.push_back (families[index[k]].name);
                    collapses += families[index[k]].name + " by " + families[i].name + "; ";
                }
            }
        }

        std::string header = "    " + pad ("target \\ impostor", 20);

        for (const auto& f : families)
            header += pad (f.name, 18);

        info (header);

        for (size_t t = 0; t < count; ++t)
        {
            std::string row = "    " + pad (families[t].name, 20);

            for (size_t i = 0; i < count; ++i)
                row += pad (cell[t * count + i], 18);

            info (row);
        }

        info ("");
        info (collapse ? "  REPRODUCED: " + collapses : "  No family other than Hall is reproduced by another family's controls.");

        check (! collapse, "FINDING: no standing family is reproduced by another family's controls");

        return reproducedNames;
    }

    // =======================================================================
    //  Parts 7 and 8 - cost, memory, switching
    // =======================================================================
    void runEngineering (const std::vector<Family>& families)
    {
        // ===================================================================
        beginSuite ("PHASE 12.6.14 - cost, memory and switching");
        // ===================================================================

        info ("  Wet path against Hall, alternately. Memory: the tank's delay lines at the");
        info ("  largest Size plus any dispersion state (the diffuser, reflections and attack");
        info ("  stage are the same for every family and are left out). Configure: the time to");
        info ("  build the structure from nothing, against Hall.");
        info ("");

        auto memoryKiB = [] (const Structure& s)
        {
            User u;
            u.size = 1.0;

            Rig rig (s, u);

            double doubles = 0.0;

            if (rig.getOpenTank() != nullptr)
                doubles = static_cast<double> (rig.getOpenTank()->memoryDoubles());
            else
                doubles = 8.0 * rig.getCapacityMs() * 0.001 * kRate;

            if (! s.preChain.empty() || s.preDispersion > 0)
                doubles += 2.0 * static_cast<double> (std::max (static_cast<size_t> (s.preDispersion), s.preChain.size()));

            return doubles * 8.0 / 1024.0;
        };

        auto configureNs = [] (const Structure& s)
        {
            double best = 1.0e30;

            for (int attempt = 0; attempt < 7; ++attempt)
            {
                const auto t0 = std::chrono::steady_clock::now();
                Rig rig (s, User {});
                const auto t1 = std::chrono::steady_clock::now();

                gSink = gSink + rig.getCapacityMs();
                best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count());
            }

            return best;
        };

        std::vector<double> cost (families.size(), 1.0);

        info ("    " + pad ("family", 10) + pad ("structure", 30) + pad ("x Hall", 8) + pad ("memory KiB", 12) + "configure x Hall");

        const double hallConfigure = configureNs (Structure {});

        for (size_t k = 0; k < families.size(); ++k)
        {
            const Structure s = families[k].target.make (User {});

            cost[k] = k == 0 ? 1.0 : wetPathRatio (s);

            info ("    " + pad (families[k].name, 10) + pad (families[k].target.name, 30) + pad (fx (cost[k]), 8)
                  + pad (fx (memoryKiB (s), 0), 12) + fx (configureNs (s) / hallConfigure));
        }

        info ("");
        info ("  Two networks at once, during a switch (sum of the two, x Hall):");

        double worstPair = 0.0;

        for (size_t a = 0; a < families.size(); ++a)
        {
            std::string row = "    " + pad (families[a].name, 10);

            for (size_t b = 0; b < families.size(); ++b)
            {
                row += pad (a == b ? "-" : fx (cost[a] + cost[b]), 8);

                if (a != b)
                    worstPair = std::max (worstPair, cost[a] + cost[b]);
            }

            info (row);
        }

        info ("    worst pair: " + fx (worstPair) + " x Hall for the length of the pre-warm and the crossfade.");

        // --- switching -------------------------------------------------------------
        info ("");
        info ("  SWITCHING. Continuous noise. A network started `pre-warm` before the switch,");
        info ("  against the same network running all along: level over the 120 ms crossfade,");
        info ("  dB. More than 1 dB short means that pre-warm is not enough.");
        info ("");
        info ("    " + pad ("family", 10) + pad ("setting", 16) + pad ("0.4 s", 9) + pad ("0.8 s", 9) + pad ("1.6 s", 9)
              + pad ("3.2 s", 9) + "enough at");

        constexpr double prewarm[4] { 0.4, 0.8, 1.6, 3.2 };

        struct Setting { const char* label; double size, decay; };
        constexpr Setting settings[2] { { "60, 2.5 s", 0.60, 2.5 }, { "85, 5.0 s", 0.85, 5.0 } };

        std::vector<double> shortfall (families.size() * 2 * 4, 0.0);

        runParallel (shortfall.size(), [&] (size_t i)
        {
            const size_t k = i / 8;
            const auto& setting = settings[(i / 4) % 2];
            const double warm = prewarm[i % 4];

            User u;
            u.size = setting.size;
            u.decay = setting.decay;

            const Structure s = families[k].target.make (u);

            const double settle = 4.0 * setting.decay;
            const auto total = static_cast<size_t> ((settle + 0.12) * kRate);
            const auto window = static_cast<size_t> (0.12 * kRate);
            const auto start = static_cast<size_t> ((settle - warm) * kRate);

            const auto noise = whiteNoise (total, 0xBEEF01u);

            auto level = [&] (size_t from)
            {
                Rig rig (s, u);
                ScopedFtz ftz;

                double sum = 0.0;

                for (size_t n = from; n < total; ++n)
                {
                    double l = noise[n], r = noise[n];
                    rig.processSample (l, r);

                    if (n >= total - window)
                        sum += l * l + r * r;
                }

                return sum;
            };

            shortfall[i] = 10.0 * std::log10 (level (start) / level (0));
        });

        bool allEnough = true;

        for (size_t k = 0; k < families.size(); ++k)
            for (size_t s = 0; s < 2; ++s)
            {
                std::string row = "    " + pad (families[k].name, 10) + pad (settings[s].label, 16);
                std::string enough = "more than 3.2 s";

                for (size_t w = 4; w-- > 0;)
                    if (shortfall[k * 8 + s * 4 + w] >= -1.0)
                        enough = fx (prewarm[w], 1) + " s";

                for (size_t w = 0; w < 4; ++w)
                    row += pad (fx (shortfall[k * 8 + s * 4 + w], 1), 9);

                if (shortfall[k * 8 + s * 4] < -1.0)
                    allEnough = false;

                info (row + enough);
            }

        info ("");
        info (allEnough ? "  400 ms of pre-warm is enough for every standing structure."
                        : "  400 ms of pre-warm is NOT enough for every structure; see the table.");

        check (allEnough, "FINDING: 400 ms of pre-warm brings every standing structure within 1 dB");
    }
}

// ===========================================================================
//  The order of work
// ===========================================================================
void runPhase12Stage6Tests()
{
    using namespace stage6;

    const auto started = std::chrono::steady_clock::now();

    ensureFloor();

    const char* only = std::getenv ("AV_S6_PART");
    const std::string part = only != nullptr ? only : "RAXPCME";

    auto wants = [&] (char c) { return part.find (c) != std::string::npos; };

    // =======================================================================
    beginSuite ("PHASE 12.6.1 - the search with a knob-dependent impostor is the frozen search");
    // =======================================================================
    {
        const User u = userAt (kPoints[0]);
        const auto target = featuresOf (stage5::standingRoom().best.make (u), u);

        SearchResult original, copy;

        runParallel (2, [&] (size_t i)
        {
            if (i == 0)
                original = mimic (target, Structure {}, floorOf(), toControls (u), stage5::seedFor (0), stage5::kLateralFamily);
            else
                copy = mimicWith (target, [] (const User&) { return Structure {}; }, floorOf(), toControls (u),
                                  stage5::seedFor (0), stage5::kLateralFamily);
        });

        bool same = original.best.worst == copy.best.worst && original.evaluations == copy.evaluations;

        for (int k = 0; k < kControls; ++k)
            if (original.controls.v[k] != copy.controls.v[k])
                same = false;

        info ("  R4c at the medium point, Hall as the impostor, one seed:");
        info ("    frozen search   " + fx (original.best.worst, 6) + "  after " + std::to_string (original.evaluations)
              + " evaluations   " + describe (original.controls));
        info ("    the copy        " + fx (copy.best.worst, 6) + "  after " + std::to_string (copy.evaluations)
              + " evaluations   " + describe (copy.controls));

        check (same, "the copy returns the frozen search's result, knob for knob");
    }

    Family room, ambient, plate, chamber;

    const bool needOthers = wants ('X') || wants ('M') || wants ('E');

    if (wants ('R'))
        room = runRoom();
    else if (needOthers)
    {
        room = familyOf ("Room " + standingRoomSpec().name, roomRaw (standingRoomSpec()), true);
        room.structural = true;
        room.status = "(not re-run: " + standingRoomSpec().name + " as defined)";
    }

    if (wants ('A'))
        ambient = runAmbient();
    else if (needOthers)
    {
        ambient = familyOf ("Ambient " + standingAmbientSpec().name, ambientRaw (standingAmbientSpec()), true);
        ambient.structural = true;
        ambient.status = "(not re-run: " + standingAmbientSpec().name + " as defined)";
    }

    bool twoModes = false;

    if (wants ('X'))
        twoModes = runRoomAgainstAmbient (room, ambient);

    if (wants ('P'))
        plate = runPlate();
    else if (wants ('M') || wants ('E'))
    {
        const Structure s = standingPlateStructure();

        plate.name = "Plate";
        plate.raw = [s] (const User&) { return s; };
        plate.target = plate.impostor = stage5::fixed (s.name, s);
        plate.structural = true;
        plate.status = "(not re-run: " + s.name + " as defined)";
    }

    if (wants ('C'))
        chamber = runChamber();

    std::vector<Family> families;

    families.push_back (hallFamily());

    for (auto* f : { &room, &ambient, &plate, &chamber })
        if (f->structural)
        {
            // The matrix and the tables name families by their role.
            Family named = *f;
            named.name = f == &room ? "Room" : f == &ambient ? "Ambient" : f == &plate ? "Plate" : "Chamber";
            families.push_back (named);
        }

    if (wants ('M'))
    {
        for (const auto& name : runMatrix (families))
            for (auto* f : { &room, &ambient, &plate, &chamber })
                if (name == (f == &room ? "Room" : f == &ambient ? "Ambient" : f == &plate ? "Plate" : "Chamber"))
                {
                    f->standing = false;
                    f->status += "  [REPRODUCED by another family in the matrix]";
                }
    }

    if (wants ('E'))
        runEngineering (families);

    if (part == "RAXPCME")
    {
        beginSuite ("PHASE 12.6.15 - Mode status after Stage 6");

        info ("    " + pad ("HALL", 10) + "PROVEN - the frozen production engine");
        info ("    " + pad ("ROOM", 10) + room.status);
        info ("    " + pad ("AMBIENT", 10) + ambient.status);
        info ("    " + pad ("ROOM / AMBIENT", 16) + (twoModes ? "DISTINCT in both directions" : "NOT shown to be two Modes"));
        info ("    " + pad ("PLATE", 10) + plate.status);
        info ("    " + pad ("CHAMBER", 10) + chamber.status);

        check (room.standing,    "FINDING: Room passes every gate of Stage 6");
        check (ambient.standing, "FINDING: Ambient passes every gate of Stage 6");
        check (twoModes,         "FINDING: Room and Ambient are two Modes");
        check (plate.standing,   "FINDING: Plate passes every gate of Stage 6 at <= 1.75 x Hall");
        check (chamber.standing, "FINDING: Chamber passes every gate of Stage 6");
    }

    info ("");
    info ("  Stage 6 suite wall time: "
          + fx (std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count(), 0)
          + " s");
}

} // namespace avtest
