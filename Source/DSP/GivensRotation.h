#pragma once

#include <cmath>

namespace av::dsp
{

/**
    A plane rotation between two of the FDN's delay lines.

    THE EQUATION
    ------------
    A Givens rotation G(i, j, theta) is the identity everywhere except in the
    (i, j) plane, where it applies

        [ x_i' ]   [  cos t   sin t ] [ x_i ]
        [ x_j' ] = [ -sin t   cos t ] [ x_j ]

    Its determinant is cos^2 t + sin^2 t = 1 and its transpose is its inverse,
    so it is orthogonal for every angle without exception. That is the whole
    reason for using it: the energy of the pair is |x_i|^2 + |x_j|^2 both before
    and after, exactly, at any angle and at any moment. There is no angle that
    breaks it, no drift to accumulate, and no renormalisation to remember.

    A time-varying orthogonal matrix built as a product of these is therefore
    orthogonal by construction rather than by maintenance, which is what makes
    it safe to put inside a feedback loop. Perturbing a Hadamard matrix's
    coefficients directly would not have that property: an orthogonal matrix
    plus a small arbitrary matrix is not orthogonal, and the error would
    compound once per sample forever.

    ORDER MATTERS
    -------------
    Rotations do not commute unless they act on disjoint pairs. The network
    applies

        A(t) = R_1(t) * R_2(t) * ... * R_k(t) * H

    so H acts on the state vector first and the rotations are then applied from
    R_k down to R_1. With a disjoint pairing schedule the order is irrelevant
    and the product is the same however it is evaluated; with an overlapping
    schedule it is not, and the schedule comparison in the tests measures both.
*/
struct RotationPair
{
    int a = 0;
    int b = 1;
};

/**
    Applies a rotation to a vector in place.

    Deliberately a free function on a pair and a precomputed (cos, sin) rather
    than an object: the cosine and sine are updated on their own schedule, which
    may be slower than the audio rate, and keeping them outside makes that
    explicit.
*/
inline void applyGivens (double* v, RotationPair pair, double cosine, double sine) noexcept
{
    const double a = v[pair.a];
    const double b = v[pair.b];

    v[pair.a] =  cosine * a + sine * b;
    v[pair.b] = -sine   * a + cosine * b;
}

/**
    Normalises a (cos, sine) pair back onto the unit circle.

    Used when the pair has been interpolated between control-rate updates:
    linear interpolation of a point on a circle lands inside it, and an
    under-unit (c, s) would make the rotation a contraction rather than a
    rotation — a slow, silent energy leak in the feedback path. One reciprocal
    square root restores it exactly.
*/
inline void normaliseRotation (double& cosine, double& sine) noexcept
{
    const double magnitude = std::sqrt (cosine * cosine + sine * sine);

    if (magnitude > 1.0e-30)
    {
        cosine /= magnitude;
        sine   /= magnitude;
    }
    else
    {
        cosine = 1.0;
        sine   = 0.0;
    }
}

} // namespace av::dsp
