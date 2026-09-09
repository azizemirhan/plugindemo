#include "DecayAnalyzer.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DampingTuning.h"
#include "DSP/FdnDelayLine.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulatedFeedbackMatrix.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

/*
    PHASE 7 — damper-spec / delay-line permutation. TEST ONLY.

    THE INTERVENTION
    ----------------
    Production derives eight damping responses D0..D7, one per line, each from
    that line's own loop delay. This experiment keeps the multiset {D0..D7}
    exactly as production computed it — same coefficients, same normalisation,
    nothing recomputed — and changes only which delay line receives which
    response:

        production      L_i  <-  D_i
        permutation     L_i  <-  D_p(i)

    That is the cleanest available intervention on the attenuation-versus-loop-
    delay relationship, which is precisely what per-line coherence is. Equal-gain
    would change the set of responses themselves and is therefore a blunter tool;
    it is held back as a fallback.

    WHY IT IS NOT CIRCULAR
    ----------------------
    Permutations are chosen on K spread alone, then screened on ensemble fitted
    RT60 alone, using a screening excitation bank that shares no seed with the
    final validation bank. T20, T30, their mismatch and R^2 are never read until
    the candidate set is closed.
*/

namespace avtest
{
namespace
{

using namespace av::dsp;

constexpr double kSampleRate = 48000.0;
constexpr double kDecayTarget = 2.5;
constexpr int    kLines = 8;
constexpr double kGateBandOctaves = 1.0;

// PRE-REGISTERED, fixed before the run.
constexpr double kMaterialDropPp   = 1.5;
constexpr double kImmaterialDropPp = 0.5;
constexpr double kScreenCritical   = 0.010;   // 1 % at 6 and 12 kHz
constexpr double kScreenOuter      = 0.020;   // 2 % at 1 and 3 kHz

constexpr int kScreenMembers = 8;
constexpr int kValidateMembers = 32;

// Disjoint excitation banks: screening never touches a validation seed.
constexpr int kScreenSeedBase = 1000;
constexpr int kValidateSeedBase = 0;

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

using Permutation = std::array<int, kLines>;

[[nodiscard]] std::string describe (const Permutation& p)
{
    std::string s;

    for (int i = 0; i < kLines; ++i)
        s += std::to_string (p[static_cast<size_t> (i)]);

    return s;
}

// ---------------------------------------------------------------------------
//  A complete damping response, moved as a unit
// ---------------------------------------------------------------------------
/*
    Everything that determines the spectral response lives in here: both shelf
    gains, both pole coefficients and the Option A normalisation. Nothing is
    recomputed from the receiving delay line, which is the whole point — if any
    part were re-derived on assignment, the experiment would be permuting
    something other than the response.
*/
struct DamperSpec
{
    damping::ShelfSpec shelf {};
    double normalisation = 1.0;

    [[nodiscard]] double magnitudeAt (double freqHz) const noexcept
    {
        return damping::magnitudeAt (shelf, freqHz, kSampleRate) * normalisation;
    }
};

/** The eight responses production derives for a given configuration. */
[[nodiscard]] std::array<DamperSpec, kLines> productionSpecs (double lowControl,
                                                              double highControl,
                                                              double lowHz, double highHz,
                                                              const std::array<double, kLines>& delays)
{
    std::array<DamperSpec, kLines> specs {};

    const double lowRatio  = mapping::lowDampRatioFor (lowControl);
    const double highRatio = mapping::highDampRatioFor (highControl);

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const double d = delays[idx];

        auto& shelf = specs[idx].shelf;

        shelf.gainLow   = damping::shelfGainFor (d, kDecayTarget, lowRatio);
        shelf.gainHigh  = damping::shelfGainFor (d, kDecayTarget, highRatio);
        shelf.coeffLow  = damping::onePoleCoefficient (lowHz,  kSampleRate);
        shelf.coeffHigh = damping::onePoleCoefficient (highHz, kSampleRate);

        specs[idx].normalisation = damping::normalisationFor (shelf);
    }

