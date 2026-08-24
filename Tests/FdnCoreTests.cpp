#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

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

    std::unique_ptr<StereoFdn<N>> makeTank (double sampleRate, double rt60,
                                            bool diffusion = false)
    {
        auto tank = std::make_unique<StereoFdn<N>>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);
        tank->reset();
        return tank;
    }

    /** Mono sum of a stereo impulse response. */
    std::vector<double> tankImpulseResponse (StereoFdn<N>& tank, size_t length)
    {
        std::vector<double> ir (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            tank.processSample (l, r);
            ir[n] = 0.5 * (l + r);
        }

        return ir;
    }

    /** The FDN's one-circulation operator, M = A*G, as an N x N matrix. */
    std::array<double, static_cast<size_t> (N * N)> loopOperator (const FdnCore<N>& core)
    {
        double a[N * N];
        core.getMatrix().getCurrentMatrix (a);

        std::array<double, static_cast<size_t> (N * N)> m {};

        for (int row = 0; row < N; ++row)
            for (int column = 0; column < N; ++column)
                m[static_cast<size_t> (row * N + column)] =
                    a[row * N + column] * core.getGain (column);

        return m;
    }
}

void runFdnCoreTests()
{
    // =======================================================================
    beginSuite ("FDN core / impulse response and decay curve");
    // =======================================================================
    {
        auto tank = makeTank (referenceRate, 2.0);
        const auto ir = tankImpulseResponse (*tank, static_cast<size_t> (referenceRate * 4.0));

        check (allFinite (ir), "the impulse response is finite throughout");
        check (! anySubnormal (ir.data(), ir.size()), "no subnormal ever appears in the response");

        // The first output cannot arrive before the shortest delay.
        // The onset is checked on the left channel, not on the mono sum. At the
        // very first echo only one delay line has produced anything, so the sum
        // is scaled by c_L[0] + c_R[0] — and for the shipped readout vectors
        // those are -1 and +1, which cancels the first echo exactly. A real
        // measurement artefact, and a good reminder that a mono sum is not a
        // neutral observer of a stereo tank.
        //
        // The delay line is read before it is written, so a sample injected at
        // n = 0 emerges at n = delay, not at delay - 1.
        std::vector<double> irLeft (4096, 0.0);
        {
            auto probe = makeTank (referenceRate, 2.0);

            for (size_t n = 0; n < irLeft.size(); ++n)
            {
                double l = (n == 0) ? 1.0 : 0.0;
                double r = l;
                probe->processSample (l, r);
                irLeft[n] = l;
            }
        }

        const int shortest = tank->getCore().getDelaySamples (0);
        double earlyPeak = 0.0;

        for (int n = 0; n < shortest; ++n)
            earlyPeak = std::max (earlyPeak, std::abs (irLeft[static_cast<size_t> (n)]));

        checkAtMost (earlyPeak, 1.0e-20, "nothing comes out before the shortest delay has elapsed");
        check (std::abs (irLeft[static_cast<size_t> (shortest)]) > 1.0e-3,
               "the first echo arrives exactly at the shortest delay");

        const auto curve = energyDecayCurve (ir);

        bool monotone = true;
        for (size_t i = 1; i < curve.size(); ++i)
            monotone &= (curve[i] <= curve[i - 1] + 1.0e-12);

        check (monotone, "the energy decay curve is monotone (Schroeder integration)");

        const auto decay = measureDecay (ir, referenceRate);

        info ("RT60 target 2.00 s:  T30 " + fixed (decay.rt60FromT30, 4)
              + " s,  T20 " + fixed (decay.rt60FromT20, 4)
              + " s,  EDT " + fixed (decay.earlyDecayTime, 4)
              + " s,  linearity R^2 " + fixed (decay.linearity, 5));

        check (decay.valid, "the response decays past -35 dB, so T30 is measurable");
        check (decay.linearity > 0.99, "the decay is straight in dB (R^2 above 0.99)");
        checkAtMost (std::abs (decay.rt60FromT30 - decay.rt60FromT20) / decay.rt60FromT30, 0.05,
                     "T20 and T30 agree, so the decay really is exponential");
    }

    // =======================================================================
    beginSuite ("FDN core / target RT60 accuracy");
    // =======================================================================
    {
        info ("  target      T30 measured   error     T20        EDT        R^2      max gain");

        double worstError = 0.0;

        for (double target : { 0.1, 0.3, 1.0, 2.0, 5.0, 10.0, 30.0, 100.0 })
        {
            auto tank = makeTank (referenceRate, target);

            // Long enough to fall past -35 dB with headroom to spare.
            const auto length = static_cast<size_t> (referenceRate * std::max (0.5, target * 0.75));
            const auto ir = tankImpulseResponse (*tank, length);
            const auto decay = measureDecay (ir, referenceRate);

            const double error = decay.valid
                ? std::abs (decay.rt60FromT30 - target) / target
                : 1.0;

            worstError = std::max (worstError, error);

            info ("  " + pad (fixed (target, 2) + " s", 12)
                  + pad (fixed (decay.rt60FromT30, 4) + " s", 15)
                  + pad (fixed (error * 100.0, 2) + " %", 10)
                  + pad (fixed (decay.rt60FromT20, 4), 11)
                  + pad (fixed (decay.earlyDecayTime, 4), 11)
                  + pad (fixed (decay.linearity, 4), 9)
                  + fixed (tank->getCore().getMaxGain(), 6));

            check (decay.valid, fixed (target, 2) + " s: the decay is measurable");
        }

        info ("worst relative RT60 error across the range: " + fixed (worstError * 100.0, 2) + " %");

        checkAtMost (worstError, 0.15,
                     "measured RT60 tracks the target within 15 % from 0.1 s to 100 s");

        // The formula itself, checked against its own inverse.
        double worstRoundTrip = 0.0;

        for (double delay : { 0.02, 0.05, 0.09 })
            for (double rt60 : { 0.1, 1.0, 10.0, 100.0 })
            {
                const double gain = fdn::feedbackGainForRt60 (delay, rt60);
                worstRoundTrip = std::max (worstRoundTrip,
                                           std::abs (fdn::rt60ForFeedbackGain (delay, gain) - rt60) / rt60);
            }

        checkAtMost (worstRoundTrip, 1.0e-12, "the RT60 gain formula inverts exactly");
    }

    // =======================================================================
    beginSuite ("FDN core / decay is the same in every band");
    // =======================================================================
    {
        // There is no damping in this phase, so any frequency-dependent decay
        // would be an artefact — most likely an interpolator that had crept
        // into a feedback path.
        auto tank = makeTank (referenceRate, 2.0);
        const auto ir = tankImpulseResponse (*tank, static_cast<size_t> (referenceRate * 6.0));

        const auto broadband = measureDecay (ir, referenceRate);
        const auto low  = measureBandDecay (ir, referenceRate, 100.0, 400.0);
        const auto mid  = measureBandDecay (ir, referenceRate, 800.0, 2500.0);
        const auto high = measureBandDecay (ir, referenceRate, 6000.0, 12000.0);

        info ("  band          T30        R^2");
        info ("  broadband     " + pad (fixed (broadband.rt60FromT30, 4) + " s", 11) + fixed (broadband.linearity, 4));
        info ("  100-400 Hz    " + pad (fixed (low.rt60FromT30, 4) + " s", 11) + fixed (low.linearity, 4));
        info ("  0.8-2.5 kHz   " + pad (fixed (mid.rt60FromT30, 4) + " s", 11) + fixed (mid.linearity, 4));
        info ("  6-12 kHz      " + pad (fixed (high.rt60FromT30, 4) + " s", 11) + fixed (high.linearity, 4));

        const double spread = std::max ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 })
                            - std::min ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 });

        info ("spread across bands: " + fixed (spread, 4) + " s ("
              + fixed (100.0 * spread / broadband.rt60FromT30, 2) + " % of the broadband figure)");

        // The low band is the loose one, and its own fit quality says why: a
        // narrow low band holds far fewer modes than a wide high one, so its
        // Schroeder curve is noisier and the straight-line fit is measurably
        // worse. That is a property of measuring a narrow band, not of the
        // tank — the mid and high bands, which have plenty of modes, agree
        // closely with each other and with the broadband figure.
        checkAtMost (std::abs (mid.rt60FromT30 - high.rt60FromT30) / broadband.rt60FromT30, 0.05,
                     "mid and high bands decay within 5 % of each other");
        checkAtMost (spread / broadband.rt60FromT30, 0.15,
                     "no band strays more than 15 % from any other (no damping yet)");
        check (low.linearity < mid.linearity && low.linearity < high.linearity,
               "the low band's looser figure comes with a measurably worse fit");
    }

    // =======================================================================
    beginSuite ("FDN core / stability");
    // =======================================================================
    {
        // Two different quantities, and the earlier version of this suite
        // conflated them. What power iteration on M^T M produces is the largest
        // singular value, which is the induced 2-norm ||M||_2 — the most the
        // operator can amplify a vector in one application. The spectral radius
        // rho(M) is the largest |eigenvalue|, which governs asymptotic growth.
        // For a non-normal M the two differ, and M = A*G is non-normal whenever
        // the per-line gains are not all equal. Both are reported, and rho is
        // computed twice by unrelated methods so the number can be trusted.
        info ("  RT60       max gain     ||M||_2      rho(M) via QR   rho(M) via Gelfand   geo-mean gain");

        bool normBelowUnity = true, radiusBelowUnity = true;

        for (double target : { 0.1, 1.0, 10.0, 100.0 })
        {
            auto tank = makeTank (referenceRate, target);
            const auto m = loopOperator (tank->getCore());

            const double maxGain = tank->getCore().getMaxGain();
            const double norm    = largestSingularValue (m.data(), N);
            const double rhoQr   = spectralRadius (m.data(), N);
            const double rhoGel  = spectralRadiusByGelfand (m.data(), N);

            // |det(A*G)|^(1/N) = geometric mean of the gains, and it is a lower
            // bound on rho for any matrix.
            double logProduct = 0.0;
            for (int i = 0; i < N; ++i)
                logProduct += std::log (tank->getCore().getGain (i));

            const double geometricMean = std::exp (logProduct / N);

            info ("  " + pad (fixed (target, 2) + " s", 11)
                  + pad (fixed (maxGain, 9), 13)
                  + pad (fixed (norm, 9), 13)
                  + pad (fixed (rhoQr, 9), 16)
                  + pad (fixed (rhoGel, 9), 21)
                  + fixed (geometricMean, 9));

            normBelowUnity   &= (norm < 1.0);
            radiusBelowUnity &= (rhoQr < 1.0);

            checkClose (norm, maxGain, 1.0e-9,
                        fixed (target, 2) + " s: ||M||_2 equals max(g_i), as orthogonality requires");

            checkClose (rhoQr, rhoGel, std::max (1.0e-6, rhoQr * 1.0e-4),
                        fixed (target, 2) + " s: the QR eigenvalues and the Gelfand limit agree on rho");

            check (rhoQr <= norm * (1.0 + 1.0e-9),
                   fixed (target, 2) + " s: rho(M) <= ||M||_2, as it must be");

            check (rhoQr >= geometricMean * (1.0 - 1.0e-6),
                   fixed (target, 2) + " s: rho(M) >= |det M|^(1/N), as it must be");
        }

        check (normBelowUnity,   "every decay setting keeps ||M||_2 strictly below 1");
        check (radiusBelowUnity, "every decay setting keeps rho(M) strictly below 1");

        // The gap between the two is the price of using the norm as the
        // stability criterion. Worth knowing how conservative it is.
        {
            auto tank = makeTank (referenceRate, 10.0);
            const auto m = loopOperator (tank->getCore());

            const double norm  = largestSingularValue (m.data(), N);
            const double rho   = spectralRadius (m.data(), N);

            info ("at a 10 s decay the norm is " + fixed (20.0 * std::log10 (norm / rho), 3)
                  + " dB above the spectral radius — the norm criterion is conservative "
                    "but not by much");

            // Every eigenvalue, so a complex pair sitting near the unit circle
            // could not hide behind a comfortable largest-magnitude figure.
            const auto lambdas = eigenvalues (m.data(), N);

            double worst = 0.0;
            for (const auto& lambda : lambdas)
                worst = std::max (worst, std::abs (lambda));

            checkClose (static_cast<double> (lambdas.size()), static_cast<double> (N), 0.0,
                        "the solver returns all eight eigenvalues");
            checkClose (worst, rho, 1.0e-12, "the largest is the reported spectral radius");

            bool allInside = true;
            for (const auto& lambda : lambdas)
                allInside &= (std::abs (lambda) < 1.0);

            check (allInside, "every eigenvalue lies strictly inside the unit circle");
        }

        // The clamp: asking for more decay than the range allows must not
        // produce a gain of 1.
        auto tank = makeTank (referenceRate, 2.0);
        tank->setRt60Seconds (1.0e9, true);

        info ("an absurd RT60 request clamps the largest gain to "
              + fixed (tank->getCore().getMaxGain(), 6));

        check (tank->getCore().getMaxGain() < 1.0, "an absurd RT60 request cannot reach unity gain");
        checkAtMost (tank->getCore().getMaxGain(), fdn::maxFeedbackGain,
                     "the gain clamp is respected");

        // 100 s decay run for two minutes: the hardest sustained case.
        auto longTank = makeTank (referenceRate, 100.0);

        double worstPeak = 0.0;
        bool finite = true;

        for (int n = 0; n < static_cast<int> (referenceRate) * 120; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            longTank->processSample (l, r);

            worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
            finite &= std::isfinite (l) && std::isfinite (r);
        }

        info ("100 s decay, 2 minutes after a unit impulse: peak " + fixed (worstPeak, 5));

        check (finite, "a 100 s decay stays finite for two minutes");
        checkAtMost (worstPeak, 2.0, "a unit impulse never produces a peak above 2.0");
    }

    // =======================================================================
    beginSuite ("FDN core / decay automation");
    // =======================================================================
    {
        // Decay changes are gain changes. The tank is never rebuilt, so the
        // only risk is a gain step large enough to hear.
        auto maxStep = [] (const std::vector<double>& x, size_t from, size_t to)
        {
            double worst = 0.0;
            for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i)
                worst = std::max (worst, std::abs (x[i] - x[i - 1]));
            return worst;
        };

        auto tank = makeTank (referenceRate, 2.0);

        const auto total = static_cast<size_t> (referenceRate * 6.0);
        const auto firstChange = static_cast<size_t> (referenceRate * 2.0);
        const auto secondChange = static_cast<size_t> (referenceRate * 4.0);

        std::vector<double> out (total);
        const auto noise = whiteNoise (total, 1717u);

        bool finite = true;

        for (size_t n = 0; n < total; ++n)
        {
            if (n == firstChange)  tank->setRt60Seconds (100.0);
            if (n == secondChange) tank->setRt60Seconds (0.1);

            double l = noise[n] * 0.5;
            double r = -l;
            tank->processSample (l, r);

            out[n] = 0.5 * (l + r);
            finite &= std::isfinite (l) && std::isfinite (r);
        }

        // The input is noise, so the sample-to-sample step is large everywhere
        // and says nothing about whether a change was audible. What a listener
        // hears is the envelope, so that is what is measured: the level in
        // consecutive 5 ms windows must never jump.
        auto worstEnvelopeJumpDb = [&] (size_t from, size_t to)
        {
            const auto window = static_cast<size_t> (referenceRate * 0.005);
            double worst = 0.0;
            double previous = -1.0;

            for (size_t start = from; start + window < to; start += window)
            {
                const double level = rms (out.data() + start, window);

                if (previous > 0.0 && level > 0.0)
                    worst = std::max (worst, std::abs (20.0 * std::log10 (level / previous)));

                previous = level;
            }

            return worst;
        };

        const double steadyJump = worstEnvelopeJumpDb (static_cast<size_t> (referenceRate), firstChange);
        const double upJump     = worstEnvelopeJumpDb (firstChange - 4800, firstChange + 24000);
        const double downJump   = worstEnvelopeJumpDb (secondChange - 4800, secondChange + 24000);

        info ("decay 2 s -> 100 s -> 0.1 s, worst 5 ms envelope jump:");
        info ("  steady " + fixed (steadyJump, 2) + " dB, at the jump up " + fixed (upJump, 2)
              + " dB, at the jump down " + fixed (downJump, 2) + " dB");
        info ("  raw sample-to-sample step, steady " + fixed (maxStep (out, static_cast<size_t> (referenceRate), firstChange), 4)
              + " (the input is noise, so this is the signal's own slew rate)");

        check (finite, "instant decay automation stays finite");
        checkAtMost (upJump, steadyJump * 2.0, "jumping to a 100 s decay does not step the level");

        // Collapsing a 100 s decay to 0.1 s is asked to make the tail vanish,
        // so the level has to fall quickly — that is the parameter doing its
        // job, not a click. What must not happen is a discontinuity, and the
        // gain smoother guarantees there is none: the raw sample-to-sample step
        // at the change stays at the signal's own slew rate.
        // Compared against the half second immediately before the drop, not
        // against the opening section: by then the tank has spent two seconds
        // filling at a 100 s decay and is running an order of magnitude louder,
        // so the earlier region is not a fair reference for a raw step size.
        const double rawSteady = maxStep (out, secondChange - 24000, secondChange - 480);
        const double rawAtDrop = maxStep (out, secondChange - 4, secondChange + 4800);

        info ("  raw step at the drop " + fixed (rawAtDrop, 4)
              + " vs the steady " + fixed (rawSteady, 4));

        checkAtMost (rawAtDrop, rawSteady * 2.0,
                     "collapsing the decay produces no sample-level discontinuity");
        checkAtMost (downJump, 12.0,
                     "the level falls fast but by a bounded amount per 5 ms window");

        // Lengthening the decay makes the tank hold more energy, which is the
        // point. What must not happen is that it arrives all at once.
        const double beforeEnergy = energy (out.data() + firstChange - 24000, 24000);
        const double afterEnergy  = energy (out.data() + firstChange, 24000);

        info ("energy in the 0.5 s before and after the jump to 100 s: "
              + fixed (beforeEnergy, 4) + " -> " + fixed (afterEnergy, 4)
              + "  (" + fixed (10.0 * std::log10 (afterEnergy / beforeEnergy), 2) + " dB)");

        checkAtMost (10.0 * std::log10 (afterEnergy / std::max (beforeEnergy, 1.0e-12)), 9.0,
                     "the level rises by less than 9 dB over the half second after the change");
    }

    // =======================================================================
    beginSuite ("FDN core / extreme inputs, silence and reset");
    // =======================================================================
    {
        // Sustained full-scale noise into a long decay.
        auto tank = makeTank (referenceRate, 30.0);

        double worstPeak = 0.0;
        bool finite = true;
        unsigned int state = 9182736u;

        for (int n = 0; n < static_cast<int> (referenceRate) * 60; ++n)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

            double l = x, r = -x;
            tank->processSample (l, r);

            worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
            finite &= std::isfinite (l) && std::isfinite (r);
        }

        info ("60 s of full-scale noise into a 30 s decay: peak " + fixed (worstPeak, 4));

        check (finite, "sustained full-scale noise stays finite");
        checkAtMost (worstPeak, 40.0, "sustained full-scale noise stays bounded");

        // Silence afterwards: the tail must decay away and never go subnormal.
        std::vector<double> tail (static_cast<size_t> (referenceRate) * 200);

        for (size_t n = 0; n < tail.size(); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
            tail[n] = 0.5 * (l + r);
        }

        check (allFinite (tail), "a 200 s silent tail stays finite");
        check (! anySubnormal (tail.data(), tail.size()), "a 200 s silent tail produces no subnormals");

        const double settledMean = mean (tail.data() + tail.size() - 1000000, 1000000);
        const double settledPeak = peakAbsolute (tail.data() + tail.size() - 1000000, 1000000);

        info ("settled silent tail: mean " + std::to_string (settledMean)
              + ", peak " + std::to_string (settledPeak));

        checkAtMost (std::abs (settledMean), 1.0e-20,
                     "the alternating guard leaves no DC in the tank");
        checkAtMost (settledPeak, 1.0e-14, "the settled silent output is far below audibility");

        // Reset must return the tank to exact silence.
        tank->reset();

        double residual = 0.0;

        for (int n = 0; n < static_cast<int> (referenceRate); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
            residual = std::max (residual, std::max (std::abs (l), std::abs (r)));
        }

        checkAtMost (residual, 1.0e-20, "reset clears every delay line completely");

        // Full-scale impulse into the longest decay.
        auto impulseTank = makeTank (referenceRate, 100.0, true);

        double impulsePeak = 0.0;
        bool impulseFinite = true;

        for (int n = 0; n < static_cast<int> (referenceRate) * 10; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            impulseTank->processSample (l, r);

            impulsePeak = std::max (impulsePeak, std::max (std::abs (l), std::abs (r)));
            impulseFinite &= std::isfinite (l) && std::isfinite (r);
        }

        info ("full-scale impulse into a 100 s decay with input diffusion: peak "
              + fixed (impulsePeak, 5));

        check (impulseFinite, "a full-scale impulse stays finite");
        checkAtMost (impulsePeak, 1.0, "a full-scale impulse never exceeds full scale at the output");
    }

    // =======================================================================
    beginSuite ("FDN core / matrix comparison");
    // =======================================================================
    {
        info ("  matrix        T30        R^2      echo density @100 ms   periodicity");

        for (auto type : { StereoFdn<N>::Matrix::Type::hadamard,
                           StereoFdn<N>::Matrix::Type::householder })
        {
            auto tank = makeTank (referenceRate, 2.0);
            tank->setMatrixType (type);
            tank->reset();

            const auto ir = tankImpulseResponse (*tank, static_cast<size_t> (referenceRate * 4.0));
            const auto decay = measureDecay (ir, referenceRate);

            const double ned = normalisedEchoDensity (ir.data(), ir.size(), 0,
                                                      static_cast<size_t> (0.1 * referenceRate));
            const double periodicity = maxEnvelopePeriodicity (
                ir.data(), 32768,
                static_cast<size_t> (0.001 * referenceRate),
                static_cast<size_t> (0.150 * referenceRate));

            const std::string name { StereoFdn<N>::Matrix::describe (type) };

            info ("  " + pad (name, 14) + pad (fixed (decay.rt60FromT30, 4) + " s", 11)
                  + pad (fixed (decay.linearity, 4), 9)
                  + pad (fixed (ned, 4), 23) + fixed (periodicity, 2));

            checkAtMost (std::abs (decay.rt60FromT30 - 2.0) / 2.0, 0.15,
                         name + ": hits the target RT60 (the decay does not depend on the matrix)");
        }
    }

    // =======================================================================
    beginSuite ("FDN core / sample rate and block size");
    // =======================================================================
    {
        info ("  rate       T30        error     echo density @100 ms");

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto tank = makeTank (rate, 2.0);
            const auto ir = tankImpulseResponse (*tank, static_cast<size_t> (rate * 3.0));
            const auto decay = measureDecay (ir, rate);

            const double ned = normalisedEchoDensity (ir.data(), ir.size(), 0,
                                                      static_cast<size_t> (0.1 * rate));
            const double error = std::abs (decay.rt60FromT30 - 2.0) / 2.0;

            info ("  " + pad (fixed (rate / 1000.0, 1) + " kHz", 11)
                  + pad (fixed (decay.rt60FromT30, 4) + " s", 11)
                  + pad (fixed (error * 100.0, 2) + " %", 10)
                  + fixed (ned, 4));

            checkAtMost (error, 0.10,
                         fixed (rate / 1000.0, 1) + " kHz: RT60 is within 10 % of the target");
            check (allFinite (ir), fixed (rate / 1000.0, 1) + " kHz: response is finite");
        }

        // Block size must not change the result.
        auto reference = makeTank (referenceRate, 2.0, true);
        const auto inL = whiteNoise (40000, 606u);
        const auto inR = whiteNoise (40000, 707u);

        std::vector<double> expectedL = inL, expectedR = inR;
        reference->processBlock (expectedL.data(), expectedR.data(),
                                 static_cast<int> (expectedL.size()));

        bool allMatch = true;

        for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 })
        {
            auto tank = makeTank (referenceRate, 2.0, true);
            std::vector<double> l = inL, r = inR;

            int n = 0;
            while (n < static_cast<int> (l.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (l.size()) - n);
                tank->processBlock (l.data() + n, r.data() + n, count);
                n += count;
            }

            for (size_t i = 0; i < l.size(); ++i)
                allMatch &= (l[i] == expectedL[i]) && (r[i] == expectedR[i]);
        }

        check (allMatch, "block sizes 1 to 8192 render bit-identically");

        // A single block far larger than anything prepare() was told about, and
        // a block size that changes on every callback.
        {
            auto tank = makeTank (referenceRate, 2.0, true);
            std::vector<double> l = inL, r = inR;
            tank->processBlock (l.data(), r.data(), static_cast<int> (l.size()));

            bool identical = true;
            for (size_t i = 0; i < l.size(); ++i)
                identical &= (l[i] == expectedL[i]) && (r[i] == expectedR[i]);

            check (identical, "one 40000-sample block matches the sample-by-sample render");
        }

        {
            auto tank = makeTank (referenceRate, 2.0, true);
            std::vector<double> l = inL, r = inR;

            const int sizes[] { 3, 64, 1, 500, 128, 7, 1024, 31 };
            int n = 0, index = 0;

            while (n < static_cast<int> (l.size()))
            {
                const int count = std::min (sizes[index++ % 8], static_cast<int> (l.size()) - n);
                tank->processBlock (l.data() + n, r.data() + n, count);
                n += count;
            }

            bool identical = true;
            for (size_t i = 0; i < l.size(); ++i)
                identical &= (l[i] == expectedL[i]) && (r[i] == expectedR[i]);

            check (identical, "a block size that changes every callback is also bit-identical");
        }

        // Determinism between two identically configured tanks.
        auto a = makeTank (referenceRate, 3.5, true);
        auto b = makeTank (referenceRate, 3.5, true);

        std::vector<double> aL = inL, aR = inR, bL = inL, bR = inR;
        a->processBlock (aL.data(), aR.data(), static_cast<int> (aL.size()));
        b->processBlock (bL.data(), bR.data(), static_cast<int> (bL.size()));

        bool identical = true;
        for (size_t i = 0; i < aL.size(); ++i)
            identical &= (aL[i] == bL[i]) && (aR[i] == bR[i]);

        check (identical, "two identically configured tanks are bit-identical");
    }

    // =======================================================================
    beginSuite ("FDN core / capacity is explicit, never silently truncated");
    // =======================================================================
    {
        auto tank = std::make_unique<StereoFdn<N>>();
        check (succeeded (tank->prepare (referenceRate)), "prepare succeeds");

        check (succeeded (tank->setSizeScale (fdn::maxSizeScale)),
               "the widest Size the tank advertises is accepted");
        check (succeeded (tank->setSizeScale (fdn::minSizeScale)),
               "the narrowest Size is accepted");

        const double sizeBefore = tank->getSizeScale();
        const auto result = tank->setSizeScale (fdn::maxSizeScale * 4.0);

        info ("a Size request four times the reserved capacity returns: "
              + std::string (describe (result)));

        checkClose (tank->getSizeScale(), std::min (fdn::maxSizeScale, fdn::maxSizeScale * 4.0),
                    1.0e-12, "an out-of-range Size is clamped to the advertised maximum");
        (void) sizeBefore;

        // A delay pattern that genuinely does not fit must be refused whole.
        double huge[N];
        for (int i = 0; i < N; ++i)
            huge[i] = 10000.0;

        const auto refused = tank->setDelayPattern (huge);

        check (refused == ConfigResult::exceedsCapacity,
               "a pattern beyond the reserved capacity is refused, not truncated");

        // ...and the tank still works afterwards, on its previous pattern.
        double l = 1.0, r = 1.0;
        tank->processSample (l, r);

        check (std::isfinite (l) && std::isfinite (r),
               "the tank keeps its previous configuration after a refused change");
    }
}

} // namespace avtest
