#pragma once

#include "Phase12Stage3Rig.h"

#include <complex>

/**
    PHASE 12 - STAGE 3. The augmented distinctness metric.

    PRE-REGISTERED. This file was committed, with its control results, before
    any Stage 3 structural candidate was scored. See the Stage 3 report for
    the commit. After that commit no scale, family, floor rule or threshold
    here may change in response to a candidate's result.

    WHY STAGE 2'S METRIC NEEDED REPLACING, in two parts.

    1. WHAT IT LOOKED AT. Its six families read the energy envelope, the echo
       density up to the moment it first saturates, broadband correlation, the
       decay time and one envelope periodicity. A tank with its mixing matrix
       removed matches all of those and was scored REPRODUCED. Nothing looked
       at the fine structure of the late field, at how the decay is shaped, at
       WHEN each frequency arrives, or at where a panned source's reverb goes.

    2. HOW IT ADDED UP. The total was the RMS over families. A structural
       difference lives in one family; every family that agrees dilutes it.
       With eleven families one family six units apart would total 1.8 and
       read as "nearly reproduced".

    WHAT IS ADDED. Seven families, each a quantity with a plain meaning:

      6  late texture     does the late field's short-time energy fluctuate
                          more than noise does? the spread, in dB, of 5 ms
                          energy against its 45 ms neighbourhood, in two
                          windows. Sparse or weakly mixed loops beat; a
                          well-mixed field does not
      7  loop repetition  does the tail repeat itself? the tallest
                          autocorrelation of the flattened late waveform, at
                          loop-scale lags (3-150 ms) and at long lags
                          (150-1000 ms), where a ring-like network returns
      8  modal structure  do single modes stand out? RMS loop-lag
                          autocorrelation against white noise; modal
                          prominence
      9  decay shape      is the decay one exponential? early decay time and
                          late slope, each against the mid slope
      10 band arrival     do all frequencies arrive together? the time each
                          of 250 Hz, 4 kHz and 8 kHz reaches 5 % of its first
                          300 ms of energy, against 1 kHz
      11 lateral spread   where does a hard-left source's reverb go, and how
                          fast does it cross? right/left output energy in
                          three windows, for a left-only impulse
      12 partial placement  is each partial of the tail placed somewhere
                          between the channels, or hard to the centre or hard
                          out of phase? the spread, across fine frequency
                          bins of the late tail, of the mid/side ratio, and
                          the share of bins more than 15 dB one way. This is
                          the mechanism behind Phase 11's narrow-band mono
                          finding: a network that does not mix leaves every
                          mode in one line, and the readout signs then place
                          it dead centre or dead opposite


    WHAT WAS TRIED AND REMOVED, on the controls, before the freeze. Echo
    density at 80, 160 and 320 ms: every network measured, including one with
    no mixing matrix at all, is at Gaussian density by 80 ms - the input
    diffuser and the in-loop allpasses see to it - so those features could
    only dilute their family. Band arrival as an energy CENTROID: a 19 ms
    dispersion moved it by under 3 ms, because a centroid over a fixed window
    of a slowly decaying field barely notices a shift. Both are recorded in
    the Stage 3 report.

    Every feature is a ratio
, a time or a correlation: LEVEL CANNOT ENTER.

    HOW IT IS SCORED.

      - Each family's distance is the RMS of its features in units of a
        just-noticeable difference where one is published, a stated proxy
        where none is.
      - Each family is then divided by max (1, its own same-family floor): the
        largest distance that family shows between things that are the same
        reverb by construction (Hall against two sibling delay sets and
        against itself with Size nudged, at three operating points). A noisy
        family therefore cannot manufacture a distinction.
      - THE VERDICT IS THE WORST FAMILY, not an average. A listener who hears
        one thing differ has heard a difference.

            REPRODUCED   worst family <= 1.5
            DISTINCT     worst family >= 3.0
            WEAK         in between

      - The impostor search minimises that same worst family (plus 5 % of the
        RMS, only so that it keeps improving the others once the worst is
        tied). The impostor is therefore judged by the number it was trying
        to minimise.
*/
namespace avtest::stage3
{

inline constexpr int kFamilies = 13;

inline constexpr const char* kFamilyNames[kFamilies]
{
    "buildup", "energy profile", "echo density", "stereo", "decay", "recurrence",
    "late texture", "loop repetition", "modal structure", "decay shape",
    "band arrival", "lateral spread", "partial placement"
};

inline constexpr double kReproduced = 1.5;
inline constexpr double kDistinct   = 3.0;

/** Long enough for the -35 dB point of the decay-shape family. */
[[nodiscard]] inline double irSecondsFor (double decay) noexcept
{
    return std::clamp (0.8 * decay + 0.9, 1.8, 6.0);
}

inline constexpr double kLeftOnlySeconds = 0.65;

struct Features
{
    mode12::Features base;

