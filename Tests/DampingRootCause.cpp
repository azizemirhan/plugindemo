#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — root cause and gate validity for the T20/T30 curvature.

    TEST ONLY. Nothing in Source/ is touched.

    The cascade study left two problems that must now be treated as
    independent until evidence says otherwise:

        PROBLEM A   a 6 kHz High Damp corner shortens 2 kHz too much
        PROBLEM B   |T20 - T30| reaches 8.5 % where RT60 changes fast with
                    frequency

    Cutting per-line K spread by 25x moved B by 0.4 points, so B's cause is
    somewhere else. This file asks what the aggregate gate is actually
    measuring before any higher-order topology is considered — because a
    steeper shelf would fix A by making the very gradient B tracks steeper
    still.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;

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

// ---------------------------------------------------------------------------
//  Narrow-band analysis
// ---------------------------------------------------------------------------
/*
    The one-pole cascade used so far cannot make a usable 1/48-octave band, so
    the narrow work uses a proper resonator: two RBJ constant-peak bandpass
    biquads, run forwards and then backwards, giving a zero-phase response with
    no pre-ring to confuse a decay measurement.

    Ringing is checked rather than assumed: a band of width B rings for about
    1/(pi B), which is reported next to the decay times it could contaminate.
*/
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    void filter (std::vector<double>& x) const noexcept
    {
        double z1 = 0.0, z2 = 0.0;

        for (auto& s : x)
        {
            const double in = s;
            const double out = b0 * in + z1;

            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;

            s = out;
        }
    }
};

[[nodiscard]] double qForFractionalOctave (double fraction) noexcept
{
    // fraction is the denominator: 6 means 1/6 octave.
    const double bandwidthOctaves = 1.0 / fraction;
    const double x = 0.5 * std::log (2.0) * bandwidthOctaves;

    return 1.0 / (2.0 * std::sinh (x));
}

[[nodiscard]] Biquad makeBandpass (double centreHz, double q, double sampleRate) noexcept
{
    const double w0 = 2.0 * damping::pi * centreHz / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    Biquad result;
    result.b0 =  alpha / a0;
    result.b1 =  0.0;
    result.b2 = -alpha / a0;
    result.a1 = (-2.0 * std::cos (w0)) / a0;
    result.a2 = (1.0 - alpha) / a0;

    return result;
}

void narrowBandpass (std::vector<double>& x, double centreHz, double fraction,
                     double sampleRate)
{
    const double q = qForFractionalOctave (fraction);
    const auto section = makeBandpass (centreHz, q, sampleRate);

    section.filter (x);
    section.filter (x);

    std::reverse (x.begin(), x.end());
    section.filter (x);
    section.filter (x);
    std::reverse (x.begin(), x.end());
}

// ---------------------------------------------------------------------------

struct DecayFit
{
    double edt = 0.0, t20 = 0.0, t30 = 0.0, fitted = 0.0, r2 = 0.0;
    bool valid = false;

    [[nodiscard]] double mismatch() const noexcept
    {
        const double mean = 0.5 * (t20 + t30);
        return mean > 0.0 ? std::abs (t20 - t30) / mean : 0.0;
    }
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

    std::vector<double> curve (n, 0.0);

    for (size_t i = 0; i < n; ++i)
        curve[i] = 10.0 * std::log10 (std::max (energy[i] / energy[0], 1.0e-300));

    const auto timeAt = [&] (double dB) -> double
    {
        for (size_t i = 0; i < n; ++i)
            if (curve[i] <= dB)
                return static_cast<double> (i) / sampleRate;

        return -1.0;
    };

    const double t5 = timeAt (-5.0), t10 = timeAt (-10.0);
    const double t25 = timeAt (-25.0), t35 = timeAt (-35.0);

    if (t5 < 0.0 || t10 <= 0.0 || t25 <= t5 || t35 <= t5)
        return result;

    result.edt = t10 * 6.0;
    result.t20 = (t25 - t5) * 3.0;
    result.t30 = (t35 - t5) * 2.0;

