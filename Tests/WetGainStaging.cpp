#include "WetGainStaging.h"

#include "DiagnosticAnalyzer.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/SizeCompensation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double pi = 3.14159265358979323846;

    using Morph = MorphingStereoFdn<N>;

    [[nodiscard]] double toDb (double linear) noexcept
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear)
                            : -std::numeric_limits<double>::infinity();
    }

    [[nodiscard]] bool isSubnormalValue (double x) noexcept
    {
        return x != 0.0 && std::abs (x) < std::numeric_limits<double>::min();
    }

    /**
        A tank configured exactly as the plugin configures it.

        One function rather than a copy per call site: the whole point of these
        measurements is that they describe the shipping chain, and three
        slightly different setups would quietly measure three different things.
    */
    [[nodiscard]] std::unique_ptr<Morph> makeTank (double sampleRate,
                                                   double decaySeconds,
                                                   double sizeControl,
                                                   bool compensationEnabled,
                                                   double modDepthControl = defaults::modDepthControl)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (sampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        tank->setCompensationEnabled (compensationEnabled, true);

        tank->setRt60Seconds (mapping::rt60For (decaySeconds), true);
        tank->setSize (mapping::sizeScaleFor (sizeControl));

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (modDepthControl),
                                      mapping::modRateScaleFor (defaults::modRateControlHz),
                                      true);

        // Settle the Size morph on silence, then clear the tank. Without this
        // the render holds a transition into the target size rather than the
        // size itself, and the level being measured is not the level that
        // setting produces.
        for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        tank->reset();

        return tank;
    }

    [[nodiscard]] double peakOf (const std::vector<StereoFrame>& frames) noexcept
    {
        double peak = 0.0;

        for (const auto& f : frames)
            peak = std::max (peak, std::max (std::abs (f.l), std::abs (f.r)));

        return peak;
    }

    template <typename T>
    void writeLittleEndian (std::ostream& out, T value)
    {
        for (size_t i = 0; i < sizeof (T); ++i)
            out.put (static_cast<char> ((value >> (8 * i)) & 0xff));
    }

    /**
        32-bit float stereo WAV.

        A second copy of the writer in GainStaging.cpp, which is file-local
        there. Float rather than integer because these files exist to be
        compared: a candidate that exceeds full scale has to survive to the
        listener's converter as the number it is, not be clipped by the file
        format on the way.
    */
    bool writeWavFrames (const std::string& path, const std::vector<StereoFrame>& frames,
                         double sampleRate)
    {
        std::ofstream out (path, std::ios::binary | std::ios::trunc);

        if (! out.is_open())
            return false;

        const uint16_t channels = 2;
        const uint16_t bitsPerSample = 32;
        const auto rate = static_cast<uint32_t> (sampleRate);
        const uint16_t blockAlign = static_cast<uint16_t> (channels * bitsPerSample / 8);
        const auto dataBytes = static_cast<uint32_t> (frames.size() * blockAlign);

        out.write ("RIFF", 4);
        writeLittleEndian (out, static_cast<uint32_t> (36 + dataBytes));
        out.write ("WAVE", 4);

        out.write ("fmt ", 4);
        writeLittleEndian (out, static_cast<uint32_t> (16));
        writeLittleEndian (out, static_cast<uint16_t> (3));   // IEEE float
        writeLittleEndian (out, channels);
        writeLittleEndian (out, rate);
        writeLittleEndian (out, static_cast<uint32_t> (rate * blockAlign));
        writeLittleEndian (out, blockAlign);
        writeLittleEndian (out, bitsPerSample);

        out.write ("data", 4);
        writeLittleEndian (out, dataBytes);

        for (const auto& frame : frames)
        {
            const float samples[2] { static_cast<float> (frame.l),
                                     static_cast<float> (frame.r) };

            uint32_t bits[2];
            std::memcpy (bits, samples, sizeof (samples));

            writeLittleEndian (out, bits[0]);
            writeLittleEndian (out, bits[1]);
        }

        return out.good();
    }

    [[nodiscard]] double truePeakOf (const std::vector<StereoFrame>& frames)
    {
        std::vector<double> left (frames.size()), right (frames.size());

        for (size_t n = 0; n < frames.size(); ++n)
        {
            left[n] = frames[n].l;
            right[n] = frames[n].r;
        }

        return std::max (truePeakOfSignal (left), truePeakOfSignal (right));
    }
}