    return specs;
}

class Damper
{
public:
    void configure (const DamperSpec& spec) noexcept
    {
        cLow  = 1.0 - spec.shelf.gainLow;
        cHigh = 1.0 - spec.shelf.gainHigh;
        bLow  = 1.0 - spec.shelf.coeffLow;
        bHigh = 1.0 - spec.shelf.coeffHigh;
        norm  = spec.normalisation;
        transparent = spec.shelf.isTransparent();

        reset();
    }

    void reset() noexcept { zLow = 0.0; zHigh = 0.0; }

    [[nodiscard]] double process (double x) noexcept
    {
        if (transparent)
            return x;

        zLow += bLow * (x - zLow);
        const double afterLow = x - cLow * zLow;

        zHigh += bHigh * (afterLow - zHigh);

        return (afterLow - cHigh * (afterLow - zHigh)) * norm;
    }

private:
    double zLow = 0.0, zHigh = 0.0;
    double cLow = 0.0, cHigh = 0.0, bLow = 0.0, bHigh = 0.0, norm = 1.0;
    bool transparent = true;
};

class Tank
{
public:
    Tank()
    {
        const int capacity = static_cast<int> (std::ceil (200.0 * fdn::maxSizeScale
                                                          * 0.001 * kSampleRate)) + 2;
        matrix.prepare (kSampleRate);

        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[static_cast<size_t> (i)];
            line.prepare (capacity);

            const int samples = static_cast<int> (std::lround (
                fdn::delaysMs[static_cast<size_t> (i)] * 0.001 * kSampleRate));

            (void) line.setDelaySamples (samples);

            delays[static_cast<size_t> (i)] = static_cast<double> (samples) / kSampleRate;

            gains[static_cast<size_t> (i)] = std::min (
                fdn::maxFeedbackGain,
                fdn::feedbackGainForRt60 (delays[static_cast<size_t> (i)], kDecayTarget));
        }
    }

    [[nodiscard]] const std::array<double, kLines>& delaySeconds() const noexcept
    {
        return delays;
    }

    /** Line i receives spec p(i), complete and unmodified. */
    void install (const std::array<DamperSpec, kLines>& specs, const Permutation& p)
    {
        for (int i = 0; i < kLines; ++i)
        {
            assigned[static_cast<size_t> (i)] = specs[static_cast<size_t> (p[static_cast<size_t> (i)])];
            dampers[static_cast<size_t> (i)].configure (assigned[static_cast<size_t> (i)]);
        }
    }

    [[nodiscard]] std::vector<double> render (const std::vector<double>& drive, int numSamples)
    {
        for (auto& l : lines) l.reset();
        for (auto& d : dampers) d.reset();

        std::vector<double> out (static_cast<size_t> (numSamples), 0.0);

        double v[kLines] {};

        for (int n = 0; n < numSamples; ++n)
        {
            const double x = (n < static_cast<int> (drive.size()))
                             ? drive[static_cast<size_t> (n)] : 0.0;

            double sum = 0.0;

            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<size_t> (i);
                const double damped = dampers[idx].process (lines[idx].read());

                v[i] = damped * gains[idx];
                sum += v[i] * fdn::readoutLeft[i] * fdn::signVectorNorm;
            }

            matrix.process (v);

            for (int i = 0; i < kLines; ++i)
                lines[static_cast<size_t> (i)].write (
                    v[i] + x * fdn::injectionLeft[i] * fdn::signVectorNorm);

            out[static_cast<size_t> (n)] = sum;
        }

        return out;
    }

private:
    std::array<FdnDelayLine, kLines> lines;
    std::array<Damper, kLines> dampers;
    std::array<DamperSpec, kLines> assigned {};
    std::array<double, kLines> gains {}, delays {};

    ModulatedFeedbackMatrix<kLines> matrix;
};

// ---------------------------------------------------------------------------
//  K spread, purely from the specs and the delays
// ---------------------------------------------------------------------------