    const size_t first = static_cast<size_t> (t5 * sampleRate);
    const size_t last  = static_cast<size_t> (t35 * sampleRate);

    if (last <= first + 16)
        return result;

    const size_t stride = std::max<size_t> (1, (last - first) / 2000);

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0, count = 0.0;

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

    result.r2 = (numerator * numerator) / (denominator * (count * syy - sy * sy));
    result.valid = true;

    return result;
}

// ---------------------------------------------------------------------------
//  Single-frequency ring-down
// ---------------------------------------------------------------------------
/*
    A sliding Hann-windowed correlation against a complex exponential: the
    narrowest honest look at one frequency. The window length is stated with
    every result, because it sets both the frequency resolution and how much of
    the decay it smears.
*/
struct RingDown
{
    double rt60 = 0.0;
    double r2 = 0.0;
    bool valid = false;
};

[[nodiscard]] RingDown singleFrequencyDecay (const std::vector<double>& ir,
                                             double freqHz, double sampleRate,
                                             double windowSeconds)
{
    const int window = std::max (64, static_cast<int> (windowSeconds * sampleRate));
    const int hop = std::max (1, window / 8);
    const int frames = (static_cast<int> (ir.size()) - window) / hop;

    RingDown result;

    if (frames < 8)
        return result;

    std::vector<double> cosTable (static_cast<size_t> (window));
    std::vector<double> sinTable (static_cast<size_t> (window));
    std::vector<double> hann (static_cast<size_t> (window));

    double windowSum = 0.0;

    for (int n = 0; n < window; ++n)
    {
        const double phase = 2.0 * damping::pi * freqHz * n / sampleRate;
        const double w = 0.5 - 0.5 * std::cos (2.0 * damping::pi * n / (window - 1));

        cosTable[static_cast<size_t> (n)] = std::cos (phase) * w;
        sinTable[static_cast<size_t> (n)] = std::sin (phase) * w;
        hann[static_cast<size_t> (n)] = w;
        windowSum += w;
    }

    std::vector<double> level (static_cast<size_t> (frames), 0.0);

    for (int f = 0; f < frames; ++f)
    {
        double re = 0.0, im = 0.0;
        const int base = f * hop;

        for (int n = 0; n < window; ++n)
        {
            const double s = ir[static_cast<size_t> (base + n)];
            re += s * cosTable[static_cast<size_t> (n)];
            im -= s * sinTable[static_cast<size_t> (n)];
        }

        level[static_cast<size_t> (f)] =
            20.0 * std::log10 (std::max (std::sqrt (re * re + im * im) / windowSum, 1.0e-300));
    }

    const double peak = *std::max_element (level.begin(), level.end());

    // Fit the straight part: -5 to -35 dB below the envelope's own peak.
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0, count = 0.0;

    for (int f = 0; f < frames; ++f)
    {
        const double y = level[static_cast<size_t> (f)] - peak;

        if (y > -5.0 || y < -35.0)
            continue;

        const double x = static_cast<double> (f * hop) / sampleRate;

        sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
        count += 1.0;
    }

    if (count < 6.0)
        return result;

    const double denominator = count * sxx - sx * sx;

    if (std::abs (denominator) < 1.0e-12)
        return result;

    const double slope = (count * sxy - sx * sy) / denominator;

    if (slope >= 0.0)
        return result;

    result.rt60 = -60.0 / slope;

    const double numerator = count * sxy - sx * sy;

    result.r2 = (numerator * numerator) / (denominator * (count * syy - sy * sy));
    result.valid = true;

    return result;
}

