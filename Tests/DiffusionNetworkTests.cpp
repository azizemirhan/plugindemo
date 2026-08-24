#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DiffusionNetwork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    using Net = DiffusionNetwork<double, Interpolation::lagrange3>;

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    /**
        Closest distance from x to a musically simple ratio, as a fraction of
        that ratio.

        Only small denominators are considered. A ratio of 5/3 makes every third
        echo of one stage land on every fifth of another; a ratio of 37/23 does
        not, even though it is also rational.
    */
    double distanceFromSimpleRatio (double x)
    {
        constexpr int maxNumerator   = 24;
        constexpr int maxDenominator = 3;

        double closest = 1.0e9;

        for (int q = 1; q <= maxDenominator; ++q)
        {
            for (int p = 1; p <= maxNumerator; ++p)
            {
                const double ratio = static_cast<double> (p) / static_cast<double> (q);
                closest = std::min (closest, std::abs (x - ratio) / ratio);
            }
        }

        return closest;
    }

    /** Worst simple-ratio proximity across every pair in a set. */
    double worstRatioProximity (const double* delays, int count)
    {
        double worst = 1.0e9;

        for (int i = 0; i < count; ++i)
            for (int j = i + 1; j < count; ++j)
                worst = std::min (worst, distanceFromSimpleRatio (delays[j] / delays[i]));

        return worst;
    }

    /** Smallest relative distance between any subset sum and any single delay. */
    double worstSubsetSumCollision (const double* delays, int count)
    {
        double worst = 1.0e9;

        for (int mask = 1; mask < (1 << count); ++mask)
        {
            // Only sums of two or more terms can collide with a single delay.
            int bits = 0;
            double sum = 0.0;

            for (int i = 0; i < count; ++i)
            {
                if (mask & (1 << i))
                {
                    sum += delays[i];
                    ++bits;
                }
            }

            if (bits < 2)
                continue;

            for (int j = 0; j < count; ++j)
                if ((mask & (1 << j)) == 0)
                    worst = std::min (worst, std::abs (sum - delays[j]) / delays[j]);
        }

        return worst;
    }

    Net makeNetwork (double sampleRate, int stages, double diffusion)
    {
        Net net;
        net.prepare (sampleRate);
        net.setStageCount (stages, true);
        net.setDiffusion (diffusion, true);
        return net;
    }
}

