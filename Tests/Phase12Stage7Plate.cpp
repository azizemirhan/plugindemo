#include "Phase12Stage7Plate.h"
#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DspAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

/*
    PHASE 12 - STAGE 7b. The Plate (PV3) validation tool.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    See Phase12Stage7Plate.h for how the suites are run, and
    Docs/Phase12-Stage7b-Plate-Validation-Setup.md (with its addendum) for
    the procedure and the limits. The limits below are copies of that
    document's and were not chosen here.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace stage7
{
    namespace
    {
        constexpr double kTwoPi = 6.283185307179586;

        // The band and the limit of check S1.
        constexpr double kBandLowHz = 100.0, kBandHighHz = 10000.0;
        constexpr int    kBandSteps = 201;
        constexpr double kRelativeLimit = 0.05, kAbsoluteLimit = 0.5e-3;

        [[nodiscard]] double bandHz (int i) noexcept
        {
            return kBandLowHz * std::pow (kBandHighHz / kBandLowHz,
                                          static_cast<double> (i) / static_cast<double> (kBandSteps - 1));
        }

        [[nodiscard]] double binomialAtMost (int n, int k) noexcept
        {
            // P (X <= k), X ~ Binomial (n, 1/2).
            double term = std::pow (0.5, n), sum = 0.0;

            for (int i = 0; i <= k; ++i)
            {
                sum += term;
                term *= static_cast<double> (n - i) / static_cast<double> (i + 1);
            }

            return sum;
        }
    }

    double plateCoefficient (double rate) noexcept
    {
        const double reference = (1.0 - kReferenceCoeff) / (1.0 + kReferenceCoeff);
        const double r = reference * rate / kReferenceRate;

        return -(r - 1.0) / (r + 1.0);
    }

    double chainDelaySeconds (double coefficient, double hz, double rate) noexcept
    {
        return static_cast<double> (kPlateSections) * dispersionDelay (coefficient, hz, rate) / rate;
    }

    double worstDelayError (double coefficient, double rate, double* atHz, bool* withinLimit)
    {
        double worst = 0.0, where = 0.0;
        bool inside = true;

        for (int i = 0; i < kBandSteps; ++i)
        {
            const double hz = bandHz (i);
            const double reference = chainDelaySeconds (kReferenceCoeff, hz, kReferenceRate);
            const double error = std::abs (chainDelaySeconds (coefficient, hz, rate) - reference);

            if (error > worst)
            {
                worst = error;
                where = hz;
            }

            if (error > std::max (kRelativeLimit * reference, kAbsoluteLimit))
                inside = false;
        }

        if (atHz != nullptr)
            *atHz = where;

        if (withinLimit != nullptr)
            *withinLimit = inside;

        return worst;
    }

    double bestFitCoefficient (double rate)
    {
        // Golden section on the largest error. The error is unimodal in the
        // coefficient: every frequency's delay is monotone in it.
        constexpr double phi = 0.6180339887498949;

        double low = -0.995, high = -0.05;
        double a = high - phi * (high - low), b = low + phi * (high - low);
        double fa = worstDelayError (a, rate), fb = worstDelayError (b, rate);

        for (int i = 0; i < 200; ++i)
        {
            if (fa < fb)
            {
                high = b;
                b = a;
                fb = fa;
                a = high - phi * (high - low);
                fa = worstDelayError (a, rate);
            }
            else
            {
                low = a;
                a = b;
                fa = fb;
                b = low + phi * (high - low);
                fb = worstDelayError (b, rate);
            }
        }

        return 0.5 * (low + high);
    }

    Structure platePV3 (double rate)
    {
        Structure s;
        s.name = "Plate PV3";
        s.preChain.assign (static_cast<size_t> (kPlateSections), ChainSection { plateCoefficient (rate), 1 });
        s.preChainSplit = kPlateSubChains;
        s.preChainRegisters = true;
        return s;
    }

    Structure platePV1 (double rate)
    {
        Structure s;
        s.name = "Plate PV1";
        s.preChain.assign (static_cast<size_t> (kPlateSections), ChainSection { plateCoefficient (rate), 1 });
        s.preChainSplit = 8;
        return s;
    }

    Structure platePlain (double rate)
    {
        Structure s;
        s.name = "Plate, plain chain";
        s.preDispersion = kPlateSections;
        s.preDispersionCoeff = plateCoefficient (rate);
        return s;
    }

    MedianRanks medianRanks (int n)
    {
        MedianRanks r;

        // The largest k with P (X <= k - 1) <= 2.5 %: the interval
        // [x(k), x(n + 1 - k)] then covers the median with at least 95 %.
        for (int k = 1; 2 * k <= n; ++k)
        {
            const double tail = binomialAtMost (n, k - 1);

            if (tail > 0.025)
                break;

            r.lower = k;
            r.upper = n + 1 - k;
            r.twoSidedCoverage = 1.0 - 2.0 * tail;
            r.upperConfidence  = 1.0 - tail;
        }

        return r;
    }

    PairStats pairStats (const std::vector<double>& hallNs, const std::vector<double>& plateNs)
    {
        PairStats s;
        s.pairs = static_cast<int> (std::min (hallNs.size(), plateNs.size()));

        if (s.pairs == 0)
            return s;

        std::vector<double> ratio (static_cast<size_t> (s.pairs));

        for (size_t i = 0; i < ratio.size(); ++i)
            ratio[i] = plateNs[i] / hallNs[i];

        std::sort (ratio.begin(), ratio.end());

        const size_t n = ratio.size();

        s.median = n % 2 == 1 ? ratio[n / 2] : 0.5 * (ratio[n / 2 - 1] + ratio[n / 2]);
        s.ranks = medianRanks (s.pairs);

        if (s.ranks.lower > 0)
        {
            s.lower = ratio[static_cast<size_t> (s.ranks.lower - 1)];
            s.upper = ratio[static_cast<size_t> (s.ranks.upper - 1)];
        }

        const auto hall  = std::minmax_element (hallNs.begin(),  hallNs.begin()  + s.pairs);
        const double plateMin = *std::min_element (plateNs.begin(), plateNs.begin() + s.pairs);

        s.ratioOfMinima = plateMin / *hall.first;
        s.hallSpread = *hall.second / *hall.first;

        return s;
    }

    Stereo to48k (const Stereo& x, double rate)
    {
        // A Hann-windowed sinc at 20 kHz, 32 zero crossings each side
        // (0.8 ms): flat to about 18.7 kHz, gone by 21.3 kHz, which is
        // under the Nyquist frequency of every supported rate. The 48 kHz
        // reference goes through the same filter, so both sides of a
        // comparison carry the same band.
        constexpr double cutoff = 20000.0;
        constexpr double half = 32.0 / (2.0 * cutoff);

        const size_t length = static_cast<size_t> (std::floor (static_cast<double> (x.l.size()) * kReferenceRate / rate));
        const double scale = (2.0 * cutoff / rate) * (rate / kReferenceRate);

        Stereo y;
        y.l.assign (length, 0.0);
        y.r.assign (length, 0.0);

        runParallel (16, [&] (size_t part)
        {
            const size_t begin = length * part / 16, end = length * (part + 1) / 16;

            for (size_t n = begin; n < end; ++n)
            {
                const double t = static_cast<double> (n) / kReferenceRate;
                const auto first = static_cast<long long> (std::ceil ((t - half) * rate));
                const auto last  = static_cast<long long> (std::floor ((t + half) * rate));

                double l = 0.0, r = 0.0;

                for (long long k = std::max (0LL, first); k <= last && k < static_cast<long long> (x.l.size()); ++k)
                {
                    const double d = t - static_cast<double> (k) / rate;
                    const double arg = kTwoPi * cutoff * d;
                    const double sinc = std::abs (arg) < 1.0e-12 ? 1.0 : std::sin (arg) / arg;
                    const double h = sinc * (0.5 + 0.5 * std::cos (0.5 * kTwoPi * d / half));

                    l += h * x.l[static_cast<size_t> (k)];
                    r += h * x.r[static_cast<size_t> (k)];
                }

                y.l[n] = scale * l;
                y.r[n] = scale * r;
            }
        });

        return y;
    }

    // =======================================================================
    //  Shared by the three on-request suites
    // =======================================================================
    namespace
    {
        volatile double gSink = 0.0;

        constexpr double kRates[4] { 44100.0, 48000.0, 96000.0, 192000.0 };
        constexpr double kLimit = 1.75;
        constexpr int    kWarmUp = 3;

        [[nodiscard]] std::string rateName (double rate)
        {
            return fx (rate / 1000.0, 1) + " kHz";
        }

        /** ns per stereo sample over one block. No allocation inside the
            timed region: the rig is driven sample by sample. */
        [[nodiscard]] double blockNs (Rig& rig, const std::vector<double>& drive, size_t length)
        {
            ScopedFtz ftz;

            double sum = 0.0;

            const auto t0 = std::chrono::steady_clock::now();

            for (size_t n = 0; n < length; ++n)
            {
                double l = n < drive.size() ? drive[n] : 0.0;
                double r = l;

                rig.processSample (l, r);
                sum += l + r;
            }

            const auto t1 = std::chrono::steady_clock::now();

            gSink = gSink + sum;

            return std::chrono::duration<double, std::nano> (t1 - t0).count() / static_cast<double> (length);
        }

        struct Campaign
        {
            PairStats stats;
            double hallNs = 0.0, plateNs = 0.0;     // fastest block of each
        };

        /** `pairs` interleaved pairs after kWarmUp discarded ones. The two
            rigs live for the whole campaign, so what is timed is the steady
            state. `preSilence` seconds of zeros are run through both first,
            untimed (the denormal case). */
        [[nodiscard]] Campaign campaign (const Structure& plate, const User& u, double rate, int pairs,
                                         double driveSeconds, double preSilence = 0.0)
        {
            const auto length = static_cast<size_t> (2.0 * rate);
            const auto drive = whiteNoise (static_cast<size_t> (driveSeconds * rate), 0xC0FFEEu);

            auto hall = std::make_unique<Rig> (Structure {}, u, rate);
            auto other = std::make_unique<Rig> (plate, u, rate);

            if (preSilence > 0.0)
            {
                const std::vector<double> burst = whiteNoise (static_cast<size_t> (0.25 * rate), 0xC0FFEEu);
                const auto quiet = static_cast<size_t> (preSilence * rate);

                gSink = gSink + blockNs (*hall, burst, quiet) + blockNs (*other, burst, quiet);
            }

            std::vector<double> hallNs, plateNs;

            for (int i = 0; i < kWarmUp + pairs; ++i)
            {
                double h = 0.0, p = 0.0;

                if (i % 2 == 0)
                {
                    h = blockNs (*hall, drive, length);
                    p = blockNs (*other, drive, length);
                }
                else
                {
                    p = blockNs (*other, drive, length);
                    h = blockNs (*hall, drive, length);
                }

                if (i >= kWarmUp)
                {
                    hallNs.push_back (h);
                    plateNs.push_back (p);
                }
            }

            Campaign c;
            c.stats = pairStats (hallNs, plateNs);
            c.hallNs = *std::min_element (hallNs.begin(), hallNs.end());
            c.plateNs = *std::min_element (plateNs.begin(), plateNs.end());
            return c;
        }

        void statisticNote (int pairs)
        {
            const auto r = medianRanks (pairs);

            auto ordinal = [] (int k)
            {
                const int tens = k % 100, units = k % 10;

                return std::to_string (k) + (tens >= 11 && tens <= 13 ? "th"
                                             : units == 1 ? "st" : units == 2 ? "nd" : units == 3 ? "rd" : "th");
            };

            info ("  THE STATISTIC. One pair is one 2 s block of Hall and one of Plate, back to back,");
            info ("  order alternating; its figure is Plate time / Hall time. " + std::to_string (kWarmUp)
                  + " warm-up pairs are discarded,");
            info ("  then exactly " + std::to_string (pairs) + " are kept and sorted.");
            info ("    median     the middle ratio");
            info ("    lower      the " + ordinal (r.lower) + " smallest");
            info ("    upper      the " + ordinal (r.upper) + " smallest. A distribution-free one-sided UPPER CONFIDENCE BOUND ON");
            info ("               THE MEDIAN RATIO, confidence " + fx (100.0 * r.upperConfidence, 1)
                  + " % (two-sided with the " + ordinal (r.lower) + ": "
                  + fx (100.0 * r.twoSidedCoverage, 1) + " %),");
            info ("               if the pairs are independent. It is NOT a bound on the 95th percentile of");
            info ("               the ratio and says nothing about the worst block.");
            info ("    min/min    fastest Plate block over fastest Hall block");
            info ("    Hall max/min   slowest Hall block over fastest: how quiet the machine was. Near 1.00");
            info ("               on an idle machine; a large value means the run should not be used.");
            info ("");
        }

        [[nodiscard]] std::string row (const std::string& label, const Campaign& c)
        {
            return "    " + pad (label, 30) + pad (fx (c.stats.median, 3), 9) + pad (fx (c.stats.lower, 3), 9)
                 + pad (fx (c.stats.upper, 3), 9) + pad (fx (c.stats.ratioOfMinima, 3), 9)
                 + pad (fx (c.hallNs, 1), 10) + pad (fx (c.plateNs, 1), 10) + fx (c.stats.hallSpread, 3);
        }

        void header()
        {
            info ("    " + pad ("condition", 30) + pad ("median", 9) + pad ("lower", 9) + pad ("upper", 9)
                  + pad ("min/min", 9) + pad ("Hall ns", 10) + pad ("Plate ns", 10) + "Hall max/min");
        }

        // --- cross-rate analysis ----------------------------------------------------
        struct Reading
        {
            Features features;
            double rt60 = 0.0;
        };

        /** Features and RT60 of a structure rendered at `rate`, analysed at
            48 kHz after to48k(). */
        [[nodiscard]] Reading readingAt (const Structure& s, const User& u, double rate)
        {
            const std::vector<double> one (1, 1.0), none (1, 0.0);

            const double seconds = irSecondsFor (u.decay);

            auto centreRig = std::make_unique<Rig> (s, u, rate);
            auto centre = to48k (centreRig->render (one, one, static_cast<size_t> ((seconds + 1.05) * rate)), rate);

            auto leftRig = std::make_unique<Rig> (s, u, rate);
            auto left = to48k (leftRig->render (one, none, static_cast<size_t> ((kLeftOnlySeconds + 0.05) * rate)), rate);

            auto trim = [] (Stereo& x, double length)
            {
                const auto n = static_cast<size_t> (length * kRate);

                x.l.resize (n, 0.0);
                x.r.resize (n, 0.0);
            };

            Reading r;

            trim (centre, seconds + 1.0);
            r.rt60 = mode12::rt60FromEnergy (detail::energyOfBoth (centre));

            trim (centre, seconds);
            trim (left, kLeftOnlySeconds);

            r.features = analyse (centre, left);
            return r;
        }

        [[nodiscard]] double score (const Features& a, const Features& b, int* family = nullptr)
        {
            return stage5::centredWorst (stage5::sameKnobs (a, b), family);
        }
    }
}

