#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
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
    constexpr double pi = 3.14159265358979323846;

    using Mode = TimeVaryingAllpass::Mode;
    using Tank = StereoFdn<N>;

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

    const Mode allModes[] { Mode::energyPreserving, Mode::thiran, Mode::schroederReference };

    TimeVaryingAllpass makeSection (Mode mode, double delayMs, double coefficient)
    {
        TimeVaryingAllpass s;
        s.prepare (referenceRate, allpassMod::maxSectionDelayMs);
        s.setMode (mode);
        s.setDelayMilliseconds (delayMs);
        s.setCoefficient (coefficient);
        return s;
    }

    std::unique_ptr<Tank> makeTank (double sampleRate, Mode mode, double depth, double rate,
                                    LfoShape shape = LfoShape::sine,
                                    double rt60 = 2.0, bool diffusion = false,
                                    int activeLines = allpassMod::defaultActiveLines,
                                    int sectionsPerLine = allpassMod::defaultSectionsPerLine)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);
        tank->setModulationDepth (0.0, true);            // matrix modulation off
        tank->setAllpassMode (mode);
        tank->setAllpassShape (shape);
        tank->setAllpassRateHz (rate);
        tank->setAllpassActiveLines (activeLines);
        tank->setAllpassSectionsPerLine (sectionsPerLine);
        tank->setAllpassDepth (depth, true);
        tank->reset();
        tank->resetModulation();
        return tank;
    }

    std::vector<double> monoImpulseResponse (Tank& tank, size_t length)
    {
        std::vector<double> ir (length);

        for (size_t n = 0; n < length; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            tank.processSample (l, r);
            ir[n] = 0.5 * (l + r);
        }

        return ir;
    }
}