// ---------------------------------------------------------------------------
//  Tank
// ---------------------------------------------------------------------------

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

    void damp (double lowControl, double highControl, double lowHz, double highHz)
    {
        core.setDamping (mapping::lowDampRatioFor (lowControl),
                         mapping::highDampRatioFor (highControl),
                         lowHz, highHz, true);
    }

    [[nodiscard]] std::vector<double> render (int numSamples)
    {
        core.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double injection[8] {};
        double state[8] {};

        for (int n = 0; n < numSamples; ++n)
        {
            for (int i = 0; i < 8; ++i)
                injection[i] = (n == 0) ? fdn::injectionLeft[i] * fdn::signVectorNorm : 0.0;

            core.processSample (injection, state);

            double sum = 0.0;
            for (int i = 0; i < 8; ++i)
                sum += state[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }

    /** Analytic RT60 at one frequency, from the line specs. Line 0 is enough:
        the per-line derivation makes the eight agree to well under a per cent
        in the plateaus, and this is used for gradients, not absolutes. */
    [[nodiscard]] double analyticRt60 (double freqHz, double decay) const
    {
        const auto& spec = core.getDampingSpec (0);
        const double norm = core.getDampingNormalisation (0);
        const double magnitude = damping::magnitudeAt (spec, freqHz, kSampleRate) * norm;
        const double d = static_cast<double> (core.getDelaySamples (0)) / kSampleRate;
        const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;

        return 1.0 / (1.0 / decay + k / 3.0);
    }
};

} // namespace

// ===========================================================================

