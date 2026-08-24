#include "SignalExport.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    constexpr double pi = 3.14159265358979323846;

    using Morph = MorphingStereoFdn<N>;

    /** Stereo sample pair. */
    struct Frame { double l = 0.0, r = 0.0; };

    // -----------------------------------------------------------------------
    //  A minimal 32-bit float WAV writer
    // -----------------------------------------------------------------------
    /*
        Float rather than 16- or 24-bit PCM on purpose. A reverb tail spans a
        very wide dynamic range and the point of these files is to hear what the
        tank does, not what a quantiser does to it; float also means nothing has
        to be normalised or dithered on the way out, so the file holds exactly
        the samples the DSP produced.
    */

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

    bool writeWav (const std::string& path, const std::vector<Frame>& frames, double sampleRate)
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
            const float samples[2] { static_cast<float> (frame.l), static_cast<float> (frame.r) };

            uint32_t bits[2];
            std::memcpy (bits, samples, sizeof (samples));

            writeLittleEndian (out, bits[0]);
            writeLittleEndian (out, bits[1]);
        }

        return out.good();
    }

    // -----------------------------------------------------------------------
    //  Source material
    // -----------------------------------------------------------------------
    /*
        Synthesised rather than sampled, so the repository carries no audio and
        the renders are reproducible from source alone. Each one is a stand-in
        for the kind of material that exposes a particular reverb failure:
        transients for echo density and flutter, sustained tones for pitch and
        amplitude modulation, chords for modal beating, stereo material for
        collapse.
    */

    struct Rng
    {
        unsigned int state;

        double next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<double> (state >> 8) / 8388608.0 - 1.0;
        }
    };

    double envelope (double t, double attack, double decay) noexcept
    {
        if (t < 0.0)
            return 0.0;

        const double rise = attack > 0.0 ? 1.0 - std::exp (-t / attack) : 1.0;

        return rise * std::exp (-t / decay);
    }

    std::vector<Frame> impulseSignal (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));

        if (! out.empty())
            out[0] = { 1.0, 1.0 };

        return out;
    }

    /** Noise burst plus a pitched body: a stand-in for a snare hit. */
    std::vector<Frame> snareLike (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));
        Rng rng { 918273u };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;

            const double noise = rng.next() * envelope (t, 0.0005, 0.12);
            const double body = (std::sin (2.0 * pi * 185.0 * t) + 0.6 * std::sin (2.0 * pi * 331.0 * t))
                              * envelope (t, 0.0005, 0.06);

            const double x = 0.55 * noise + 0.35 * body;
            out[n] = { x, x };
        }

        return out;
    }

    /** Several short bursts a few milliseconds apart: a stand-in for a clap. */
    std::vector<Frame> clapLike (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));
        Rng rng { 5647382u };

        const double offsets[] { 0.0, 0.011, 0.019, 0.026, 0.048 };
        const double weights[] { 0.7, 0.9, 1.0, 0.8, 0.5 };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double noise = rng.next();

            double x = 0.0;

            for (int i = 0; i < 5; ++i)
                x += weights[i] * noise * envelope (t - offsets[i], 0.0003,
                                                    i == 4 ? 0.09 : 0.010);

            out[n] = { 0.35 * x, 0.35 * x };
        }

        return out;
    }

    /** A struck chord with inharmonic partials: exposes modal beating. */
    std::vector<Frame> pianoLike (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));

        // A minor 9th voicing, low enough to sit in the tank's modal region.
        const double notes[] { 146.83, 220.00, 261.63, 329.63, 493.88 };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;

            double x = 0.0;

            for (double f : notes)
            {
                for (int partial = 1; partial <= 6; ++partial)
                {
                    // Slight inharmonicity, as a real string has.
                    const double stretch = 1.0 + 0.0004 * partial * partial;
                    const double amplitude = 1.0 / (partial * partial);

                    x += amplitude * std::sin (2.0 * pi * f * partial * stretch * t)
                         * envelope (t, 0.002, 1.6 / std::sqrt (static_cast<double> (partial)));
                }
            }

            const double y = 0.10 * x;
            out[n] = { y, y };
        }

        return out;
    }

    /** A sustained, slightly vibrato'd voiced tone: exposes pitch modulation. */
    std::vector<Frame> vocalLike (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));

        constexpr double f0 = 196.0;

        // Three formants, roughly an "ah".
        const double formants[] { 730.0, 1090.0, 2440.0 };
        const double formantGains[] { 1.0, 0.45, 0.20 };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;

            // 5 Hz vibrato at about 25 cents, which is what a singer does and
            // is far more than the reverb adds — useful for telling the two
            // apart by ear.
            const double vibrato = 1.0 + 0.0145 * std::sin (2.0 * pi * 5.0 * t);

            double x = 0.0;

            for (int harmonic = 1; harmonic <= 40; ++harmonic)
            {
                const double f = f0 * vibrato * harmonic;

                if (f > 0.45 * rate)
                    break;

                double gain = 0.0;

                for (int i = 0; i < 3; ++i)
                {
                    const double bandwidth = 90.0 * (i + 1);
                    const double d = (f - formants[i]) / bandwidth;
                    gain += formantGains[i] / (1.0 + d * d);
                }

                x += gain * std::sin (2.0 * pi * f * t) / harmonic;
            }

            const double y = 0.09 * x * envelope (t, 0.05, 12.0)
                             * (t > seconds - 0.4 ? std::max (0.0, (seconds - t) / 0.4) : 1.0);

            out[n] = { y, y };
        }

        return out;
    }

    /** Slow, wide, detuned: the material that makes a tail's movement audible. */
    std::vector<Frame> synthPad (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));

        const double notes[] { 110.0, 164.81, 220.0, 277.18, 329.63 };
        const double detune[] { -0.06, 0.04, -0.03, 0.05, -0.02 };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;

            double left = 0.0, right = 0.0;

            for (int i = 0; i < 5; ++i)
            {
                // Sawtooth by summed harmonics, band-limited by the loop bound.
                for (int harmonic = 1; harmonic <= 12; ++harmonic)
                {
                    const double f = notes[i] * harmonic;

                    if (f > 0.45 * rate)
                        break;

                    const double a = 1.0 / harmonic;

                    left  += a * std::sin (2.0 * pi * (f + detune[i]) * t);
                    right += a * std::sin (2.0 * pi * (f - detune[i]) * t + 0.7);
                }
            }

            const double gain = 0.035 * envelope (t, 0.8, 30.0)
                                * (t > seconds - 1.0 ? std::max (0.0, (seconds - t) / 1.0) : 1.0);

            out[n] = { gain * left, gain * right };
        }

        return out;
    }

    /** Short, bright and decaying: exposes echo density in the first 100 ms. */
    std::vector<Frame> pluck (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));

        constexpr double f0 = 329.63;

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;

            double x = 0.0;

            for (int harmonic = 1; harmonic <= 24; ++harmonic)
            {
                const double f = f0 * harmonic;

                if (f > 0.45 * rate)
                    break;

                // Higher partials die first, as a plucked string's do.
                x += std::sin (2.0 * pi * f * t) / harmonic
                     * envelope (t, 0.0008, 1.2 / harmonic);
            }

            const double y = 0.16 * x;
            out[n] = { y, y };
        }

        return out;
    }

    std::vector<Frame> monoNoiseBurst (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));
        Rng rng { 24681357u };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double x = 0.35 * rng.next() * (t < 0.25 ? 1.0 : 0.0);

            out[n] = { x, x };
        }

        return out;
    }

    /** Decorrelated stereo: the material that exposes a mono collapse. */
    std::vector<Frame> stereoSource (double rate, double seconds)
    {
        std::vector<Frame> out (static_cast<size_t> (rate * seconds));
        Rng left { 111u }, right { 222u };

        for (size_t n = 0; n < out.size(); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double e = envelope (t, 0.005, 0.35);

            out[n] = { 0.30 * left.next() * e + 0.20 * std::sin (2.0 * pi * 440.0 * t) * e,
                       0.30 * right.next() * e + 0.20 * std::sin (2.0 * pi * 660.0 * t) * e };
        }

        return out;
    }

    struct Source
    {
        const char* name;
        std::vector<Frame> (*make) (double, double);
        double seconds;
    };

    const Source sources[]
    {
        { "impulse",      impulseSignal,  0.05 },
        { "snare",        snareLike,      0.60 },
        { "clap",         clapLike,       0.60 },
        { "piano-chord",  pianoLike,      3.00 },
        { "vocal",        vocalLike,      4.00 },
        { "synth-pad",    synthPad,       8.00 },
        { "pluck",        pluck,          2.00 },
        { "noise-burst",  monoNoiseBurst, 0.50 },
        { "stereo",       stereoSource,   1.00 },
    };

    struct Preset
    {
        const char* name;
        double decaySeconds;
        double sizeControl;
        double depthControl;
        double rateControlHz;
    };

    // Three points across the control space rather than one, so a listening
    // pass covers a small room, the default, and a long tail.
    const Preset presets[]
    {
        { "room",    0.9,  0.30, 0.30, 0.5 },
        { "default", 2.5,  0.60, 0.30, 0.5 },
        { "hall",    6.0,  0.85, 0.45, 0.35 },
    };
}

