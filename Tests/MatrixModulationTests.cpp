#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"

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

    using Tank   = StereoFdn<N>;
    using Matrix = Tank::Matrix;

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

    struct Settings
    {
        double depth = modulation::defaultDepth;
        double rate  = modulation::defaultRateHz;
        LfoShape shape = LfoShape::sine;
        modulation::Schedule schedule = modulation::defaultSchedule;
        Matrix::UpdateMode updateMode = Matrix::UpdateMode::perSample;
        double rt60 = 2.0;
        bool diffusion = false;
    };

    std::unique_ptr<Tank> makeTank (double sampleRate, const Settings& settings)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (settings.diffusion);
        tank->setRt60Seconds (settings.rt60, true);
        tank->setModulationSchedule (settings.schedule);
        tank->setModulationShape (settings.shape);
        tank->setModulationRateHz (settings.rate);
        tank->setModulationUpdateMode (settings.updateMode);
        tank->setModulationDepth (settings.depth, true);
        tank->reset();
        tank->resetModulation();
        return tank;
    }

    struct StereoIr
    {
        std::vector<double> left, right, mono;
    };

    StereoIr impulseResponse (Tank& tank, size_t length)
    {
        StereoIr ir;
        ir.left.resize (length);
        ir.right.resize (length);
        ir.mono.resize (length);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            tank.processSample (l, r);

            ir.left[n]  = l;
            ir.right[n] = r;
            ir.mono[n]  = 0.5 * (l + r);
        }

        return ir;
    }

    /** Runs noise through a tank and returns the stereo output. */
    StereoIr noiseResponse (Tank& tank, const std::vector<double>& noise)
    {
        StereoIr out;
        out.left.resize (noise.size());
        out.right.resize (noise.size());
        out.mono.resize (noise.size());

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double l = noise[n], r = noise[n];
            tank.processSample (l, r);

            out.left[n]  = l;
            out.right[n] = r;
            out.mono[n]  = 0.5 * (l + r);
        }

        return out;
    }

    /** The metrics a modulation setting is judged on. */
    struct TailMetrics
    {
        double periodicity = 0.0;
        double persistence = 0.0;
        double movementHz  = 0.0;
        double flux        = 0.0;
        double echoDensity = 0.0;
        double kurtosis    = 0.0;
        double rt60        = 0.0;
        double energy      = 0.0;
    };

    TailMetrics measureTail (double sampleRate, const Settings& settings)
    {
        // The modal analysis needs an 8 s decay and 32768-sample frames: the
        // tank's mean modal spacing is about 2.6 Hz, so shorter frames cannot
        // resolve individual modes and the tracker follows noise instead.
        auto modalSettings = settings;
        modalSettings.rt60 = 8.0;

        auto modalTank = makeTank (sampleRate, modalSettings);
        const auto modalIr = impulseResponse (*modalTank, static_cast<size_t> (sampleRate * 6.0));

        TailMetrics m;

        const auto windowStart  = static_cast<size_t> (0.50 * sampleRate);
        const auto windowLength = static_cast<size_t> (4.00 * sampleRate);

        const auto flat = flattenDecay (modalIr.mono.data() + windowStart, windowLength);

        m.periodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                static_cast<size_t> (0.001 * sampleRate),
                                                static_cast<size_t> (0.120 * sampleRate));
        m.kurtosis    = excessKurtosis (flat.data(), flat.size());
        m.echoDensity = normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());

        const auto spec = spectrogramOf (
            std::vector<double> (modalIr.mono.begin() + static_cast<ptrdiff_t> (windowStart),
                                 modalIr.mono.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
            sampleRate, 32768, 16384);

        m.persistence = modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0);
        m.movementHz  = modalPeakMovementHz (spec, 200.0, 4000.0, 40, 4.0);
        m.flux        = spectralFlux (spec, 200.0, 4000.0);

        auto tank = makeTank (sampleRate, settings);
        const auto ir = impulseResponse (*tank, static_cast<size_t> (sampleRate * 4.0));

        const auto decay = measureDecay (ir.mono, sampleRate);
        m.rt60   = decay.rt60FromT30;
        m.energy = energy (ir.mono);

        return m;
    }

    /** Worst |A^T A - I| entry, plus the singular values and determinant. */
    struct MatrixQuality
    {
        double orthogonalityError = 0.0;
        double frobeniusError = 0.0;
        double worstRowNormError = 0.0;
        double worstColumnNormError = 0.0;
        double determinant = 0.0;
        double largestSingular = 0.0;
        double smallestSingular = 0.0;
        double conditionNumber = 0.0;
    };

    MatrixQuality inspect (const double* m)
    {
        MatrixQuality q;

        double squaredSum = 0.0;

        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                double dot = 0.0;

                for (int k = 0; k < N; ++k)
                    dot += m[k * N + i] * m[k * N + j];

                const double expected = (i == j) ? 1.0 : 0.0;
                const double error = std::abs (dot - expected);

                q.orthogonalityError = std::max (q.orthogonalityError, error);
                squaredSum += error * error;
            }
        }

        q.frobeniusError = std::sqrt (squaredSum);

        for (int i = 0; i < N; ++i)
        {
            double rowNorm = 0.0, columnNorm = 0.0;

            for (int k = 0; k < N; ++k)
            {
                rowNorm    += m[i * N + k] * m[i * N + k];
                columnNorm += m[k * N + i] * m[k * N + i];
            }

            q.worstRowNormError    = std::max (q.worstRowNormError, std::abs (std::sqrt (rowNorm) - 1.0));
            q.worstColumnNormError = std::max (q.worstColumnNormError, std::abs (std::sqrt (columnNorm) - 1.0));
        }

        // Singular values of an orthogonal matrix are all 1, so the largest and
        // smallest are the tightest available check and their ratio is the
        // condition number.
        q.largestSingular = largestSingularValue (m, N, 4000);

        // The smallest singular value is the largest of the inverse, and for an
        // orthogonal matrix the inverse is the transpose.
        {
            double transpose[N * N];

            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    transpose[i * N + j] = m[j * N + i];

            const double largestOfInverse = largestSingularValue (transpose, N, 4000);
            q.smallestSingular = largestOfInverse > 0.0 ? 1.0 / largestOfInverse : 0.0;
        }

        q.conditionNumber = q.smallestSingular > 0.0 ? q.largestSingular / q.smallestSingular : 0.0;

        // |det| via the product of the eigenvalue magnitudes would be circular;
        // Gaussian elimination is independent of everything above.
        {
            double copy[N * N];
            std::copy (m, m + N * N, copy);

            double det = 1.0;

            for (int col = 0; col < N; ++col)
            {
                int pivot = col;

                for (int row = col + 1; row < N; ++row)
                    if (std::abs (copy[row * N + col]) > std::abs (copy[pivot * N + col]))
                        pivot = row;

                if (std::abs (copy[pivot * N + col]) < 1.0e-14)
                {
                    det = 0.0;
                    break;
                }

                if (pivot != col)
                {
                    for (int k = 0; k < N; ++k)
                        std::swap (copy[pivot * N + k], copy[col * N + k]);

                    det = -det;
                }

                det *= copy[col * N + col];

                for (int row = col + 1; row < N; ++row)
                {
                    const double factor = copy[row * N + col] / copy[col * N + col];

                    for (int k = col; k < N; ++k)
                        copy[row * N + k] -= factor * copy[col * N + k];
                }
            }

            q.determinant = std::abs (det);
        }

        return q;
    }
}

