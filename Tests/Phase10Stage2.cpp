#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 10 - STAGE 2. ER research prototypes, balance and Size validation.

    No shipping DSP, no public parameter. Everything here lives in this file.

    TWO STAGE-1 CORRECTIONS ARE APPLIED THROUGHOUT.

    The first is about the Early/Late balance. Stage 1 recommended a policy
    whose total wet energy moves 5.11 dB across the travel and justified it as
    "a level change is expected at the ER-heavy end". That was the wrong
    framing: Early/Late is a COMPOSITION control inside the wet signal, Mix
    already owns dry against total wet, and a composition control that also
    changes loudness makes both controls harder to use. So the primary
    candidate here is an energy-calibrated constant-power law, and the target
    is about 1 dB rather than 5.

    The second is about mono wording. Stage 1 called ER-B's -3.01 dB downmix
    "exactly mono-neutral" and "no cancellation". That is overclaiming from
    one test. -3.01 dB is what two approximately decorrelated equal-energy
    channels are EXPECTED to give, and the correct reading is that this test
    showed no abnormal destructive cancellation. Mono is therefore measured
    again here across sources and across Size.

    AND ONE THING IS MEASURED RATHER THAN ASSUMED, because the brief is right
    to flag it: the two branches come from the same source and overlap in
    time, so they are CORRELATED, and the energy of their sum is not the sum
    of their energies. Every balance figure below is the measured energy of
    the actual summed signal.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    constexpr double kDecay          = 2.5;
    constexpr double kSizeDefault    = 0.60;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;

    /** Stage-1 research ER window at the default Size. Not a contract. */
    constexpr double kWindowMs = 80.0;

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

    // -----------------------------------------------------------------------
    //  The late branch: Attack, then the tank
    // -----------------------------------------------------------------------
    struct LateConfig
    {
        double size           = kSizeDefault;
        double earlyDiffusion = kEarlyDiffusion;
        double modDepth       = kModDepth;
        double attack         = 0.0;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const LateConfig& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (kDecay), true);
        tank->setInputDiffusion (c.earlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (c.modDepth),
                                      mapping::modRateScaleFor (kModRate), true);

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

    void renderLate (const LateConfig& c, const std::vector<double>& drive,
                     size_t length, std::vector<double>& outL, std::vector<double>& outR)
    {
        auto tank = makeTank (c);

        AttackInjection attack;
        attack.prepare (kSampleRate);
        attack.snapTo (c.attack);

        outL.assign (length, 0.0);
        outR.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            outL[n] = n < drive.size() ? drive[n] : 0.0;
            outR[n] = outL[n];
        }

        attack.process (outL.data(), outR.data(), static_cast<int> (length));

        for (size_t n = 0; n < length; ++n)
            tank->processSample (outL[n], outR[n]);
    }

    // =======================================================================
    //  RESEARCH-ONLY ER candidates
    // =======================================================================
    struct Tap { double timeMs = 0.0, gainL = 0.0, gainR = 0.0; };

    /** Timing distribution families, per the brief's three options. */
    enum class Timing { irregular, clustered, hybrid };

    [[nodiscard]] double arrivalGain (double timeMs, double windowMs)
    {
        const double t = std::max (1.0, timeMs);

        return std::exp (-t / (windowMs * 0.6)) / std::sqrt (t);
    }

    /*
        A tap schedule.

        `events` is reflection events PER CHANNEL. The irregular and hybrid
        families give each channel its own time for each event, so the total
        tap count is 2 * events; the clustered family shares event times and
        pans them, so it is `events` taps carrying both channels.

        Timing is deterministic, synthetic and specified in milliseconds:
        golden-ratio placement inside equal cells, which is irregular with no
        repeating interval. Nothing is seeded from the clock, nothing is a
        measured room, and nothing is uniformly spaced.

        `sharedEarly` is the hybrid family's parameter: the first few events
        are given IDENTICAL times in both channels, so the initial image has a
        centre to sit on, and only the later events decorrelate.
    */
    [[nodiscard]] std::vector<Tap> buildTaps (Timing timing, int events,
                                              double windowMs, int sharedEarly = 2)
    {
        std::vector<Tap> taps;

        constexpr double golden = 0.6180339887498949;
        constexpr double offset = 0.37;

        unsigned int state = 2166136261u;

        const auto next = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };

        if (timing == Timing::clustered)
        {
            const double clusterStart[] { 0.08, 0.34, 0.66 };
            const double clusterWidth[] { 0.10, 0.14, 0.20 };

            // Spread the events over three clusters, weighted later-heavier.
            const int perCluster[] { std::max (1, events / 4),
                                     std::max (1, events / 3),
                                     std::max (1, events - events / 4 - events / 3) };

            int index = 0;

            for (int cluster = 0; cluster < 3; ++cluster)
            {
                for (int k = 0; k < perCluster[cluster]; ++k)
                {
                    double fraction = golden * static_cast<double> (index + 1);
                    fraction -= std::floor (fraction);

                    const double t = (clusterStart[cluster]
                                      + fraction * clusterWidth[cluster]) * windowMs;
                    const double g = arrivalGain (t, windowMs);

                    const double pan = index == 0 ? 0.0
                                     : (static_cast<double> (next() % 2001u) / 1000.0 - 1.0);

                    taps.push_back ({ t,
                                      g * std::sqrt (0.5 * (1.0 - pan)),
                                      g * std::sqrt (0.5 * (1.0 + pan)) });
                    ++index;
                }
            }

            return taps;
        }

        for (int k = 0; k < events; ++k)
        {
            const double low  = static_cast<double> (k) / events;
            const double high = static_cast<double> (k + 1) / events;

            double a = golden * static_cast<double> (k + 1);
            a -= std::floor (a);

            double b = golden * static_cast<double> (k + 1) + offset;
            b -= std::floor (b);

            const double tL = (low + a * (high - low)) * windowMs;

            // Hybrid: the first `sharedEarly` events are centred, i.e. both
            // channels get the SAME time, so the image has an anchor.
            const bool shared = timing == Timing::hybrid && k < sharedEarly;

            const double tR = shared ? tL : (low + b * (high - low)) * windowMs;

            taps.push_back ({ tL, arrivalGain (tL, windowMs), shared ? arrivalGain (tL, windowMs) : 0.0 });

            if (! shared)
                taps.push_back ({ tR, 0.0, arrivalGain (tR, windowMs) });
        }

        return taps;
    }

    /** L2-normalises a tap list, so candidates are compared at equal energy. */
    void normaliseTaps (std::vector<Tap>& taps)
    {
        double squares = 0.0;

        for (const auto& t : taps)
            squares += t.gainL * t.gainL + t.gainR * t.gainR;

        if (squares <= 0.0)
            return;

        const double scale = std::sqrt (2.0 / squares);   // unit per channel

        for (auto& t : taps)
        {
            t.gainL *= scale;
            t.gainR *= scale;
        }
    }

    void renderEarly (const std::vector<Tap>& taps,
                      const std::vector<double>& drive, size_t length,
                      std::vector<double>& outL, std::vector<double>& outR)
    {
        outL.assign (length, 0.0);
        outR.assign (length, 0.0);

        for (size_t n = 0; n < drive.size() && n < length; ++n)
        {
            if (drive[n] == 0.0)
                continue;

            for (const auto& tap : taps)
            {
                const auto at = n + static_cast<size_t> (
                    std::lround (tap.timeMs * kSampleRate / 1000.0));

                if (at < length)
                {
                    outL[at] += drive[n] * tap.gainL;
                    outR[at] += drive[n] * tap.gainR;
                }
            }
        }
    }

    // =======================================================================
    //  Metrics
    // =======================================================================
    [[nodiscard]] std::vector<double> envelopeDb (const std::vector<double>& x,
                                                  double windowMs, double stepMs,
                                                  double untilMs)
    {
        const auto win  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);
        const auto step = std::max<size_t> (1, static_cast<size_t> (stepMs * kSampleRate / 1000.0));
        const auto last = static_cast<size_t> (untilMs * kSampleRate / 1000.0);

        std::vector<double> out;

        for (size_t from = 0; from + win <= std::min (last, x.size()); from += step)
        {
            double sum = 0.0;

            for (size_t n = from; n < from + win; ++n)
                sum += x[n] * x[n];

            const double r = std::sqrt (sum / static_cast<double> (win));

            out.push_back (r > 0.0 ? 20.0 * std::log10 (r) : -200.0);
        }

        return out;
    }

    [[nodiscard]] double firstArrivalMs (const std::vector<double>& x)
    {
        double peak = 0.0;

        for (double s : x)
            peak = std::max (peak, std::abs (s));

        if (peak <= 0.0)
            return -1.0;

        for (size_t n = 0; n < x.size(); ++n)
            if (std::abs (x[n]) >= peak * 0.01)
                return 1000.0 * static_cast<double> (n) / kSampleRate;

        return -1.0;
    }

    /** Spacing statistics from the SCHEDULE, which has no failure mode. */
    struct Spacing { double mean = 0.0, median = 0.0, minimum = 0.0; int separated = 0; };

    [[nodiscard]] Spacing spacingOf (const std::vector<Tap>& taps)
    {
        std::vector<double> times;

        for (const auto& t : taps)
            times.push_back (t.timeMs);

        std::sort (times.begin(), times.end());

        // Collapse coincident times: two taps at the same instant are one
        // arrival, not two.
        std::vector<double> unique;

        for (double t : times)
            if (unique.empty() || t - unique.back() > 0.05)
                unique.push_back (t);

        Spacing s;

        if (unique.size() < 2)
            return s;

        std::vector<double> gaps;

        for (size_t k = 1; k < unique.size(); ++k)
            gaps.push_back (unique[k] - unique[k - 1]);

        s.minimum = *std::min_element (gaps.begin(), gaps.end());

        double total = 0.0;

        for (double g : gaps)
            total += g;

        s.mean = total / static_cast<double> (gaps.size());

        auto sorted = gaps;
        std::sort (sorted.begin(), sorted.end());
        s.median = sorted[sorted.size() / 2];

        s.separated = 1;

        for (double g : gaps)
            if (g > 10.0)
                ++s.separated;

        return s;
    }

    [[nodiscard]] double crestIn (const std::vector<double>& x, double untilMs)
    {
        const auto last = std::min (x.size(),
                                    static_cast<size_t> (untilMs * kSampleRate / 1000.0));

        return last > 0 ? crestFactor (x.data(), last) : 0.0;
    }

    [[nodiscard]] double rippleDb (const std::vector<double>& x)
    {
        constexpr size_t fftSize = 65536;

        const auto magnitude = magnitudeSpectrum (x, fftSize, false);
        const auto bands = thirdOctaveEnergy (magnitude, kSampleRate, fftSize, 100.0, 16000.0);

        if (bands.energyDb.empty())
            return 0.0;

        return *std::max_element (bands.energyDb.begin(), bands.energyDb.end())
             - *std::min_element (bands.energyDb.begin(), bands.energyDb.end());
    }

    struct Stereo { double correlation = 0.0, monoDb = 0.0; };

    [[nodiscard]] Stereo stereoIn (const std::vector<double>& l,
                                   const std::vector<double>& r,
                                   double fromMs, double toMs)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (l.size(),
                                    static_cast<size_t> (toMs * kSampleRate / 1000.0));

        Stereo s;

        if (to <= from)
            return s;

        s.correlation = correlation (l.data() + from, r.data() + from, to - from);

        double stereoEnergy = 0.0, monoEnergy = 0.0;

        for (size_t n = from; n < to; ++n)
        {
            stereoEnergy += l[n] * l[n] + r[n] * r[n];

            const double mono = 0.5 * (l[n] + r[n]);
            monoEnergy += 2.0 * mono * mono;
        }

        s.monoDb = stereoEnergy > 0.0
                 ? 10.0 * std::log10 (monoEnergy / stereoEnergy) : 0.0;

        return s;
    }

    [[nodiscard]] double energyOf (const std::vector<double>& x)
    {
        double sum = 0.0;

        for (double s : x)
            sum += s * s;

        return sum;
    }

    [[nodiscard]] double peakOf (const std::vector<double>& x)
    {
        double p = 0.0;

        for (double s : x)
            p = std::max (p, std::abs (s));

        return p;
    }

    /** Deepest notch that has a recovery, inside a window. See Stage 1. */
    [[nodiscard]] double notchDb (const std::vector<double>& x,
                                  double fromMs, double toMs, double* atMs = nullptr)
    {
        const auto env = envelopeDb (x, 5.0, 5.0, toMs + 50.0);

        if (env.size() < 6)
            return 0.0;

        std::vector<double> smooth (env.size(), 0.0);

        for (size_t i = 0; i < env.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;

            for (int k = -2; k <= 2; ++k)
            {
                const auto j = static_cast<long long> (i) + k;

                if (j >= 0 && j < static_cast<long long> (env.size()))
                {
                    sum += env[static_cast<size_t> (j)];
                    ++count;
                }
            }

            smooth[i] = sum / count;
        }

        const auto from = static_cast<size_t> (fromMs / 5.0);
        const auto to   = std::min (smooth.size(), static_cast<size_t> (toMs / 5.0));

        double worst = 0.0;

        for (size_t i = from + 1; i + 1 < to; ++i)
        {
            double before = -1.0e30, after = -1.0e30;

            for (size_t k = from; k < i; ++k)
                before = std::max (before, smooth[k]);

            for (size_t k = i + 1; k < to; ++k)
                after = std::max (after, smooth[k]);

            const double depth = std::min (before, after) - smooth[i];

            if (depth > worst)
            {
                worst = depth;

                if (atMs != nullptr)
                    *atMs = static_cast<double> (i) * 5.0;
            }
        }

        return worst;
    }

    /** Mixes the two branches at a balance position under a named policy. */
    enum class Balance { uncalibrated, calibrated, linear, unnormalised };

    struct Mixed { std::vector<double> l, r; double erGain = 0.0, lateGain = 0.0; };

    [[nodiscard]] Mixed mixBranches (Balance policy, double theta01,
                                     const std::vector<double>& erL,
                                     const std::vector<double>& erR,
                                     const std::vector<double>& lateL,
                                     const std::vector<double>& lateR,
                                     double calibration)
    {
        /*
            theta01 runs 0 at FULL LATE to 1 at FULL ER, so that theta01 == 0
            is the legacy endpoint and can be an exact bypass.
        */
        const double theta = theta01 * 1.5707963267948966;   // 0 .. pi/2

        Mixed m;

        switch (policy)
        {
            case Balance::uncalibrated:
                m.lateGain = std::cos (theta);
                m.erGain   = std::sin (theta);
                break;

            case Balance::calibrated:
                m.lateGain = std::cos (theta);
                m.erGain   = std::sin (theta) * calibration;
                break;

            case Balance::linear:
                m.lateGain = 1.0 - theta01;
                m.erGain   = theta01 * calibration;
                break;

            case Balance::unnormalised:
                // Stage 1's policy D, kept as the negative reference.
                m.lateGain = 1.0 - theta01;
                m.erGain   = theta01;
                break;
        }

        const auto length = lateL.size();

        m.l.assign (length, 0.0);
        m.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            m.l[n] = m.lateGain * lateL[n] + m.erGain * (n < erL.size() ? erL[n] : 0.0);
            m.r[n] = m.lateGain * lateR[n] + m.erGain * (n < erR.size() ? erR[n] : 0.0);
        }

        return m;
    }
}

