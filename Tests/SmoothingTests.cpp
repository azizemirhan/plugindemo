#include "Suites.h"
#include "TestFramework.h"

#include "DSP/SmoothedParameter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double maxFirstDifference (const std::vector<double>& x)
    {
        double worst = 0.0;
        for (size_t i = 1; i < x.size(); ++i)
            worst = std::max (worst, std::abs (x[i] - x[i - 1]));
        return worst;
    }

    double maxSecondDifference (const std::vector<double>& x)
    {
        double worst = 0.0;
        for (size_t i = 2; i < x.size(); ++i)
            worst = std::max (worst, std::abs (x[i] - 2.0 * x[i - 1] + x[i - 2]));
        return worst;
    }
}

void runSmoothingTests()
{
    constexpr double sampleRate = 48000.0;

    // =======================================================================
    beginSuite ("LinearSmoother (audio rate, fixed duration)");
    // =======================================================================
    {
        LinearSmoother<double> s;
        s.prepare (sampleRate, 0.01); // 480 samples

        checkClose (s.getRampLengthSamples(), 480, 0, "10 ms at 48 kHz is a 480 sample ramp");

        s.snapTo (0.0);
        s.setTarget (1.0);

        check (s.isSmoothing(), "reports that it is smoothing after a target change");

        std::vector<double> values;
        values.reserve (600);

        for (int n = 0; n < 479; ++n)
            values.push_back (s.getNextValue());

        check (s.getCurrentValue() < 1.0, "has not arrived one sample early");

        values.push_back (s.getNextValue());

        checkClose (values.back(), 1.0, 0.0, "arrives exactly on target at sample 480");
        check (! s.isSmoothing(), "stops smoothing once it arrives");
        checkClose (s.getNextValue(), 1.0, 0.0, "stays on target afterwards");

        bool monotonic = true;
        for (size_t i = 1; i < values.size(); ++i)
            monotonic &= (values[i] >= values[i - 1]);

        check (monotonic, "ramp is monotonic");
        checkClose (maxFirstDifference (values), 1.0 / 480.0, 1.0e-12,
                    "every step is exactly 1/480 of the distance");

        // skip() must be indistinguishable from calling getNextValue() n times.
        LinearSmoother<double> a, b;
        a.prepare (sampleRate, 0.01);
        b.prepare (sampleRate, 0.01);
        a.snapTo (2.0); b.snapTo (2.0);
        a.setTarget (-3.0); b.setTarget (-3.0);

        for (int n = 0; n < 137; ++n)
            (void) a.getNextValue();

        b.skip (137);

        checkClose (b.getCurrentValue(), a.getCurrentValue(), 1.0e-12,
                    "skip(n) matches n calls to getNextValue()");

        // Re-targeting mid-ramp must start from where it actually is.
        LinearSmoother<double> r;
        r.prepare (sampleRate, 0.01);
        r.snapTo (0.0);
        r.setTarget (1.0);
        r.skip (240);
        const double midway = r.getCurrentValue();
        r.setTarget (0.0);

        check (std::abs (midway - 0.5) < 0.01, "halfway through a ramp is halfway to the target");
        checkClose (r.getNextValue(), midway - midway / 480.0, 1.0e-12,
                    "re-targeting mid-ramp continues from the current value");
    }

    // =======================================================================
    beginSuite ("OnePoleSmoother (audio rate, exponential)");
    // =======================================================================
    {
        OnePoleSmoother<double> s;
        s.prepare (sampleRate, 0.01); // 10 ms time constant
        s.snapTo (0.0);
        s.setTarget (1.0);

        std::vector<double> values;
        values.reserve (48000);

        for (int n = 0; n < 480; ++n)
            values.push_back (s.getNextValue());

        checkClose (values.back(), 1.0 - std::exp (-1.0), 0.002,
                    "closes 63 % of the gap in one time constant");

        bool monotonic = true, noOvershoot = true;
        for (size_t i = 0; i < values.size(); ++i)
        {
            noOvershoot &= (values[i] <= 1.0);
            if (i > 0)
                monotonic &= (values[i] >= values[i - 1]);
        }

        check (monotonic,   "approach is monotonic");
        check (noOvershoot, "never overshoots the target");

        for (int n = 0; n < 48000; ++n)
            (void) s.getNextValue();

        check (! s.isSmoothing(), "settles exactly on target instead of decaying forever");
        checkClose (s.getCurrentValue(), 1.0, 0.0, "final value is exactly the target");

        // skip() closed form vs. the iterative path.
        OnePoleSmoother<double> a, b;
        a.prepare (sampleRate, 0.02);
        b.prepare (sampleRate, 0.02);
        a.snapTo (100.0); b.snapTo (100.0);
        a.setTarget (250.0); b.setTarget (250.0);

        for (int n = 0; n < 913; ++n)
            (void) a.getNextValue();

        b.skip (913);

        checkClose (b.getCurrentValue(), a.getCurrentValue(), 1.0e-9,
                    "skip(n) matches n calls to getNextValue()");

        // The reason a delay time uses this and not a linear ramp: the ramp has
        // a corner at each end, and for a delay tap a corner in the value is a
        // step in the pitch.
        LinearSmoother<double> ramp;
        ramp.prepare (sampleRate, 0.01);
        ramp.snapTo (0.0);
        ramp.setTarget (1.0);

        std::vector<double> rampValues;
        for (int n = 0; n < 600; ++n)
            rampValues.push_back (ramp.getNextValue());

        OnePoleSmoother<double> pole;
        pole.prepare (sampleRate, 0.01);
        pole.snapTo (0.0);
        pole.setTarget (1.0);

        std::vector<double> poleValues;
        for (int n = 0; n < 600; ++n)
            poleValues.push_back (pole.getNextValue());

        const double rampCorner = maxSecondDifference (rampValues);
        const double poleCorner = maxSecondDifference (poleValues);

        info ("max second difference   linear ramp: " + std::to_string (rampCorner)
              + "   one-pole: " + std::to_string (poleCorner));

        check (poleCorner < rampCorner / 100.0,
               "one-pole curvature is over 100x smaller than a ramp's corner");
    }

    // =======================================================================
    beginSuite ("LogSmoother (control rate, log domain)");
    // =======================================================================
    {
        constexpr double timeConstant = 0.01;
        const double coeff = 1.0 - std::exp (-1.0 / (timeConstant * sampleRate));

        // Samples needed to close exactly half the remaining log distance.
        const int halfway = static_cast<int> (std::lround (std::log (0.5) / std::log (1.0 - coeff)));

        LogSmoother<double> logS;
        logS.prepare (sampleRate, timeConstant, 1.0);
        logS.snapTo (100.0);
        logS.setTarget (10000.0);

        const double logMidpoint = logS.advance (halfway);

        // Half of a 100 -> 10000 glide in log domain is the geometric mean.
        checkClose (logMidpoint, 1000.0, 10.0,
                    "half of a 100 -> 10000 glide lands on the geometric mean (1000 Hz)");

        OnePoleSmoother<double> linS;
        linS.prepare (sampleRate, timeConstant);
        linS.snapTo (100.0);
        linS.setTarget (10000.0);
        linS.skip (halfway);

        info ("halfway through a 100 -> 10000 Hz sweep:  log domain "
              + std::to_string (logMidpoint) + " Hz,  linear domain "
              + std::to_string (linS.getCurrentValue()) + " Hz");

        check (linS.getCurrentValue() > 4000.0,
               "a linear smoother would be past 4 kHz at the same point (control case)");

        // Settling is judged in the log domain against a 1e-9 relative
        // threshold, so closing a 100:1 ratio takes about 20 time constants.
        for (int n = 0; n < 500; ++n)
            (void) logS.advance (32);

        checkClose (logS.getCurrentValue(), 10000.0, 1.0e-6, "arrives exactly on target");
        check (! logS.isSmoothing(), "settles");

        // Equal time must mean equal ratio, at both ends of the range.
        auto ratioAfter = [&] (double from, double to, int samples)
        {
            LogSmoother<double> s;
            s.prepare (sampleRate, timeConstant, 1.0);
            s.snapTo (from);
            s.setTarget (to);
            return s.advance (samples) / from;
        };

        const double lowRatio  = ratioAfter (100.0,  200.0,  halfway);
        const double highRatio = ratioAfter (5000.0, 10000.0, halfway);

        checkClose (lowRatio, highRatio, 1.0e-9,
                    "a 2x move covers the same ratio in the same time at 100 Hz and 5 kHz");

        // Values at or below the floor are clamped rather than producing log(0).
        LogSmoother<double> floored;
        floored.prepare (sampleRate, timeConstant, 20.0);
        floored.snapTo (1000.0);
        floored.setTarget (0.0);

        double v = 0.0;
        for (int n = 0; n < 2000; ++n)
            v = floored.advance (32);

        check (std::isfinite (v), "a zero target does not produce a non-finite value");
        checkClose (v, 20.0, 1.0e-6, "a zero target is clamped to the configured floor");
    }

    // =======================================================================
    beginSuite ("Zipper noise");
    // =======================================================================
    {
        // A gain step applied to a sine is the classic zipper case.
        std::vector<double> stepped, smoothedOut;
        stepped.reserve (4800);
        smoothedOut.reserve (4800);

        LinearSmoother<double> gain;
        gain.prepare (sampleRate, 0.02);
        gain.snapTo (0.0);

        for (int n = 0; n < 4800; ++n)
        {
            const double x = std::sin (2.0 * pi * 1000.0 * n / sampleRate);

            if (n == 1000)
                gain.setTarget (1.0);

            stepped.push_back (x * (n < 1000 ? 0.0 : 1.0));
            smoothedOut.push_back (x * gain.getNextValue());
        }

        const double steppedJump  = maxFirstDifference (stepped);
        const double smoothedJump = maxFirstDifference (smoothedOut);

        info ("max sample-to-sample step   hard switch: " + std::to_string (steppedJump)
              + "   smoothed: " + std::to_string (smoothedJump));

        checkAtMost (smoothedJump, 0.14, "smoothed gain change stays at the signal's own slew rate");
        check (steppedJump > smoothedJump * 3.0, "the unsmoothed control case really does step");
    }
}

} // namespace avtest
