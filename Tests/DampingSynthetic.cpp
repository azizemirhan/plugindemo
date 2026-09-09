#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/*
    PHASE 7 — synthetic physics control for the T20/T30 curvature.

    THE QUESTION
    ------------
    The ensemble gate reports 6.00 % at High Damp 100 %, 12 kHz. That number is
    converged, half-ensemble precise to 0.90 %, and agrees with the canonical
    impulse — so it is a real property of the damped tank, not estimator noise.
    But R^2 is 0.99741, so the tail is not broken either.

    What remains unknown is whether a T20/T30 difference of that size is simply
    what happens when RT60 varies inside the analysis band, or whether the tank
    adds curvature of its own on top of that.

    This file answers it with a control that has NO feedback path, NO filter and
    NO tank: a dense sum of independently and perfectly exponentially decaying
    sinusoids, put through the identical analyser and the identical ensemble
    estimator.

    NOT CIRCULAR
    ------------
    The decay law is taken from the DSP model, never from the measurement. The
    RT60 given to each synthetic mode is the analytic value the current damping
    response predicts at that frequency — base Decay, the per-line shelf
    response, Option A normalisation, the current control settings — computed
    without reference to the observed 6.00 % or to any T20/T30 figure. Nothing
    here is tuned until it matches.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kEnsemble = 32;
constexpr double kGateBandOctaves = 1.0;

/*
    Modal density of the tank, from its total delay. This is a property of the
    delay set, not something fitted: an FDN's mode count per Hz equals the sum
    of its loop delays in seconds.
*/
[[nodiscard]] inline double tankModalDensity() noexcept
{
    double total = 0.0;

    for (int i = 0; i < 8; ++i)
        total += fdn::delaysMs[static_cast<size_t> (i)] * 0.001;

    return total;   // 0.3885 modes per Hz, i.e. 2.574 Hz spacing
}

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
//  The analytic decay law, taken from the DSP model
// ---------------------------------------------------------------------------

struct DecayLaw
{
    FdnCore<8> core;
    double decay = kDecayTarget;

    DecayLaw (double lowControl, double highControl, double lowHz, double highHz)
    {
        core.prepare (kSampleRate, 200.0 * fdn::maxSizeScale);
        (void) core.setDelaysMilliseconds (fdn::delaysMs, 1.0);
        core.setRt60Seconds (decay, true);

        if (lowControl > 0.0 || highControl > 0.0)
            core.setDamping (mapping::lowDampRatioFor (lowControl),
                             mapping::highDampRatioFor (highControl),
                             lowHz, highHz, true);
    }

    /** RT60 the damping response predicts at one frequency, averaged over the
        eight lines. The per-line spread is reported separately so the average
        is not passed off as an exact quantity. */
    [[nodiscard]] double rt60At (double freqHz, double* spreadPercent = nullptr) const
    {
        double sum = 0.0, lowest = 1.0e9, highest = 0.0;

        for (int i = 0; i < 8; ++i)
        {
            const auto& spec = core.getDampingSpec (i);
            const double norm = core.getDampingNormalisation (i);
            const double magnitude = damping::magnitudeAt (spec, freqHz, kSampleRate) * norm;
            const double d = static_cast<double> (core.getDelaySamples (i)) / kSampleRate;
            const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / d;
            const double rt = 1.0 / (1.0 / decay + k / 3.0);

            sum += rt;
            lowest = std::min (lowest, rt);
            highest = std::max (highest, rt);
        }

        const double mean = sum / 8.0;

        if (spreadPercent != nullptr)
            *spreadPercent = (highest - lowest) / std::max (highest, 1.0e-12) * 100.0;

        return mean;
    }
};

// ---------------------------------------------------------------------------
//  Synthetic modal mixture
// ---------------------------------------------------------------------------

enum class Spacing { uniform, jittered };

struct Mixture
{
    std::vector<double> frequency;
    std::vector<double> rt60;
    int modes = 0;
    double achievedDensity = 0.0;
    double lowHz = 0.0, highHz = 0.0;
};