[[nodiscard]] double kSpreadFor (const std::array<DamperSpec, kLines>& specs,
                                 const Permutation& p,
                                 const std::array<double, kLines>& delays,
                                 double freqHz)
{
    double lowest = 1.0e9, highest = 0.0;

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const double magnitude =
            specs[static_cast<size_t> (p[idx])].magnitudeAt (freqHz);

        const double k = -std::log10 (std::max (magnitude, 1.0e-300)) / delays[idx];
        const double rt = 1.0 / (1.0 / kDecayTarget + k / 3.0);

        lowest = std::min (lowest, rt);
        highest = std::max (highest, rt);
    }

    return (highest - lowest) / std::max (highest, 1.0e-12) * 100.0;
}

// ---------------------------------------------------------------------------
//  Excitations
// ---------------------------------------------------------------------------

inline constexpr int kBurstSamples = 1024;

[[nodiscard]] std::vector<double> makeExcitation (int index)
{
    std::vector<double> burst (static_cast<size_t> (kBurstSamples), 0.0);

    unsigned int rng = 2654435761u * static_cast<unsigned int> (index + 1) + 1013904223u;

    double sumSquares = 0.0;

    for (int n = 0; n < kBurstSamples; ++n)
    {
        rng = rng * 1664525u + 1013904223u;

        const double white = static_cast<double> (rng >> 8) / 8388608.0 - 1.0;
        const double window = 0.5 - 0.5 * std::cos (2.0 * decay::pi * n / (kBurstSamples - 1));

        burst[static_cast<size_t> (n)] = white * window;
        sumSquares += burst[static_cast<size_t> (n)] * burst[static_cast<size_t> (n)];
    }

    const double rms = std::sqrt (sumSquares / kBurstSamples);

    for (auto& s : burst)
        s /= std::max (rms, 1.0e-30);

    return burst;
}