    double fluctuation[2] {};   // dB, 50-200 ms and 200-500 ms

    double lagPeak = 0.0, longLagPeak = 0.0;   // 3-150 ms, 150-1000 ms
    double lagRmsDb = 0.0, modalDb = 0.0;

    double placementSpreadDb = 0.0, placementHard = 0.0;

    double edtRatio = 1.0, lateRatio = 1.0;

    double arrival[3] {};       // ms to 5 %: 250 Hz, 4 kHz, 8 kHz, minus 1 kHz

    double lateral[3] {};       // dB right/left: 0-50, 50-200, 200-600 ms
};

namespace detail
{
    inline constexpr double kPi = 3.14159265358979323846;

    [[nodiscard]] inline std::vector<double> energyOfBoth (const Stereo& ir)
    {
        std::vector<double> e (ir.l.size());

        for (size_t i = 0; i < e.size(); ++i)
            e[i] = ir.l[i] * ir.l[i] + ir.r[i] * ir.r[i];

        return e;
    }

    /** Energy of both channels through one RBJ band-pass, Q = 1.4. */
    [[nodiscard]] inline std::vector<double> bandEnergy (const Stereo& ir, double hz, size_t length)
    {
        const double w0 = 2.0 * kPi * hz / kRate;
        const double alpha = std::sin (w0) / (2.0 * 1.4);
        const double a0 = 1.0 + alpha;

        const double b0 = alpha / a0, b2 = -alpha / a0;
        const double a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;

        length = std::min (length, ir.l.size());

        std::vector<double> e (length, 0.0);

        for (const auto* channel : { &ir.l, &ir.r })
        {
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                const double x = (*channel)[n];
                const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;

                x2 = x1; x1 = x;
                y2 = y1; y1 = y;

                e[n] += y * y;
            }
        }

        return e;
    }

    /** When the running energy reaches a fraction of its total, in ms. */
    [[nodiscard]] inline double onsetMs (const std::vector<double>& e, double fraction)
    {
        double total = 0.0;

        for (double v : e)
            total += v;

        double running = 0.0;

        for (size_t n = 0; n < e.size(); ++n)
        {
            running += e[n];

            if (running >= fraction * total)
                return static_cast<double> (n) * 1000.0 / kRate;
        }

        return 0.0;
    }
}

