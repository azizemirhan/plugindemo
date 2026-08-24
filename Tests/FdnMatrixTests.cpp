#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/FeedbackMatrix.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = 8;
    using Matrix = FeedbackMatrix<N>;

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string scientific (double v)
    {
        std::ostringstream s;
        s.setf (std::ios::scientific);
        s.precision (2);
        s << v;
        return s.str();
    }

    /** Largest absolute deviation of M^T M from the identity. */
    double orthogonalityError (const double* m)
    {
        double worst = 0.0;

        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                double dot = 0.0;

                for (int k = 0; k < N; ++k)
                    dot += m[k * N + i] * m[k * N + j];

                const double expected = (i == j) ? 1.0 : 0.0;
                worst = std::max (worst, std::abs (dot - expected));
            }
        }

        return worst;
    }

    double determinant (double* m)
    {
        // Gaussian elimination with partial pivoting; destroys the input.
        double det = 1.0;

        for (int col = 0; col < N; ++col)
        {
            int pivot = col;

            for (int row = col + 1; row < N; ++row)
                if (std::abs (m[row * N + col]) > std::abs (m[pivot * N + col]))
                    pivot = row;

            if (std::abs (m[pivot * N + col]) < 1.0e-14)
                return 0.0;

            if (pivot != col)
            {
                for (int k = 0; k < N; ++k)
                    std::swap (m[pivot * N + k], m[col * N + k]);

                det = -det;
            }

            det *= m[col * N + col];

            for (int row = col + 1; row < N; ++row)
            {
                const double factor = m[row * N + col] / m[col * N + col];

                for (int k = col; k < N; ++k)
                    m[row * N + k] -= factor * m[col * N + k];
            }
        }

        return det;
    }
}

