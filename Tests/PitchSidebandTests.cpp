#include "DspAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/StereoFdn.h"
#include "DSP/TimeVaryingAllpass.h"

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

    /**
        The probe grid, as fractions of fs/M.

        PHASE 5B found that a section is exactly transparent to its own
        coefficient wherever M*omega is a multiple of pi, because the numerator
        and denominator of (-g + z^-M)/(1 - g z^-M) then carry the same
        g-dependence and it cancels. Those points measure zero pitch deviation at
        any depth, and a suite that probed only there passed while measuring
        nothing.

        The grid keeps the blind points in deliberately. Their job is to stay
        zero: if one of them ever reads non-zero the structural claim is wrong,
        and if a non-blind point reads zero the measurement has broken.
    */
    struct Probe
    {
        const char* label;
        double fractionOfBlind;   /**< Multiplied by fs/M. */
        bool   blind;
    };

    const Probe probes[]
    {
        { "fs/(8M)",  0.125, false },
        { "fs/(4M)",  0.250, false },
        { "3fs/(8M)", 0.375, false },
        { "fs/(2M)",  0.500, true  },
        { "3fs/(4M)", 0.750, false },
        { "fs/M",     1.000, true  },
    };

    TimeVaryingAllpass makeSection (Mode mode, double delayMs, double coefficient)
    {
        TimeVaryingAllpass s;
        s.prepare (referenceRate, allpassMod::maxSectionDelayMs);
        s.setMode (mode);
        s.setDelayMilliseconds (delayMs);
        s.setCoefficient (coefficient);
        return s;
    }

    /** A single section driven by a sinusoidal coefficient, fed a steady tone. */
    std::vector<double> sweepSection (double toneHz, double swing, double rateHz,
                                      double delayMs, double seconds)
    {
        auto section = makeSection (Mode::energyPreserving, delayMs, allpassMod::coefficientCentre);

        const auto length = static_cast<size_t> (seconds * referenceRate);

        std::vector<double> out (length);

        for (size_t n = 0; n < length; ++n)
        {
            const double phase = 2.0 * pi * rateHz * static_cast<double> (n) / referenceRate;
            section.setCoefficient (allpassMod::coefficientCentre + swing * std::sin (phase));

            out[n] = section.processSample (
                0.5 * std::sin (2.0 * pi * toneHz * static_cast<double> (n) / referenceRate));
        }

        return out;
    }

    std::unique_ptr<Tank> makeDefaultTank (double rt60)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (referenceRate);
        tank->setRt60Seconds (rt60, true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        architecture::apply (*tank, architecture::defaultCandidate, true);
        tank->reset();
        return tank;
    }
}