// ===========================================================================
//  Fast: the tool's own arithmetic. Part of the regression.
// ===========================================================================
void runPhase12Stage7PlateFastTests()
{
    using namespace stage7;

    beginSuite ("PHASE 12.7.0 - the Plate validation tool's arithmetic");

    // --- the law -----------------------------------------------------------------
    {
        const double at48 = plateCoefficient (48000.0);

        info ("    coefficient by the law: 44.1 kHz " + fx (plateCoefficient (44100.0), 6)
              + ", 48 kHz " + fx (at48, 6) + ", 96 kHz " + fx (plateCoefficient (96000.0), 6)
              + ", 192 kHz " + fx (plateCoefficient (192000.0), 6));

        check (std::abs (at48 - kReferenceCoeff) < 1.0e-15, "at 48 kHz the law returns the reference coefficient");

        bool ordered = true, matched = true;
        const double reference = chainDelaySeconds (kReferenceCoeff, 0.0, kReferenceRate);

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            const double a = plateCoefficient (rate);

            if (! (a > -1.0 && a < 0.0))
                ordered = false;

            if (std::abs (chainDelaySeconds (a, 0.0, rate) / reference - 1.0) > 1.0e-12)
                matched = false;
        }

        if (! (plateCoefficient (44100.0) > plateCoefficient (48000.0)
               && plateCoefficient (48000.0) > plateCoefficient (96000.0)
               && plateCoefficient (96000.0) > plateCoefficient (192000.0)))
            ordered = false;

        check (ordered, "the coefficient is stable and moves one way with the rate");
        check (matched, "the chain's delay at zero frequency is the same number of seconds at every rate");
    }

    // --- the statistic -----------------------------------------------------------
    {
        const auto r31 = medianRanks (31);
        const auto r7  = medianRanks (7);

        check (r31.lower == 10 && r31.upper == 22, "31 pairs: the interval for the median is the 10th to the 22nd");
        check (std::abs (r31.twoSidedCoverage - 0.9706) < 5.0e-4 && std::abs (r31.upperConfidence - 0.9853) < 5.0e-4,
               "31 pairs: 97.1 % two-sided, 98.5 % one-sided");
        check (r7.lower == 1 && r7.upper == 7, "7 pairs: the interval is the whole range");

        std::vector<double> hall (31, 2.0), plate (31);

        for (size_t i = 0; i < plate.size(); ++i)
            plate[i] = 2.0 * static_cast<double> ((i * 7) % 31 + 1);     // 1 .. 31, shuffled

        const auto s = pairStats (hall, plate);

        check (s.median == 16.0 && s.lower == 10.0 && s.upper == 22.0 && s.ratioOfMinima == 1.0,
               "the order statistics are read from the sorted ratios");
    }

    // --- the chain at every rate's coefficient ------------------------------------
    {
        const auto noise = whiteNoise (8192, 0xA11CEu);

        double worst = 0.0, worstEnergyDb = 0.0;

        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            const double a = plateCoefficient (rate);

            Dispersion plainL, plainR;
            plainL.prepare (kPlateSections, a);
            plainR.prepare (kPlateSections, a);

            stage6::PipelinedChain chain;
            const bool ok = chain.prepare (std::vector<double> (static_cast<size_t> (kPlateSections), a),
                                           kPlateSubChains, false, true);

            if (! ok)
                worst = 1.0;

            std::vector<double> referenceL (noise.size()), referenceR (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                referenceL[n] = plainL.process (noise[n]);
                referenceR[n] = plainR.process (-0.5 * noise[n]);
            }

            const auto lag = static_cast<size_t> (kPipelineSamples);

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double l = noise[n], r = -0.5 * noise[n];
                chain.process (l, r);

                if (n >= lag)
                    worst = std::max (worst, std::max (std::abs (l - referenceL[n - lag]),
                                                       std::abs (r - referenceR[n - lag])));
            }

            // Lossless: an impulse through the plain chain keeps its energy.
            Dispersion impulse;
            impulse.prepare (kPlateSections, a);

            double energy = 0.0;

            for (size_t n = 0; n < static_cast<size_t> (1.0 * rate); ++n)
            {
                const double y = impulse.process (n == 0 ? 1.0 : 0.0);
                energy += y * y;
            }

            worstEnergyDb = std::max (worstEnergyDb, std::abs (10.0 * std::log10 (energy)));
        }

        info ("    PV3 against the plain chain, 15 samples later, worst over four rates: " + fx (worst * 1.0e12, 3) + " e-12");
        info ("    energy of the chain's impulse response, worst over four rates: " + fx (worstEnergyDb, 6) + " dB");

        check (worst < 1.0e-9, "at every rate's coefficient PV3 is the plain chain delayed by exactly 15 samples");
        check (worstEnergyDb < 0.01, "at every rate's coefficient the chain is lossless within 0.01 dB");
    }

    // --- the resampler --------------------------------------------------------------
    {
        double worst = 0.0;

        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Stereo x;
            x.l.resize (static_cast<size_t> (0.1 * rate));
            x.r.resize (x.l.size());

            for (size_t n = 0; n < x.l.size(); ++n)
                x.l[n] = x.r[n] = std::sin (6.283185307179586 * 1000.0 * static_cast<double> (n) / rate);

            const auto y = to48k (x, rate);

            // An impulse response keeps its scale, so a signal is scaled by rate / 48000.
            for (size_t n = 480; n + 480 < y.l.size(); ++n)
                worst = std::max (worst, std::abs (y.l[n] * 48000.0 / rate
                                                   - std::sin (6.283185307179586 * 1000.0 * static_cast<double> (n) / 48000.0)));
        }

        info ("    a 1 kHz sine through to48k(), worst error over four rates: " + fx (worst * 1.0e6, 2) + " e-6");
        check (worst < 1.0e-3, "the resampler returns a 1 kHz sine at 48 kHz within 0.001");
    }
}