void runDampingRootCauseTests()
{
    const double decay = 2.5;

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - modal overlap, is a single mode separable");

    {
        /*
            Before measuring individual modes, check whether they exist as
            separable objects at all. The tank's modal density is fixed by its
            total delay; a mode's bandwidth is fixed by its decay. When the
            second reaches the first, modes overlap and "the RT60 of one mode"
            stops being a well-posed measurement.
        */
        double totalDelay = 0.0;

        for (int i = 0; i < 8; ++i)
            totalDelay += fdn::delaysMs[static_cast<size_t> (i)] * 0.001;

        const double density = totalDelay;               // modes per Hz
        const double spacing = 1.0 / density;

        info ("  total delay        " + number (totalDelay * 1000.0, 2) + " ms");
        info ("  modal density      " + number (density, 4) + " modes/Hz");
        info ("  modal spacing      " + number (spacing, 3) + " Hz");
        info ("");
        info ("    RT60      mode bandwidth   overlap index M   separable?");

        for (double rt : { 2.5, 1.5, 0.76, 0.5 })
        {
            const double bandwidth = 2.2 / rt;
            const double overlap = density * bandwidth;

            info ("    " + pad (number (rt, 2) + " s", 10)
                  + pad (number (bandwidth, 3) + " Hz", 17)
                  + pad (number (overlap, 3), 18)
                  + (overlap >= 1.0 ? "NO - modes overlap" : "marginal"));
        }

        info ("");
        info ("  At the damped high band the tank runs at RT60 ~0.76 s, where a");
        info ("  mode is wider than the gap to its neighbour. Individual modal");
        info ("  RT60 is therefore not a well-posed quantity there, and any");
        info ("  method that claims to extract it is reporting the analysis");
        info ("  window instead. The narrow-band work below is framed as");
        info ("  single-FREQUENCY ring-down for that reason.");

        check (density * (2.2 / 0.76) >= 1.0,
               "modes overlap in the damped high band - stated, not assumed");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - T20/T30 against analysis bandwidth");

    {
        Tank tank (decay);
        tank.damp (0.0, 1.0, 200.0, 6000.0);

        const int length = static_cast<int> (kSampleRate * 8.0);
        const auto reference = tank.render (length);

        info ("  High Damp 100 %, Decay 2.5 s, 8 s render.");
        info ("");
        info ("    centre    band        BW      ring     EDT     T20     T30     R^2      |T20-T30|");

        for (double centre : { 3000.0, 12000.0 })
        {
            for (double fraction : { 6.0, 12.0, 24.0, 48.0 })
            {
                auto ir = reference;
                narrowBandpass (ir, centre, fraction, kSampleRate);

                const double bandwidth = centre * (std::pow (2.0, 0.5 / fraction)
                                                   - std::pow (2.0, -0.5 / fraction));
                const double ring = 1.0 / (damping::pi * std::max (bandwidth, 1.0e-9));

                const auto fit = analyseDecay (ir, kSampleRate);

                std::string row = "    " + pad (number (centre, 0), 10)
                                  + pad ("1/" + number (fraction, 0) + " oct", 12)
                                  + pad (number (bandwidth, 1) + " Hz", 8)
                                  + pad (number (ring * 1000.0, 1) + " ms", 9);

                if (! fit.valid)
                {
                    info (row + "(no usable decay)");
                    continue;
                }

                info (row + pad (number (fit.edt, 3), 8)
                      + pad (number (fit.t20, 3), 8)
                      + pad (number (fit.t30, 3), 8)
                      + pad (number (fit.r2, 5), 9)
                      + number (fit.mismatch() * 100.0, 2) + " %");
            }

            info ("");
        }

        info ("  Ring time is printed so it can be compared with the decay times:");
        info ("  a band whose own ring-down approached the tail would invalidate");
        info ("  its own measurement.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - single-frequency ring-down");

    {
        Tank tank (decay);
        tank.damp (0.0, 1.0, 200.0, 6000.0);

        const int length = static_cast<int> (kSampleRate * 8.0);
        const auto ir = tank.render (length);

        info ("  Sliding Hann correlation, window 120 ms (~33 Hz resolution).");
        info ("  Frequencies 4 Hz apart across a narrow neighbourhood.");
        info ("");

        for (double centre : { 3000.0, 12000.0 })
        {
            info ("    around " + number (centre, 0) + " Hz");
            info ("      freq       RT60      R^2");

            double minRt = 1.0e9, maxRt = 0.0, worstR2 = 1.0;
            int used = 0;

            for (int k = -4; k <= 4; ++k)
            {
                const double f = centre + 4.0 * k;

                const auto rd = singleFrequencyDecay (ir, f, kSampleRate, 0.120);

                if (! rd.valid)
                {
                    info ("      " + pad (number (f, 0), 11) + "(no usable decay)");
                    continue;
                }

                info ("      " + pad (number (f, 0), 11)
                      + pad (number (rd.rt60, 4) + " s", 10)
                      + number (rd.r2, 5));

                minRt = std::min (minRt, rd.rt60);
                maxRt = std::max (maxRt, rd.rt60);
                worstR2 = std::min (worstR2, rd.r2);
                ++used;
            }

            if (used > 0)
            {
                const double spread = (maxRt - minRt) / std::max (maxRt, 1.0e-12) * 100.0;

                info ("      spread across the neighbourhood " + number (spread, 2) + " %");
                info ("      worst single-frequency fit R^2  " + number (worstR2, 5));
            }

            info ("");
        }

        info ("  A high R^2 here means each frequency decays as one exponential.");
        info ("  If that holds while the octave band curves, the curvature is a");
        info ("  mixture of clean exponentials, not a bent one.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - synthetic exponential mixture control");

    {
        /*
            The decisive control. A sum of perfectly exponential decaying
            sinusoids, no feedback, no FDN, no filter of any kind — with RT60
            varying across frequency at the gradient Aurora shows around the
            transition. If the same pipeline reports T30 > T20 on this, then the
            curvature does not require a defective tank to explain it.
        */
        Tank tank (decay);
        tank.damp (0.0, 1.0, 200.0, 6000.0);

        const int length = static_cast<int> (kSampleRate * 8.0);
        const int modes = 240;

        info ("    centre    RT60 across the octave        T20     T30     R^2      |T20-T30|");

        for (double centre : { 3000.0, 12000.0 })
        {
            const double below  = tank.analyticRt60 (centre / 1.4142, decay);
            const double middle = tank.analyticRt60 (centre, decay);
            const double above  = tank.analyticRt60 (centre * 1.4142, decay);

            // A flat control at the same average decay, to show the pipeline
            // reports a clean exponential when there is no gradient to mix.
            for (int variant = 0; variant < 2; ++variant)
            {
                const bool graded = (variant == 0);

                std::vector<double> synthetic (static_cast<size_t> (length), 0.0);

                unsigned int rng = 20260824u;

                for (int m = 0; m < modes; ++m)
                {
                    const double t = static_cast<double> (m) / (modes - 1);
                    const double f = centre / 1.4142 * std::pow (2.0, t);

                    const double rt = graded ? tank.analyticRt60 (f, decay) : middle;
                    const double alpha = std::log (1.0e-3) / rt;

                    rng = rng * 1664525u + 1013904223u;
                    const double phase = 2.0 * damping::pi
                                         * (static_cast<double> (rng >> 8) / 16777216.0);

                    const double omega = 2.0 * damping::pi * f / kSampleRate;
                    const double decayStep = std::exp (alpha / kSampleRate);

                    double amplitude = 1.0;

                    for (int n = 0; n < length; ++n)
                    {
                        synthetic[static_cast<size_t> (n)] +=
                            amplitude * std::sin (omega * n + phase);

                        amplitude *= decayStep;

                        if (amplitude < 1.0e-30)
                            break;
                    }
                }

                auto band = synthetic;
                narrowBandpass (band, centre, 1.0, kSampleRate);

                const auto fit = analyseDecay (band, kSampleRate);

                std::string row = "    " + pad (number (centre, 0), 10);

                row += pad (graded
                            ? (number (below, 3) + " - " + number (above, 3) + " s")
                            : ("flat " + number (middle, 3) + " s"), 30);

                if (! fit.valid)
                {
                    info (row + "(no usable decay)");
                    continue;
                }

                info (row + pad (number (fit.t20, 3), 8)
                      + pad (number (fit.t30, 3), 8)
                      + pad (number (fit.r2, 5), 9)
                      + number (fit.mismatch() * 100.0, 2) + " %");
            }
        }

        info ("");
        info ("  Every component here is a perfect exponential and there is no");
        info ("  feedback path anywhere. Any curvature the pipeline reports on");
        info ("  the graded rows is produced by the mixture alone.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - curvature against the RT60 gradient");

    {
        Tank tank (decay);
        tank.damp (0.0, 1.0, 200.0, 6000.0);

        const int length = static_cast<int> (kSampleRate * 8.0);
        const auto ir = tank.render (length);

        info ("    freq      RT60      dRT60/dlog2(f)     |T20-T30|");

        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0, count = 0.0;

        for (double f : { 1000.0, 2000.0, 3000.0, 6000.0, 12000.0 })
        {
            const double below = tank.analyticRt60 (f / 1.1892, decay);   // -1/4 octave
            const double above = tank.analyticRt60 (f * 1.1892, decay);

            const double gradient = (above - below) / 0.5;   // per octave

            auto band = ir;
            narrowBandpass (band, f, 1.0, kSampleRate);

            const auto fit = analyseDecay (band, kSampleRate);

            if (! fit.valid)
                continue;

            const double mismatch = fit.mismatch() * 100.0;

            info ("    " + pad (number (f, 0), 10)
                  + pad (number (fit.fitted, 3) + " s", 10)
                  + pad (number (gradient, 4) + " s/oct", 19)
                  + number (mismatch, 2) + " %");

            const double x = std::abs (gradient);
            const double y = mismatch;

            sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
            count += 1.0;
        }

        if (count >= 3.0)
        {
            const double denominator = std::sqrt ((count * sxx - sx * sx)
                                                  * (count * syy - sy * sy));

            const double r = (denominator > 0.0)
                             ? (count * sxy - sx * sy) / denominator
                             : 0.0;

            info ("");
            info ("  correlation between |dRT60/dlog2(f)| and the mismatch: r = "
                  + number (r, 4) + "   (r^2 = " + number (r * r, 4) + ")");

            check (std::isfinite (r), "the correlation is computable");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - strong but locally flat damping");

    {
        /*
            Separates "strongly damped" from "damping changing fast with
            frequency". Two ways of reaching a short decay at one frequency:

              flat      Decay itself lowered, damping off - perfectly flat
              plateau   damping on, measured well above the corner where the
                        shelf has finished moving
              transition  damping on, measured in the middle of the shelf

            If only the last one curves, the curvature belongs to the gradient.
        */
        const double target = 0.76;   // roughly the damped high band

        info ("    configuration                 measured at   RT60     T20     T30     |T20-T30|");

        struct Row { const char* label; double centre; bool dampingOn; double decaySeconds; };

        const Row rows[]
        {
            { "flat: Decay lowered, damp off",  12000.0, false, target },
            { "flat: Decay lowered, damp off",   3000.0, false, target },
            { "plateau: damped, above corner",  20000.0, true,  decay },
            { "transition: damped, on skirt",    3000.0, true,  decay },
            { "transition: damped, on skirt",   12000.0, true,  decay },
        };

        for (const auto& row : rows)
        {
            Tank tank (row.decaySeconds);

            if (row.dampingOn)
                tank.damp (0.0, 1.0, 200.0, 6000.0);

            const int length = static_cast<int> (kSampleRate
                                                 * std::max (3.0, row.decaySeconds * 2.5));

            auto band = tank.render (length);
            narrowBandpass (band, row.centre, 1.0, kSampleRate);

            const auto fit = analyseDecay (band, kSampleRate);

            std::string line = "    " + pad (row.label, 30)
                               + pad (number (row.centre, 0) + " Hz", 14);

            if (! fit.valid)
            {
                info (line + "(no usable decay)");
                continue;
            }

            info (line + pad (number (fit.fitted, 3), 9)
                  + pad (number (fit.t20, 3), 8)
                  + pad (number (fit.t30, 3), 8)
                  + number (fit.mismatch() * 100.0, 2) + " %");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - the two band filters, side by side");

    {
        /*
            The 8.50 % that started this investigation was measured with the
            band filter the earlier suites used: two one-pole highpasses and two
            one-pole lowpasses, run filtfilt. That is a gentle, leaky shape, and
            at 12 kHz its skirt reaches far down into a region whose RT60 is
            three times longer.

            Same tank, same configuration, same decay analysis — only the band
            filter changes.
        */
        Tank tank (decay);
        tank.damp (0.0, 1.0, 200.0, 6000.0);

        const int length = static_cast<int> (kSampleRate * 8.0);
        const auto reference = tank.render (length);

        // The earlier filter, reproduced exactly.
        const auto legacyBand = [] (std::vector<double>& x, double centreHz, double sampleRate)
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
        };

        info ("    centre    filter                       T20     T30     R^2      |T20-T30|");

        for (double centre : { 3000.0, 6000.0, 12000.0 })
        {
            {
                auto band = reference;
                legacyBand (band, centre, kSampleRate);

                const auto fit = analyseDecay (band, kSampleRate);

                if (fit.valid)
                    info ("    " + pad (number (centre, 0), 10)
                          + pad ("one-pole cascade (earlier)", 29)
                          + pad (number (fit.t20, 3), 8)
                          + pad (number (fit.t30, 3), 8)
                          + pad (number (fit.r2, 5), 9)
                          + number (fit.mismatch() * 100.0, 2) + " %");
            }

            {
                auto band = reference;
                narrowBandpass (band, centre, 1.0, kSampleRate);

                const auto fit = analyseDecay (band, kSampleRate);

                if (fit.valid)
                    info ("    " + pad (number (centre, 0), 10)
                          + pad ("constant-Q biquad, 1 oct", 29)
                          + pad (number (fit.t20, 3), 8)
                          + pad (number (fit.t30, 3), 8)
                          + pad (number (fit.r2, 5), 9)
                          + number (fit.mismatch() * 100.0, 2) + " %");
            }
        }

        info ("");
        info ("  If the two columns disagree, the gate was reading the analysis");
        info ("  filter and the number it produced is not a property of the tank.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - what a steeper shelf would do");

    {
        /*
            Analytic, before anything is built. A steeper shelf is modelled as
            N identical sections sharing the SAME pole — the version that really
            does steepen, unlike the calibrated cascade — with the total plateau
            gain held at the production value.

            Two quantities matter and they pull against each other:
              A  attenuation at 2 kHz from a 6 kHz corner  (want less)
              B  the steepest |dRT60/dlog2(f)| anywhere    (want less)
        */
        Tank reference (decay);
        reference.damp (0.20, 0.50, 200.0, 6000.0);

        const auto& base = reference.core.getDampingSpec (0);
        const double d = static_cast<double> (reference.core.getDelaySamples (0)) / kSampleRate;

        info ("    order   RT60 at 2 kHz   dev from Decay   worst |dRT60/dlog2 f|   at");

        for (int order : { 1, 2, 3, 4 })
        {
            damping::ShelfSpec spec = base;

            // Same total plateau gain, same pole, more sections: the section
            // gain is the Nth root and the transition genuinely steepens.
            spec.gainLow  = std::pow (base.gainLow,  1.0 / order);
            spec.gainHigh = std::pow (base.gainHigh, 1.0 / order);

            // Peak normalisation for the cascade, from the same closed form.
            const auto bound = damping::compositeMaximum (spec);
            const double norm = 1.0 / std::pow (bound.upper, static_cast<double> (order));

            const auto rt60At = [&] (double f)
            {
                const double magnitude =
                    std::pow (damping::magnitudeAt (spec, f, kSampleRate),
                              static_cast<double> (order)) * norm;

                const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;

                return 1.0 / (1.0 / decay + k / 3.0);
            };

            const double at2k = rt60At (2000.0);

            double worstGradient = 0.0, worstAt = 0.0;

            for (int p = 0; p <= 400; ++p)
            {
                const double f = 100.0 * std::pow (200.0, p / 400.0);

                const double gradient = (rt60At (f * 1.1892) - rt60At (f / 1.1892)) / 0.5;

                if (std::abs (gradient) > std::abs (worstGradient))
                {
                    worstGradient = gradient;
                    worstAt = f;
                }
            }

            info ("    " + pad (std::to_string (order), 8)
                  + pad (number (at2k, 4) + " s", 16)
                  + pad (number ((at2k / decay - 1.0) * 100.0, 2) + " %", 17)
                  + pad (number (worstGradient, 4) + " s/oct", 23)
                  + number (worstAt, 0) + " Hz");
        }

        info ("");
        info ("  Cascading real poles at a fixed TOTAL gain does not steepen the");
        info ("  transition: each section has to be shallower by exactly as much");
        info ("  as the extra section adds, and the width is set by the one-pole");
        info ("  curve either way. So this table is not a model of a biquad");
        info ("  shelf - it is the reason a biquad is a genuinely different");
        info ("  thing rather than more of the same. The prediction below uses a");
        info ("  shelf slope model instead.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 root cause - higher-order prediction from slope");

    {
        /*
            An order-N shelf reaches at most 6N dB/octave in its transition, so
            a shelf of depth D dB completes in about D/(6N) octaves. That is the
            whole of Problem A and the whole of Problem B in one number, and it
            lets both be predicted without building the filter.

            From RT60(f) = 1 / (1/RT60_mid + K(f)/3) with K = -log10|H| / d,

                |dRT60 / dlog2 f|  =  RT60^2 / (3 d) * slope_dB_per_oct / 20

            which is LINEAR in the order. Selectivity improves as 1/N and the
            gradient worsens as N — the trade is exactly one for one.
        */
        Tank tank (decay);
        tank.damp (0.20, 0.50, 200.0, 6000.0);

        const double d = static_cast<double> (tank.core.getDelaySamples (0)) / kSampleRate;
        const auto& spec = tank.core.getDampingSpec (0);

        // Depth of the shipping-default high shelf, on line 0.
        const double depthDb = -20.0 * std::log10 (std::max (spec.gainHigh, 1.0e-12));

        const double octavesBelow = std::log2 (6000.0 / 2000.0);

        /*
            The shelf here is shallow — about 1 dB on line 0 — so its width is
            set by the pole, not by its depth. A depth/slope model would be
            wrong for it, so the width is measured off the actual response and
            the order scaling applied to that.
        */
        const auto shelfDb = [&] (double f)
        {
            return 20.0 * std::log10 (std::max (damping::magnitudeAt (spec, f, kSampleRate),
                                                1.0e-12));
        };

        const double top = shelfDb (100.0);
        const double bottom = shelfDb (22000.0);
        const double total = top - bottom;

        double f10 = 0.0, f90 = 0.0;

        for (int p = 0; p <= 2000; ++p)
        {
            const double f = 100.0 * std::pow (220.0, p / 2000.0);
            const double fraction = (top - shelfDb (f)) / std::max (total, 1.0e-12);

            if (f10 <= 0.0 && fraction >= 0.10) f10 = f;
            if (f90 <= 0.0 && fraction >= 0.90) f90 = f;
        }

        const double widthOctaves = (f10 > 0.0 && f90 > f10) ? std::log2 (f90 / f10) : 0.0;

        // The steepest gradient the production shelf actually produces, from
        // the measured curve rather than a model.
        double measuredWorst = 0.0, worstAt = 0.0;

        const double norm = tank.core.getDampingNormalisation (0);

        const auto rt60At = [&] (double f)
        {
            const double magnitude = damping::magnitudeAt (spec, f, kSampleRate) * norm;
            const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;

            return 1.0 / (1.0 / decay + k / 3.0);
        };

        for (int p = 0; p <= 800; ++p)
        {
            const double f = 100.0 * std::pow (200.0, p / 800.0);
            const double gradient = (rt60At (f * 1.1892) - rt60At (f / 1.1892)) / 0.5;

            if (std::abs (gradient) > std::abs (measuredWorst))
            {
                measuredWorst = gradient;
                worstAt = f;
            }
        }

        info ("  Shipping default, line 0: high shelf total change "
              + number (total, 3) + " dB, corner 6 kHz.");
        info ("  Measured 10-90 % transition width: " + number (widthOctaves, 2)
              + " octaves (" + number (f10, 0) + " Hz to " + number (f90, 0) + " Hz).");
        info ("  2 kHz sits " + number (octavesBelow, 3) + " octaves below the corner,");
        info ("  i.e. INSIDE that transition — which is Problem A.");
        info ("");
        info ("  Measured worst |dRT60/dlog2 f| for this first-order shelf: "
              + number (measuredWorst, 4) + " s/oct at " + number (worstAt, 0) + " Hz.");
        info ("");
        info ("    order   predicted width   is 2 kHz outside it   predicted worst gradient");

        for (int order : { 1, 2, 3, 4 })
        {
            const double width = widthOctaves / order;
            const bool outside = (0.5 * width) < octavesBelow;

            info ("    " + pad (std::to_string (order), 8)
                  + pad (number (width, 2) + " oct", 18)
                  + pad (outside ? "yes" : "no", 22)
                  + number (measuredWorst * order, 4) + " s/oct");
        }

        info ("");
        info ("  PREDICTION. An order-N shelf compresses the same total dB change");
        info ("  into 1/N of the log-frequency span, so the gradient through it");
        info ("  scales as N. Second order is enough to put 2 kHz outside the");
        info ("  transition and solve Problem A — and it doubles the steepest");
        info ("  dRT60/dlog2 f while doing so. The two are the same parameter");
        info ("  read in opposite directions; no order setting trades one away.");
        info ("");
        info ("  Whether that matters depends on what Problem B turns out to be.");
        info ("  If the curvature is a property of the analysis filter rather");
        info ("  than of the tank, a steeper gradient costs nothing measurable.");
    }
}

} // namespace avtest