void runPitchSidebandTests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("Pitch / multi-frequency probes");
    // =======================================================================
    {
        // The shipped section length, so the numbers describe the reverb rather
        // than a convenient round figure.
        const double delayMs = allpassMod::sectionDelaysMs[0];
        const double m = std::round (delayMs * 0.001 * referenceRate);
        const double blindHz = referenceRate / m;

        info ("Shipped section 0: " + fixed (delayMs, 2) + " ms = " + fixed (m, 0)
              + " samples, so fs/M = " + fixed (blindHz, 1) + " Hz.");
        info ("Swing " + fixed (allpassMod::maxCoefficientSwing, 2)
              + ", rate 1.00 Hz — the research maximum, not the shipped setting, so that a");
        info ("blind point reading zero cannot be blamed on there being nothing to measure.");
        info ("");
        info ("  probe        frequency    peak ct   rms ct    blind");

        double worstNonBlind = 0.0, worstBlind = 0.0;
        double rmsAtMostSensitive = 0.0;

        for (const auto& probe : probes)
        {
            const double toneHz = probe.fractionOfBlind * blindHz;
            const auto out = sweepSection (toneHz, allpassMod::maxCoefficientSwing, 1.0, delayMs, 12.0);

            const auto excursion = pitchCentroidExcursion (out, referenceRate, toneHz,
                                                           16384, 4096, 40.0);

            const auto split = amFmSeparation (out, referenceRate, toneHz, 40.0);

            info ("  " + pad (probe.label, 13)
                  + pad (fixed (toneHz, 1) + " Hz", 13)
                  + pad (fixed (excursion.peakCents, 3), 10)
                  + pad (fixed (split.fmRmsCents, 3), 10)
                  + (probe.blind ? "yes" : "no"));

            if (probe.blind)
                worstBlind = std::max (worstBlind, excursion.peakCents);
            else
                worstNonBlind = std::max (worstNonBlind, excursion.peakCents);

            if (std::abs (probe.fractionOfBlind - 0.25) < 1.0e-9)
                rmsAtMostSensitive = split.fmRmsCents;
        }

        info ("");
        info ("maximum measured pitch deviation across the grid: " + fixed (worstNonBlind, 3) + " ct");
        info ("RMS pitch deviation at the most sensitive probe:   "
              + fixed (rmsAtMostSensitive, 3) + " ct");

        check (worstNonBlind > 0.0,
               "at least one probe measures a real, non-zero pitch deviation");

        checkAtMost (worstBlind, 1.0e-6,
                     "the structural blind points measure zero, as the algebra says they must");

        check (worstNonBlind > worstBlind,
               "the non-blind probes measure more than the blind ones");
    }

    // =======================================================================
    beginSuite ("Pitch / analytical bound and its tightness");
    // =======================================================================
    {
        const double delayMs = allpassMod::sectionDelaysMs[0];
        const double m = std::round (delayMs * 0.001 * referenceRate);
        const double sensitiveHz = referenceRate / (4.0 * m);

        info ("  swing  rate      predicted   measured    ratio");

        double tightest = 0.0;
        bool bounded = true;

        for (double rate : { 0.20, 0.50, 1.00 })
        {
            for (double swing : { 0.105, 0.20, 0.30 })
            {
                const double predicted = allpassMod::peakPitchCentsForCoefficientSwing (
                    m, allpassMod::coefficientCentre, swing, rate, referenceRate);

                const auto out = sweepSection (sensitiveHz, swing, rate, delayMs, 12.0);
                const double measured = pitchCentroidExcursion (out, referenceRate, sensitiveHz,
                                                                16384, 4096, 40.0).peakCents;

                const double ratio = predicted > 0.0 ? measured / predicted : 0.0;

                info ("  " + pad (fixed (swing, 3), 7)
                      + pad (fixed (rate, 2) + " Hz", 10)
                      + pad (fixed (predicted, 3) + " ct", 12)
                      + pad (fixed (measured, 3) + " ct", 12)
                      + fixed (ratio, 3));

                bounded = bounded && (measured <= predicted) && (measured > 0.0);
                tightest = std::max (tightest, ratio);
            }
        }

        check (bounded, "the analytical bound holds and is approached at every point");

        info ("");
        info ("tightest point: measured / predicted = " + fixed (tightest, 3));
        info ("The bound evaluates dtau/dg at DC, where both the group delay and its");
        info ("sensitivity peak, so it over-reports. Over-reporting pitch is the safe");
        info ("direction for a budget check and the bound is left conservative.");
    }

    // =======================================================================
    beginSuite ("Pitch / sidebands");
    // =======================================================================
    {
        // Resolving sidebands spaced by a fifth of a Hertz needs a very fine
        // transform: 2^20 bins at 48 kHz is 0.046 Hz, so the first sideband sits
        // about four bins out and the split is a measurement rather than a
        // guess.
        constexpr size_t fftSize = 1u << 20;
        const double resolution = referenceRate / static_cast<double> (fftSize);

        info ("FFT size " + std::to_string (fftSize) + " at " + fixed (referenceRate / 1000.0, 1)
              + " kHz gives " + fixed (resolution, 4) + " Hz per bin, so the "
              + fixed (allpassMod::defaultRateHz, 2) + " Hz");
        info ("sideband spacing spans about "
              + fixed (allpassMod::defaultRateHz / resolution, 1) + " bins.");
        info ("");
        info ("");
        info ("Sideband ORDER is only defined when there is one modulation rate. The shipped");
        info ("bank runs eight lanes at eight different rates, so energy at 2 x 0.20 Hz is not");
        info ("necessarily a second-order sideband of lane 0 — it may be the first-order");
        info ("sideband of another lane. The strict order analysis is therefore done with a");
        info ("single lane engaged, where the assumption holds; the shipped configuration is");
        info ("reported separately, by distance from the carrier, which needs no such");
        info ("assumption.");
        info ("");
        info ("  single lane           fundamental  1st order  higher    residual  resolved");

        const auto measureSidebands = [&] (const std::string& label, double depth, double rateHz)
        {
            auto tank = makeDefaultTank (2.0);
            tank->setAllpassActiveLines (1);
            tank->setAllpassRateHz (rateHz);
            tank->setAllpassDepth (depth, true);
            tank->reset();

            constexpr double toneHz = 1000.0;
            const auto total = static_cast<size_t> (referenceRate * 30.0);
            const auto skip  = static_cast<size_t> (referenceRate * 2.0);

            std::vector<double> out;
            out.reserve (total - skip);

            for (size_t n = 0; n < total; ++n)
            {
                const double x = 0.25 * std::sin (2.0 * pi * toneHz
                                                  * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                tank->processSample (l, r);

                if (n >= skip)
                    out.push_back (0.5 * (l + r));
            }

            const auto s = sidebandEnergy (out, referenceRate, toneHz, rateHz, fftSize, 6, 2);

            info ("  " + pad (label, 22)
                  + pad (fixed (100.0 * s.fundamental, 2) + " %", 13)
                  + pad (fixed (100.0 * s.firstOrder, 2) + " %", 11)
                  + pad (fixed (100.0 * s.higherOrder, 2) + " %", 10)
                  + pad (fixed (100.0 * s.residual, 2) + " %", 10)
                  + (s.resolved ? "yes" : "no"));

            return s;
        };

        const auto off      = measureSidebands ("depth 0 (bypassed)", 0.0, allpassMod::defaultRateHz);
        const auto shipped  = measureSidebands ("shipped default", allpassMod::defaultDepth,
                                                allpassMod::defaultRateHz);
        const auto full     = measureSidebands ("full depth", 1.0, allpassMod::defaultRateHz);

        check (off.resolved && shipped.resolved, "the sidebands are actually resolved");

        check (shipped.fundamental < off.fundamental,
               "modulation moves energy off the carrier");

        check (shipped.higherOrder < shipped.firstOrder,
               "with one lane, first-order sidebands dominate the higher orders at the"
               " shipped depth");

        check (full.firstOrder >= shipped.firstOrder,
               "more depth puts more energy into the first-order sidebands");

        info ("");
        info ("First-order sidebands are movement. Higher orders are what a listener starts to");
        info ("hear as warble, and with one lane at the shipped depth they stay below it.");

        // -------------------------------------------------------------------
        //  The shipped eight-lane bank, without assuming a single rate
        // -------------------------------------------------------------------
        info ("");
        info ("  eight lanes           energy by distance from the carrier");

        const auto spread = [&] (const std::string& label, double depth)
        {
            auto tank = makeDefaultTank (2.0);
            tank->setAllpassDepth (depth, true);
            tank->reset();

            constexpr double toneHz = 1000.0;
            const auto total = static_cast<size_t> (referenceRate * 30.0);
            const auto skip  = static_cast<size_t> (referenceRate * 2.0);

            std::vector<double> out;
            out.reserve (total - skip);

            for (size_t n = 0; n < total; ++n)
            {
                const double x = 0.25 * std::sin (2.0 * pi * toneHz
                                                  * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                tank->processSample (l, r);

                if (n >= skip)
                    out.push_back (0.5 * (l + r));
            }

            const size_t start = (out.size() - fftSize) / 2;
            const auto magnitude = magnitudeSpectrum (out.data() + start, fftSize, fftSize, true);

            // Lane rates run from 0.200 to 0.365 Hz, so the shells are chosen to
            // put "one lane rate out" and "two lane rates out" in their own
            // bands without having to say which lane.
            const double shells[] { 0.10, 0.45, 0.85, 2.00, 8.00 };

            double total_ = 0.0;
            double bands[5] { 0.0, 0.0, 0.0, 0.0, 0.0 };

            const long centre = static_cast<long> (std::llround (toneHz / resolution));
            const long span   = static_cast<long> (std::llround (8.0 / resolution));

            for (long k = centre - span; k <= centre + span; ++k)
            {
                if (k < 0 || static_cast<size_t> (k) >= magnitude.size())
                    continue;

                const double power = magnitude[static_cast<size_t> (k)]
                                   * magnitude[static_cast<size_t> (k)];
                const double distance = std::abs (static_cast<double> (k - centre)) * resolution;

                total_ += power;

                for (int b = 0; b < 5; ++b)
                    if (distance <= shells[b])
                    {
                        bands[b] += power;
                        break;
                    }
            }

            if (total_ <= 0.0)
                return;

            info ("  " + pad (label, 22)
                  + pad ("<0.1Hz " + fixed (100.0 * bands[0] / total_, 2) + " %", 17)
                  + pad ("<0.45 " + fixed (100.0 * bands[1] / total_, 2) + " %", 15)
                  + pad ("<0.85 " + fixed (100.0 * bands[2] / total_, 2) + " %", 15)
                  + pad ("<2 " + fixed (100.0 * bands[3] / total_, 2) + " %", 13)
                  + "<8 " + fixed (100.0 * bands[4] / total_, 2) + " %");
        };

        spread ("depth 0 (bypassed)", 0.0);
        spread ("shipped default", allpassMod::defaultDepth);
        spread ("full depth", 1.0);

        info ("");
        info ("Lane rates span 0.200 to 0.365 Hz, so the first shell past the carrier holds");
        info ("what a single-rate analysis would have called first-order sidebands, whichever");
        info ("lane produced them. Energy beyond 2 Hz is the part that would be audible as");
        info ("warble rather than as movement.");
    }

    // =======================================================================
    beginSuite ("Pitch / AM and FM separated");
    // =======================================================================
    {
        // A single "wobble" figure cannot say whether a tail is breathing or
        // detuning. The analytic signal separates the two: its magnitude is the
        // instantaneous amplitude and the derivative of its argument is the
        // instantaneous frequency.
        info ("  configuration         AM peak   AM rms    FM peak   FM rms    centre");

        const auto split = [&] (const std::string& label, architecture::Candidate candidate)
        {
            auto tank = std::make_unique<Tank>();
            tank->prepare (referenceRate);
            tank->setRt60Seconds (2.0, true);
            tank->setAllpassDepth (0.0, true);
            tank->setModulationDepth (0.0, true);
            architecture::apply (*tank, candidate, true);
            tank->reset();

            constexpr double toneHz = 1000.0;
            const auto total = static_cast<size_t> (referenceRate * 25.0);
            const auto skip  = static_cast<size_t> (referenceRate * 2.0);

            std::vector<double> out;
            out.reserve (total - skip);

            for (size_t n = 0; n < total; ++n)
            {
                const double x = 0.25 * std::sin (2.0 * pi * toneHz
                                                  * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                tank->processSample (l, r);

                if (n >= skip)
                    out.push_back (0.5 * (l + r));
            }

            const auto s = amFmSeparation (out, referenceRate, toneHz, 40.0);

            info ("  " + pad (label, 22)
                  + pad (fixed (s.amDepthDb, 2) + " dB", 10)
                  + pad (fixed (s.amRmsDb, 3) + " dB", 10)
                  + pad (fixed (s.fmPeakCents, 2) + " ct", 10)
                  + pad (fixed (s.fmRmsCents, 3) + " ct", 10)
                  + fixed (s.centreHz, 2) + " Hz");

            return s;
        };

        const auto staticTank = split ("A static FDN", architecture::Candidate::staticFdn);
        const auto matrix     = split ("B matrix-only", architecture::Candidate::matrixOnly);
        const auto allpass    = split ("C allpass-only", architecture::Candidate::allpassOnly);
        const auto hybrid     = split ("F hybrid 7deg", architecture::Candidate::existingHybrid);

        info ("");
        info ("This overturns a PHASE 5A statement that was reasoned rather than measured.");
        info ("");
        info ("PHASE 5A concluded that matrix modulation 'costs amplitude, not detune', on the");
        info ("grounds that a time-varying orthogonal mix never changes a delay length. The");
        info ("first half holds. The second does not: rotating energy between lines of");
        info ("different lengths changes the effective group delay of the composite path a");
        info ("signal takes through the tank, and a changing effective delay is frequency");
        info ("modulation whether or not any single delay line moved. Measured, matrix-only");
        info ("produces MORE frequency modulation than the allpass engine — 0.19 against");
        info ("0.15 cents RMS — while also producing more amplitude modulation per unit of");
        info ("periodicity improvement.");
        info ("");
        info ("Both figures are tiny and neither is a problem on its own. What it changes is");
        info ("the reasoning: matrix modulation is not the 'pitch-free' option it was assumed");
        info ("to be, so there is no pitch-budget argument for preferring it. That removes one");
        info ("more reason to carry it in the default architecture.");

        check (matrix.fmRmsCents > 0.0 && allpass.fmRmsCents > 0.0,
               "both engines produce measurable frequency modulation");

        check (allpass.amRmsDb > staticTank.amRmsDb,
               "the allpass engine does produce some amplitude modulation");

        check (matrix.fmRmsCents > staticTank.fmRmsCents * 10.0,
               "matrix modulation's frequency modulation is far above the static tank's floor,"
               " so it is real rather than measurement noise");

        check (hybrid.amRmsDb > allpass.amRmsDb,
               "adding the matrix layer adds amplitude modulation");

        check (hybrid.fmRmsCents > allpass.fmRmsCents,
               "adding the matrix layer adds frequency modulation too");

        checkAtMost (allpass.fmPeakCents, 3.0,
                     "the shipped architecture stays inside the 3 cent pitch budget");
    }
}

} // namespace avtest
