#include "CandidateAnalysis.h"

#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"

#include "DSP/Denormal.h"
#include "DSP/StereoFdn.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double pi = 3.14159265358979323846;

    using Tank = StereoFdn<N>;

    std::unique_ptr<Tank> makeTank (architecture::Candidate candidate,
                                    double sampleRate, double rt60, bool diffusion)
    {
        auto tank = std::make_unique<Tank>();
        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (diffusion);
        tank->setRt60Seconds (rt60, true);

        // Both engines start from a known-off state so a candidate that leaves
        // one of them alone genuinely has it off.
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);

        architecture::apply (*tank, candidate, true);
        tank->reset();

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

    /** Steady tone through the tank, returned mono, past the build-up. */
    std::vector<double> toneTail (Tank& tank, double toneHz, double seconds, double sampleRate)
    {
        const auto total = static_cast<size_t> (seconds * sampleRate);
        const auto skip  = static_cast<size_t> (1.0 * sampleRate);

        std::vector<double> out;
        out.reserve (total > skip ? total - skip : 0);

        for (size_t n = 0; n < total; ++n)
        {
            const double x = 0.25 * std::sin (2.0 * pi * toneHz * static_cast<double> (n) / sampleRate);

            double l = x, r = x;
            tank.processSample (l, r);

            if (n >= skip)
                out.push_back (0.5 * (l + r));
        }

        return out;
    }
}

