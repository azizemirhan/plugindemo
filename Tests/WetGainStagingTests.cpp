#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/ParameterMapping.h"
#include "DSP/SizeCompensation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    [[nodiscard]] std::string fixed (double value, int places)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision (places) << value;
        return out.str();
    }

    [[nodiscard]] std::string pad (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : text + std::string (width - text.size(), ' ');
    }

    [[nodiscard]] std::string padLeft (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : std::string (width - text.size(), ' ') + text;
    }

    [[nodiscard]] std::string dbText (double value, int places = 2)
    {
        if (! std::isfinite (value))
            return "  -inf";

        return fixed (value, places);
    }
}

void runWetGainStagingTests()
{
    constexpr double rate = 48000.0;

    // =======================================================================
    beginSuite ("FU-2 / window measurement self-check");
    // =======================================================================
    {
        // Before any of the numbers below mean anything, the instrument has to
        // be shown to read correctly. A signal whose peak, RMS and DC are known
        // in closed form is the only way to say that without circularity - the
        // K-weighting meter itself is already checked against the published
        // BS.1770 table in the gain staging suite.
        {
            const size_t length = static_cast<size_t> (rate * 4.0);
            std::vector<StereoFrame> sine (length);

            constexpr double amplitude = 0.5;
            constexpr double frequency = 1000.0;

            for (size_t n = 0; n < length; ++n)
            {
                const double t = static_cast<double> (n) / rate;
                const double x = amplitude * std::sin (2.0 * 3.14159265358979323846 * frequency * t);
                sine[n] = { x, x };
            }

            const auto m = measureWindow ("1 kHz sine at -6 dBFS", sine, rate);

            info ("  measured on a 1 kHz sine, amplitude 0.5, four seconds:");
            info ("    peak      " + dbText (m.peakDb) + " dB   (expected -6.02)");
            info ("    rms       " + dbText (m.rmsDb) + " dB   (expected -9.03)");
            info ("    crest     " + dbText (m.crestDb) + " dB   (expected  3.01)");
            info ("    L/R rms   " + dbText (m.rmsLeftDb) + " / " + dbText (m.rmsRightDb) + " dB");
            info ("    dc        " + fixed (m.dcLeft, 9) + " / " + fixed (m.dcRight, 9));

            checkClose (m.peakDb, -6.0206, 0.01, "peak reads the amplitude");
            checkClose (m.rmsDb, -9.0309, 0.01, "rms reads amplitude / sqrt(2)");
            checkClose (m.crestDb, 3.0103, 0.01, "crest factor of a sine is 3.01 dB");
            checkClose (m.rmsLeftDb, m.rmsRightDb, 1.0e-9, "identical channels measure identically");
            checkAtMost (std::abs (m.dcLeft), 1.0e-6, "a sine has no DC");
            checkEqual (static_cast<double> (m.overUnity), 0.0, "and nothing over unity");

            // True peak must be at least the sample peak, and for a 1 kHz sine
            // sampled at 48 kHz it should be very close to it.
            check (m.truePeak >= m.peak, "true peak is never below sample peak");
            checkAtMost (m.truePeakDb - m.peakDb, 0.1,
                         "and is within 0.1 dB of it for a well-sampled sine");
        }

        // Over-unity has to be counted, not clamped.
        {
            std::vector<StereoFrame> hot (100, StereoFrame { 0.5, 0.5 });
            hot[10] = { 1.5, 0.2 };
            hot[20] = { 0.2, -2.0 };

            const auto m = measureWindow ("over unity", hot, rate);

            checkEqual (static_cast<double> (m.overUnity), 2.0, "over-unity samples are counted");
            checkClose (m.peak, 2.0, 1.0e-12, "and the peak is reported, not clipped");
        }

        // A deliberate DC offset must show up on the channel that has it.
        {
            std::vector<StereoFrame> offset (1000, StereoFrame { 0.25, -0.125 });

            const auto m = measureWindow ("dc", offset, rate);

            checkClose (m.dcLeft, 0.25, 1.0e-12, "left DC is measured");
            checkClose (m.dcRight, -0.125, 1.0e-12, "right DC is measured, sign included");
        }
    }

    // =======================================================================
    beginSuite ("FU-2 / sources");
    // =======================================================================
    {
        const auto sources = makeFu2Sources (rate);

        checkEqual (static_cast<double> (sources.size()), 4.0, "four sources");

        info ("");
        info ("  source      seconds     peak dB      rms dB       LUFS");
        info ("  -------------------------------------------------------");

        for (const auto& s : sources)
        {
            const auto m = measureWindow (s.name, s.frames, rate);

            info ("  " + pad (s.name, 12)
                  + padLeft (fixed (m.seconds, 2), 7)
                  + padLeft (dbText (m.peakDb), 12)
                  + padLeft (dbText (m.rmsDb), 12)
                  + padLeft (dbText (m.loudnessLufs), 11));

            check (! s.frames.empty(), s.name + " is not empty");
            check (m.peak > 0.0 && m.peak <= 1.0, s.name + " is inside full scale");
            check (std::isfinite (m.loudnessLufs), s.name + " has a measurable loudness");
        }
    }

    // =======================================================================
    beginSuite ("FU-2 / wet path decomposition");
    // =======================================================================
    {
        const auto sources = makeFu2Sources (rate);

        info ("");
        info ("The chain, in the order PluginProcessor applies it. Every column is");
        info ("gated LUFS; the delta column is against the node above it.");
        info ("");

        double firstCompensationDb = 0.0;
        double firstTrimDb = 0.0;
        bool first = true;

        for (const auto& s : sources)
        {
            const auto taps = renderWetPathTaps (s.frames, rate);

            // Validity first. If tap 3 is not tap 2 scaled by the reported
            // gain, the two-pass construction is wrong and nothing below it
            // means anything - so this is checked before the numbers are read,
            // not after.
            check (taps.compensationModelError < 1.0e-12,
                   s.name + ": tap 3 is tap 2 times the reported compensation gain");

            // Three routes to the same number: the tank's accessor, the ratio
            // recovered from the two renders, and the analytic surface. The
            // first version of this read bank 0 rather than the active bank
            // and got 1.0 - a plausible-looking value that the ratio check is
            // what caught.
            checkClose (taps.measuredCompensationDb, taps.compensationDb, 1.0e-9,
                        s.name + ": the measured compensation matches the reported one");
            checkClose (taps.compensationDb, taps.analyticCompensationDb, 0.01,
                        s.name + ": and both match sizeComp::correctionDb");

            const auto mIn    = measureWindow ("input", taps.input, rate);
            const auto mRaw   = measureWindow ("raw tank", taps.rawTank, rate, &mIn);
            const auto mComp  = measureWindow ("after compensation", taps.afterComp, rate, &mIn);
            const auto mTrim  = measureWindow ("after trim", taps.afterTrim, rate, &mIn);
            const auto mWidth = measureWindow ("after width", taps.afterWidth, rate, &mIn);
            const auto mWet   = measureWindow ("final wet", taps.finalWet, rate, &mIn);

            info ("  " + s.name);
            info ("    node                     LUFS      d(node)    d(input)     peak dB   truepk dB");
            info ("    ---------------------------------------------------------------------------");

            const auto row = [&] (const std::string& label, const WindowMeasurement& m,
                                  const WindowMeasurement& previous)
            {
                const double stepDelta = std::isfinite (m.loudnessLufs)
                                       && std::isfinite (previous.loudnessLufs)
                                       ? m.loudnessLufs - previous.loudnessLufs : 0.0;

                info ("    " + pad (label, 22)
                      + padLeft (dbText (m.loudnessLufs), 9)
                      + padLeft (dbText (stepDelta), 11)
                      + padLeft (dbText (m.loudnessDeltaVsInput), 12)
                      + padLeft (dbText (m.peakDb), 12)
                      + padLeft (dbText (m.truePeakDb), 12));
            };

            info ("    " + pad ("1. input / dry", 22)
                  + padLeft (dbText (mIn.loudnessLufs), 9)
                  + padLeft ("-", 11)
                  + padLeft ("-", 12)
                  + padLeft (dbText (mIn.peakDb), 12)
                  + padLeft (dbText (mIn.truePeakDb), 12));

            row ("2. raw tank", mRaw, mIn);
            row ("3. + size comp", mComp, mRaw);
            row ("4. + preview trim", mTrim, mComp);
            row ("5. + width 100%", mWidth, mTrim);
            row ("6. + mix 100%", mWet, mWidth);

            info ("");
            info ("    size compensation: reported " + dbText (taps.compensationDb, 4)
                  + " dB, measured " + dbText (taps.measuredCompensationDb, 4)
                  + " dB, analytic " + dbText (taps.analyticCompensationDb, 4) + " dB");
            info ("    preview trim: " + dbText (taps.trimDb, 4) + " dB");
            info ("    width max |difference|: " + fixed (taps.widthMaxAbsDiff, 20)
                  + (taps.widthBitExact ? "  (bit-exact)" : "  (NOT bit-exact)"));
            info ("");

            // Width at 100% is an identity in exact arithmetic. Whether the
            // floating-point implementation preserves that is a separate
            // question and is measured rather than asserted: mid + side is
            // (l+r)*0.5 + (l-r)*0.5, and the two halves are not guaranteed to
            // reassociate to l for every pair of doubles.
            checkAtMost (taps.widthMaxAbsDiff, 1.0e-15,
                         s.name + ": width at 100% changes nothing beyond rounding");

            checkAtMost (std::abs (mWidth.loudnessLufs - mTrim.loudnessLufs), 0.001,
                         s.name + ": width at 100% costs no loudness");

            // Mix at 100% pins dry to zero and wet to one, so tap 6 must equal
            // tap 5 exactly.
            checkAtMost (std::abs (mWet.loudnessLufs - mWidth.loudnessLufs), 0.001,
                         s.name + ": mix at 100% passes the wet path unchanged");

            // The two scalars are parameter-derived, so they cannot vary
            // between sources. If they do, something is reading the signal.
            if (first)
            {
                firstCompensationDb = taps.compensationDb;
                firstTrimDb = taps.trimDb;
                first = false;
            }
            else
            {
                checkClose (taps.compensationDb, firstCompensationDb, 1.0e-12,
                            s.name + ": size compensation does not depend on the signal");
                checkClose (taps.trimDb, firstTrimDb, 1.0e-12,
                            s.name + ": the preview trim does not depend on the signal");
            }
        }

        info ("The tank's own transfer is the dry-to-raw-tank column, and it is not one");
        info ("number. The tank changes the temporal and spectral shape of what it is");
        info ("given, and gated LUFS weights both, so a sustained pad and a snare do not");
        info ("lose the same amount through the same tank. Reading a single 'tank loss'");
        info ("off these figures would be reading an average of four different things.");
    }

    // =======================================================================
    beginSuite ("FU-2 / mix points, per source");
    // =======================================================================
    {
        const auto sources = makeFu2Sources (rate);
        const double points[] = { 0.0, 0.30, 0.50, 1.00 };

        for (const auto& s : sources)
        {
            // One wet render, four mix points. This is what makes the four
            // rows comparable: identical samples, identical tank state, two
            // scalars apart.
            const auto wet = renderWetOnly (s.frames, rate);

            info ("");
            info ("  " + s.name);
            info ("    mix     LUFS   d(dry)      rms dB    peak dB  truepk dB   crest      L rms      R rms     over");
            info ("    ------------------------------------------------------------------------------------------------");

            WindowMeasurement dryReference;

            for (double m : points)
            {
                const auto mixed = mixDryWet (s.frames, wet, MixLaw::auroraCustom, m);
                const auto w = measureWindow ("", mixed, rate,
                                              m == 0.0 ? nullptr : &dryReference);

                if (m == 0.0)
                    dryReference = w;

                info ("    " + padLeft (fixed (100.0 * m, 0) + "%", 5)
                      + padLeft (dbText (w.loudnessLufs), 9)
                      + padLeft (m == 0.0 ? std::string ("-") : dbText (w.loudnessDeltaVsInput), 9)
                      + padLeft (dbText (w.rmsDb), 11)
                      + padLeft (dbText (w.peakDb), 11)
                      + padLeft (dbText (w.truePeakDb), 11)
                      + padLeft (dbText (w.crestDb), 8)
                      + padLeft (dbText (w.rmsLeftDb), 11)
                      + padLeft (dbText (w.rmsRightDb), 11)
                      + padLeft (std::to_string (w.overUnity), 9));

                check (std::isfinite (w.loudnessLufs), s.name + " at this mix has a loudness");
                checkEqual (static_cast<double> (w.overUnity), 0.0,
                            s.name + " at this mix produces nothing over unity");
                check (std::abs (w.dcLeft) < 1.0e-3 && std::abs (w.dcRight) < 1.0e-3,
                       s.name + " at this mix has no meaningful DC");
            }
        }
    }

    // =======================================================================
    beginSuite ("FU-2 / headroom and stress");
    // =======================================================================
    {
        const auto cases = measureStressCases (rate);

        info ("");
        info ("Pre-trim peaks. Warm-up is rendered and discarded; only the window is");
        info ("measured. Impulses are not warmed up, because discarding the start");
        info ("would discard the peak.");
        info ("");
        info ("  case                             kind     warm    window    raw peak   raw truepk   decay f   effective   1/truepk");
        info ("  ---------------------------------------------------------------------------------------------------------------");

        for (const auto& c : cases)
        {
            info ("  " + pad (c.name, 33)
                  + pad (c.kind, 9)
                  + padLeft (fixed (c.warmupSeconds, 1) + "s", 6)
                  + padLeft (fixed (c.windowSeconds, 1) + "s", 9)
                  + padLeft (fixed (c.rawPeak, 4), 12)
                  + padLeft (fixed (c.rawTruePeak, 4), 13)
                  + padLeft (fixed (c.decayFactor, 4), 10)
                  + padLeft (fixed (c.effectiveTruePeak, 4), 12)
                  + padLeft (fixed (c.hardCeilingGain, 4), 11));

            check (c.finite, std::string (c.name) + ": no NaN or Inf");
            check (c.subnormalFree, std::string (c.name) + ": no subnormals");
            check (c.rawTruePeak >= c.rawPeak,
                   std::string (c.name) + ": true peak is not below sample peak");
        }

        info ("");
        info ("What ships today, post-trim:");
        info ("");
        info ("  case                             post-trim peak    headroom");
        info ("  ------------------------------------------------------------");

        for (const auto& c : cases)
        {
            const double headroom = c.postTrimPeak > 0.0
                                  ? -20.0 * std::log10 (c.postTrimPeak) : 0.0;

            info ("  " + pad (c.name, 33)
                  + padLeft (fixed (c.postTrimPeak, 4), 14)
                  + padLeft (fixed (headroom, 2) + " dB", 14));

            if (c.kind == "musical")
                checkAtMost (c.postTrimPeak, 1.0,
                             std::string (c.name) + " does not clip as shipped");
        }

        const auto candidates = deriveTrimCandidates (cases);

        info ("");
        info ("Candidate base trims, derived from the measurement above rather than");
        info ("chosen. None of these is applied to anything: they are evaluated by");
        info ("scaling the measured peaks, which is exact because the trim is a gain.");
        info ("");
        info ("  candidate                          trim     trim dB   vs now    musical pk   margin    stress pk    margin");
        info ("  ------------------------------------------------------------------------------------------------------");

        for (const auto& t : candidates)
        {
            info ("  " + pad (t.name, 33)
                  + padLeft (fixed (t.trim, 4), 9)
                  + padLeft (dbText (t.trimDb), 10)
                  + padLeft (dbText (t.relativeToCurrentDb), 10)
                  + padLeft (fixed (t.worstMusicalPeak, 4), 13)
                  + padLeft (dbText (t.musicalMarginDb) + " dB", 12)
                  + padLeft (fixed (t.worstStressPeak, 4), 12)
                  + padLeft (dbText (t.stressMarginDb) + " dB", 12));
        }

        // The current value has to be in the set, or there is no fixed point to
        // compare against.
        check (! candidates.empty() && candidates.front().trim == mapping::previewBaseTrim,
               "the shipping trim is the first candidate");

        // A candidate derived at a stated margin must actually deliver it.
        for (const auto& t : candidates)
        {
            if (t.name.find ("1 dB margin") != std::string::npos)
                checkClose (t.musicalMarginDb, 1.0, 0.01,
                            "the 1 dB candidate leaves 1 dB on the musical cases");

            if (t.name.find ("3 dB margin") != std::string::npos)
                checkClose (t.musicalMarginDb, 3.0, 0.01,
                            "the 3 dB candidate leaves 3 dB on the musical cases");

            if (t.name.find ("0 dB margin") != std::string::npos
                && t.name.find ("musical") != std::string::npos)
                checkClose (t.worstMusicalPeak, 1.0, 0.001,
                            "the 0 dB candidate lands exactly on full scale");
        }
    }

    // =======================================================================
    beginSuite ("FU-2 / sustained tone stress scan");
    // =======================================================================
    {
        const auto points = measureSustainedToneScan (rate);

        info ("");
        info ("Full-scale sine at maximum Size, third-octave centres from 40 Hz to");
        info ("12.5 kHz, at three decays. Warm-up is five fill time constants of the");
        info ("corrected RT60 and is discarded. Two equal 10 s blocks are then");
        info ("measured; the peak is taken over both.");
        info ("");
        info ("The settle column is the second block against the first. Positive means");
        info ("the peak was still growing, which is the direction that would make the");
        info ("headroom figure unsafe. Negative is the modulation beating against the");
        info ("excitation and is expected.");
        info ("");

        // The number the tone scan has to be read against.
        const auto stress = measureStressCases (rate);

        double worstNoise = 0.0;
        std::string worstNoiseName;

        for (const auto& c : stress)
        {
            if (c.kind == "stress" && c.name.find ("noise") != std::string::npos
                && c.effectiveTruePeak > worstNoise)
            {
                worstNoise = c.effectiveTruePeak;
                worstNoiseName = c.name;
            }
        }

        for (double decay : { 2.5, 20.0, 60.0 })
        {
            info ("  Decay " + fixed (decay, 1) + " s, Size 100%");
            info ("     freq Hz    raw peak   raw truepk   decay f   effective   at trim 1.0    settle");
            info ("    -------------------------------------------------------------------------------");

            TonePoint worst;
            double worstEffective = 0.0;
            double worstRise = -1000.0;
            double worstSwing = 0.0;

            for (const auto& p : points)
            {
                if (p.decaySeconds != decay)
                    continue;

                info ("    " + padLeft (fixed (p.frequencyHz, 1), 9)
                      + padLeft (fixed (p.rawPeak, 4), 12)
                      + padLeft (fixed (p.rawTruePeak, 4), 13)
                      + padLeft (fixed (p.decayFactor, 4), 10)
                      + padLeft (fixed (p.effectiveTruePeak, 4), 12)
                      + padLeft (fixed (p.peakAtUnityTrim, 4), 14)
                      + padLeft (dbText (p.settleDb) + " dB", 11));

                if (p.effectiveTruePeak > worstEffective)
                {
                    worstEffective = p.effectiveTruePeak;
                    worst = p;
                }

                worstRise = std::max (worstRise, p.settleDb);
                worstSwing = std::max (worstSwing, std::abs (p.settleDb));

                check (p.finite, "tone scan: no NaN or Inf");
                check (p.subnormalFree, "tone scan: no subnormals");
            }

            // Where could an unconverged point still take the bound? Grow each
            // point by one more block at its own observed rate and see whether
            // any of them could reach the peak that sets the headroom figure.
            double worstReachable = 0.0;
            double worstReachableHz = 0.0;

            for (const auto& p : points)
            {
                if (p.decaySeconds != decay || p.frequencyHz == worst.frequencyHz)
                    continue;

                const double reachable = p.effectiveTruePeak
                                       * std::pow (10.0, std::max (0.0, p.settleDb) / 20.0);

                if (reachable > worstReachable)
                {
                    worstReachable = reachable;
                    worstReachableHz = p.frequencyHz;
                }
            }

            info ("");
            info ("    worst: " + fixed (worst.frequencyHz, 1) + " Hz, effective true peak "
                  + fixed (worstEffective, 4)
                  + ", at trim 1.0 that is " + dbText (20.0 * std::log10 (worstEffective)) + " dBFS");
            info ("    the binding point's own settle: " + dbText (worst.settleDb) + " dB");
            info ("    largest rise anywhere in this row: " + fixed (worstRise, 3)
                  + " dB, largest swing either way " + fixed (worstSwing, 3) + " dB");
            info ("    highest any other point could reach with one more block of its own");
            info ("      growth: " + fixed (worstReachable, 4) + " at "
                  + fixed (worstReachableHz, 1) + " Hz");
            info ("");

            // This criterion took four attempts, and the last three failures
            // were the criterion being wrong rather than the measurement.
            //
            //   |settle| small           - impossible: the modulation beats
            //                              against the excitation, so the
            //                              envelope never goes flat
            //   settle small, one-sided  - still failed, and got *worse* when
            //                              the warm-up was lengthened, which is
            //                              the signature of sampling a cycle
            //   every point converged    - too strict: it fails a row when a
            //                              point 20 dB below the worst wobbles
            //
            // What actually has to hold is narrower. The headroom figure comes
            // from one point, so that point must have converged; and no other
            // point may be able to overtake it. Everything else is free to
            // wobble, because nothing downstream reads it.
            checkAtMost (std::abs (worst.settleDb), 0.5,
                         "Decay " + fixed (decay, 1) + " s: the point that sets the bound has converged");

            check (worstReachable < worstEffective,
                   "Decay " + fixed (decay, 1) + " s: no other frequency can overtake it");
        }

        double worstTone = 0.0;
        TonePoint worstTonePoint;

        for (const auto& p : points)
        {
            if (p.effectiveTruePeak > worstTone)
            {
                worstTone = p.effectiveTruePeak;
                worstTonePoint = p;
            }
        }

        info ("=================================================================");
        info ("  worst sustained tone : " + fixed (worstTonePoint.frequencyHz, 1) + " Hz, Decay "
              + fixed (worstTonePoint.decaySeconds, 1) + " s, Size 100%");
        info ("                         effective true peak " + fixed (worstTone, 4)
              + "  (" + dbText (20.0 * std::log10 (worstTone)) + " dBFS at trim 1.0)");
        info ("  worst noise stress   : " + worstNoiseName);
        info ("                         effective true peak " + fixed (worstNoise, 4)
              + "  (" + dbText (20.0 * std::log10 (worstNoise)) + " dBFS at trim 1.0)");
        info ("  tone vs noise        : " + dbText (20.0 * std::log10 (worstTone / worstNoise)) + " dB");
        info ("=================================================================");

        check (worstTone > 0.0, "the tone scan produced a worst case");
        check (worstNoise > 0.0, "and so did the noise stress set");
    }

    // =======================================================================
    beginSuite ("FU-2 / nothing was changed");
    // =======================================================================
    {
        // The easiest way for a measurement to become wrong is for it to quietly
        // move what it measures. These are the values that must not drift.
        //
        // previewBaseTrim is the one value FU-2 did change, from 0.25 to 1.0,
        // and it is pinned here at its new value for the same reason it was
        // pinned at the old one.
        checkEqual (mapping::previewBaseTrim, 1.0,
                    "previewBaseTrim is 1.0 - the FU-2 production decision");
        checkEqual (mapping::previewReferenceRt60, 2.5, "the trim reference decay is untouched");
        checkEqual (mapping::previewReferenceSize, 1.0, "the trim reference size is untouched");
        checkEqual (mapping::mixWetExponent, 0.799604, "the mix law exponent is untouched");

        const auto zero = mapping::mixCoefficientsFor (0.0);
        const auto one  = mapping::mixCoefficientsFor (1.0);

        checkEqual (zero.dry, 1.0, "the mix law still passes dry exactly at 0");
        checkEqual (one.wet, 1.0, "and wet exactly at 1");

        checkEqual (defaults::mix, 0.30, "the default mix is untouched");
        checkEqual (defaults::width, 1.0, "the default width is untouched");
        checkEqual (defaults::decaySeconds, 2.5, "the default decay is untouched");
        checkEqual (defaults::sizeControl, 0.60, "the default size is untouched");
    }

    // =======================================================================
    beginSuite ("FU-2 / production headroom regression");
    // =======================================================================
    {
        // The reference values the production decision was taken on. Their job
        // is not to re-derive the decision but to notice if the ground moves
        // under it: a change anywhere in the tank, the compensation surface or
        // the decay-aware factor shows up here as a number that no longer
        // matches what was measured when the value was chosen.
        //
        // Tolerances are set for float and harness repeatability, not for
        // exactness. These renders run the tank for tens of seconds, and the
        // last bits of a peak are not reproducible across compilers; a real
        // change in the DSP moves these by far more than the band allows.
        const auto cases = measureStressCases (rate);

        double worstMusical = 0.0;
        double worstStress = 0.0;
        std::string worstMusicalName, worstStressName;

        for (const auto& c : cases)
        {
            const double postTrim = c.effectiveTruePeak * mapping::previewBaseTrim;

            if (c.kind == "musical")
            {
                if (postTrim > worstMusical) { worstMusical = postTrim; worstMusicalName = c.name; }
            }
            else if (postTrim > worstStress)
            {
                worstStress = postTrim; worstStressName = c.name;
            }

            // A hard requirement regardless of the staging decision. A case that
            // stops being finite, or runs away, is a fault and not a trade-off.
            check (c.finite, std::string (c.name) + ": finite");
            check (c.subnormalFree, std::string (c.name) + ": no subnormals");
            check (std::isfinite (c.rawTruePeak) && c.rawTruePeak < 1000.0,
                   std::string (c.name) + ": bounded, not running away");
        }

        const double musicalHeadroomDb = worstMusical > 0.0
                                       ? -20.0 * std::log10 (worstMusical) : 0.0;

        info ("");
        info ("At the production trim of " + fixed (mapping::previewBaseTrim, 4) + ":");
        info ("");
        info ("  worst musical case   " + worstMusicalName);
        info ("    peak               " + fixed (worstMusical, 4) + "   (recorded 0.2977)");
        info ("    headroom           " + dbText (musicalHeadroomDb) + " dB   (recorded 10.52)");
        info ("");
        info ("  worst stress case    " + worstStressName);
        info ("    peak               " + fixed (worstStress, 4) + "   (recorded 4.1785)");
        info ("    over unity by      " + dbText (20.0 * std::log10 (std::max (worstStress, 1.0e-12)))
              + " dB - by design, see previewBaseTrim");
        info ("");

        checkClose (worstMusical, 0.2977, 0.01,
                    "the musical worst case matches what the decision was taken on");
        checkClose (musicalHeadroomDb, 10.52, 0.30,
                    "and so does its headroom");

        // Known and accepted. Asserted as a band rather than a limit, so that
        // the over-unity behaviour stays visible in the report and a large
        // change in it still fails.
        check (worstStress > 1.0,
               "the worst stress case exceeds unity - known, accepted, and recorded");
        checkClose (worstStress, 4.1785, 0.25,
                    "and by the amount it was accepted at");

        // The musical bound is what governs. If it ever came within a few dB of
        // the stress bound the product decision would need revisiting, and this
        // says so out loud rather than leaving it to be noticed.
        check (musicalHeadroomDb > 6.0,
               "musical material still has meaningful headroom at the production trim");
    }
}

} // namespace avtest