// ===========================================================================
//  On request: primary CPU closure, 48 kHz
// ===========================================================================
void runPhase12Stage7PlateCpu()
{
    using namespace stage7;

    beginSuite ("PHASE 12.7 PLATE - primary CPU closure, 48 kHz, 31 interleaved pairs");

    info ("  ONE RUN of the three required. Valid ONLY on an idle machine: this tool cannot");
    info ("  establish idleness. Wet path, single thread, " + pinAndPrioritise() + ".");
    info ("");

    statisticNote (31);
    header();

    const Structure plate = platePV3 (48000.0);

    bool pass = true;

    for (const auto& p : kPoints)
    {
        const auto c = campaign (plate, userAt (p), 48000.0, 31, 2.0);

        info (row (p.label, c));

        if (! (c.stats.upper < kLimit))
            pass = false;
    }

    info ("");
    check (pass, "the upper confidence bound on the median ratio is below 1.75 at all three points");
}

// ===========================================================================
//  On request: stress conditions and the scalar form. Reported, not gating.
// ===========================================================================
void runPhase12Stage7PlateStress()
{
    using namespace stage7;

    beginSuite ("PHASE 12.7 PLATE - stress conditions and the scalar form, 48 kHz");

    info ("  REPORTED, NOT GATING. An upper bound at or above 1.75 here is an open finding.");
    info ("  Valid only on an idle machine. " + pinAndPrioritise() + ".");
    info ("");

    statisticNote (31);
    header();

    const Structure pv3 = platePV3 (48000.0), pv1 = platePV1 (48000.0);

    std::vector<std::string> open;

    auto report = [&] (const std::string& label, const Campaign& c)
    {
        info (row (label, c));

        if (! (c.stats.upper < kLimit))
            open.push_back (label);
    };

    {
        User u;
        u.size = 0.0;
        report ("PV3, Size 0", campaign (pv3, u, 48000.0, 31, 2.0));

        u.size = 1.0;
        report ("PV3, Size 100", campaign (pv3, u, 48000.0, 31, 2.0));
    }

    {
        User u;
        u.modDepth = 0.0;
        report ("PV3, modulation off", campaign (pv3, u, 48000.0, 31, 2.0));
    }

    {
        // 100 s of silence after a burst at Decay 0.8 s: the tank is some
        // thousands of dB down, where a denormal stall would show. The
        // absolute ns columns are what to read: they should not be above
        // the driven figures.
        const User u = userAt (kPoints[1]);
        report ("PV3, silence after 100 s", campaign (pv3, u, 48000.0, 31, 0.0, 100.0));
    }

    for (const auto& p : kPoints)
        report (std::string ("PV1 scalar, ") + p.label, campaign (pv1, userAt (p), 48000.0, 31, 2.0));

    info ("");

    if (open.empty())
        info ("  No condition at or above 1.75.");
    else
        for (const auto& name : open)
            info ("  OPEN FINDING: upper bound at or above 1.75 in: " + name);
}

