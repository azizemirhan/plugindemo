#include "AllocationDetector.h"
#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
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

    using Morph = MorphingStereoFdn<N>;

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

    /** The preview chain as the processor drives it, minus JUCE. */
    struct PreviewChain
    {
        std::unique_ptr<Morph> tank;

        void prepare (double sampleRate)
        {
            tank = std::make_unique<Morph>();
            tank->prepare (sampleRate);
            tank->setInputDiffusionEnabled (true);
            tank->setAllpassDepth (0.0, true);
            tank->setModulationDepth (0.0, true);
            tank->setCompensationEnabled (true, true);
            setControls (2.5, 0.60, 0.30, 0.5, true);
        }

        void setControls (double decaySeconds, double sizeControl,
                          double depthControl, double rateControlHz,
                          bool immediate = false)
        {
            tank->setRt60Seconds (mapping::rt60For (decaySeconds), immediate);
            tank->setSize (mapping::sizeScaleFor (sizeControl));

            architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                          mapping::modDepthFor (depthControl),
                                          mapping::modRateScaleFor (rateControlHz),
                                          immediate);
        }

        void process (double& l, double& r) { tank->processSample (l, r); }
    };

    /** What the processor applies to the wet path, for the settings in use. */
    double previewTrimFor (double decaySeconds, double sizeControl)
    {
        return mapping::previewWetTrim (mapping::rt60For (decaySeconds),
                                        mapping::sizeScaleFor (sizeControl));
    }
}