[[nodiscard]] inline Features analyse (const Stereo& centre, const Stereo& leftOnly)
{
    using namespace detail;

    Features f;

    f.base = mode12::analyse (centre);

    const size_t n = centre.l.size();
    const auto   e = energyOfBoth (centre);

    // --- 6: late texture ----------------------------------------------------
    {
        // 5 ms energy bins against their own 45 ms neighbourhood.
        const size_t bin  = msToSamples (5.0);
        const size_t bins = std::min<size_t> (104, n / bin);

        std::vector<double> env (bins, 0.0);

        for (size_t b = 0; b < bins; ++b)
            for (size_t i = 0; i < bin; ++i)
                env[b] += e[b * bin + i];

        constexpr size_t edge[3] { 10, 40, 100 };

        for (int k = 0; k < 2; ++k)
        {
            double sum = 0.0, sumSq = 0.0;
            int count = 0;

            for (size_t b = edge[k]; b < edge[k + 1] && b + 4 < bins; ++b)
            {
                double mean = 0.0;

                for (size_t j = b - 4; j <= b + 4; ++j)
                    mean += env[j];

                mean /= 9.0;

                if (mean <= 0.0 || env[b] <= 0.0)
                    continue;

                const double db = 10.0 * std::log10 (env[b] / mean);

                sum += db;
                sumSq += db * db;
                ++count;
            }

            if (count > 1)
                f.fluctuation[k] = std::sqrt (std::max (0.0, sumSq / count
                                                             - (sum / count) * (sum / count)));
        }
    }

    // --- 7, 8: loop repetition, modal structure --------------------------------------------------
    {
        constexpr size_t segment = 65536, modalSegment = 32768;

        const size_t start = msToSamples (200.0);
        const size_t half  = msToSamples (10.0);

        if (n >= start + segment + half)
        {
            // Flattened by a 20 ms moving RMS so the decay is not structure.
            std::vector<double> prefix (n + 1, 0.0);

            for (size_t i = 0; i < n; ++i)
                prefix[i + 1] = prefix[i] + centre.l[i] * centre.l[i];

            std::vector<std::complex<double>> x (2 * segment, 0.0);

            // Only as far as the tail is still 180 dB above nothing: past
            // that a short decay has left only the denormal guards, and
            // flattening would promote them to signal.
            const double first = std::sqrt ((prefix[start + half] - prefix[start - half])
                                            / static_cast<double> (2 * half));

            size_t valid = 0;

            for (size_t i = 0; i < segment; ++i)
            {
                const size_t at = start + i;
                const double local = std::sqrt ((prefix[at + half] - prefix[at - half])
                                                / static_cast<double> (2 * half));

                if (! (local > 1.0e-9 * first))
                    break;

                x[i] = centre.l[at] / local;
                valid = i + 1;
            }


            fft (x);

            for (auto& v : x)
                v = std::norm (v);

            ifft (x);

            const double zero = x[0].real();

            if (zero > 0.0 && valid > 0)
            {
                const size_t from = msToSamples (3.0), to = msToSamples (150.0);
                const size_t far  = msToSamples (1000.0);

                // Unbiased, and only out to half of what is valid: beyond
                // that too few products remain to call it an estimate.
                auto rhoAt = [&] (size_t lag)
                {
                    return 2 * lag <= valid ? x[lag].real() / zero * static_cast<double> (valid)
                                                  / static_cast<double> (valid - lag)
                                            : 0.0;
                };

                double peak = 0.0, sumSq = 0.0;

                for (size_t lag = from; lag <= to; ++lag)
                {
                    const double rho = rhoAt (lag);

                    peak = std::max (peak, std::abs (rho));
                    sumSq += rho * rho;
                }

                f.lagPeak = peak;

                for (size_t lag = to + 1; lag <= far; ++lag)
                    f.longLagPeak = std::max (f.longLagPeak, std::abs (rhoAt (lag)));


                // 0 dB is what white Gaussian noise of this length would give.
                f.lagRmsDb = 20.0 * std::log10 (std::max (1.0e-12,
                    std::sqrt (sumSq / static_cast<double> (to - from + 1))
                    * std::sqrt (static_cast<double> (valid))));
            }

            // Modal prominence: how far the 95th-percentile bin stands above
            // the median of its own third-octave neighbourhood, 300 Hz-3 kHz.
            std::vector<std::complex<double>> s (modalSegment);

            for (size_t i = 0; i < modalSegment; ++i)
                s[i] = centre.l[start + i]
                     * (0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / (modalSegment - 1)));

            fft (s);

            const size_t low  = binForFrequency (300.0,  modalSegment, kRate);
            const size_t high = binForFrequency (3000.0, modalSegment, kRate);

            std::vector<double> mag (high + 400);

            for (size_t k = 0; k < mag.size(); ++k)
                mag[k] = 20.0 * std::log10 (std::max (1.0e-30, std::abs (s[k])));

            std::vector<double> excess, window;

            for (size_t k = low; k < high; k += 3)
            {
                const size_t reach = std::max<size_t> (8, static_cast<size_t> (k * 0.115));

                window.assign (mag.begin() + static_cast<long long> (k - reach),
                               mag.begin() + static_cast<long long> (k + reach));

                std::nth_element (window.begin(), window.begin() + static_cast<long long> (reach),
                                  window.end());

                excess.push_back (mag[k] - window[reach]);
            }

            const auto rank = static_cast<long long> (excess.size() * 95 / 100);

            std::nth_element (excess.begin(), excess.begin() + rank, excess.end());

            f.modalDb = excess[static_cast<size_t> (rank)];

            // --- 12: partial placement ---------------------------------------
            // The same windowed segment of the right channel; then, bin by
            // bin, how the energy divides between mid and side.
            std::vector<std::complex<double>> t (modalSegment);

            for (size_t i = 0; i < modalSegment; ++i)
                t[i] = centre.r[start + i]
                     * (0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / (modalSegment - 1)));

            fft (t);

            std::vector<double> ratio;
            size_t hard = 0;

            for (size_t k = low; k < high; ++k)
            {
                const double mid  = std::norm (s[k] + t[k]);
                const double side = std::norm (s[k] - t[k]);

                const double db = std::clamp (10.0 * std::log10 (std::max (1.0e-300, mid)
                                                                 / std::max (1.0e-300, side)),
                                              -60.0, 60.0);

                ratio.push_back (db);

                if (std::abs (db) > 15.0)
                    ++hard;
            }

            if (ratio.size() > 20)
            {
                std::sort (ratio.begin(), ratio.end());

                f.placementSpreadDb = ratio[ratio.size() * 9 / 10] - ratio[ratio.size() / 10];
                f.placementHard     = static_cast<double> (hard) / static_cast<double> (ratio.size());
            }

        }
    }

    // --- 9: decay shape -----------------------------------------------------
    {
        std::vector<double> edc (n);
        double running = 0.0;

        for (size_t i = n; i-- > 0;)
        {
            running += e[i];
            edc[i] = running;
        }

        const double total = n > 0 ? edc[0] : 0.0;

        auto timeAt = [&] (double db)
        {
            const double level = total * std::pow (10.0, db / 10.0);

            for (size_t i = 0; i < n; ++i)
                if (edc[i] <= level)
                    return static_cast<double> (i) / kRate;

            return -1.0;
        };

        const double limit = 0.8 * static_cast<double> (n) / kRate;

        const double t1 = timeAt (-1.0),  t5  = timeAt (-5.0), t10 = timeAt (-10.0);
        const double t20 = timeAt (-20.0), t35 = timeAt (-35.0);

        if (total > 0.0 && t20 > t5 && t5 >= 0.0 && t20 < limit)
        {
            const double mid = t20 - t5;                       // 15 dB of decay

            if (t10 > t1 && t1 >= 0.0)
                f.edtRatio = ((t10 - t1) / 9.0) / (mid / 15.0);

            if (t35 > t20 && t35 < limit)
                f.lateRatio = (t35 - t20) / mid;
        }
    }

    // --- 10: band arrival ----------------------------------------------------
    {
        const size_t length = msToSamples (300.0);

        const double reference = onsetMs (bandEnergy (centre, 1000.0, length), 0.05);

        constexpr double band[3] { 250.0, 4000.0, 8000.0 };

        for (int k = 0; k < 3; ++k)
            f.arrival[k] = onsetMs (bandEnergy (centre, band[k], length), 0.05) - reference;
    }

    // --- 11: lateral spread -------------------------------------------------
    {
        constexpr double edge[4] { 0.0, 50.0, 200.0, 600.0 };

        for (int k = 0; k < 3; ++k)
        {
            const size_t a = std::min (leftOnly.l.size(), msToSamples (edge[k]));
            const size_t b = std::min (leftOnly.l.size(), msToSamples (edge[k + 1]));

            double ll = 0.0, rr = 0.0;

            for (size_t i = a; i < b; ++i)
            {
                ll += leftOnly.l[i] * leftOnly.l[i];
                rr += leftOnly.r[i] * leftOnly.r[i];
            }

            f.lateral[k] = std::clamp (10.0 * std::log10 (std::max (1.0e-300, rr)
                                                          / std::max (1.0e-300, ll)),
                                       -30.0, 30.0);
        }
    }

    return f;
}

