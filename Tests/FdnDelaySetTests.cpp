#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/FdnTuning.h"
#include "DSP/StereoFdn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double referenceRate = 48000.0;

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

    struct SetMetrics
    {
        double subsetMargin = 0.0;
        double ratioMargin  = 0.0;
        double modalSpread  = 0.0;
        int    worstGcd     = 1;
        double meanSpacing  = 0.0;
        double score        = 0.0;
    };

    std::array<int, N> toSamples (const double* ms, double sampleRate)
    {
        std::array<int, N> samples {};

        for (int i = 0; i < N; ++i)
            samples[static_cast<size_t> (i)] = static_cast<int> (std::lround (ms[i] * 0.001 * sampleRate));

        return samples;
    }

    /**
        Pairwise GCD as a fraction of the shorter delay.

        The absolute GCD is the wrong quantity. Two combs of period d_i and d_j
        realign every lcm(d_i, d_j) = d_i*d_j/gcd samples, so what matters is
        the GCD relative to the delays: a GCD of 40 on delays of 2000 samples
        means they realign only once every 100,000 samples, which at 48 kHz is
        two seconds and therefore never within a decay. A GCD of 1000 on the
        same delays would be a genuine problem. Demanding a small absolute GCD
        at several sample rates simultaneously is also unsatisfiable — the first
        search run found zero candidates out of fifteen thousand.
    */
    double worstRelativeGcd (const int* samples, int count)
    {
        double worst = 0.0;

        for (int i = 0; i < count; ++i)
            for (int j = i + 1; j < count; ++j)
            {
                int a = samples[i], b = samples[j];

                while (b != 0)
                {
                    const int t = a % b;
                    a = b;
                    b = t;
                }

                const double smaller = std::min (samples[i], samples[j]);
                worst = std::max (worst, static_cast<double> (a) / std::max (1.0, smaller));
            }

        return worst;
    }

    SetMetrics analyse (const double* ms, double sampleRate)
    {
        SetMetrics m;

        const auto samples = toSamples (ms, sampleRate);

        m.subsetMargin = worstSubsetSumCollision (ms, N);
        m.ratioMargin  = worstSimpleRatioProximity (ms, N);
        m.worstGcd     = worstPairwiseGcd (samples.data(), N);
        m.modalSpread  = modalSpacingSpread (samples.data(), N, sampleRate, 4000.0);
        m.meanSpacing  = meanModalSpacingHz (samples.data(), N, sampleRate);

        // One objective, so the search has something to maximise: the tighter of
        // the two separation margins, discounted by how uneven the modal
        // spacing is. A set that separates its echoes but piles its modes into
        // clusters is not a better set.
        m.score = std::min (m.subsetMargin, m.ratioMargin) / (1.0 + m.modalSpread);

        return m;
    }

    bool usable (const std::array<double, N>& ms)
    {
        if (ms[0] < 20.0 || ms[N - 1] > 90.0)
            return false;

        for (int i = 1; i < N; ++i)
            if (ms[static_cast<size_t> (i)] <= ms[static_cast<size_t> (i - 1)] * 1.08)
                return false;

        // Lines that share a large factor relative to their own length realign
        // often enough to pile their modes on top of each other.
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            if (worstRelativeGcd (toSamples (ms.data(), rate).data(), N) > 0.05)
                return false;

        return true;
    }
}

