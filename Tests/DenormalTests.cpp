#include "Suites.h"
#include "TestFramework.h"

#include "DSP/DelayLine.h"
#include "DSP/Denormal.h"
#include "DSP/DspConfig.h"

#include <chrono>
#include <cmath>
#include <string>

using namespace av::dsp;

namespace avtest
{

namespace
{
    /** Smallest normal double. Anything below this is subnormal. */
    constexpr double smallestNormal = 2.2250738585072014e-308;

    /** A leaky integrator, the shape every reverb feedback path reduces to. */
    template <bool useGuard>
    double decayUntilTiny (int iterations, bool& sawSubnormal)
    {
        volatile double state = 1.0;
        sawSubnormal = false;

        for (int n = 0; n < iterations; ++n)
        {
            double next = state * 0.9;

            if constexpr (useGuard)
                next += denormalGuard;

            state = next;

            if (isSubnormal (static_cast<double> (state)))
                sawSubnormal = true;
        }

        return state;
    }
}

void runDenormalTests()
{
    // =======================================================================
    beginSuite ("Denormal / CPU control");
    // =======================================================================
    {
        check (ScopedFtz::isSupported(), "this build can control the FPU's subnormal handling");
        check (! ScopedFtz::isCurrentlyEnabled(), "flush-to-zero is off by default");

        {
            ScopedFtz ftz;
            check (ScopedFtz::isCurrentlyEnabled(), "ScopedFtz enables flush-to-zero and DAZ");
        }

        check (! ScopedFtz::isCurrentlyEnabled(),
               "ScopedFtz restores the host's floating-point state on exit");

        // Nesting must not lose the outer state.
        {
            ScopedFtz outer;
            {
                ScopedFtz inner;
                check (ScopedFtz::isCurrentlyEnabled(), "nested scope keeps flush-to-zero on");
            }
            check (ScopedFtz::isCurrentlyEnabled(), "inner scope restores to the outer scope's state");
        }

        check (! ScopedFtz::isCurrentlyEnabled(), "state is fully restored after nesting");
    }

    // =======================================================================
    beginSuite ("Denormal / arithmetic behaviour");
    // =======================================================================
    {
        volatile double tiny = 1.0e-320; // subnormal for double

        check (isSubnormal (static_cast<double> (tiny)), "the test value really is subnormal");

        double withoutFtz = 0.0;
        {
            volatile double t = tiny;
            withoutFtz = t * 0.5;
        }

        check (withoutFtz != 0.0 && isSubnormal (withoutFtz),
               "without protection a subnormal operand yields another subnormal");

        double withFtz = 0.0;
        {
            ScopedFtz ftz;
            volatile double t = tiny;
            withFtz = t * 0.5;
        }

        checkClose (withFtz, 0.0, 0.0, "under ScopedFtz the same operation flushes to zero");

        checkClose (av::flushDenormal (1.0e-30), 0.0, 0.0, "flushDenormal kills values near the subnormal range");
        checkClose (av::flushDenormal (0.5), 0.5, 0.0, "flushDenormal preserves normal values");
        checkClose (av::flushDenormal (-0.5), -0.5, 0.0, "flushDenormal preserves negative values");
    }

    // =======================================================================
    beginSuite ("Denormal / feedback paths");
    // =======================================================================
    {
        // A decaying feedback path with no protection is the failure case: it
        // walks straight through the subnormal range on its way to zero.
        bool sawSubnormal = false;
        (void) decayUntilTiny<false> (8000, sawSubnormal);
        check (sawSubnormal, "an unprotected decaying feedback path does enter the subnormal range");

        // The additive guard parks it at a small *normal* value instead.
        bool guardedSawSubnormal = false;
        const double settled = decayUntilTiny<true> (20000, guardedSawSubnormal);

        check (! guardedSawSubnormal, "the additive guard never lets the state become subnormal");
        check (settled >= smallestNormal, "guarded state stays in the normal range");

        // It settles where the algebra says it should: guard / (1 - gain).
        checkClose (settled, denormalGuard / (1.0 - 0.9), denormalGuard * 0.01,
                    "guarded state settles at guard/(1-gain)");

        // ...and the guard is far below anything audible.
        const double guardDb = 20.0 * std::log10 (settled);
        info ("guarded steady state: " + std::to_string (settled)
              + "  (" + std::to_string (guardDb) + " dBFS)");
        check (guardDb < -400.0, "the guard sits below -400 dBFS, i.e. inaudible");
    }

    // =======================================================================
    beginSuite ("Denormal / long reverb tail");
    // =======================================================================
    {
        // The realistic case: a 1000-sample recirculating delay tuned to a 2 s
        // RT60, fed one impulse, then left in silence.
        //
        // The arithmetic is worth stating, because it explains why this is a
        // real problem rather than a theoretical one. Reaching the subnormal
        // range means falling ~6160 dB below the impulse. At RT60 = 2 s that
        // takes a little over 200 seconds. A DAW left paused with a reverb on
        // an idle track gets there in three and a half minutes.

        constexpr double sampleRate   = 48000.0;
        constexpr double rt60Seconds  = 2.0;
        constexpr int    loopSamples  = 1000;
        constexpr double totalSeconds = 240.0;

        const int    totalSamples = static_cast<int> (sampleRate * totalSeconds);
        const double feedback     = std::pow (10.0, -3.0 * (loopSamples / sampleRate) / rt60Seconds);

        info ("loop gain for RT60 = 2 s with a 1000 sample loop: " + std::to_string (feedback));

        DelayLine<double> line;
        line.prepare (sampleRate, 0.05);

        bool anySubnormal = false, anyNonFinite = false;
        int  firstSubnormalSample = -1;

        auto runTail = [&] (bool useGuard)
        {
            line.reset();
            anySubnormal = false;
            anyNonFinite = false;
            firstSubnormalSample = -1;

            const auto start = std::chrono::steady_clock::now();

            for (int n = 0; n < totalSamples; ++n)
            {
                const double delayed = line.readAt (loopSamples);
                double next = delayed * feedback + (n == 0 ? 1.0 : 0.0);

                if (useGuard)
                    next += denormalGuard;

                line.write (next);

                if (isSubnormal (next))
                {
                    anySubnormal = true;
                    if (firstSubnormalSample < 0)
                        firstSubnormalSample = n;
                }

                if (! std::isfinite (next))
                    anyNonFinite = true;
            }

            const auto elapsed = std::chrono::steady_clock::now() - start;
            return std::chrono::duration<double, std::milli> (elapsed).count();
        };

        const double guardedMs = runTail (true);

        check (! anySubnormal, "240 s guarded tail never produces a subnormal");
        check (! anyNonFinite, "240 s guarded tail stays finite");

        const double unguardedMs = runTail (false);

        check (anySubnormal, "the same tail without the guard does reach the subnormal range");

        if (firstSubnormalSample >= 0)
            info ("unguarded tail became subnormal after "
                  + std::to_string (firstSubnormalSample / sampleRate) + " s of silence");

        info ("240 s of tail at 48 kHz:  guarded " + std::to_string (guardedMs)
              + " ms,  unguarded " + std::to_string (unguardedMs) + " ms");

        // The point of the guard: a silent tail must not cost more than a loud
        // one. Measured with flush-to-zero off, which is the worst case.
        check (guardedMs <= unguardedMs * 1.2,
               "the guarded path is not slower than the unguarded one");
    }
}

} // namespace avtest
