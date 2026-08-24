#include "GainStaging.h"

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
#include <iomanip>
#include <map>
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

    // -----------------------------------------------------------------------
    //  K-weighting
    // -----------------------------------------------------------------------
    /*
        BS.1770 tabulates its coefficients at 48 kHz only. Copying that table
        would give a loudness figure that is quietly wrong at 44.1 kHz - which
        is the rate the first FL Studio session ran at, and the rate the next
        comparison would have to be made against. So the two sections are
        designed from the filter parameters the table was itself derived from,
        and the design is checked against the published 48 kHz numbers in the
        test suite.
    */
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }

        [[nodiscard]] double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

            x2 = x1; x1 = x;
            y2 = y1; y1 = y;

            return y;
        }
    };

    /*
        The design, and why it is this one.

        The first attempt used the familiar RBJ cookbook shelf and high-pass
        with the published K-weighting parameters. It read -20.253 LUFS for a
        tone defined to read -20.000, at every sample rate - a constant error,
        which is the signature of a filter that is internally consistent and
        wrong. It was: those parameters do not belong to the RBJ formulation.
        The RBJ high-pass alone gives b = [0.995, -1.990, 0.995] where the
        standard's table says exactly [1, -2, 1].

        This is the bilinear form the parameters were derived with. At 48 kHz it
        reproduces BS.1770-4's tables to seven decimal places, and the test
        suite checks that against the published numbers rather than trusting
        this comment.
    */

    /** The high-shelf stage: the head's acoustic effect. */
    [[nodiscard]] Biquad designShelf (double sampleRate)
    {
        constexpr double gainDb = 3.999843853973347;
        constexpr double q      = 0.7071752369554196;
        constexpr double fc     = 1681.974450955533;

        const double K  = std::tan (pi * fc / sampleRate);
        const double Vh = std::pow (10.0, gainDb / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);

        const double a0 = 1.0 + K / q + K * K;

        Biquad f;
        f.b0 = (Vh + Vb * K / q + K * K) / a0;
        f.b1 = 2.0 * (K * K - Vh) / a0;
        f.b2 = (Vh - Vb * K / q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / q + K * K) / a0;

        return f;
    }

    /** The RLB high-pass stage. */
    [[nodiscard]] Biquad designHighPass (double sampleRate)
    {
        constexpr double q  = 0.5003270373238773;
        constexpr double fc = 38.13547087602444;

        const double K = std::tan (pi * fc / sampleRate);

        const double a0 = 1.0 + K / q + K * K;

        Biquad f;
        f.b0 =  1.0;
        f.b1 = -2.0;
        f.b2 =  1.0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / q + K * K) / a0;

        return f;
    }
}

KWeightingCoefficients kWeightingCoefficients (double sampleRate)
{
    const auto shelf = designShelf (sampleRate);
    const auto hp    = designHighPass (sampleRate);

    KWeightingCoefficients c;
    c.shelfB = { shelf.b0, shelf.b1, shelf.b2 };
    c.shelfA = { 1.0, shelf.a1, shelf.a2 };
    c.highPassB = { hp.b0, hp.b1, hp.b2 };
    c.highPassA = { 1.0, hp.a1, hp.a2 };

    return c;
}