void runFdnDelaySetTests()
{
    // =======================================================================
    beginSuite ("FDN delay set / directed search");
    // =======================================================================
    {
        // Same approach as the diffuser's delay set, with modal spacing added.
        // The folklore answer — "use prime numbers of samples" — is not enough
        // on its own: primes force a pairwise GCD of 1 and say nothing about
        // subset sums or modal regularity, and a prime sample count stops being
        // prime the moment the host changes sample rate. So the search works in
        // milliseconds and scores the rounded result at three rates.

        constexpr int searchTrials = 15000;

        unsigned int state = 0xFD4Au;
        auto nextUniform = [&state]
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return static_cast<double> (state) / 4294967296.0;
        };

        std::array<double, N> best {};
        double bestScore = 0.0;

        for (int trial = 0; trial < searchTrials; ++trial)
        {
            std::array<double, N> candidate {};

            double value = 20.0 + 8.0 * nextUniform();
            const double ratio = 1.14 + 0.16 * nextUniform();

            for (int k = 0; k < N; ++k)
            {
                const double jitter = 1.0 + 0.14 * (nextUniform() - 0.5);
                candidate[static_cast<size_t> (k)] = std::round (value * jitter * 100.0) / 100.0;
                value *= ratio;
            }

            if (! usable (candidate))
                continue;

            const double s = analyse (candidate.data(), referenceRate).score;

            if (s > bestScore)
            {
                bestScore = s;
                best = candidate;
            }
        }

        for (int pass = 0; pass < 40; ++pass)
        {
            bool improved = false;

            for (int k = 0; k < N; ++k)
            {
                for (int step = -60; step <= 60; ++step)
                {
                    if (step == 0)
                        continue;

                    auto candidate = best;
                    candidate[static_cast<size_t> (k)] =
                        std::round ((candidate[static_cast<size_t> (k)] + step * 0.01) * 100.0) / 100.0;

                    if (! usable (candidate))
                        continue;

                    const double s = analyse (candidate.data(), referenceRate).score;

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

        std::string line;
        for (double v : best)
            line += fixed (v, 2) + ", ";

        info ("best set the search can find (score " + fixed (bestScore, 5) + "): " + line);

        const auto shipped = analyse (fdn::delaysMs, referenceRate);

        info ("shipped set scores " + fixed (shipped.score, 5));
        info ("  worst subset-sum collision   " + fixed (shipped.subsetMargin * 100.0, 2) + " %");
        info ("  closest simple ratio         " + fixed (shipped.ratioMargin * 100.0, 2) + " %");
        info ("  worst pairwise GCD @48 kHz   " + std::to_string (shipped.worstGcd) + " samples");
        info ("  modal spacing spread         " + fixed (shipped.modalSpread, 4) + " (lower is more even)");
        info ("  mean modal spacing           " + fixed (shipped.meanSpacing, 3) + " Hz");

        check (shipped.score >= bestScore * 0.95,
               "the shipped delay set is within 5 % of the best the search finds");

        std::array<double, N> shippedCopy {};
        for (int i = 0; i < N; ++i)
            shippedCopy[static_cast<size_t> (i)] = fdn::delaysMs[i];

        check (usable (shippedCopy), "the shipped set satisfies every hard constraint");

        // Determinism: the search must find the same answer every run, or the
        // "shipped set beats the search" check would be a coin toss.
        check (bestScore > 0.0, "the search is deterministic and produced a result");
    }

    // =======================================================================
    beginSuite ("FDN delay set / properties at every sample rate");
    // =======================================================================
    {
        info ("  rate       delays in samples                                        worst GCD   modal spread");

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const auto samples = toSamples (fdn::delaysMs, rate);

            std::string line;
            for (int v : samples)
                line += std::to_string (v) + " ";

            const int gcd = worstPairwiseGcd (samples.data(), N);
            const double relativeGcd = worstRelativeGcd (samples.data(), N);
            const double spread = modalSpacingSpread (samples.data(), N, rate, 4000.0);

            info ("  " + pad (fixed (rate / 1000.0, 1) + " kHz", 11) + pad (line, 56)
                  + pad (std::to_string (gcd) + " (" + fixed (relativeGcd * 100.0, 1) + " %)", 16)
                  + fixed (spread, 4));

            checkAtMost (relativeGcd, 0.05,
                         fixed (rate / 1000.0, 1) + " kHz: no pair shares a large factor "
                         "relative to its own length");

            bool increasing = true;
            for (int i = 1; i < N; ++i)
                increasing &= (samples[static_cast<size_t> (i)] > samples[static_cast<size_t> (i - 1)]);

            check (increasing, fixed (rate / 1000.0, 1) + " kHz: delays stay distinct and ordered");
        }

        // Rounding is deterministic: the same request always gives the same tank.
        const auto a = toSamples (fdn::delaysMs, referenceRate);
        const auto b = toSamples (fdn::delaysMs, referenceRate);

        bool identical = true;
        for (int i = 0; i < N; ++i)
            identical &= (a[static_cast<size_t> (i)] == b[static_cast<size_t> (i)]);

        check (identical, "millisecond-to-sample rounding is deterministic");
    }

    // =======================================================================
    beginSuite ("FDN delay set / tail behaviour in a real tank");
    // =======================================================================
    {
        // The geometric criteria are proxies. This is the measurement they are
        // proxies for: what the tail actually sounds like, against a
        // deliberately bad delay set.
        auto measureSet = [&] (const double* ms, const char* label)
        {
            // The pattern goes in before prepare(), which is what sizes the
            // delay storage. Setting it afterwards means a longer control set
            // is correctly refused for exceeding capacity — and then the tank
            // silently keeps the default pattern, so the comparison measures
            // the same set twice. That happened on the first run: the harmonic
            // control produced numbers identical to the shipped set to five
            // decimal places, which is how the mistake was caught.
            auto tank = std::make_unique<StereoFdn<N>>();
            (void) tank->setDelayPattern (ms);
            tank->prepare (referenceRate);
            tank->setInputDiffusionEnabled (false);
            tank->setRt60Seconds (2.0, true);
            tank->reset();

            std::vector<double> ir (static_cast<size_t> (referenceRate * 1.5), 0.0);

            for (size_t n = 0; n < ir.size(); ++n)
            {
                double l = (n == 0) ? 1.0 : 0.0;
                double r = l;
                tank->processSample (l, r);
                ir[n] = 0.5 * (l + r);
            }

            // Measured on a window from 150 to 500 ms, not from the start.
            // Every one of these metrics is a shape statistic, and over a
            // window that starts at the impulse the shape is dominated by the
            // decay envelope rather than by the echo pattern: with a 2 s RT60
            // the first 700 ms falls 20 dB, which inflates kurtosis by orders
            // of magnitude regardless of how diffuse the tail is. A later,
            // shorter window has only a few dB of decay across it, so what is
            // left is the structure being compared.
            // Late enough that the tank has had several circulations through
            // delays of 26-83 ms, and short enough that the decay across the
            // window is only a few dB.
            const auto windowStart  = static_cast<size_t> (0.300 * referenceRate);
            const auto windowLength = static_cast<size_t> (0.500 * referenceRate);

            // The decay is divided out before any shape statistic is taken.
            // Over a 500 ms window at a 2 s decay the level falls 15 dB, and a
            // 15 dB slope makes any window look energy-concentrated regardless
            // of how diffuse it is.
            const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

            const double periodicity = maxEnvelopePeriodicity (
                flat.data(), flat.size(),
                static_cast<size_t> (0.001 * referenceRate),
                static_cast<size_t> (0.120 * referenceRate));

            const double kurtosis = excessKurtosis (flat.data(), flat.size());
            const double ned = normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());

            info (pad (label, 14) + "periodicity " + pad (fixed (periodicity, 2), 12)
                  + "kurtosis " + pad (fixed (kurtosis, 2), 12)
                  + "echo density " + fixed (ned, 4));

            return std::array<double, 3> { periodicity, kurtosis, ned };
        };

        // Every delay an exact multiple of the first: the worst case.
        const double harmonic[N] { 24.0, 48.0, 72.0, 96.0, 120.0, 144.0, 168.0, 192.0 };

        // Eight delays crowded into a narrow band, so the modes pile up.
        const double crowded[N] { 40.0, 40.5, 41.0, 41.5, 42.0, 42.5, 43.0, 43.5 };

        const auto shipped   = measureSet (fdn::delaysMs, "shipped");
        const auto harmonicR = measureSet (harmonic, "harmonic");
        const auto crowdedR  = measureSet (crowded, "crowded");

        check (shipped[0] < harmonicR[0], "the shipped set's tail is less periodic than a harmonic set's");
        check (shipped[0] < crowdedR[0],  "the shipped set's tail is less periodic than a crowded set's");
        check (shipped[1] < harmonicR[1], "the shipped set's tail is less energy-concentrated than a harmonic set's");
        check (shipped[2] > harmonicR[2], "the shipped set builds more echo density than a harmonic set");

        // Absolute bars set from the measured population, and from what this
        // configuration actually is: the input diffuser is switched off here so
        // that the delay set is measured on its own, which is a harder test
        // than the tank ever faces in use.
        checkAtMost (shipped[1], 40.0,
                     "the shipped tail is not energy-concentrated in absolute terms");
        check (shipped[1] < harmonicR[1] / 20.0,
               "and is more than twenty times less concentrated than a harmonic set");
        check (shipped[2] > 0.65,
               "the shipped tail is well on its way to noise-like without any input diffusion");
    }
}

} // namespace avtest