void runPreviewIntegrationTests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("Preview / parameter mapping");
    // =======================================================================
    {
        // The curves are pure functions, so they can be checked exactly rather
        // than by loading a host and listening.
        info ("  control    size scale   depth      rate Hz    clamped");

        for (double u : { 0.0, 0.2, 0.4, 0.6, 0.667, 0.8, 1.0 })
        {
            const double controlHz = 0.01 + u * (10.0 - 0.01);

            info ("  " + pad (fixed (u, 3), 11)
                  + pad (fixed (mapping::sizeScaleFor (u), 4), 13)
                  + pad (fixed (mapping::modDepthFor (u), 4), 11)
                  + pad (fixed (mapping::allpassRateFor (controlHz), 4), 11)
                  + (mapping::rateIsClamped (controlHz) ? "yes" : "no"));
        }

        // Monotone, and hitting the ends exactly.
        bool sizeMonotone = true, depthMonotone = true, rateMonotone = true;
        double previousSize = -1.0, previousDepth = -1.0, previousRate = -1.0;

        for (int i = 0; i <= 1000; ++i)
        {
            const double u = static_cast<double> (i) / 1000.0;
            const double controlHz = 0.01 + u * (10.0 - 0.01);

            const double size = mapping::sizeScaleFor (u);
            const double depth = mapping::modDepthFor (u);
            const double rate = mapping::allpassRateFor (controlHz);

            sizeMonotone  = sizeMonotone  && size  >= previousSize;
            depthMonotone = depthMonotone && depth >= previousDepth;
            rateMonotone  = rateMonotone  && rate  >= previousRate;

            previousSize = size; previousDepth = depth; previousRate = rate;
        }

        check (sizeMonotone,  "Size is monotone across its whole travel");
        check (depthMonotone, "Mod Depth is monotone across its whole travel");
        check (rateMonotone,  "Mod Rate is monotone across its whole travel");

        checkClose (mapping::sizeScaleFor (0.0), fdn::minSizeScale, 1.0e-12,
                    "Size at 0 % is exactly the smallest tank");
        checkClose (mapping::sizeScaleFor (1.0), fdn::maxSizeScale, 1.0e-12,
                    "Size at 100 % is exactly the largest tank");

        checkClose (mapping::modDepthFor (0.0), 0.0, 1.0e-15,
                    "Mod Depth at 0 % is exactly zero, so bypass is reachable");
        checkClose (mapping::modDepthFor (1.0), 1.0, 1.0e-12,
                    "Mod Depth at 100 % reaches the full research range");

        // The point of the curve: the control's own default lands on the
        // setting the measurements chose.
        checkClose (mapping::modDepthFor (mapping::depthControlDefault),
                    allpassMod::defaultDepth, 1.0e-9,
                    "the Mod Depth default position produces the measured default depth");

        checkClose (mapping::allpassRateFor (mapping::rateControlDefaultHz),
                    allpassMod::defaultRateHz, 1.0e-12,
                    "the Mod Rate default position produces the measured default rate");

        // Round trip.
        double worstRoundTrip = 0.0;

        for (int i = 0; i <= 100; ++i)
        {
            const double u = static_cast<double> (i) / 100.0;
            worstRoundTrip = std::max (worstRoundTrip,
                                       std::abs (mapping::sizeControlFor (mapping::sizeScaleFor (u)) - u));
        }

        checkAtMost (worstRoundTrip, 1.0e-12, "the Size mapping round-trips exactly");

        info ("");
        info ("Mod Rate is a scale on the measured default rather than a raw frequency: the");
        info ("engine was measured over 0.02 to 2.0 Hz and the control's range runs to 10 Hz,");
        info ("so the top of the travel clamps. Reported rather than hidden — the alternative");
        info ("is a control that reaches settings no measurement supports.");
    }

    // =======================================================================
    beginSuite ("Preview / depth zero equivalence");
    // =======================================================================
    {
        // Mod Depth at 0 has to give the static tank, with both engines
        // bypassed and the LFO phases still advancing.
        PreviewChain modulated, plain;

        modulated.prepare (referenceRate);
        plain.prepare (referenceRate);

        modulated.setControls (2.5, 0.60, 0.0, 0.5, true);

        plain.tank->setRt60Seconds (mapping::rt60For (2.5), true);
        plain.tank->setSize (mapping::sizeScaleFor (0.60));
        plain.tank->setAllpassDepth (0.0, true);
        plain.tank->setModulationDepth (0.0, true);

        // Both need the Size morph to finish before comparing.
        for (int n = 0; n < static_cast<int> (referenceRate * 3.0); ++n)
        {
            double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
            modulated.process (a, b);
            plain.process (c, d);
        }

        const auto noise = whiteNoise (200000, 3030u);

        bool identical = true;
        double worst = 0.0;

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double al = noise[n], ar = noise[n];
            double bl = noise[n], br = noise[n];

            modulated.process (al, ar);
            plain.process (bl, br);

            worst = std::max (worst, std::max (std::abs (al - bl), std::abs (ar - br)));
            identical = identical && al == bl && ar == br;
        }

        info ("worst difference at Mod Depth 0: " + fixed (worst, 12));

        check (identical, "Mod Depth 0 through the preview chain is the static tank exactly");
    }

    // =======================================================================
    beginSuite ("Preview / automation");
    // =======================================================================
    {
        info ("  automation                          worst 10 ms jump   finite");

        const auto automate = [&] (const std::string& label, auto&& act)
        {
            PreviewChain chain;
            chain.prepare (referenceRate);

            const auto length = static_cast<size_t> (referenceRate * 12.0);
            const auto settle = static_cast<size_t> (referenceRate * 3.0);
            const auto noise = whiteNoise (length, 6060u);

            std::vector<double> envelope (length);
            bool finite = true;
            double peak = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                act (chain, n);

                double l = 0.2 * noise[n], r = 0.2 * noise[n];
                chain.process (l, r);

                const double trim = previewTrimFor (2.5, 0.60);
                l *= trim;
                r *= trim;

                finite = finite && std::isfinite (l) && std::isfinite (r);
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
                envelope[n] = 0.5 * (std::abs (l) + std::abs (r));
            }

            const auto window = static_cast<size_t> (0.010 * referenceRate);

            double previous = rms (envelope.data() + settle, window);
            double worst = 0.0;

            for (size_t start = settle + window; start + window < length; start += window)
            {
                const double level = rms (envelope.data() + start, window);

                if (level > 1.0e-12 && previous > 1.0e-12)
                    worst = std::max (worst, std::abs (20.0 * std::log10 (level / previous)));

                previous = level;
            }

            info ("  " + pad (label, 36) + pad (fixed (worst, 2) + " dB", 19)
                  + (finite ? "yes" : "NO"));

            check (finite, label + ": stays finite");
            checkAtMost (worst, 4.0, label + ": no level step");
            checkAtMost (peak, 4.0, label + ": no uncontrolled peak");
        };

        const auto at = [] (double seconds) { return static_cast<size_t> (referenceRate * seconds); };

        automate ("Mod Depth 0 -> 100 %", [&] (PreviewChain& c, size_t n)
        {
            if (n == at (6.0)) c.setControls (2.5, 0.60, 1.0, 0.5);
        });

        automate ("Mod Depth 100 -> 0 %", [&] (PreviewChain& c, size_t n)
        {
            if (n == 0) c.setControls (2.5, 0.60, 1.0, 0.5, true);
            if (n == at (6.0)) c.setControls (2.5, 0.60, 0.0, 0.5);
        });

        automate ("Mod Rate swept min -> max", [&] (PreviewChain& c, size_t n)
        {
            const double u = static_cast<double> (n) / (referenceRate * 12.0);
            c.setControls (2.5, 0.60, 0.30, 0.01 + u * 9.99);
        });

        automate ("Size swept 0 -> 100 %", [&] (PreviewChain& c, size_t n)
        {
            if (n % 4800 == 0)
                c.setControls (2.5, static_cast<double> (n) / (referenceRate * 12.0), 0.30, 0.5);
        });

        automate ("Decay swept 0.3 -> 20 s", [&] (PreviewChain& c, size_t n)
        {
            const double u = static_cast<double> (n) / (referenceRate * 12.0);
            c.setControls (0.3 + u * 19.7, 0.60, 0.30, 0.5);
        });

        automate ("everything at once, every 10 ms", [&] (PreviewChain& c, size_t n)
        {
            if (n % 480 != 0)
                return;

            const double u = static_cast<double> (n) / (referenceRate * 12.0);
            const double wobble = 0.5 + 0.5 * std::sin (40.0 * u);

            c.setControls (0.3 + u * 8.0, wobble, 1.0 - u, 0.05 + 2.0 * wobble);
        });
    }

    // =======================================================================
    beginSuite ("Preview / gain safety");
    // =======================================================================
    {
        info ("  input                          peak out   finite  subnormal-free");

        const auto safety = [&] (const std::string& label, auto&& source, double sizeControl,
                                 double decaySeconds = 20.0)
        {
            PreviewChain chain;
            chain.prepare (referenceRate);
            chain.setControls (decaySeconds, sizeControl, 1.0, 0.5, true);

            const auto length = static_cast<size_t> (referenceRate * 20.0);
            const double trim = previewTrimFor (decaySeconds, sizeControl);

            double peak = 0.0;
            bool finite = true, clean = true;

            for (size_t n = 0; n < length; ++n)
            {
                double x = source (n);
                double l = x, r = x;

                chain.process (l, r);

                l *= trim;
                r *= trim;

                finite = finite && std::isfinite (l) && std::isfinite (r);
                clean = clean && ! isSubnormal (l) && ! isSubnormal (r);
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            }

            info ("  " + pad (label, 31)
                  + pad (fixed (peak, 4), 11)
                  + pad (finite ? "yes" : "NO", 8)
                  + (clean ? "yes" : "NO"));

            check (finite, label + ": no NaN or Inf");
            check (clean, label + ": no subnormals");

            return peak;
        };

        unsigned int seed = 24680u;

        auto noise = [&seed] (size_t)
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 8388608.0 - 1.0;
        };

        const double impulsePeak = safety ("full-scale impulse",
                                           [] (size_t n) { return n == 0 ? 1.0 : 0.0; }, 0.60);

        const double impulseLong = safety ("full-scale impulse, 60 s decay",
                                           [] (size_t n) { return n == 0 ? 1.0 : 0.0; },
                                           0.60, 60.0);

        const double noiseDefault = safety ("sustained noise, default decay", noise, 0.60, 2.5);
        const double noisePeak    = safety ("sustained noise, 20 s decay", noise, 0.60);

        const double noiseSmall = safety ("sustained noise, smallest Size", noise, 0.0);
        const double noiseLarge = safety ("sustained noise, largest Size", noise, 1.0);

        const double dcPeak = safety ("full-scale DC", [] (size_t) { return 1.0; }, 0.60);

        // These assertions used to say "and nothing clips". That was true at
        // previewBaseTrim 0.25 for the cases listed, and it is not true at 1.0 —
        // which is a decision PHASE 5F / FU-2 took deliberately, not a
        // regression. Deleting them would have removed the evidence along with
        // the constraint, so they are kept as measurements with recorded values.
        //
        // What is still a hard requirement is unchanged: nothing may become
        // non-finite or subnormal, and no case may run away. Those are checked
        // above, per case, and they are the ones that would indicate a real
        // fault rather than a staging choice.

        // Musical material. This is the bound that governs the product decision,
        // so it is the one pinned tightly.
        checkAtMost (impulsePeak, 1.0,
                     "a full-scale impulse still does not clip");

        checkAtMost (impulseLong, 1.0,
                     "and does not clip at the longest decay either");

        // Sustained full-scale stress. Recorded, not required.
        //
        // The reference values are this suite's own, measured by this suite.
        // The first version of these took the numbers from the FU-2 harness,
        // which measures a different thing: FU-2 runs the DC case at the default
        // 2.5 s decay and this one runs it at 20 s, so the recorded 3.3993 met a
        // measured 11.1430 and the check failed. It was right to fail. Three of
        // the other four happened to land inside the band anyway, which is worse
        // than failing - a reference that agrees by luck is not a reference.
        //
        // Two percent. This suite is fully deterministic: same seed, same
        // signals, same code path. Changing previewBaseTrim from 0.25 to 1.0
        // multiplied every one of these by 4.0000 to within 3e-4, so the band is
        // far wider than the noise floor and still catches any real drift.
        const auto recordStress = [&] (const std::string& label, double measured, double expected)
        {
            const double ratio = expected > 0.0 ? measured / expected : 0.0;

            info ("  " + pad (label, 33) + "measured " + fixed (measured, 4)
                  + "   recorded " + fixed (expected, 4)
                  + "   ratio " + fixed (ratio, 4));

            check (ratio > 0.98 && ratio < 1.02,
                   label + ": within recorded bounds");
        };

        info ("");
        info ("Sustained full-scale stress, post-trim, at previewBaseTrim 1.0. Above");
        info ("unity by design - see the constant's own comment. Recorded so that a");
        info ("change in the DSP moves these rather than passing unnoticed.");
        info ("");
        info ("Note the decays: only the first of these runs at the 2.5 s default. The");
        info ("rest, including DC, run at 20 s, which is why they are not the same");
        info ("numbers the FU-2 stress table reports for cases of the same name.");
        info ("");

        recordStress ("sustained noise, default decay", noiseDefault, 2.3791);
        recordStress ("sustained noise, 20 s decay", noisePeak, 2.8764);
        recordStress ("sustained noise, smallest Size", noiseSmall, 1.4821);
        recordStress ("sustained noise, largest Size", noiseLarge, 4.5249);
        recordStress ("full-scale DC", dcPeak, 11.1430);

        info ("");
        info ("  case                            headroom");
        info ("  full-scale impulse              " + fixed (-20.0 * std::log10 (std::max (impulsePeak, 1.0e-12)), 2) + " dB");
        info ("  impulse, 60 s decay             " + fixed (-20.0 * std::log10 (std::max (impulseLong, 1.0e-12)), 2) + " dB");
        info ("  sustained noise, default decay  " + fixed (-20.0 * std::log10 (std::max (noiseDefault, 1.0e-12)), 2) + " dB");
        info ("  sustained noise, 20 s decay     " + fixed (-20.0 * std::log10 (std::max (noisePeak, 1.0e-12)), 2) + " dB");
        info ("  sustained noise, smallest Size  " + fixed (-20.0 * std::log10 (std::max (noiseSmall, 1.0e-12)), 2) + " dB");
        info ("  sustained noise, largest Size   " + fixed (-20.0 * std::log10 (std::max (noiseLarge, 1.0e-12)), 2) + " dB");
        info ("  full-scale DC                   " + fixed (-20.0 * std::log10 (std::max (dcPeak, 1.0e-12)), 2) + " dB");
        info ("");
        info ("There is no limiter anywhere in this path, and PHASE 5F / FU-2 confirmed");
        info ("that decision rather than revisiting it. A trim that depends only on");
        info ("parameters is exactly reversible and cannot hide a peak; a limiter would");
        info ("have made every measurement in this phase describe the limiter instead of");
        info ("the reverb.");
        info ("");
        info ("Before the trim was made decay-aware, sustained noise at 20 s reached 2.18");
        info ("and full-scale DC reached 8.01. That span is why the decay-aware factor");
        info ("exists, and it is unchanged.");
        info ("");
        info ("What changed at FU-2 is the fixed part, which is now unity. The sustained");
        info ("cases above therefore exceed full scale, deliberately: a third-octave tone");
        info ("scan found the worst sustained case reaching an effective true peak of 6.38");
        info ("before any trim, which the old 0.25 did not cover either - it left that case");
        info ("about 4 dB over. Paying 22.6 dB on musical material for protection that was");
        info ("not achieved is the trade FU-2 declined. No guarantee of wet <= 1.0 is offered");
        info ("for sustained full-scale input at maximum Size and long Decay.");
    }

    // =======================================================================
    beginSuite ("Preview / raw and loudness-matched comparison");
    // =======================================================================
    {
        // Comparing architectures by ear is worthless if one of them is simply
        // louder. Both numbers are reported so a level difference cannot be
        // mistaken for a quality difference.
        info ("  architecture          raw level   loudness-matched gain");

        for (auto candidate : architecture::allCandidates)
        {
            auto tank = std::make_unique<Morph>();
            tank->prepare (referenceRate);
            tank->setInputDiffusionEnabled (true);
            tank->setRt60Seconds (2.0, true);
            tank->setAllpassDepth (0.0, true);
            tank->setModulationDepth (0.0, true);
            tank->setCompensationEnabled (true, true);
            architecture::apply (*tank, candidate, true);

            unsigned int seed = 31415u;
            double power = 0.0;

            const auto settle = static_cast<size_t> (referenceRate * 3.0);
            const auto measure = static_cast<size_t> (referenceRate * 10.0);

            for (size_t n = 0; n < settle + measure; ++n)
            {
                seed = seed * 1664525u + 1013904223u;
                const double x = 0.1 * (static_cast<double> (seed >> 8) / 8388608.0 - 1.0);

                double l = x, r = x;
                tank->processSample (l, r);

                if (n >= settle)
                    power += 0.5 * (l * l + r * r);
            }

            const double db = 10.0 * std::log10 (
                std::max (power / static_cast<double> (measure), 1.0e-300));

            info ("  " + pad (architecture::describe (candidate), 22)
                  + pad (fixed (db, 2) + " dB", 12)
                  + fixed (-db, 2) + " dB");
        }

        info ("");
        info ("The right-hand column is what each architecture would need to be played at to");
        info ("compare it against the others fairly.");
    }

    // =======================================================================
    beginSuite ("Preview / real-time safety");
    // =======================================================================
    {
        auto chain = std::make_unique<PreviewChain>();
        chain->prepare (referenceRate);

        // Everything the audio thread is allowed to do, all at once.
        ScopedAllocationCounter counter;

        for (int block = 0; block < 400; ++block)
        {
            const int count = 1 + (block * 37) % 1024;

            chain->setControls (0.3 + 0.05 * (block % 100),
                                static_cast<double> (block % 101) / 100.0,
                                static_cast<double> (block % 51) / 50.0,
                                0.01 + 0.02 * (block % 300));

            if (block % 71 == 0)
                chain->tank->setCompensationEnabled (block % 142 == 0);

            if (block % 53 == 0)
                chain->tank->setLoopDelayCorrectionEnabled (block % 106 == 0);

            for (int n = 0; n < count; ++n)
            {
                double l = 0.05, r = -0.05;
                chain->process (l, r);
            }
        }

        const auto allocations = counter.count();

        info ("allocations during 400 blocks of parameter changes, Size morphs,");
        info ("compensation toggling and correction toggling: " + std::to_string (allocations));

        checkNoAllocations (allocations,
                            "the whole preview chain allocates nothing on the audio thread");
    }

    // =======================================================================
    beginSuite ("Preview / block segmentation and sample rates");
    // =======================================================================
    {
        // The same input must render identically however it is cut into blocks.
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const auto length = static_cast<size_t> (rate * 0.5);
            const auto noise = whiteNoise (length, 1234u);

            std::vector<double> reference (length), segmented (length);

            {
                PreviewChain chain;
                chain.prepare (rate);

                for (size_t n = 0; n < length; ++n)
                {
                    double l = noise[n], r = noise[n];
                    chain.process (l, r);
                    reference[n] = 0.5 * (l + r);
                }
            }

            {
                PreviewChain chain;
                chain.prepare (rate);

                size_t n = 0;
                int step = 1;

                while (n < length)
                {
                    const auto count = std::min<size_t> (static_cast<size_t> (step), length - n);

                    for (size_t i = 0; i < count; ++i)
                    {
                        double l = noise[n + i], r = noise[n + i];
                        chain.process (l, r);
                        segmented[n + i] = 0.5 * (l + r);
                    }

                    n += count;
                    step = 1 + (step * 7) % 1024;
                }
            }

            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
                worst = std::max (worst, std::abs (reference[n] - segmented[n]));

            check (worst == 0.0, fixed (rate / 1000.0, 1)
                                 + " kHz: block segmentation changes nothing");

            check (allFinite (reference), fixed (rate / 1000.0, 1) + " kHz: output is finite");
        }
    }
}

} // namespace avtest