double integratedLoudnessLufs (const std::vector<StereoFrame>& frames, double sampleRate)
{
    if (frames.empty() || sampleRate <= 0.0)
        return -std::numeric_limits<double>::infinity();

    auto shelfL = designShelf (sampleRate), shelfR = shelfL;
    auto hpL    = designHighPass (sampleRate), hpR = hpL;

    std::vector<double> weightedL (frames.size()), weightedR (frames.size());

    for (size_t n = 0; n < frames.size(); ++n)
    {
        weightedL[n] = hpL.process (shelfL.process (frames[n].l));
        weightedR[n] = hpR.process (shelfR.process (frames[n].r));
    }

    // 400 ms blocks, 100 ms apart. A signal shorter than one block has no
    // integrated loudness the standard would recognise, so it is reported as
    // silence rather than as a number computed a different way.
    const auto blockLength = static_cast<size_t> (0.4 * sampleRate);
    const auto step        = static_cast<size_t> (0.1 * sampleRate);

    if (blockLength == 0 || step == 0 || frames.size() < blockLength)
        return -std::numeric_limits<double>::infinity();

    struct Block { double zL = 0.0, zR = 0.0, loudness = 0.0; };

    std::vector<Block> blocks;
    blocks.reserve ((frames.size() - blockLength) / step + 1);

    for (size_t start = 0; start + blockLength <= frames.size(); start += step)
    {
        double sumL = 0.0, sumR = 0.0;

        for (size_t n = start; n < start + blockLength; ++n)
        {
            sumL += weightedL[n] * weightedL[n];
            sumR += weightedR[n] * weightedR[n];
        }

        Block b;
        b.zL = sumL / static_cast<double> (blockLength);
        b.zR = sumR / static_cast<double> (blockLength);

        const double sum = b.zL + b.zR;
        b.loudness = sum > 0.0 ? -0.691 + 10.0 * std::log10 (sum)
                               : -std::numeric_limits<double>::infinity();

        blocks.push_back (b);
    }

    // Absolute gate, then a relative gate 10 LU below what survived it. Both
    // are required: without the relative gate a long reverb tail decaying into
    // near-silence drags the figure down by several LU.
    const auto meanOf = [&blocks] (double threshold, double& zL, double& zR)
    {
        double sumL = 0.0, sumR = 0.0;
        size_t count = 0;

        for (const auto& b : blocks)
            if (b.loudness > threshold)
            {
                sumL += b.zL;
                sumR += b.zR;
                ++count;
            }

        if (count == 0)
            return false;

        zL = sumL / static_cast<double> (count);
        zR = sumR / static_cast<double> (count);
        return true;
    };

    double zL = 0.0, zR = 0.0;

    if (! meanOf (-70.0, zL, zR))
        return -std::numeric_limits<double>::infinity();

    const double ungated = zL + zR;

    if (ungated <= 0.0)
        return -std::numeric_limits<double>::infinity();

    const double relativeGate = -0.691 + 10.0 * std::log10 (ungated) - 10.0;

    if (! meanOf (std::max (-70.0, relativeGate), zL, zR))
        return -std::numeric_limits<double>::infinity();

    const double gated = zL + zR;

    return gated > 0.0 ? -0.691 + 10.0 * std::log10 (gated)
                       : -std::numeric_limits<double>::infinity();
}

LevelMeasurement measureLevel (const std::string& name,
                               const std::vector<StereoFrame>& frames,
                               double sampleRate)
{
    LevelMeasurement m;
    m.name = name;
    m.frames = frames.size();

    double peak = 0.0, sumSquares = 0.0;

    for (const auto& f : frames)
    {
        peak = std::max (peak, std::max (std::abs (f.l), std::abs (f.r)));
        sumSquares += f.l * f.l + f.r * f.r;
    }

    m.peak = peak;
    m.rms = frames.empty() ? 0.0
          : std::sqrt (sumSquares / (2.0 * static_cast<double> (frames.size())));

    const auto toDb = [] (double linear)
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear)
                            : -std::numeric_limits<double>::infinity();
    };

    m.peakDb = toDb (m.peak);
    m.rmsDb  = toDb (m.rms);
    m.loudnessLufs = integratedLoudnessLufs (frames, sampleRate);

    return m;
}

namespace
{
    // -----------------------------------------------------------------------
    //  A 32-bit float WAV writer
    // -----------------------------------------------------------------------
    void writeLittleEndian (std::ofstream& out, uint32_t value)
    {
        const unsigned char bytes[4]
        {
            static_cast<unsigned char> (value & 0xff),
            static_cast<unsigned char> ((value >> 8) & 0xff),
            static_cast<unsigned char> ((value >> 16) & 0xff),
            static_cast<unsigned char> ((value >> 24) & 0xff),
        };

        out.write (reinterpret_cast<const char*> (bytes), 4);
    }

    void writeLittleEndian (std::ofstream& out, uint16_t value)
    {
        const unsigned char bytes[2]
        {
            static_cast<unsigned char> (value & 0xff),
            static_cast<unsigned char> ((value >> 8) & 0xff),
        };

        out.write (reinterpret_cast<const char*> (bytes), 2);
    }