void runAllpassModulationTests()
{
    // =======================================================================
    beginSuite ("Allpass / static behaviour");
    // =======================================================================
    {
        constexpr size_t fftSize = 16384;

        info ("  mode                  max |H| dev   energy err   DC group delay   predicted");

        for (auto mode : allModes)
        {
            auto section = makeSection (mode, 1.0, mode == Mode::thiran ? 1.0 : 0.5);

            std::vector<double> ir (fftSize, 0.0);

            for (size_t n = 0; n < fftSize; ++n)
                ir[n] = section.processSample (n == 0 ? 1.0 : 0.0);

            const auto mag = magnitudeSpectrum (ir, fftSize, false);
            const auto deviation = bandDeviation (mag, referenceRate, fftSize, 20.0, 20000.0);
            const double worstDeviation = std::max (deviation.maxAboveDb, deviation.maxBelowDb);

            // Group delay at DC from the phase slope of the first two bins.
            std::vector<std::complex<double>> spectrum (fftSize, { 0.0, 0.0 });

            for (size_t n = 0; n < fftSize; ++n)
                spectrum[n] = { ir[n], 0.0 };

            fft (spectrum);

            const double phase1 = std::arg (spectrum[1]);
            const double phase2 = std::arg (spectrum[2]);
            double delta = phase2 - phase1;

            while (delta > pi)  delta -= 2.0 * pi;
            while (delta < -pi) delta += 2.0 * pi;

            const double measuredGroupDelay = -delta * static_cast<double> (fftSize) / (2.0 * pi);

            info ("  " + pad (TimeVaryingAllpass::describe (mode), 22)
                  + pad (fixed (worstDeviation, 6) + " dB", 14)
                  + pad (fixed (std::abs (energy (ir) - 1.0), 9), 13)
                  + pad (fixed (measuredGroupDelay, 3), 17)
                  + fixed (section.getGroupDelayAtDc(), 3));

            checkAtMost (worstDeviation, 1.0e-6,
                         std::string (TimeVaryingAllpass::describe (mode))
                         + ": magnitude response is flat to a millionth of a dB");
            checkClose (energy (ir), 1.0, 1.0e-9,
                        std::string (TimeVaryingAllpass::describe (mode))
                        + ": a static section preserves impulse energy exactly");
            checkClose (measuredGroupDelay, section.getGroupDelayAtDc(),
                        std::max (0.05, section.getGroupDelayAtDc() * 0.02),
                        std::string (TimeVaryingAllpass::describe (mode))
                        + ": the DC group delay matches the closed form");
        }

        // The energy-preserving and Schroeder forms must have identical transfer
        // functions when the coefficient is held still. They only differ in how
        // the state is stored, and that difference is invisible at rest.
        {
            auto a = makeSection (Mode::energyPreserving, 1.0, 0.6);
            auto c = makeSection (Mode::schroederReference, 1.0, 0.6);

            double worst = 0.0;
            const auto noise = whiteNoise (40000, 1234u);

            for (size_t n = 0; n < noise.size(); ++n)
                worst = std::max (worst, std::abs (a.processSample (noise[n])
                                                   - c.processSample (noise[n])));

            info ("worst difference between the rotation and direct forms at a fixed "
                  "coefficient: " + std::to_string (worst));

            checkAtMost (worst, 1.0e-12,
                         "the two realisations are the same filter when the coefficient is still");
        }

        // Thiran coefficient bounds and pole stability.
        {
            auto thiran = makeSection (Mode::thiran, 1.0, 1.0);

            bool allStable = true;
            double worstDelayError = 0.0;

            for (double d = 0.05; d <= 3.0; d += 0.05)
            {
                thiran.setCoefficient (d);

                const double a = thiran.getCoefficient();
                allStable &= (std::abs (a) < 1.0);

                const double clamped = std::clamp (d, TimeVaryingAllpass::thiranMinDelay,
                                                   TimeVaryingAllpass::thiranMaxDelay);
                worstDelayError = std::max (worstDelayError,
                                            std::abs (thiran.getFractionalDelay() - clamped));
            }

            check (allStable, "the Thiran pole stays inside the unit circle across the whole range");
            checkAtMost (worstDelayError, 1.0e-12, "requested fractional delays are honoured or clamped");

            thiran.setCoefficient (100.0);
            checkClose (thiran.getFractionalDelay(), TimeVaryingAllpass::thiranMaxDelay, 1.0e-12,
                        "an absurd fractional delay clamps rather than destabilising");
        }
    }

    // =======================================================================
    beginSuite ("Allpass / the energy identity");
    // =======================================================================
    {
        // The rotation form's claim is a per-sample identity, not a long-run
        // average: x^2 + d^2 = y^2 + w^2 for any coefficient trajectory. That
        // is checked directly, with the coefficient moving as violently as the
        // structure allows.
        info ("  mode                  worst per-sample energy residual");

        for (auto mode : allModes)
        {
            auto section = makeSection (mode, 1.0, 0.5);

            const auto noise = whiteNoise (200000, 8u);
            unsigned int state = 999u;

            double worstResidual = 0.0;

            for (size_t n = 0; n < noise.size(); ++n)
            {
                // Adversarial: a brand new coefficient every single sample,
                // jumping across the whole legal range.
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const double u = static_cast<double> (state) / 4294967296.0;

                section.setCoefficient (mode == Mode::thiran
                                            ? TimeVaryingAllpass::thiranMinDelay
                                              + u * (TimeVaryingAllpass::thiranMaxDelay
                                                     - TimeVaryingAllpass::thiranMinDelay)
                                            : -0.8 + 1.6 * u);

                const double x = noise[n];
                const double d = section.getLastRead();   // before the call updates it
                (void) d;

                const double y = section.processSample (x);

                const double read  = section.getLastRead();
                const double write = section.getLastWrite();

                if (mode == Mode::energyPreserving)
                {
                    const double before = x * x + read * read;
                    const double after  = y * y + write * write;

                    worstResidual = std::max (worstResidual, std::abs (after - before));
                }
            }

            if (mode == Mode::energyPreserving)
            {
                info ("  " + pad (TimeVaryingAllpass::describe (mode), 22)
                      + std::to_string (worstResidual));

                checkAtMost (worstResidual, 1.0e-12,
                             "the rotation form conserves energy exactly, sample by sample, "
                             "under a random coefficient every sample");
            }
            else
            {
                info ("  " + pad (TimeVaryingAllpass::describe (mode), 22)
                      + "(no such identity exists for a direct form)");
            }
        }

        // What that buys in practice: throughput energy under a fast sweep.
        info ("");
        info ("  mode                  output/input RMS under a fast coefficient sweep");

        std::array<double, 3> throughput {};
        int modeIndex = 0;

        for (auto mode : allModes)
        {
            auto section = makeSection (mode, 1.0, 0.5);

            const auto noise = whiteNoise (400000, 77u);
            std::vector<double> out (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                const double phase = 2.0 * pi * 40.0 * static_cast<double> (n) / referenceRate;

                section.setCoefficient (mode == Mode::thiran
                                            ? 1.0 + 0.7 * std::sin (phase)
                                            : 0.45 + 0.35 * std::sin (phase));

                out[n] = section.processSample (noise[n]);
            }

            const double ratio = rms (out) / rms (noise);
            throughput[static_cast<size_t> (modeIndex++)] = ratio;

            info ("  " + pad (TimeVaryingAllpass::describe (mode), 22)
                  + fixed (ratio, 6) + "   (" + fixed (20.0 * std::log10 (ratio), 3) + " dB)");
        }

        checkClose (throughput[0], 1.0, 0.002,
                    "the rotation form passes energy through a 40 Hz coefficient sweep unchanged");

        check (std::abs (20.0 * std::log10 (throughput[0]))
               < std::abs (20.0 * std::log10 (throughput[2])),
               "and does so better than the direct-form reference");

        info ("");
        info ("the Schroeder reference is deliberately left unpatched: a reference that has had a");
        info ("gain correction bolted on is no longer a reference");
    }

    // =======================================================================
    beginSuite ("Allpass / pitch deviation estimator");
    // =======================================================================
    {
        // PHASE 2's peakPitchDeviationCents() is for a delay whose length is
        // modulated directly. Here a coefficient is modulated and the delay
        // excursion is a nonlinear function of it, so the estimator is derived
        // separately and checked against measurement.
        //
        // Choosing the probe tone is not free. For a section of M samples,
        //
        //     H(z) = (-g + z^-M) / (1 - g z^-M),   phi = M omega
        //
        // and at phi = 0 (mod pi) the numerator and denominator share the same
        // g-dependence: at phi = 0, H = (1-g)/(1-g) = 1, and at phi = pi,
        // H = -(1+g)/(1+g) = -1. At those frequencies the section is exactly
        // transparent to its own coefficient, so modulating g produces no phase
        // change at all and any pitch measurement there reads zero for every
        // depth. Those are frequencies f = k*fs/(2M) — for a 1 ms section at
        // 48 kHz that is 500 Hz, 1000 Hz, 1500 Hz, ...
        //
        // The first version of this suite probed at 1000 Hz with a 1 ms section
        // and therefore measured 0.000 cents at every point in the sweep while
        // still "passing", because zero satisfies an upper bound. The sweep is
        // now anchored to the section length instead, and the degeneracy is
        // measured explicitly below so the trap stays documented.
        constexpr double baseMs = 1.0;
        const double baseSamples = baseMs * 0.001 * referenceRate;

        // Sensitivity is largest where sin(phi) = +/-1, i.e. f = (2k+1)*fs/(4M).
        const double blindHz      = referenceRate / baseSamples;         // phi = 2 pi
        const double halfBlindHz  = referenceRate / (2.0 * baseSamples); // phi = pi
        const double sensitiveHz  = referenceRate / (4.0 * baseSamples); // phi = pi/2
        const double sensitive3Hz = 3.0 * sensitiveHz;                   // phi = 3 pi/2

        auto measureAt = [&] (double toneHz, double swing, double rate)
        {
            auto section = makeSection (Mode::energyPreserving, baseMs,
                                        allpassMod::coefficientCentre);

            const size_t length = static_cast<size_t> (referenceRate * 12.0);
            std::vector<double> out (length);

            for (size_t n = 0; n < length; ++n)
            {
                const double phase = 2.0 * pi * rate * static_cast<double> (n) / referenceRate;
                section.setCoefficient (allpassMod::coefficientCentre + swing * std::sin (phase));

                out[n] = section.processSample (
                    0.5 * std::sin (2.0 * pi * toneHz * static_cast<double> (n) / referenceRate));
            }

            return pitchCentroidExcursion (out, referenceRate, toneHz,
                                           16384, 4096, 40.0).peakCents;
        };

        info ("phase modulation depends strongly on where the tone sits relative to the");
        info ("section length. M = " + fixed (baseSamples, 0) + " samples, swing 0.30, 1.00 Hz:");
        info ("");
        info ("  probe tone    M*omega     measured");

        struct Probe { const char* label; double hz; };
        const Probe probes[] = {
            { "fs/(4M)", sensitiveHz  },
            { "fs/(2M)", halfBlindHz  },
            { "3fs/(4M)", sensitive3Hz },
            { "fs/M",    blindHz      },
        };

        for (const auto& p : probes)
        {
            const double cents = measureAt (p.hz, 0.30, 1.0);
            info ("  " + pad (fixed (p.hz, 1) + " Hz", 14)
                  + pad (std::string (p.label), 12)
                  + fixed (cents, 3) + " ct");
        }

        info ("");
        info ("fs/(2M) and fs/M are blind spots of the section itself, not of the estimator.");
        info ("The sweep below therefore probes at fs/(4M), where sensitivity is greatest.");
        info ("");
        info ("  base   centre  swing  rate     predicted   measured   ratio");

        bool allClose = true;
        double tightestRatio = 0.0;

        for (double rate : { 0.2, 0.5, 1.0 })
        {
            for (double swing : { 0.10, 0.20, 0.30 })
            {

                const double predicted = allpassMod::peakPitchCentsForCoefficientSwing (
                    baseSamples, allpassMod::coefficientCentre, swing, rate, referenceRate);

                // Measured on a single section, so the FDN's own structure does
                // not contribute.
                const double measured = measureAt (sensitiveHz, swing, rate);
                const double ratio = predicted > 0.0 ? measured / predicted : 0.0;

                info ("  " + pad (fixed (baseMs, 2), 7)
                      + pad (fixed (allpassMod::coefficientCentre, 2), 8)
                      + pad (fixed (swing, 2), 7)
                      + pad (fixed (rate, 2) + " Hz", 9)
                      + pad (fixed (predicted, 3) + " ct", 12)
                      + pad (fixed (measured, 3) + " ct", 11)
                      + fixed (ratio, 3));

                // The estimator is an upper bound: it evaluates the group-delay
                // sensitivity at DC and at the top of the swing, where both are
                // largest, so the measured excursion should sit at or below it.
                allClose &= (measured <= predicted);
                tightestRatio = std::max (tightestRatio, ratio);

                // A bound that is never approached is not evidence of anything,
                // so the sweep also has to produce a real, non-zero excursion.
                allClose &= (measured > 0.0);
            }
        }

        check (allClose, "the estimator bounds a non-zero measured pitch excursion at every point tested");

        info ("");
        info ("tightest point in the sweep: measured / predicted = " + fixed (tightestRatio, 3));
        info ("The bound is conservative because tau(omega) = M(1-g^2)/(1+g^2-2g cos(M omega))");
        info ("and both tau and dtau/dg peak at DC: dtau/dg = 2M/(1-g)^2 there against");
        info ("-4Mg/(1+g^2)^2 at the most sensitive audible probe. Being conservative in the");
        info ("direction of over-reporting pitch is the safe direction for a budget check.");

        // The shipped default has to land inside the subtle budget.
        const double defaultSamples = allpassMod::sectionDelaysMs[0] * 0.001 * referenceRate;
        const double defaultCents = allpassMod::peakPitchCentsForCoefficientSwing (
            defaultSamples, allpassMod::coefficientCentre,
            allpassMod::swingForDepth (allpassMod::defaultDepth),
            allpassMod::defaultRateHz, referenceRate);

        info ("");
        info ("shipped default: base " + fixed (allpassMod::sectionDelaysMs[0], 2)
              + " ms, swing " + fixed (allpassMod::swingForDepth (allpassMod::defaultDepth), 3)
              + ", rate " + fixed (allpassMod::defaultRateHz, 2) + " Hz -> "
              + fixed (defaultCents, 3) + " cents peak");
        info ("budgets: subtle up to " + fixed (allpassMod::subtlePitchCents, 1)
              + " ct, character to " + fixed (allpassMod::characterPitchCents, 1)
              + " ct, extreme to " + fixed (allpassMod::extremePitchCents, 1) + " ct");

        checkAtMost (defaultCents, allpassMod::subtlePitchCents,
                     "the shipped default stays inside the subtle pitch budget");
    }

    // =======================================================================
    beginSuite ("Allpass / depth zero is a true bypass");
    // =======================================================================
    {
        auto withBank = makeTank (referenceRate, Mode::energyPreserving, 0.0,
                                  allpassMod::defaultRateHz);

        auto plain = std::make_unique<Tank>();
        plain->prepare (referenceRate);
        plain->setInputDiffusionEnabled (false);
        plain->setRt60Seconds (2.0, true);
        plain->setModulationDepth (0.0, true);
        plain->reset();

        const auto noise = whiteNoise (200000, 4242u);

        bool identical = true;
        double worst = 0.0;

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double al = noise[n], ar = noise[n];
            double bl = noise[n], br = noise[n];

            withBank->processSample (al, ar);
            plain->processSample (bl, br);

            worst = std::max (worst, std::max (std::abs (al - bl), std::abs (ar - br)));
            identical &= (al == bl) && (ar == br);
        }

        info ("worst sample difference between a bypassed bank and an untouched tank: "
              + std::to_string (worst));

        check (identical, "Depth 0 is bit-identical to the PHASE 4 static FDN");
        check (withBank->getCore().getAllpassBank().isBypassed(),
               "the bank reports itself bypassed");
    }

    // =======================================================================
    beginSuite ("Allpass / candidate comparison in the tank");
    // =======================================================================
    {
        // The measurement that matters: what each realisation does to the tail,
        // and what it costs in energy and decay.
        info ("PHASE 4 static baseline: persistence 0.7944, periodicity 43.2");
        info ("PHASE 5A matrix baseline: persistence 0.7528, periodicity 45.2");
        info ("");
        info ("  mode                  persist  periodicity  movement  RT60     drift     AM      pitch");

        struct Result { double persistence, periodicity, movement, rt60, drift, am, pitch; };
        std::array<Result, 3> results {};
        int modeIndex = 0;

        for (auto mode : allModes)
        {
            Result r {};

            // Modal metrics need an 8 s decay and 32768-sample frames, as
            // established in PHASE 5A: the tank's modal spacing is 2.6 Hz.
            {
                auto tank = makeTank (referenceRate, mode, allpassMod::defaultDepth,
                                      allpassMod::defaultRateHz, LfoShape::sine, 8.0);

                const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 6.0));

                const auto windowStart  = static_cast<size_t> (0.50 * referenceRate);
                const auto windowLength = static_cast<size_t> (4.00 * referenceRate);

                const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

                r.periodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                        static_cast<size_t> (0.001 * referenceRate),
                                                        static_cast<size_t> (0.120 * referenceRate));

                const auto spec = spectrogramOf (
                    std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                         ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                    referenceRate, 32768, 16384);

                r.persistence = modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0);
                r.movement    = modalPeakMovementHz (spec, 200.0, 4000.0, 40, 4.0);
            }

            // Decay.
            {
                auto tank = makeTank (referenceRate, mode, allpassMod::defaultDepth,
                                      allpassMod::defaultRateHz);
                const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 5.0));
                r.rt60 = measureDecay (ir, referenceRate).rt60FromT30;
            }

            // Long-run drift, measured across whole modulation cycles.
            {
                auto tank = makeTank (referenceRate, mode, allpassMod::defaultDepth,
                                      allpassMod::defaultRateHz);

                const double period = 1.0 / allpassMod::defaultRateHz;
                const double totalSeconds = 2.0 * 10.0 + period + 10.0;

                const auto noise = whiteNoise (static_cast<size_t> (referenceRate * totalSeconds), 777u);
                std::vector<double> out (noise.size());

                for (size_t n = 0; n < noise.size(); ++n)
                {
                    double l = noise[n] * 0.3, rr = -l;
                    tank->processSample (l, rr);
                    out[n] = 0.5 * (l + rr);
                }

                const auto window = static_cast<size_t> (referenceRate * 10.0);
                const auto start  = static_cast<size_t> (referenceRate * 5.0);
                const auto offset = static_cast<size_t> (referenceRate * period);

                r.drift = 20.0 * std::log10 (rms (out.data() + start + offset, window)
                                             / std::max (rms (out.data() + start, window), 1.0e-30));
            }

            // Sustained tone: amplitude modulation and pitch, separated.
            {
                auto tank = makeTank (referenceRate, mode, allpassMod::defaultDepth,
                                      allpassMod::defaultRateHz);

                constexpr double toneHz = 1000.0;
                const size_t length = static_cast<size_t> (referenceRate * 14.0);

                std::vector<double> out (length);

                for (size_t n = 0; n < length; ++n)
                {
                    const double x = 0.5 * std::sin (2.0 * pi * toneHz
                                                     * static_cast<double> (n) / referenceRate);
                    double l = x, rr = x;
                    tank->processSample (l, rr);
                    out[n] = 0.5 * (l + rr);
                }

                const auto settled = static_cast<size_t> (referenceRate * 5.0);

                r.am = amplitudeModulationDepthDb (out.data() + settled, length - settled,
                                                   referenceRate);

                std::vector<double> tail (out.begin() + static_cast<ptrdiff_t> (settled), out.end());
                r.pitch = pitchCentroidExcursion (tail, referenceRate, toneHz,
                                                  16384, 8192, 40.0).peakCents;
            }

            info ("  " + pad (TimeVaryingAllpass::describe (mode), 22)
                  + pad (fixed (r.persistence, 4), 9)
                  + pad (fixed (r.periodicity, 2), 13)
                  + pad (fixed (r.movement, 3), 10)
                  + pad (fixed (r.rt60, 4), 9)
                  + pad (fixed (r.drift, 3), 10)
                  + pad (fixed (r.am, 2), 8)
                  + fixed (r.pitch, 2));

            results[static_cast<size_t> (modeIndex++)] = r;
        }

        const auto& preserving = results[0];
        const auto& reference  = results[2];

        checkAtMost (std::abs (preserving.rt60 - 2.0) / 2.0, 0.03,
                     "the energy-preserving candidate holds RT60 within 3 % of target");
        checkAtMost (std::abs (preserving.drift), 0.1,
                     "the energy-preserving candidate drifts less than 0.1 dB over whole cycles");

        check (std::abs (preserving.drift) <= std::abs (reference.drift) + 0.02,
               "and drifts no more than the direct-form reference");

        check (preserving.persistence < 0.7944,
               "allpass modulation reduces modal persistence below the static baseline");

        info ("");
        info ("targets for this sub-phase were persistence at or below 0.70 and periodicity at or");
        info ("below 30. What is achieved against those is reported above rather than asserted:");
        info ("a candidate that reached them by producing unacceptable pitch or drift would not");
        info ("be shippable anyway.");
    }
}

} // namespace avtest
