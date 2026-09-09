#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — default-mapping and crossover-coherence checkpoint.

    Two findings from the Option A checkpoint are design decisions rather than
    defects, and this file is what turns them into numbers:

      1. At the shipping defaults the broad mid band moves further from Decay
         than the mid-anchored scheme's +/-2 % target allowed. Whether that is
         acceptable depends on how strong default damping should be, which is a
         product decision, so three control mappings are measured side by side
         WITHOUT changing the production mapping.

      2. Per-line K_i spread reaches 25 % around a crossover under heavy high
         damping. A per-line metric is not by itself a fault: the feedback
         matrix mixes the lines, and what a listener hears is the aggregate
         tail. So the aggregate decay is measured against a quality gate fixed
         before the results were looked at.

    Everything here is TEST ONLY. Nothing in Source/ is changed by this file,
    and the production mapping is used only as candidate A.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;

// ---------------------------------------------------------------------------
//  PRE-REGISTERED aggregate quality gate
// ---------------------------------------------------------------------------
/*
    Fixed before any aggregate decay was measured, and not to be adjusted after
    seeing them. These are aggregate-tail criteria; they say nothing about
    per-line K_i, which is the point of measuring them separately.
*/
constexpr double kGateMinimumR2         = 0.995;
constexpr double kGateMaxT20T30Mismatch = 0.05;   // 5 % of their mean

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string number (double v, int decimals = 4)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

[[nodiscard]] std::string pad (const std::string& s, size_t width)
{
    return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
}

/**
    Candidate control mapping: ratio(p) = r_min ^ (p^gamma).

    gamma = 1 is the production mapping. Larger gamma bends the curve so that
    the low end of the control does less, without moving either endpoint:
    p = 0 still gives exactly 1.0 and p = 1 still gives exactly r_min, for any
    gamma, so both the transparency guarantee and the full-scale target survive
    the choice.
*/
[[nodiscard]] double candidateRatio (double p, double rMin, double gamma) noexcept
{
    if (p <= 0.0) return 1.0;
    if (p >= 1.0) return rMin;

    return std::pow (rMin, std::pow (p, gamma));
}

struct Tank
{
    FdnCore<8> core;

    explicit Tank (double rt60, double sampleRate = kSampleRate)
    {
        core.prepare (sampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (rt60, true);
        core.reset();
    }

    void dampRatios (double lowRatio, double highRatio, double lowHz, double highHz)
    {
        core.setDamping (lowRatio, highRatio, lowHz, highHz, true);
    }

    /** Impulse response, optionally excited by a deterministic noise burst. */
    [[nodiscard]] std::vector<double> render (int numSamples, unsigned int seed)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[8] {};
        double state[8] {};

        // seed 0 is a bare impulse; the others are short deterministic bursts,
        // so a result does not rest on one excitation.
        const int burst = (seed == 0u) ? 1 : 64;
        unsigned int rng = seed * 1103515245u + 12345u;

        for (int n = 0; n < numSamples; ++n)
        {
            double drive = 0.0;

            if (n < burst)
            {
                if (seed == 0u)
                {
                    drive = 1.0;
                }
                else
                {
                    rng = rng * 1664525u + 1013904223u;
                    drive = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
                }
            }

            for (int i = 0; i < 8; ++i)
                injection[i] = drive * fdn::injectionLeft[i] * fdn::signVectorNorm;

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < 8; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }
};

/** Zero-phase one-octave bandpass, as in the main damping suite. */
void bandpass (std::vector<double>& x, double centreHz, double sampleRate)
{
    const double lowHz  = std::max (10.0, centreHz * 0.7071);
    const double highHz = std::min (centreHz * 1.4142, 0.45 * sampleRate);

    const auto onePoleHigh = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;
        for (auto& s : v) { z += (1.0 - a) * (s - z); s -= z; }
    };

    const auto onePoleLow = [&] (std::vector<double>& v, double fc)
    {
        const double a = std::exp (-2.0 * damping::pi * fc / sampleRate);
        double z = 0.0;
        for (auto& s : v) { z += (1.0 - a) * (s - z); s = z; }
    };