    bool writeWav (const std::string& path, const std::vector<StereoFrame>& frames,
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
}

// ===========================================================================
//  Source and chain
// ===========================================================================

std::vector<StereoFrame> makeGainStagingSource (double sampleRate)
{
    // Six seconds: long enough for BS.1770's relative gate to have plenty of
    // blocks to work with, short enough to render quickly.
    const auto length = static_cast<size_t> (sampleRate * 6.0);

    std::vector<StereoFrame> frames (length);

    unsigned int seed = 20260805u;

    // A sustained two-note interval under a repeating transient. The interval
    // is a fifth, which beats slowly enough to expose modal behaviour without
    // being a test tone.
    constexpr double f0 = 220.0;
    constexpr double f1 = 330.0;

    for (size_t n = 0; n < length; ++n)
    {
        const double t = static_cast<double> (n) / sampleRate;

        // Sustained part, with a slow fade in and out so the file has no edge
        // discontinuity for the loudness gate to chew on.
        const double envelope = std::min (1.0, std::min (t / 0.5, (6.0 - t) / 0.5));

        const double tone = 0.18 * envelope
                          * (std::sin (2.0 * pi * f0 * t) + 0.7 * std::sin (2.0 * pi * f1 * t));

        // A transient every 750 ms, decaying over 120 ms.
        const double sincePulse = std::fmod (t, 0.75);
        const double pulseEnvelope = std::exp (-sincePulse / 0.03);

        seed = seed * 1664525u + 1013904223u;
        const double noise = static_cast<double> (seed >> 8) / 8388608.0 - 1.0;

        const double transient = 0.35 * pulseEnvelope * noise;

        // Slightly decorrelated channels, so width and stereo behaviour are
        // exercised rather than being a mono signal in two files.
        frames[n].l = tone + transient;
        frames[n].r = tone * 0.95 + transient * 0.85
                      + 0.05 * envelope * std::sin (2.0 * pi * f1 * t + 0.7);
    }

    return frames;
}

std::vector<StereoFrame> renderPreviewChain (const std::vector<StereoFrame>& dry,
                                             double sampleRate, double wetMix)
{
    auto tank = std::make_unique<Morph>();
    tank->prepare (sampleRate);
    tank->setInputDiffusionEnabled (true);
    tank->setAllpassDepth (0.0, true);
    tank->setModulationDepth (0.0, true);
    tank->setCompensationEnabled (true, true);

    tank->setRt60Seconds (mapping::rt60For (defaults::decaySeconds), true);
    tank->setSize (mapping::sizeScaleFor (defaults::sizeControl));

    architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                  mapping::modDepthFor (defaults::modDepthControl),
                                  mapping::modRateScaleFor (defaults::modRateControlHz),
                                  true);

    // Settle the Size morph before anything is measured, so the render holds
    // the tank at its target size rather than a transition into it.
    for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
    {
        double l = 0.0, r = 0.0;
        tank->processSample (l, r);
    }

    tank->reset();

    const double trim = mapping::previewWetTrim (
        mapping::rt60For (defaults::decaySeconds),
        mapping::sizeScaleFor (defaults::sizeControl));

    const auto tail = static_cast<size_t> (sampleRate * (defaults::decaySeconds + 1.0));

    std::vector<StereoFrame> out (dry.size() + tail);

    for (size_t n = 0; n < out.size(); ++n)
    {
        const double dryL = n < dry.size() ? dry[n].l : 0.0;
        const double dryR = n < dry.size() ? dry[n].r : 0.0;

        double wetL = dryL, wetR = dryR;
        tank->processSample (wetL, wetR);

        // 1. the decay-aware preview trim
        wetL *= trim;
        wetR *= trim;

        // 2. width, on the wet signal only. At the default 100% this is an
        //    identity, and it is here so that the chain matches the plugin's
        //    rather than matching it only at one setting.
        const double mid  = (wetL + wetR) * 0.5;
        const double side = (wetL - wetR) * 0.5 * defaults::width;

        wetL = mid + side;
        wetR = mid - side;

        // 3. the crossfade. Linear, not equal-power: at 30% wet the dry signal
        //    keeps 70% of its amplitude, which is -3.1 dB on its own.
        const double dryAmount = 1.0 - wetMix;

        out[n].l = dryL * dryAmount + wetL * wetMix;
        out[n].r = dryR * dryAmount + wetR * wetMix;
    }