std::vector<StereoFrame> makeVocalReference (double sampleRate, double seconds)
{
    // The same generator the export set uses, converted at the boundary. One
    // definition, so a listening set and the export set cannot drift apart.
    const auto frames = vocalLike (sampleRate, seconds);

    std::vector<StereoFrame> out;

    out.reserve (frames.size());

    for (const auto& f : frames)
        out.push_back ({ f.l, f.r });

    return out;
}

int exportTestSignals (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    int written = 0;

    for (const auto& source : sources)
    {
        const auto dry = source.make (sampleRate, source.seconds);

        if (! writeWav (directory + "/" + source.name + "-dry.wav", dry, sampleRate))
            return -1;

        ++written;

        for (const auto& preset : presets)
        {
            auto tank = std::make_unique<Morph>();
            tank->prepare (sampleRate);
            tank->setInputDiffusionEnabled (true);
            tank->setAllpassDepth (0.0, true);
            tank->setModulationDepth (0.0, true);
            tank->setCompensationEnabled (true, true);

            tank->setRt60Seconds (mapping::rt60For (preset.decaySeconds), true);
            tank->setSize (mapping::sizeScaleFor (preset.sizeControl));

            architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                          mapping::modDepthFor (preset.depthControl),
                                          mapping::modRateScaleFor (preset.rateControlHz),
                                          true);

            // Let the Size morph settle before the source arrives, so the file
            // contains the tank at its target size and not a transition.
            for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
            {
                double l = 0.0, r = 0.0;
                tank->processSample (l, r);
            }

            tank->reset();

            const double trim = mapping::previewWetTrim (
                mapping::rt60For (preset.decaySeconds),
                mapping::sizeScaleFor (preset.sizeControl));

            // Long enough for the tail to fall well below audibility.
            const auto tail = static_cast<size_t> (sampleRate * (preset.decaySeconds + 1.0));

            std::vector<Frame> wet (dry.size() + tail);

            for (size_t n = 0; n < wet.size(); ++n)
            {
                double l = n < dry.size() ? dry[n].l : 0.0;
                double r = n < dry.size() ? dry[n].r : 0.0;

                tank->processSample (l, r);

                wet[n] = { l * trim, r * trim };
            }

            const std::string path = directory + "/" + source.name + "-wet-" + preset.name + ".wav";

            if (! writeWav (path, wet, sampleRate))
                return -1;

            ++written;
        }
    }

    return written;
}

} // namespace avtest