    for (int pass = 0; pass < 2; ++pass)
    {
        onePoleHigh (x, lowHz);
        onePoleHigh (x, lowHz);
        onePoleLow  (x, highHz);
        onePoleLow  (x, highHz);
        std::reverse (x.begin(), x.end());
    }
}

/** Everything the aggregate gate needs, from one band-limited response. */
struct DecayFit
{
    double edt = 0.0;      ///< 0 to -10 dB, extrapolated
    double t20 = 0.0;      ///< -5 to -25 dB
    double t30 = 0.0;      ///< -5 to -35 dB
    double fitted = 0.0;   ///< least squares over -5 to -35 dB
    double r2 = 0.0;       ///< of that fit
    double energyDb = 0.0; ///< total band energy, dB
    bool   valid = false;
};

[[nodiscard]] DecayFit analyseDecay (const std::vector<double>& ir, double sampleRate)
{
    const size_t n = ir.size();

    std::vector<double> energy (n + 1, 0.0);

    for (size_t i = n; i-- > 0;)
        energy[i] = energy[i + 1] + ir[i] * ir[i];

    DecayFit result;

    if (energy[0] <= 0.0)
        return result;

    result.energyDb = 10.0 * std::log10 (energy[0]);

    const double reference = energy[0];

    std::vector<double> curve (n, 0.0);

    for (size_t i = 0; i < n; ++i)
        curve[i] = 10.0 * std::log10 (std::max (energy[i] / reference, 1.0e-300));

    const auto timeAt = [&] (double dB) -> double
    {
        for (size_t i = 0; i < n; ++i)
            if (curve[i] <= dB)
                return static_cast<double> (i) / sampleRate;

        return -1.0;
    };

    const double t5  = timeAt (-5.0);
    const double t10 = timeAt (-10.0);
    const double t25 = timeAt (-25.0);
    const double t35 = timeAt (-35.0);

    if (t5 < 0.0 || t10 <= 0.0 || t25 <= t5 || t35 <= t5)
        return result;

    result.edt = t10 * 6.0;
    result.t20 = (t25 - t5) * 3.0;
    result.t30 = (t35 - t5) * 2.0;

    // Least-squares slope of the decay curve over the T30 range. A single
    // exponential decay is a straight line here; multi-slope behaviour is
    // exactly what a poor R^2 reports.
    const size_t first = static_cast<size_t> (t5 * sampleRate);
    const size_t last  = static_cast<size_t> (t35 * sampleRate);

    if (last <= first + 16)
        return result;

    const size_t stride = std::max<size_t> (1, (last - first) / 2000);

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0;
    double count = 0.0;

    for (size_t i = first; i <= last; i += stride)
    {
        const double x = static_cast<double> (i) / sampleRate;
        const double y = curve[i];

        sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
        count += 1.0;
    }

    const double denominator = count * sxx - sx * sx;

    if (std::abs (denominator) < 1.0e-12)
        return result;

    const double slope = (count * sxy - sx * sy) / denominator;

    if (slope >= 0.0)
        return result;

    result.fitted = -60.0 / slope;

    const double numerator = count * sxy - sx * sy;
    const double r = numerator / std::sqrt (denominator * (count * syy - sy * sy));

    result.r2 = r * r;
    result.valid = true;

    return result;
}

/** Aggregate decay of a tank in one band, averaged over deterministic seeds. */
struct BandResult
{
    DecayFit mean;
    double spreadFitted = 0.0;   ///< max-min of fitted RT60 across seeds
    int seeds = 0;
};