// ===========================================================================
//  On request: 44.1 / 96 / 192 kHz, checks S0 - S7
// ===========================================================================
void runPhase12Stage7PlateRates()
{
    using namespace stage7;

    beginSuite ("PHASE 12.7 PLATE - secondary rates: 44.1, 96 and 192 kHz");

    // --- the law and S1 -----------------------------------------------------------
    info ("  THE LAW. 240 sections at every rate; the coefficient is the one at which a section's");
    info ("  delay at zero frequency, in seconds, is the 48 kHz section's. No other number is fitted.");
    info ("");
    info ("  S1. Group delay of the chain, ms, against the 48 kHz chain. Limit over 100 Hz - 10 kHz:");
    info ("  5 % or 0.5 ms, whichever is larger. 'total' adds the 15-sample pipeline delay.");
    info ("");

    {
        constexpr double shown[10] { 20.0, 100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 10000.0, 16000.0 };

        std::string head = "    " + pad ("rate", 11) + pad ("coefficient", 13);

        for (double hz : shown)
            head += pad (fx (hz, 0), 8);

        info (head + pad ("+pipeline", 11));

        for (double rate : kRates)
        {
            const double a = plateCoefficient (rate);
            std::string line = "    " + pad (rateName (rate), 11) + pad (fx (a, 6), 13);

            for (double hz : shown)
                line += pad (fx (1000.0 * chainDelaySeconds (a, hz, rate), 2), 8);

            info (line + fx (1000.0 * kPipelineSamples / rate, 3));
        }

        info ("");
        info ("    " + pad ("rate", 11) + pad ("worst chain error", 30) + pad ("worst total error", 30)
              + pad ("best single coefficient", 26) + "its worst error");

        for (double rate : kRates)
        {
            if (rate == kReferenceRate)
                continue;

            const double a = plateCoefficient (rate);

            double atHz = 0.0;
            bool inside = false;
            const double worst = worstDelayError (a, rate, &atHz, &inside);

            // The total: the same comparison with each side's pipeline delay added.
            const double pipeline = kPipelineSamples / rate - kPipelineSamples / kReferenceRate;

            double worstTotal = 0.0, totalHz = 0.0;
            bool totalInside = true;

            for (int i = 0; i < kBandSteps; ++i)
            {
                const double hz = bandHz (i);
                const double reference = chainDelaySeconds (kReferenceCoeff, hz, kReferenceRate)
                                       + kPipelineSamples / kReferenceRate;
                const double error = std::abs (chainDelaySeconds (a, hz, rate) + kPipelineSamples / rate - reference);

                if (error > worstTotal)
                {
                    worstTotal = error;
                    totalHz = hz;
                }

                if (error > std::max (kRelativeLimit * reference, kAbsoluteLimit))
                    totalInside = false;
            }

            const double best = bestFitCoefficient (rate);

            info ("    " + pad (rateName (rate), 11)
                  + pad (fx (1000.0 * worst, 3) + " ms at " + fx (atHz, 0) + " Hz", 30)
                  + pad (fx (1000.0 * worstTotal, 3) + " ms at " + fx (totalHz, 0) + " Hz", 30)
                  + pad (fx (best, 6), 26) + fx (1000.0 * worstDelayError (best, rate), 3) + " ms");

            info ("      pipeline delay differs from 48 kHz by " + fx (1000.0 * pipeline, 3) + " ms at every frequency");

            check (inside, "S1 " + rateName (rate) + ": the chain's group delay is within the limit, 100 Hz - 10 kHz");

            if (! totalInside)
                info ("      FINDING: with the pipeline delay added the total is OUTSIDE the same limit at " + rateName (rate));
        }
    }

    // --- S6: the pipeline delay, in the whole wet path ------------------------------
    info ("");
    info ("  S6. PV3's wet path against the plain chain's, modulation off: the same response,");
    info ("  exactly 15 samples later.");
    info ("");

    for (double rate : kRates)
    {
        User u;
        u.modDepth = 0.0;

        const std::vector<double> one (1, 1.0);
        const auto length = static_cast<size_t> (0.5 * rate);

        auto a = std::make_unique<Rig> (platePlain (rate), u, rate);
        auto b = std::make_unique<Rig> (platePV3 (rate), u, rate);

        const auto plain = a->render (one, one, length);
        const auto piped = b->render (one, one, length);

        double worst = 0.0, peak = 0.0;

        for (size_t n = 0; n + kPipelineSamples < length; ++n)
        {
            peak = std::max (peak, std::abs (plain.l[n]));
            worst = std::max (worst, std::max (std::abs (piped.l[n + kPipelineSamples] - plain.l[n]),
                                               std::abs (piped.r[n + kPipelineSamples] - plain.r[n])));
        }

        info ("    " + pad (rateName (rate), 11) + "15 samples = " + fx (1000.0 * kPipelineSamples / rate, 3)
              + " ms; worst difference / peak " + fx (worst / std::max (1.0e-300, peak) * 1.0e12, 3) + " e-12");

        check (worst <= 1.0e-9 * peak, "S6 " + rateName (rate) + ": the wet path is the plain chain's, 15 samples later");
    }

    // --- S0, S3, S4: Hall and Plate at every rate, analysed at 48 kHz ---------------
    info ("");
    info ("  S0 (control), S3, S4. Each response is rendered at its rate, band-limited to 20 kHz,");
    info ("  resampled to 48 kHz and read by the frozen metric at the same knobs.");
    info ("    S0  Hall at the rate against Hall at 48 kHz: <= 1.5, or the rig is not valid there");
    info ("        and S3 - S5 at that rate are NOT READ.");
    info ("    S3  Plate at the rate against Plate at 48 kHz: <= 1.5.");
    info ("    S4  RT60: Plate / Hall at the rate within 10 %; Plate at the rate / at 48 kHz within 5 %.");
    info ("");

    // [rate][structure: 0 Hall, 1 Plate][point]
    std::vector<Reading> readings (4 * 2 * kPointCount);

    runParallel (readings.size(), [&] (size_t i)
    {
        const size_t rate = i / (2 * kPointCount), which = (i / kPointCount) % 2, point = i % kPointCount;

        readings[i] = readingAt (which == 0 ? Structure {} : platePV3 (kRates[rate]),
                                 userAt (kPoints[point]), kRates[rate]);
    });

    auto at = [&] (size_t rate, size_t which, size_t point) -> const Reading&
    {
        return readings[(rate * 2 + which) * kPointCount + point];
    };

    constexpr size_t kReferenceIndex = 1;

    {
        // What the 20 kHz band limit alone does to the metric: Hall at 48 kHz
        // through to48k() against Hall as the metric normally sees it.
        std::string line = "    the band limit alone (Hall, 48 kHz, filtered against unfiltered):";

        for (size_t p = 0; p < kPointCount; ++p)
            line += " " + fx (score (featuresOf (Structure {}, userAt (kPoints[p])), at (kReferenceIndex, 0, p).features));

        info (line);

        line = "    Plate against Hall at 48 kHz, both filtered (Stage 6 same-knob figure, for scale):";

        for (size_t p = 0; p < kPointCount; ++p)
            line += " " + fx (score (at (kReferenceIndex, 0, p).features, at (kReferenceIndex, 1, p).features));

        info (line);
        info ("");
    }

    info ("    " + pad ("rate", 11) + pad ("S0 Hall  med sma lar", 26) + pad ("S3 Plate  med sma lar", 26)
          + pad ("RT Plate/Hall", 22) + "RT Plate/Plate48");

    bool valid[4] { true, true, true, true };

    for (size_t r = 0; r < 4; ++r)
    {
        if (r == kReferenceIndex)
            continue;

        std::string s0, s3, rtHall, rt48;
        bool s0Pass = true, s3Pass = true, s4Pass = true;

        for (size_t p = 0; p < kPointCount; ++p)
        {
            int family = 0;

            const double hall = score (at (kReferenceIndex, 0, p).features, at (r, 0, p).features, &family);
            s0 += fx (hall) + " ";

            if (hall > kReproduced)
            {
                s0Pass = false;
                s0 += std::string ("(") + kFamilyNames[family] + ") ";
            }

            const double plate = score (at (kReferenceIndex, 1, p).features, at (r, 1, p).features, &family);
            s3 += fx (plate) + " ";

            if (plate > kReproduced)
            {
                s3Pass = false;
                s3 += std::string ("(") + kFamilyNames[family] + ") ";
            }

            const double a = at (r, 1, p).rt60 / at (r, 0, p).rt60;
            const double b = at (r, 1, p).rt60 / at (kReferenceIndex, 1, p).rt60;

            rtHall += fx (a, 3) + " ";
            rt48 += fx (b, 3) + " ";

            if (std::abs (a - 1.0) > 0.10 || std::abs (b - 1.0) > 0.05)
                s4Pass = false;
        }

        info ("    " + pad (rateName (kRates[r]), 11) + pad (s0, 26) + pad (s3, 26) + pad (rtHall, 22) + rt48);

        valid[r] = s0Pass;

        check (s0Pass, "S0 " + rateName (kRates[r]) + ": Hall at this rate is Hall at 48 kHz (the rig is valid here)");

        if (s0Pass)
        {
            check (s3Pass, "S3 " + rateName (kRates[r]) + ": Plate at this rate is the 48 kHz Plate");
            check (s4Pass, "S4 " + rateName (kRates[r]) + ": Decay holds");
        }
        else
        {
            info ("      S3 and S4 at " + rateName (kRates[r]) + " are NOT READ: the control failed.");
        }
    }

    // --- S5: Size ---------------------------------------------------------------------
    info ("");
    info ("  S5. Time to 10 % of the energy, ms, at Size 10 / 30 / 50 / 70 / 90. Monotone, and the");
    info ("  ratio largest / smallest within 10 % of the 48 kHz ratio.");
    info ("");

    constexpr double sizes[5] { 0.10, 0.30, 0.50, 0.70, 0.90 };

    std::vector<double> t10 (4 * 5);

    runParallel (t10.size(), [&] (size_t i)
    {
        User u;
        u.size = sizes[i % 5];

        t10[i] = readingAt (platePV3 (kRates[i / 5]), u, kRates[i / 5]).features.base.t10;
    });

    const double referenceRatio = t10[kReferenceIndex * 5 + 4] / t10[kReferenceIndex * 5];

    for (size_t r = 0; r < 4; ++r)
    {
        std::string line = "    " + pad (rateName (kRates[r]), 11);
        bool monotone = true;

        for (size_t i = 0; i < 5; ++i)
        {
            line += pad (fx (t10[r * 5 + i], 1), 8);

            if (i > 0 && t10[r * 5 + i] < t10[r * 5 + i - 1] - 0.05)
                monotone = false;
        }

        const double ratio = t10[r * 5 + 4] / t10[r * 5];

        info (line + "ratio " + fx (ratio, 3));

        if (r == kReferenceIndex)
            continue;

        if (valid[r])
            check (monotone && std::abs (ratio / referenceRatio - 1.0) <= 0.10,
                   "S5 " + rateName (kRates[r]) + ": Size is monotone and spans what it spans at 48 kHz");
        else
            info ("      S5 at " + rateName (kRates[r]) + " is NOT READ: the control failed.");
    }

    // --- S7: CPU ----------------------------------------------------------------------
    info ("");
    info ("  S7. CPU, seven interleaved pairs at the medium point, each rate. Limit: the median");
    info ("  no more than 0.15 above the 48 kHz median measured here, and under 1.75. A failure");
    info ("  calls for the full 31-pair campaign at that rate. Valid only on an idle machine.");
    info ("  " + pinAndPrioritise() + ".");
    info ("");

    statisticNote (7);
    header();

    Campaign cpu[4];

    for (size_t r = 0; r < 4; ++r)
    {
        cpu[r] = campaign (platePV3 (kRates[r]), User {}, kRates[r], 7, 2.0);
        info (row (rateName (kRates[r]), cpu[r]));
    }

    info ("");

    for (size_t r = 0; r < 4; ++r)
        if (r != kReferenceIndex)
            check (cpu[r].stats.median <= cpu[kReferenceIndex].stats.median + 0.15 && cpu[r].stats.median < kLimit,
                   "S7 " + rateName (kRates[r]) + ": no rate-specific CPU regression");
}

} // namespace avtest
