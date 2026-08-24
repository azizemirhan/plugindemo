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
    constexpr double pi = 3.14159265358979323846;
    constexpr size_t analysisLength = 32768;

    using Mode = TimeVaryingAllpass::Mode;
    using Tank = StereoFdn<N>;

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
                                                  double rt60 = 2.0, bool diffusion = false,
                                                  int activeLines = allpassMod::defaultActiveLines,
                                                  int sectionsPerLine = 1)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);
        tank->setModulationDepth (0.0, true);
        tank->setAllpassMode (Mode::energyPreserving);
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

    struct Point
    {
        double persistence = 0.0;
        double periodicity = 0.0;
        double movement = 0.0;
        double flux = 0.0;
        double rt60 = 0.0;
        double bandSpread = 0.0;
        double drift = 0.0;
        double am = 0.0;
        double pitch = 0.0;
        double concentration = 0.0;
        double echoDensity = 0.0;
    };

    Point measure (double depth, double rate, LfoShape shape = LfoShape::sine,
                   int activeLines = allpassMod::defaultActiveLines, int sections = 1)
    {
        Point p;

        {
            auto tank = makeTank (referenceRate, depth, rate, shape, 8.0, false,
                                  activeLines, sections);
            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (referenceRate * 6.0));

            const auto windowStart  = static_cast<size_t> (0.50 * referenceRate);
            const auto windowLength = static_cast<size_t> (4.00 * referenceRate);
            const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

            p.periodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                    static_cast<size_t> (0.001 * referenceRate),
                                                    static_cast<size_t> (0.120 * referenceRate));
            p.echoDensity = normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());

            const auto spec = spectrogramOf (
                std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                     ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                referenceRate, 32768, 16384);

            p.persistence = modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0);
            p.movement    = modalPeakMovementHz (spec, 200.0, 4000.0, 40, 4.0);
            p.flux        = spectralFlux (spec, 200.0, 4000.0);
        }

        {
            auto tank = makeTank (referenceRate, depth, rate, shape, 2.0, false,
                                  activeLines, sections);
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

        {
            auto tank = makeTank (referenceRate, depth, rate, shape, 2.0, false,
                                  activeLines, sections);

            const double period = 1.0 / rate;
            const double totalSeconds = std::min (150.0, std::max (40.0, 25.0 + period));

            const auto noise = whiteNoise (static_cast<size_t> (referenceRate * totalSeconds), 777u);
            std::vector<double> out (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
            {
                double l = noise[n] * 0.3, r = -l;
                tank->processSample (l, r);
                out[n] = 0.5 * (l + r);
            }

            const auto window = static_cast<size_t> (referenceRate * 10.0);
            const auto start  = static_cast<size_t> (referenceRate * 5.0);
            const auto cycles = std::max (1.0, std::floor ((totalSeconds - 5.0 - 20.0) / period));
            const auto offset = static_cast<size_t> (referenceRate * period * cycles);

            if (start + offset + window <= out.size())
                p.drift = 20.0 * std::log10 (rms (out.data() + start + offset, window)
                                             / std::max (rms (out.data() + start, window), 1.0e-30));
        }

        {
            auto tank = makeTank (referenceRate, depth, rate, shape, 2.0, false,
                                  activeLines, sections);

            constexpr double toneHz = 1000.0;
            const size_t length = static_cast<size_t> (referenceRate * 14.0);
            std::vector<double> out (length);

            for (size_t n = 0; n < length; ++n)
            {
                const double x = 0.5 * std::sin (2.0 * pi * toneHz
                                                 * static_cast<double> (n) / referenceRate);
                double l = x, r = x;
                tank->processSample (l, r);
                out[n] = 0.5 * (l + r);
            }

            const auto settled = static_cast<size_t> (referenceRate * 5.0);

            p.am = amplitudeModulationDepthDb (out.data() + settled, length - settled, referenceRate);

            std::vector<double> tail (out.begin() + static_cast<ptrdiff_t> (settled), out.end());
            p.pitch = pitchCentroidExcursion (tail, referenceRate, toneHz, 16384, 8192, 40.0).peakCents;
            p.concentration = spectralConcentrationDb (tail, referenceRate, toneHz, 262144, 2.0, 60.0);
        }

        return p;
    }

    void report (const std::string& label, const Point& p)
    {
        info ("  " + pad (label, 14)
              + pad (fixed (p.persistence, 4), 9)
              + pad (fixed (p.periodicity, 2), 13)
              + pad (fixed (p.movement, 3), 10)
              + pad (fixed (p.rt60, 4), 9)
              + pad (fixed (p.drift, 3), 9)
              + pad (fixed (p.am, 2), 7)
              + pad (fixed (p.pitch, 2), 8)
              + fixed (p.concentration, 1));
    }
}