[[nodiscard]] BandResult measureBand (Tank& tank, double centreHz, double rt60Hint)
{
    const int length = static_cast<int> (std::ceil (kSampleRate
                                                    * std::max (0.5, rt60Hint * 2.5)));

    BandResult result;

    double minFitted = 1.0e9, maxFitted = 0.0;
    DecayFit sum;

    for (unsigned int seed : { 0u, 7u, 4242u })
    {
        auto ir = tank.render (length, seed);
        bandpass (ir, centreHz, kSampleRate);

        const auto fit = analyseDecay (ir, kSampleRate);

        if (! fit.valid)
            continue;

        sum.edt += fit.edt;
        sum.t20 += fit.t20;
        sum.t30 += fit.t30;
        sum.fitted += fit.fitted;
        sum.r2 += fit.r2;
        sum.energyDb += fit.energyDb;

        minFitted = std::min (minFitted, fit.fitted);
        maxFitted = std::max (maxFitted, fit.fitted);

        ++result.seeds;
    }

    if (result.seeds == 0)
        return result;

    const double k = 1.0 / static_cast<double> (result.seeds);

    result.mean.edt = sum.edt * k;
    result.mean.t20 = sum.t20 * k;
    result.mean.t30 = sum.t30 * k;
    result.mean.fitted = sum.fitted * k;
    result.mean.r2 = sum.r2 * k;
    result.mean.energyDb = sum.energyDb * k;
    result.mean.valid = true;
    result.spreadFitted = maxFitted - minFitted;

    return result;
}

} // namespace

// ===========================================================================

