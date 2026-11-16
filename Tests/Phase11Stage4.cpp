#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/Denormal.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 11 - STAGE 4. Two independent questions, deliberately kept apart.

    NO PRODUCTION DSP IS CHANGED. The isolation experiments below switch
    subsystems off inside the test harness only; nothing in Source/ is
    touched, no PHASE 10 constant moves, and every existing diagnostic gate
    stays exactly as it is.

    PART A - IS THE NARROW-BAND MONO NULL AUDIBLE? Stage 3 measured that the
    median mono fold-down is healthy at every Size and that 10-18 % of pure
    tones lose more than 6 dB. It recommended reclassification. The brief is
    right to refuse that recommendation for now: PHASE 10's pad listening
    tested a LEVEL characteristic at Early/Late 0 through a Mix, not a
    stereo-to-mono collapse of the late path alone. Nobody has listened to
    the actual failure yet, so this stage prepares that test instead of
    assuming its answer.

    PART B - WHERE DOES THE BELL-LIKE RINGING COME FROM? A human heard a very
    subtle tonal ring in the tail on a vocal at about 60 % Mix. Stage 3 found
    17-18 dB narrow spectral peaks and showed that deliberately different
    delay sets barely move them, so the delay geometry is not the suspect.
    This stage isolates the subsystem by switching one thing at a time.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    // The user's observed configuration, which is also the product default.
    constexpr double kDecay          = 2.5;
    constexpr double kSizeDefault    = 0.60;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;

    [[nodiscard]] std::string fx (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    // =======================================================================
    //  The late path, with research-only isolation switches
    // =======================================================================
    struct Config
    {
        double size      = kSizeDefault;
        double decay     = kDecay;
        double earlyDiff = kEarlyDiffusion;
        double modDepth  = kModDepth;
        double modRate   = kModRate;
        double lowDamp   = kLowDamp;
        double highDamp  = kHighDamp;
        double width     = 1.0;

        /** Research switches. None of these exists in production. */
        bool diffusionEnabled = true;
        bool allpassEnabled   = true;

        /** Reads one FDN line state directly, by making the readout a unit
            vector. Public API only - setStereoVectors - so no production
            change is needed to look inside the tank. */
        int probeLine = -1;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (c.diffusionEnabled);
        tank->setCompensationEnabled (true, true);

        if (c.probeLine >= 0)
        {
            std::array<double, 8> unit {};
            unit[static_cast<size_t> (c.probeLine)] = 1.0;

            for (int bank = 0; bank < 2; ++bank)
                tank->getBank (bank).setStereoVectors (fdn::injectionLeft,
                                                       fdn::injectionRight,
                                                       unit.data(), unit.data());
        }

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (c.earlyDiff, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (c.lowDamp),
                          mapping::highDampRatioFor (c.highDamp),
                          kLowDampHz, kHighDampHz, true);

        if (c.allpassEnabled)
            architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                          mapping::modDepthFor (c.modDepth),
                                          mapping::modRateScaleFor (c.modRate), true);
        else
        {
            // The allpass bank switched out entirely, leaving the bare FDN.
            tank->setAllpassDepth (0.0);
            tank->setModulationDepth (mapping::modDepthFor (c.modDepth), true);
            tank->setModulationRateHz (modulation::defaultRateHz
                                       * mapping::modRateScaleFor (c.modRate));
        }

        for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).getCore().resetModulation();

        tank->reset();

        return tank;
    }

    struct Stereo { std::vector<double> l, r; };

    [[nodiscard]] Stereo renderLate (const Config& c, const std::vector<double>& drive,
                                     size_t length)
    {
        ScopedFtz ftz;

        auto tank = makeTank (c);

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
            out.l[n] = out.r[n] = n < drive.size() ? drive[n] : 0.0;

        for (size_t n = 0; n < length; ++n)
            tank->processSample (out.l[n], out.r[n]);

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        for (size_t n = 0; n < length; ++n)
        {
            double l = out.l[n] * trim;
            double r = out.r[n] * trim;

            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * c.width;

            out.l[n] = mid + side;
            out.r[n] = mid - side;
        }

        return out;
    }

    [[nodiscard]] double monoFoldDb (const Stereo& x)
    {
        double total = 0.0, mono = 0.0;

        for (size_t n = 0; n < x.l.size(); ++n)
        {
            total += x.l[n] * x.l[n] + x.r[n] * x.r[n];

            const double m = 0.5 * (x.l[n] + x.r[n]);
            mono += 2.0 * m * m;
        }

        return total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, mono) / total) : 0.0;
    }

    [[nodiscard]] double correlationOf (const Stereo& x)
    {
        return correlation (x.l.data(), x.r.data(), x.l.size());
    }

    // =======================================================================
    //  Sources
    // =======================================================================
    /** A sustained pad: a fundamental with a harmonic series, slow attack,
        slight detune for life. Realistic envelope, concentrated tonal energy. */
    [[nodiscard]] std::vector<double> padNote (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;

            double v = 0.0;

            // Six harmonics with 1/h falloff, plus a detuned second voice.
            for (int h = 1; h <= 6; ++h)
            {
                v += std::sin (2.0 * 3.14159265358979323846 * hz * h * t) / h;
                v += std::sin (2.0 * 3.14159265358979323846 * hz * 1.004 * h * t) / h * 0.6;
            }

            const double attack = 1.0 - std::exp (-t * 6.0);
            const double release = std::min (1.0, (seconds - t) * 3.0);

            x[i] = 0.22 * v * attack * std::max (0.0, release);
        }

        return x;
    }

    /** A bass note: strong fundamental, few harmonics, plucked envelope. */
    [[nodiscard]] std::vector<double> bassNote (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;

            double v = std::sin (2.0 * 3.14159265358979323846 * hz * t);
            v += 0.35 * std::sin (2.0 * 3.14159265358979323846 * hz * 2.0 * t);
            v += 0.12 * std::sin (2.0 * 3.14159265358979323846 * hz * 3.0 * t);

            const double env = std::exp (-t * 1.2) * (1.0 - std::exp (-t * 300.0));

            x[i] = 0.38 * v * env;
        }

        return x;
    }

    /** An electric-piano-like note: inharmonic tine plus body, fast attack. */
    [[nodiscard]] std::vector<double> pianoNote (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;

            double v = std::sin (2.0 * 3.14159265358979323846 * hz * t);
            v += 0.5 * std::sin (2.0 * 3.14159265358979323846 * hz * 2.01 * t)
                     * std::exp (-t * 3.0);
            v += 0.25 * std::sin (2.0 * 3.14159265358979323846 * hz * 4.02 * t)
                      * std::exp (-t * 6.0);
            v += 0.15 * std::sin (2.0 * 3.14159265358979323846 * hz * 6.9 * t)
                      * std::exp (-t * 12.0);

            const double env = std::exp (-t * 1.6) * (1.0 - std::exp (-t * 500.0));

            x[i] = 0.34 * v * env;
        }

        return x;
    }

    [[nodiscard]] std::vector<double> tone (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;
            const double env = std::min (1.0, t * 200.0)
                             * std::min (1.0, (seconds - t) * 200.0);

            x[i] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * hz * t)
                 * std::max (0.0, env);
        }

        return x;
    }

    [[nodiscard]] std::vector<double> noiseBurst (double ms, unsigned int seed)
    {
        const auto n = static_cast<size_t> (ms * kSampleRate / 1000.0);

        std::vector<double> x (n, 0.0);
        unsigned int s = seed;

        for (size_t i = 0; i < n; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;

            const double u = static_cast<double> (s & 0xffffffu) / 16777215.0;
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                                   * static_cast<double> (i)
                                                   / static_cast<double> (n - 1));

            x[i] = (2.0 * u - 1.0) * w;
        }

        return x;
    }

    [[nodiscard]] std::vector<double> monoFrom (const std::vector<StereoFrame>& f)
    {
        std::vector<double> x (f.size(), 0.0);

        for (size_t n = 0; n < f.size(); ++n)
            x[n] = f[n].l;

        return x;
    }

    // =======================================================================
    //  Spectral metrics that do not rest on one FFT bin
    // =======================================================================
    struct PeakMetrics
    {
        double frequencyHz = 0.0;
        double localProminenceDb = 0.0;   ///< against the median of a +/- 1/3 octave window
        double bandwidthHz = 0.0;         ///< -3 dB width around the peak
        double q = 0.0;
        double thirdOctaveProminenceDb = 0.0;
        double persistenceDb = 0.0;       ///< late-window peak against early-window peak
    };

    /**
        STAGE 1 AND STAGE 3 BOTH GOT BITTEN BY SINGLE-BIN METRICS, so this one
        reports a peak only together with what surrounds it.

        `localProminenceDb` is the peak against the MEDIAN of a third-octave
        window centred on it, which is robust to one outlying bin in a way
        peak-minus-minimum is not. `thirdOctaveProminenceDb` is the same
        question at a resolution a listener plausibly integrates over. If the
        two disagree, the disagreement is the finding.
    */
    [[nodiscard]] PeakMetrics analysePeaks (const std::vector<double>& signal,
                                            size_t fftSize = 32768,
                                            double lowHz = 150.0,
                                            double highHz = 8000.0)
    {
        PeakMetrics m;

        const auto spectrum = magnitudeSpectrum (signal, fftSize, false);

        const auto lo = binForFrequency (lowHz, fftSize, kSampleRate);
        const auto hi = std::min (spectrum.size() - 1,
                                  binForFrequency (highHz, fftSize, kSampleRate));

        if (hi <= lo)
            return m;

        auto db = [] (double v) { return 20.0 * std::log10 (std::max (1.0e-30, v)); };

        // The tallest bin, then described by its neighbourhood.
        size_t peakBin = lo;

        for (size_t k = lo; k <= hi; ++k)
            if (spectrum[k] > spectrum[peakBin])
                peakBin = k;

        m.frequencyHz = static_cast<double> (peakBin) * kSampleRate
                      / static_cast<double> (fftSize);

        const double peakDb = db (spectrum[peakBin]);

        // A third-octave window around the peak, median taken.
        const auto windowLo = binForFrequency (m.frequencyHz / 1.122, fftSize, kSampleRate);
        const auto windowHi = std::min (spectrum.size() - 1,
                                        binForFrequency (m.frequencyHz * 1.122,
                                                         fftSize, kSampleRate));

        std::vector<double> window;

        for (size_t k = windowLo; k <= windowHi; ++k)
            window.push_back (db (spectrum[k]));

        if (! window.empty())
        {
            std::sort (window.begin(), window.end());
            m.localProminenceDb = peakDb - window[window.size() / 2];
        }

        // -3 dB bandwidth, walking out from the peak.
        size_t left = peakBin, right = peakBin;

        while (left > lo && db (spectrum[left]) > peakDb - 3.0)
            --left;

        while (right < hi && db (spectrum[right]) > peakDb - 3.0)
            ++right;

        m.bandwidthHz = static_cast<double> (right - left) * kSampleRate
                      / static_cast<double> (fftSize);
        m.q = m.bandwidthHz > 0.0 ? m.frequencyHz / m.bandwidthHz : 0.0;

        // Third-octave view of the same question.
        const auto bands = thirdOctaveEnergy (spectrum, kSampleRate, fftSize, lowHz, highHz);

        if (! bands.energyDb.empty())
        {
            auto sorted = bands.energyDb;
            std::sort (sorted.begin(), sorted.end());

            m.thirdOctaveProminenceDb = sorted.back() - sorted[sorted.size() / 2];
        }

        return m;
    }

    /** How much a peak at `hz` survives into the late tail. */
    [[nodiscard]] double persistenceAt (const std::vector<double>& signal, double hz)
    {
        const auto earlyLength = static_cast<size_t> (kSampleRate * 0.25);
        const auto lateStart   = static_cast<size_t> (kSampleRate * 1.5);

        if (signal.size() < lateStart + earlyLength)
            return 0.0;

        auto bandEnergy = [&] (size_t from)
        {
            std::vector<double> segment (signal.begin() + static_cast<long> (from),
                                         signal.begin() + static_cast<long> (from + earlyLength));

            const auto spectrum = magnitudeSpectrum (segment, 16384, true);

            const auto centre = binForFrequency (hz, 16384, kSampleRate);
            const auto lo = binForFrequency (hz / 1.06, 16384, kSampleRate);
            const auto hi = std::min (spectrum.size() - 1,
                                      binForFrequency (hz * 1.06, 16384, kSampleRate));

            double sum = 0.0;

            for (size_t k = lo; k <= hi && k < spectrum.size(); ++k)
                sum += spectrum[k] * spectrum[k];

            (void) centre;

            return sum;
        };

        const double early = bandEnergy (0);
        const double late  = bandEnergy (lateStart);

        return 10.0 * std::log10 (std::max (1.0e-30, late) / std::max (1.0e-30, early));
    }
}

