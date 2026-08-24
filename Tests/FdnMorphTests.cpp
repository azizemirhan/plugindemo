#include "AllocationDetector.h"
#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double referenceRate = 48000.0;

    /** Longest a transition can take: the priming cap plus the fade. The level
        trigger normally starts the fade well before the cap. */
    constexpr double worstCaseTransitionSeconds = fdn::morphPrimeMaxSeconds + fdn::morphSeconds;

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

    std::unique_ptr<MorphingStereoFdn<N>> makeTank (double sampleRate, double rt60)
    {
        auto tank = std::make_unique<MorphingStereoFdn<N>>();
        tank->prepare (sampleRate);
        tank->setRt60Seconds (rt60, true);
        tank->reset();
        return tank;
    }

    /**
        Worst jump between consecutive 5 ms window levels, in dB.

        The input is noise, so raw sample-to-sample steps are large everywhere
        and say nothing about whether a transition was audible. What a listener
        hears is the envelope.
    */
    double worstEnvelopeJumpDb (const std::vector<double>& x, size_t from, size_t to)
    {
        const auto window = static_cast<size_t> (referenceRate * 0.005);

        double worst = 0.0, previous = -1.0;

        for (size_t start = from; start + window < std::min (to, x.size()); start += window)
        {
            const double level = rms (x.data() + start, window);

            if (previous > 0.0 && level > 0.0)
                worst = std::max (worst, std::abs (20.0 * std::log10 (level / previous)));

            previous = level;
        }

        return worst;
    }
}