void runDampingCheckpointTests()
{
    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 checkpoint - candidate control mappings");

    {
        /*
            ratio(p) = r_min^(p^gamma). The endpoints are fixed for every gamma,
            so this bends the middle of the control without touching either the
            transparency guarantee or the full-scale target. The production
            mapping is gamma = 1 and is not being changed here.
        */
        struct Candidate { double gamma; const char* label; };

        const Candidate candidates[] { { 1.0, "A  gamma 1.0" },
                                       { 1.5, "B  gamma 1.5" },
                                       { 2.0, "C  gamma 2.0" } };

        info ("  Low Damp, r_min = " + number (damping::lowRatioAtFull, 3));
        info ("    mapping          20 % (default)      50 %          100 %");

        for (const auto& c : candidates)
        {
            info ("    " + pad (c.label, 16)
                  + pad (number (candidateRatio (0.20, damping::lowRatioAtFull, c.gamma), 4), 18)
                  + pad (number (candidateRatio (0.50, damping::lowRatioAtFull, c.gamma), 4), 14)
                  + number (candidateRatio (1.00, damping::lowRatioAtFull, c.gamma), 4));
        }

        info ("");
        info ("  High Damp, r_min = " + number (damping::highRatioAtFull, 3));
        info ("    mapping          50 % (default)      75 %          100 %");

        for (const auto& c : candidates)
        {
            info ("    " + pad (c.label, 16)
                  + pad (number (candidateRatio (0.50, damping::highRatioAtFull, c.gamma), 4), 18)
                  + pad (number (candidateRatio (0.75, damping::highRatioAtFull, c.gamma), 4), 14)
                  + number (candidateRatio (1.00, damping::highRatioAtFull, c.gamma), 4));
        }

        // The three properties every candidate must keep.
        for (const auto& c : candidates)
        {
            checkEqual (candidateRatio (0.0, damping::highRatioAtFull, c.gamma), 1.0,
                        std::string (c.label) + ": p = 0 is exactly ratio 1.0");

            checkEqual (candidateRatio (1.0, damping::highRatioAtFull, c.gamma),
                        damping::highRatioAtFull,
                        std::string (c.label) + ": p = 1 is exactly r_min");

            bool monotonic = true;
            double previous = 2.0;

            for (int i = 0; i <= 200; ++i)
            {
                const double r = candidateRatio (i * 0.005, damping::highRatioAtFull, c.gamma);

                if (r > previous + 1.0e-15)
                    monotonic = false;

                previous = r;
            }

            check (monotonic, std::string (c.label) + ": monotonic across the control");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 checkpoint - default character per mapping");

    {
        const double decay = 2.5;
        const double lowHz = 200.0, highHz = 6000.0;
        const double lowControl = 0.20, highControl = 0.50;

        const double bands[] { 80.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 6000.0, 12000.0 };

        // Damping OFF reference first: every tilt figure below is relative to
        // it, and the invariant is stated against its Decay target.
        std::array<double, 8> offRt {};
        std::array<double, 8> offEnergy {};

        {
            Tank tank (decay);

            info ("  damping OFF reference, Decay " + number (decay, 2) + " s");

            std::string line = "    RT60  ";

            for (size_t b = 0; b < 8; ++b)
            {
                const auto r = measureBand (tank, bands[b], decay);
                offRt[b] = r.mean.fitted;
                offEnergy[b] = r.mean.energyDb;

                line += pad (number (offRt[b], 3), 8);
            }

            info ("    band  " + pad ("80", 8) + pad ("250", 8) + pad ("500", 8)
                  + pad ("1k", 8) + pad ("2k", 8) + pad ("4k", 8) + pad ("6k", 8) + "12k");
            info (line);
        }

        struct Candidate { double gamma; const char* label; };

        const Candidate candidates[] { { 1.0, "A  gamma 1.0 (production)" },
                                       { 1.5, "B  gamma 1.5" },
                                       { 2.0, "C  gamma 2.0" } };

        for (const auto& c : candidates)
        {
            const double lowRatio  = candidateRatio (lowControl,  damping::lowRatioAtFull,  c.gamma);
            const double highRatio = candidateRatio (highControl, damping::highRatioAtFull, c.gamma);

            Tank tank (decay);
            tank.dampRatios (lowRatio, highRatio, lowHz, highHz);

            info ("");
            info ("  " + std::string (c.label)
                  + "   low ratio " + number (lowRatio, 4)
                  + ", high ratio " + number (highRatio, 4));

            std::string rtLine = "    RT60  ";
            std::string devLine = "    dev%  ";

            double longest = 0.0;
            double worstMid = 0.0;
            double highReduction = 1.0;
            double tiltHigh = 0.0, tiltLow = 0.0;

            for (size_t b = 0; b < 8; ++b)
            {
                const auto r = measureBand (tank, bands[b], decay);
                const double rt = r.mean.fitted;
                const double deviation = (rt / decay - 1.0) * 100.0;

                rtLine  += pad (number (rt, 3), 8);
                devLine += pad (number (deviation, 2), 8);

                longest = std::max (longest, rt);

                if (bands[b] >= 500.0 && bands[b] <= 2000.0)
                    worstMid = std::max (worstMid, std::abs (deviation));

                if (bands[b] >= 12000.0 && offRt[b] > 0.0)
                    highReduction = rt / offRt[b];

                if (bands[b] == 250.0)  tiltLow  = r.mean.energyDb - offEnergy[b];
                if (bands[b] == 12000.0) tiltHigh = r.mean.energyDb - offEnergy[b];
            }

            info ("    band  " + pad ("80", 8) + pad ("250", 8) + pad ("500", 8)
                  + pad ("1k", 8) + pad ("2k", 8) + pad ("4k", 8) + pad ("6k", 8) + "12k");
            info (rtLine);
            info (devLine);
            info ("    longest measured RT60        " + number (longest, 4) + " s"
                  + "   (Decay target " + number (decay, 2) + " s)");
            info ("    worst 500 Hz - 2 kHz deviation " + number (worstMid, 3) + " %");
            info ("    high-band decay reduction     " + number (highReduction, 4)
                  + "x at 12 kHz");
            info ("    spectral tilt vs OFF          "
                  + number (tiltHigh - tiltLow, 2) + " dB (12 kHz minus 250 Hz)");
            info ("    meets the original +/-2 % mid target: "
                  + std::string (worstMid <= 2.0 ? "YES" : "NO"));

            // The invariant holds whichever mapping is chosen; that is what
            // Option A guarantees and it is asserted for each candidate.
            checkAtMost (longest, decay * 1.02,
                         std::string (c.label) + ": no band exceeds the Decay target");
        }

        info ("");
        info ("  The +/-2 % target is reported, not enforced: it belonged to the");
        info ("  mid-anchored scheme. It has not been loosened, and it is allowed");
        info ("  for no candidate to meet it.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 checkpoint - line spread root cause");

    {
        /*
            Claim to confirm: the ASYMPTOTIC gains scale exactly with delay
            length, so plateau decay rates align; the TRANSITION shape does not,
            so crossover coherence cannot be exact.

            The shelf gain is built so that G_i = G_ref^(d_i/d_ref). Coherence at
            every frequency would need

                |H_i(w)| = |H_ref(w)|^t        t = d_i / d_ref

            At DC that is the definition of G_i, so it holds exactly. At any
            other frequency it is a different equation, and it does not.
        */
        const double lambdaCases[] { 0.2, 0.5, 0.8 };   // LP value away from DC
        const double ratios[] { 1.5, 2.0, 3.16 };       // d_i / d_ref
        const double cRef = 0.5;                        // reference shelf depth

        info ("  Low-shelf form H = 1 - c*LP.  Requiring G_i = G_ref^t fixes c_i,");
        info ("  then asks whether |H_i| = |H_ref|^t holds away from DC.");
        info ("");
        info ("    t       LP      required        actual        error");

        double worstError = 0.0;

        for (double t : ratios)
        {
            const double cI = 1.0 - std::pow (1.0 - cRef, t);

            for (double lambda : lambdaCases)
            {
                const double actual   = 1.0 - cI * lambda;
                const double required = std::pow (1.0 - cRef * lambda, t);

                const double error = std::abs (actual - required)
                                     / std::max (required, 1.0e-12) * 100.0;

                worstError = std::max (worstError, error);

                info ("    " + pad (number (t, 2), 8) + pad (number (lambda, 2), 8)
                      + pad (number (required, 6), 16) + pad (number (actual, 6), 14)
                      + number (error, 2) + " %");
            }
        }

        info ("");
        info ("  At DC (LP = 1) the two agree by construction; away from it they");
        info ("  do not, and the gap grows with both the delay ratio and the");
        info ("  shelf depth. That is the whole source of the crossover K_i");
        info ("  spread - it is a property of a first-order shelf, not an error");
        info ("  in the per-line derivation and not something a per-line cutoff");
        info ("  offset would fix without a derivation to justify it.");

        // Exactness at DC, inexactness away from it - both asserted, so the
        // claim is a test result rather than a paragraph.
        for (double t : ratios)
        {
            const double cI = 1.0 - std::pow (1.0 - cRef, t);

            checkClose ((1.0 - cI) / std::pow (1.0 - cRef, t), 1.0, 1.0e-12,
                        "delay ratio " + number (t, 2)
                        + ": the DC asymptote scales exactly");
        }

        check (worstError > 1.0,
               "away from DC the shape does not scale - confirmed, not assumed");

        info ("  worst shape mismatch measured here: " + number (worstError, 2) + " %");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 checkpoint - aggregate crossover decay (SUPERSEDED)");

    {
        /*
            This suite used to run the aggregate T20/T30 gate here, on a band
            filter built from one-pole highpasses and lowpasses. It reported
            8.50 % for High Damp 100 % at 12 kHz and that number drove two
            topology studies before it was traced to the filter rather than the
            tank: the one-pole skirt leaked into frequencies whose RT60 was
            three times longer, so the estimator was averaging decays that did
            not belong in the band.

            Measured on one identical rendered tail, changing only the filter:

                3 kHz     one-pole cascade  3.55 %      constant-Q  1.40 %
                6 kHz     one-pole cascade  6.57 %      constant-Q  0.86 %
                12 kHz    one-pole cascade  5.79 %      constant-Q  0.79 %

            The gate now lives in DampingGate.cpp on a constant-Q analyser whose
            achieved band edges are measured rather than assumed. Thresholds are
            unchanged; only the measurement was replaced.

            The old figure is deliberately left in the record here rather than
            deleted, marked for what it is.
        */
        info ("  SUPERSEDED. The aggregate gate moved to DampingGate.cpp after the");
        info ("  band filter used here was shown to be the source of its own");
        info ("  failure.");
        info ("");
        info ("  Historical result, retained: |T20-T30| = 8.50 % at High Damp 100 %,");
        info ("  12 kHz, one-octave band.");
        info ("  Status: OLD ANALYSER / INVALID FOR GATING.");
        info ("  Not evidence of an FDN decay-quality defect, and not to be cited");
        info ("  as one.");
    }
}

} // namespace avtest
