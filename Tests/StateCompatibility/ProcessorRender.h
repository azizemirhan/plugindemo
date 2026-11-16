#pragma once

/*
    A deterministic render of the real, assembled processor.

    WHY THIS HEADER IS SHARED WITH A BUILD OF AN OLDER COMMIT
    ---------------------------------------------------------
    PHASE 10's legacy-neutrality gate says the plugin at Early/Late 0 must
    reproduce the PHASE 9 signal path BIT FOR BIT. There is only one honest
    way to test that: render both builds and compare.

    So this header is written to compile against a build that has never heard
    of `earlyLate`. It touches no PHASE 10 symbol, refers to parameters by
    string id, and silently ignores an id the build does not have. That is
    what makes it droppable into a worktree at the PHASE 9 commit, where it
    produces the reference hashes that the current suite then checks itself
    against.

    DETERMINISM IS THE WHOLE VALUE HERE. Nothing is seeded from the clock,
    the excitation is generated arithmetically, the parameters are set before
    prepareToPlay so the first block snaps rather than ramps, and the block
    size is fixed. Two runs of the same build produce the same bytes, or the
    comparison means nothing.
*/

#include "PluginProcessor.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace avrender
{

inline constexpr double sampleRate = 48000.0;
inline constexpr int    blockSize  = 512;

struct Setting { const char* id; float value; };

/** The excitation families the brief asks the neutrality gate to cover. */
enum class Signal
{
    impulse,        // the classic, and the one that exposes a stray tap
    sine,           // steady state, where a comb would show as a level change
    noise,          // broadband, the densest thing the tank will ever see
    transient,      // repeated attacks, which is what Attack is about
    vocalLike,      // harmonic and sustained, with vibrato
    stereoDiffer    // the two channels uncorrelated, so a stereo fault shows
};

[[nodiscard]] inline const char* nameOf (Signal s)
{
    switch (s)
    {
        case Signal::impulse:      return "impulse";
        case Signal::sine:         return "440 Hz sine";
        case Signal::noise:        return "white noise";
        case Signal::transient:    return "transient train";
        case Signal::vocalLike:    return "vocal-like tone";
        case Signal::stereoDiffer: return "uncorrelated stereo";
    }

    return "?";
}

/** Deterministic excitation. Arithmetic only - no clock, no library RNG. */
inline void fillSignal (Signal kind, std::vector<float>& l, std::vector<float>& r)
{
    const size_t n = l.size();

    unsigned int state = 123456789u;

    auto nextUniform = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        return static_cast<float> (static_cast<double> (state & 0xffffffu) / 8388607.5 - 1.0);
    };

    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;

        switch (kind)
        {
            case Signal::impulse:
                l[i] = r[i] = (i == 64 ? 1.0f : 0.0f);
                break;

            case Signal::sine:
                l[i] = r[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * 440.0 * t));
                break;

            case Signal::noise:
                l[i] = r[i] = 0.3f * nextUniform();
                break;

            case Signal::transient:
            {
                // An attack every 120 ms: a 2 ms burst with a fast decay.
                const double phase = std::fmod (t, 0.12);
                const double env   = phase < 0.002 ? std::exp (-phase * 3000.0) : 0.0;

                l[i] = r[i] = static_cast<float> (env * nextUniform());
                break;
            }

            case Signal::vocalLike:
            {
                const double vib = 1.0 + 0.004 * std::sin (2.0 * 3.14159265358979 * 5.2 * t);
                double v = 0.0;

                for (int h = 1; h <= 6; ++h)
                    v += std::sin (2.0 * 3.14159265358979 * 220.0 * h * vib * t) / h;

                l[i] = r[i] = static_cast<float> (0.2 * v);
                break;
            }

            case Signal::stereoDiffer:
                l[i] = 0.3f * nextUniform();
                r[i] = 0.3f * nextUniform();
                break;
        }
    }
}

/** Sets a parameter by id, in its own units. Unknown ids are ignored, which
    is what lets this run against a build predating the parameter. */
inline bool applySetting (av::AuroraVerbProcessor& processor, const Setting& s)
{
    for (auto* parameter : processor.getParameters())
    {
        auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter);

        if (hosted == nullptr || hosted->getParameterID() != juce::String (s.id))
            continue;

        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            ranged->setValueNotifyingHost (ranged->getNormalisableRange().convertTo0to1 (s.value));
            return true;
        }
    }

    return false;
}

