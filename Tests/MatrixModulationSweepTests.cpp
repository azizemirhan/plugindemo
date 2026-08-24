#include "AllocationDetector.h"
#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"

#include <algorithm>
#include <array>
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
    constexpr size_t analysisLength = 32768;

    using Tank   = StereoFdn<N>;
    using Matrix = Tank::Matrix;

    volatile double sink = 0.0;

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

    [[nodiscard]] std::unique_ptr<Tank> makeTank (double sampleRate, double depth, double rate,
                                                  LfoShape shape = LfoShape::sine,
                                                  double rt60 = 2.0, bool diffusion = false)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);
        tank->setModulationSchedule (modulation::defaultSchedule);
        tank->setModulationShape (shape);
        tank->setModulationRateHz (rate);
        tank->setModulationDepth (depth, true);
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

    struct SweepPoint
    {
        double periodicity = 0.0;
        double persistence = 0.0;
        double flux = 0.0;
        double rt60 = 0.0;
        double bandSpread = 0.0;
        double energyDriftDb = 0.0;
        double amDepthDb = 0.0;
        double concentrationDb = 0.0;
        double pitchPeakCents = 0.0;
    };

    SweepPoint measurePoint (double depth, double rate, LfoShape shape = LfoShape::sine)
    {
        SweepPoint p;

        // --- tail structure and decay ---------------------------------------
        //
        // The modal analysis runs at an 8 s decay, not the 2 s used elsewhere.
        // The tank's mean modal spacing is about 2.6 Hz, so resolving
        // individual modes needs frames of 32768 samples (1.5 Hz), and frames
        // that long need several seconds of usable tail to sit in. The first
        // version used 8192-sample frames at a 2 s decay, which could not
        // resolve the modes at all: it was tracking noise, and reported a
        // static tank and a modulated one as equally unstable.
        {
            auto tank = makeTank (referenceRate, depth, rate, shape, 8.0);
            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 6.0));

            const auto windowStart  = static_cast<size_t> (0.50 * referenceRate);
            const auto windowLength = static_cast<size_t> (4.00 * referenceRate);

            const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

            p.periodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                    static_cast<size_t> (0.001 * referenceRate),
                                                    static_cast<size_t> (0.120 * referenceRate));

            const auto spec = spectrogramOf (
                std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                     ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                referenceRate, 32768, 16384);

            p.persistence = modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0);
            p.flux        = spectralFlux (spec, 200.0, 4000.0);
        }

        // --- decay, measured at the 2 s setting used everywhere else ---------
        {
            auto tank = makeTank (referenceRate, depth, rate, shape);
            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 5.0));

            const auto broadband = measureDecay (ir, referenceRate);
            const auto low  = measureBandDecay (ir, referenceRate, 100.0, 400.0);
            const auto mid  = measureBandDecay (ir, referenceRate, 800.0, 2500.0);
            const auto high = measureBandDecay (ir, referenceRate, 6000.0, 12000.0);

            p.rt60 = broadband.rt60FromT30;
            p.bandSpread = (std::max ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 })
                          - std::min ({ low.rt60FromT30, mid.rt60FromT30, high.rt60FromT30 }))
                         / std::max (broadband.rt60FromT30, 1.0e-9);
        }

        // --- long-run energy drift --------------------------------------------
        //
        // The two windows are separated by a whole number of periods of the
        // slowest lane, so the modulation is at the same point in its cycle in
        // both and only a genuine leak can show up. Comparing two arbitrary
        // windows measures the modulation itself: at 0.02 Hz one cycle is fifty
        // seconds long, and two windows inside it differed by 0.17 dB with no
        // drift present at all.
        {
            auto tank = makeTank (referenceRate, depth, rate, shape);

            constexpr double windowSeconds = 10.0;
            const double period = 1.0 / rate;
            const double totalSeconds = std::min (150.0,
                                                  std::max (40.0, 2.0 * windowSeconds + period + 5.0));

            const auto noise = whiteNoise (static_cast<size_t> (referenceRate * totalSeconds), 777u);
            std::vector<double> out (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double l = noise[n] * 0.3, r = -l;
                tank->processSample (l, r);
                out[n] = 0.5 * (l + r);
            }

            const auto window = static_cast<size_t> (referenceRate * windowSeconds);
            const auto start  = static_cast<size_t> (referenceRate * 5.0);

            // Whole cycles between the two windows, and as many as fit.
            const auto cycles = std::max (1.0, std::floor ((totalSeconds - 5.0 - 2.0 * windowSeconds)
                                                           / period));
            const auto offset = static_cast<size_t> (referenceRate * period * cycles);

            if (start + offset + window <= out.size())
            {
                const double early = rms (out.data() + start, window);
                const double late  = rms (out.data() + start + offset, window);

                p.energyDriftDb = 20.0 * std::log10 (std::max (late, 1.0e-30)
                                                     / std::max (early, 1.0e-30));
            }
        }

        // --- sustained tone: amplitude modulation and spectral spread ---------
        {
            auto tank = makeTank (referenceRate, depth, rate, shape);

            constexpr double toneHz = 1000.0;
            const size_t length = static_cast<size_t> (referenceRate * 12.0);

            std::vector<double> out (length);

            for (size_t n = 0; n < length; ++n)
            {
                const double x = 0.5 * std::sin (2.0 * 3.14159265358979323846 * toneHz
                                                 * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                tank->processSample (l, r);
                out[n] = 0.5 * (l + r);
            }

            // Well past the tank filling.
            const auto settled = static_cast<size_t> (referenceRate * 4.0);

            p.amDepthDb = amplitudeModulationDepthDb (out.data() + settled, length - settled,
                                                      referenceRate);

            std::vector<double> tail (out.begin() + static_cast<ptrdiff_t> (settled), out.end());

            p.concentrationDb = spectralConcentrationDb (tail, referenceRate, toneHz,
                                                         262144, 2.0, 60.0);

            const auto excursion = pitchCentroidExcursion (tail, referenceRate, toneHz,
                                                           16384, 8192, 40.0);
            p.pitchPeakCents = excursion.peakCents;
        }

        return p;
    }
}