// ===========================================================================
//  FU2-B
// ===========================================================================

WindowMeasurement measureWindow (const std::string& name,
                                 const std::vector<StereoFrame>& frames,
                                 double sampleRate,
                                 const WindowMeasurement* inputReference)
{
    WindowMeasurement m;

    m.name = name;
    m.frames = frames.size();
    m.seconds = sampleRate > 0.0 ? static_cast<double> (frames.size()) / sampleRate : 0.0;

    if (frames.empty())
        return m;

    double sumL = 0.0, sumR = 0.0, dcL = 0.0, dcR = 0.0;

    for (const auto& f : frames)
    {
        sumL += f.l * f.l;
        sumR += f.r * f.r;
        dcL += f.l;
        dcR += f.r;

        m.peak = std::max (m.peak, std::max (std::abs (f.l), std::abs (f.r)));

        if (std::abs (f.l) > 1.0) ++m.overUnity;
        if (std::abs (f.r) > 1.0) ++m.overUnity;
    }

    const auto count = static_cast<double> (frames.size());

    const double msL = sumL / count;
    const double msR = sumR / count;

    m.rms = std::sqrt (0.5 * (msL + msR));
    m.rmsDb = toDb (m.rms);
    m.rmsLeftDb = toDb (std::sqrt (msL));
    m.rmsRightDb = toDb (std::sqrt (msR));

    m.peakDb = toDb (m.peak);
    m.truePeak = truePeakOf (frames);
    m.truePeakDb = toDb (m.truePeak);

    m.crestDb = m.rms > 0.0 ? toDb (m.peak / m.rms) : 0.0;

    m.dcLeft = dcL / count;
    m.dcRight = dcR / count;

    m.loudnessLufs = integratedLoudnessLufs (frames, sampleRate);

    if (inputReference != nullptr)
    {
        if (std::isfinite (m.loudnessLufs) && std::isfinite (inputReference->loudnessLufs))
            m.loudnessDeltaVsInput = m.loudnessLufs - inputReference->loudnessLufs;

        if (std::isfinite (m.peakDb) && std::isfinite (inputReference->peakDb))
            m.peakDeltaVsInput = m.peakDb - inputReference->peakDb;
    }

    return m;
}

bool writeStereoWav (const std::string& path, const std::vector<StereoFrame>& frames,
                     double sampleRate)
{
    return writeWavFrames (path, frames, sampleRate);
}

std::vector<StereoFrame> renderShippingWet (const std::vector<StereoFrame>& dry,
                                            double sampleRate, double modDepthControl,
                                            double tailSeconds)
{
    auto tank = makeTank (sampleRate, defaults::decaySeconds, defaults::sizeControl, true,
                          modDepthControl);

    const auto tail = static_cast<size_t> (sampleRate * std::max (0.0, tailSeconds));

    std::vector<StereoFrame> wet (dry.size() + tail);

    for (size_t n = 0; n < wet.size(); ++n)
    {
        double l = n < dry.size() ? dry[n].l : 0.0;
        double r = n < dry.size() ? dry[n].r : 0.0;

        tank->processSample (l, r);

        const double mid  = (l + r) * 0.5;
        const double side = (l - r) * 0.5 * defaults::width;

        wet[n] = { mid + side, mid - side };
    }

    // The shipping preview trim, exactly as the plugin computes it. Not a
    // normalisation and not a loudness match: the same number is applied to
    // every render in the set, so nothing about the relative levels moves.
    const double trim = mapping::previewWetTrim (mapping::rt60For (defaults::decaySeconds),
                                                 mapping::sizeScaleFor (defaults::sizeControl));

    for (auto& f : wet)
    {
        f.l *= trim;
        f.r *= trim;
    }

    return wet;
}