    return out;
}

const char* describe (MixLaw law) noexcept
{
    switch (law)
    {
        case MixLaw::linear:       return "linear";
        case MixLaw::equalPower:   return "equal-power";
        case MixLaw::auroraCustom: return "aurora-custom";
    }

    return "unknown";
}

MixGains gainsFor (MixLaw law, double mix) noexcept
{
    const double m = std::clamp (mix, 0.0, 1.0);

    switch (law)
    {
        case MixLaw::linear:
            return { 1.0 - m, m };

        case MixLaw::equalPower:
            return { std::cos (m * pi * 0.5), std::sin (m * pi * 0.5) };

        case MixLaw::auroraCustom:
        {
            // The production curve, called through the shipping function so
            // the comparison and the plugin cannot drift apart. Everything
            // about why this exponent is used lives with it, in
            // ParameterMapping.h.
            const auto g = mapping::mixCoefficientsFor (m);
            return { g.dry, g.wet };
        }
    }

    return { 1.0 - m, m };
}

std::vector<StereoFrame> renderWetOnly (const std::vector<StereoFrame>& dry, double sampleRate)
{
    auto tank = std::make_unique<Morph>();
    tank->prepare (sampleRate);
    tank->setInputDiffusionEnabled (true);
    tank->setAllpassDepth (0.0, true);
    tank->setModulationDepth (0.0, true);
    tank->setCompensationEnabled (true, true);

    tank->setRt60Seconds (mapping::rt60For (defaults::decaySeconds), true);
    tank->setSize (mapping::sizeScaleFor (defaults::sizeControl));

    architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                  mapping::modDepthFor (defaults::modDepthControl),
                                  mapping::modRateScaleFor (defaults::modRateControlHz),
                                  true);

    for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
    {
        double l = 0.0, r = 0.0;
        tank->processSample (l, r);
    }

    tank->reset();

    const double trim = mapping::previewWetTrim (
        mapping::rt60For (defaults::decaySeconds),
        mapping::sizeScaleFor (defaults::sizeControl));

    const auto tail = static_cast<size_t> (sampleRate * (defaults::decaySeconds + 1.0));

    std::vector<StereoFrame> wet (dry.size() + tail);

    for (size_t n = 0; n < wet.size(); ++n)
    {
        double l = n < dry.size() ? dry[n].l : 0.0;
        double r = n < dry.size() ? dry[n].r : 0.0;

        tank->processSample (l, r);

        l *= trim;
        r *= trim;

        // Width, on the wet only, exactly as the plugin does it. Identity at
        // the default, and here so the chain matches at every setting.
        const double mid  = (l + r) * 0.5;
        const double side = (l - r) * 0.5 * defaults::width;

        wet[n] = { mid + side, mid - side };
    }

    return wet;
}

std::vector<StereoFrame> mixDryWet (const std::vector<StereoFrame>& dry,
                                    const std::vector<StereoFrame>& wet,
                                    MixLaw law, double mix)
{
    const auto g = gainsFor (law, mix);

    std::vector<StereoFrame> out (wet.size());

    for (size_t n = 0; n < out.size(); ++n)
    {
        const double dl = n < dry.size() ? dry[n].l : 0.0;
        const double dr = n < dry.size() ? dry[n].r : 0.0;

        out[n] = { dl * g.dry + wet[n].l * g.wet,
                   dr * g.dry + wet[n].r * g.wet };
    }

    return out;
}