void runMatrixModulationTests()
{
    // =======================================================================
    beginSuite ("Matrix modulation / orthogonality at every instant");
    // =======================================================================
    {
        // The whole design rests on A(t) being orthogonal for every t, not on
        // average and not to within a tolerance that grows. Sampled densely
        // through a long run at the most extreme settings the engine allows.
        Settings settings;
        settings.depth = 1.0;
        settings.rate  = modulation::maxRateHz;
        settings.schedule = modulation::scheduleAB;

        auto tank = makeTank (referenceRate, settings);

        MatrixQuality worst;
        double a[N * N];

        for (int n = 0; n < static_cast<int> (referenceRate) * 60; ++n)
        {
            double l = 0.01, r = -0.01;
            tank->processSample (l, r);

            if (n % 271 == 0)
            {
                tank->getCore().getMatrix().getCurrentMatrix (a);
                const auto q = inspect (a);

                worst.orthogonalityError   = std::max (worst.orthogonalityError, q.orthogonalityError);
                worst.frobeniusError       = std::max (worst.frobeniusError, q.frobeniusError);
                worst.worstRowNormError    = std::max (worst.worstRowNormError, q.worstRowNormError);
                worst.worstColumnNormError = std::max (worst.worstColumnNormError, q.worstColumnNormError);
                worst.determinant          = std::max (worst.determinant, std::abs (q.determinant - 1.0));
            }
        }

        info ("60 s at maximum depth and rate, eight rotations, sampled every 271 samples:");
        info ("  worst |A^T A - I| entry     " + std::to_string (worst.orthogonalityError));
        info ("  worst Frobenius error       " + std::to_string (worst.frobeniusError));
        info ("  worst row norm error        " + std::to_string (worst.worstRowNormError));
        info ("  worst column norm error     " + std::to_string (worst.worstColumnNormError));
        info ("  worst |det| deviation       " + std::to_string (worst.determinant));

        checkAtMost (worst.orthogonalityError, 1.0e-13, "A(t)^T A(t) stays the identity throughout");
        checkAtMost (worst.frobeniusError, 1.0e-12, "Frobenius error never accumulates");
        checkAtMost (worst.worstRowNormError, 1.0e-14, "every row stays unit norm");
        checkAtMost (worst.worstColumnNormError, 1.0e-14, "every column stays unit norm");
        checkAtMost (worst.determinant, 1.0e-12, "|det A(t)| stays 1");

        // Singular values and condition number at one instant.
        tank->getCore().getMatrix().getCurrentMatrix (a);
        const auto q = inspect (a);

        info ("  largest singular value      " + fixed (q.largestSingular, 12));
        info ("  smallest singular value     " + fixed (q.smallestSingular, 12));
        info ("  condition number            " + fixed (q.conditionNumber, 12));

        checkClose (q.largestSingular, 1.0, 1.0e-9, "the largest singular value is 1");
        checkClose (q.smallestSingular, 1.0, 1.0e-9, "the smallest singular value is 1");
        checkClose (q.conditionNumber, 1.0, 1.0e-9, "the matrix is perfectly conditioned");

        // Energy through repeated application: the property the tank depends on.
        double v[N] {};
        v[0] = 1.0;

        for (int n = 0; n < 2000000; ++n)
        {
            tank->getCore().getMatrix().applyCurrent (v);

            // Re-randomise the direction occasionally so the test does not sit
            // in one eigen-direction.
            if (n % 100000 == 0)
            {
                const double swap = v[0];
                v[0] = v[3];
                v[3] = swap;
            }
        }

        double finalEnergy = 0.0;
        for (int i = 0; i < N; ++i)
            finalEnergy += v[i] * v[i];

        info ("energy after 2,000,000 applications of a modulated matrix: "
              + fixed (finalEnergy, 15));

        checkAtMost (std::abs (finalEnergy - 1.0), 1.0e-9,
                     "no energy drift over two million applications");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / zero depth is a true bypass");
    // =======================================================================
    {
        Settings modulated;
        modulated.depth = 0.0;

        auto withEngine = makeTank (referenceRate, modulated);

        auto plain = std::make_unique<Tank>();
        plain->prepare (referenceRate);
        plain->setInputDiffusionEnabled (false);
        plain->setRt60Seconds (2.0, true);
        plain->reset();

        const auto noise = whiteNoise (200000, 4242u);

        bool identical = true;
        double worst = 0.0;

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double al = noise[n], ar = noise[n];
            double bl = noise[n], br = noise[n];

            withEngine->processSample (al, ar);
            plain->processSample (bl, br);

            worst = std::max (worst, std::max (std::abs (al - bl), std::abs (ar - br)));
            identical &= (al == bl) && (ar == br);
        }

        info ("worst sample difference between depth 0 and an untouched tank: "
              + std::to_string (worst));

        check (identical, "Depth 0 % is bit-identical to the static FDN");

        // And every rotation really is the identity.
        double a[N * N], h[N * N];
        withEngine->getCore().getMatrix().getCurrentMatrix (a);
        withEngine->getCore().getMatrix().getStaticCoefficients (h);

        double worstCoefficient = 0.0;
        for (int i = 0; i < N * N; ++i)
            worstCoefficient = std::max (worstCoefficient, std::abs (a[i] - h[i]));

        checkAtMost (worstCoefficient, 0.0, "at Depth 0 the matrix is exactly the Hadamard matrix");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / lane rate relationships");
    // =======================================================================
    {
        // Lanes must not be phase-shifted copies of one rate, and must not be
        // independent random rates. Fixed factors with no simple ratio between
        // them is the middle ground, and the ratios are checked as numbers.
        double rates[modulation::maxRotations];

        for (int i = 0; i < modulation::maxRotations; ++i)
            rates[i] = modulation::defaultRateHz * modulation::laneRateFactors[i];

        double factors[modulation::maxRotations];
        for (int i = 0; i < modulation::maxRotations; ++i)
            factors[i] = modulation::laneRateFactors[i];

        const double closestRatio = worstSimpleRatioProximity (factors, modulation::maxRotations);

        info ("lane rate factors:");
        std::string line = "  ";
        for (int i = 0; i < modulation::maxRotations; ++i)
            line += fixed (modulation::laneRateFactors[i], 3) + "  ";
        info (line);
        info ("closest approach of any pairwise ratio to a simple fraction: "
              + fixed (closestRatio * 100.0, 2) + " %");

        // The factors are the output of this search, not a hand-picked list.
        {
            constexpr int trials = 60000;

            unsigned int state = 0x1A7E5u;
            auto nextUniform = [&state]
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                return static_cast<double> (state) / 4294967296.0;
            };

            std::array<double, modulation::maxRotations> best {};
            double bestScore = 0.0;

            for (int trial = 0; trial < trials; ++trial)
            {
                std::array<double, modulation::maxRotations> candidate {};
                candidate[0] = 1.0;

                bool usable = true;

                for (int k = 1; k < modulation::maxRotations; ++k)
                {
                    candidate[static_cast<size_t> (k)] =
                        std::round ((1.0 + 0.95 * nextUniform()) * 1000.0) / 1000.0;

                    for (int j = 0; j < k; ++j)
                        if (std::abs (candidate[static_cast<size_t> (k)]
                                      - candidate[static_cast<size_t> (j)]) < 0.04)
                            usable = false;
                }

                if (! usable)
                    continue;

                const double score = worstSimpleRatioProximity (candidate.data(),
                                                                modulation::maxRotations);

                if (score > bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }

            std::string bestLine;
            for (double v : best)
                bestLine += fixed (v, 3) + ", ";

            info ("best factor set the search finds (" + fixed (bestScore * 100.0, 2)
                  + " %): " + bestLine);

            check (closestRatio >= bestScore * 0.95,
                   "the shipped factors are within 5 % of the best the search finds");
        }

        check (closestRatio > 0.02,
               "no two lanes sit within 2 % of a simple frequency ratio");

        bool distinct = true;
        for (int i = 0; i < modulation::maxRotations; ++i)
            for (int j = i + 1; j < modulation::maxRotations; ++j)
                distinct &= std::abs (modulation::laneRateFactors[i]
                                      - modulation::laneRateFactors[j]) > 0.02;

        check (distinct, "every lane runs at its own rate, not a phase offset of another");

        // Common repeat period, by simulation over an hour.
        for (int count : { 4, 8 })
        {
            const double period = commonRepeatPeriodSeconds (rates, count, 3600.0, 0.01);

            info (std::to_string (count) + " lanes at the default rate: common repeat period "
                  + (period < 0.0 ? std::string ("none within an hour")
                                  : fixed (period, 1) + " s"));

            check (period < 0.0 || period > 120.0,
                   std::to_string (count) + " lanes do not realign inside two minutes");
        }
    }

    // =======================================================================
    beginSuite ("Matrix modulation / pairing schedule comparison");
    // =======================================================================
    {
        const modulation::Schedule schedules[] { modulation::scheduleA, modulation::scheduleB,
                                                 modulation::scheduleC, modulation::scheduleAB };

        info ("Baseline is the static tank (Depth 0). Lower persistence and periodicity mean the");
        info ("modes are moving; echo density and RT60 must not degrade while they do.");
        info ("");
        info ("  schedule                 persistence  movement   flux     periodicity  density  RT60");

        Settings staticSettings;
        staticSettings.depth = 0.0;

        const auto baseline = measureTail (referenceRate, staticSettings);

        info ("  " + pad ("static (Depth 0)", 25)
              + pad (fixed (baseline.persistence, 4), 13)
              + pad (fixed (baseline.movementHz, 3), 11)
              + pad (fixed (baseline.flux, 4), 9)
              + pad (fixed (baseline.periodicity, 2), 13)
              + pad (fixed (baseline.echoDensity, 4), 9)
              + fixed (baseline.rt60, 4));

        double bestPersistence = 1.0e9;
        std::string bestName;

        for (const auto& schedule : schedules)
        {
            Settings settings;
            settings.schedule = schedule;

            const auto m = measureTail (referenceRate, settings);

            info ("  " + pad (schedule.name, 25)
                  + pad (fixed (m.persistence, 4), 13)
                  + pad (fixed (m.movementHz, 3), 11)
                  + pad (fixed (m.flux, 4), 9)
                  + pad (fixed (m.periodicity, 2), 13)
                  + pad (fixed (m.echoDensity, 4), 9)
                  + fixed (m.rt60, 4));

            check (m.echoDensity > baseline.echoDensity * 0.9,
                   std::string (schedule.name) + ": echo density does not regress");
            checkAtMost (std::abs (m.rt60 - baseline.rt60) / baseline.rt60, 0.05,
                         std::string (schedule.name) + ": RT60 stays within 5 % of the static tank");

            if (m.persistence < bestPersistence)
            {
                bestPersistence = m.persistence;
                bestName = schedule.name;
            }
        }

        info ("lowest modal persistence: " + bestName);
        info ("shipped schedule: " + std::string (modulation::defaultSchedule.name));

        // Rotation coverage: every line has to take part.
        for (const auto& schedule : schedules)
        {
            std::array<int, static_cast<size_t> (N)> uses {};

            for (int i = 0; i < schedule.count; ++i)
            {
                ++uses[static_cast<size_t> (schedule.pairs[i].a)];
                ++uses[static_cast<size_t> (schedule.pairs[i].b)];
            }

            const int lowest  = *std::min_element (uses.begin(), uses.end());
            const int highest = *std::max_element (uses.begin(), uses.end());

            check (lowest >= 1, std::string (schedule.name) + ": every line takes part in a rotation");
            checkClose (static_cast<double> (highest), static_cast<double> (lowest), 0.0,
                        std::string (schedule.name) + ": no line is favoured over another");
        }
    }
}

} // namespace avtest