CandidateMetrics measureCandidate (architecture::Candidate candidate,
                                   double sampleRate, double rt60Seconds)
{
    ScopedFtz ftz;

    CandidateMetrics m;

    // --- Movement, on a long decay so the modal analysis has something to look
    //     at. 32768-sample frames, as PHASE 5A established: the tank's modal
    //     spacing is about 2.6 Hz and shorter frames cannot resolve it.
    {
        auto tank = makeTank (candidate, sampleRate, 8.0, false);

        const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (sampleRate * 6.0));

        const auto windowStart  = static_cast<size_t> (0.50 * sampleRate);
        const auto windowLength = static_cast<size_t> (4.00 * sampleRate);

        const auto flat = flattenDecay (ir.data() + windowStart, windowLength);

        m.latePeriodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                    static_cast<size_t> (0.001 * sampleRate),
                                                    static_cast<size_t> (0.120 * sampleRate));

        const std::vector<double> window (ir.begin() + static_cast<ptrdiff_t> (windowStart),
                                          ir.begin() + static_cast<ptrdiff_t> (windowStart + windowLength));

        const auto spec = spectrogramOf (window, sampleRate, 32768, 16384);

        m.modalPersistence = modalPeakPersistence (spec, 200.0, 4000.0, 40, 4.0);
        m.modalMovementHz  = modalPeakMovementHz (spec, 200.0, 4000.0, 40, 4.0);
        m.spectralFlux     = spectralFlux (spec, 200.0, 4000.0);
        m.peakLifetime     = modalPeakLifetime (spec, 200.0, 4000.0, 40, 4.0).meanSeconds;

        m.finite = allFinite (ir);
    }

    // --- Decay.
    {
        auto tank = makeTank (candidate, sampleRate, rt60Seconds, false);
        const auto ir = monoImpulseResponse (*tank, static_cast<size_t> (sampleRate * 5.0));

        const auto decay = measureDecay (ir, sampleRate);

        m.rt60 = decay.rt60FromT30;
        m.decayLinearity = decay.linearity;
        m.rt60ErrorPercent = 100.0 * std::abs (decay.rt60FromT30 - rt60Seconds) / rt60Seconds;

        m.finite = m.finite && allFinite (ir);
    }

    // --- Sustained tone: AM, pitch, concentration.
    {
        auto tank = makeTank (candidate, sampleRate, rt60Seconds, false);

        constexpr double toneHz = 1000.0;
        const auto tail = toneTail (*tank, toneHz, 25.0, sampleRate);

        m.amDepthDb = amplitudeModulationDepthDb (tail.data(), tail.size(), sampleRate);

        const auto bands = amplitudeModulationBands (tail.data(), tail.size(), sampleRate, 1.0);

        m.amSlowDb     = bands.slowDepthDb;
        m.amFastDb     = bands.fastDepthDb;
        m.amDominantHz = bands.dominantHz;

        m.pitchCents = pitchCentroidExcursion (tail, sampleRate, toneHz, 16384, 8192, 40.0).peakCents;
        m.concentrationDb = spectralConcentrationDb (tail, sampleRate, toneHz, 32768, 4.0, 60.0);

        m.finite = m.finite && allFinite (tail);
    }

    // --- Long-run energy drift, measured across a whole number of modulation
    //     cycles of the slowest lane so the modulation itself is not read as a
    //     trend. PHASE 5A learned this the hard way.
    {
        auto tank = makeTank (candidate, sampleRate, rt60Seconds, false);

        const auto settings = architecture::settingsFor (candidate);
        const double slowest = std::max (0.02, std::min (settings.allpassRateHz > 0.0 ? settings.allpassRateHz : 1.0e9,
                                                         settings.matrixRateHz > 0.0 ? settings.matrixRateHz : 1.0e9));
        const double period = 1.0 / slowest;

        const auto cycleSamples = static_cast<size_t> (period * sampleRate);
        const auto settle = static_cast<size_t> (2.0 * sampleRate);

        double first = 0.0, last = 0.0;
        unsigned int seed = 20250804u;

        auto nextNoise = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 8388608.0 - 1.0;
        };

        const size_t total = settle + 8 * cycleSamples;

        for (size_t n = 0; n < total; ++n)
        {
            const double x = 0.1 * nextNoise();

            double l = x, r = x;
            tank->processSample (l, r);

            const double power = 0.5 * (l * l + r * r);

            if (n >= settle && n < settle + cycleSamples)
                first += power;

            if (n >= settle + 7 * cycleSamples)
                last += power;
        }

        if (first > 0.0 && last > 0.0)
            m.energyDriftDb = 10.0 * std::log10 (last / first);
    }

    // --- Stereo and mono, with the input diffuser in place because that is how
    //     the reverb actually runs.
    //
    //     Measured exactly the way PHASE 4 and PHASE 5B measured it: white noise
    //     rather than an impulse, third octaves over 100 Hz to 10 kHz, and the
    //     worst dip taken against the band mean. The 4 dB constraint refers to
    //     that quantity and its 1.98 dB static baseline, so measuring some other
    //     defensible quantity here would silently move the goalposts.
    {
        auto tank = makeTank (candidate, sampleRate, rt60Seconds, true);

        constexpr size_t length = 262144;
        const auto noise = whiteNoise (length, 5150u);

        std::vector<double> left (length), right (length), mono (length);

        for (size_t n = 0; n < length; ++n)
        {
            double l = noise[n], r = noise[n];
            tank->processSample (l, r);
            left[n] = l;
            right[n] = r;
            mono[n] = 0.5 * (l + r);
        }

        m.correlation = correlation (left, right);

        const double leftRms = rms (left), rightRms = rms (right);

        if (leftRms > 0.0 && rightRms > 0.0)
            m.balanceDb = 20.0 * std::log10 (leftRms / rightRms);

        if (rms (mono) > 0.0 && leftRms > 0.0)
            m.monoDownmixDb = 20.0 * std::log10 (rms (mono) / leftRms);

        const auto magnitude = magnitudeSpectrum (mono, length, true);
        const auto bands = thirdOctaveEnergy (magnitude, sampleRate, length, 100.0, 10000.0);

        double sum = 0.0;

        for (double e : bands.energyDb)
            sum += e;

        const double meanDb = sum / static_cast<double> (std::max<size_t> (1, bands.energyDb.size()));

        double worst = 0.0;

        for (double e : bands.energyDb)
            worst = std::max (worst, meanDb - e);

        m.worstThirdOctaveDb = worst;

        m.finite = m.finite && allFinite (left) && allFinite (right);
    }

    return m;
}

std::string ConstraintCheck::firstFailure() const
{
    if (! finiteOk) return "non-finite output";
    if (! driftOk)  return "energy drift";
    if (! rt60Ok)   return "RT60 error";
    if (! monoOk)   return "mono cancellation";
    if (! pitchOk)  return "pitch deviation";
    if (! chorusOk) return "spectral concentration collapsed";

    return "none";
}

ConstraintCheck checkConstraints (const CandidateMetrics& m, double staticConcentrationDb)
{
    ConstraintCheck c;

    c.finiteOk = m.finite;
    c.driftOk  = std::abs (m.energyDriftDb) <= 0.1;
    c.rt60Ok   = m.rt60ErrorPercent <= 3.0;
    c.monoOk   = m.worstThirdOctaveDb < 4.0;
    c.pitchOk  = m.pitchCents < 3.0;
    c.chorusOk = m.concentrationDb >= staticConcentrationDb - chorusConcentrationDropDb;

    return c;
}

} // namespace avtest
