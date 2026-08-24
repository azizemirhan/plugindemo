#pragma once

#include <cmath>
#include <cstddef>

namespace av::dsp
{

/**
    The mixing matrix at the heart of a feedback delay network.

    WHY IT MUST BE ORTHOGONAL
    -------------------------
    The FDN's state vector is multiplied by this matrix once per circulation. If
    the matrix had any singular value above 1 the network would grow along that
    direction no matter how the per-line decay gains were set, and if it had one
    below 1 the decay would depend on which direction the energy happened to be
    pointing — a tail that changes character as it decays. An orthogonal matrix
    has every singular value exactly 1: it redistributes energy between the
    delay lines without creating or destroying any, which is what lets the decay
    time be controlled purely by the per-line gains.

    With an orthogonal A and gains g_i, the one-circulation operator M = A*G has
    induced 2-norm ||M||_2 = max(g_i), because an orthogonal factor leaves
    singular values untouched. Since the spectral radius never exceeds the norm,

        rho(M) <= ||M||_2 = max(g_i)

    the network is stable whenever every gain is below 1. The norm is the
    conservative criterion of the two — M is not normal unless all the gains are
    equal, so rho is strictly smaller in practice — and FdnCoreTests reports
    both, computing rho twice by unrelated methods.

    THE TWO MATRICES
    ----------------
    Hadamard (default). Every entry is +/-1/sqrt(N), so each delay line receives
    an equal share of every other line. That is the densest mixing available
    from a sign matrix, and density is what a reverb tank is for. Evaluated with
    a fast Walsh-Hadamard butterfly: N*log2(N) additions and one final scale, no
    multiplies in the butterfly itself.

    Householder. A = I - (2/N) * 1 * 1^T, the reflection about the plane normal
    to the all-ones vector. Costs one sum and one multiply-add per element,
    cheaper than the butterfly, and is the classic Jot-style FDN matrix. Its
    diagonal is 1 - 2/N = 0.75 at N = 8, so each line keeps three quarters of
    its own signal and mixing is correspondingly weaker. Kept as a measured
    alternative rather than an assumption.

    Identity is provided for tests that need to isolate the delay network from
    the mixing.

    All three are applied in place, are branch-free in the inner loop, and never
    allocate.
*/
template <int N = 8>
class FeedbackMatrix
{
public:
    static_assert (N == 4 || N == 8 || N == 16,
                   "The Hadamard butterfly needs a power-of-two order");

    enum class Type
    {
        hadamard,     /**< Densest mixing. The default. */
        householder,  /**< Cheaper, weaker mixing. */
        identity      /**< No mixing. Tests only. */
    };

    void setType (Type t) noexcept { type = t; }
    [[nodiscard]] Type getType() const noexcept { return type; }

    /** Applies the matrix in place to a vector of N samples. */
    void process (double* v) const noexcept
    {
        switch (type)
        {
            case Type::hadamard:    applyHadamard (v);    break;
            case Type::householder: applyHouseholder (v); break;
            case Type::identity:                          break;
        }
    }

    /**
        Reference implementation: an explicit N x N matrix multiply through a
        scratch vector.

        Slower by design. The optimised paths above are only trustworthy if
        something independent agrees with them, and that is this. The tests
        compare the two element by element.
    */
    void processReference (double* v) const noexcept
    {
        double coefficients[N * N];
        getCoefficients (coefficients);

        double result[N];

        for (int row = 0; row < N; ++row)
        {
            double sum = 0.0;

            for (int column = 0; column < N; ++column)
                sum += coefficients[row * N + column] * v[column];

            result[row] = sum;
        }

        for (int i = 0; i < N; ++i)
            v[i] = result[i];
    }

    /** Writes the matrix as N*N coefficients in row-major order. */
    void getCoefficients (double* out) const noexcept
    {
        for (int column = 0; column < N; ++column)
        {
            double basis[N] {};
            basis[column] = 1.0;

            switch (type)
            {
                case Type::hadamard:    applyHadamard (basis);    break;
                case Type::householder: applyHouseholder (basis); break;
                case Type::identity:                              break;
            }

            // Column `column` of the matrix is the image of the basis vector.
            for (int row = 0; row < N; ++row)
                out[row * N + column] = basis[row];
        }
    }

    [[nodiscard]] static constexpr const char* describe (Type t) noexcept
    {
        switch (t)
        {
            case Type::hadamard:    return "hadamard";
            case Type::householder: return "householder";
            case Type::identity:    return "identity";
        }

        return "unknown";
    }

private:
    /** In-place fast Walsh-Hadamard transform, then the 1/sqrt(N) normalisation
        that turns it from orthogonal-up-to-scale into orthonormal. */
    static void applyHadamard (double* v) noexcept
    {
        for (int span = 1; span < N; span <<= 1)
        {
            for (int base = 0; base < N; base += span << 1)
            {
                for (int k = 0; k < span; ++k)
                {
                    const double a = v[base + k];
                    const double b = v[base + k + span];

                    v[base + k]        = a + b;
                    v[base + k + span] = a - b;
                }
            }
        }

        constexpr double normalisation = 1.0 / 2.8284271247461900976; // 1/sqrt(8)
        constexpr double scale = (N == 8) ? normalisation
                               : (N == 4) ? 0.5
                                          : 0.25;

        for (int i = 0; i < N; ++i)
            v[i] *= scale;
    }

    /** A = I - (2/N) * ones * ones^T. */
    static void applyHouseholder (double* v) noexcept
    {
        double sum = 0.0;

        for (int i = 0; i < N; ++i)
            sum += v[i];

        const double shift = (2.0 / static_cast<double> (N)) * sum;

        for (int i = 0; i < N; ++i)
            v[i] -= shift;
    }

    Type type = Type::hadamard;
};

} // namespace av::dsp