[[nodiscard]] inline Features featuresOf (const Structure& s, const User& u)
{
    return analyse (impulseResponse (s, u, irSecondsFor (u.decay)),
                    impulseResponse (s, u, kLeftOnlySeconds, true, false));
}

// ===========================================================================
//  Distance and score
// ===========================================================================

struct Distance
{
    double family[kFamilies] {};
};

[[nodiscard]] inline Distance distance (const Features& a, const Features& b)
{
    auto rms = [] (std::initializer_list<double> v)
    {
        double sum = 0.0;

        for (double x : v)
            sum += x * x;

        return std::sqrt (sum / static_cast<double> (v.size()));
    };

    Distance d;

    // 0-5: Stage 2's families, with Stage 2's scales, unchanged.
    const auto old = mode12::distance (a.base, b.base);

    for (int k = 0; k < mode12::kFamilies; ++k)
        d.family[k] = old.family[k];

    // 6: amplitude modulation of a noise carrier is detected at a depth of
    // about 0.056 at low rates (Viemeister 1979), which is 0.34 dB of level
    // spread.
    d.family[6] = rms ({ (a.fluctuation[0] - b.fluctuation[0]) / 0.35,
                         (a.fluctuation[1] - b.fluctuation[1]) / 0.35 });

    // 7: proxy. The family floor calibrates it.
    d.family[7] = rms ({ (a.lagPeak - b.lagPeak) / 0.05,
                         (a.longLagPeak - b.longLagPeak) / 0.05 });

    // 8: proxies.
    d.family[8] = rms ({ (a.lagRmsDb - b.lagRmsDb) / 1.5,
                         (a.modalDb - b.modalDb) / 1.5 });

    // 9: ISO 3382-1 gives 5 % for the early decay time and for the
    // reverberation time; a ratio of two such quantities is held to 10 %.
    d.family[9] = rms ({ (a.edtRatio - b.edtRatio) / 0.10,
                         (a.lateRatio - b.lateRatio) / 0.10 });

    // 10: group-delay audibility on clicks is roughly 1-3 ms through the
    // mid band; inside a reverberant onset it is certainly coarser. 4 ms at
    // 250 Hz, 3 ms above. Proxies.
    d.family[10] = rms ({ (a.arrival[0] - b.arrival[0]) / 4.0,
                          (a.arrival[1] - b.arrival[1]) / 3.0,
                          (a.arrival[2] - b.arrival[2]) / 3.0 });

    // 11: an interaural level difference is noticed at about 1 dB; 1.5 dB
    // for a windowed estimate.
    d.family[11] = rms ({ (a.lateral[0] - b.lateral[0]) / 1.5,
                          (a.lateral[1] - b.lateral[1]) / 1.5,
                          (a.lateral[2] - b.lateral[2]) / 1.5 });

    // 12: proxies. Two independent Gaussian channels give a spread of 19 dB
    // and a hard share of 0.06 by chance alone.
    d.family[12] = rms ({ (a.placementSpreadDb - b.placementSpreadDb) / 3.0,
                          (a.placementHard - b.placementHard) / 0.05 });


    return d;
}