void runAllpassModulationSweepTests()
{
    // =======================================================================
    beginSuite ("Allpass / depth sweep");
    // =======================================================================
    {
        info ("  depth         persist  periodicity  movement  RT60     drift    AM     pitch   concentr");

        const double depths[] { 0.0, 0.1, 0.25, 0.35, 0.5, 0.75, 1.0 };
        std::vector<Point> points;

        for (double depth : depths)
        {
            const auto p = measure (depth, allpassMod::defaultRateHz);
            points.push_back (p);
            report (fixed (depth, 2), p);
        }

        const auto& staticPoint = points.front();

        check (points.back().persistence < staticPoint.persistence,
               "modal persistence falls as depth rises");

        bool monotone = true;
        for (size_t i = 1; i < points.size(); ++i)
            monotone &= (points[i].persistence <= points[i - 1].persistence + 0.03);

        check (monotone, "persistence falls monotonically with depth");

        for (size_t i = 0; i < points.size(); ++i)
        {
            checkAtMost (std::abs (points[i].rt60 - staticPoint.rt60) / staticPoint.rt60, 0.03,
                         "depth " + fixed (depths[i], 2) + ": RT60 within 3 % of the bypassed tank");
            checkAtMost (std::abs (points[i].drift), 0.1,
                         "depth " + fixed (depths[i], 2) + ": energy drift under 0.1 dB");
        }

        info ("");
        info ("shipped default depth " + fixed (allpassMod::defaultDepth, 2)
              + ", coefficient swing " + fixed (allpassMod::swingForDepth (allpassMod::defaultDepth), 3));
    }

    // =======================================================================
    beginSuite ("Allpass / rate sweep");
    // =======================================================================
    {
        info ("  rate          persist  periodicity  movement  RT60     drift    AM     pitch   concentr");

        const double rates[] { 0.02, 0.05, 0.10, 0.20, 0.40, 0.80, 1.20, 2.00 };

        for (double rate : rates)
        {
            const auto p = measure (allpassMod::defaultDepth, rate);
            report (fixed (rate, 2) + " Hz", p);

            checkAtMost (std::abs (p.drift), 0.15,
                         fixed (rate, 2) + " Hz: energy drift stays bounded");
            check (std::isfinite (p.rt60) && p.rt60 > 0.0,
                   fixed (rate, 2) + " Hz: the decay stays measurable");
        }

        info ("");
        info ("shipped default rate " + fixed (allpassMod::defaultRateHz, 2) + " Hz");
    }

    // =======================================================================
    beginSuite ("Allpass / bank size and cascade depth");
    // =======================================================================
    {
        info ("  configuration persist  periodicity  movement  RT60     drift    AM     pitch   concentr");

        for (int lines : { 2, 4, 8 })
            report (std::to_string (lines) + " lines, 1x",
                    measure (allpassMod::defaultDepth, allpassMod::defaultRateHz,
                             LfoShape::sine, lines, 1));

        report ("8 lines, 2x", measure (allpassMod::defaultDepth, allpassMod::defaultRateHz,
                                        LfoShape::sine, 8, 2));

        info ("");
        info ("shipped: " + std::to_string (allpassMod::defaultActiveLines) + " lines, "
              + std::to_string (allpassMod::defaultSectionsPerLine) + " section(s) per line");
    }

    // =======================================================================
    beginSuite ("Allpass / LFO shapes");
    // =======================================================================
    {
        info ("  shape         persist  periodicity  movement  RT60     drift    AM     pitch   concentr");

        report ("sine", measure (allpassMod::defaultDepth, allpassMod::defaultRateHz, LfoShape::sine));
        report ("smooth random", measure (allpassMod::defaultDepth, allpassMod::defaultRateHz,
                                          LfoShape::smoothRandom));

        // Determinism.
        auto a = makeTank (referenceRate, allpassMod::defaultDepth, allpassMod::defaultRateHz,
                           LfoShape::smoothRandom);
        auto b = makeTank (referenceRate, allpassMod::defaultDepth, allpassMod::defaultRateHz,
                           LfoShape::smoothRandom);

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

        check (identical, "smooth random allpass modulation is deterministic across instances");
    }

    // =======================================================================
    beginSuite ("Allpass / stereo and mono");
    // =======================================================================
    {
        info ("  configuration        L/R corr   balance    downmix    worst dip   separation");

        auto measureStereo = [&] (double depth, const char* label)
        {
            auto tank = makeTank (referenceRate, depth, allpassMod::defaultRateHz,
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

            auto panTank = makeTank (referenceRate, depth, allpassMod::defaultRateHz,
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

            info ("  " + pad (label, 21) + pad (fixed (rho, 4), 11)
                  + pad (fixed (balance, 3) + " dB", 11)
                  + pad (fixed (downmix, 2) + " dB", 11)
                  + pad (fixed (worstDip, 2) + " dB", 12)
                  + fixed (separation, 2) + " dB");

            return std::array<double, 5> { rho, balance, downmix, worstDip, separation };
        };

        const auto bypassed = measureStereo (0.0, "bypassed");
        const auto shipped  = measureStereo (allpassMod::defaultDepth, "default depth");
        const auto extreme  = measureStereo (1.0, "full depth");

        info ("");
        info ("PHASE 4 baseline: correlation 0.0499, balance 0.350 dB, downmix -2.97 dB,");
        info ("worst third-octave dip 1.98 dB, hard-pan separation 1.07 dB");

        check (std::abs (shipped[0]) < 0.35, "the channels stay decorrelated");
        check (shipped[0] > -0.3, "correlation is not driven strongly negative");
        checkAtMost (std::abs (shipped[1]), 1.0, "left and right stay balanced");
        check (shipped[2] > -4.0, "the mono downmix loses less than 4 dB");
        checkAtMost (shipped[3], 4.0, "worst third-octave cancellation stays under 4 dB");
        check (shipped[4] > 0.8, "a hard-panned input still favours its own channel");
        checkAtMost (shipped[3] - bypassed[3], 1.0,
                     "allpass modulation adds less than 1 dB to the worst mono dip");

        (void) extreme;
    }

    // =======================================================================
    beginSuite ("Allpass / automation, stability and long decays");
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
        const auto halfway = static_cast<size_t> (referenceRate * 4.0);

        info ("  automation                        steady jump  worst jump");

        auto runAutomation = [&] (const char* label,
                                  const std::function<void (Tank&, size_t)>& automate)
        {
            auto tank = makeTank (referenceRate, allpassMod::defaultDepth,
                                  allpassMod::defaultRateHz, LfoShape::sine, 3.0, true);

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
        };

        runAutomation ("Depth 0 -> 100 %", [&] (Tank& t, size_t n)
        { if (n == halfway) t.setAllpassDepth (1.0); });

        runAutomation ("Depth 100 -> 0 %", [&] (Tank& t, size_t n)
        { if (n == 0) t.setAllpassDepth (1.0, true); if (n == halfway) t.setAllpassDepth (0.0); });

        runAutomation ("Rate min -> max", [&] (Tank& t, size_t n)
        { if (n == 0) t.setAllpassRateHz (allpassMod::minRateHz);
          if (n == halfway) t.setAllpassRateHz (allpassMod::maxRateHz); });

        runAutomation ("Rate max -> min", [&] (Tank& t, size_t n)
        { if (n == 0) t.setAllpassRateHz (allpassMod::maxRateHz);
          if (n == halfway) t.setAllpassRateHz (allpassMod::minRateHz); });

        runAutomation ("mode A -> B -> C", [&] (Tank& t, size_t n)
        { if (n == halfway / 2) t.setAllpassMode (Mode::thiran);
          if (n == halfway) t.setAllpassMode (Mode::schroederReference);
          if (n == halfway + halfway / 2) t.setAllpassMode (Mode::energyPreserving); });

        runAutomation ("sine -> smooth random", [&] (Tank& t, size_t n)
        { if (n == halfway) t.setAllpassShape (LfoShape::smoothRandom); });

        runAutomation ("both automated every sample", [&] (Tank& t, size_t n)
        {
            const double phase = 2.0 * pi * 0.7 * static_cast<double> (n) / referenceRate;
            t.setAllpassDepth (0.5 + 0.5 * std::sin (phase));
            t.setAllpassRateHz (0.15 + 0.14 * std::sin (phase * 1.31));
        });

        runAutomation ("with matrix modulation running", [&] (Tank& t, size_t n)
        {
            if (n == 0) t.setModulationDepth (modulation::defaultDepth, true);
            if (n == halfway) t.setAllpassDepth (1.0);
        });

        // A rate change must not reset the LFO phase.
        {
            auto tank = makeTank (referenceRate, allpassMod::defaultDepth, 0.5);

            for (int n = 0; n < 20000; ++n)
            {
                double l = 0.0, r = 0.0;
                tank->processSample (l, r);
            }

            const double before = tank->getCore().getAllpassBank().getCoefficient (0);
            tank->setAllpassRateHz (1.3);

            double l = 0.0, r = 0.0;
            tank->processSample (l, r);

            const double after = tank->getCore().getAllpassBank().getCoefficient (0);

            info ("");
            info ("coefficient across a rate change: " + fixed (before, 6) + " -> " + fixed (after, 6));

            checkAtMost (std::abs (after - before), 0.002,
                         "a rate change does not reset the LFO phase");
        }

        // Long decays, including the research boundary at unity loop gain.
        info ("");
        info ("  decay          peak      finite   subnormal-free");

        for (double rt60 : { 2.0, 10.0, 100.0 })
        {
            auto tank = makeTank (referenceRate, 1.0, allpassMod::maxRateHz,
                                  LfoShape::smoothRandom, rt60, true);

            double worstPeak = 0.0;
            bool finite = true, clean = true;
            unsigned int state = 5150u;

            for (int n = 0; n < static_cast<int> (referenceRate) * 120; ++n)
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

                double l = x, r = -x;
                tank->processSample (l, r);

                worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
                finite &= std::isfinite (l) && std::isfinite (r);
                clean &= ! (std::fpclassify (l) == FP_SUBNORMAL
                            || std::fpclassify (r) == FP_SUBNORMAL);
            }

            info ("  " + pad (fixed (rt60, 1) + " s", 15) + pad (fixed (worstPeak, 3), 10)
                  + pad (finite ? "yes" : "NO", 9) + (clean ? "yes" : "NO"));

            check (finite, fixed (rt60, 1) + " s: two minutes at maximum depth stays finite");
            check (clean, fixed (rt60, 1) + " s: no subnormals");
            checkAtMost (worstPeak, 80.0, fixed (rt60, 1) + " s: stays bounded");
        }

        // The research boundary: a loop gain of exactly 1 is not a product
        // setting, but the energy-preserving structure has to survive it.
        {
            auto tank = std::make_unique<Tank>();
            tank->prepare (referenceRate);
            tank->setInputDiffusionEnabled (false);
            tank->setRt60Seconds (fdn::maxRt60Seconds, true);
            tank->setAllpassMode (Mode::energyPreserving);
            tank->setAllpassRateHz (allpassMod::maxRateHz);
            tank->setAllpassDepth (1.0, true);
            tank->reset();

            double worstPeak = 0.0;
            bool finite = true;

            for (int n = 0; n < static_cast<int> (referenceRate) * 120; ++n)
            {
                double l = (n == 0) ? 1.0 : 0.0;
                double r = l;
                tank->processSample (l, r);

                worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
                finite &= std::isfinite (l) && std::isfinite (r);
            }

            info ("");
            info ("research boundary, the longest decay the tank exposes, maximum modulation:");
            info ("  two minutes after one impulse, peak " + fixed (worstPeak, 5));

            check (finite, "the research boundary stays finite");
            checkAtMost (worstPeak, 2.0, "and bounded");
        }
    }

    // =======================================================================
    beginSuite ("Allpass / sample rate, block size and real-time safety");
    // =======================================================================
    {
        info ("  rate       RT60     persist  drift");

        for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto tank = makeTank (sampleRate, allpassMod::defaultDepth, allpassMod::defaultRateHz);
            const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (sampleRate * 4.0));

            const auto windowStart  = static_cast<size_t> (0.50 * sampleRate);
            const auto windowLength = static_cast<size_t> (2.00 * sampleRate);

            const auto spec = spectrogramOf (
                std::vector<double> (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                     ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength)),
                sampleRate, 32768, 16384);

            const auto decay = measureDecay (ir, sampleRate);

            auto driftTank = makeTank (sampleRate, allpassMod::defaultDepth, allpassMod::defaultRateHz);
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
                  + pad (fixed (modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0), 4), 9)
                  + fixed (drift, 3) + " dB");

            checkAtMost (std::abs (decay.rt60FromT30 - 2.0) / 2.0, 0.10,
                         fixed (sampleRate / 1000.0, 1) + " kHz: RT60 within 10 % of target");
            check (allFinite (ir), fixed (sampleRate / 1000.0, 1) + " kHz: response is finite");
        }

        // Block segmentation invariance for every mode.
        const auto inputL = whiteNoise (40000, 313u);
        const auto inputR = whiteNoise (40000, 717u);

        for (auto mode : { Mode::energyPreserving, Mode::thiran, Mode::schroederReference })
        {
            auto render = [&] (int blockSize)
            {
                auto tank = makeTank (referenceRate, allpassMod::defaultDepth,
                                      allpassMod::defaultRateHz);
                tank->setAllpassMode (mode);

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

            const auto reference = render (static_cast<int> (inputL.size()));
            bool allMatch = true;

            for (int blockSize : { 1, 16, 64, 512, 8192 })
            {
                const auto rendered = render (blockSize);

                for (size_t i = 0; i < reference.first.size(); ++i)
                    allMatch &= (rendered.first[i] == reference.first[i])
                             && (rendered.second[i] == reference.second[i]);
            }

            check (allMatch, std::string (TimeVaryingAllpass::describe (mode))
                             + ": every block size renders bit-identically");
        }

        // Real-time safety.
        {
            auto tank = std::make_unique<MorphingStereoFdn<N>>();
            check (succeeded (tank->prepare (referenceRate)), "prepare succeeds");

            long long allocations = 0;
            bool finite = true;

            {
                ScopedAllocationCounter counter;

                for (auto mode : { Mode::energyPreserving, Mode::thiran, Mode::schroederReference })
                {
                    tank->setAllpassMode (mode);

                    for (auto shape : { LfoShape::sine, LfoShape::smoothRandom })
                    {
                        tank->setAllpassShape (shape);

                        for (int n = 0; n < 48000; ++n)
                        {
                            tank->setAllpassDepth (static_cast<double> (n % 1000) / 1000.0);
                            tank->setAllpassRateHz (0.02 + 1.98 * static_cast<double> (n % 997) / 997.0);
                            tank->setAllpassActiveLines (n % 9);
                            tank->setAllpassSectionsPerLine (1 + (n % 2));

                            double l = 0.01, r = -0.01;
                            tank->processSample (l, r);
                            finite &= std::isfinite (l) && std::isfinite (r);
                            sink = l + r;
                        }
                    }
                }

                // Together with Size morphs, decay automation and matrix modulation.
                for (int n = 0; n < 240000; ++n)
                {
                    tank->setSize (0.3 + 1.6 * static_cast<double> (n % 977) / 977.0);
                    tank->setRt60Seconds (0.5 + 20.0 * static_cast<double> (n % 631) / 631.0);
                    tank->setModulationDepth (static_cast<double> (n % 311) / 311.0);
                    tank->setAllpassDepth (static_cast<double> (n % 419) / 419.0);

                    double l = 0.01, r = -0.01;
                    tank->processSample (l, r);
                    sink = l;
                }

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
                        "allpass processing in all three modes, both shapes, rate and depth "
                        "automation, bank resizing, Size morphs, decay automation, matrix "
                        "modulation, varying block sizes and reset allocate exactly nothing");
        }

        // Size morph regression with allpass modulation running.
        {
            using MTank = MorphingStereoFdn<N>;

            auto tank = std::make_unique<MTank>();
            tank->prepare (referenceRate);
            tank->setRt60Seconds (4.0, true);
            tank->setAllpassDepth (allpassMod::defaultDepth, true);
            tank->reset();

            for (int n = 0; n < static_cast<int> (referenceRate); ++n)
            {
                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
            }

            tank->setSize (1.7);

            const int ceiling = tank->getWorstCaseTransitionSamples();
            int elapsed = 0;

            while (tank->isMorphing() && elapsed < ceiling * 4)
            {
                double l = 0.3, r = -0.3;
                tank->processSample (l, r);
                ++elapsed;
            }

            check (! tank->isMorphing(), "a Size morph still completes with allpass modulation running");
            checkAtMost (static_cast<double> (elapsed), static_cast<double> (ceiling),
                         "and inside the worst-case bound");
        }
    }
}

} // namespace avtest