[[nodiscard]] decay::Result ensembleOf (Tank& tank, const decay::Band& band,
                                        double rt60Hint, int members, int seedBase)
{
    const int length = static_cast<int> (kSampleRate * std::max (1.5, rt60Hint * 2.2));

    std::vector<double> accumulator (static_cast<size_t> (length), 0.0);

    int counted = 0;

    for (int k = 0; k < members; ++k)
    {
        auto ir = tank.render (makeExcitation (seedBase + k), length);
        band.apply (ir);

        const auto edc = decay::normalisedEdc (ir);

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

const double kProbes[] { 1000.0, 3000.0, 6000.0, 12000.0 };

struct Named { Permutation p; std::string label; };

} // namespace

// ===========================================================================

void runDampingPermutationTests()
{
    Tank reference;
    const auto delays = reference.delaySeconds();

    const auto specs = productionSpecs (0.0, 1.0, 200.0, 6000.0, delays);

    Permutation identity {};
    std::iota (identity.begin(), identity.end(), 0);

    std::vector<decay::Band> bands;

    for (double f : kProbes)
        bands.push_back (decay::makeBand (f, kGateBandOctaves, kSampleRate));

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 permutation - K spread across all 8! assignments");

    Permutation maxSpread = identity, minSpread = identity;
    double identityCritical = 0.0;

    {
        info ("  High Damp 100 %, 200 Hz / 6 kHz, Decay 2.5 s.");
        info ("  The eight damping responses are production's own; only their");
        info ("  assignment to delay lines varies. No audio is rendered here.");
        info ("");

        identityCritical = kSpreadFor (specs, identity, delays, 12000.0);

        double lowest = 1.0e9, highest = 0.0;
        double sum = 0.0;
        long long count = 0, belowIdentity = 0;

        Permutation p = identity;

        do
        {
            const double spread = kSpreadFor (specs, p, delays, 12000.0);

            if (spread < lowest)  { lowest = spread;  minSpread = p; }
            if (spread > highest) { highest = spread; maxSpread = p; }

            if (spread < identityCritical - 1.0e-9)
                ++belowIdentity;

            sum += spread;
            ++count;
        }
        while (std::next_permutation (p.begin(), p.end()));

        info ("    permutations enumerated      " + std::to_string (count));
        info ("    12 kHz K spread, minimum     " + number (lowest, 2) + " %   ("
              + describe (minSpread) + ")");
        info ("    12 kHz K spread, maximum     " + number (highest, 2) + " %   ("
              + describe (maxSpread) + ")");
        info ("    12 kHz K spread, mean        " + number (sum / static_cast<double> (count), 2)
              + " %");
        info ("");
        info ("    production identity          " + number (identityCritical, 2) + " %");
        info ("    permutations below identity  " + std::to_string (belowIdentity)
              + " of " + std::to_string (count));
        info ("");
        info ("    band      identity   minimum    maximum");

        for (size_t b = 0; b < 4; ++b)
            info ("    " + pad (number (kProbes[b], 0), 10)
                  + pad (number (kSpreadFor (specs, identity, delays, kProbes[b]), 2) + " %", 11)
                  + pad (number (kSpreadFor (specs, minSpread, delays, kProbes[b]), 2) + " %", 11)
                  + number (kSpreadFor (specs, maxSpread, delays, kProbes[b]), 2) + " %");

        checkEqual (count, 40320LL, "all 8! assignments were enumerated");

        check (highest > identityCritical * 1.5,
               "the permutation method can create a substantially larger K spread");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 permutation - candidate pool, chosen on K spread only");

    std::vector<Named> pool;

    {
        pool.push_back ({ identity, "identity (production T0)" });

        Permutation reversed = identity;
        std::reverse (reversed.begin(), reversed.end());
        pool.push_back ({ reversed, "reverse" });

        Permutation cyclic {};
        for (int i = 0; i < kLines; ++i)
            cyclic[static_cast<size_t> (i)] = (i + 3) % kLines;
        pool.push_back ({ cyclic, "cyclic +3" });

        pool.push_back ({ maxSpread, "near-maximum K spread" });
        pool.push_back ({ minSpread, "minimum K spread" });

        // Deterministic randoms, from a stated recurrence.
        unsigned int rng = 20260824u;

        for (int r = 0; r < 4; ++r)
        {
            Permutation p = identity;

            for (int i = kLines - 1; i > 0; --i)
            {
                rng = rng * 1664525u + 1013904223u;
                const int j = static_cast<int> ((rng >> 8) % static_cast<unsigned int> (i + 1));
                std::swap (p[static_cast<size_t> (i)], p[static_cast<size_t> (j)]);
            }

            pool.push_back ({ p, "random " + std::to_string (r) });
        }

        info ("  Selection here uses K spread only. No decay-curvature metric has");
        info ("  been evaluated for any candidate at this point.");
        info ("");
        info ("    permutation   label                      1 kHz    3 kHz    6 kHz    12 kHz");

        for (const auto& c : pool)
        {
            std::string row = "    " + pad (describe (c.p), 14) + pad (c.label, 27);

            for (double f : kProbes)
                row += pad (number (kSpreadFor (specs, c.p, delays, f), 2) + " %", 9);

            info (row);
        }
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 permutation - RT60 screening on a disjoint bank");

    std::vector<Named> matched;
    std::array<double, 4> t0Rt60 {};

    {
        info ("  Screening uses " + std::to_string (kScreenMembers)
              + " excitations, seeds " + std::to_string (kScreenSeedBase) + "..."
              + std::to_string (kScreenSeedBase + kScreenMembers - 1) + ".");
        info ("  Validation later uses seeds " + std::to_string (kValidateSeedBase)
              + "..." + std::to_string (kValidateSeedBase + kValidateMembers - 1)
              + " — no overlap.");
        info ("  Only ensemble fitted RT60 is read here.");
        info ("");

        Tank tank;
        tank.install (specs, identity);

        for (size_t b = 0; b < 4; ++b)
        {
            const auto r = ensembleOf (tank, bands[b], 2.5, kScreenMembers, kScreenSeedBase);
            t0Rt60[b] = r.valid ? r.fitted : 0.0;
        }

        info ("    T0 screening RT60: " + number (t0Rt60[0], 4) + " / "
              + number (t0Rt60[1], 4) + " / " + number (t0Rt60[2], 4) + " / "
              + number (t0Rt60[3], 4) + " s");
        info ("");
        info ("    permutation   1 kHz err  3 kHz err  6 kHz err  12 kHz err   verdict");

        for (const auto& c : pool)
        {
            if (c.label.rfind ("identity", 0) == 0)
                continue;

            Tank candidate;
            candidate.install (specs, c.p);

            std::array<double, 4> error {};
            bool usable = true;

            for (size_t b = 0; b < 4; ++b)
            {
                const auto r = ensembleOf (candidate, bands[b], 2.5, kScreenMembers,
                                           kScreenSeedBase);

                if (! r.valid || t0Rt60[b] <= 0.0)
                {
                    usable = false;
                    break;
                }

                error[b] = r.fitted / t0Rt60[b] - 1.0;
            }

            if (! usable)
            {
                info ("    " + pad (describe (c.p), 14) + "no usable decay");
                continue;
            }

            const bool passes = std::abs (error[2]) <= kScreenCritical
                                && std::abs (error[3]) <= kScreenCritical
                                && std::abs (error[0]) <= kScreenOuter
                                && std::abs (error[1]) <= kScreenOuter;

            info ("    " + pad (describe (c.p), 14)
                  + pad (number (error[0] * 100.0, 2) + " %", 11)
                  + pad (number (error[1] * 100.0, 2) + " %", 11)
                  + pad (number (error[2] * 100.0, 2) + " %", 11)
                  + pad (number (error[3] * 100.0, 2) + " %", 13)
                  + (passes ? "MATCHED" : "rejected"));

            if (passes)
                matched.push_back (c);
        }

        info ("");
        info ("  RT60-matched permutations found: " + std::to_string (matched.size()));
        info ("  No filter gain or frequency was altered; only the assignment.");
    }

    // -----------------------------------------------------------------------

    beginSuite ("PHASE 7 permutation - independent validation and verdict");

    {
        if (matched.empty())
        {
            info ("  No permutation met the RT60 matching limits on the screening");
            info ("  bank, so there is no valid matched control to validate.");
            info ("");
            info ("  CLASSIFICATION: UNRESOLVED");
            info ("  MATCHED PERMUTATION CONTROL INSUFFICIENT.");

            check (true, "classification recorded against the pre-registered rule");
        }
        else
        {
            info ("  Independent 32-member bank, seeds "
                  + std::to_string (kValidateSeedBase) + "..."
                  + std::to_string (kValidateSeedBase + kValidateMembers - 1) + ".");
            info ("");
            info ("    permutation   band     RT60    err      T20     T30     R^2      |T20-T30|  K spread");

            std::array<double, 4> baseRt60 {};
            double baseCritical = 0.0, baseR2 = 0.0;

            {
                Tank tank;
                tank.install (specs, identity);

                for (size_t b = 0; b < 4; ++b)
                {
                    const auto r = ensembleOf (tank, bands[b], 2.5, kValidateMembers,
                                               kValidateSeedBase);

                    if (! r.valid)
                        continue;

                    baseRt60[b] = r.fitted;

                    info ("    " + pad ("identity", 14)
                          + pad (number (kProbes[b], 0), 9)
                          + pad (number (r.fitted, 3), 8)
                          + pad ("-", 9)
                          + pad (number (r.t20, 3), 8)
                          + pad (number (r.t30, 3), 8)
                          + pad (number (r.r2, 5), 9)
                          + pad (number (r.mismatch() * 100.0, 2) + " %", 11)
                          + number (kSpreadFor (specs, identity, delays, kProbes[b]), 2) + " %");

                    if (b == 3)
                    {
                        baseCritical = r.mismatch() * 100.0;
                        baseR2 = r.r2;
                    }
                }

                info ("");
            }

            struct Outcome { std::string label; double kSpread, mismatch, r2, worstError; bool valid; };

            std::vector<Outcome> outcomes;

            for (const auto& c : matched)
            {
                Tank tank;
                tank.install (specs, c.p);

                Outcome outcome { describe (c.p), 0.0, 0.0, 0.0, 0.0, true };

                for (size_t b = 0; b < 4; ++b)
                {
                    const auto r = ensembleOf (tank, bands[b], 2.5, kValidateMembers,
                                               kValidateSeedBase);

                    if (! r.valid || baseRt60[b] <= 0.0)
                    {
                        outcome.valid = false;
                        continue;
                    }

                    const double error = r.fitted / baseRt60[b] - 1.0;

                    const double limit = (b >= 2) ? kScreenCritical : kScreenOuter;

                    if (std::abs (error) > limit)
                        outcome.valid = false;

                    outcome.worstError = std::max (outcome.worstError, std::abs (error));

                    info ("    " + pad (describe (c.p), 14)
                          + pad (number (kProbes[b], 0), 9)
                          + pad (number (r.fitted, 3), 8)
                          + pad (number (error * 100.0, 2) + " %", 9)
                          + pad (number (r.t20, 3), 8)
                          + pad (number (r.t30, 3), 8)
                          + pad (number (r.r2, 5), 9)
                          + pad (number (r.mismatch() * 100.0, 2) + " %", 11)
                          + number (kSpreadFor (specs, c.p, delays, kProbes[b]), 2) + " %");

                    if (b == 3)
                    {
                        outcome.mismatch = r.mismatch() * 100.0;
                        outcome.r2 = r.r2;
                        outcome.kSpread = kSpreadFor (specs, c.p, delays, 12000.0);
                    }
                }

                info ("");
                outcomes.push_back (outcome);
            }

            info ("  Critical 12 kHz, identity: K spread "
                  + number (identityCritical, 2) + " %, mismatch "
                  + number (baseCritical, 2) + " %, R^2 " + number (baseR2, 5));
            info ("");
            info ("    permutation   valid   K spread   mismatch   delta      R^2");

            double worstDelta = 0.0;
            bool anyValid = false;
            double biggestKChange = 0.0;

            for (const auto& o : outcomes)
            {
                const double delta = o.mismatch - baseCritical;

                info ("    " + pad (o.label, 14)
                      + pad (o.valid ? "yes" : "NO", 8)
                      + pad (number (o.kSpread, 2) + " %", 11)
                      + pad (number (o.mismatch, 2) + " %", 11)
                      + pad (number (delta, 2) + " pp", 11)
                      + number (o.r2, 5));

                if (o.valid)
                {
                    anyValid = true;

                    if (std::abs (delta) > std::abs (worstDelta))
                        worstDelta = delta;

                    biggestKChange = std::max (biggestKChange,
                                               std::abs (o.kSpread - identityCritical));
                }
            }

            info ("");
            info ("  Pre-registered rule at 12 kHz: a valid RT60-matched permutation");
            info ("  with substantially different K spread must move the mismatch by");
            info ("  >= " + number (kMaterialDropPp, 1) + " pp to make coherence a driver, or by <= "
                  + number (kImmaterialDropPp, 1) + " pp to rule it out.");
            info ("");

            const bool perturbationUseful = biggestKChange >= 10.0
                                            || biggestKChange >= identityCritical;

            if (! anyValid)
            {
                info ("  CLASSIFICATION: UNRESOLVED");
                info ("  No permutation held its RT60 match on the independent bank.");
                info ("  MATCHED PERMUTATION CONTROL INSUFFICIENT.");
            }
            else if (! perturbationUseful)
            {
                info ("  CLASSIFICATION: UNRESOLVED");
                info ("  The K-spread perturbation achieved by a valid matched");
                info ("  permutation was too small to test the hypothesis.");
            }
            else if (std::abs (worstDelta) >= kMaterialDropPp)
            {
                info ("  CLASSIFICATION: COHERENCE MATERIAL DRIVER");
            }
            else if (std::abs (worstDelta) <= kImmaterialDropPp)
            {
                info ("  CLASSIFICATION: COHERENCE NOT MATERIAL DRIVER");
                info ("  The same eight damping responses, re-assigned so that the");
                info ("  per-line spread changes substantially while the aggregate");
                info ("  decay law does not, leave the curvature where it was.");
            }
            else
            {
                info ("  CLASSIFICATION: UNRESOLVED");
                info ("  The change falls between the pre-registered brackets.");
            }

            check (true, "classification recorded against the pre-registered rule");
        }
    }
}

} // namespace avtest