std::vector<NamedSource> makeFu2Sources (double sampleRate)
{
    // Duplicated from exportMixLawCandidates rather than shared through a
    // header, because that function builds them as local lambdas and lifting
    // them out would change a file the listening set was already made from.
    // The test suite checks these against that set sample for sample.
    const auto makeTone = [] (double rate, double seconds, double f, double amp)
    {
        std::vector<StereoFrame> v (static_cast<size_t> (rate * seconds));

        for (size_t n = 0; n < v.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double env = std::min (1.0, std::min (t / 0.3, (seconds - t) / 0.3));
            const double x = amp * env * std::sin (2.0 * pi * f * t);
            v[n] = { x, x * 0.97 };
        }

        return v;
    };

    const auto makeHits = [] (double rate, double seconds, double amp)
    {
        std::vector<StereoFrame> v (static_cast<size_t> (rate * seconds));
        unsigned int seed = 4242u;

        for (size_t n = 0; n < v.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double since = std::fmod (t, 0.5);
            const double env = std::exp (-since / 0.012);

            seed = seed * 1664525u + 1013904223u;
            const double noise = static_cast<double> (seed >> 8) / 8388608.0 - 1.0;

            const double x = amp * env * noise;
            v[n] = { x, x * 0.9 };
        }

        return v;
    };

    const auto makeChord = [] (double rate, double seconds, double amp)
    {
        std::vector<StereoFrame> v (static_cast<size_t> (rate * seconds));
        const double f[] = { 220.0, 277.18, 329.63, 440.0 };

        for (size_t n = 0; n < v.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double env = std::exp (-t / 2.0);

            double s = 0.0;
            for (double freq : f)
                s += std::sin (2.0 * pi * freq * t);

            const double x = amp * env * s * 0.25;
            v[n] = { x, x * 0.95 };
        }

        return v;
    };

    std::vector<NamedSource> sources;

    sources.push_back ({ "mixed", makeGainStagingSource (sampleRate) });
    sources.push_back ({ "pad",   makeTone (sampleRate, 6.0, 220.0, 0.25) });
    sources.push_back ({ "snare", makeHits (sampleRate, 6.0, 0.55) });
    sources.push_back ({ "piano", makeChord (sampleRate, 6.0, 0.45) });

    return sources;
}

// ===========================================================================
//  FU2-C
// ===========================================================================

WetPathTaps renderWetPathTaps (const std::vector<StereoFrame>& dry, double sampleRate)
{
    ScopedFtz ftz;

    WetPathTaps taps;

    const double rt60 = mapping::rt60For (defaults::decaySeconds);
    const double sizeScale = mapping::sizeScaleFor (defaults::sizeControl);

    taps.trim = mapping::previewWetTrim (rt60, sizeScale);
    taps.trimDb = toDb (taps.trim);

    const auto tail = static_cast<size_t> (sampleRate * (defaults::decaySeconds + 1.0));
    const size_t length = dry.size() + tail;

    taps.input.assign (length, StereoFrame {});

    for (size_t n = 0; n < length; ++n)
        taps.input[n] = n < dry.size() ? dry[n] : StereoFrame {};

    // --- pass 1: compensation on, every node after it -----------------------

    {
        auto tank = makeTank (sampleRate, defaults::decaySeconds, defaults::sizeControl, true);

        // The active bank, not bank 0. Setting Size starts a morph and the
        // morph swaps banks, so after the settle the active index is 1 and
        // bank 0 still holds the gain for the size the tank opened at.
        taps.compensationGain = tank->getCompensationGain (tank->getActiveBankIndex());
        taps.compensationDb = toDb (taps.compensationGain);

        taps.analyticCompensationDb =
            sizeComp::correctionDb (sizeScale, rt60, true);

        taps.afterComp.assign (length, StereoFrame {});
        taps.afterTrim.assign (length, StereoFrame {});
        taps.afterWidth.assign (length, StereoFrame {});
        taps.finalWet.assign (length, StereoFrame {});

        const auto mixGains = mapping::mixCoefficientsFor (1.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = taps.input[n].l;
            double r = taps.input[n].r;

            tank->processSample (l, r);
            taps.afterComp[n] = { l, r };

            l *= taps.trim;
            r *= taps.trim;
            taps.afterTrim[n] = { l, r };

            // Width, exactly as PluginProcessor applies it.
            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * defaults::width;

            const double wl = mid + side;
            const double wr = mid - side;

            taps.afterWidth[n] = { wl, wr };

            // Mix at 100%. The dry coefficient is pinned to zero, so this is
            // the wet branch alone - written out in full rather than assumed,
            // so the tap is the arithmetic the plugin performs.
            taps.finalWet[n] = { taps.input[n].l * mixGains.dry + wl * mixGains.wet,
                                 taps.input[n].r * mixGains.dry + wr * mixGains.wet };

            taps.widthMaxAbsDiff = std::max (taps.widthMaxAbsDiff,
                                             std::max (std::abs (wl - taps.afterTrim[n].l),
                                                       std::abs (wr - taps.afterTrim[n].r)));
        }

        taps.widthBitExact = (taps.widthMaxAbsDiff == 0.0);
    }

    // --- pass 2: compensation off, so the tank is seen raw ------------------

    {
        auto tank = makeTank (sampleRate, defaults::decaySeconds, defaults::sizeControl, false);

        taps.rawTank.assign (length, StereoFrame {});

        // The ratio is recovered from the samples that carry enough level for
        // it to mean something. Near a zero crossing the quotient of two small
        // numbers is dominated by rounding, so those samples would widen the
        // estimate without informing it.
        double ratioSum = 0.0;
        size_t ratioCount = 0;

        for (size_t n = 0; n < length; ++n)
        {
            double l = taps.input[n].l;
            double r = taps.input[n].r;

            tank->processSample (l, r);
            taps.rawTank[n] = { l, r };

            // Does tap 3 really equal tap 2 scaled? If not, the two passes are
            // not the same render and nothing derived from them holds.
            taps.compensationModelError =
                std::max (taps.compensationModelError,
                          std::max (std::abs (taps.afterComp[n].l - l * taps.compensationGain),
                                    std::abs (taps.afterComp[n].r - r * taps.compensationGain)));

            if (std::abs (l) > 1.0e-6) { ratioSum += taps.afterComp[n].l / l; ++ratioCount; }
            if (std::abs (r) > 1.0e-6) { ratioSum += taps.afterComp[n].r / r; ++ratioCount; }
        }

        if (ratioCount > 0)
        {
            taps.measuredCompensationGain = ratioSum / static_cast<double> (ratioCount);
            taps.measuredCompensationDb = toDb (taps.measuredCompensationGain);
        }
    }

    return taps;
}

