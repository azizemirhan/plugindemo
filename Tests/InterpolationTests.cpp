#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Interpolation.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace av::dsp;

namespace avtest
{

void runInterpolationTests()
{
    beginSuite ("Interpolation kernels");

    constexpr double eps = 1.0e-12;

    // --- Endpoints -------------------------------------------------------
    //
    // Every kernel must return the sample itself at t = 0 and the next sample
    // at t = 1, otherwise a delay line reading at an integer position would not
    // agree with readAt().

    checkClose (interpolateLinear (3.0, 7.0, 0.0), 3.0, eps, "linear: t=0 returns x0");
    checkClose (interpolateLinear (3.0, 7.0, 1.0), 7.0, eps, "linear: t=1 returns x1");

    checkClose (interpolateHermite (1.0, 3.0, 7.0, 9.0, 0.0), 3.0, eps, "hermite: t=0 returns x0");
    checkClose (interpolateHermite (1.0, 3.0, 7.0, 9.0, 1.0), 7.0, eps, "hermite: t=1 returns x1");

    checkClose (interpolateLagrange3 (1.0, 3.0, 7.0, 9.0, 0.0), 3.0, eps, "lagrange3: t=0 returns x0");
    checkClose (interpolateLagrange3 (1.0, 3.0, 7.0, 9.0, 1.0), 7.0, eps, "lagrange3: t=1 returns x1");

    // --- Polynomial reproduction -----------------------------------------

    auto linearFn    = [] (double x) { return 2.5 * x - 1.25; };
    auto quadraticFn = [] (double x) { return 0.7 * x * x - 1.3 * x + 0.4; };
    auto cubicFn     = [] (double x) { return 0.5 * x * x * x - 0.2 * x * x + x - 3.0; };

    double worstLinear = 0.0, worstHermite = 0.0, worstLagrange = 0.0;

    for (double t = 0.05; t < 1.0; t += 0.05)
    {
        worstLinear = std::max (worstLinear,
            std::abs (interpolateLinear (linearFn (0.0), linearFn (1.0), t) - linearFn (t)));

        worstHermite = std::max (worstHermite,
            std::abs (interpolateHermite (quadraticFn (-1.0), quadraticFn (0.0),
                                          quadraticFn (1.0),  quadraticFn (2.0), t) - quadraticFn (t)));

        worstLagrange = std::max (worstLagrange,
            std::abs (interpolateLagrange3 (cubicFn (-1.0), cubicFn (0.0),
                                            cubicFn (1.0),  cubicFn (2.0), t) - cubicFn (t)));
    }

    checkAtMost (worstLinear,   eps, "linear reproduces a straight line exactly");
    checkAtMost (worstHermite,  eps, "hermite reproduces a quadratic exactly");
    checkAtMost (worstLagrange, eps, "lagrange3 reproduces a cubic exactly");

    // Hermite is only 2nd-order exact. Documenting the boundary matters: it is
    // the reason lagrange3 exists as an option for the longest feedback paths.
    double hermiteOnCubic = 0.0;

    for (double t = 0.05; t < 1.0; t += 0.05)
        hermiteOnCubic = std::max (hermiteOnCubic,
            std::abs (interpolateHermite (cubicFn (-1.0), cubicFn (0.0),
                                          cubicFn (1.0),  cubicFn (2.0), t) - cubicFn (t)));

    check (hermiteOnCubic > eps, "hermite is not exact for a cubic (expected: 2nd order)");
    info ("hermite worst-case error on a cubic: " + std::to_string (hermiteOnCubic));

    // --- Bandlimited accuracy --------------------------------------------
    //
    // The number that actually matters: how well each kernel reconstructs a
    // sine at a fractional offset. Measured across the audio band at 48 kHz.

    // At exactly t = 0.5 the Catmull-Rom and 4-point Lagrange polynomials
    // coincide, so measuring there would hide the difference between them.
    // Verify that identity explicitly, then measure away from it.
    {
        double worstDifference = 0.0;

        for (double a : { -1.0, 0.3, 2.7 })
            for (double b : { 0.5, -2.0 })
                worstDifference = std::max (worstDifference,
                    std::abs (interpolateHermite (a, b, a + b, b - a, 0.5)
                            - interpolateLagrange3 (a, b, a + b, b - a, 0.5)));

        checkAtMost (worstDifference, 1.0e-15,
                     "hermite and lagrange3 are identical at exactly t = 0.5");
    }

    beginSuite ("Interpolation accuracy (48 kHz sine, offset 0.3 samples)");

    constexpr double sampleRate = 48000.0;
    constexpr double t          = 0.3;

    for (double freq : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0 })
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;

        double errLinear = 0.0, errHermite = 0.0, errLagrange = 0.0;

