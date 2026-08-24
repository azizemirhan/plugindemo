#pragma once

#include <complex>
#include <cstddef>
#include <vector>

/**
    Reverb decay measurement.

    RT60 is not measured from a single broadband RMS line here. The standard
    method is Schroeder's backward energy integration: integrate the squared
    impulse response from the end towards the start, which turns a noisy decay
    into a monotone curve, then fit a straight line to it in dB. Reading a decay
    time off the raw envelope instead gives a number that changes depending on
    where you happen to sample it.
*/
namespace avtest
{

/**
    Result of a decay measurement.

    T30 is the primary figure: the slope is fitted between -5 and -35 dB and
    extrapolated to -60. Starting at -5 skips the direct sound and the early
    reflections, which do not belong to the decay; stopping at -35 keeps the fit
    clear of the noise floor. T20 is reported too because a tank whose T20 and
    T30 disagree is not decaying exponentially, which is itself the finding.
*/
struct DecayMeasurement
{
    double rt60FromT30 = 0.0;   /**< 2 * (t(-35 dB) - t(-5 dB)), in seconds. */
    double rt60FromT20 = 0.0;   /**< 3 * (t(-25 dB) - t(-5 dB)), in seconds. */
    double earlyDecayTime = 0.0;/**< 6 * (t(-10 dB) - t(0 dB)), in seconds. */
    double linearity = 0.0;     /**< R^2 of the straight-line fit, 1 = perfect. */
    double totalEnergy = 0.0;
    bool   valid = false;       /**< False if the response never reached -35 dB. */
};

/** Schroeder backward energy integration, in dB relative to the total. */
std::vector<double> energyDecayCurve (const std::vector<double>& impulseResponse);

/** Measures the decay of a broadband impulse response. */
DecayMeasurement measureDecay (const std::vector<double>& impulseResponse, double sampleRate);

/** Measures the decay inside one frequency band. */
DecayMeasurement measureBandDecay (const std::vector<double>& impulseResponse, double sampleRate,
                                   double lowHz, double highHz);

// ===========================================================================
//  Feedback operator analysis
// ===========================================================================
/*
    Two different numbers govern an FDN's stability, and they are not the same
    thing.

    The induced 2-norm ||M||_2 is the largest singular value: the most the
    operator can amplify any vector in a single application. For M = G*A with A
    orthogonal it equals max(g_i) exactly.

    The spectral radius rho(M) is the largest magnitude among the eigenvalues:
    the asymptotic growth rate under repeated application. In general

        rho(M) <= ||M||_2

    with equality only for normal matrices. G*A is not normal unless every gain
    is identical, so the two genuinely differ and the norm is the conservative
    one. Stability needs rho < 1; a norm below 1 is sufficient but not
    necessary, which is why both are reported.
*/

/** Largest singular value of an n x n matrix, by power iteration on M^T M. */
double largestSingularValue (const double* matrix, int n, int iterations = 40000);

/**
    Spectral radius by the Gelfand formula, rho(M) = lim ||M^k||^(1/k).

    Computed with repeated squaring and renormalisation, so k reaches 2^30
    without overflow. Independent of the eigenvalue solver below, which is the
    point: two unrelated methods agreeing is what makes the number trustworthy.
*/
double spectralRadiusByGelfand (const double* matrix, int n);

/**
    All n eigenvalues of a real n x n matrix.

    Householder reduction to upper Hessenberg form, then shifted QR with
    deflation. Real eigenvalues come out of 1x1 blocks and complex conjugate
    pairs out of 2x2 blocks.
*/
std::vector<std::complex<double>> eigenvalues (const double* matrix, int n);

/** Largest |eigenvalue|, from the solver above. */
double spectralRadius (const double* matrix, int n);

// ===========================================================================
//  Delay-set analysis
// ===========================================================================

/** Greatest common divisor of every pair of delays, in samples. Larger values
    mean the two lines share whole families of modes. */
int worstPairwiseGcd (const int* delaySamples, int count);

/**
    Regularity of the network's modal spacing.

    Each delay line resonates at multiples of sampleRate/d_i. The union of those
    frequencies is where the tank's modes cluster, and a good delay set spreads
    them evenly: clusters mean some pitches ring while the gaps between them
    sound hollow. Returned as the coefficient of variation of the gaps between
    consecutive mode frequencies — lower is more even.
*/
double modalSpacingSpread (const int* delaySamples, int count,
                           double sampleRate, double upToHz);

/** Mean spacing between modes, in Hz. Roughly sampleRate / sum(delays). */
double meanModalSpacingHz (const int* delaySamples, int count, double sampleRate);

/** Smallest relative distance between any subset sum and any single delay. */
double worstSubsetSumCollision (const double* delays, int count);

/** Closest approach of any pairwise ratio to a musically simple fraction. */
double worstSimpleRatioProximity (const double* delays, int count);

} // namespace avtest