/**
    Modes covering the analyser's passband with margin.

    The composite band is 3 dB down an octave apart and about 25 dB down two
    octaves from centre, so energy more than 1.8 octaves away contributes under
    -50 dB in the energy domain. The span below covers that with room to spare.
*/
[[nodiscard]] Mixture buildMixture (double centreHz, double density, Spacing spacing,
                                    const DecayLaw* law, double flatRt60)
{
    Mixture m;

    m.lowHz  = std::max (20.0, centreHz / 3.5);
    m.highHz = std::min (centreHz * 3.5, 0.49 * kSampleRate);

    const double spacingHz = 1.0 / density;

    unsigned int rng = 1234567891u;

    double f = m.lowHz;

    while (f < m.highHz)
    {
        m.frequency.push_back (f);
        m.rt60.push_back (law != nullptr ? law->rt60At (f) : flatRt60);

        double step = spacingHz;

        if (spacing == Spacing::jittered)
        {
            // Deterministic jitter of +/-40 % about the mean spacing. The
            // amount is fixed here and is NOT adjusted from any result.
            rng = rng * 1664525u + 1013904223u;
            const double u = static_cast<double> (rng >> 8) / 16777216.0;
            step = spacingHz * (0.6 + 0.8 * u);
        }

        f += step;
    }

    m.modes = static_cast<int> (m.frequency.size());
    m.achievedDensity = m.modes / std::max (m.highHz - m.lowHz, 1.0e-9);

    return m;
}

/**
    One realisation: the same modes and the same decay rates, different phases.

    Rendered with a recursive complex rotation rather than a per-sample sine, so
    a dense mixture stays affordable. Amplitude decay is folded into the
    rotation radius, which makes every mode an exact exponential by
    construction.
*/
[[nodiscard]] std::vector<double> renderMixture (const Mixture& m, int realisation,
                                                 int numSamples)
{
    std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

    unsigned int rng = 2654435761u * static_cast<unsigned int> (realisation + 1) + 22222u;

    for (int k = 0; k < m.modes; ++k)
    {
        rng = rng * 1664525u + 1013904223u;

        const double phase = 2.0 * decay::pi
                             * (static_cast<double> (rng >> 8) / 16777216.0);

        const double w = 2.0 * decay::pi * m.frequency[static_cast<size_t> (k)] / kSampleRate;

        // amplitude rate: |a| = 10^(-3 t / RT60)
        const double radius = std::pow (10.0, -3.0 / (m.rt60[static_cast<size_t> (k)]
                                                      * kSampleRate));

        const double cr = radius * std::cos (w);
        const double ci = radius * std::sin (w);

        double zr = std::cos (phase);
        double zi = std::sin (phase);

        for (int n = 0; n < numSamples; ++n)
        {
            out[static_cast<size_t> (n)] += zi;

            const double nr = zr * cr - zi * ci;
            zi = zr * ci + zi * cr;
            zr = nr;
        }
    }

    return out;
}

/** Same estimator philosophy as the tank: normalised linear EDCs, averaged. */
[[nodiscard]] decay::Result ensembleOf (const Mixture& m, const decay::Band& band,
                                        double rt60Hint, int members = kEnsemble)
{
    const int length = static_cast<int> (kSampleRate * std::max (1.0, rt60Hint * 1.6));

    std::vector<double> accumulator (static_cast<size_t> (length), 0.0);

    int counted = 0;

    for (int r = 0; r < members; ++r)
    {
        auto signal = renderMixture (m, r, length);
        band.apply (signal);

        const auto edc = decay::normalisedEdc (signal);

        if (edc.empty())
            continue;

        for (size_t i = 0; i < edc.size(); ++i)
            accumulator[i] += edc[i];

        ++counted;
    }

    if (counted == 0)
        return {};

    const double k = 1.0 / static_cast<double> (counted);

    for (auto& e : accumulator)
        e *= k;

    return decay::analyseEdc (accumulator, band, kSampleRate);
}

// Aurora's measured ensemble figures, quoted from the test output so the
// comparison is against what was actually produced.
struct Reference { double hz; double mismatchPercent; double r2; double rt60; };

const Reference kAurora[]
{
    {  1000.0, 1.27, 0.99975, 2.333 },
    {  3000.0, 2.55, 0.99965, 1.750 },
    {  6000.0, 4.22, 0.99815, 1.248 },
    { 12000.0, 6.00, 0.99741, 0.816 },
};

} // namespace

// ===========================================================================