void runFdnMatrixTests()
{
    const Matrix::Type types[] { Matrix::Type::hadamard, Matrix::Type::householder };

    // =======================================================================
    beginSuite ("FDN matrix / orthogonality");
    // =======================================================================
    {
        info ("  matrix        orthogonality err   |det|      worst row norm err   worst col norm err");

        for (auto type : types)
        {
            Matrix matrix;
            matrix.setType (type);

            double m[N * N];
            matrix.getCoefficients (m);

            const double orthoError = orthogonalityError (m);

            double worstRow = 0.0, worstColumn = 0.0;

            for (int i = 0; i < N; ++i)
            {
                double rowNorm = 0.0, columnNorm = 0.0;

                for (int k = 0; k < N; ++k)
                {
                    rowNorm    += m[i * N + k] * m[i * N + k];
                    columnNorm += m[k * N + i] * m[k * N + i];
                }

                worstRow    = std::max (worstRow,    std::abs (std::sqrt (rowNorm) - 1.0));
                worstColumn = std::max (worstColumn, std::abs (std::sqrt (columnNorm) - 1.0));
            }

            double copy[N * N];
            std::copy (std::begin (m), std::end (m), std::begin (copy));
            const double det = std::abs (determinant (copy));

            info ("  " + std::string (Matrix::describe (type))
                  + std::string (14 - std::string (Matrix::describe (type)).size(), ' ')
                  + scientific (orthoError) + "            "
                  + fixed (det, 9) + "  " + scientific (worstRow)
                  + "            " + scientific (worstColumn));

            checkAtMost (orthoError, 1.0e-14,
                         std::string (Matrix::describe (type)) + ": M^T M is the identity");
            checkAtMost (std::abs (det - 1.0), 1.0e-12,
                         std::string (Matrix::describe (type)) + ": |determinant| is 1");
            checkAtMost (worstRow, 1.0e-14,
                         std::string (Matrix::describe (type)) + ": every row has unit norm");
            checkAtMost (worstColumn, 1.0e-14,
                         std::string (Matrix::describe (type)) + ": every column has unit norm");
        }
    }

    // =======================================================================
    beginSuite ("FDN matrix / energy preservation");
    // =======================================================================
    {
        for (auto type : types)
        {
            Matrix matrix;
            matrix.setType (type);

            const std::string name { Matrix::describe (type) };

            // A single application must preserve the vector's length exactly.
            auto noise = whiteNoise (N * 200, 8888u);
            double worstSingle = 0.0;

            for (size_t offset = 0; offset + N <= noise.size(); offset += N)
            {
                double v[N];
                double before = 0.0;

                for (int i = 0; i < N; ++i)
                {
                    v[i] = noise[offset + static_cast<size_t> (i)];
                    before += v[i] * v[i];
                }

                matrix.process (v);

                double after = 0.0;
                for (int i = 0; i < N; ++i)
                    after += v[i] * v[i];

                worstSingle = std::max (worstSingle, std::abs (after - before) / std::max (before, 1.0e-30));
            }

            checkAtMost (worstSingle, 1.0e-14, name + ": one application preserves energy");

            // A million applications must not drift. This is the property the
            // whole FDN rests on: the matrix runs once per sample forever.
            double v[N];
            for (int i = 0; i < N; ++i)
                v[i] = (i == 0) ? 1.0 : 0.0;

            double initial = 1.0;

            for (int n = 0; n < 1000000; ++n)
                matrix.process (v);

            double finalEnergy = 0.0;
            for (int i = 0; i < N; ++i)
                finalEnergy += v[i] * v[i];

            const double drift = std::abs (finalEnergy - initial);

            info (name + ": energy after 1,000,000 applications = " + fixed (finalEnergy, 15)
                  + "  (drift " + scientific (drift) + ")");

            checkAtMost (drift, 1.0e-9, name + ": no floating-point drift over a million applications");
        }
    }

    // =======================================================================
    beginSuite ("FDN matrix / optimised path matches the reference");
    // =======================================================================
    {
        for (auto type : types)
        {
            Matrix matrix;
            matrix.setType (type);

            const std::string name { Matrix::describe (type) };
            const auto noise = whiteNoise (N * 5000, 4321u);

            double worst = 0.0;

            for (size_t offset = 0; offset + N <= noise.size(); offset += N)
            {
                double fast[N], reference[N];

                for (int i = 0; i < N; ++i)
                    fast[i] = reference[i] = noise[offset + static_cast<size_t> (i)];

                matrix.process (fast);
                matrix.processReference (reference);

                for (int i = 0; i < N; ++i)
                    worst = std::max (worst, std::abs (fast[i] - reference[i]));
            }

            info (name + ": worst difference between the butterfly and an explicit "
                         "matrix multiply = " + scientific (worst));

            checkAtMost (worst, 1.0e-15,
                         name + ": the optimised path agrees with the reference multiply");
        }
    }

    // =======================================================================
    beginSuite ("FDN matrix / in-place safety and structure");
    // =======================================================================
    {
        Matrix hadamard;
        hadamard.setType (Matrix::Type::hadamard);

        // Processing in place must not read an element after overwriting it.
        // Compared against an out-of-place computation through a scratch copy.
        const auto noise = whiteNoise (N * 2000, 246u);
        double worst = 0.0;

        for (size_t offset = 0; offset + N <= noise.size(); offset += N)
        {
            double inPlace[N], viaCopy[N], scratch[N];

            for (int i = 0; i < N; ++i)
                inPlace[i] = scratch[i] = noise[offset + static_cast<size_t> (i)];

            hadamard.process (inPlace);

            double m[N * N];
            hadamard.getCoefficients (m);

            for (int row = 0; row < N; ++row)
            {
                double sum = 0.0;

                for (int column = 0; column < N; ++column)
                    sum += m[row * N + column] * scratch[column];

                viaCopy[row] = sum;
            }

            for (int i = 0; i < N; ++i)
                worst = std::max (worst, std::abs (inPlace[i] - viaCopy[i]));
        }

        checkAtMost (worst, 1.0e-15, "in-place processing matches an out-of-place multiply");

        // Structure: every Hadamard entry has the same magnitude, which is what
        // makes it the densest mixing available from a sign matrix.
        double m[N * N];
        hadamard.getCoefficients (m);

        double smallest = 1.0e9, largest = 0.0;

        for (int i = 0; i < N * N; ++i)
        {
            smallest = std::min (smallest, std::abs (m[i]));
            largest  = std::max (largest,  std::abs (m[i]));
        }

        checkClose (smallest, largest, 1.0e-15, "every Hadamard entry has the same magnitude");
        checkClose (largest, 1.0 / std::sqrt (8.0), 1.0e-15, "that magnitude is 1/sqrt(8)");

        // Householder's diagonal is 1 - 2/N, which is why it mixes less.
        Matrix householder;
        householder.setType (Matrix::Type::householder);
        householder.getCoefficients (m);

        checkClose (m[0], 1.0 - 2.0 / N, 1.0e-15, "Householder keeps 1-2/N of each line on its diagonal");
        checkClose (m[1], -2.0 / N, 1.0e-15, "and spreads -2/N to every other line");

        info ("Hadamard off-diagonal magnitude " + fixed (1.0 / std::sqrt (8.0), 4)
              + " vs Householder " + fixed (2.0 / N, 4)
              + " — Hadamard mixes " + fixed ((1.0 / std::sqrt (8.0)) / (2.0 / N), 2) + "x harder");

        // Identity is genuinely the identity, so tests can isolate the delays.
        Matrix identity;
        identity.setType (Matrix::Type::identity);

        double v[N];
        for (int i = 0; i < N; ++i)
            v[i] = static_cast<double> (i) + 1.0;

        identity.process (v);

        bool unchanged = true;
        for (int i = 0; i < N; ++i)
            unchanged &= (v[i] == static_cast<double> (i) + 1.0);

        check (unchanged, "the identity matrix leaves the vector untouched");
    }
}

} // namespace avtest