/**
    Renders one configuration and returns the interleaved output.

    Parameters are applied BEFORE prepareToPlay, so the processor's first
    block snaps to its targets instead of ramping into them. A render whose
    first 20 ms is a smoother ramp would compare two ramps rather than two
    signal paths.
*/
[[nodiscard]] inline std::vector<float> render (const std::vector<Setting>& settings,
                                                Signal kind, double seconds,
                                                int blockOverride = 0)
{
    const int blockSize = blockOverride > 0 ? blockOverride : avrender::blockSize;

    av::AuroraVerbProcessor processor;

    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);

    for (const auto& s : settings)
        applySetting (processor, s);

    processor.prepareToPlay (sampleRate, blockSize);

    const auto total = static_cast<size_t> (seconds * sampleRate);

    std::vector<float> inL (total, 0.0f), inR (total, 0.0f);
    fillSignal (kind, inL, inR);

    std::vector<float> out;
    out.reserve (total * 2);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (size_t at = 0; at < total; at += blockSize)
    {
        const auto count = static_cast<int> (std::min<size_t> (blockSize, total - at));

        buffer.clear();

        for (int i = 0; i < count; ++i)
        {
            buffer.setSample (0, i, inL[at + static_cast<size_t> (i)]);
            buffer.setSample (1, i, inR[at + static_cast<size_t> (i)]);
        }

        buffer.setSize (2, count, true, false, true);
        processor.processBlock (buffer, midi);

        for (int i = 0; i < count; ++i)
        {
            out.push_back (buffer.getSample (0, i));
            out.push_back (buffer.getSample (1, i));
        }

        buffer.setSize (2, blockSize, true, false, true);
    }

    processor.releaseResources();

    return out;
}

/** FNV-1a over the raw bits. Bit-exactness is exactly what a hash proves. */
[[nodiscard]] inline std::uint64_t hashOf (const std::vector<float>& x)
{
    std::uint64_t h = 1469598103934665603ull;

    const auto* bytes = reinterpret_cast<const unsigned char*> (x.data());
    const size_t n = x.size() * sizeof (float);

    for (size_t i = 0; i < n; ++i)
    {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }

    return h;
}

[[nodiscard]] inline std::string hex (std::uint64_t v)
{
    static const char* digits = "0123456789abcdef";

    std::string s (16, '0');

    for (int i = 15; i >= 0; --i)
    {
        s[static_cast<size_t> (i)] = digits[v & 0xf];
        v >>= 4;
    }

    return "0x" + s;
}

// ---------------------------------------------------------------------------
//  The legacy-neutrality grid
// ---------------------------------------------------------------------------
/*
    Each case holds every parameter the PHASE 9 path can see, plus
    `earlyLate` at 0 - which the reference build ignores and the current build
    must treat as a bypass. If the two produce the same bytes, the early
    reflection layer is provably absent from the signal at Early/Late 0.

    The grid moves the things most likely to expose a leak: the pre-delay the
    split now sits behind, the Attack the ER branch deliberately bypasses, the
    Size that drives the ER schedule, and the Decay the trim depends on.
*/
struct LegacyCase
{
    const char* name;
    Signal signal;
    double seconds;
    std::vector<Setting> settings;
};

[[nodiscard]] inline std::vector<LegacyCase> legacyCases()
{
    return {
        { "defaults, impulse", Signal::impulse, 1.5,
          { { "earlyLate", 0.0f } } },

        { "defaults, noise", Signal::noise, 1.5,
          { { "earlyLate", 0.0f } } },

        { "defaults, sine", Signal::sine, 1.0,
          { { "earlyLate", 0.0f } } },

        { "defaults, transients", Signal::transient, 1.5,
          { { "earlyLate", 0.0f } } },

        { "defaults, vocal-like", Signal::vocalLike, 1.0,
          { { "earlyLate", 0.0f } } },

        { "uncorrelated stereo", Signal::stereoDiffer, 1.0,
          { { "earlyLate", 0.0f } } },

        { "pre-delay 80 ms", Signal::transient, 1.5,
          { { "earlyLate", 0.0f }, { "preDelay", 80.0f } } },

        { "attack 100 %", Signal::transient, 1.5,
          { { "earlyLate", 0.0f }, { "attack", 100.0f } } },

        { "size 100 %, decay 8 s", Signal::noise, 2.0,
          { { "earlyLate", 0.0f }, { "size", 100.0f }, { "decay", 8.0f } } },

        { "size 0 %, decay 0.4 s", Signal::impulse, 1.0,
          { { "earlyLate", 0.0f }, { "size", 0.0f }, { "decay", 0.4f } } },

        { "cuts and damping engaged", Signal::noise, 1.5,
          { { "earlyLate", 0.0f }, { "lowCut", 200.0f }, { "highCut", 6000.0f },
            { "lowDamp", 60.0f }, { "highDamp", 80.0f } } },

        { "mix 100 %, width 200 %", Signal::transient, 1.5,
          { { "earlyLate", 0.0f }, { "mix", 100.0f }, { "width", 200.0f } } },
    };
}

} // namespace avrender