// ===========================================================================
//  FU2-D
// ===========================================================================

std::vector<StressResult> measureStressCases (double sampleRate)
{
    ScopedFtz ftz;

    struct Case
    {
        const char* name;
        const char* kind;
        int signalKind;          // 0 impulse, 1 noise, 2 DC, 3 program
        double decaySeconds;
        double sizeControl;
        double warmupSeconds;
        double windowSeconds;
    };

    // Warm-up matters only for sustained excitation: an impulse is measured
    // from the moment it is fed in, and discarding the start would discard the
    // peak. Sustained cases get 5 s to fill the tank before anything counts,
    // which is two decay times at the default and well past the point the tank
    // stops rising at 20 s.
    const Case cases[] =
    {
        { "program material, default",      "musical", 3, 2.5,  0.60,  0.0, 10.0 },
        { "full-scale impulse",             "musical", 0, 2.5,  0.60,  0.0, 20.0 },
        { "full-scale impulse, 60 s decay", "musical", 0, 60.0, 0.60,  0.0, 20.0 },
        { "sustained noise, default decay", "stress",  1, 2.5,  0.60,  5.0, 15.0 },
        { "sustained noise, 20 s decay",    "stress",  1, 20.0, 0.60,  5.0, 15.0 },
        { "sustained noise, smallest Size", "stress",  1, 20.0, 0.00,  5.0, 15.0 },
        { "sustained noise, largest Size",  "stress",  1, 20.0, 1.00,  5.0, 15.0 },
        { "full-scale DC",                  "stress",  2, 2.5,  0.60,  5.0, 15.0 },
    };

    const auto program = makeGainStagingSource (sampleRate);

    std::vector<StressResult> results;

    for (const auto& c : cases)
    {
        auto tank = makeTank (sampleRate, c.decaySeconds, c.sizeControl, true);

        StressResult r;
        r.name = c.name;
        r.kind = c.kind;
        r.decaySeconds = c.decaySeconds;
        r.sizeControl = c.sizeControl;
        r.warmupSeconds = c.warmupSeconds;
        r.windowSeconds = c.windowSeconds;
        r.trim = mapping::previewWetTrim (mapping::rt60For (c.decaySeconds),
                                          mapping::sizeScaleFor (c.sizeControl));

        // A fixed seed per case, so the noise is the same signal every run and
        // a change in the number means a change in the DSP.
        unsigned int seed = 24680u;

        const auto sourceAt = [&] (size_t n) -> double
        {
            switch (c.signalKind)
            {
                case 0: return n == 0 ? 1.0 : 0.0;

                case 1:
                    seed = seed * 1664525u + 1013904223u;
                    return static_cast<double> (seed >> 8) / 8388608.0 - 1.0;

                case 2: return 1.0;

                default:
                    return n < program.size() ? program[n].l : 0.0;
            }
        };

        const auto warmupFrames = static_cast<size_t> (sampleRate * c.warmupSeconds);
        const auto windowFrames = static_cast<size_t> (sampleRate * c.windowSeconds);

        for (size_t n = 0; n < warmupFrames; ++n)
        {
            double l = sourceAt (n), rr = l;
            tank->processSample (l, rr);
        }

        std::vector<StereoFrame> window (windowFrames);

        for (size_t n = 0; n < windowFrames; ++n)
        {
            const size_t index = warmupFrames + n;

            double l = sourceAt (index);
            double rr = c.signalKind == 3
                      ? (index < program.size() ? program[index].r : 0.0)
                      : l;

            tank->processSample (l, rr);

            r.finite = r.finite && std::isfinite (l) && std::isfinite (rr);
            r.subnormalFree = r.subnormalFree && ! isSubnormalValue (l) && ! isSubnormalValue (rr);

            window[n] = { l, rr };
        }

        r.rawPeak = peakOf (window);
        r.rawTruePeak = truePeakOf (window);
        r.postTrimPeak = r.rawPeak * r.trim;

        // The gain at which this case's own wet output reaches full scale.
        r.hardCeilingGain = r.rawTruePeak > 0.0 ? 1.0 / r.rawTruePeak : 0.0;

        // The same peak expressed against the base trim, so cases at different
        // decays and sizes can be compared. previewWetTrim is baseTrim times a
        // decay-aware factor, and it is that factor which is folded in here.
        r.decayFactor = mapping::previewBaseTrim > 0.0 ? r.trim / mapping::previewBaseTrim : 1.0;
        r.effectiveTruePeak = r.rawTruePeak * r.decayFactor;

        results.push_back (r);
    }

    return results;
}