void runMatrixModulationSweepTests()
{
    // =======================================================================
    beginSuite ("Matrix modulation / depth sweep");
    // =======================================================================
    {
        info ("Angle at 100 % is " + fixed (modulation::maxAngleDegrees, 2) + " degrees.");
        info ("");
        info ("  depth   angle     persist  periodicity  flux    RT60     band     drift     AM      concentr  pitch");

        std::vector<SweepPoint> points;

        // Depths chosen so the angle column lands on the candidate list:
        // 0, 0.25, 0.5, 1, 2, 5, 10 and 20 degrees.
        const double depths[] { 0.0, 0.0125, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0 };

        for (double depth : depths)
        {
            const auto p = measurePoint (depth, modulation::defaultRateHz);
            points.push_back (p);

            info ("  " + pad (fixed (depth, 2), 8)
                  + pad (fixed (modulation::angleForDepth (depth) * 180.0 / 3.14159265358979323846, 2), 10)
                  + pad (fixed (p.persistence, 4), 9)
                  + pad (fixed (p.periodicity, 2), 13)
                  + pad (fixed (p.flux, 4), 8)
                  + pad (fixed (p.rt60, 4), 9)
                  + pad (fixed (p.bandSpread * 100.0, 1) + "%", 9)
                  + pad (fixed (p.energyDriftDb, 3), 10)
                  + pad (fixed (p.amDepthDb, 2), 8)
                  + pad (fixed (p.concentrationDb, 1), 10)
                  + fixed (p.pitchPeakCents, 2));
        }

        const auto& staticPoint = points.front();

        // Modulation has to actually do something at the top of its range.
        check (points.back().persistence < staticPoint.persistence,
               "at full depth the modes stop standing still");
        check (points.back().flux > staticPoint.flux,
               "at full depth the spectrum moves more than the static tank's");

        // Monotonic in the direction that matters, allowing for the noise in a
        // peak-tracking statistic.
        bool persistenceFalls = true;
        for (size_t i = 1; i < points.size(); ++i)
            persistenceFalls &= (points[i].persistence <= points[i - 1].persistence + 0.03);

        check (persistenceFalls, "modal persistence falls monotonically with depth");

        // And must not damage anything.
        for (size_t i = 0; i < points.size(); ++i)
        {
            checkAtMost (std::abs (points[i].rt60 - staticPoint.rt60) / staticPoint.rt60, 0.03,
                         "depth " + fixed (depths[i], 4) + ": RT60 stays within 3 % of static");
            checkAtMost (std::abs (points[i].energyDriftDb), 0.1,
                         "depth " + fixed (depths[i], 4) + ": long-run energy drift under 0.1 dB");
        }

        info ("");
        info ("shipped default depth " + fixed (modulation::defaultDepth, 3) + " = "
              + fixed (modulation::angleForDepth (modulation::defaultDepth) * 180.0
                       / 3.14159265358979323846, 2) + " degrees");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / rate sweep");
    // =======================================================================
    {
        info ("  rate     persist  periodicity  flux    RT60     drift     AM       concentr  pitch");

        const double rates[] { 0.02, 0.05, 0.10, 0.20, 0.40, 0.80, 1.20, 2.00 };

        std::vector<SweepPoint> points;

        for (double rate : rates)
        {
            const auto p = measurePoint (modulation::defaultDepth, rate);
            points.push_back (p);

            info ("  " + pad (fixed (rate, 2) + " Hz", 9)
                  + pad (fixed (p.persistence, 4), 9)
                  + pad (fixed (p.periodicity, 2), 13)
                  + pad (fixed (p.flux, 4), 8)
                  + pad (fixed (p.rt60, 4), 9)
                  + pad (fixed (p.energyDriftDb, 3), 10)
                  + pad (fixed (p.amDepthDb, 2), 9)
                  + pad (fixed (p.concentrationDb, 1), 10)
                  + fixed (p.pitchPeakCents, 2));
        }

        for (size_t i = 0; i < points.size(); ++i)
        {
            checkAtMost (std::abs (points[i].energyDriftDb), 0.15,
                         fixed (rates[i], 2) + " Hz: long-run energy drift stays bounded");
            check (std::isfinite (points[i].rt60) && points[i].rt60 > 0.0,
                   fixed (rates[i], 2) + " Hz: the decay stays measurable");
        }

        info ("");
        info ("shipped default rate " + fixed (modulation::defaultRateHz, 2) + " Hz");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / update rate and block invariance");
    // =======================================================================
    {
        // Control-rate updates are only acceptable if they do not depend on how
        // the host chops the stream, which is why the update counter is
        // free-running rather than per block.
        const auto inputL = whiteNoise (40000, 313u);
        const auto inputR = whiteNoise (40000, 717u);

        auto render = [&] (Matrix::UpdateMode mode, int blockSize)
        {
            auto tank = makeTank (referenceRate, modulation::defaultDepth,
                                  modulation::defaultRateHz);
            tank->setModulationUpdateMode (mode);

            std::vector<double> l = inputL, r = inputR;
            int n = 0;

            while (n < static_cast<int> (l.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (l.size()) - n);
                tank->processBlock (l.data() + n, r.data() + n, count);
                n += count;
            }

            return std::pair { l, r };
        };

        for (auto mode : { Matrix::UpdateMode::perSample, Matrix::UpdateMode::controlRate })
        {
            const std::string name = mode == Matrix::UpdateMode::perSample ? "per-sample" : "control-rate";
            const auto reference = render (mode, static_cast<int> (inputL.size()));

            bool allMatch = true;

            for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 })
            {
                const auto rendered = render (mode, blockSize);

                for (size_t i = 0; i < reference.first.size(); ++i)
                    allMatch &= (rendered.first[i] == reference.first[i])
                             && (rendered.second[i] == reference.second[i]);
            }

            check (allMatch, name + ": every block size renders bit-identically");

            // A block size that changes on every callback.
            {
                auto tank = makeTank (referenceRate, modulation::defaultDepth,
                                      modulation::defaultRateHz);
                tank->setModulationUpdateMode (mode);

                std::vector<double> l = inputL, r = inputR;
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
                    identical &= (l[i] == reference.first[i]) && (r[i] == reference.second[i]);

                check (identical, name + ": a block size that changes every callback also matches");
            }
        }

        // How the two modes compare on the things that matter.
        info ("");
        info ("  update mode     periodicity  persist  flux     AM depth   concentration");

        for (auto mode : { Matrix::UpdateMode::perSample, Matrix::UpdateMode::controlRate })
        {
            auto tank = makeTank (referenceRate, modulation::defaultDepth, modulation::defaultRateHz);
            tank->setModulationUpdateMode (mode);

            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 4.0));

            const auto windowStart  = static_cast<size_t> (0.30 * referenceRate);
            const auto windowLength = static_cast<size_t> (1.50 * referenceRate);
            const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

            const double periodicity = maxEnvelopePeriodicity (
                flat.data(), flat.size(),
                static_cast<size_t> (0.001 * referenceRate),
                static_cast<size_t> (0.120 * referenceRate));

            const auto spec = spectrogramOf (
                std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                     ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                referenceRate, 8192, 4096);

            // Sustained tone for the AM and concentration figures.
            auto toneTank = makeTank (referenceRate, modulation::defaultDepth, modulation::defaultRateHz);
            toneTank->setModulationUpdateMode (mode);

            const size_t toneLength = static_cast<size_t> (referenceRate * 10.0);
            std::vector<double> tone (toneLength);

            for (size_t n = 0; n < toneLength; ++n)
            {
                const double x = 0.5 * std::sin (2.0 * 3.14159265358979323846 * 1000.0
                                                 * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                toneTank->processSample (l, r);
                tone[n] = 0.5 * (l + r);
            }

            const auto settled = static_cast<size_t> (referenceRate * 4.0);
            const double am = amplitudeModulationDepthDb (tone.data() + settled,
                                                          toneLength - settled, referenceRate);

            std::vector<double> tail (tone.begin() + static_cast<ptrdiff_t> (settled), tone.end());
            const double concentration = spectralConcentrationDb (tail, referenceRate, 1000.0,
                                                                  262144, 2.0, 60.0);

            const std::string name = mode == Matrix::UpdateMode::perSample ? "per-sample" : "control-rate";

            info ("  " + pad (name, 16)
                  + pad (fixed (periodicity, 2), 13)
                  + pad (fixed (modalPeakPersistence (spec, 200.0, 4000.0, 24, 12.0), 4), 9)
                  + pad (fixed (spectralFlux (spec, 200.0, 4000.0), 4), 9)
                  + pad (fixed (am, 2) + " dB", 11)
                  + fixed (concentration, 1) + " dB");
        }
    }

    // =======================================================================
    beginSuite ("Matrix modulation / stereo and mono regression");
    // =======================================================================
    {
        // PHASE 4 shipped figures are the baseline, and modulation must not
        // move them meaningfully.
        info ("  configuration        L/R corr   balance    downmix    worst dip   separation");

        auto measureStereo = [&] (double depth, const char* label)
        {
            auto tank = makeTank (referenceRate, depth, modulation::defaultRateHz,
                                  LfoShape::sine, 2.0, true);

            const auto noise = whiteNoise (analysisLength, 5150u);
            std::vector<double> l (analysisLength), r (analysisLength), mono (analysisLength);

            for (size_t n = 0; n < analysisLength; ++n)
            {
                double a = noise[n], b = noise[n];
                tank->processSample (a, b);
                l[n] = a; r[n] = b; mono[n] = 0.5 * (a + b);
            }

            const double rho = correlation (l, r);
            const double balance = 20.0 * std::log10 (rms (l) / std::max (rms (r), 1.0e-30));
            const double downmix = 20.0 * std::log10 (rms (mono) / std::max (rms (l), 1.0e-30));

            const auto mag = magnitudeSpectrum (mono, analysisLength, true);
            const auto bands = thirdOctaveEnergy (mag, referenceRate, analysisLength, 100.0, 10000.0);

            double sum = 0.0;
            for (double e : bands.energyDb)
                sum += e;

            const double meanDb = sum / static_cast<double> (std::max<size_t> (1, bands.energyDb.size()));

            double worstDip = 0.0;
            for (double e : bands.energyDb)
                worstDip = std::max (worstDip, meanDb - e);

            // Hard-panned separation.
            auto panTank = makeTank (referenceRate, depth, modulation::defaultRateHz,
                                     LfoShape::sine, 2.0, true);

            std::vector<double> pl (analysisLength), pr (analysisLength);

            for (size_t n = 0; n < analysisLength; ++n)
            {
                double a = noise[n], b = 0.0;
                panTank->processSample (a, b);
                pl[n] = a; pr[n] = b;
            }

            const double separation = 20.0 * std::log10 (std::max (rms (pl), 1.0e-30)
                                                         / std::max (rms (pr), 1.0e-30));

            info ("  " + pad (label, 21)
                  + pad (fixed (rho, 4), 11)
                  + pad (fixed (balance, 3) + " dB", 11)
                  + pad (fixed (downmix, 2) + " dB", 11)
                  + pad (fixed (worstDip, 2) + " dB", 12)
                  + fixed (separation, 2) + " dB");

            return std::array<double, 5> { rho, balance, downmix, worstDip, separation };
        };

        const auto staticStereo   = measureStereo (0.0, "static (Depth 0)");
        const auto defaultStereo  = measureStereo (modulation::defaultDepth, "default depth");
        const auto extremeStereo  = measureStereo (1.0, "full depth");

        info ("");
        info ("PHASE 4 baseline: correlation 0.0499, balance 0.350 dB, downmix -2.97 dB,");
        info ("worst third-octave dip 1.98 dB, hard-pan separation 1.07 dB");

        check (std::abs (defaultStereo[0]) < 0.35, "default modulation keeps the channels decorrelated");
        check (defaultStereo[0] > -0.3, "correlation is not driven strongly negative");
        checkAtMost (std::abs (defaultStereo[1]), 1.0, "left and right stay balanced");
        check (defaultStereo[2] > -4.0, "the mono downmix loses less than 4 dB");
        checkAtMost (defaultStereo[3], 4.0, "worst third-octave cancellation stays under 4 dB");
        check (defaultStereo[4] > 0.8, "a hard-panned input still favours its own channel");

        checkAtMost (defaultStereo[3] - staticStereo[3], 1.0,
                     "modulation adds less than 1 dB to the worst mono dip");

        (void) extremeStereo;
    }

    // =======================================================================
    beginSuite ("Matrix modulation / automation and stability");
    // =======================================================================
    {
        auto worstEnvelopeJumpDb = [] (const std::vector<double>& x, size_t from, size_t to)
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
        };

        const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 8.0), 246810u);

        auto runAutomation = [&] (const char* label,
                                  const std::function<void (Tank&, size_t)>& automate)
        {
            auto tank = makeTank (referenceRate, modulation::defaultDepth,
                                  modulation::defaultRateHz, LfoShape::sine, 3.0, true);

            std::vector<double> out (noise.size());
            bool finite = true;

            for (size_t n = 0; n < noise.size(); ++n)
            {
                automate (*tank, n);

                double l = noise[n] * 0.4, r = -l;
                tank->processSample (l, r);

                out[n] = 0.5 * (l + r);
                finite &= std::isfinite (l) && std::isfinite (r);
            }

            const double steady = worstEnvelopeJumpDb (out, static_cast<size_t> (referenceRate),
                                                       static_cast<size_t> (referenceRate * 3.0));
            const double worst  = worstEnvelopeJumpDb (out, static_cast<size_t> (referenceRate * 3.0),
                                                       out.size());

            info ("  " + pad (label, 34) + pad (fixed (steady, 2) + " dB", 12)
                  + fixed (worst, 2) + " dB");

            check (finite, std::string (label) + ": output stays finite");
            checkAtMost (worst, std::max (steady * 1.5, 3.0),
                         std::string (label) + ": no level step");

            return out;
        };

        info ("  automation                        steady jump  worst jump");

        const auto halfway = static_cast<size_t> (referenceRate * 4.0);

        runAutomation ("Depth 0 -> 100 % instantly", [&] (Tank& t, size_t n)
        {
            if (n == halfway) t.setModulationDepth (1.0);
        });

        runAutomation ("Depth 100 -> 0 % instantly", [&] (Tank& t, size_t n)
        {
            if (n == 0) t.setModulationDepth (1.0, true);
            if (n == halfway) t.setModulationDepth (0.0);
        });

        runAutomation ("Rate min -> max instantly", [&] (Tank& t, size_t n)
        {
            if (n == 0) t.setModulationRateHz (modulation::minRateHz);
            if (n == halfway) t.setModulationRateHz (modulation::maxRateHz);
        });

        runAutomation ("Rate max -> min instantly", [&] (Tank& t, size_t n)
        {
            if (n == 0) t.setModulationRateHz (modulation::maxRateHz);
            if (n == halfway) t.setModulationRateHz (modulation::minRateHz);
        });

        runAutomation ("both automated every sample", [&] (Tank& t, size_t n)
        {
            const double phase = 2.0 * 3.14159265358979323846 * 0.7
                               * static_cast<double> (n) / referenceRate;
            t.setModulationDepth (0.5 + 0.5 * std::sin (phase));
            t.setModulationRateHz (0.4 + 0.35 * std::sin (phase * 1.31));
        });

        // A rate change must not reset the LFO phase, or every automation move
        // would produce a discontinuity in the matrix.
        {
            auto a = makeTank (referenceRate, modulation::defaultDepth, 0.5);
            auto b = makeTank (referenceRate, modulation::defaultDepth, 0.5);

            for (int n = 0; n < 20000; ++n)
            {
                double l = 0.0, r = 0.0;
                a->processSample (l, r);
                l = 0.0; r = 0.0;
                b->processSample (l, r);
            }

            const double angleBefore = a->getCore().getMatrix().getCurrentAngle (0);

            a->setModulationRateHz (1.3);

            double l = 0.0, r = 0.0;
            a->processSample (l, r);

            const double angleAfter = a->getCore().getMatrix().getCurrentAngle (0);

            info ("");
            info ("rotation angle across a rate change: " + fixed (angleBefore, 6)
                  + " -> " + fixed (angleAfter, 6) + " rad");

            checkAtMost (std::abs (angleAfter - angleBefore), 0.002,
                         "a rate change does not reset the LFO phase");
        }

        // Long run at the most extreme settings the engine allows.
        {
            auto tank = makeTank (referenceRate, 1.0, modulation::maxRateHz,
                                  LfoShape::smoothRandom, 60.0, true);

            double worstPeak = 0.0;
            bool finite = true, noSubnormal = true;
            unsigned int state = 5150u;

            for (int n = 0; n < static_cast<int> (referenceRate) * 300; ++n)
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

                double l = x, r = -x;
                tank->processSample (l, r);

                worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
                finite &= std::isfinite (l) && std::isfinite (r);
                noSubnormal &= ! (std::fpclassify (l) == FP_SUBNORMAL
                                  || std::fpclassify (r) == FP_SUBNORMAL);
            }

            info ("5 minutes of full-scale noise at maximum depth and rate into a 60 s decay:");
            info ("  worst peak " + fixed (worstPeak, 4));

            check (finite, "stays finite");
            check (noSubnormal, "produces no subnormals");
            checkAtMost (worstPeak, 60.0, "stays bounded");
        }
    }

    // =======================================================================
    beginSuite ("Matrix modulation / smooth random shape");
    // =======================================================================
    {
        info ("  shape           persist  periodicity  flux     AM depth   drift");

        for (auto shape : { LfoShape::sine, LfoShape::smoothRandom })
        {
            const auto p = measurePoint (modulation::defaultDepth, modulation::defaultRateHz, shape);

            info ("  " + pad (shape == LfoShape::sine ? "sine" : "smooth random", 16)
                  + pad (fixed (p.persistence, 4), 9)
                  + pad (fixed (p.periodicity, 2), 13)
                  + pad (fixed (p.flux, 4), 9)
                  + pad (fixed (p.amDepthDb, 2) + " dB", 11)
                  + fixed (p.energyDriftDb, 3) + " dB");

            checkAtMost (std::abs (p.energyDriftDb), 0.1,
                         std::string (shape == LfoShape::sine ? "sine" : "smooth random")
                         + ": long-run energy drift under 0.1 dB");
        }

        // Determinism: the same seeds must give the same output every time.
        auto a = makeTank (referenceRate, modulation::defaultDepth,
                           modulation::defaultRateHz, LfoShape::smoothRandom);
        auto b = makeTank (referenceRate, modulation::defaultDepth,
                           modulation::defaultRateHz, LfoShape::smoothRandom);

        const auto noise = whiteNoise (100000, 31337u);
        bool identical = true;

        for (size_t n = 0; n < noise.size(); ++n)
        {
            double al = noise[n], ar = noise[n];
            double bl = noise[n], br = noise[n];

            a->processSample (al, ar);
            b->processSample (bl, br);

            identical &= (al == bl) && (ar == br);
        }

        check (identical, "smooth random modulation is deterministic across instances");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / sample rates");
    // =======================================================================
    {
        info ("  rate       RT60     periodicity  persist  drift");

        for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto tank = makeTank (sampleRate, modulation::defaultDepth, modulation::defaultRateHz);
            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (sampleRate * 4.0));

            const auto windowStart  = static_cast<size_t> (0.30 * sampleRate);
            const auto windowLength = static_cast<size_t> (1.50 * sampleRate);
            const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

            const double periodicity = maxEnvelopePeriodicity (
                flat.data(), flat.size(),
                static_cast<size_t> (0.001 * sampleRate),
                static_cast<size_t> (0.120 * sampleRate));

            const auto spec = spectrogramOf (
                std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                     ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                sampleRate, 8192, 4096);

            const auto decay = measureDecay (ir, sampleRate);

            // Drift over a shorter run, so the matrix stays affordable at 192k.
            auto driftTank = makeTank (sampleRate, modulation::defaultDepth, modulation::defaultRateHz);
            const auto noise = whiteNoise (static_cast<size_t> (sampleRate * 20.0), 4u);
            std::vector<double> out (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double l = noise[n] * 0.3, r = -l;
                driftTank->processSample (l, r);
                out[n] = 0.5 * (l + r);
            }

            const auto chunk = static_cast<size_t> (sampleRate * 5.0);
            const double drift = 20.0 * std::log10 (rms (out.data() + chunk * 3, chunk)
                                                    / std::max (rms (out.data() + chunk, chunk), 1.0e-30));

            info ("  " + pad (fixed (sampleRate / 1000.0, 1) + " kHz", 11)
                  + pad (fixed (decay.rt60FromT30, 4), 9)
                  + pad (fixed (periodicity, 2), 13)
                  + pad (fixed (modalPeakPersistence (spec, 200.0, 4000.0, 24, 12.0), 4), 9)
                  + fixed (drift, 3) + " dB");

            checkAtMost (std::abs (decay.rt60FromT30 - 2.0) / 2.0, 0.10,
                         fixed (sampleRate / 1000.0, 1) + " kHz: RT60 within 10 % of target");
            checkAtMost (std::abs (drift), 0.15,
                         fixed (sampleRate / 1000.0, 1) + " kHz: energy drift stays bounded");
            check (allFinite (ir), fixed (sampleRate / 1000.0, 1) + " kHz: response is finite");
        }
    }

    // =======================================================================
    beginSuite ("Matrix modulation / real-time safety");
    // =======================================================================
    {
        auto tank = std::make_unique<MorphingStereoFdn<N>>();

        check (succeeded (tank->prepare (referenceRate)), "prepare succeeds");

        tank->setModulationDepth (modulation::defaultDepth, true);
        tank->setModulationRateHz (modulation::defaultRateHz);

        long long allocations = 0;
        bool finite = true;

        {
            ScopedAllocationCounter counter;

            // Modulated processing, both update modes, both shapes.
            for (auto mode : { MorphingStereoFdn<N>::Matrix::UpdateMode::perSample,
                               MorphingStereoFdn<N>::Matrix::UpdateMode::controlRate })
            {
                tank->setModulationUpdateMode (mode);

                for (auto shape : { LfoShape::sine, LfoShape::smoothRandom, LfoShape::triangle })
                {
                    tank->setModulationShape (shape);

                    for (int n = 0; n < 48000; ++n)
                    {
                        double l = 0.01, r = -0.01;
                        tank->processSample (l, r);
                        finite &= std::isfinite (l) && std::isfinite (r);
                        sink = l + r;
                    }
                }
            }

            // Depth and rate automation on every sample.
            for (int n = 0; n < 240000; ++n)
            {
                tank->setModulationDepth (static_cast<double> (n % 1000) / 1000.0);
                tank->setModulationRateHz (0.02 + 1.98 * static_cast<double> (n % 997) / 997.0);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = l;
            }

            // Schedule switching.
            for (int n = 0; n < 20000; ++n)
            {
                const modulation::Schedule schedules[] { modulation::scheduleA, modulation::scheduleB,
                                                         modulation::scheduleC, modulation::scheduleAB };
                tank->setModulationSchedule (schedules[n % 4]);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = r;
            }

            // Modulation together with Size morphs and decay automation.
            for (int n = 0; n < 480000; ++n)
            {
                tank->setSize (0.3 + 1.6 * static_cast<double> (n % 977) / 977.0);
                tank->setRt60Seconds (0.5 + 20.0 * static_cast<double> (n % 631) / 631.0);
                tank->setModulationDepth (static_cast<double> (n % 311) / 311.0);

                double l = 0.01, r = -0.01;
                tank->processSample (l, r);
                sink = l + r;
            }

            // Varying block sizes.
            {
                const int sizes[] { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 };
                double left[8192] {}, right[8192] {};

                for (int repeat = 0; repeat < 4; ++repeat)
                    for (int size : sizes)
                        tank->processBlock (left, right, size);

                sink = left[0];
            }

            for (int n = 0; n < 200; ++n)
                tank->reset();

            allocations = counter.count();
        }

        check (finite, "everything above stayed finite");
        checkNoAllocations (allocations,
                    "matrix generation, rotations, both update modes, rate and depth automation, "
                    "schedule switching, Size morphs, decay automation, varying block sizes and "
                    "reset allocate exactly nothing");
    }

    // =======================================================================
    beginSuite ("Matrix modulation / Size morph regression");
    // =======================================================================
    {
        using MTank = MorphingStereoFdn<N>;

        info ("  input                    priming ended    after       finished");

        auto scenario = [&] (const char* label, double rt60,
                             const std::function<double (int)>& source)
        {
            auto tank = std::make_unique<MTank>();
            tank->prepare (referenceRate);
            tank->setRt60Seconds (rt60, true);
            tank->setModulationDepth (modulation::defaultDepth, true);
            tank->setModulationRateHz (modulation::defaultRateHz);
            tank->reset();

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
                double l = source (static_cast<int> (referenceRate) + elapsed), r = -l;
                tank->processSample (l, r);

                finite &= std::isfinite (l) && std::isfinite (r);
                ++elapsed;
            }

            info ("  " + pad (label, 25)
                  + pad (MTank::describe (tank->getLastPrimeReason()), 17)
                  + pad (fixed (1000.0 * elapsed / referenceRate, 1) + " ms", 12)
                  + (tank->isMorphing() ? "NO" : "yes"));

            check (! tank->isMorphing(), std::string (label) + ": the transition completes");
            check (finite, std::string (label) + ": output stays finite");
            checkAtMost (static_cast<double> (elapsed), static_cast<double> (ceiling),
                         std::string (label) + ": inside the worst-case bound");
        };

        const auto noise = whiteNoise (static_cast<size_t> (referenceRate * 8.0), 90210u);

        scenario ("silence", 4.0, [] (int) { return 0.0; });
        scenario ("input at -140 dBFS", 4.0,
                  [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 1.0e-7; });
        scenario ("a single short impulse", 4.0, [] (int n) { return n == 0 ? 1.0 : 0.0; });
        scenario ("full scale, 0.3 s decay", 0.3,
                  [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 0.5; });
        scenario ("full scale, 60 s decay", 60.0,
                  [&] (int n) { return noise[static_cast<size_t> (n) % noise.size()] * 0.5; });

        // Continuous Size automation with modulation running.
        {
            auto tank = std::make_unique<MTank>();
            tank->prepare (referenceRate);
            tank->setRt60Seconds (3.0, true);
            tank->setModulationDepth (modulation::defaultDepth, true);
            tank->reset();

            int swaps = 0;
            int lastBank = tank->getActiveBankIndex();
            bool finite = true;

            for (int n = 0; n < static_cast<int> (referenceRate) * 10; ++n)
            {
                tank->setSize (1.0 + 0.7 * std::sin (2.0 * 3.14159265358979323846 * 0.5
                                                     * static_cast<double> (n) / referenceRate));

                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
                finite &= std::isfinite (l) && std::isfinite (r);

                if (tank->getActiveBankIndex() != lastBank)
                {
                    ++swaps;
                    lastBank = tank->getActiveBankIndex();
                }
            }

            info ("10 s of continuous Size automation with modulation running: "
                  + std::to_string (swaps) + " bank swaps");

            check (finite, "continuous Size automation with modulation stays finite");
            check (swaps >= 1, "transitions still complete with modulation running");
            checkAtMost (static_cast<double> (swaps), 10.0 / fdn::morphSeconds + 2.0,
                         "coalescing still bounds the transition count");
        }
    }
}

} // namespace avtest