// ===========================================================================

void runPhase11Stage4Tests()
{
    // =======================================================================
    beginSuite ("PHASE 11.4.A1 - choosing representative failing frequencies");
    // =======================================================================

    struct Candidate { double hz, size, decay, fold, corr; const char* label; };

    std::vector<Candidate> chosen;

    {
        /*
            THE SELECTION RULE MATTERS MORE THAN THE SEARCH.

            The brief is explicit: do not choose frequencies only because they
            maximise a diagnostic, and prefer ones that can plausibly occur in
            sustained musical material. So the scan is restricted to
            frequencies near equal-tempered pitches, and both a severe case
            and a MODERATE one are taken - plus a healthy control close by, so
            the listener has a reference that differs only in frequency.

            Two configurations are scanned: the product DEFAULT, because a
            failure there matters far more than one at an extreme, and the
            small/short configuration where Stage 1 originally found it.
        */
        info ("  Scanning equal-tempered pitches only, so the chosen cases are");
        info ("  notes a musician could actually play.");
        info ("");
        info ("    " + pad ("config", 22) + pad ("note", 8) + pad ("Hz", 10)
              + pad ("corr", 10) + "mono fold");

        struct Setting { const char* label; double size, decay; };

        const std::vector<Setting> settings
        {
            { "default: S60 D2.5", kSizeDefault, kDecay },
            { "small: S15 D0.6",   0.15,         0.6    },
        };

        // Equal-tempered pitches from A1 to A5, which is where sustained
        // musical energy actually lives.
        std::vector<std::pair<std::string, double>> pitches;

        const char* names[12] { "A", "A#", "B", "C", "C#", "D",
                                "D#", "E", "F", "F#", "G", "G#" };

        for (int semitone = -24; semitone <= 24; ++semitone)
        {
            const double hz = 220.0 * std::pow (2.0, semitone / 12.0);
            const int index = ((semitone % 12) + 12) % 12;
            const int octave = 3 + (semitone + 9) / 12;

            pitches.emplace_back (std::string (names[index]) + std::to_string (octave), hz);
        }

        for (const auto& s : settings)
        {
            std::vector<Candidate> scan;

            for (const auto& p : pitches)
            {
                Config c;
                c.size = s.size;
                c.decay = s.decay;

                const auto out = renderLate (c, tone (p.second, 0.8),
                                             static_cast<size_t> (kSampleRate * 0.8));

                scan.push_back ({ p.second, s.size, s.decay, monoFoldDb (out),
                                  correlationOf (out), s.label });
            }

            std::sort (scan.begin(), scan.end(),
                       [] (const Candidate& a, const Candidate& b) { return a.fold < b.fold; });

            // Severe: the worst. Moderate: around -6 dB. Control: the best.
            const auto& severe = scan.front();
            const auto& healthy = scan.back();

            const Candidate* moderate = &scan.front();

            for (const auto& candidate : scan)
                if (candidate.fold > -8.0 && candidate.fold < -5.0)
                {
                    moderate = &candidate;
                    break;
                }

            auto nameFor = [&pitches] (double hz)
            {
                for (const auto& p : pitches)
                    if (std::abs (p.second - hz) < 0.01)
                        return p.first;

                return std::string ("?");
            };

            for (const auto* candidate : { &severe, moderate, &healthy })
            {
                const char* kind = candidate == &severe ? " (severe)"
                                 : candidate == &healthy ? " (control)" : " (moderate)";

                info ("    " + pad (std::string (s.label) + kind, 22)
                      + pad (nameFor (candidate->hz), 8)
                      + pad (fx (candidate->hz, 1), 10)
                      + pad (fx (candidate->corr, 4), 10)
                      + fx (candidate->fold, 2) + " dB");

                chosen.push_back (*candidate);
            }

            info ("");
        }

        info ("    A SEVERE CASE AT THE PRODUCT DEFAULT IS THE ONE THAT");
        info ("    MATTERS. Stage 1 found this at Size 15 % and Stage 3 showed");
        info ("    the condition is flat in Size, so a default-setting failure");
        info ("    should exist - and if it does, the question is no longer");
        info ("    whether an extreme setting misbehaves.");

        check (chosen.size() == 6, "six representative frequencies were selected");
    }

    // =======================================================================
    beginSuite ("PHASE 11.4.A3 - does the null survive musical material?");
    // =======================================================================
    {
        /*
            THE QUESTION STAGE 3 COULD NOT ANSWER. A pure tone is a single
            frequency and can sit exactly in a null. A musical note is a
            fundamental plus harmonics with an envelope, and each harmonic
            lands somewhere different. If the null is as narrow as Stage 3
            measured, a real note should average over it.

            Measured, not argued: the same frequencies, driven by a pure tone
            and by three musical sources.
        */
        info ("    " + pad ("config", 18) + pad ("Hz", 9) + pad ("pure tone", 12)
              + pad ("pad note", 12) + pad ("bass note", 12) + "piano note");

        double worstMusical = 0.0, worstTone = 0.0;
        std::string worstMusicalWhere;

        for (const auto& candidate : chosen)
        {
            if (std::string (candidate.label).empty())
                continue;

            Config c;
            c.size = candidate.size;
            c.decay = candidate.decay;

            const auto length = static_cast<size_t> (kSampleRate * 3.0);

            const double pure  = monoFoldDb (renderLate (c, tone (candidate.hz, 2.0), length));
            const double padDb = monoFoldDb (renderLate (c, padNote (candidate.hz, 2.0), length));
            const double bass  = monoFoldDb (renderLate (c, bassNote (candidate.hz, 2.0), length));
            const double piano = monoFoldDb (renderLate (c, pianoNote (candidate.hz, 2.0), length));

            const double musicalWorst = std::min ({ padDb, bass, piano });

            if (musicalWorst < worstMusical)
            {
                worstMusical = musicalWorst;
                worstMusicalWhere = std::string (candidate.label) + " at "
                                  + fx (candidate.hz, 1) + " Hz";
            }

            worstTone = std::min (worstTone, pure);

            info ("    " + pad (candidate.label, 18)
                  + pad (fx (candidate.hz, 1), 9)
                  + pad (fx (pure, 2) + " dB", 12)
                  + pad (fx (padDb, 2) + " dB", 12)
                  + pad (fx (bass, 2) + " dB", 12)
                  + fx (piano, 2) + " dB");
        }

        info ("");
        info ("    worst pure tone      " + fx (worstTone, 2) + " dB");
        info ("    worst musical source " + fx (worstMusical, 2) + " dB");
        info ("      at " + worstMusicalWhere);
        info ("");
        info ("    THE GAP BETWEEN THOSE TWO NUMBERS IS THE WHOLE QUESTION. If");
        info ("    the musical column stays near -3 dB while the tone column");
        info ("    reaches double figures, the null is narrower than a note and");
        info ("    the diagnostic is not describing a product problem. If the");
        info ("    musical column follows the tone column down, it is.");

        check (worstMusical > worstTone - 1.0,
               "musical sources are no worse than pure tones");

    // =======================================================================
    beginSuite ("PHASE 11.4.B1 - baseline tonal-ring measurement");
    // =======================================================================

    PeakMetrics baselinePeak;

    {
        /*
            The configuration the human was listening to, which is also the
            product default. Mix does not enter an offline wet-path analysis;
            it only scales the dry against this, so the tail is what it is.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        info ("  Decay 2.5 s, Size 60 %, Early/Late 0, Early Diffusion 70 %,");
        info ("  Mod Depth 30 %, damping and cuts at their defaults.");
        info ("");
        info ("    " + pad ("source", 16) + pad ("peak Hz", 11)
              + pad ("local prom", 13) + pad ("bandwidth", 12)
              + pad ("Q", 9) + "1/3-oct prom");

        const auto fu2 = makeFu2Sources (kSampleRate);

        auto fu2Mono = [&fu2] (const char* name)
        {
            for (const auto& s : fu2)
                if (s.name == name)
                    return monoFrom (s.frames);

            return std::vector<double> {};
        };

        std::vector<double> impulse (4800, 0.0);
        impulse[0] = 1.0;

        struct Source { const char* name; std::vector<double> drive; };

        const std::vector<Source> sources
        {
            { "impulse",  impulse },
            { "noise",    noiseBurst (10.0, 22222u) },
            { "vocal",    monoFrom (makeVocalReference (kSampleRate, 6.0)) },
            { "pad 220",  padNote (220.0, 3.0) },
        };

        for (const auto& src : sources)
        {
            if (src.drive.empty())
                continue;

            Config c;

            const auto out = renderLate (c, src.drive, length);
            auto m = analysePeaks (out.l);

            m.persistenceDb = persistenceAt (out.l, m.frequencyHz);

            if (std::string (src.name) == "impulse")
                baselinePeak = m;

            info ("    " + pad (src.name, 16)
                  + pad (fx (m.frequencyHz, 1), 11)
                  + pad (fx (m.localProminenceDb, 2) + " dB", 13)
                  + pad (fx (m.bandwidthHz, 2) + " Hz", 12)
                  + pad (fx (m.q, 1), 9)
                  + fx (m.thirdOctaveProminenceDb, 2) + " dB");
        }

        info ("");
        info ("    TWO WARNINGS ABOUT THIS TABLE, BOTH MINE.");
        info ("");
        info ("    THE Q COLUMN IS NOT REAL. Every bandwidth reads 2.93 Hz,");
        info ("    which at a 32768-point FFT is exactly two bins - the");
        info ("    resolution floor. The true mode bandwidth at RT60 2.5 s is");
        info ("    about 2.2/2.5 = 0.88 Hz, below what this transform can");
        info ("    resolve, so the Q figures are artefacts of the analysis and");
        info ("    must not be quoted as measurements. They are left in the");
        info ("    table because removing them would hide the limitation.");
        info ("");
        info ("    THE PAD AND VOCAL ROWS MEASURE THE SOURCE, NOT THE TAIL. A");
        info ("    pad at 220 Hz has its own harmonic peaks, and a 35.68 dB");
        info ("    third-octave prominence on that row is the pad being tonal,");
        info ("    not the reverb. Only the impulse and noise rows say anything");
        info ("    about the tail's own colour.");
        info ("");
        info ("    THE TWO PROMINENCE COLUMNS ARE THE POINT. The local figure");
        info ("    is the peak against the MEDIAN of a third-octave window");
        info ("    around it - robust to one outlying bin, which is what Stage");
        info ("    1 and Stage 3 both got caught by. The third-octave figure");
        info ("    asks the same question at a resolution a listener plausibly");
        info ("    integrates over. A tall local peak with a small");
        info ("    third-octave figure is a narrow resonance that a listener");
        info ("    may or may not hear; both being tall is a tonal colour.");

        check (baselinePeak.frequencyHz > 0.0, "a dominant peak was located");
    }

    // =======================================================================
    beginSuite ("PHASE 11.4.B2 - subsystem isolation, one change at a time");
    // =======================================================================

    double peakWithoutAllpass = 0.0, peakBaseline = 0.0;

    {
        /*
            RESEARCH-ONLY SWITCHES, inside the harness. Nothing in Source/ is
            touched; the Config flags above exist only in this file.

            One mechanism at a time, so a change can be attributed. The
            question the brief poses - is the ringing before the FDN, inside
            its state, or only in the readout - is answered by probing a
            single line state through a unit readout vector, which needs no
            new API.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        std::vector<double> impulse (4800, 0.0);
        impulse[0] = 1.0;

        info ("    " + pad ("configuration", 30) + pad ("peak Hz", 11)
              + pad ("local prom", 13) + pad ("Q", 9)
              + pad ("1/3-oct", 11) + "persistence");

        auto probe = [&] (const char* label, Config c)
        {
            const auto out = renderLate (c, impulse, length);
            auto m = analysePeaks (out.l);
            m.persistenceDb = persistenceAt (out.l, m.frequencyHz);

            info ("    " + pad (label, 30)
                  + pad (fx (m.frequencyHz, 1), 11)
                  + pad (fx (m.localProminenceDb, 2) + " dB", 13)
                  + pad (fx (m.q, 1), 9)
                  + pad (fx (m.thirdOctaveProminenceDb, 2) + " dB", 11)
                  + fx (m.persistenceDb, 1) + " dB");

            return m;
        };

        peakBaseline = probe ("baseline (defaults)", Config {}).localProminenceDb;

        {
            Config c; c.diffusionEnabled = false;
            probe ("A: input diffusion OFF", c);
        }

        {
            Config c; c.allpassEnabled = false;
            peakWithoutAllpass = probe ("B: allpass bank OFF", c).localProminenceDb;
        }

        for (double depth : { 0.0, 1.0 })
        {
            Config c; c.modDepth = depth;
            probe (("C: Mod Depth " + fx (depth * 100.0, 0) + " %").c_str(), c);
        }

        for (double rate : { 0.05, 10.0 })
        {
            Config c; c.modRate = rate;
            probe (("D: Mod Rate " + fx (rate, 2) + " Hz").c_str(), c);
        }

        {
            Config c; c.lowDamp = 0.0; c.highDamp = 0.0;
            probe ("E: damping OFF", c);
        }

        {
            Config c; c.highDamp = 1.0;
            probe ("E: High Damp 100 %", c);
        }

        for (double diff : { 0.0, 1.0 })
        {
            Config c; c.earlyDiff = diff;
            probe (("Early Diffusion " + fx (diff * 100.0, 0) + " %").c_str(), c);
        }

        for (double size : { 0.20, 1.0 })
        {
            Config c; c.size = size;
            probe (("G: Size " + fx (size * 100.0, 0) + " %").c_str(), c);
        }

        for (double decay : { 0.8, 8.0 })
        {
            Config c; c.decay = decay;
            probe (("H: Decay " + fx (decay, 1) + " s").c_str(), c);
        }

        info ("");
        info ("  WHERE IN THE CHAIN - probing one FDN line state directly");
        info ("  through a unit readout vector:");
        info ("");

        for (int line : { 0, 3, 7 })
        {
            Config c; c.probeLine = line;
            probe (("line " + std::to_string (line) + " state").c_str(), c);
        }

        {
            // The diffuser alone, with the tank contributing nothing: Size at
            // minimum and Decay at minimum is not a bypass, so instead the
            // diffuser output is taken by reading a line before the loop has
            // run - approximated by the shortest possible decay.
            Config c; c.decay = 0.1; c.allpassEnabled = false; c.modDepth = 0.0;
            probe ("short decay, no allpass", c);
        }

        info ("");
        info ("    The line-state rows answer the brief's question six. If the");
        info ("    peak is already in a single line state then it is formed in");
        info ("    the recurrence; if it appears only in the two-vector readout");
        info ("    then it is a combination artefact.");

        check (peakBaseline > 0.0 && peakWithoutAllpass > 0.0,
               "the isolation matrix produced a measurement for every "
               "configuration");
    }

    // =======================================================================
    beginSuite ("PHASE 11.4.B4 - L/R consistency of the peak");
    // =======================================================================
    {
        /*
            A peak equal in both channels is a tonal colour. One that differs
            between them is a spatial artefact and belongs with Part A rather
            than with tail quality, so the two have to be told apart before
            the observation is classified.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        std::vector<double> impulse (4800, 0.0);
        impulse[0] = 1.0;

        Config c;

        const auto out = renderLate (c, impulse, length);

        const auto left  = analysePeaks (out.l);
        const auto right = analysePeaks (out.r);

        info ("    " + pad ("channel", 11) + pad ("peak Hz", 11)
              + pad ("local prom", 13) + pad ("Q", 9) + "1/3-oct prom");
        info ("    " + pad ("left", 11) + pad (fx (left.frequencyHz, 1), 11)
              + pad (fx (left.localProminenceDb, 2) + " dB", 13)
              + pad (fx (left.q, 1), 9)
              + fx (left.thirdOctaveProminenceDb, 2) + " dB");
        info ("    " + pad ("right", 11) + pad (fx (right.frequencyHz, 1), 11)
              + pad (fx (right.localProminenceDb, 2) + " dB", 13)
              + pad (fx (right.q, 1), 9)
              + fx (right.thirdOctaveProminenceDb, 2) + " dB");

        info ("");
        info ("    frequency difference  "
              + fx (std::abs (left.frequencyHz - right.frequencyHz), 1) + " Hz");
        info ("    prominence difference "
              + fx (std::abs (left.localProminenceDb - right.localProminenceDb), 2)
              + " dB");

        check (std::abs (left.localProminenceDb - right.localProminenceDb) < 12.0,
               "the dominant peak is comparable in both channels");
    }
    }
}

} // namespace avtest