        for (int n = 0; n < 2000; ++n)
        {
            const double base = static_cast<double> (n);

            const double xm1 = std::sin (w * (base - 1.0));
            const double x0  = std::sin (w * base);
            const double x1  = std::sin (w * (base + 1.0));
            const double x2  = std::sin (w * (base + 2.0));

            const double exact = std::sin (w * (base + t));

            errLinear   = std::max (errLinear,   std::abs (interpolateLinear (x0, x1, t) - exact));
            errHermite  = std::max (errHermite,  std::abs (interpolateHermite (xm1, x0, x1, x2, t) - exact));
            errLagrange = std::max (errLagrange, std::abs (interpolateLagrange3 (xm1, x0, x1, x2, t) - exact));
        }

        auto toDb = [] (double e) { return 20.0 * std::log10 (std::max (e, 1.0e-300)); };

        info (std::to_string (static_cast<int> (freq)) + " Hz:  linear "
              + std::to_string (toDb (errLinear)) + " dB   hermite "
              + std::to_string (toDb (errHermite)) + " dB   lagrange3 "
              + std::to_string (toDb (errLagrange)) + " dB");

        check (errHermite  < errLinear,
               "hermite beats linear at " + std::to_string (static_cast<int> (freq)) + " Hz");
        check (errLagrange < errLinear,
               "lagrange3 beats linear at " + std::to_string (static_cast<int> (freq)) + " Hz");
    }

    // -----------------------------------------------------------------------
    //  Behaviour across integer boundaries — the measurement that decides
    //  which kernel a *moving* tap should use.
    //
    //  Static accuracy is only half the question. As a modulated read position
    //  sweeps past an integer sample, each kernel switches to a new set of four
    //  points. Catmull-Rom is C1 continuous across that switch by construction
    //  (its tangent at a knot is defined identically from both sides), while
    //  4-point Lagrange is only C0: the value is continuous but its slope
    //  jumps. For a delay line, a slope jump is a step in pitch.
    // -----------------------------------------------------------------------

    beginSuite ("Interpolation across integer boundaries (sweeping tap)");
    {
        constexpr double rate      = 48000.0;
        constexpr double freq      = 5000.0;
        constexpr int    length    = 8000;
        constexpr double startTap  = 40.0;
        constexpr double endTap    = 48.0; // eight knot crossings

        const double w = 2.0 * 3.14159265358979323846 * freq / rate;

        auto sweep = [&] (Interpolation mode)
        {
            std::vector<double> error;
            error.reserve (length);

            for (int n = 0; n < length; ++n)
            {
                const double tap  = startTap + (endTap - startTap) * n / length;
                const double base = static_cast<double> (n);
                const int    i    = static_cast<int> (tap);
                const double t    = tap - i;

                // Sample the source analytically, so the only error present is
                // the interpolator's.
                const double xm1 = std::sin (w * (base - i + 1.0));
                const double x0  = std::sin (w * (base - i));
                const double x1  = std::sin (w * (base - i - 1.0));
                const double x2  = std::sin (w * (base - i - 2.0));

                double y = 0.0;
                switch (mode)
                {
                    case Interpolation::linear:    y = interpolateLinear (x0, x1, t); break;
                    case Interpolation::hermite:   y = interpolateHermite (xm1, x0, x1, x2, t); break;
                    case Interpolation::lagrange3: y = interpolateLagrange3 (xm1, x0, x1, x2, t); break;
                    default:                       y = x0; break;
                }

                error.push_back (y - std::sin (w * (base - tap)));
            }

            double rms = 0.0, worstCurvature = 0.0;

            for (size_t k = 0; k < error.size(); ++k)
            {
                rms += error[k] * error[k];

                if (k >= 2)
                    worstCurvature = std::max (worstCurvature,
                        std::abs (error[k] - 2.0 * error[k - 1] + error[k - 2]));
            }

            return std::pair { std::sqrt (rms / error.size()), worstCurvature };
        };

        const auto lin  = sweep (Interpolation::linear);
        const auto herm = sweep (Interpolation::hermite);
        const auto lagr = sweep (Interpolation::lagrange3);

        auto toDb = [] (double e) { return 20.0 * std::log10 (std::max (e, 1.0e-300)); };

        info ("5 kHz source, tap swept 40 -> 48 samples over 8000 samples:");
        info ("  linear     RMS error " + std::to_string (toDb (lin.first))
              + " dB,  worst error curvature " + std::to_string (lin.second));
        info ("  hermite    RMS error " + std::to_string (toDb (herm.first))
              + " dB,  worst error curvature " + std::to_string (herm.second));
        info ("  lagrange3  RMS error " + std::to_string (toDb (lagr.first))
              + " dB,  worst error curvature " + std::to_string (lagr.second));

        check (herm.first < lin.first,  "hermite beats linear on a sweeping tap");
        check (lagr.first < lin.first,  "lagrange3 beats linear on a sweeping tap");

        info (lagr.first < herm.first
                ? "lagrange3 has the lower static error on this sweep"
                : "hermite has the lower static error on this sweep");
        info (herm.second < lagr.second
                ? "hermite is the smoother of the two across knot crossings"
                : "lagrange3 is the smoother of the two across knot crossings");
    }
}

} // namespace avtest