std::vector<TrimCandidate> deriveTrimCandidates (const std::vector<StressResult>& cases)
{
    // The quantity a base trim is bounded by is not the raw peak. Each case
    // carries its own decay-aware factor, so what a candidate base trim T
    // produces is
    //
    //     post-trim peak = rawTruePeak * T * (caseTrim / previewBaseTrim)
    //
    // and the bound is therefore on rawTruePeak times that factor, not on
    // rawTruePeak alone. Taking the ceiling from the raw peak would set the
    // trim by whichever case happened to have the largest tank output rather
    // than by whichever case ends up closest to full scale, and at 60 s decay
    // those are not the same case.
    const auto worstEffective = [&cases] (const char* kind)
    {
        double worst = 0.0;

        for (const auto& c : cases)
            if (c.kind == std::string (kind))
                worst = std::max (worst, c.effectiveTruePeak);

        return worst;
    };

    const double worstMusical = worstEffective ("musical");
    const double worstStress  = worstEffective ("stress");

    // The ceiling that matters for a production decision is the musical one:
    // a signal a user will really send must not clip. The stress ceiling is
    // computed too, and reported, so the cost of that choice is visible rather
    // than implied.
    const double musicalCeiling = worstMusical > 0.0 ? 1.0 / worstMusical : 0.0;

    const auto atMargin = [musicalCeiling] (double marginDb)
    {
        return musicalCeiling * std::pow (10.0, -marginDb / 20.0);
    };

    struct Spec { std::string name; double trim; };

    std::vector<Spec> specs;

    // Always present, so every comparison has a fixed point.
    specs.push_back ({ "current (previewBaseTrim)", mapping::previewBaseTrim });

    specs.push_back ({ "musical ceiling, 0 dB margin", atMargin (0.0) });
    specs.push_back ({ "musical ceiling, 1 dB margin", atMargin (1.0) });
    specs.push_back ({ "musical ceiling, 3 dB margin", atMargin (3.0) });

    if (worstStress > 0.0)
        specs.push_back ({ "stress ceiling, 0 dB margin", 1.0 / worstStress });

    std::vector<TrimCandidate> candidates;

    for (const auto& s : specs)
    {
        TrimCandidate t;

        t.name = s.name;
        t.trim = s.trim;
        t.trimDb = toDb (t.trim);
        t.relativeToCurrentDb = toDb (t.trim / mapping::previewBaseTrim);

        for (const auto& c : cases)
        {
            // Applied as a ratio against the base trim, not as an absolute
            // gain: anything else would silently discard the decay
            // compensation the trim exists to provide.
            const double scaled = c.effectiveTruePeak * t.trim;

            if (c.kind == "musical")
                t.worstMusicalPeak = std::max (t.worstMusicalPeak, scaled);
            else
                t.worstStressPeak = std::max (t.worstStressPeak, scaled);
        }

        t.musicalMarginDb = t.worstMusicalPeak > 0.0 ? -toDb (t.worstMusicalPeak) : 0.0;
        t.stressMarginDb = t.worstStressPeak > 0.0 ? -toDb (t.worstStressPeak) : 0.0;

        t.clipsMusical = t.worstMusicalPeak > 1.0;
        t.clipsStress = t.worstStressPeak > 1.0;

        candidates.push_back (t);
    }

    return candidates;
}