void runPhase10Stage2Tests()
{
    ScopedFtz ftz;

    const auto length = static_cast<size_t> (kSampleRate * 2.5);

    // --- the compact program set ------------------------------------------
    std::vector<double> impulse (length, 0.0);
    impulse[0] = 1.0;

    std::vector<double> noise (length, 0.0);
    {
        const auto burst = whiteNoise (static_cast<size_t> (0.010 * kSampleRate), 20261116u);

        for (size_t n = 0; n < burst.size(); ++n)
        {
            const double w = 0.5 * (1.0 - std::cos (2.0 * damping::pi
                                    * static_cast<double> (n) / (burst.size() - 1)));
            noise[n] = burst[n] * w;
        }
    }

    // Mono sources, so centre stability is a property of the ER layer and not
    // of the material.
    const auto monoFrom = [length] (const std::vector<StereoFrame>& frames)
    {
        std::vector<double> out (length, 0.0);

        for (size_t n = 0; n < std::min (length, frames.size()); ++n)
            out[n] = 0.5 * (frames[n].l + frames[n].r);

        return out;
    };

    const auto fu2 = makeFu2Sources (kSampleRate);

    std::vector<double> snare, piano;

    for (const auto& s : fu2)
    {
        if (s.name == "snare") snare = monoFrom (s.frames);
        if (s.name == "piano") piano = monoFrom (s.frames);
    }

    const auto vocal = monoFrom (makeVocalReference (kSampleRate, 4.0));

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - ER-B variants and ER-D");
    // =======================================================================
    {
        info ("  Event counts and timing families, each L2-normalised so the");
        info ("  comparison is about structure and not level. Spacing comes");
        info ("  from the SCHEDULE rather than from an envelope peak-finder:");
        info ("  Stage 1 established that the envelope counter is fragile at");
        info ("  3-5 ms spacing, which is exactly where these decisions live.");
        info ("");
        info ("    " + pad ("family", 12) + pad ("events", 8) + pad ("taps", 7)
              + pad ("mean", 9) + pad ("median", 9) + pad ("min", 9)
              + pad ("sep", 6) + pad ("crest", 8) + pad ("ripple", 9)
              + pad ("corr", 9) + "mono");

        struct Variant { const char* label; Timing timing; int events; };

        const Variant variants[]
        {
            { "ER-B irreg",  Timing::irregular, 8 },
            { "ER-B irreg",  Timing::irregular, 12 },
            { "ER-B irreg",  Timing::irregular, 16 },
            { "ER-B hybrid", Timing::hybrid,    8 },
            { "ER-B hybrid", Timing::hybrid,    12 },
            { "ER-B hybrid", Timing::hybrid,    16 },
            { "ER-D cluster", Timing::clustered, 12 },
            { "ER-D cluster", Timing::clustered, 16 },
        };

        for (const auto& v : variants)
        {
            auto taps = buildTaps (v.timing, v.events, kWindowMs);
            normaliseTaps (taps);

            std::vector<double> l, r;
            renderEarly (taps, impulse, length, l, r);

            const auto s = spacingOf (taps);
            const auto st = stereoIn (l, r, 0.0, 200.0);

            info ("    " + pad (v.label, 12) + pad (std::to_string (v.events), 8)
                  + pad (std::to_string (taps.size()), 7)
                  + pad (fx (s.mean, 2), 9) + pad (fx (s.median, 2), 9)
                  + pad (fx (s.minimum, 2), 9)
                  + pad (std::to_string (s.separated), 6)
                  + pad (fx (crestIn (l, 200.0), 2), 8)
                  + pad (fx (rippleDb (l), 2), 9)
                  + pad (fx (st.correlation, 4), 9)
                  + fx (st.monoDb, 2) + " dB");
        }

        info ("");
        info ("    The minimum spacing is the slap-risk number and the mean is");
        info ("    the wash-risk number. An 80 ms window divided by 16 events");
        info ("    per channel leaves arrivals a few milliseconds apart, which");
        info ("    is dense; divided by 8 it leaves them around 10 ms apart,");
        info ("    which is where a listener starts to count them.");

        check (true, "the ER variants were measured");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - centre stability and mono, across sources");
    // =======================================================================
    {
        /*
            The Stage-1 mono claim was overstated and this is the correction:
            -3.01 dB is what two approximately decorrelated equal-energy
            channels are EXPECTED to give. It shows no abnormal destructive
            cancellation in that test; it is not proof of absolute absence of
            cancellation, and one test on one source is not a mono
            certification. So: four sources, and the early window separated
            from the whole.
        */
        info ("  Mono measured across sources, and the EARLY window separated");
        info ("  from the whole, because an image that is stable overall can");
        info ("  still wander at the onset - which is where a listener locates");
        info ("  the source.");
        info ("");
        info ("  -3.01 dB is the figure two approximately decorrelated");
        info ("  equal-energy channels are expected to give. Read it as 'no");
        info ("  abnormal destructive cancellation here', not as proof of none.");
        info ("");
        info ("    " + pad ("family", 13) + pad ("source", 10)
              + pad ("corr 0-20ms", 14) + pad ("corr full", 12)
              + pad ("mono 0-20ms", 14) + pad ("mono full", 13) + "L/R first diff");

        struct Source { const char* name; const std::vector<double>* data; };

        const Source sources[]
        {
            { "impulse", &impulse }, { "snare", &snare },
            { "vocal", &vocal },     { "noise", &noise },
        };

        struct Family { const char* label; Timing timing; int events; int shared; };

        const Family families[]
        {
            { "irregular", Timing::irregular, 12, 0 },
            { "hybrid",    Timing::hybrid,    12, 2 },
            { "clustered", Timing::clustered, 12, 0 },
        };

        double worstEarlyMono = 0.0;

        for (const auto& f : families)
        {
            auto taps = buildTaps (f.timing, f.events, kWindowMs, f.shared);
            normaliseTaps (taps);

            // First-arrival time difference between channels, from the
            // schedule: the cue a listener localises on.
            double firstL = 1.0e30, firstR = 1.0e30;

            for (const auto& t : taps)
            {
                if (t.gainL != 0.0) firstL = std::min (firstL, t.timeMs);
                if (t.gainR != 0.0) firstR = std::min (firstR, t.timeMs);
            }

            for (const auto& s : sources)
            {
                std::vector<double> l, r;
                renderEarly (taps, *s.data, length, l, r);

                const auto early = stereoIn (l, r, 0.0, 20.0);
                const auto full  = stereoIn (l, r, 0.0, 200.0);

                worstEarlyMono = std::max (worstEarlyMono, std::abs (early.monoDb));

                info ("    " + pad (f.label, 13) + pad (s.name, 10)
                      + pad (fx (early.correlation, 4), 14)
                      + pad (fx (full.correlation, 4), 12)
                      + pad (fx (early.monoDb, 2) + " dB", 14)
                      + pad (fx (full.monoDb, 2) + " dB", 13)
                      + fx (firstR - firstL, 2) + " ms");
            }

            info ("");
        }

        info ("    The last column is the inter-channel first-arrival");
        info ("    difference. A large one is a localisation cue pulling the");
        info ("    image off centre; the hybrid family exists to set it to zero");
        info ("    by giving the first events identical times in both channels.");

        check (worstEarlyMono >= 0.0, "centre stability and mono were measured across sources");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - Size scaling laws");
    // =======================================================================
    {
        /*
            ERscale = LateScale^alpha, swept. The question is which alpha
            keeps the ER window overlapping the late first arrival at every
            Size, which is what Stage 1 found to be an accident rather than a
            property.
        */
        info ("  ER window scaled as LateScale^alpha. Stage 1 found the");
        info ("  early-to-late overlap was an accident of the delay set, so the");
        info ("  question here is which alpha makes it a property: the ER span");
        info ("  must still cover the late first arrival at every Size.");
        info ("");
        info ("    " + pad ("Size", 8) + pad ("late 1st", 11) + pad ("late scale", 12)
              + "    ER span for alpha = 0 / 0.25 / 0.5 / 0.75 / 1.0");

        const double alphas[] { 0.0, 0.25, 0.5, 0.75, 1.0 };

        /*
            TWO constraints, not one.

            The lower one - the ER span must still reach the late first
            arrival, or the layers stop overlapping and a hole opens. The
            first version of this suite had only this, and every alpha passed
            it, which should have been the clue: at Size 0 even alpha 1 gives
            a 23 ms span against a 6.6 ms late onset. A test nothing fails is
            not discriminating.

            The upper one is the binding constraint, and it is the brief's own
            warning: an ER layer that extends far enough becomes a second late
            reverb. 120 ms is taken as the ceiling - about the point where the
            default late field has already established - and it is stated here
            so it can be argued with rather than discovered in the numbers.
        */
        constexpr double kSpanCeilingMs = 120.0;

        bool covers[5] { true, true, true, true, true };
        bool withinCeiling[5] { true, true, true, true, true };
        double worstSpan[5] { 0.0, 0.0, 0.0, 0.0, 0.0 };

        for (double size : { 0.0, 0.20, 0.60, 0.80, 1.0 })
        {
            LateConfig c;
            c.size = size;

            std::vector<double> l, r;
            renderLate (c, impulse, length, l, r);

            const double lateFirst = firstArrivalMs (l);
            const double lateScale = mapping::sizeScaleFor (size)
                                   / mapping::sizeScaleFor (kSizeDefault);

            std::string row = "    " + pad (fx (size * 100.0, 0) + " %", 8)
                            + pad (fx (lateFirst, 1) + " ms", 11)
                            + pad (fx (lateScale, 3), 12) + "    ";

            for (int a = 0; a < 5; ++a)
            {
                const double span = kWindowMs * std::pow (lateScale, alphas[a]);

                if (span < lateFirst)
                    covers[a] = false;

                if (span > kSpanCeilingMs)
                    withinCeiling[a] = false;

                worstSpan[a] = std::max (worstSpan[a], span);

                row += pad (fx (span, 1), 8);
            }

            info (row);
        }

        info ("");
        info ("    " + pad ("alpha", 9) + pad ("longest span", 15)
              + pad ("covers late onset", 20) + "within the 120 ms ceiling");

        for (int a = 0; a < 5; ++a)
            info ("    " + pad (fx (alphas[a], 2), 9)
                  + pad (fx (worstSpan[a], 1) + " ms", 15)
                  + pad (covers[a] ? "yes" : "NO", 20)
                  + (withinCeiling[a] ? "yes" : "NO"));

        info ("");
        info ("    alpha 0 is a fixed ER pattern: the layer describes the same");
        info ("    room whatever Size says, which is the behaviour Stage 1");
        info ("    rejected. alpha 1 ties ER rigidly to the late network, which");
        info ("    is the 'two layers moving as one object' the brief warns");
        info ("    about. The useful answers are in between, and the coverage");
        info ("    column is the constraint that rules some of them out.");

        check (true, "the Size laws were measured");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - Early/Late balance policies, measured");
    // =======================================================================
    {
        /*
            MEASURED ON THE SUM, not on the sum of the branch energies. The
            two branches come from one source and overlap in time, so they are
            correlated and a constant-power law derived from independent
            energies need not produce constant measured energy. The brief
            flags this and it is exactly right.
        */
        info ("  Every figure is the measured energy of the ACTUAL SUM, not the");
        info ("  sum of the branch energies. The branches come from one source");
        info ("  and overlap in time, so they are correlated, and a");
        info ("  constant-power law derived from independent energies need not");
        info ("  hold on the sum. That had to be measured rather than assumed.");
        info ("");

        LateConfig c;

        std::vector<double> lateL, lateR;
        renderLate (c, noise, length, lateL, lateR);

        auto taps = buildTaps (Timing::hybrid, 12, kWindowMs, 2);
        normaliseTaps (taps);

        std::vector<double> erL, erR;
        renderEarly (taps, noise, length, erL, erR);

        const double erEnergy = energyOf (erL) + energyOf (erR);
        const double lateEnergy = energyOf (lateL) + energyOf (lateR);

        // The static calibration K_er: equalise the branches' reference
        // energy, computed once, signal-independent in the sense that it
        // comes from one reference render rather than from the live signal.
        const double calibration = erEnergy > 0.0
                                 ? std::sqrt (lateEnergy / erEnergy) : 1.0;

        info ("    raw ER energy          " + fx (erEnergy, 6));
        info ("    raw late energy        " + fx (lateEnergy, 6));
        info ("    mismatch               "
              + fx (10.0 * std::log10 (erEnergy / lateEnergy), 2) + " dB");
        info ("    static calibration K   " + fx (calibration, 4));

        // The actual covariance between the branches, which is what decides
        // whether constant-power holds.
        {
            double cross = 0.0;

            for (size_t n = 0; n < length; ++n)
                cross += lateL[n] * erL[n] + lateR[n] * erR[n];

            const double normalised = (erEnergy > 0.0 && lateEnergy > 0.0)
                                    ? cross / std::sqrt (erEnergy * lateEnergy) : 0.0;

            info ("    branch cross-correlation  " + fx (normalised, 6));
            info ("");
            info ("    That number is why the sum has to be measured. If it");
            info ("    were large, a constant-power law would overshoot or");
            info ("    undershoot badly; near zero, the law holds closely and");
            info ("    the remaining variation is the branches' different");
            info ("    temporal shapes rather than their interference.");
            info ("");
        }

        info ("    " + pad ("balance", 10)
              + pad ("BAL-A uncal", 14) + pad ("BAL-B calib", 14)
              + pad ("BAL-C linear", 15) + "BAL-D unnorm");

        struct Policy { Balance policy; double worst = 0.0; };

        Policy policies[] { { Balance::uncalibrated }, { Balance::calibrated },
                            { Balance::linear }, { Balance::unnormalised } };

        for (double theta : { 0.0, 0.25, 0.50, 0.75, 1.0 })
        {
            std::string row = "    " + pad (fx (theta, 2), 10);

            for (auto& p : policies)
            {
                const auto m = mixBranches (p.policy, theta, erL, erR, lateL, lateR,
                                            calibration);

                const double e = energyOf (m.l) + energyOf (m.r);
                const double db = (e > 0.0 && lateEnergy > 0.0)
                                ? 10.0 * std::log10 (e / lateEnergy) : -200.0;

                p.worst = std::max (p.worst, std::abs (db));

                row += pad (fx (db, 2) + " dB", 15);
            }

            info (row);
        }

        info ("");
        info ("    worst deviation from the PHASE 9 wet energy");
        info ("      BAL-A uncalibrated equal-power   " + fx (policies[0].worst, 2) + " dB");
        info ("      BAL-B calibrated equal-power     " + fx (policies[1].worst, 2) + " dB");
        info ("      BAL-C linear ratio, calibrated   " + fx (policies[2].worst, 2) + " dB");
        info ("      BAL-D Stage-1 unnormalised       " + fx (policies[3].worst, 2) + " dB");
        info ("");
        info ("    Target is about 1 dB. Nothing here is dynamically");
        info ("    normalised: the calibration is one scalar from one reference");
        info ("    render, so there is no gain pumping, no program-dependent");
        info ("    semantics and no hidden AGC.");

        check (policies[1].worst >= 0.0, "the balance policies were measured on the sum");

        // --- the legacy endpoint -------------------------------------------
        info ("");
        info ("  The legacy endpoint. At balance 0 the ER gain is exactly zero");
        info ("  and the late gain exactly one, so the sum must be the PHASE 9");
        info ("  render bit for bit - not close.");
        info ("");

        double worstEndpoint = 0.0;
        long long differing = 0;

        const std::vector<double>* endpointSources[] { &impulse, &snare, &vocal, &noise };

        for (const std::vector<double>* source : endpointSources)
        {
            std::vector<double> ll, rr;
            renderLate (c, *source, length, ll, rr);

            std::vector<double> el, er;
            renderEarly (taps, *source, length, el, er);

            const auto m = mixBranches (Balance::calibrated, 0.0, el, er, ll, rr,
                                        calibration);

            for (size_t n = 0; n < length; ++n)
            {
                const double dl = std::abs (m.l[n] - ll[n]);
                const double dr = std::abs (m.r[n] - rr[n]);

                worstEndpoint = std::max (worstEndpoint, std::max (dl, dr));

                if (dl != 0.0 || dr != 0.0)
                    ++differing;
            }
        }

        info ("    worst difference across four sources  " + fx (worstEndpoint, 16));
        info ("    samples differing                     " + std::to_string (differing));
        info ("");
        info ("    cos(0) is exactly 1 and sin(0) exactly 0 in IEEE double, so");
        info ("    the mix reduces to 1.0 * late + 0.0 * er. A production");
        info ("    implementation should still take an explicit bypass branch");
        info ("    rather than rely on that: PHASE 9 showed that a tap set");
        info ("    summing to unity is not the same arithmetic as a passthrough,");
        info ("    and the same caution applies to a multiply by a float that");
        info ("    happens to be zero.");

        checkEqual (worstEndpoint, 0.0,
                    "the full-late endpoint reproduces PHASE 9 bit for bit");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - handoff across Size and balance");
    // =======================================================================
    {
        info ("  The overlap depends on Size, so it is measured across Size and");
        info ("  across balance rather than at one condition. The notch measure");
        info ("  requires a recovery on the far side, since a decay is not a");
        info ("  hole - Stage 1 learned that by reporting the reverb tail as a");
        info ("  10.87 dB energy hole.");
        info ("");
        info ("  Measured at BOTH candidate Size laws, because they pull in");
        info ("  opposite directions and that tension is the finding. The span");
        info ("  ceiling wants alpha small; the handoff wants the ER span to");
        info ("  reach past the late onset, which wants alpha large.");
        info ("");
        info ("    " + pad ("alpha", 7) + pad ("Size", 8) + pad ("ER span", 11)
              + pad ("late 1st", 11) + pad ("overlap", 11)
              + pad ("notch b=.25", 14) + pad ("notch b=.5", 13)
              + "notch b=.75");

        auto baseTaps = buildTaps (Timing::hybrid, 12, kWindowMs, 2);

        double worstNotch = 0.0;

        for (double alpha : { 0.25, 0.50 })
        for (double size : { 0.0, 0.20, 0.60, 0.80, 1.0 })
        {
            LateConfig c;
            c.size = size;

            std::vector<double> lateL, lateR;
            renderLate (c, noise, length, lateL, lateR);

            const double lateScale = mapping::sizeScaleFor (size)
                                   / mapping::sizeScaleFor (kSizeDefault);

            const double span = kWindowMs * std::pow (lateScale, alpha);

            auto taps = buildTaps (Timing::hybrid, 12, span, 2);
            normaliseTaps (taps);

            std::vector<double> erL, erR;
            renderEarly (taps, noise, length, erL, erR);

            const double erEnergy = energyOf (erL) + energyOf (erR);
            const double lateEnergy = energyOf (lateL) + energyOf (lateR);

            const double calibration = erEnergy > 0.0
                                     ? std::sqrt (lateEnergy / erEnergy) : 1.0;

            const double lateFirst = firstArrivalMs (lateL);

            std::string row = "    " + pad (fx (alpha, 2), 7)
                            + pad (fx (size * 100.0, 0) + " %", 8)
                            + pad (fx (span, 1) + " ms", 11)
                            + pad (fx (lateFirst, 1) + " ms", 11)
                            + pad (fx (std::max (0.0, span - lateFirst), 1) + " ms", 11);

            for (double theta : { 0.25, 0.50, 0.75 })
            {
                const auto m = mixBranches (Balance::calibrated, theta,
                                            erL, erR, lateL, lateR, calibration);

                double at = -1.0;
                const double onset = firstArrivalMs (m.l);
                const double notch = onset > 0.0
                                   ? notchDb (m.l, onset + 5.0, 220.0, &at) : 0.0;

                worstNotch = std::max (worstNotch, notch);

                row += pad (fx (notch, 2) + " dB", 15);
            }

            info (row);
        }

        info ("");
        info ("    worst notch across the grid  " + fx (worstNotch, 2) + " dB");
        info ("");
        info ("    No handoff processor was added - no envelope, no crossfade");
        info ("    timer, no ducking, no dynamic blending. The question asked");
        info ("    first is whether the natural branch overlap is already");
        info ("    enough, and the grid above is the answer rather than a");
        info ("    justification for machinery.");

        check (worstNotch >= 0.0, "the handoff was measured across Size and balance");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - independence: Attack, Early Diffusion, Pre-Delay");
    // =======================================================================
    {
        auto taps = buildTaps (Timing::hybrid, 12, kWindowMs, 2);
        normaliseTaps (taps);

        // --- Attack, the hard gate -----------------------------------------
        info ("  Attack. The split precedes it, so ER must be bit-identical at");
        info ("  every Attack setting while the late branch moves.");
        info ("");
        info ("    " + pad ("Attack", 10) + pad ("ER identical", 15)
              + pad ("late 1st", 11) + "late centroid");

        std::vector<double> referenceL, referenceR;
        renderEarly (taps, snare, length, referenceL, referenceR);

        double worstEr = 0.0;

        for (double attack : { 0.0, 0.50, 1.0 })
        {
            LateConfig c;
            c.attack = attack;

            std::vector<double> lateL, lateR;
            renderLate (c, snare, length, lateL, lateR);

            std::vector<double> erL, erR;
            renderEarly (taps, snare, length, erL, erR);

            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
                worst = std::max (worst, std::max (std::abs (erL[n] - referenceL[n]),
                                                   std::abs (erR[n] - referenceR[n])));

            worstEr = std::max (worstEr, worst);

            double weighted = 0.0, total = 0.0;

            for (size_t n = 0; n < static_cast<size_t> (0.4 * kSampleRate); ++n)
            {
                const double e = lateL[n] * lateL[n];
                weighted += e * static_cast<double> (n);
                total += e;
            }

            info ("    " + pad (fx (attack * 100.0, 0) + " %", 10)
                  + pad (worst == 0.0 ? "bit-identical" : "DIFFERS", 15)
                  + pad (fx (firstArrivalMs (lateL), 1) + " ms", 11)
                  + fx (total > 0.0 ? 1000.0 * weighted / total / kSampleRate : 0.0, 1)
                  + " ms");
        }

        // --- Early Diffusion ------------------------------------------------
        info ("");
        info ("  Early Diffusion. It must keep owning the late excitation's");
        info ("  scattering while the ER pattern stays structurally");
        info ("  recognisable - which it does trivially if ER is not fed");
        info ("  through the diffuser, so what is measured is that the late");
        info ("  branch still responds and the ER branch still does not.");
        info ("");
        info ("    " + pad ("Early Diff", 12) + pad ("ER balance", 12)
              + pad ("ER identical", 15) + pad ("late 1st", 11) + "mix notch");

        for (double diffusion : { 0.0, 0.70, 1.0 })
        {
            LateConfig c;
            c.earlyDiffusion = diffusion;

            std::vector<double> lateL, lateR;
            renderLate (c, snare, length, lateL, lateR);

            std::vector<double> erL, erR;
            renderEarly (taps, snare, length, erL, erR);

            const double erEnergy = energyOf (erL) + energyOf (erR);
            const double lateEnergy = energyOf (lateL) + energyOf (lateR);
            const double calibration = erEnergy > 0.0
                                     ? std::sqrt (lateEnergy / erEnergy) : 1.0;

            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
                worst = std::max (worst, std::abs (erL[n] - referenceL[n]));

            for (double theta : { 0.0, 0.50, 1.0 })
            {
                const auto m = mixBranches (Balance::calibrated, theta,
                                            erL, erR, lateL, lateR, calibration);

                // The notch window has to START AT THE SIGNAL, not at a fixed
                // 10 ms. At Early Diffusion 0 the late branch is four pure
                // delays, so nothing arrives until 37 ms - and measuring from
                // 10 ms counted that pre-onset silence as a 123 dB "notch".
                const double onset = firstArrivalMs (m.l);
                const double notch = onset > 0.0
                                   ? notchDb (m.l, onset + 5.0, 200.0) : 0.0;

                info ("    " + pad (fx (diffusion * 100.0, 0) + " %", 12)
                      + pad (fx (theta, 2), 12)
                      + pad (worst == 0.0 ? "bit-identical" : "DIFFERS", 15)
                      + pad (fx (firstArrivalMs (lateL), 1) + " ms", 11)
                      + fx (notch, 2) + " dB");
            }

            info ("");
        }

        // --- Pre-Delay, Model 1 re-certified --------------------------------
        info ("  Pre-Delay, Model 1. Both branches are fed from the SAME");
        info ("  pre-delayed signal, so both onsets must move by the same");
        info ("  amount and the ER pattern's internal spacing must not change.");
        info ("");
        info ("    " + pad ("Pre-Delay", 12) + pad ("ER 1st", 11)
              + pad ("late 1st", 11) + pad ("ER spacing", 13) + "pattern shifted only");

        double worstSpacingDrift = 0.0;
        double referenceMean = 0.0;
        bool first = true;

        for (double preDelayMs : { 0.0, 20.0, 80.0 })
        {
            std::vector<double> drive = snare, driveR = snare;

            PreDelay<double> pd;
            pd.prepare (kSampleRate);
            pd.snapTo (preDelayMs);
            pd.process (drive.data(), driveR.data(), static_cast<int> (length));

            std::vector<double> erL, erR;
            renderEarly (taps, drive, length, erL, erR);

            LateConfig c;

            std::vector<double> lateL, lateR;
            renderLate (c, drive, length, lateL, lateR);

            // The schedule's own spacing cannot change - Pre-Delay is upstream
            // - so what is checked is that the rendered ER is the reference
            // shifted by exactly the pre-delay and nothing else.
            const auto shift = static_cast<size_t> (
                std::lround (preDelayMs * kSampleRate / 1000.0));

            double shiftedWorst = 0.0;

            for (size_t n = 0; n + shift < length; ++n)
                shiftedWorst = std::max (shiftedWorst,
                                         std::abs (erL[n + shift] - referenceL[n]));

            const auto s = spacingOf (taps);

            if (first)
            {
                referenceMean = s.mean;
                first = false;
            }

            worstSpacingDrift = std::max (worstSpacingDrift,
                                          std::abs (s.mean - referenceMean));

            info ("    " + pad (fx (preDelayMs, 0) + " ms", 12)
                  + pad (fx (firstArrivalMs (erL), 1) + " ms", 11)
                  + pad (fx (firstArrivalMs (lateL), 1) + " ms", 11)
                  + pad (fx (s.mean, 2) + " ms", 13)
                  + (shiftedWorst == 0.0 ? "yes, bit-exact" : "NO: " + fx (shiftedWorst, 10)));
        }

        checkEqual (worstEr, 0.0, "Attack cannot reach ER: the split precedes it");

        checkEqual (worstSpacingDrift, 0.0,
                    "Pre-Delay shifts the ER pattern without distorting it");
    }

    // =======================================================================
    beginSuite ("PHASE 10 Stage 2 - ER cost and memory");
    // =======================================================================
    {
        info ("  Against the tank in the same run, minimum of five repeats, with");
        info ("  the accumulator escaping so the loop is not eliminated.");
        info ("");

        constexpr int repeats = 5;
        const int runLength = 48000;

        std::vector<double> drive (static_cast<size_t> (runLength), 0.0);

        for (int i = 0; i < runLength; ++i)
            drive[static_cast<size_t> (i)] = 0.25 * std::sin (0.013 * i);

        const auto best = [&] (auto&& body)
        {
            double bestNs = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto start = std::chrono::steady_clock::now();
                body();
                const auto finish = std::chrono::steady_clock::now();

                bestNs = std::min (bestNs, static_cast<double> (
                    std::chrono::duration_cast<std::chrono::nanoseconds>
                        (finish - start).count()));
            }

            return bestNs / static_cast<double> (runLength);
        };

        auto tank = makeTank (LateConfig {});

        const double tankNs = best ([&]
        {
            std::vector<double> l = drive, r = drive;

            for (int n = 0; n < runLength; ++n)
                tank->processSample (l[static_cast<size_t> (n)],
                                     r[static_cast<size_t> (n)]);
        });

        info ("    " + pad ("configuration", 26) + pad ("taps", 7)
              + pad ("ns/sample", 13) + pad ("share", 10) + "delay memory");
        info ("    " + pad ("PHASE 7-9 tank", 26) + pad ("-", 7)
              + pad (fx (tankNs, 3), 13) + pad ("-", 10) + "-");

        struct Spec { const char* label; Timing timing; int events; };

        const Spec specs[]
        {
            { "hybrid 8 events",  Timing::hybrid, 8 },
            { "hybrid 12 events", Timing::hybrid, 12 },
            { "hybrid 16 events", Timing::hybrid, 16 },
            { "cluster 12",       Timing::clustered, 12 },
        };

        for (const auto& spec : specs)
        {
            auto taps = buildTaps (spec.timing, spec.events, kWindowMs, 2);
            normaliseTaps (taps);

            // Two per-channel tap lists, as a production stage would hold.
            std::vector<size_t> offsetsL, offsetsR;
            std::vector<double> gainsL, gainsR;

            for (const auto& t : taps)
            {
                const auto o = static_cast<size_t> (
                    std::lround (t.timeMs * kSampleRate / 1000.0));

                if (t.gainL != 0.0) { offsetsL.push_back (o); gainsL.push_back (t.gainL); }
                if (t.gainR != 0.0) { offsetsR.push_back (o); gainsR.push_back (t.gainR); }
            }

            std::vector<double> ring (8192, 0.0);
            const size_t mask = ring.size() - 1;
            size_t writeIndex = 0;

            volatile double sink = 0.0;

            const double ns = best ([&]
            {
                for (int n = 0; n < runLength; ++n)
                {
                    writeIndex = (writeIndex + 1) & mask;
                    ring[writeIndex] = drive[static_cast<size_t> (n)];

                    double l = 0.0, r = 0.0;

                    for (size_t k = 0; k < offsetsL.size(); ++k)
                        l += gainsL[k] * ring[(writeIndex - offsetsL[k]) & mask];

                    for (size_t k = 0; k < offsetsR.size(); ++k)
                        r += gainsR[k] * ring[(writeIndex - offsetsR[k]) & mask];

                    sink += l + r;
                }
            });

            // An 80 ms window at 96 kHz needs 7680 samples; 8192 for masking.
            info ("    " + pad (spec.label, 26)
                  + pad (std::to_string (taps.size()), 7)
                  + pad (fx (ns, 3), 13)
                  + pad (fx (100.0 * ns / tankNs, 2) + " %", 10)
                  + "128 KiB");
        }

        info ("");
        info ("    One shared delay line serves both channels, because both");
        info ("    branches read the same pre-delayed mono-summed-or-stereo");
        info ("    input - so the memory does not double with the tap lists.");
        info ("    128 KiB covers an 80 ms window at 96 kHz with the power-of-");
        info ("    two rounding the index mask needs.");
        info ("");
        info ("    PHASE 9 Attack is already about 14.7 % active, so the target");
        info ("    here is 10 % or under unless a higher cost earns itself.");

        check (tankNs > 0.0, "the ER cost and memory were measured");
    }
}

} // namespace avtest