/** The same-family floor, one figure per family. Measured, never assumed. */
struct Floor
{
    double family[kFamilies] {};

    void absorb (const Distance& d) noexcept
    {
        for (int k = 0; k < kFamilies; ++k)
            family[k] = std::max (family[k], d.family[k]);
    }
};

struct Score
{
    double scaled[kFamilies] {};
    double worst = 0.0, rms = 0.0;
    int    worstFamily = 0;

    /** What the impostor search minimises. */
    [[nodiscard]] double objective() const noexcept { return worst + 0.05 * rms; }
};

[[nodiscard]] inline Score score (const Distance& d, const Floor& floor) noexcept
{
    Score s;

    double sumSq = 0.0;

    for (int k = 0; k < kFamilies; ++k)
    {
        s.scaled[k] = d.family[k] / std::max (1.0, floor.family[k]);
        sumSq += s.scaled[k] * s.scaled[k];

        if (s.scaled[k] > s.worst)
        {
            s.worst = s.scaled[k];
            s.worstFamily = k;
        }
    }

    s.rms = std::sqrt (sumSq / kFamilies);

    return s;
}

[[nodiscard]] inline const char* verdictOf (double worst) noexcept
{
    return worst <= kReproduced ? "REPRODUCED" : worst >= kDistinct ? "DISTINCT" : "WEAK";
}

} // namespace avtest::stage3