// ===========================================================================
//  Sustained tone stress scan
// ===========================================================================

std::vector<TonePoint> measureSustainedToneScan (double sampleRate)
{
    ScopedFtz ftz;

    // Third-octave centres. Generated rather than tabulated, so the spacing is
    // exactly 2^(1/3) and no band is quietly missing.
    std::vector<double> frequencies;

    for (double f = 40.0; f <= 12500.0 * 1.0001; f *= std::pow (2.0, 1.0 / 3.0))
        frequencies.push_back (f);

    const double decays[] = { 2.5, 20.0, 60.0 };

    std::vector<TonePoint> points;

    for (double decaySeconds : decays)
    {
        // A tank at RT60 seconds fills with time constant RT60 / 6.91. Five
        // constants, from the corrected RT60 the tank is actually set to rather
        // than from the control value.
        const double warmup = std::max (5.0, 5.0 * mapping::rt60For (decaySeconds) / 6.907755);

        // Two equal blocks, not one window split in half.
        //
        // The first attempt compared the two halves of a single window and
        // required them to agree. That cannot work here. The modulation moves
        // modes past the excitation frequency and beats with it, so the
        // envelope oscillates on a timescale close to the window's, and which
        // part of the beat a half lands on depends on how long the warm-up was.
        // Lengthening the warm-up from three time constants to five to eight
        // made the disagreement worse rather than better, which is the
        // signature of sampling a cycle rather than watching a climb.
        //
        // Comparing two equal-length blocks that both follow the warm-up
        // answers the question that actually matters - has the peak stopped
        // growing - and is immune to where in the beat each block starts.
        constexpr double window = 10.0;

        for (double frequency : frequencies)
        {
            auto tank = makeTank (sampleRate, decaySeconds, 1.0, true);

            TonePoint p;
            p.frequencyHz = frequency;
            p.decaySeconds = decaySeconds;
            p.sizeControl = 1.0;
            p.warmupSeconds = warmup;
            p.windowSeconds = 2.0 * window;

            const double trim = mapping::previewWetTrim (mapping::rt60For (decaySeconds),
                                                         mapping::sizeScaleFor (1.0));

            p.decayFactor = mapping::previewBaseTrim > 0.0 ? trim / mapping::previewBaseTrim : 1.0;

            const double step = 2.0 * pi * frequency / sampleRate;

            const auto warmupFrames = static_cast<size_t> (sampleRate * warmup);
            const auto windowFrames = static_cast<size_t> (sampleRate * window);

            // Full scale, and the same phase every run: a sine is deterministic
            // by construction, so nothing here needs a seed.
            for (size_t n = 0; n < warmupFrames; ++n)
            {
                double l = std::sin (step * static_cast<double> (n));
                double r = l;
                tank->processSample (l, r);
            }

            std::vector<StereoFrame> blockA (windowFrames), blockB (windowFrames);

            for (size_t block = 0; block < 2; ++block)
            {
                auto& target = block == 0 ? blockA : blockB;

                for (size_t n = 0; n < windowFrames; ++n)
                {
                    const size_t index = warmupFrames + block * windowFrames + n;

                    double l = std::sin (step * static_cast<double> (index));
                    double r = l;

                    tank->processSample (l, r);

                    p.finite = p.finite && std::isfinite (l) && std::isfinite (r);
                    p.subnormalFree = p.subnormalFree
                                   && ! isSubnormalValue (l) && ! isSubnormalValue (r);

                    target[n] = { l, r };
                }
            }

            const double peakA = peakOf (blockA);
            const double peakB = peakOf (blockB);

            // Positive means the second block found something the first did
            // not, which is the only direction that makes a headroom figure
            // unsafe.
            p.settleDb = (peakA > 0.0 && peakB > 0.0) ? 20.0 * std::log10 (peakB / peakA) : 0.0;

            // Both blocks count towards the peak. Discarding the first would
            // throw away a measurement already paid for, and the quantity
            // wanted is the largest value the tank reaches at all.
            std::vector<StereoFrame> combined;
            combined.reserve (blockA.size() + blockB.size());
            combined.insert (combined.end(), blockA.begin(), blockA.end());
            combined.insert (combined.end(), blockB.begin(), blockB.end());

            p.rawPeak = peakOf (combined);
            p.rawTruePeak = truePeakOf (combined);
            p.effectiveTruePeak = p.rawTruePeak * p.decayFactor;
            p.peakAtUnityTrim = p.effectiveTruePeak * 1.0;

            points.push_back (p);
        }
    }

    return points;
}