void runDiffusionNetworkTests()
{
    constexpr double sampleRate = 48000.0;

    // =======================================================================
    beginSuite ("Diffusion / delay set constraints");
    // =======================================================================
    {
        // The two failure modes the delay set has to avoid, checked as numbers
        // rather than asserted in a comment.
        auto examine = [] (const char* name, const double* delays, int count)
        {
            const double worstRatio = worstRatioProximity (delays, count);
            const double worstSum   = worstSubsetSumCollision (delays, count);

            info (pad (name, 10) + "closest simple ratio: " + fixed (worstRatio * 100.0, 2)
                  + " %   closest subset-sum collision: " + fixed (worstSum * 100.0, 2) + " %");

            return std::pair { worstRatio, worstSum };
        };

        // --- Where the shipped numbers came from --------------------------
        //
        // The set is the winner of this search, not a hand-picked guess.
        // Candidates are jittered geometric progressions rounded to the two
        // decimals that actually ship, scored on the worse of the two
        // constraints. Keeping the search in the suite means the shipped set
        // has to keep beating freshly generated alternatives.

        constexpr int searchTrials = 12000;

        auto scoreOf = [] (const double* d, int n)
        {
            return std::min (worstRatioProximity (d, n), worstSubsetSumCollision (d, n));
        };

        auto usableSet = [] (const std::array<double, 8>& d)
        {
            if (d[0] < 0.6)
                return false;

            double total = d[0];

            for (int i = 1; i < 8; ++i)
            {
                if (d[static_cast<size_t> (i)] <= d[static_cast<size_t> (i - 1)] * 1.15)
                    return false;

                total += d[static_cast<size_t> (i)];
            }

            // The whole chain is a group delay the signal has to pass through,
            // so it is bounded on both sides: too short and the stages have no
            // room to build density, too long and the diffuser becomes an
            // audible pre-delay of its own.
            return total >= 38.0 && total <= 62.0;
        };

        // Runs the search once. `avoid` is the other channel's set, which the
        // candidate must stay clear of so the two chains stay decorrelated; its
        // shared leading stages are copied rather than searched, because the
        // stereo network overrides them anyway.
        auto search = [&] (unsigned int seed, const double* avoid)
        {
            unsigned int state = seed;
            auto nextUniform = [&state]
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                return static_cast<double> (state) / 4294967296.0;
            };

            auto acceptable = [&] (const std::array<double, 8>& d)
            {
                if (! usableSet (d))
                    return false;

                if (avoid != nullptr)
                    for (int i = diffusion::defaultSharedStages; i < 8; ++i)
                        if (std::abs (d[static_cast<size_t> (i)] - avoid[i]) / avoid[i] < 0.08)
                            return false;

                return true;
            };

            double bestScore = 0.0;
            std::array<double, 8> best {};

            for (int trial = 0; trial < searchTrials; ++trial)
            {
                std::array<double, 8> candidate {};

                double value = 0.95 + 0.55 * nextUniform();
                const double ratio = 1.38 + 0.28 * nextUniform();

                for (int k = 0; k < 8; ++k)
                {
                    const double jitter = 1.0 + 0.20 * (nextUniform() - 0.5);
                    candidate[static_cast<size_t> (k)] = std::round (value * jitter * 100.0) / 100.0;
                    value *= ratio;
                }

                if (avoid != nullptr)
                    for (int k = 0; k < diffusion::defaultSharedStages; ++k)
                        candidate[static_cast<size_t> (k)] = avoid[k];

                if (! acceptable (candidate))
                    continue;

                const double s = scoreOf (candidate.data(), 8);

                if (s > bestScore)
                {
                    bestScore = s;
                    best = candidate;
                }
            }

            // Local refinement. Random sets get close; nudging one delay at a
            // time by the hundredth of a millisecond that actually ships gets
            // considerably closer.
            for (int pass = 0; pass < 60; ++pass)
            {
                bool improved = false;

                for (int k = 0; k < 8; ++k)
                {
                    for (int step = -40; step <= 40; ++step)
                    {
                        if (step == 0)
                            continue;

                        if (avoid != nullptr && k < diffusion::defaultSharedStages)
                            continue;

                        auto candidate = best;
                        candidate[static_cast<size_t> (k)] =
                            std::round ((candidate[static_cast<size_t> (k)] + step * 0.01) * 100.0) / 100.0;

                        if (! acceptable (candidate))
                            continue;

                        const double s = scoreOf (candidate.data(), 8);

                        if (s > bestScore)
                        {
                            bestScore = s;
                            best = candidate;
                            improved = true;
                        }
                    }
                }

                if (! improved)
                    break;
            }

            return std::pair { bestScore, best };
        };

        const auto foundLeft  = search (0xC0FFEEu, nullptr);
        const auto foundRight = search (0x5EED17u, foundLeft.second.data());

        auto describe = [] (const std::array<double, 8>& set)
        {
            std::string line;
            for (double v : set)
                line += fixed (v, 2) + ", ";
            return line;
        };

        info ("best left set found  (score " + fixed (foundLeft.first * 100.0, 3)
              + " %): " + describe (foundLeft.second));
        info ("best right set found (score " + fixed (foundRight.first * 100.0, 3)
              + " %): " + describe (foundRight.second));

        const auto left  = examine ("left", diffusion::leftDelaysMs, diffusion::maxStages);
        const auto right = examine ("right", diffusion::rightDelaysMs, diffusion::maxStages);

        const double leftScore  = std::min (left.first, left.second);
        const double rightScore = std::min (right.first, right.second);

        info ("shipped scores:  left " + fixed (leftScore * 100.0, 3)
              + " %,  right " + fixed (rightScore * 100.0, 3) + " %");

        // With eight delays there are 255 subset sums packed into a two-decade
        // range, so perfect separation is not available at any price. The bar is
        // that the shipped sets are as good as a directed search can manage.
        check (leftScore >= foundLeft.first * 0.95,
               "the shipped left set is within 5 % of the best the search finds");
        check (rightScore >= foundRight.first * 0.95,
               "the shipped right set is within 5 % of the best the search finds");

        // The two channels must also be well separated from each other, or the
        // stereo field collapses towards mono.
        double worstChannelSeparation = 1.0e9;

        for (int i = diffusion::defaultSharedStages; i < diffusion::maxStages; ++i)
        {
            const double l = diffusion::leftDelaysMs[i];
            const double r = diffusion::rightDelaysMs[i];
            worstChannelSeparation = std::min (worstChannelSeparation, std::abs (l - r) / l);
        }

        info ("closest left/right delay pair among the unshared stages: "
              + fixed (worstChannelSeparation * 100.0, 2) + " %");

        check (worstChannelSeparation > 0.05,
               "no unshared stage pair sits within 5 % across the two channels");

        check (left.first  > 0.004, "left set: no pair sits within 0.4 % of a simple ratio");
        check (right.first > 0.004, "right set: no pair sits within 0.4 % of a simple ratio");
        check (left.second  > 0.004, "left set: no subset sum lands within 0.4 % of another delay");
        check (right.second > 0.004, "right set: no subset sum lands within 0.4 % of another delay");

        // A deliberately harmonic control set, to show the checks can fail.
        const double harmonic[8] { 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0 };
        const auto bad = examine ("harmonic", harmonic, 8);

        check (bad.first < 0.001 && bad.second < 0.001,
               "the harmonic control set fails both checks (the checks work)");

        // Delays must be strictly increasing, so stage order is meaningful.
        bool increasing = true;
        for (int i = 1; i < diffusion::maxStages; ++i)
        {
            increasing &= (diffusion::leftDelaysMs[i]  > diffusion::leftDelaysMs[i - 1]);
            increasing &= (diffusion::rightDelaysMs[i] > diffusion::rightDelaysMs[i - 1]);
        }

        check (increasing, "both delay sets increase monotonically");
    }

    // =======================================================================
    beginSuite ("Diffusion / the network is magnitude-flat");
    // =======================================================================
    {
        // Every stage is an allpass, so the chain must be one too. Any
        // deviation here comes from the interpolators inside the loops.
        constexpr size_t fftSize = 32768;

        info ("  stages  diffusion   energy      max +dB   max -dB   LF dev    HF dev");

        bool allFlat = true, allEnergyPreserved = true;

        for (int stages : { 2, 4, 8 })
        {
            for (double d : { 0.25, 0.5, 1.0 })
            {
                auto net = makeNetwork (sampleRate, stages, d);
                const auto ir = impulseResponseOf (net, fftSize);
                const auto mag = magnitudeSpectrum (ir, fftSize, false);

                const auto full = bandDeviation (mag, sampleRate, fftSize, 20.0, 20000.0);
                const auto low  = bandDeviation (mag, sampleRate, fftSize, 20.0, 200.0);
                const auto high = bandDeviation (mag, sampleRate, fftSize, 10000.0, 20000.0);
                const double e = energy (ir);

                info ("    " + fixed (stages, 0) + "      " + fixed (d, 2) + "      "
                      + pad (fixed (e, 6), 12)
                      + pad (fixed (full.maxAboveDb, 3), 10)
                      + pad (fixed (full.maxBelowDb, 3), 10)
                      + pad (fixed (std::max (low.maxAboveDb, low.maxBelowDb), 3), 10)
                      + fixed (std::max (high.maxAboveDb, high.maxBelowDb), 3));

                allFlat &= (full.maxAboveDb < 1.5);
                allEnergyPreserved &= (std::abs (e - 1.0) < 0.05);
            }
        }

        check (allFlat, "no configuration rises more than 1.5 dB above its mean level");
        check (allEnergyPreserved, "every configuration preserves energy within 5 %");
    }

    // =======================================================================
    beginSuite ("Diffusion / echo density grows with the control");
    // =======================================================================
    {
        // Normalised echo density (Abel & Huang): the fraction of samples
        // exceeding one standard deviation, scaled so that Gaussian noise gives
        // 1.0. This is the measurement that turns "sounds denser" into a number.
        const double windowsMs[] { 5.0, 10.0, 20.0, 50.0, 100.0, 250.0 };

        for (int stages : { 4, 8 })
        {
            info ("");
            info (std::to_string (stages) + " stages — normalised echo density in [0, T]:");
            info ("  diffusion    5 ms    10 ms    20 ms    50 ms   100 ms   250 ms");

            std::array<std::vector<double>, 5> densities;
            std::array<std::vector<double>, 5> concentration;
            int row = 0;

            for (double d : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            {
                auto net = makeNetwork (sampleRate, stages, d);
                const auto ir = impulseResponseOf (net, 48000);

                std::string line = "     " + fixed (d, 2) + "   ";

                for (double ms : windowsMs)
                {
                    const auto length = static_cast<size_t> (ms * 0.001 * sampleRate);

                    densities[static_cast<size_t> (row)].push_back (
                        normalisedEchoDensity (ir.data(), ir.size(), 0, length));

                    // Kurtosis of the same window. Sparse echoes give a huge
                    // value; a diffuse cloud approaches the Gaussian zero.
                    concentration[static_cast<size_t> (row)].push_back (
                        excessKurtosis (ir.data(), length));

                    line += pad (fixed (densities[static_cast<size_t> (row)].back(), 4), 9);
                }

                info (line);
                ++row;
            }

            info ("  the same windows as energy concentration (excess kurtosis, lower = denser):");

            for (int r = 0; r < 5; ++r)
            {
                std::string line = "     " + fixed (0.25 * r, 2) + "   ";
                for (double k : concentration[static_cast<size_t> (r)])
                    line += pad (fixed (k, 1), 9);
                info (line);
            }

            // Monotonicity is the property that matters, and it is asserted on
            // concentration rather than on NED. NED is the standard metric and
            // is reported above, but it is a ratio over a fixed window: once a
            // higher coefficient starts pushing energy past the window's end it
            // can fall again even though the response has become denser.
            // Kurtosis has no such window artefact.
            //
            // Assertion is on normalised echo density, the standard metric.
            // Kurtosis is printed alongside because it is the more robust
            // density proxy in the middle of the range, but it disagrees at the
            // very top: the first output sample of an allpass is -g, so raising
            // the coefficient makes the direct spike larger, and kurtosis —
            // being a fourth-moment measure — is dominated by that single
            // sample. Echo density counts how many samples clear one sigma and
            // is not fooled by it.
            double worstRegression = 0.0;

            for (size_t w = 0; w < densities[0].size(); ++w)
                for (size_t r = 1; r < densities.size(); ++r)
                    worstRegression = std::max (worstRegression,
                        (densities[r - 1][w] - densities[r][w])
                        / std::max (0.01, densities[r - 1][w]));

            check (worstRegression < 0.05, std::to_string (stages)
                   + " stages: echo density never falls by more than 5 % as diffusion rises"
                     " (worst regression " + fixed (worstRegression * 100.0, 1) + " %)");

            // At 0 % the chain is a pure delay: one impulse out, no echoes.
            {
                auto net = makeNetwork (sampleRate, stages, 0.0);
                const auto ir = impulseResponseOf (net, 48000);

                const double peak = peakAbsolute (ir);
                size_t significant = 0;

                for (double v : ir)
                    if (std::abs (v) > 0.01 * peak)
                        ++significant;

                checkClose (static_cast<double> (significant), 1.0, 0.0,
                            std::to_string (stages)
                            + " stages at 0 %: exactly one impulse comes out (a pure delay chain)");
            }

            check (concentration[4][2] < concentration[1][2] * 0.6,
                   std::to_string (stages)
                   + " stages: 100 % is far denser than 25 % at 20 ms");

            check (densities[4][2] > densities[1][2],
                   std::to_string (stages)
                   + " stages: normalised echo density at 20 ms also rises from 25 % to 100 %");
        }
    }

    // =======================================================================
    beginSuite ("Diffusion / mapping shape");
    // =======================================================================
    {
        // The exponent in DiffusionTuning.h is chosen here: the mapping whose
        // density steps across 0/25/50/75/100 % are the most evenly spaced is
        // the one that feels linear to a user.
        const double exponents[] { 1.0, 0.7, 0.5, 0.35 };
        const auto window = static_cast<size_t> (0.02 * sampleRate);

        info ("  exponent   density index at 0/25/50/75/100 %          NED spread   index spread");

        double bestNedSpread = 1.0e9, bestNedExponent = 0.0;
        double bestIndexSpread = 1.0e9, bestIndexExponent = 0.0;

        for (double exponent : exponents)
        {
            std::vector<double> ned, index;

            for (double d : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            {
                Net net;
                net.prepare (sampleRate);
                net.setStageCount (4, true);

                const double g = diffusion::diffusionMaxCoefficient * std::pow (d, exponent);

                for (int i = 0; i < Net::maxStages; ++i)
                    net.getStage (i).setCoefficient (d <= 0.0 ? 0.0 : g, true);

                const auto ir = impulseResponseOf (net, 48000);

                ned.push_back (normalisedEchoDensity (ir.data(), ir.size(), 0, window));

                // A density index bounded to 0..1 that has no window artefact:
                // sparse responses have huge kurtosis, diffuse ones approach 0.
                index.push_back (1.0 / (1.0 + excessKurtosis (ir.data(), window)));
            }

            auto spreadOf = [] (const std::vector<double>& v)
            {
                double smallest = 1.0e9, largest = -1.0e9;

                for (size_t i = 1; i < v.size(); ++i)
                {
                    smallest = std::min (smallest, v[i] - v[i - 1]);
                    largest  = std::max (largest,  v[i] - v[i - 1]);
                }

                return largest - smallest;
            };

            const double nedSpread   = spreadOf (ned);
            const double indexSpread = spreadOf (index);

            std::string line = "    " + fixed (exponent, 2) + "      ";
            for (double v : index)
                line += pad (fixed (v, 4), 9);
            line += "   " + pad (fixed (nedSpread, 4), 13) + fixed (indexSpread, 4);
            info (line);

            if (nedSpread < bestNedSpread)   { bestNedSpread = nedSpread;   bestNedExponent = exponent; }
            if (indexSpread < bestIndexSpread) { bestIndexSpread = indexSpread; bestIndexExponent = exponent; }
        }

        info ("most evenly spaced exponent:  by echo density " + fixed (bestNedExponent, 2)
              + ",  by concentration index " + fixed (bestIndexExponent, 2)
              + "   (shipped: " + fixed (diffusion::diffusionShapeExponent, 2) + ")");

        check (std::abs (diffusion::diffusionShapeExponent - bestNedExponent) < 0.001
            || std::abs (diffusion::diffusionShapeExponent - bestIndexExponent) < 0.001,
               "the shipped exponent is the one a density measurement selects");

        // Mapping sanity.
        checkClose (diffusion::coefficientForDiffusion (0.0), 0.0, 0.0,
                    "diffusion 0 % gives a zero coefficient (a pure delay chain, still flat)");
        checkClose (diffusion::coefficientForDiffusion (1.0), diffusion::diffusionMaxCoefficient, 1.0e-12,
                    "diffusion 100 % reaches the recommended ceiling");

        bool mappingMonotonic = true;
        double previous = -1.0;

        for (double d = 0.0; d <= 1.0001; d += 0.01)
        {
            const double g = diffusion::coefficientForDiffusion (d);
            mappingMonotonic &= (g >= previous - 1.0e-15);
            previous = g;
        }

        check (mappingMonotonic, "the mapping is monotonic across its whole range");
        checkClose (diffusion::coefficientForDiffusion (0.5),
                    diffusion::diffusionMaxCoefficient * std::pow (0.5, diffusion::diffusionShapeExponent),
                    1.0e-12, "the mapping follows the documented power law");
    }

    // =======================================================================
    beginSuite ("Diffusion / metallic ringing");
    // =======================================================================
    {
        // A resonance that survives in the tail shows up as a narrow spectral
        // peak. Measured against a deliberately harmonic delay set, so the
        // metric is shown to have discrimination rather than just producing a
        // number that looks acceptable.
        constexpr size_t fftSize = 32768;

        auto measure = [&] (const double* delays, const char* label)
        {
            const double signs[8] { 1, 1, 1, 1, 1, 1, 1, 1 };

            // The pattern goes in before prepare(), because prepare() sizes each
            // stage's storage from the pattern it is holding. Setting it
            // afterwards would silently clamp the longer control sets and make
            // the comparison meaningless.
            Net net;
            (void) net.setDelayPattern (delays, signs, true); // before prepare: always accepted
            net.prepare (sampleRate);
            net.setStageCount (8, true);
            net.setDiffusion (1.0, true);

            const auto ir = impulseResponseOf (net, fftSize);

            // The magnitude spectrum of the whole response is flat to the last
            // bit — every stage is exactly allpass, so it has to be. Spectral
            // flatness therefore cannot detect ringing here, which is itself
            // worth knowing: in an allpass cascade, metallic ringing is a
            // time-domain and phase phenomenon, not a magnitude one.
            const auto mag = magnitudeSpectrum (ir, fftSize, false);
            const double wholeFlatness = spectralFlatness (mag, sampleRate, fftSize, 100.0, 15000.0);

            // What does detect it: how concentrated the energy is in time, and
            // whether a short late window has narrow peaks.
            const double kurtosis = excessKurtosis (ir.data(), ir.size());

            // Periodicity of the energy envelope: how strongly the echo pattern
            // repeats. Lags from 0.5 ms to 40 ms cover every plausible period.
            const double periodicity = maxEnvelopePeriodicity (
                ir.data(), 16384,
                static_cast<size_t> (0.0005 * sampleRate),
                static_cast<size_t> (0.040 * sampleRate));

            constexpr size_t lateSize = 8192;
            const auto lateStart = static_cast<size_t> (0.05 * sampleRate);
            const auto lateMag = magnitudeSpectrum (ir.data() + lateStart, lateSize, lateSize, true);
            const double latePeak = peakAboveMedianDb (lateMag, sampleRate, lateSize, 100.0, 15000.0);

            info (pad (label, 12) + "kurtosis " + pad (fixed (kurtosis, 1), 12)
                  + "periodicity " + pad (fixed (periodicity, 4), 12)
                  + "late peak " + pad (fixed (latePeak, 2) + " dB", 12)
                  + "whole-IR flatness " + fixed (wholeFlatness, 5));

            return std::array<double, 3> { kurtosis, periodicity, latePeak };
        };

        const double harmonic[8] { 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0 };
        const double identical[8] { 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0 };

        const auto chosen     = measure (diffusion::leftDelaysMs, "chosen");
        const auto harmonicR  = measure (harmonic, "harmonic");
        const auto identicalR = measure (identical, "identical");

        check (chosen[0] < harmonicR[0] * 0.5,
               "the chosen set is at least twice as diffuse as a harmonic set");
        check (chosen[0] < identicalR[0] * 0.5,
               "the chosen set is at least twice as diffuse as eight identical delays");

        check (chosen[1] < harmonicR[1] * 0.5,
               "the chosen set's echo pattern is at least twice less periodic than a harmonic set's");
        check (chosen[1] < identicalR[1] * 0.5,
               "the chosen set's echo pattern is far less periodic than eight identical delays");

        check (chosen[2] < identicalR[2],
               "the chosen set's late window is less peaky than eight identical delays");

        checkAtMost (chosen[0], 100.0,
                     "the chosen set's impulse response is not energy-concentrated");
        checkAtMost (chosen[1], 50.0,
                     "no single lag stands more than 50x above the typical one");

        // Worth stating plainly, because it invalidates the usual test: the
        // late-window spectral peak does not separate the chosen set from a
        // harmonic one. A harmonic cascade is dense in that window, just
        // periodic, and periodicity is not what a short-window spectrum sees.
        info (chosen[2] < harmonicR[2]
                ? "late-window peak also separates the chosen set from the harmonic one"
                : "late-window peak does NOT separate the chosen set from the harmonic one "
                  "— periodicity and kurtosis are the metrics that do");

        // Sine bursts, since ringing is easiest to provoke with a tone that
        // stops. The test is not "is there a tail" — an 8-stage chain at the
        // maximum coefficient legitimately has one — but "does it keep
        // decaying", which is what separates diffusion from a resonance.
        for (double freq : { 500.0, 2000.0, 6000.0 })
        {
            auto net = makeNetwork (sampleRate, 8, 1.0);

            const auto burst = sineBurst (65536, 4800, freq, sampleRate);
            const auto out = processThrough (net, burst);

            auto windowDb = [&] (double afterSeconds)
            {
                const auto start = static_cast<size_t> (4800 + afterSeconds * sampleRate);
                const auto length = static_cast<size_t> (0.05 * sampleRate);

                if (start + length >= out.size())
                    return -300.0;

                return 10.0 * std::log10 (std::max (energy (out.data() + start, length)
                                                    / energy (out), 1.0e-300));
            };

            const double at100 = windowDb (0.1);
            const double at500 = windowDb (0.5);

            info (fixed (freq, 0) + " Hz burst: 50 ms window 100 ms after the burst "
                  + fixed (at100, 1) + " dB, 500 ms after " + fixed (at500, 1) + " dB");

            check (at500 < at100 - 20.0,
                   fixed (freq, 0) + " Hz burst: the tail keeps decaying (20 dB further by 500 ms)");
        }
    }

    // =======================================================================
    beginSuite ("Diffusion / transient smear");
    // =======================================================================
    {
        // Diffusion trades transient definition for density. The trade must be
        // gradual and monotonic, not a cliff.
        info ("  diffusion   crest factor   energy within 5 ms   kurtosis");

        double previousKurtosis = 1.0e9;
        double previousCrest = 1.0e9;
        bool kurtosisFalls = true, crestFalls = true;

        for (double d : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            auto net = makeNetwork (sampleRate, 4, d);

            const auto burst = transientBurst (16384, sampleRate, 1234u);
            const auto out = processThrough (net, burst);

            const auto earlySamples = static_cast<size_t> (0.005 * sampleRate);
            const double earlyRatio = energy (out.data(), earlySamples) / energy (out);
            const double crest = crestFactor (out.data(), out.size());
            const double kurt = excessKurtosis (out.data(), out.size());

            info ("     " + fixed (d, 2) + "      " + pad (fixed (crest, 3), 15)
                  + pad (fixed (earlyRatio, 4), 21) + fixed (kurt, 2));

            kurtosisFalls &= (kurt <= previousKurtosis);
            crestFalls    &= (crest <= previousCrest + 0.5);

            previousKurtosis = kurt;
            previousCrest = crest;
        }

        check (kurtosisFalls,
               "energy concentration falls monotonically as diffusion rises "
               "(the transient is being spread)");

        // Crest factor is reported but not asserted on. It is peak over RMS, and
        // the peak of an allpass chain is its direct term, which is -g per
        // stage: raising the coefficient raises the peak even as the tail
        // becomes denser. Kurtosis weighs the whole distribution and does not
        // have that blind spot.
        info (crestFalls
                ? "crest factor also falls monotonically"
                : "crest factor is not monotonic at the top of the range — it tracks the "
                  "direct -g term, which grows with the coefficient");
    }

    // =======================================================================
    beginSuite ("Diffusion / stage count changes are click-free");
    // =======================================================================
    {
        auto maxStep = [] (const std::vector<double>& x, size_t from, size_t to)
        {
            double worst = 0.0;
            for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i)
                worst = std::max (worst, std::abs (x[i] - x[i - 1]));
            return worst;
        };

        constexpr size_t total = 96000;
        constexpr size_t changeAt = 24000;

        auto run = [&] (int from, int to, bool immediate)
        {
            auto net = makeNetwork (sampleRate, from, 0.7);
            std::vector<double> out (total);

            for (size_t n = 0; n < total; ++n)
            {
                if (n == changeAt)
                    net.setStageCount (to, immediate);

                out[n] = net.processSample (std::sin (2.0 * pi * 700.0
                                                      * static_cast<double> (n) / sampleRate));
            }

            return std::pair { maxStep (out, 4000, changeAt), maxStep (out, changeAt, total) };
        };

        const auto grow   = run (4, 8, false);
        const auto shrink = run (8, 4, false);
        const auto abrupt = run (8, 4, true);

        info ("stage count 4 -> 8:  steady step " + fixed (grow.first, 5)
              + ", smoothed " + fixed (grow.second, 5));
        info ("stage count 8 -> 4:  steady step " + fixed (shrink.first, 5)
              + ", smoothed " + fixed (shrink.second, 5)
              + ", immediate " + fixed (abrupt.second, 5));

        checkAtMost (grow.second, grow.first * 2.5,
                     "growing the chain never exceeds 2.5x the steady-state step");
        checkAtMost (shrink.second, shrink.first * 2.5,
                     "shrinking the chain never exceeds 2.5x the steady-state step");
        check (abrupt.second > shrink.second * 1.5,
               "an immediate stage-count change really does step (control case)");

        // After the crossfade completes, the disabled stages stop costing CPU.
        auto net = makeNetwork (sampleRate, 8, 0.7);
        net.setStageCount (2, false);

        for (int n = 0; n < 48000; ++n)
            (void) net.processSample (0.0);

        checkClose (net.getProcessedStageCount(), 2, 0,
                    "the chain stops processing the retired stages once they have faded");

        // A disengaged stage must be bit-transparent.
        Net transparent;
        transparent.prepare (sampleRate);
        transparent.setStageCount (0, true);
        transparent.setDiffusion (1.0, true);

        const auto noise = whiteNoise (10000, 77u);
        const auto out = processThrough (transparent, noise);

        double worst = 0.0;
        for (size_t i = 0; i < noise.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - noise[i]));

        checkClose (worst, 0.0, 0.0, "a zero-stage chain passes the signal through bit-exactly");
    }

    // =======================================================================
    beginSuite ("Diffusion / sample rate and block size matrix");
    // =======================================================================
    {
        // The character must survive a rate change, and the render must not
        // depend on how the host chops it up.
        info ("  rate      total delay   energy    energy within 5/20/50 ms");

        // Energy arriving inside a fixed span of *time* is the rate-independent
        // statement of "the same reverb". Normalised echo density is not: it
        // counts samples, so a sparse response scores lower simply for being
        // sampled more often.
        std::vector<std::array<double, 3>> envelopePerRate;

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto net = makeNetwork (rate, 4, 0.75);

            const auto ir = impulseResponseOf (net, static_cast<size_t> (rate * 0.5));
            const double total = energy (ir);
            const double totalMs = 1000.0 * net.getTotalDelaySamples() / rate;

            std::array<double, 3> envelope {};
            int slot = 0;

            for (double ms : { 5.0, 20.0, 50.0 })
                envelope[static_cast<size_t> (slot++)] =
                    energy (ir.data(), static_cast<size_t> (ms * 0.001 * rate)) / total;

            envelopePerRate.push_back (envelope);

            info ("  " + pad (fixed (rate / 1000.0, 1) + " kHz", 10)
                  + pad (fixed (totalMs, 3) + " ms", 14)
                  + pad (fixed (total, 5), 10)
                  + fixed (envelope[0], 4) + " / " + fixed (envelope[1], 4)
                  + " / " + fixed (envelope[2], 4));

            checkClose (totalMs, 10.59, 0.05,
                        fixed (rate / 1000.0, 1) + " kHz: the chain's total delay is the same in ms");
            check (allFinite (ir), fixed (rate / 1000.0, 1) + " kHz: response is finite");
        }

        double worstEnvelopeSpread = 0.0;

        for (const auto& envelope : envelopePerRate)
            for (size_t k = 0; k < envelope.size(); ++k)
                worstEnvelopeSpread = std::max (worstEnvelopeSpread,
                                                std::abs (envelope[k] - envelopePerRate[1][k]));

        checkAtMost (worstEnvelopeSpread, 0.03,
                     "the energy envelope in time is within 3 % across all five sample rates");

        // Block sizes, including one larger than the prepared size and one that
        // changes on every callback.
        auto reference = makeNetwork (sampleRate, 6, 0.8);
        const auto input = whiteNoise (30000, 909u);
        const auto expected = processThrough (reference, input);

        bool allMatch = true;

        for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 })
        {
            auto net = makeNetwork (sampleRate, 6, 0.8);
            std::vector<double> out = input;

            int n = 0;
            while (n < static_cast<int> (out.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (out.size()) - n);
                net.processBlock (out.data() + n, count);
                n += count;
            }

            for (size_t i = 0; i < out.size(); ++i)
                allMatch &= (out[i] == expected[i]);
        }

        check (allMatch, "block sizes 1 to 8192 all render bit-identically");

        {
            auto net = makeNetwork (sampleRate, 6, 0.8);
            std::vector<double> out = input;

            int n = 0, index = 0;
            while (n < static_cast<int> (out.size()))
            {
                const int sizes[] { 3, 64, 1, 500, 128, 7, 1024, 31 };
                const int count = std::min (sizes[index++ % 8], static_cast<int> (out.size()) - n);
                net.processBlock (out.data() + n, count);
                n += count;
            }

            bool identical = true;
            for (size_t i = 0; i < out.size(); ++i)
                identical &= (out[i] == expected[i]);

            check (identical, "a block size that changes every callback is also bit-identical");
        }
    }

    // =======================================================================
    beginSuite ("Diffusion / stability, determinism and silence");
    // =======================================================================
    {
        // Determinism: two identically configured networks must agree bit for bit.
        auto a = makeNetwork (sampleRate, 8, 0.9);
        auto b = makeNetwork (sampleRate, 8, 0.9);

        const auto input = whiteNoise (50000, 31337u);
        const auto outA = processThrough (a, input);
        const auto outB = processThrough (b, input);

        bool identical = true;
        for (size_t i = 0; i < outA.size(); ++i)
            identical &= (outA[i] == outB[i]);

        check (identical, "two identically configured networks are bit-identical");

        // Five minutes of full-scale noise with continuous automation.
        auto net = makeNetwork (sampleRate, 8, 0.5);
        double worstPeak = 0.0;
        bool finite = true, scaleAlwaysFitted = true;
        unsigned int state = 20260804u;

        const int totalSamples = static_cast<int> (sampleRate) * 300;

        for (int n = 0; n < totalSamples; ++n)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

            if (n % 64 == 0)
            {
                const double phase = 2.0 * pi * 0.3 * static_cast<double> (n) / sampleRate;
                net.setDiffusion (0.5 + 0.5 * std::sin (phase));
                scaleAlwaysFitted &= succeeded (net.setDelayScale (1.0 + 0.5 * std::sin (phase * 0.61)));
            }

            const double y = net.processSample (x);
            worstPeak = std::max (worstPeak, std::abs (y));
            finite &= std::isfinite (y);
        }

        check (finite, "5 minutes of full-scale noise with continuous automation stays finite");
        check (scaleAlwaysFitted, "every delay-scale request stayed inside the reserved capacity");
        info ("worst peak over 5 minutes with automation: " + fixed (worstPeak, 4));
        checkAtMost (worstPeak, 6.0, "no runaway growth over 5 minutes");

        // Silence in must give silence out, and never a subnormal.
        auto quiet = makeNetwork (sampleRate, 8, 1.0);
        (void) quiet.processSample (1.0);

        std::vector<double> tail (1000000);
        for (auto& s : tail)
            s = quiet.processSample (0.0);

        check (allFinite (tail), "a 20 s silent tail stays finite");
        check (! anySubnormal (tail.data(), tail.size()), "a 20 s silent tail produces no subnormals");

        // Measured after the impulse has decayed: the chain has unity DC gain,
        // so the impulse's own contribution would otherwise dominate.
        checkAtMost (std::abs (mean (tail.data() + 800000, 200000)), 1.0e-22,
                     "the settled silent tail leaves no DC offset");
        checkAtMost (peakAbsolute (tail.data() + 800000, 200000), 1.0e-18,
                     "the settled silent output is below -360 dBFS");

        // Reset.
        auto resettable = makeNetwork (sampleRate, 8, 0.9);
        const auto noise = whiteNoise (20000, 5u);
        (void) processThrough (resettable, noise);

        resettable.reset();

        double residual = 0.0;
        for (int n = 0; n < 48000; ++n)
            residual = std::max (residual, std::abs (resettable.processSample (0.0)));

        checkAtMost (residual, 1.0e-20, "reset clears every stage");
    }
}

} // namespace avtest