MixLawMeasurement measureMixLaw (const std::vector<StereoFrame>& dry,
                                 const std::vector<StereoFrame>& wet,
                                 MixLaw law, double mix,
                                 double dryLoudnessLufs, double sampleRate)
{
    const auto mixed = mixDryWet (dry, wet, law, mix);
    const auto level = measureLevel ("", mixed, sampleRate);
    const auto g = gainsFor (law, mix);

    std::vector<double> left (mixed.size()), right (mixed.size());

    for (size_t n = 0; n < mixed.size(); ++n)
    {
        left[n] = mixed[n].l;
        right[n] = mixed[n].r;
    }

    MixLawMeasurement m;
    m.law = law;
    m.mix = mix;
    m.dryGain = g.dry;
    m.wetGain = g.wet;
    m.peak = level.peak;
    m.truePeak = std::max (truePeakOfSignal (left), truePeakOfSignal (right));
    m.rms = level.rms;
    m.loudnessLufs = level.loudnessLufs;
    m.loudnessDeltaVsDry = std::isfinite (level.loudnessLufs) && std::isfinite (dryLoudnessLufs)
                         ? level.loudnessLufs - dryLoudnessLufs
                         : 0.0;

    return m;
}

GainStagingBreakdown breakdownFor (double decaySeconds, double sizeControl, double wetMix)
{
    const double rt60 = mapping::rt60For (decaySeconds);
    const double sizeScale = mapping::sizeScaleFor (sizeControl);

    const auto toDb = [] (double linear)
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear)
                            : -std::numeric_limits<double>::infinity();
    };

    GainStagingBreakdown b;

    b.previewTrimDb = toDb (mapping::previewWetTrim (rt60, sizeScale));

    // The allpass bank is in the loop at the shipping default, and it lengthens
    // the loop enough to move the correction, so the engaged figure is the one
    // that describes what a user hears.
    b.sizeCompensationDb = sizeComp::correctionDb (sizeScale, rt60, true);

    b.mixLawWetDb = toDb (wetMix);
    b.mixLawDryDb = toDb (1.0 - wetMix);

    return b;
}

int exportGainStagingRenders (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    const auto dry = makeGainStagingSource (sampleRate);

    const auto current = renderPreviewChain (dry, sampleRate, defaults::mix);
    const auto wet100  = renderPreviewChain (dry, sampleRate, 1.0);

    const double dryLufs = integratedLoudnessLufs (dry, sampleRate);
    const double currentLufs = integratedLoudnessLufs (current, sampleRate);

    auto matched = current;

    if (std::isfinite (dryLufs) && std::isfinite (currentLufs))
    {
        const double gain = std::pow (10.0, (dryLufs - currentLufs) / 20.0);

        for (auto& f : matched)
        {
            f.l *= gain;
            f.r *= gain;
        }
    }

    int written = 0;

    const struct { const char* name; const std::vector<StereoFrame>* frames; } outputs[]
    {
        { "dry-reference",   &dry },
        { "preview-current", &current },
        { "preview-matched", &matched },
        { "preview-wet-100", &wet100 },
    };

    for (const auto& output : outputs)
    {
        if (! writeWav (directory + "/" + output.name + ".wav", *output.frames, sampleRate))
            return -1;

        ++written;
    }

    return written;
}


