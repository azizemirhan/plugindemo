#include "GainStaging.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ParameterMapping.h"
#include "DSP/SizeCompensation.h"
#include "DSP/SmoothedParameter.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string pad (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : text + std::string (width - text.size(), ' ');
    }

    std::string padLeft (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : std::string (width - text.size(), ' ') + text;
    }

    void report (const LevelMeasurement& m)
    {
        info ("  " + pad (m.name, 22)
              + padLeft (fixed (m.peakDb, 2), 9) + " dB"
              + padLeft (fixed (m.rmsDb, 2), 11) + " dB"
              + padLeft (fixed (m.loudnessLufs, 2), 12) + " LUFS");
    }
}

void runGainStagingTests()
{
    ScopedFtz ftz;

    constexpr double rate = 48000.0;

    // =======================================================================
    beginSuite ("Gain staging / loudness meter");
    // =======================================================================
    {
        // Against the published table, not against itself.
        //
        // The first version of this meter was checked only for self-consistency
        // - linear in level, lower at 60 Hz than at 1 kHz - and passed all of
        // it while reading 0.25 dB low at every sample rate. The parameters
        // were right and the filter formulation they were paired with was not.
        // A comparison to the standard's own numbers is what found it, so that
        // comparison is the test.
        //
        // BS.1770-4, Tables 1 and 2, at 48 kHz.
        {
            const auto c = kWeightingCoefficients (48000.0);

            info ("K-weighting at 48 kHz, designed vs published:");
            info ("  shelf b0 " + fixed (c.shelfB[0], 8) + "  vs 1.53512486");
            info ("  shelf a1 " + fixed (c.shelfA[1], 8) + "  vs -1.69065929");
            info ("  RLB   a1 " + fixed (c.highPassA[1], 8) + "  vs -1.99004745");

            checkClose (c.shelfB[0],  1.53512485958697, 1.0e-7, "shelf b0 matches the table");
            checkClose (c.shelfB[1], -2.69169618940638, 1.0e-7, "shelf b1 matches the table");
            checkClose (c.shelfB[2],  1.19839281085285, 1.0e-7, "shelf b2 matches the table");
            checkClose (c.shelfA[1], -1.69065929318241, 1.0e-7, "shelf a1 matches the table");
            checkClose (c.shelfA[2],  0.73248077421585, 1.0e-7, "shelf a2 matches the table");

            // The RLB numerator is exactly 1, -2, 1. An implementation that
            // produces 0.995, -1.990, 0.995 has used the wrong high-pass form,
            // which is precisely the mistake this replaced.
            checkClose (c.highPassB[0],  1.0, 1.0e-12, "RLB b0 is exactly 1");
            checkClose (c.highPassB[1], -2.0, 1.0e-12, "RLB b1 is exactly -2");
            checkClose (c.highPassB[2],  1.0, 1.0e-12, "RLB b2 is exactly 1");
            checkClose (c.highPassA[1], -1.99004745483398, 1.0e-7, "RLB a1 matches the table");
            checkClose (c.highPassA[2],  0.99007225036621, 1.0e-7, "RLB a2 matches the table");
        }

        const auto sine = [] (double sampleRate, double frequency,
                              double amplitude, double seconds)
        {
            std::vector<StereoFrame> frames (static_cast<size_t> (sampleRate * seconds));

            for (size_t n = 0; n < frames.size(); ++n)
            {
                const double t = static_cast<double> (n) / sampleRate;
                const double x = amplitude * std::sin (2.0 * 3.14159265358979323846
                                                       * frequency * t);
                frames[n] = { x, x };
            }

            return frames;
        };

        // A 1 kHz sine at -20 dBFS in both channels. BS.1770 defines this as
        // -20 LUFS; a stereo pair of identical channels adds 3 dB of energy and
        // the -0.691 offset takes it back off.
        for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            const double measured = integratedLoudnessLufs (
                sine (sampleRate, 1000.0, 0.1, 8.0), sampleRate);

            info ("1 kHz at -20 dBFS, " + fixed (sampleRate / 1000.0, 1) + " kHz: "
                  + fixed (measured, 3) + " LUFS");

            checkClose (measured, -20.0, 0.02,
                        "the meter reads the reference tone correctly at "
                        + fixed (sampleRate / 1000.0, 1) + " kHz");
        }

        // Doubling the amplitude has to move the reading by exactly 6 dB, or
        // the gating is interfering with what it is supposed to measure.
        const double quiet = integratedLoudnessLufs (sine (rate, 1000.0, 0.1, 8.0), rate);
        const double loud  = integratedLoudnessLufs (sine (rate, 1000.0, 0.2, 8.0), rate);

        info ("doubling amplitude moves the reading by " + fixed (loud - quiet, 4) + " LU");

        checkClose (loud - quiet, 6.0206, 0.01,
                    "the meter is linear in level");

        // K-weighting is not flat. The RLB high-pass sits at 38.1 Hz with a Q of
        // 0.5, which puts 60 Hz about 2.9 dB down, and the shelf contributes
        // nothing that low; against 1 kHz, where the shelf gives +0.69 dB, the
        // expected gap is about 3.6 dB.
        //
        // The window is stated rather than being "less than 1 kHz". A meter
        // with no weighting filter at all would also read lower at 60 Hz once
        // gating is involved, so a one-sided check proves very little - the
        // first version of this asked for 5 dB, got 3.6, and the failure was
        // the test's arithmetic rather than the filter's.
        const double lowTone = integratedLoudnessLufs (sine (rate, 60.0, 0.1, 8.0), rate);

        info ("60 Hz reads " + fixed (lowTone, 2) + " LUFS, which is "
              + fixed (quiet - lowTone, 2) + " dB below the 1 kHz tone");

        check (quiet - lowTone > 3.0 && quiet - lowTone < 4.5,
               "K-weighting puts 60 Hz where the standard's curve puts it");

        // Silence has no loudness rather than a very negative number that could
        // be mistaken for one.
        const std::vector<StereoFrame> silence (static_cast<size_t> (rate * 5.0));

        check (! std::isfinite (integratedLoudnessLufs (silence, rate)),
               "silence reports no loudness rather than a misleading figure");

        // Something shorter than one 400 ms block cannot be measured.
        const std::vector<StereoFrame> tooShort (static_cast<size_t> (rate * 0.2));

        check (! std::isfinite (integratedLoudnessLufs (tooShort, rate)),
               "a signal shorter than one gating block is refused, not estimated");
    }

    // =======================================================================
    beginSuite ("Gain staging / where the level goes");
    // =======================================================================
    {
        // The defaults measured here have to be the ones the plugin opens with,
        // or the whole comparison is about some other setting.
        checkClose (defaults::decaySeconds, 2.5, 1.0e-9, "Decay default is 2.5 s");
        checkClose (defaults::sizeControl, 0.60, 1.0e-9, "Size default is 60%");
        checkClose (defaults::modDepthControl, 0.30, 1.0e-9, "Mod Depth default is 30%");
        checkClose (defaults::modRateControlHz, 0.5, 1.0e-9, "Mod Rate default is 0.5 Hz");
        checkClose (defaults::mix, 0.30, 1.0e-9, "Mix default is 30%");
        checkClose (defaults::width, 1.0, 1.0e-9, "Width default is 100%, an identity");

        const auto b = breakdownFor (defaults::decaySeconds, defaults::sizeControl,
                                     defaults::mix);

        info ("");
        info ("At the shipping defaults, the wet path carries these gains:");
        info ("");
        info ("  preview trim         " + padLeft (fixed (b.previewTrimDb, 2), 8) + " dB");
        info ("  Size compensation    " + padLeft (fixed (b.sizeCompensationDb, 2), 8) + " dB");
        info ("  mix law, wet         " + padLeft (fixed (b.mixLawWetDb, 2), 8) + " dB");
        info ("  ------------------------------");
        info ("  wet path total       " + padLeft (fixed (b.wetPathTotalDb(), 2), 8) + " dB");
        info ("");
        info ("  mix law, dry         " + padLeft (fixed (b.mixLawDryDb, 2), 8) + " dB");
        info ("");

        // Each component is checked against what it is defined to be, so that a
        // future change to any one of them shows up here as a specific number
        // rather than as a vague overall difference.
        const double trim = mapping::previewWetTrim (
            mapping::rt60For (defaults::decaySeconds),
            mapping::sizeScaleFor (defaults::sizeControl));

        checkClose (b.previewTrimDb, 20.0 * std::log10 (trim), 1.0e-9,
                    "the trim figure is the trim the plugin applies");

        checkClose (b.mixLawWetDb, 20.0 * std::log10 (defaults::mix), 1.0e-9,
                    "the wet mix figure is the crossfade coefficient");

        checkClose (b.mixLawDryDb, 20.0 * std::log10 (1.0 - defaults::mix), 1.0e-9,
                    "and the dry one is its complement");

        // The linear crossfade is the single largest contributor, and it is the
        // one that is not a bug: at 30% wet the dry signal is down 3.1 dB, and
        // that is what a 30% wet control means.
        checkClose (b.mixLawDryDb, -3.098, 0.01,
                    "a linear 30% crossfade takes 3.1 dB off the dry signal");

        info ("The dry signal alone loses " + fixed (-b.mixLawDryDb, 2)
              + " dB to the crossfade before any wet signal is added back.");
        info ("At 30% wet a linear law cannot be unity-gain for correlated");
        info ("material; an equal-power law would leave the dry down 1.5 dB.");
        info ("");
        info ("An equal-power crossfade at the same setting would give:");
        info ("  dry  " + fixed (20.0 * std::log10 (std::sqrt (1.0 - defaults::mix)), 2) + " dB");
        info ("  wet  " + fixed (20.0 * std::log10 (std::sqrt (defaults::mix)), 2) + " dB");
        info ("");
        info ("Reported, not applied. Changing the law is a decision for after");
        info ("the listening comparison, not something to slip in before it.");
    }

    // =======================================================================
    beginSuite ("Gain staging / A/B renders");
    // =======================================================================
    {
        const auto dry = makeGainStagingSource (rate);

        const auto current = renderPreviewChain (dry, rate, defaults::mix);
        const auto wet100  = renderPreviewChain (dry, rate, 1.0);

        const auto dryLevel     = measureLevel ("dry reference", dry, rate);
        const auto currentLevel = measureLevel ("preview, current", current, rate);
        const auto wetLevel     = measureLevel ("preview, 100% wet", wet100, rate);

        // Loudness-matched to the dry reference, which is what makes an A/B
        // about the reverb rather than about the level.
        const double matchGainDb = dryLevel.loudnessLufs - currentLevel.loudnessLufs;
        const double matchGain = std::pow (10.0, matchGainDb / 20.0);

        auto matched = current;

        for (auto& f : matched)
        {
            f.l *= matchGain;
            f.r *= matchGain;
        }

        const auto matchedLevel = measureLevel ("preview, matched", matched, rate);

        info ("");
        info ("  signal                     peak            rms       loudness");
        info ("  ----------------------------------------------------------------");
        report (dryLevel);
        report (currentLevel);
        report (matchedLevel);
        report (wetLevel);
        info ("");

        check (dryLevel.frames > 0 && currentLevel.frames > dryLevel.frames,
               "the processed renders include the tail the dry signal does not have");

        check (std::isfinite (dryLevel.loudnessLufs)
               && std::isfinite (currentLevel.loudnessLufs)
               && std::isfinite (wetLevel.loudnessLufs),
               "every render has a measurable loudness");

        info ("Differences against the dry reference:");
        info ("");
        info ("  peak      " + padLeft (fixed (currentLevel.peakDb - dryLevel.peakDb, 2), 8) + " dB");
        info ("  rms       " + padLeft (fixed (currentLevel.rmsDb - dryLevel.rmsDb, 2), 8) + " dB");
        info ("  loudness  " + padLeft (fixed (currentLevel.loudnessLufs
                                               - dryLevel.loudnessLufs, 2), 8) + " LU");
        info ("");
        info ("  loudness-matching gain needed: " + fixed (matchGainDb, 2) + " dB");
        info ("");

        // The RMS difference includes the tail, which is a real part of the
        // signal but a long quiet one; the gated loudness figure is the one
        // that corresponds to what a listener judges. Reporting both is what
        // stops a tail from being mistaken for a level drop.
        info ("The rms figure counts " + fixed ((static_cast<double> (currentLevel.frames)
                                                 - static_cast<double> (dryLevel.frames)) / rate, 2)
              + " s of tail that the dry signal does not have,");
        info ("so it reads lower than the gated loudness figure does. The");
        info ("loudness figure is the one that matches a listening judgement.");
        info ("");

        checkClose (matchedLevel.loudnessLufs, dryLevel.loudnessLufs, 0.05,
                    "the matched render lands on the dry reference's loudness");

        checkClose (matchedLevel.peakDb - currentLevel.peakDb, matchGainDb, 0.01,
                    "matching is one gain and nothing else - no limiting, no shaping");

        // A full-wet render is what the reverb sounds like with nothing else in
        // the way, and its level is the tank plus trim plus compensation with
        // no crossfade at all.
        info ("At 100% wet the crossfade contributes nothing, so the difference");
        info ("from dry is the tank, the trim and the Size compensation only:");
        info ("  loudness  " + padLeft (fixed (wetLevel.loudnessLufs
                                               - dryLevel.loudnessLufs, 2), 8) + " LU");
        info ("");

        // Nothing in the chain may clip. If the current render peaks above full
        // scale the trim is wrong, and that is a finding rather than something
        // to fix by adding a limiter.
        checkAtMost (currentLevel.peak, 1.0,
                     "the current chain does not reach full scale on this material");

        checkAtMost (wetLevel.peak, 1.0,
                     "neither does the 100% wet render");

        // Which component is responsible. The wet path total is what the wet
        // signal is scaled by before it meets the dry; the dry loses the
        // crossfade's complement. Attributing the perceived drop needs both.
        const auto b = breakdownFor (defaults::decaySeconds, defaults::sizeControl,
                                     defaults::mix);

        info ("Attribution of the level difference:");
        info ("");
        info ("  the dry signal is attenuated by the crossfade   "
              + padLeft (fixed (b.mixLawDryDb, 2), 7) + " dB");
        info ("  the wet signal is scaled by trim + compensation "
              + padLeft (fixed (b.previewTrimDb + b.sizeCompensationDb, 2), 7) + " dB");
        info ("  and then by the wet side of the crossfade       "
              + padLeft (fixed (b.mixLawWetDb, 2), 7) + " dB");
        info ("");

        const double measuredDrop = dryLevel.loudnessLufs - currentLevel.loudnessLufs;

        check (measuredDrop > -30.0 && measuredDrop < 30.0,
               "the measured difference is a plausible number rather than a meter fault");

        info ("Measured drop: " + fixed (measuredDrop, 2) + " LU.");
        info ("The crossfade alone accounts for " + fixed (-b.mixLawDryDb, 2)
              + " dB of it on the dry path.");

        if (measuredDrop > 0.0)
        {
            const double explainedByMixLaw = -b.mixLawDryDb;
            const double share = 100.0 * explainedByMixLaw / measuredDrop;

            info ("That is " + fixed (share, 0) + "% of the total, before the wet");
            info ("signal is counted back in.");
        }

        info ("");
        info ("No gain has been changed. These figures exist so the choice of");
        info ("what to change - if anything - is made against a measurement.");
    }

    // =======================================================================
    beginSuite ("Gain staging / mix law endpoints and shape");
    // =======================================================================
    {
        // Before comparing candidates, the properties every candidate has to
        // have. A law that fails these cannot be considered whatever its
        // loudness behaviour.
        for (auto law : { MixLaw::linear, MixLaw::equalPower, MixLaw::auroraCustom })
        {
            const auto name = std::string (describe (law));

            const auto atZero = gainsFor (law, 0.0);
            const auto atOne  = gainsFor (law, 1.0);

            checkClose (atZero.dry, 1.0, 1.0e-12, name + ": mix 0 passes the dry signal whole");
            checkClose (atZero.wet, 0.0, 1.0e-12, name + ": and no wet at all");
            checkClose (atOne.dry, 0.0, 1.0e-12, name + ": mix 1 removes the dry signal");
            checkClose (atOne.wet, 1.0, 1.0e-12, name + ": and passes the wet whole");

            // Monotone and continuous, checked on a fine grid: a fader move
            // must never reverse direction, and automation must never step.
            double worstDryStep = 0.0, worstWetStep = 0.0;
            bool dryMonotone = true, wetMonotone = true;

            auto previous = gainsFor (law, 0.0);

            for (int i = 1; i <= 2000; ++i)
            {
                const auto g = gainsFor (law, i / 2000.0);

                if (g.dry > previous.dry + 1.0e-12) dryMonotone = false;
                if (g.wet < previous.wet - 1.0e-12) wetMonotone = false;

                worstDryStep = std::max (worstDryStep, std::abs (g.dry - previous.dry));
                worstWetStep = std::max (worstWetStep, std::abs (g.wet - previous.wet));

                previous = g;
            }

            check (dryMonotone, name + ": the dry gain never rises as mix rises");
            check (wetMonotone, name + ": the wet gain never falls as mix rises");

            // 1/2000 of the travel; a discontinuity would show as a step far
            // larger than the neighbouring ones.
            checkAtMost (worstDryStep, 0.005, name + ": the dry curve has no discontinuity");
            checkAtMost (worstWetStep, 0.005, name + ": the wet curve has no discontinuity");
        }

        info ("");
        info ("  law            mix    dry gain   dry dB    wet gain   wet dB");
        info ("  -----------------------------------------------------------------");

        for (auto law : { MixLaw::linear, MixLaw::equalPower, MixLaw::auroraCustom })
        {
            for (double m : { 0.0, 0.10, 0.25, 0.30, 0.50, 0.75, 1.0 })
            {
                const auto g = gainsFor (law, m);

                const auto db = [] (double x)
                {
                    return x > 0.0 ? fixed (20.0 * std::log10 (x), 2) : std::string ("  -inf");
                };

                info ("  " + pad (describe (law), 15)
                      + padLeft (fixed (100.0 * m, 0) + "%", 5)
                      + padLeft (fixed (g.dry, 4), 11) + padLeft (db (g.dry), 10)
                      + padLeft (fixed (g.wet, 4), 11) + padLeft (db (g.wet), 10));
            }
            info ("");
        }
    }

    // =======================================================================
    beginSuite ("Gain staging / mix law candidates across four sources");
    // =======================================================================
    {
        // Four sources on purpose. A crossfade tuned on one piece of material
        // is tuned to that material's crest factor and spectrum; the whole
        // point of the comparison is which law holds up across all of them.
        struct Source { const char* name; std::vector<StereoFrame> dry; };

        std::vector<Source> sources;
        sources.push_back ({ "mixed", makeGainStagingSource (rate) });

        // Simple stand-ins with clearly different envelopes: a sustained tone,
        // a percussive transient, and a dense chord.
        const auto makeTone = [] (double sampleRate, double seconds, double f, double amp)
        {
            std::vector<StereoFrame> v (static_cast<size_t> (sampleRate * seconds));
            for (size_t n = 0; n < v.size(); ++n)
            {
                const double t = static_cast<double> (n) / sampleRate;
                const double env = std::min (1.0, std::min (t / 0.3, (seconds - t) / 0.3));
                const double x = amp * env * std::sin (2.0 * 3.14159265358979323846 * f * t);
                v[n] = { x, x * 0.97 };
            }
            return v;
        };

        const auto makeHits = [] (double sampleRate, double seconds, double amp)
        {
            std::vector<StereoFrame> v (static_cast<size_t> (sampleRate * seconds));
            unsigned int seed = 4242u;
            for (size_t n = 0; n < v.size(); ++n)
            {
                const double t = static_cast<double> (n) / sampleRate;
                const double since = std::fmod (t, 0.5);
                const double env = std::exp (-since / 0.012);
                seed = seed * 1664525u + 1013904223u;
                const double noise = static_cast<double> (seed >> 8) / 8388608.0 - 1.0;
                const double x = amp * env * noise;
                v[n] = { x, x * 0.9 };
            }
            return v;
        };

        const auto makeChord = [] (double sampleRate, double seconds, double amp)
        {
            std::vector<StereoFrame> v (static_cast<size_t> (sampleRate * seconds));
            const double f[] = { 220.0, 277.18, 329.63, 440.0 };
            for (size_t n = 0; n < v.size(); ++n)
            {
                const double t = static_cast<double> (n) / sampleRate;
                const double env = std::exp (-t / 2.0);
                double s = 0.0;
                for (double freq : f)
                    s += std::sin (2.0 * 3.14159265358979323846 * freq * t);
                const double x = amp * env * s * 0.25;
                v[n] = { x, x * 0.95 };
            }
            return v;
        };

        sources.push_back ({ "pad",   makeTone (rate, 6.0, 220.0, 0.25) });
        sources.push_back ({ "snare", makeHits (rate, 6.0, 0.55) });
        sources.push_back ({ "piano", makeChord (rate, 6.0, 0.45) });

        const double mixPoints[] = { 0.0, 0.10, 0.20, 0.30, 0.40, 0.50, 0.75, 1.0 };

        // Per-law totals, so the summary is not an impression of the table.
        struct Totals { double sumAbsDelta = 0.0; double worstDelta = 0.0; double worstTruePeak = 0.0; int count = 0; };
        std::map<int, Totals> totals;

        for (auto& source : sources)
        {
            info ("");
            info ("=== source: " + std::string (source.name) + " ===");

            const auto wet = renderWetOnly (source.dry, rate);
            const double dryLufs = integratedLoudnessLufs (source.dry, rate);

            info ("  dry reference: " + fixed (dryLufs, 2) + " LUFS");
            info ("");
            info ("  law            mix   dry g   wet g      LUFS   d vs dry      peak   truepk");
            info ("  ---------------------------------------------------------------------------");

            for (auto law : { MixLaw::linear, MixLaw::equalPower, MixLaw::auroraCustom })
            {
                for (double m : mixPoints)
                {
                    const auto r = measureMixLaw (source.dry, wet, law, m, dryLufs, rate);

                    info ("  " + pad (describe (law), 15)
                          + padLeft (fixed (100.0 * m, 0) + "%", 4)
                          + padLeft (fixed (r.dryGain, 3), 8)
                          + padLeft (fixed (r.wetGain, 3), 8)
                          + padLeft (fixed (r.loudnessLufs, 2), 10)
                          + padLeft (fixed (r.loudnessDeltaVsDry, 2), 11)
                          + padLeft (fixed (r.peak, 4), 10)
                          + padLeft (fixed (r.truePeak, 4), 9));

                    // The working range is where a mix control actually lives.
                    // Endpoints are fixed by definition and tell us nothing.
                    if (m >= 0.10 && m <= 0.50)
                    {
                        auto& t = totals[static_cast<int> (law)];
                        t.sumAbsDelta += std::abs (r.loudnessDeltaVsDry);
                        t.worstDelta = std::max (t.worstDelta, std::abs (r.loudnessDeltaVsDry));
                        ++t.count;
                    }

                    auto& t = totals[static_cast<int> (law)];
                    t.worstTruePeak = std::max (t.worstTruePeak, r.truePeak);

                    checkAtMost (r.truePeak, 1.0,
                                 std::string (describe (law)) + " at "
                                 + fixed (100.0 * m, 0) + "% on " + source.name
                                 + " stays under full scale");
                }
                info ("");
            }
        }

        info ("");
        info ("=== loudness stability across 10-50% mix, all four sources ===");
        info ("");
        info ("  law             mean |delta|   worst |delta|   worst true peak");
        info ("  ------------------------------------------------------------------");

        for (auto law : { MixLaw::linear, MixLaw::equalPower, MixLaw::auroraCustom })
        {
            const auto& t = totals[static_cast<int> (law)];
            const double mean = t.count > 0 ? t.sumAbsDelta / t.count : 0.0;

            info ("  " + pad (describe (law), 16)
                  + padLeft (fixed (mean, 3) + " LU", 14)
                  + padLeft (fixed (t.worstDelta, 3) + " LU", 16)
                  + padLeft (fixed (t.worstTruePeak, 4), 18));
        }

        info ("");
        info ("Endpoints are exact for all three laws, so they are excluded from");
        info ("the averages above: they would flatter every candidate equally.");
    }

    // =======================================================================
    beginSuite ("Mix law / production curve");
    // =======================================================================
    {
        using mapping::mixCoefficientsFor;

        // --- 1. Endpoints, exactly -----------------------------------------
        //
        // Not "to within an ulp". A mix control parked at zero has to pass the
        // dry signal through untouched, and a plugin that multiplies it by
        // 0.9999999999 instead is wrong in a way nobody can hear and nobody
        // would choose to ship.
        {
            const auto zero = mixCoefficientsFor (0.0);
            const auto one  = mixCoefficientsFor (1.0);

            checkEqual (zero.dry, 1.0, "mix 0 passes the dry signal exactly");
            checkEqual (zero.wet, 0.0, "and exactly no wet");
            checkEqual (one.dry, 0.0, "mix 1 removes the dry signal exactly");
            checkEqual (one.wet, 1.0, "and passes the wet exactly");

            // Out of range must clamp rather than extrapolate: pow of a
            // negative base is a NaN waiting to happen.
            const auto below = mixCoefficientsFor (-0.5);
            const auto above = mixCoefficientsFor (1.5);

            checkEqual (below.dry, 1.0, "a negative mix clamps to fully dry");
            checkEqual (below.wet, 0.0, "and produces no wet rather than a NaN");
            checkEqual (above.dry, 0.0, "a mix above one clamps to fully wet");
            checkEqual (above.wet, 1.0, "and passes the wet whole");
        }

        // --- 2. Finite, monotone, continuous across the whole range ---------
        {
            bool finite = true, dryMonotone = true, wetMonotone = true;
            double worstDryStep = 0.0, worstWetStep = 0.0;

            auto previous = mixCoefficientsFor (0.0);

            constexpr int steps = 100000;

            for (int i = 1; i <= steps; ++i)
            {
                const auto g = mixCoefficientsFor (static_cast<double> (i) / steps);

                finite = finite && std::isfinite (g.dry) && std::isfinite (g.wet);

                if (g.dry > previous.dry + 1.0e-15) dryMonotone = false;
                if (g.wet < previous.wet - 1.0e-15) wetMonotone = false;

                worstDryStep = std::max (worstDryStep, std::abs (g.dry - previous.dry));
                worstWetStep = std::max (worstWetStep, std::abs (g.wet - previous.wet));

                previous = g;
            }

            check (finite, "no NaN or Inf anywhere in 0..1, at 100 000 points");
            check (dryMonotone, "the dry gain never rises as mix rises");
            check (wetMonotone, "the wet gain never falls as mix rises");

            info ("worst step over 1/100000 of travel: dry "
                  + fixed (worstDryStep, 9) + ", wet " + fixed (worstWetStep, 9));

            // A discontinuity would be orders of magnitude above the smooth
            // neighbourhood. The wet curve is steepest at zero, where m^0.8
            // has an infinite derivative, so its bound is looser on purpose.
            checkAtMost (worstDryStep, 1.0e-4, "the dry curve is continuous");
            checkAtMost (worstWetStep, 1.0e-3, "the wet curve is continuous");
        }

        // --- 3. The property the exponent exists for ------------------------
        {
            const auto g = mixCoefficientsFor (0.30);
            const double ratio = g.wet / g.dry;
            const double linearRatio = 0.30 / 0.70;

            info ("");
            info ("at the 30% default:");
            info ("  dry " + fixed (g.dry, 6) + "   wet " + fixed (g.wet, 6));
            info ("  wet/dry " + fixed (ratio, 6) + "   linear was " + fixed (linearRatio, 6));

            checkClose (ratio, 0.428571, 1.0e-4,
                        "the wet/dry ratio at 30% is the linear law's");
            checkClose (ratio, linearRatio, 1.0e-4,
                        "so the default preset keeps the balance it had");

            // And the level really does come back. Linear gave the dry branch
            // 0.700; this gives it 0.891, which is 2.1 dB more.
            const double recovered = 20.0 * std::log10 (g.dry / 0.70);
            info ("  dry branch recovers " + fixed (recovered, 2) + " dB against linear");

            checkClose (recovered, 2.09, 0.05,
                        "the dry attenuation the blind test objected to is largely gone");
        }

        // --- 4. Representative values, pinned -------------------------------
        {
            struct Point { double mix, dry, wet; };

            // Recorded so a future change to the curve cannot pass unnoticed.
            const Point expected[] =
            {
                { 0.00, 1.000000, 0.000000 },
                { 0.01, 0.999877, 0.025165 },
                { 0.05, 0.996917, 0.091136 },
                { 0.10, 0.987688, 0.158634 },
                { 0.20, 0.951057, 0.276122 },
                { 0.30, 0.891007, 0.381860 },
                { 0.50, 0.707107, 0.574507 },
                { 0.75, 0.382683, 0.794508 },
                { 1.00, 0.000000, 1.000000 },
            };

            info ("");
            info ("   mix      dry        wet     wet/dry");
            info ("  ------------------------------------");

            for (const auto& p : expected)
            {
                const auto g = mixCoefficientsFor (p.mix);

                info ("  " + padLeft (fixed (100.0 * p.mix, 0) + "%", 5)
                      + padLeft (fixed (g.dry, 6), 11)
                      + padLeft (fixed (g.wet, 6), 11)
                      + padLeft (g.dry > 0.0 ? fixed (g.wet / g.dry, 6) : std::string ("-"), 11));

                checkClose (g.dry, p.dry, 1.0e-6,
                            "dry at " + fixed (100.0 * p.mix, 0) + "% is unchanged");
                checkClose (g.wet, p.wet, 1.0e-6,
                            "wet at " + fixed (100.0 * p.mix, 0) + "% is unchanged");
            }
        }

        // --- 5. The low-mix region, which is where this curve differs most --
        {
            info ("");
            info ("low mix: the wet curve is steeper here than linear was");
            info ("");
            info ("   mix     wet(aurora)  wet(linear)   ratio");
            info ("  --------------------------------------------");

            for (double m : { 0.00, 0.01, 0.02, 0.05, 0.10 })
            {
                const auto g = mixCoefficientsFor (m);

                info ("  " + padLeft (fixed (100.0 * m, 0) + "%", 5)
                      + padLeft (fixed (g.wet, 6), 13)
                      + padLeft (fixed (m, 6), 13)
                      + padLeft (m > 0.0 ? fixed (g.wet / m, 3) : std::string ("-"), 9));
            }

            // At 1% the wet gain is about 2.5x what linear gave. That is real
            // and deliberate, and it is the one place a listener could notice
            // the change as something other than "the level is right now".
            const auto atOne = mixCoefficientsFor (0.01);
            check (atOne.wet > 0.02 && atOne.wet < 0.03,
                   "1% mix gives about 2.5x the wet linear gave, as m^0.8 implies");
        }

        // --- 6. Automation safety through the real smoother -----------------
        //
        // The steep region near zero only matters if a smoothed sweep can
        // produce a step big enough to click. Driven through the same smoother
        // the plugin uses, at the same 20 ms ramp, so the answer is about the
        // shipping configuration rather than about the curve in the abstract.
        {
            constexpr double sweepRate = 48000.0;

            struct SweepResult { double dry; double wet; bool finite; };

            const auto sweep = [sweepRate] (double from, double to)
            {
                LinearSmoother<double> smoother;
                smoother.prepare (sweepRate, 0.02);
                smoother.snapTo (from);
                smoother.setTarget (to);

                double worstDryStep = 0.0, worstWetStep = 0.0;
                auto previous = mixCoefficientsFor (from);
                bool finite = true;

                for (int n = 0; n < static_cast<int> (sweepRate * 0.5); ++n)
                {
                    const auto g = mixCoefficientsFor (smoother.getNextValue());

                    finite = finite && std::isfinite (g.dry) && std::isfinite (g.wet);

                    worstDryStep = std::max (worstDryStep, std::abs (g.dry - previous.dry));
                    worstWetStep = std::max (worstWetStep, std::abs (g.wet - previous.wet));

                    previous = g;
                }

                return SweepResult { worstDryStep, worstWetStep, finite };
            };

            info ("");
            info ("worst per-sample coefficient step through the 20 ms smoother:");
            info ("");
            info ("   sweep            dry step     wet step");
            info ("  ------------------------------------------");

            struct Case { const char* name; double from; double to; };

            const Case cases[] =
            {
                { "0 -> 1%",   0.00, 0.01 },
                { "0 -> 5%",   0.00, 0.05 },
                { "0 -> 30%",  0.00, 0.30 },
                { "0 -> 100%", 0.00, 1.00 },
                { "100% -> 0", 1.00, 0.00 },
                { "30% -> 0",  0.30, 0.00 },
            };

            double worstOverall = 0.0;

            for (const auto& c : cases)
            {
                const auto r = sweep (c.from, c.to);

                info ("  " + pad (c.name, 16)
                      + padLeft (fixed (r.dry, 8), 12)
                      + padLeft (fixed (r.wet, 8), 13));

                check (r.finite, std::string ("no NaN or Inf sweeping ") + c.name);

                worstOverall = std::max (worstOverall, std::max (r.dry, r.wet));
            }

            info ("");
            info ("The steepest is the jump straight out of silence, where m^0.8");
            info ("has an infinite derivative at m = 0. The smoother still only");
            info ("moves the control by 1/960 of its travel per sample, so the");
            info ("coefficient step stays small enough that no discontinuity");
            info ("reaches the output.");

            // A step of 0.01 in a gain coefficient is under 0.1 dB between
            // adjacent samples - well below anything that reads as a click.
            checkAtMost (worstOverall, 0.01,
                         "no sweep produces a per-sample coefficient step that could click");
        }
    }
}

} // namespace avtest