void runDampingSyntheticTests()
{
    const double density = tankModalDensity();

    const DecayLaw damped (0.0, 1.0, 200.0, 6000.0);   // High Damp 100 %

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 synthetic - the decay law, taken from the DSP model");

    {
        info ("  RT60(f) below comes from base Decay " + number (kDecayTarget, 2)
              + " s, the per-line shelf response,");
        info ("  Option A normalisation and High Damp 100 % / Low Damp 0 %,");
        info ("  200 Hz / 6 kHz. It is computed from the model and is not fitted");
        info ("  to any measured T20/T30 figure.");
        info ("");
        info ("    freq      analytic RT60   per-line spread   Aurora measured RT60");

        for (const auto& ref : kAurora)
        {
            double spread = 0.0;
            const double rt = damped.rt60At (ref.hz, &spread);

            info ("    " + pad (number (ref.hz, 0), 10)
                  + pad (number (rt, 4) + " s", 16)
                  + pad (number (spread, 2) + " %", 18)
                  + number (ref.rt60, 3) + " s");
        }

        info ("");
        info ("  modal density from the delay set: " + number (density, 4)
              + " modes/Hz  (" + number (1.0 / density, 3) + " Hz spacing)");

        // The analytic law should land near the measured RT60 without having
        // been fitted to it. If it did not, the control would be modelling
        // something other than this tank.
        double worstError = 0.0;

        for (const auto& ref : kAurora)
            worstError = std::max (worstError,
                                   std::abs (damped.rt60At (ref.hz) - ref.rt60)
                                   / ref.rt60);

        info ("  worst disagreement between the analytic law and the measured RT60: "
              + number (worstError * 100.0, 2) + " %");

        checkAtMost (worstError, 0.15,
                     "the analytic decay law describes the tank it is standing in for");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 synthetic - S0 flat, S1 matched gradient, S2 base decay");

    {
        info ("  S0  every mode at the RT60 the damped tank has AT the band centre");
        info ("  S1  every mode at the analytic RT60 for its own frequency");
        info ("  S2  every mode at the base Decay, 2.5 s");
        info ("");
        info ("    freq      control   modes    T20     T30     RT60    R^2      |T20-T30|   Aurora");

        for (const auto& ref : kAurora)
        {
            const auto band = decay::makeBand (ref.hz, kGateBandOctaves, kSampleRate);

            const double centreRt60 = damped.rt60At (ref.hz);

            struct Case { const char* label; const DecayLaw* law; double flat; double hint; };

            const Case cases[]
            {
                { "S0 flat", nullptr, centreRt60, centreRt60 },
                { "S1 grad", &damped, 0.0, centreRt60 },
                { "S2 base", nullptr, kDecayTarget, kDecayTarget },
            };

            for (const auto& c : cases)
            {
                const auto mixture = buildMixture (ref.hz, density, Spacing::uniform,
                                                   c.law, c.flat);

                const auto result = ensembleOf (mixture, band, c.hint);

                std::string row = "    " + pad (number (ref.hz, 0), 10)
                                  + pad (c.label, 10)
                                  + pad (std::to_string (mixture.modes), 9);

                if (! result.valid)
                {
                    info (row + "INVALID - " + result.reason);
                    continue;
                }

                info (row + pad (number (result.t20, 3), 8)
                      + pad (number (result.t30, 3), 8)
                      + pad (number (result.fitted, 3), 8)
                      + pad (number (result.r2, 5), 9)
                      + pad (number (result.mismatch() * 100.0, 2) + " %", 12)
                      + number (ref.mismatchPercent, 2) + " %");
            }

            info ("");
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 synthetic - excess curvature and classification");

    {
        /*
            Decision rule, written before the numbers were produced:

              EXPECTED SPECTRAL-DECAY MIXTURE
                  S1 rises with frequency, S1 at 12 kHz is within 1.0
                  percentage point of Aurora's 6.00 %, and the flat controls
                  stay clearly lower.

              REAL ENGINE RESIDUAL
                  S1 stays at or below 3 % while Aurora stays near 6 %.

              STILL UNRESOLVED
                  anything in between.
        */
        info ("    freq      Aurora    S1 matched   excess curvature   S0 flat   S2 base");

        double s1AtCritical = 0.0, s0AtCritical = 0.0, s2AtCritical = 0.0;
        bool risesWithFrequency = true;
        double previousS1 = -1.0;
        double worstFlat = 0.0;

        for (const auto& ref : kAurora)
        {
            const auto band = decay::makeBand (ref.hz, kGateBandOctaves, kSampleRate);
            const double centreRt60 = damped.rt60At (ref.hz);

            const auto s1 = ensembleOf (buildMixture (ref.hz, density, Spacing::uniform,
                                                      &damped, 0.0),
                                        band, centreRt60);

            const auto s0 = ensembleOf (buildMixture (ref.hz, density, Spacing::uniform,
                                                      nullptr, centreRt60),
                                        band, centreRt60);

            const auto s2 = ensembleOf (buildMixture (ref.hz, density, Spacing::uniform,
                                                      nullptr, kDecayTarget),
                                        band, kDecayTarget);

            const double s1m = s1.valid ? s1.mismatch() * 100.0 : 0.0;
            const double s0m = s0.valid ? s0.mismatch() * 100.0 : 0.0;
            const double s2m = s2.valid ? s2.mismatch() * 100.0 : 0.0;

            const double excess = ref.mismatchPercent - s1m;

            info ("    " + pad (number (ref.hz, 0), 10)
                  + pad (number (ref.mismatchPercent, 2) + " %", 10)
                  + pad (number (s1m, 2) + " %", 13)
                  + pad (number (excess, 2) + " pp", 19)
                  + pad (number (s0m, 2) + " %", 10)
                  + number (s2m, 2) + " %");

            if (previousS1 >= 0.0 && s1m < previousS1 - 0.5)
                risesWithFrequency = false;

            previousS1 = s1m;
            worstFlat = std::max ({ worstFlat, s0m, s2m });

            if (ref.hz >= 12000.0)
            {
                s1AtCritical = s1m;
                s0AtCritical = s0m;
                s2AtCritical = s2m;
            }
        }

        const double criticalGap = std::abs (s1AtCritical - 6.00);

        info ("");
        info ("  S1 rises with frequency:            " + std::string (risesWithFrequency ? "yes" : "no"));
        info ("  S1 at 12 kHz:                       " + number (s1AtCritical, 2) + " %");
        info ("  distance from Aurora's 6.00 %:      " + number (criticalGap, 2) + " pp");
        info ("  flat controls at 12 kHz:            S0 " + number (s0AtCritical, 2)
              + " %, S2 " + number (s2AtCritical, 2) + " %");
        info ("  worst flat control anywhere:        " + number (worstFlat, 2) + " %");
        info ("");

        const bool expectedMixture = risesWithFrequency
                                     && criticalGap <= 1.0
                                     && worstFlat < s1AtCritical - 1.0;

        const bool engineResidual = (s1AtCritical <= 3.0);

        if (expectedMixture)
        {
            info ("  CLASSIFICATION: EXPECTED SPECTRAL-DECAY MIXTURE");
            info ("  A mixture of perfectly exponential modes, with no feedback and");
            info ("  no filter, reproduces the tank's figure. T20 != T30 does not");
            info ("  imply a malformed decay envelope when RT60 varies inside the");
            info ("  analysis band on purpose.");
        }
        else if (engineResidual)
        {
            info ("  CLASSIFICATION: REAL ENGINE RESIDUAL");
            info ("  The spectral gradient alone does not account for the tank's");
            info ("  figure. The remainder belongs to the engine.");
        }
        else
        {
            info ("  CLASSIFICATION: STILL UNRESOLVED");
            info ("  S1 sits between the two pre-registered brackets.");
        }

        check (true, "classification recorded against the pre-registered rule");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 synthetic - spacing and density sensitivity");

    {
        const auto band = decay::makeBand (12000.0, kGateBandOctaves, kSampleRate);
        const double centreRt60 = damped.rt60At (12000.0);

        info ("  Critical case only: S1 at 12 kHz. Diagnostic, nothing is tuned.");
        info ("");
        info ("    arrangement                  modes    density     T20     T30     R^2      |T20-T30|");

        struct Variant { Spacing spacing; double scale; const char* label; };

        const Variant variants[]
        {
            { Spacing::uniform,  1.0, "uniform, 1.0x density" },
            { Spacing::jittered, 1.0, "jittered, 1.0x density" },
            { Spacing::uniform,  0.5, "uniform, 0.5x density" },
            { Spacing::uniform,  2.0, "uniform, 2.0x density" },
        };

        for (const auto& v : variants)
        {
            const auto mixture = buildMixture (12000.0, density * v.scale, v.spacing,
                                               &damped, 0.0);

            const auto result = ensembleOf (mixture, band, centreRt60);

            std::string row = "    " + pad (v.label, 29)
                              + pad (std::to_string (mixture.modes), 9)
                              + pad (number (mixture.achievedDensity, 4), 12);

            if (! result.valid)
            {
                info (row + "INVALID - " + result.reason);
                continue;
            }

            info (row + pad (number (result.t20, 3), 8)
                  + pad (number (result.t30, 3), 8)
                  + pad (number (result.r2, 5), 9)
                  + number (result.mismatch() * 100.0, 2) + " %");
        }

        info ("");
        info ("  If uniform and jittered agree, the result is not an artefact of");
        info ("  perfectly periodic synthetic spacing. If density barely matters,");
        info ("  the curvature belongs to the RT60 gradient rather than to modal");
        info ("  overlap.");
    }
}

} // namespace avtest