int exportMixLawCandidates (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    struct Source { std::string name; std::vector<StereoFrame> dry; };

    // The same four the comparison suite measured, built the same way, so the
    // files a listener judges are the files the numbers describe.
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

    std::vector<Source> sources;
    sources.push_back ({ "mixed", makeGainStagingSource (sampleRate) });
    sources.push_back ({ "pad",   makeTone (sampleRate, 6.0, 220.0, 0.25) });
    sources.push_back ({ "snare", makeHits (sampleRate, 6.0, 0.55) });
    sources.push_back ({ "piano", makeChord (sampleRate, 6.0, 0.45) });

    const MixLaw laws[] = { MixLaw::linear, MixLaw::equalPower, MixLaw::auroraCustom };
    const char* lawFile[] = { "linear-30", "equalpower-30", "aurora-30" };

    // A fixed permutation per source rather than a clock-seeded one: the set is
    // reproducible, and a listener still cannot infer the order from the file
    // names because it differs between sources.
    const int order[4][3] = { { 2, 0, 1 }, { 0, 2, 1 }, { 1, 2, 0 }, { 2, 1, 0 } };

    std::ostringstream manifest;
    std::ostringstream key;

    manifest << "PHASE 5F - CANDIDATE LISTENING MANIFEST\n";
    manifest << "=======================================\n\n";
    manifest << "Mix 30%. Decay 2.5 s, Size 60%, Mod Rate 0.5 Hz, Mod Depth 30%,\n";
    manifest << "Width 100%. 48 kHz, 32-bit float.\n\n";
    manifest << "Every candidate for a source shares one dry render and one wet\n";
    manifest << "render. The reverb was run once; the three candidates differ only\n";
    manifest << "by the two crossfade coefficients. No normalisation, no loudness\n";
    manifest << "matching, no limiting, no saturation. Preview trim, Size\n";
    manifest << "compensation and Width are exactly as they ship.\n\n";
    manifest << "aurora exponent: 0.799604\n\n";

    key << "PHASE 5F - BLIND KEY\n";
    key << "====================\n\n";
    key << "Do not read this before listening.\n\n";

    int written = 0;

    for (size_t s = 0; s < sources.size(); ++s)
    {
        const auto& src = sources[s];

        const auto wet = renderWetOnly (src.dry, sampleRate);
        const double dryLufs = integratedLoudnessLufs (src.dry, sampleRate);

        // The dry reference is padded to the candidates' length so every file
        // for a source has the same sample count and starts on the same sample.
        std::vector<StereoFrame> dryPadded (wet.size());
        for (size_t n = 0; n < src.dry.size() && n < dryPadded.size(); ++n)
            dryPadded[n] = src.dry[n];

        if (! writeWav (directory + "/" + src.name + "-dry-reference.wav", dryPadded, sampleRate))
            return -1;
        ++written;

        const auto dryLevel = measureLevel ("", dryPadded, sampleRate);

        std::vector<double> dl (dryPadded.size()), dr (dryPadded.size());
        for (size_t n = 0; n < dryPadded.size(); ++n) { dl[n] = dryPadded[n].l; dr[n] = dryPadded[n].r; }

        manifest << "--- " << src.name << " ---\n";
        manifest << "  dry-reference        dry 1.0000  wet 0.0000  ratio    -    "
                 << "  LUFS " << dryLevel.loudnessLufs
                 << "  truepeak " << std::max (truePeakOfSignal (dl), truePeakOfSignal (dr)) << "\n";

        std::vector<StereoFrame> rendered[3];

        for (int i = 0; i < 3; ++i)
        {
            const auto law = laws[i];
            const auto g = gainsFor (law, defaults::mix);

            rendered[i] = mixDryWet (dryPadded, wet, law, defaults::mix);

            if (! writeWav (directory + "/" + src.name + "-" + lawFile[i] + ".wav",
                            rendered[i], sampleRate))
                return -1;
            ++written;

            const auto m = measureMixLaw (dryPadded, wet, law, defaults::mix, dryLufs, sampleRate);

            manifest << "  " << lawFile[i];
            for (size_t p = std::strlen (lawFile[i]); p < 21; ++p) manifest << ' ';
            manifest << "dry " << g.dry << "  wet " << g.wet
                     << "  ratio " << (g.dry > 0.0 ? g.wet / g.dry : 0.0)
                     << "  LUFS " << m.loudnessLufs
                     << "  truepeak " << m.truePeak << "\n";
        }

        // Blind copies, in this source's own order.
        const char* letters[] = { "A", "B", "C" };

        key << src.name << ":\n";

        for (int slot = 0; slot < 3; ++slot)
        {
            const int lawIndex = order[s % 4][slot];

            if (! writeWav (directory + "/" + src.name + "-candidate-" + letters[slot] + ".wav",
                            rendered[lawIndex], sampleRate))
                return -1;
            ++written;

            key << "  candidate-" << letters[slot] << "  =  " << describe (laws[lawIndex]) << "\n";
        }

        key << "\n";
        manifest << "\n";
    }

    {
        std::ofstream out (directory + "/LISTENING_MANIFEST.txt", std::ios::binary | std::ios::trunc);
        if (! out.is_open()) return -1;
        out << manifest.str();
    }

    {
        std::ofstream out (directory + "/BLIND_KEY.txt", std::ios::binary | std::ios::trunc);
        if (! out.is_open()) return -1;
        out << key.str();
    }

    return written;
}
} // namespace avtest