void runFdnMorphTests()
{
    // =======================================================================
    beginSuite ("FDN morph / Size changes are click-free");
    // =======================================================================
    {
        constexpr size_t total = static_cast<size_t> (referenceRate * 6.0);
        constexpr size_t changeAt = static_cast<size_t> (referenceRate * 3.0);

        auto run = [&] (double fromSize, double toSize)
        {
            auto tank = makeTank (referenceRate, 3.0);
            tank->setSize (fromSize);

            // Let the first morph finish and the tank fill.
            for (int n = 0; n < static_cast<int> (referenceRate); ++n)
            {
                double l = 0.0, r = 0.0;
                tank->processSample (l, r);
            }

            const auto noise = whiteNoise (total, 424242u);
            std::vector<double> out (total);
            bool finite = true;

            for (size_t n = 0; n < total; ++n)
            {
                if (n == changeAt)
                    tank->setSize (toSize);

                double l = noise[n] * 0.3;
                double r = -l;
                tank->processSample (l, r);

                out[n] = 0.5 * (l + r);
                finite &= std::isfinite (l) && std::isfinite (r);
            }

            const double steady = worstEnvelopeJumpDb (out, static_cast<size_t> (referenceRate), changeAt);
            const double during = worstEnvelopeJumpDb (out, changeAt - 4800,
                                                       changeAt + static_cast<size_t> (referenceRate * 0.6));

            return std::array<double, 3> { steady, during, finite ? 1.0 : 0.0 };
        };

        info ("  Size change     steady envelope jump    jump across the transition");

        for (auto pair : { std::pair { 1.0, 2.0 }, std::pair { 1.0, 0.3 },
                           std::pair { 0.3, 2.0 }, std::pair { 2.0, 0.25 } })
        {
            const auto r = run (pair.first, pair.second);

            info ("  " + fixed (pair.first, 2) + " -> " + pad (fixed (pair.second, 2), 8)
                  + pad (fixed (r[0], 2) + " dB", 24) + fixed (r[1], 2) + " dB");

            check (r[2] > 0.5, "Size " + fixed (pair.first, 2) + " -> " + fixed (pair.second, 2)
                               + ": output stays finite");
            checkAtMost (r[1], std::max (r[0] * 1.5, 3.0),
                         "Size " + fixed (pair.first, 2) + " -> " + fixed (pair.second, 2)
                         + ": the transition never jumps the level");
        }
    }

    // =======================================================================
    beginSuite ("FDN morph / energy continuity");
    // =======================================================================
    {
        // An equal-power crossfade between two uncorrelated tails should keep
        // the level flat. A linear fade would dip about 3 dB in the middle.
        auto tank = makeTank (referenceRate, 4.0);
        tank->setSize (1.0);

        const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 8.0), 31415u);
        std::vector<double> out (noise.size());

        const auto changeAt = static_cast<size_t> (referenceRate * 4.0);

        for (size_t n = 0; n < noise.size(); ++n)
        {
            if (n == changeAt)
                tank->setSize (1.8);

            double l = noise[n] * 0.3;
            double r = -l;
            tank->processSample (l, r);
            out[n] = 0.5 * (l + r);
        }

        // RMS in short windows across the whole transition, priming included.
        // A generous span, because the level trigger decides when the fade
        // starts and that depends on how fast the incoming tank fills.
        const auto windowSamples     = static_cast<size_t> (referenceRate * 0.02);
        const auto transitionSamples = static_cast<size_t> (referenceRate * 2.0);

        auto variationDb = [&] (const std::vector<double>& signal)
        {
            double lowest = 1.0e9, highest = 0.0;

            for (size_t start = changeAt - windowSamples;
                 start + windowSamples < changeAt + transitionSamples;
                 start += windowSamples / 2)
            {
                const double level = rms (signal.data() + start, windowSamples);
                lowest  = std::min (lowest, level);
                highest = std::max (highest, level);
            }

            return std::abs (20.0 * std::log10 (std::max (lowest / highest, 1.0e-30)));
        };

        const double dipDb = variationDb (out);

        // Before that number can mean anything, the two sizes' own steady
        // levels have to be known. They are not the same, and that is physics
        // rather than a defect: at a fixed RT60 a longer delay set has smaller
        // per-line gains, so the tank stores less energy per unit of input. A
        // bigger room with the same decay time is a quieter room.
        auto steadyLevel = [&] (double size)
        {
            auto probe = makeTank (referenceRate, 4.0);
            probe->setSize (size);

            std::vector<double> quiet (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double l = noise[n] * 0.3;
                double r = -l;
                probe->processSample (l, r);
                quiet[n] = 0.5 * (l + r);
            }

            // Well past any transition, and past the tank filling.
            return rms (quiet.data() + quiet.size() / 2, quiet.size() / 4);
        };

        const double levelAtOne   = steadyLevel (1.0);
        const double levelAtEight = steadyLevel (1.8);
        const double sizeStepDb   = 20.0 * std::log10 (levelAtEight / levelAtOne);

        info ("steady level at Size 1.0 " + fixed (levelAtOne, 5)
              + ", at Size 1.8 " + fixed (levelAtEight, 5)
              + "  (" + fixed (sizeStepDb, 2) + " dB apart at the same RT60)");
        info ("level variation measured across the transition: " + fixed (dipDb, 2) + " dB");

        check (sizeStepDb < 0.0,
               "a larger tank is quieter at the same decay time, as the physics requires");

        // The real question is whether the transition ever drops below where it
        // is going, which is what a dropout would look like.
        const double floorLevel = std::min (levelAtOne, levelAtEight);
        double lowestDuring = 1.0e9;

        for (size_t start = changeAt - windowSamples;
             start + windowSamples < changeAt + transitionSamples;
             start += windowSamples / 2)
            lowestDuring = std::min (lowestDuring, rms (out.data() + start, windowSamples));

        const double underShootDb = 20.0 * std::log10 (lowestDuring / floorLevel);

        info ("lowest level during the transition sits " + fixed (underShootDb, 2)
              + " dB relative to the quieter of the two steady states");

        check (underShootDb > -1.5,
               "the transition never drops more than 1.5 dB below either steady state");

        // How long the priming has to be, measured rather than guessed. The
        // incoming tank fills with a time constant of roughly RT60/13.8, so the
        // answer depends on the decay; the sweep is run at a 4 s decay, which
        // is a demanding but ordinary setting.
        // What the level trigger is worth, against fixed prime times. A fixed
        // time cannot win here: the incoming tank fills at a rate set by the
        // decay, and the sweep below shows even half a second falling short at
        // a four-second decay.
        info ("");
        info ("  priming strategy          level variation across the transition");

        auto runWithPriming = [&] (double capSeconds, bool settlingTrigger)
        {
            auto probe = makeTank (referenceRate, 4.0);
            probe->setPrimeCapSeconds (capSeconds);
            probe->setPrimeMinimumSeconds (std::min (capSeconds, fdn::morphPrimeMinSeconds));
            probe->setSettlingTriggerEnabled (settlingTrigger);
            probe->setSize (1.0);

            std::vector<double> raw (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                if (n == changeAt)
                    probe->setSize (1.8);

                double l = noise[n] * 0.3;
                double r = -l;
                probe->processSample (l, r);
                raw[n] = 0.5 * (l + r);
            }

            return variationDb (raw);
        };

        const double noPriming = runWithPriming (0.0, false);
        info ("  " + pad ("none", 26) + fixed (noPriming, 2) + " dB");

        for (double fixedPrime : { 0.12, 0.25, 0.50, 1.50 })
            info ("  " + pad ("fixed " + fixed (fixedPrime * 1000.0, 0) + " ms", 26)
                  + fixed (runWithPriming (fixedPrime, false), 2) + " dB");

        info ("  " + pad ("settling trigger", 26)
              + fixed (runWithPriming (fdn::morphPrimeMaxSeconds, true), 2) + " dB");

        info ("shipped: settling trigger, warm-up at least "
              + fixed (fdn::morphPrimeMinSeconds * 1000.0, 0) + " ms, capped at "
              + fixed (fdn::morphPrimeMaxSeconds * 1000.0, 0)
              + " ms, then a " + fixed (fdn::morphSeconds * 1000.0, 0) + " ms equal-power fade");

        check (dipDb < noPriming - 4.0,
               "priming is worth at least 4 dB over no priming at all");

        // No burst either.
        const double before = rms (out.data() + changeAt - windowSamples * 4, windowSamples * 4);
        const double after  = rms (out.data() + changeAt + transitionSamples, windowSamples * 4);

        info ("RMS before " + fixed (before, 5) + ", after " + fixed (after, 5));

        checkAtMost (after / std::max (before, 1.0e-12), 2.0, "the morph does not burst energy");
    }

    // =======================================================================
    beginSuite ("FDN morph / rapid automation and target coalescing");
    // =======================================================================
    {
        auto tank = makeTank (referenceRate, 3.0);

        // A Size value on every single sample, sweeping continuously. Without
        // coalescing this would try to start a morph 480000 times.
        int morphsStarted = 0;
        bool wasMorphing = false;
        bool finite = true;
        double worstPeak = 0.0;

        const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 10.0), 987u);

        for (size_t n = 0; n < noise.size(); ++n)
        {
            const double phase = 2.0 * 3.14159265358979323846 * 0.5
                               * static_cast<double> (n) / referenceRate;
            tank->setSize (1.0 + 0.7 * std::sin (phase));

            double l = noise[n] * 0.3;
            double r = -l;
            tank->processSample (l, r);

            finite &= std::isfinite (l) && std::isfinite (r);
            worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));

            if (tank->isMorphing() && ! wasMorphing)
                ++morphsStarted;

            wasMorphing = tank->isMorphing();
        }

        // 480000 Size requests went in. Without coalescing each would try to
        // start a transition; the bound is one per completed transition, and
        // the fastest a transition can complete is the fade length alone.
        const double expectedMax = 10.0 / fdn::morphSeconds;

        info ("10 s of continuous Size automation at 0.5 Hz, a new target every sample:");
        info ("  Size requests  " + std::to_string (static_cast<int> (noise.size())));
        info ("  morphs started " + std::to_string (morphsStarted)
              + "  (upper bound if every transition took only the fade time: "
              + fixed (expectedMax, 0) + ")");
        info ("  worst peak " + fixed (worstPeak, 4));

        check (finite, "continuous Size automation stays finite");
        checkAtMost (static_cast<double> (morphsStarted), expectedMax + 2.0,
                     "coalescing bounds the morph count by the transition length, "
                     "not by the number of requests");
        checkAtMost (worstPeak, 8.0, "continuous Size automation does not run away");

        // Only the most recent target survives a burst of requests.
        auto coalescing = makeTank (referenceRate, 2.0);
        coalescing->setSize (2.0);

        check (coalescing->isMorphing(), "the first Size request starts a morph");

        for (double s : { 1.5, 0.5, 1.2, 0.8 })
            coalescing->setSize (s);

        check (coalescing->hasPendingSize(), "further requests are queued, not started");

        // Run past the end of the crossfade; the queued target is then applied.
        for (int n = 0; n < static_cast<int> (referenceRate * worstCaseTransitionSeconds) + 16; ++n)
        {
            double l = 0.0, r = 0.0;
            coalescing->processSample (l, r);
        }

        checkClose (coalescing->getSize(), 0.8, 1.0e-9,
                    "the last requested Size is the one that takes effect");
        check (! coalescing->hasPendingSize(), "the queue is empty once the pending target is applied");

        // A change smaller than the threshold must not retrigger anything.
        auto quiet = makeTank (referenceRate, 2.0);

        for (int n = 0; n < 48000; ++n)
        {
            double l = 0.0, r = 0.0;
            quiet->processSample (l, r);
        }

        quiet->setSize (1.0 + fdn::sizeMorphThreshold * 0.4);
        check (! quiet->isMorphing(), "a sub-threshold Size change is ignored");
    }

    // =======================================================================
    beginSuite ("FDN morph / the transition always completes");
    // =======================================================================
    {
        // A level trigger alone can hang. Every input below is one that could
        // stop the envelopes from ever converging, and every one of them has to
        // still finish in bounded time.
        using Tank = MorphingStereoFdn<N>;

        info ("  input                       priming ended     after      finished");

        auto runScenario = [&] (const char* label, double rt60,
                                const std::function<double (int)>& source)
        {
            auto tank = makeTank (referenceRate, rt60);

            // Fill first, so there is something to fade out of.
            for (int n = 0; n < static_cast<int> (referenceRate); ++n)
            {
                double l = source (n), r = -l;
                tank->processSample (l, r);
            }

            tank->setSize (1.7);

            const int ceiling = tank->getWorstCaseTransitionSamples();
            int elapsed = 0;
            bool finite = true;

            while (tank->isMorphing() && elapsed < ceiling * 4)
            {
                double l = source (static_cast<int> (referenceRate) + elapsed);
                double r = -l;
                tank->processSample (l, r);

                finite &= std::isfinite (l) && std::isfinite (r);
                ++elapsed;
            }

            const bool finished = ! tank->isMorphing();

            info ("  " + pad (label, 28)
                  + pad (Tank::describe (tank->getLastPrimeReason()), 18)
                  + pad (fixed (1000.0 * tank->getLastPrimeSamples() / referenceRate, 1) + " ms", 11)
                  + (finished ? "yes" : "NO"));

            check (finished, std::string (label) + ": the transition completes");
            check (finite, std::string (label) + ": output stays finite throughout");
            checkAtMost (static_cast<double> (elapsed), static_cast<double> (ceiling),
                         std::string (label) + ": it completes inside the worst-case bound");

            return tank->getLastPrimeReason();
        };

        const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 8.0), 90210u);

        const auto silence = runScenario ("silence", 4.0,
                                          [] (int) { return 0.0; });

        const auto tiny = runScenario ("input at -140 dBFS", 4.0,
                                       [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 1.0e-7; });

        const auto impulse = runScenario ("a single short impulse", 4.0,
                                          [] (int n) { return n == 0 ? 1.0 : 0.0; });

        const auto shortDecay = runScenario ("full scale, 0.3 s decay", 0.3,
                                             [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 0.5; });

        const auto longDecay = runScenario ("full scale, 60 s decay", 60.0,
                                            [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 0.5; });

        check (silence == Tank::PrimeReason::silence,
               "silence is recognised as such rather than waiting for a level that never arrives");

        (void) impulse;
        (void) tiny;
        (void) longDecay;
        (void) shortDecay;

        // Which of the three routes fires is not the property that matters —
        // any of them is a correct way to finish. What matters is that the
        // waiting time tracks the decay, so a short reverb does not sit through
        // a ceiling meant for a long one.
        info ("");
        info ("  decay      priming stage    ceiling from the decay");

        for (double rt60 : { 0.3, 1.0, 2.0, 4.0, 10.0, 60.0 })
        {
            auto tank = makeTank (referenceRate, rt60);

            for (int n = 0; n < static_cast<int> (referenceRate * 2.0); ++n)
            {
                double l = noise[static_cast<size_t> (n) % noise.size()] * 0.5;
                double r = -l;
                tank->processSample (l, r);
            }

            tank->setSize (1.7);

            int elapsed = 0;
            while (tank->isPriming() && elapsed < 480000)
            {
                double l = noise[static_cast<size_t> (elapsed) % noise.size()] * 0.5;
                double r = -l;
                tank->processSample (l, r);
                ++elapsed;
            }

            const double expectedCap = std::clamp (fdn::morphPrimeDecayFactor * rt60,
                                                   fdn::morphPrimeMinSeconds,
                                                   fdn::morphPrimeMaxSeconds);

            info ("  " + pad (fixed (rt60, 1) + " s", 11)
                  + pad (fixed (1000.0 * elapsed / referenceRate, 1) + " ms", 17)
                  + fixed (expectedCap * 1000.0, 1) + " ms  ("
                  + Tank::describe (tank->getLastPrimeReason()) + ")");

            checkAtMost (1000.0 * elapsed / referenceRate, expectedCap * 1000.0 + 1.0,
                         fixed (rt60, 1) + " s: priming never exceeds the decay-derived ceiling");
            check (1000.0 * elapsed / referenceRate >= fdn::morphPrimeMinSeconds * 1000.0 - 1.0,
                   fixed (rt60, 1) + " s: priming never ends before the warm-up");
        }

        info ("a short decay waits " + fixed (fdn::morphPrimeMinSeconds * 1000.0, 0)
              + " ms and a long one at most " + fixed (fdn::morphPrimeMaxSeconds * 1000.0, 0)
              + " ms, so the transition is always bounded and always tracks the decay");

        info ("a 60 s decay ended priming by: " + std::string (Tank::describe (longDecay)));

        // The minimum warm-up: a Size change must never fade in a tank whose
        // longest delay has not yet produced anything.
        {
            auto tank = makeTank (referenceRate, 0.3);

            for (int n = 0; n < static_cast<int> (referenceRate); ++n)
            {
                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
            }

            tank->setSize (1.9);

            int elapsed = 0;
            while (tank->isPriming() && elapsed < 480000)
            {
                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
                ++elapsed;
            }

            const double warmupMs = 1000.0 * elapsed / referenceRate;
            const double longestDelayMs = fdn::delaysMs[fdn::order - 1] * 1.9;

            info ("shortest observed priming stage: " + fixed (warmupMs, 1)
                  + " ms, against a longest delay of " + fixed (longestDelayMs, 1) + " ms");

            check (warmupMs >= fdn::morphPrimeMinSeconds * 1000.0 - 0.1,
                   "priming never ends before the minimum warm-up");
            check (warmupMs >= longestDelayMs,
                   "the warm-up covers the longest delay in the incoming tank");
        }

        // Continuous Size targets during a transition must not extend it.
        {
            auto tank = makeTank (referenceRate, 4.0);

            for (int n = 0; n < static_cast<int> (referenceRate); ++n)
            {
                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
            }

            const int ceiling = tank->getWorstCaseTransitionSamples();
            tank->setSize (1.8);

            int elapsed = 0;
            int transitionsSeen = 0;
            int lastBank = tank->getActiveBankIndex();

            while (elapsed < ceiling * 3)
            {
                // A brand new target on every single sample.
                tank->setSize (0.3 + 1.6 * static_cast<double> (elapsed % 1000) / 1000.0);

                double l = 0.3, r = -0.3;
                tank->processSample (l, r);

                // Counted as bank swaps, not as isMorphing() going false. With
                // a target arriving every sample the queued one starts the next
                // transition in the same call, so the flag never reads false
                // and counting its edges would report zero for a system that is
                // in fact completing transitions steadily.
                if (tank->getActiveBankIndex() != lastBank)
                {
                    ++transitionsSeen;
                    lastBank = tank->getActiveBankIndex();
                }

                ++elapsed;
            }

            info ("three worst-case windows of one Size target per sample produced "
                  + std::to_string (transitionsSeen) + " completed transitions");

            check (transitionsSeen >= 1,
                   "a transition still completes while targets keep arriving");
            checkAtMost (static_cast<double> (transitionsSeen), 6.0,
                         "coalescing keeps completed transitions bounded");
        }
    }

    // =======================================================================
    beginSuite ("FDN morph / dual-bank behaviour");
    // =======================================================================
    {
        auto tank = makeTank (referenceRate, 3.0);

        const int firstBank = tank->getActiveBankIndex();
        tank->setSize (1.6);

        check (tank->isMorphing(), "a Size change starts a crossfade");
        checkClose (tank->getActiveBankIndex(), firstBank, 0,
                    "the active bank does not change until the crossfade completes");

        const auto morphSamples = static_cast<int> (referenceRate * worstCaseTransitionSeconds);

        for (int n = 0; n < morphSamples + 8; ++n)
        {
            double l = 0.1, r = -0.1;
            tank->processSample (l, r);
        }

        check (! tank->isMorphing(), "the crossfade completes on schedule");
        check (tank->getActiveBankIndex() != firstBank, "the banks swap when it completes");

        // The retired bank is cleared, so the next morph starts from silence.
        auto& retired = tank->getBank (firstBank);
        double residual = 0.0;

        for (int n = 0; n < 4800; ++n)
        {
            double l = 0.0, r = 0.0;
            retired.processSample (l, r);
            residual = std::max (residual, std::max (std::abs (l), std::abs (r)));
        }

        checkAtMost (residual, 1.0e-20, "the retired bank is cleared once it has faded out");

        // Both banks carry the same decay, so the crossfade is between two
        // sizes and nothing else.
        checkClose (tank->getBank (0).getRt60Seconds(), tank->getBank (1).getRt60Seconds(), 1.0e-12,
                    "both banks share the decay setting");

        // Allocation: the whole morph machinery must be allocation free.
        long long duringMorphs = 0;
        {
            auto counted = makeTank (referenceRate, 2.0);

            ScopedAllocationCounter counter;

            for (int cycle = 0; cycle < 40; ++cycle)
            {
                counted->setSize (cycle % 2 == 0 ? 1.9 : 0.4);

                for (int n = 0; n < morphSamples + 64; ++n)
                {
                    double l = 0.05, r = -0.05;
                    counted->processSample (l, r);
                }
            }

            duringMorphs = counter.count();
        }

        checkNoAllocations (duringMorphs,
                    "40 complete Size morphs allocate nothing");
    }

    // =======================================================================
    beginSuite ("FDN morph / sample rate, block size and reset");
    // =======================================================================
    {
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto tank = makeTank (rate, 2.0);
            tank->setSize (1.7);

            bool finite = true;
            double worstPeak = 0.0;

            for (int n = 0; n < static_cast<int> (rate); ++n)
            {
                double l = (n == 0) ? 1.0 : 0.0;
                double r = l;
                tank->processSample (l, r);

                finite &= std::isfinite (l) && std::isfinite (r);
                worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
            }

            check (finite, fixed (rate / 1000.0, 1) + " kHz: morphing output stays finite");
            checkAtMost (worstPeak, 2.0, fixed (rate / 1000.0, 1) + " kHz: no peak above 2.0");
        }

        // Block size independence, with a morph running across the boundaries.
        const auto inL = whiteNoise (30000, 1234u);
        const auto inR = whiteNoise (30000, 5678u);

        auto render = [&] (int blockSize)
        {
            auto tank = makeTank (referenceRate, 2.0);
            tank->setSize (1.5);

            std::vector<double> l = inL, r = inR;
            int n = 0;

            while (n < static_cast<int> (l.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (l.size()) - n);
                tank->processBlock (l.data() + n, r.data() + n, count);
                n += count;
            }

            return std::pair { l, r };
        };

        const auto expected = render (static_cast<int> (inL.size()));
        bool allMatch = true;

        for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 })
        {
            const auto rendered = render (blockSize);

            for (size_t i = 0; i < expected.first.size(); ++i)
                allMatch &= (rendered.first[i] == expected.first[i])
                         && (rendered.second[i] == expected.second[i]);
        }

        check (allMatch, "block sizes 1 to 8192 render a morph bit-identically");

        // Reset during a morph must cancel it cleanly.
        auto tank = makeTank (referenceRate, 2.0);
        tank->setSize (1.9);

        for (int n = 0; n < 1000; ++n)
        {
            double l = 0.5, r = -0.5;
            tank->processSample (l, r);
        }

        check (tank->isMorphing(), "a morph is in progress before the reset");

        tank->reset();

        check (! tank->isMorphing(), "reset cancels the morph");

        double residual = 0.0;

        for (int n = 0; n < 48000; ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
            residual = std::max (residual, std::max (std::abs (l), std::abs (r)));
        }

        checkAtMost (residual, 1.0e-20, "reset clears both banks completely");
    }
}

} // namespace avtest