// ===========================================================================
//  Blind listening set
// ===========================================================================

namespace
{
    /**
        The wet path with no trim at all: tank, compensation, width.

        The trim is left out so a candidate can be applied afterwards as a
        single multiply. Width is linear, so scaling before or after it gives
        identical results - which is what lets one render serve every candidate.
    */
    [[nodiscard]] std::vector<StereoFrame> renderWetUntrimmed (const std::vector<StereoFrame>& dry,
                                                               double sampleRate)
    {
        auto tank = makeTank (sampleRate, defaults::decaySeconds, defaults::sizeControl, true);

        const auto tail = static_cast<size_t> (sampleRate * (defaults::decaySeconds + 1.0));

        std::vector<StereoFrame> wet (dry.size() + tail);

        for (size_t n = 0; n < wet.size(); ++n)
        {
            double l = n < dry.size() ? dry[n].l : 0.0;
            double r = n < dry.size() ? dry[n].r : 0.0;

            tank->processSample (l, r);

            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * defaults::width;

            wet[n] = { mid + side, mid - side };
        }

        return wet;
    }
}

int exportTrimCandidates (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    const double rt60 = mapping::rt60For (defaults::decaySeconds);
    const double sizeScale = mapping::sizeScaleFor (defaults::sizeControl);
    const double shippingTrim = mapping::previewWetTrim (rt60, sizeScale);

    // Candidate C is derived, not typed in. The worst measured noise stress has
    // an effective true peak; the value that would put it at +9 dBFS is that
    // peak divided into 10^(9/20). Recomputing it here means a change in the
    // measurement moves the candidate rather than leaving a stale constant.
    const auto stress = measureStressCases (sampleRate);

    double worstNoiseEffective = 0.0;

    for (const auto& c : stress)
        if (c.kind == "stress" && c.name.find ("noise") != std::string::npos)
            worstNoiseEffective = std::max (worstNoiseEffective, c.effectiveTruePeak);

    const double ninedB = std::pow (10.0, 9.0 / 20.0);
    const double derivedC = worstNoiseEffective > 0.0 ? ninedB / worstNoiseEffective : 0.674;

    struct Candidate { char letter; std::string name; double baseTrim; };

    const Candidate candidates[] =
    {
        { 'A', "current", 0.25 },
        { 'B', "noise at default decay reaches full scale", 0.4050 },
        { 'C', "worst noise stress held to +9 dBFS", derivedC },
        { 'D', "no fixed attenuation", 1.0 },
    };

    constexpr int candidateCount = 4;

    const auto sources = makeFu2Sources (sampleRate);
    const double mixPoints[] = { 0.30, 1.00 };
    const char* mixNames[] = { "mix30", "mix100" };

    // A fixed permutation per (source, mix point) rather than a clock-seeded
    // one. The set is reproducible - a rerun produces the same files - and a
    // listener still cannot infer the order, because it differs between every
    // source and every mix point.
    const int order[8][candidateCount] =
    {
        { 1, 2, 0, 3 }, { 3, 0, 2, 1 },
        { 0, 3, 1, 2 }, { 2, 1, 3, 0 },
        { 2, 3, 0, 1 }, { 1, 0, 3, 2 },
        { 3, 1, 2, 0 }, { 0, 2, 1, 3 },
    };

    std::ostringstream manifest, key;

    manifest << "PHASE 5F / FU-2 - PREVIEW TRIM CANDIDATE LISTENING SET\n";
    manifest << "======================================================\n\n";
    manifest << "Decay 2.5 s, Size 60%, Mod Rate 0.5 Hz, Mod Depth 30%, Width 100%.\n";
    manifest << "48 kHz, 32-bit float. Mix law: Aurora Custom, exponent "
             << mapping::mixWetExponent << ".\n\n";
    manifest << "Every candidate for one source and one mix point shares a single wet\n";
    manifest << "render. The reverb was run once per source; the candidates differ by\n";
    manifest << "one multiplication. No normalisation, no loudness matching, no\n";
    manifest << "limiting, no saturation. The decay-aware factor, the Size\n";
    manifest << "compensation, Width and the mix law are exactly as they ship - only\n";
    manifest << "the base trim differs.\n\n";
    manifest << "shipping trim at these settings: " << shippingTrim
             << "  (base " << mapping::previewBaseTrim
             << " x decay factor " << shippingTrim / mapping::previewBaseTrim << ")\n\n";
    manifest << "candidate C is derived: 10^(9/20) / " << worstNoiseEffective
             << " = " << derivedC << "\n";
    manifest << "  where " << worstNoiseEffective << " is the worst measured noise\n";
    manifest << "  stress effective true peak.\n\n";

    key << "PHASE 5F / FU-2 - BLIND KEY\n";
    key << "===========================\n\n";
    key << "Do not read this before listening.\n\n";

    int written = 0;

    for (size_t s = 0; s < sources.size(); ++s)
    {
        const auto& src = sources[s];
        const auto wetUntrimmed = renderWetUntrimmed (src.frames, sampleRate);

        for (size_t p = 0; p < 2; ++p)
        {
            const double mix = mixPoints[p];
            const auto mixGains = mapping::mixCoefficientsFor (mix);

            const size_t slotIndex = (s * 2 + p) % 8;

            manifest << "--- " << src.name << ", Mix " << static_cast<int> (100.0 * mix) << "% ---\n";
            manifest << "  dry gain " << mixGains.dry << ", wet gain " << mixGains.wet << "\n";
            manifest << "  candidate-A .. candidate-D, in an order that differs for\n";
            manifest << "  every source and every mix point.\n";

            for (int c = 0; c < candidateCount; ++c)
            {
                const auto& cand = candidates[c];

                const double trim = shippingTrim * (cand.baseTrim / mapping::previewBaseTrim);

                std::vector<StereoFrame> wet (wetUntrimmed.size());

                for (size_t n = 0; n < wet.size(); ++n)
                    wet[n] = { wetUntrimmed[n].l * trim, wetUntrimmed[n].r * trim };

                const auto mixed = mixDryWet (src.frames, wet, MixLaw::auroraCustom, mix);
                const auto level = measureWindow ("", mixed, sampleRate);

                // Which blind letter this candidate takes here.
                int slot = 0;

                for (int i = 0; i < candidateCount; ++i)
                    if (order[slotIndex][i] == c)
                        slot = i;

                const char letters[] = { 'A', 'B', 'C', 'D' };

                const std::string blindName = src.name + "-" + mixNames[p]
                                            + "-candidate-" + letters[slot] + ".wav";

                if (! writeWavFrames (directory + "/" + blindName, mixed, sampleRate))
                    return -1;

                ++written;

                // The manifest must not say which letter carries which trim,
                // and must not carry per-letter measurements either: a LUFS
                // column is the mapping written in another alphabet. Both go
                // into the key, which is the file that is meant to be left
                // unread until afterwards.
                key << src.name << " Mix " << static_cast<int> (100.0 * mix) << "%  "
                    << "candidate-" << letters[slot] << " = base trim " << cand.baseTrim
                    << "  (" << cand.name << ")"
                    << "   LUFS " << level.loudnessLufs
                    << "   peak " << level.peak
                    << "   truepeak " << level.truePeak
                    << "   over " << level.overUnity << "\n";
            }

            manifest << "\n";
        }

        key << "\n";
    }

    {
        std::ofstream out (directory + "/LISTENING_MANIFEST.txt", std::ios::trunc);

        if (! out.is_open())
            return -1;

        out << manifest.str();
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::trunc);

        if (! out.is_open())
            return -1;

        out << key.str();
    }

    return written;
}

} // namespace avtest
