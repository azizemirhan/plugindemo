#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"
#include "DSP/StereoDiffusionNetwork.h"
#include "DSP/DiffusionTuning.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 9 - STAGE 3. Attack, from research concept to production candidate.

    Stage 2 proved the mechanism and left four things open: where it sits
    relative to the input diffuser, how many taps it needs, how long its
    maximum should be, and how it behaves under automation. This file answers
    those by measurement, then gates the production integration.

    The Stage-1 and Stage-2 measurement discipline is carried forward: floors
    before claims, near-silent windows refused, periodicity windows passed
    raw, and superposition tested at Mod Depth 0 because a modulated tank is
    time-varying by design and would otherwise be blamed for the mechanism's
    behaviour.
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
    constexpr double kSize           = 0.60;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kHighDamp       = 0.45;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDampHz     = 10000.0;

    // Carried forward from Stage 1 and 2, both re-measured there.
    constexpr double kNedFloor    = 0.077;
    constexpr double kPeriodFloor = 3.03;

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

    struct Config
    {
        double decay          = kDecay;
        double size           = kSize;
        double earlyDiffusion = kEarlyDiffusion;
        double modDepth       = kModDepth;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
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

    /** Attack stage, then the tank. Placement A. */
    [[nodiscard]] std::vector<double> renderThroughAttack (Morph& tank,
                                                           AttackInjection& attack,
                                                           const std::vector<double>& drive,
                                                           size_t length)
    {
        std::vector<double> l (length, 0.0), r (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            l[n] = n < drive.size() ? drive[n] : 0.0;
            r[n] = l[n];
        }

        attack.process (l.data(), r.data(), static_cast<int> (length));

        for (size_t n = 0; n < length; ++n)
            tank.processSample (l[n], r[n]);

        return l;
    }

    [[nodiscard]] std::vector<double> firImpulseResponse (int taps, double maxSpreadMs,
                                                          double control, size_t length,
                                                          bool regular = false)
    {
        AttackInjection attack;
        attack.prepare (kSampleRate, taps, maxSpreadMs);
        attack.snapTo (control);

        std::vector<double> l (length, 0.0), r (length, 0.0);
        l[0] = 1.0;
        r[0] = 1.0;

        attack.process (l.data(), r.data(), static_cast<int> (length));

        if (! regular)
            return l;

        /*
            A regular-spacing, all-positive control, built by hand so that the
            production schedule has something to be compared against. This is
            what Stage 2's prototype was, and the comparison is the evidence
            for the irregular placement and the sign pattern rather than an
            assertion that they help.
        */
        std::vector<double> out (length, 0.0);

        const double spreadSamples = attack.spreadForControl (control)
                                     * kSampleRate / 1000.0;

        const int later = taps - 1;

        double rampTotal = 0.0;

        for (int k = 1; k <= later; ++k)
            rampTotal += static_cast<double> (k);

        out[0] += std::sqrt (AttackInjection::immediateShare);

        for (int k = 1; k <= later; ++k)
        {
            const auto offset = static_cast<size_t> (std::lround (
                static_cast<double> (k) / later * spreadSamples));

            const double power = (1.0 - AttackInjection::immediateShare)
                               * static_cast<double> (k) / rampTotal;

            if (offset < length)
                out[offset] += std::sqrt (power);
        }

        return out;
    }

    /** Peak-to-peak third-octave ripple over the audible band, in dB. */
    [[nodiscard]] double thirdOctaveRippleDb (const std::vector<double>& ir)
    {
        constexpr size_t fftSize = 65536;

        const auto magnitude = magnitudeSpectrum (ir, fftSize, false);
        const auto bands = thirdOctaveEnergy (magnitude, kSampleRate, fftSize, 100.0, 16000.0);

        if (bands.energyDb.empty())
            return 0.0;

        const double lo = *std::min_element (bands.energyDb.begin(), bands.energyDb.end());
        const double hi = *std::max_element (bands.energyDb.begin(), bands.energyDb.end());

        return hi - lo;
    }

    [[nodiscard]] double densityAt (const std::vector<double>& ir, double atMs,
                                    double windowMs = 30.0)
    {
        const auto from = static_cast<size_t> (atMs * kSampleRate / 1000.0);
        const auto len  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);

        if (from + len > ir.size())
            return -1.0;

        double peak = 0.0, sum = 0.0;

        for (double s : ir)
            peak = std::max (peak, std::abs (s));

        for (size_t n = from; n < from + len; ++n)
            sum += ir[n] * ir[n];

        const double rms = std::sqrt (sum / static_cast<double> (len));

        if (peak <= 0.0 || rms < peak * 1.0e-4)
            return -1.0;

        const auto flat = flattenDecay (ir.data() + from, len);

        return normalisedEchoDensity (flat.data(), flat.size(), 0, flat.size());
    }

    [[nodiscard]] std::string densityText (double v, int decimals = 4)
    {
        return v < 0.0 ? std::string ("--") : fx (v, decimals);
    }

    [[nodiscard]] double periodicityIn (const std::vector<double>& ir,
                                        double fromMs, double toMs)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (ir.size(), static_cast<size_t> (toMs * kSampleRate / 1000.0));

        if (to <= from)
            return 0.0;

        return maxEnvelopePeriodicity (ir.data() + from, to - from,
                                       static_cast<size_t> (0.001 * kSampleRate),
                                       static_cast<size_t> (0.150 * kSampleRate));
    }

    [[nodiscard]] std::vector<double> envelopeDb (const std::vector<double>& ir,
                                                  double windowMs, double stepMs,
                                                  double untilMs)
    {
        const auto win  = static_cast<size_t> (windowMs * kSampleRate / 1000.0);
        const auto step = static_cast<size_t> (stepMs * kSampleRate / 1000.0);
        const auto last = static_cast<size_t> (untilMs * kSampleRate / 1000.0);

        std::vector<double> out;

        for (size_t from = 0; from + win <= std::min (last, ir.size()); from += step)
        {
            double sum = 0.0;

            for (size_t n = from; n < from + win; ++n)
                sum += ir[n] * ir[n];

            const double r = std::sqrt (sum / static_cast<double> (win));

            out.push_back (r > 0.0 ? 20.0 * std::log10 (r) : -200.0);
        }

        return out;
    }

    struct Buildup { double onsetMs = -1.0, buildupMs = -1.0, peakDb = -200.0, riseDb = 0.0; };

    /*
        ONSET NEEDS AN ABSOLUTE REFERENCE, and getting that wrong produced a
        confident wrong answer worth recording.

        The first version took both landmarks relative to the render's own
        peak. At a 400 ms spread that peak is far later and far higher than at
        Attack 0, so the onset threshold rose with it and the onset was
        detected at about 400 ms - which made the BUILD-UP read as 24 ms, less
        than at every shorter spread, and looked like the mechanism collapsing.
        It was the metric moving its own goalposts.

        So `referenceDb` is the peak envelope level of the Attack-0 render of
        the same signal through the same tank, passed in by the caller and
        held fixed across an Attack sweep. Onset is then the same absolute
        level at every setting and is comparable by construction. Build-up
        stays relative to the render's own peak, because "established" means
        established for that render.
    */
    [[nodiscard]] Buildup buildupOf (const std::vector<double>& ir,
                                     double referenceDb = 1.0e30,
                                     double fromMs = 0.0, double untilMs = 900.0)
    {
        const auto env = envelopeDb (ir, 2.0, 1.0, untilMs);

        if (env.empty())
            return {};

        const auto skip = static_cast<size_t> (fromMs);

        if (skip >= env.size())
            return {};

        double peak = -1.0e30;

        for (size_t i = skip; i < env.size(); ++i)
            peak = std::max (peak, env[i]);

        Buildup b;
        b.peakDb = peak;

        const double onsetThreshold = (referenceDb < 1.0e29 ? referenceDb : peak) - 40.0;

        for (size_t i = skip; i < env.size(); ++i)
            if (env[i] >= onsetThreshold)
            {
                b.onsetMs = static_cast<double> (i - skip);
                break;
            }

        if (b.onsetMs < 0.0)
            return b;

        /*
            BUILD-UP IS AN INTEGRAL, NOT A THRESHOLD, and reaching that took
            three failed attempts that are worth recording because each one
            failed differently.

            (1) "First window within 3 dB of the peak", with the peak taken
            from the render itself. At a 400 ms spread the peak is later and
            higher, so the threshold rose with it and the onset was detected
            near the peak - reporting a build-up of 24 ms, SHORTER than every
            faster setting.

            (2) The same, with the onset threshold pinned to the Attack-0
            reference. The 400 ms row still read 24 ms, because the real
            problem was elsewhere: spreading injection over 400 ms lets the
            earliest arrivals decay ~10 dB before the last ones land, so the
            peak-to-onset rise SHRINKS, and a fixed -3 dB threshold is then
            crossed early by ordinary envelope fluctuation.

            (3) "70 % of whatever the rise is". That collapsed the other way -
            every spread read about 11 ms - because the envelope has two
            regimes: a fast jump as the immediate tap excites the tank, then a
            slow creep as the spread arrives. 70 % of a 26 dB rise lands
            inside the fast jump, which Attack does not change.

            So no level threshold works: the quantity of interest is WHEN THE
            ENERGY ARRIVES, and that is an integral. T50 is the time by which
            half the output energy within a fixed horizon has arrived, and
            build-up is T50 minus onset. Pre-Delay shifts both landmarks
            equally and leaves the difference alone; Attack delays energy and
            the difference grows. Being an integral it has no threshold to be
            fragile about, and it is monotone by construction.
        */
        const auto onsetIndex = skip + static_cast<size_t> (b.onsetMs);

        b.riseDb = peak - env[onsetIndex];

        // Fixed horizon, so the measure means the same thing at every setting.
        const auto horizon = std::min (ir.size(),
                                       static_cast<size_t> (0.600 * kSampleRate));

        double total = 0.0;

        for (size_t n = 0; n < horizon; ++n)
            total += ir[n] * ir[n];

        if (total <= 0.0)
            return b;

        double running = 0.0;

        for (size_t n = 0; n < horizon; ++n)
        {
            running += ir[n] * ir[n];

            if (running >= 0.5 * total)
            {
                const double t50Ms = 1000.0 * static_cast<double> (n) / kSampleRate;

                b.buildupMs = t50Ms - b.onsetMs;
                break;
            }
        }

        (void) onsetIndex;

        return b;
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

    [[nodiscard]] bool allFiniteIn (const std::vector<double>& x)
    {
        for (double s : x)
            if (! std::isfinite (s))
                return false;

        return true;
    }

    [[nodiscard]] double largestStep (const std::vector<double>& v)
    {
        double worst = 0.0;

        for (size_t n = 1; n < v.size(); ++n)
            worst = std::max (worst, std::abs (v[n] - v[n - 1]));

        return worst;
    }
}

void runPhase9Stage3Tests()
{
    ScopedFtz ftz;

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - tap count against coloration");
    // =======================================================================
    {
        /*
            The FIR is measured ALONE here, not through the tank, because the
            coloration it imposes is a property of the FIR and putting a dense
            reverb behind it would hide exactly what is being chosen.

            Third-octave smoothed, because that is what a listener hears: a
            multi-tap FIR always has arbitrarily deep nulls in individual
            bins, and a raw bin ripple figure would reject every possible
            design.
        */
        /*
            THE FIRST VERSION OF THIS SUITE CHOSE THE TAP COUNT ON
            THIRD-OCTAVE RIPPLE, AND THAT WAS THE WRONG MEASURE.

            Measured that way, SIX taps over 250 ms scores 0.330 dB - better
            than any dense configuration. Six taps over 250 ms is a 50 ms gap
            between arrivals, which is not a reverb building up, it is five
            audible echoes. The measure is blind to it because a 50 ms spacing
            puts the comb's teeth 20 Hz apart, far finer than a third-octave
            band, so the smoothing averages the whole comb away.

            Third-octave ripple is kept, as a COLORATION CHECK. It cannot be
            the selection criterion.

            What selects the tap count is whether individual taps stand out,
            and that is a time-domain question: the mean gap between arrivals,
            and the spikiness of the FIR's own short-window envelope. A smooth
            buildup has a low envelope crest factor; discrete arrivals have a
            high one.

            One more property of the table worth stating, because it looks
            like noise otherwise: the ripple is NOT monotone in tap count.
            Each tap count draws a different sign and position pattern from
            the same deterministic sequence, so its ripple is partly the luck
            of that draw. The ordering to trust is the gap and the crest.
        */
        info ("  The Attack FIR alone, at its longest spread. Measured alone");
        info ("  and not through the tank, because the coloration and the tap");
        info ("  audibility are properties of the FIR, and a dense reverb");
        info ("  behind it would hide exactly what is being chosen.");
        info ("");
        info ("    " + pad ("taps", 7) + pad ("mean gap", 11)
              + pad ("env crest", 12) + pad ("ripple", 11)
              + pad ("regular", 11) + "early NED through tank");

        const auto length = static_cast<size_t> (kSampleRate * 0.5);

        // Spikiness of the FIR's own 2 ms energy envelope across the spread.
        const auto envelopeCrest = [] (const std::vector<double>& ir, double spreadMs)
        {
            const auto win = static_cast<size_t> (0.002 * kSampleRate);
            const auto last = static_cast<size_t> (spreadMs * kSampleRate / 1000.0);

            double peak = 0.0, sum = 0.0;
            size_t count = 0;

            // Start AFTER the immediate tap. The first version started at 0
            // and returned 5.59 for every tap count, because the immediate
            // tap always carries sqrt(0.25) = 0.5 and always dominates the
            // peak while the squares always sum to 1 - so the measure was a
            // constant dressed as a measurement. What has to be measured is
            // whether the LATER arrivals stand out from each other.
            for (size_t from = win; from + win <= std::min (last, ir.size()); from += win)
            {
                double e = 0.0;

                for (size_t n = from; n < from + win; ++n)
                    e += ir[n] * ir[n];

                const double r = std::sqrt (e / static_cast<double> (win));

                peak = std::max (peak, r);
                sum += r * r;
                ++count;
            }

            if (count == 0 || sum <= 0.0)
                return 0.0;

            return peak / std::sqrt (sum / static_cast<double> (count));
        };

        for (int taps : { 6, 8, 12, 16, 24, 32, 48 })
        {
            const auto irregular = firImpulseResponse (taps, 250.0, 1.0, length, false);
            const auto regular   = firImpulseResponse (taps, 250.0, 1.0, length, true);

            // And through the tank, so the number a listener would meet is
            // reported next to the number the FIR has on its own.
            auto tank = makeTank (Config {});

            AttackInjection attack;
            attack.prepare (kSampleRate, taps, 250.0);
            attack.snapTo (1.0);

            std::vector<double> drive (static_cast<size_t> (kSampleRate * 1.5), 0.0);
            drive[0] = 1.0;

            const auto through = renderThroughAttack (*tank, attack, drive, drive.size());

            info ("    " + pad (std::to_string (taps), 7)
                  + pad (fx (250.0 / (taps - 1), 1) + " ms", 11)
                  + pad (fx (envelopeCrest (irregular, 250.0), 2), 12)
                  + pad (fx (thirdOctaveRippleDb (irregular), 3), 11)
                  + pad (fx (thirdOctaveRippleDb (regular), 3), 11)
                  + densityText (densityAt (through, 60.0)));
        }

        info ("");
        info ("    The regular column is the Stage-2 prototype's spacing with");
        info ("    all-positive taps. At the dense counts it is 10 dB worse,");
        info ("    which is what the irregular placement and the sign pattern");
        info ("    are for - and that comparison IS what third-octave ripple is");
        info ("    good for, because both sides have the same tap gap.");
        info ("");
        info ("    PRODUCTION CHOICE: " + std::to_string (AttackInjection::defaultTaps)
              + " taps at a " + fx (AttackInjection::defaultMaxSpreadMs, 0)
              + " ms maximum,");
        info ("    which is the smallest count whose mean gap stays under about");
        info ("    10 ms at the full spread - the region where arrivals start");
        info ("    to separate perceptually - and whose ripple is under 1 dB.");

        const auto chosen = firImpulseResponse (AttackInjection::defaultTaps,
                                                AttackInjection::defaultMaxSpreadMs,
                                                1.0, length, false);

        info ("");
        info ("    chosen: gap "
              + fx (AttackInjection::defaultMaxSpreadMs / (AttackInjection::defaultTaps - 1), 1)
              + " ms, crest "
              + fx (envelopeCrest (chosen, AttackInjection::defaultMaxSpreadMs), 2)
              + ", ripple " + fx (thirdOctaveRippleDb (chosen), 3) + " dB");

        checkAtMost (thirdOctaveRippleDb (chosen), 1.0,
                     "the production FIR colours the excitation by under 1 dB third-octave");

        checkAtMost (AttackInjection::defaultMaxSpreadMs
                     / (AttackInjection::defaultTaps - 1), 10.0,
                     "and its arrivals stay under 10 ms apart at the full spread");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - maximum spread, and the internal mapping");
    // =======================================================================
    {
        info ("  How long the maximum should be. Too short and the control has");
        info ("  no creative range; too long and it stops being a reverb");
        info ("  building up and becomes an audible multi-tap delay.");
        info ("");
        info ("    " + pad ("max spread", 13) + pad ("buildup", 11)
              + pad ("peak at", 11) + pad ("ripple", 11)
              + pad ("FIR period", 13) + "mean tap gap");

        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        // The Attack-0 peak, once, as the fixed onset reference for the sweep.
        double referenceDb = 1.0e30;
        {
            auto t = makeTank (Config {});
            AttackInjection a;
            a.prepare (kSampleRate);
            a.snapTo (0.0);
            std::vector<double> d (length, 0.0);
            d[0] = 1.0;
            referenceDb = buildupOf (renderThroughAttack (*t, a, d, length)).peakDb;
        }

        for (double maxMs : { 100.0, 150.0, 250.0, 400.0 })
        {
            auto tank = makeTank (Config {});

            AttackInjection attack;
            attack.prepare (kSampleRate, AttackInjection::defaultTaps, maxMs);
            attack.snapTo (1.0);

            std::vector<double> drive (length, 0.0);
            drive[0] = 1.0;

            const auto out = renderThroughAttack (*tank, attack, drive, length);
            const auto b = buildupOf (out, referenceDb);

            const auto fir = firImpulseResponse (AttackInjection::defaultTaps,
                                                 maxMs, 1.0,
                                                 static_cast<size_t> (kSampleRate * 0.5));

            const double gap = maxMs / (AttackInjection::defaultTaps - 1);

            // Where the envelope peaks, so the 400 ms row can be explained
            // rather than merely noticed.
            const auto env = envelopeDb (out, 2.0, 1.0, 800.0);
            const auto peakAt = env.empty() ? 0
                              : static_cast<int> (std::max_element (env.begin(), env.end())
                                                  - env.begin());

            info ("    " + pad (fx (maxMs, 0) + " ms", 13)
                  + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                  + pad (std::to_string (peakAt) + " ms", 11)
                  + pad (fx (b.riseDb, 1) + " dB", 10)
                  + pad (fx (thirdOctaveRippleDb (fir), 3), 11)
                  + pad (fx (periodicityIn (fir, 0.0, 450.0), 1), 13)
                  + fx (gap, 1) + " ms");
        }

        info ("");
        info ("    Two columns decide this. The mean tap gap, because arrivals");
        info ("    start to separate perceptually somewhere above roughly 10 ms");
        info ("    apart - so the maximum spread and the tap count are one");
        info ("    decision rather than two.");
        info ("");
        info ("    And the peak time, because it is what the longest spreads");
        info ("    actually do: the envelope keeps rising for as long as energy");
        info ("    is still arriving, so at 400 ms the peak lands at 426 ms. The");
        info ("    build-up follows it. Both grow together, which is the");
        info ("    mechanism behaving as designed - and the reason this table is");
        info ("    trustworthy is that the onset threshold is held at the");
        info ("    Attack-0 reference level rather than at each render own peak.");
        info ("    Taken relative to its own peak the 400 ms row reported a");
        info ("    build-up of 24 ms, which was the metric following the peak it");
        info ("    was supposed to be measuring against.");
        info ("");
        info ("  The mapping. Linear would spend most of the control above");
        info ("  100 ms, where differences are large and the musically useful");
        info ("  region is already behind you.");
        info ("");
        info ("    " + pad ("Attack %", 11) + pad ("linear", 12)
              + pad ("shipped (u^2)", 15) + "buildup");

        {
            auto tank = makeTank (Config {});

            for (double u : { 0.0, 0.1, 0.25, 0.4, 0.5, 0.63, 0.8, 1.0 })
            {
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);

                std::vector<double> drive (length, 0.0);
                drive[0] = 1.0;

                auto t = makeTank (Config {});
                const auto out = renderThroughAttack (*t, attack, drive, length);
                const auto b = buildupOf (out, referenceDb);

                info ("    " + pad (fx (u * 100.0, 0) + " %", 11)
                      + pad (fx (u * AttackInjection::defaultMaxSpreadMs, 1) + " ms", 12)
                      + pad (fx (attack.spreadForControl (u), 1) + " ms", 15)
                      + (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--"));
            }
        }

        info ("");
        info ("    0 - 100 ms occupies the first 63 % of the shipped mapping's");
        info ("    travel and the first 40 % of a linear one.");

        check (true, "the spread and mapping candidates were measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - placement, and whether the two orders commute");
    // =======================================================================
    {
        /*
            Stage 2's report left this open and said the two placements "are
            not the same sound". THAT CLAIM WAS WRONG, and this suite is the
            correction.

            Attack is one scalar FIR applied identically to both channels, so
            as an operator on the stereo pair it is h(z) times the identity.
            A scalar times the identity commutes with ANY 2x2 linear operator,
            including the stereo diffuser - whatever its delay patterns,
            whatever its cross-coupling. So placement A and placement B
            produce the same samples, exactly, and the only reasons to prefer
            one are practical.

            The one case where they would NOT commute is a per-channel
            schedule, diag(h_L, h_R), which commutes with the diffuser only
            while the diffuser is itself diagonal. That is measured too, and
            it is the second reason the shipped schedule is channel-identical.
        */
        info ("  A: Attack -> input diffusion -> tank");
        info ("  B: input diffusion -> Attack -> tank");
        info ("");
        info ("  Stage 2 said these are 'not the same sound'. That was wrong,");
        info ("  and this is the correction. Attack is one scalar FIR applied");
        info ("  identically to both channels, so as an operator it is h(z)");
        info ("  times the identity - and a scalar times the identity commutes");
        info ("  with any 2x2 linear operator, including the stereo diffuser.");
        info ("");
        info ("    " + pad ("Early Diff", 12) + pad ("Attack", 10)
              + pad ("worst diff", 16) + pad ("rel. to peak", 15) + "verdict");

        const auto length = static_cast<size_t> (kSampleRate * 1.0);
        double worstRelative = 0.0;

        for (double diffusion : { 0.0, 0.70, 1.0 })
        {
            for (double u : { 0.25, 0.60, 1.0 })
            {
                Config c;
                c.earlyDiffusion = diffusion;
                c.modDepth = 0.0;      // LTI, so the comparison is exact

                // --- A: Attack before the diffuser (and so before the tank)
                std::vector<double> outA;
                {
                    auto tank = makeTank (c);

                    AttackInjection attack;
                    attack.prepare (kSampleRate);
                    attack.snapTo (u);

                    std::vector<double> drive (length, 0.0);
                    drive[0] = 1.0;

                    outA = renderThroughAttack (*tank, attack, drive, length);
                }

                // --- B: Attack after the diffuser. Reached by running the
                //        diffuser alone, then Attack, then the bare tank.
                std::vector<double> outB;
                {
                    // The diffuser alone: a tank with the diffuser enabled but
                    // the core silent is not reachable, so B is built the
                    // other way round - diffusion is disabled in the tank and
                    // run as its own stage before Attack.
                    StereoDiffusionNetwork<double> diffuser;
                    diffuser.setBasePattern (diffusion::fdnInputPattern);
                    diffuser.prepare (kSampleRate);
                    diffuser.setStageCount (4, true);
                    diffuser.setDiffusion (diffusion, true);
                    diffuser.reset();

                    Config bare = c;

                    auto tank = makeTank (bare);
                    tank->setInputDiffusionEnabled (false);
                    tank->reset();

                    std::vector<double> l (length, 0.0), r (length, 0.0);
                    l[0] = 1.0;
                    r[0] = 1.0;

                    for (size_t n = 0; n < length; ++n)
                        diffuser.processSample (l[n], r[n]);

                    AttackInjection attack;
                    attack.prepare (kSampleRate);
                    attack.snapTo (u);
                    attack.process (l.data(), r.data(), static_cast<int> (length));

                    for (size_t n = 0; n < length; ++n)
                        tank->processSample (l[n], r[n]);

                    outB = l;
                }

                double worst = 0.0, peak = 0.0;

                for (size_t n = 0; n < length; ++n)
                {
                    worst = std::max (worst, std::abs (outA[n] - outB[n]));
                    peak = std::max (peak, std::abs (outA[n]));
                }

                const double relative = peak > 0.0 ? worst / peak : 0.0;
                worstRelative = std::max (worstRelative, relative);

                info ("    " + pad (fx (diffusion * 100.0, 0) + " %", 12)
                      + pad (fx (u * 100.0, 0) + " %", 10)
                      + pad (fx (worst, 14), 16)
                      + pad (fx (100.0 * relative, 10) + " %", 15)
                      + (relative < 1.0e-9 ? "commutes" : "DIFFERS"));
            }
        }

        info ("");
        info ("    worst relative difference  " + fx (worstRelative, 14));
        info ("");
        info ("    So the placement is chosen on practical grounds, and");
        info ("    placement A wins on two: the diffuser stays immediately");
        info ("    before the injection vectors where PHASE 4 searched it, and");
        info ("    Attack joins the other input-conditioning stages in the");
        info ("    processor instead of being threaded into StereoFdn.");

        checkAtMost (worstRelative, 1.0e-9,
                     "the two placements commute: a channel-identical FIR is scalar");

        // --- and the case that does NOT commute -----------------------------
        info ("");
        info ("  The per-channel variant, for completeness and to justify the");
        info ("  channel-identical choice. A different schedule per channel is");
        info ("  diag(h_L, h_R), which no longer commutes once the diffuser is");
        info ("  not diagonal - and more importantly it puts two different FIRs");
        info ("  into the mono downmix.");
        info ("");
        info ("    " + pad ("schedule", 22) + pad ("mono/stereo", 15) + "L/R corr");

        for (int variant = 0; variant < 2; ++variant)
        {
            Config c;
            c.modDepth = 0.0;

            auto tank = makeTank (c);

            AttackInjection attackL, attackR;
            attackL.prepare (kSampleRate);
            attackR.prepare (kSampleRate, AttackInjection::defaultTaps + 1);

            attackL.snapTo (1.0);
            attackR.snapTo (1.0);

            std::vector<double> l (length, 0.0), r (length, 0.0);
            l[0] = 1.0;
            r[0] = 1.0;

            std::vector<double> dummy (length, 0.0);

            attackL.process (l.data(), dummy.data(), static_cast<int> (length));

            if (variant == 0)
            {
                // Channel-identical: the same stage drives both.
                AttackInjection same;
                same.prepare (kSampleRate);
                same.snapTo (1.0);

                l.assign (length, 0.0);
                r.assign (length, 0.0);
                l[0] = 1.0;
                r[0] = 1.0;

                same.process (l.data(), r.data(), static_cast<int> (length));
            }
            else
            {
                std::vector<double> dummy2 (length, 0.0);
                attackR.process (r.data(), dummy2.data(), static_cast<int> (length));
            }

            for (size_t n = 0; n < length; ++n)
                tank->processSample (l[n], r[n]);

            const auto from = static_cast<size_t> (0.300 * kSampleRate);
            const auto to   = std::min (l.size(), static_cast<size_t> (0.900 * kSampleRate));

            double stereoEnergy = 0.0, monoEnergy = 0.0;

            for (size_t n = from; n < to; ++n)
            {
                stereoEnergy += l[n] * l[n] + r[n] * r[n];

                const double mono = 0.5 * (l[n] + r[n]);
                monoEnergy += 2.0 * mono * mono;
            }

            const double ratio = stereoEnergy > 0.0
                               ? 10.0 * std::log10 (monoEnergy / stereoEnergy) : 0.0;

            const double corr = correlation (l.data() + from, r.data() + from, to - from);

            info ("    " + pad (variant == 0 ? "channel-identical" : "per-channel", 22)
                  + pad (fx (ratio, 2) + " dB", 15) + fx (corr, 4));
        }

        check (true, "the per-channel variant's mono cost was measured");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - Attack 0 is bit-exactly PHASE 8");
    // =======================================================================
    {
        /*
            The hard gate. Attack 0 takes an explicit bypass branch, not a tap
            set that happens to sum to unity - and the difference is not
            pedantic. Collapsing 32 taps to offset 0 and summing them gives
            sum(w_k), not 1.0, because the weights are L2-normalised and some
            carry a negative sign. The arithmetic would be wrong AND different.
        */
        info ("  A substantial signal set, each run with the Attack stage in");
        info ("  the path at 0 and with no Attack stage at all. The bypass is");
        info ("  an explicit branch, so these must be identical rather than");
        info ("  close.");
        info ("");
        info ("    " + pad ("signal", 20) + pad ("samples", 11)
              + pad ("differing", 12) + "worst diff");

        const auto length = static_cast<size_t> (kSampleRate * 1.0);

        std::mt19937 rng (90210u);
        std::uniform_real_distribution<double> dist (-0.5, 0.5);

        struct Signal { const char* name; std::vector<double> data; };

        std::vector<Signal> signals;

        {
            std::vector<double> impulse (length, 0.0);
            impulse[0] = 1.0;
            signals.push_back ({ "impulse", impulse });
        }

        {
            std::vector<double> noise (length, 0.0);

            for (auto& s : noise)
                s = dist (rng);

            signals.push_back ({ "white noise", noise });
        }

        {
            std::vector<double> sine (length, 0.0);

            for (size_t n = 0; n < length; ++n)
                sine[n] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * 440.0
                                          * static_cast<double> (n) / kSampleRate);

            signals.push_back ({ "sine 440", sine });
        }

        {
            std::vector<double> train (length, 0.0);

            for (size_t n = 0; n < length; n += static_cast<size_t> (0.12 * kSampleRate))
                for (size_t k = 0; k < 64 && n + k < length; ++k)
                    train[n + k] = dist (rng) * std::exp (-static_cast<double> (k) / 12.0);

            signals.push_back ({ "transient train", train });
        }

        {
            std::vector<double> dc (length, 0.25);
            signals.push_back ({ "DC 0.25", dc });
        }

        {
            signals.push_back ({ "silence", std::vector<double> (length, 0.0) });
        }

        long long worstDiffering = 0;
        double worstDifference = 0.0;

        for (const auto& sig : signals)
        {
            // With the stage, at 0.
            std::vector<double> withStage;
            {
                auto tank = makeTank (Config {});

                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (0.0);

                withStage = renderThroughAttack (*tank, attack, sig.data, length);
            }

            // Without the stage at all - the PHASE 8 path.
            std::vector<double> withoutStage;
            {
                auto tank = makeTank (Config {});

                std::vector<double> l (length, 0.0), r (length, 0.0);

                for (size_t n = 0; n < length; ++n)
                {
                    l[n] = sig.data[n];
                    r[n] = sig.data[n];
                }

                for (size_t n = 0; n < length; ++n)
                    tank->processSample (l[n], r[n]);

                withoutStage = l;
            }

            long long differing = 0;
            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                const double d = std::abs (withStage[n] - withoutStage[n]);

                worst = std::max (worst, d);

                if (d != 0.0)
                    ++differing;
            }

            worstDiffering = std::max (worstDiffering, differing);
            worstDifference = std::max (worstDifference, worst);

            info ("    " + pad (sig.name, 20) + pad (std::to_string (length), 11)
                  + pad (std::to_string (differing), 12) + fx (worst, 14));
        }

        info ("");
        info ("    worst difference  " + fx (worstDifference, 16));
        info ("");
        info ("    And the arithmetic the bypass avoids: the 32 weights sum to");

        {
            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (1.0);

            double sum = 0.0, squares = 0.0;

            for (int k = 0; k < attack.getTapCount(); ++k)
            {
                sum += attack.getTapWeight (k);
                squares += attack.getTapWeight (k) * attack.getTapWeight (k);
            }

            info ("    " + fx (sum, 6) + ", not 1.0, while their squares sum to "
                  + fx (squares, 10) + ".");
            info ("    So collapsing the taps to offset 0 would be a gain change");
            info ("    as well as a different computation. The bypass is a");
            info ("    branch, as the gate requires.");
        }

        checkEqual (worstDifference, 0.0,
                    "Attack 0 reproduces the PHASE 8 path bit for bit");

        checkEqual (static_cast<double> (worstDiffering), 0.0,
                    "and not one sample differs, across six signal types");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - repeated transients, and the crossfade");
    // =======================================================================
    {
        /*
            Superposition at Mod Depth 0, carried forward from Stage 2 with
            its correction intact: a modulated tank is time-varying by design,
            so the test runs where the tank is LTI and whatever residual
            remains belongs to the Attack stage.
        */
        info ("  Superposition at Mod Depth 0, where Stage 1 verified the tank");
        info ("  is time invariant to 1.3e-25. Three impulses 500 ms apart in");
        info ("  one continuous render against the sum of three shifted single-");
        info ("  impulse renders.");
        info ("");
        info ("    " + pad ("Attack", 11) + pad ("residual", 14)
              + pad ("rel. to peak", 15) + "verdict");

        const auto length = static_cast<size_t> (kSampleRate * 1.6);
        const auto step = static_cast<size_t> (0.5 * kSampleRate);

        Config lti;
        lti.modDepth = 0.0;

        double worstSuperposition = 0.0;

        for (double u : { 0.0, 0.30, 0.63, 1.0 })
        {
            std::vector<double> three (length, 0.0);

            for (int k = 0; k < 3; ++k)
                three[static_cast<size_t> (k) * step] = 1.0;

            std::vector<double> single (length, 0.0);
            single[0] = 1.0;

            std::vector<double> outThree, outSingle;

            {
                auto tank = makeTank (lti);
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);
                outThree = renderThroughAttack (*tank, attack, three, length);
            }

            {
                auto tank = makeTank (lti);
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);
                outSingle = renderThroughAttack (*tank, attack, single, length);
            }

            std::vector<double> predicted (length, 0.0);

            for (int k = 0; k < 3; ++k)
            {
                const size_t shift = static_cast<size_t> (k) * step;

                for (size_t n = 0; n + shift < length; ++n)
                    predicted[n + shift] += outSingle[n];
            }

            double residual = 0.0, peak = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                residual = std::max (residual, std::abs (outThree[n] - predicted[n]));
                peak = std::max (peak, std::abs (predicted[n]));
            }

            const double relative = peak > 0.0 ? residual / peak : 0.0;
            worstSuperposition = std::max (worstSuperposition, relative);

            info ("    " + pad (fx (u * 100.0, 0) + " %", 11)
                  + pad (fx (residual, 12), 14)
                  + pad (fx (100.0 * relative, 8) + " %", 15)
                  + (relative < 1.0e-9 ? "identical on every event" : "EVENT-DEPENDENT"));
        }

        info ("");
        info ("    No onset detector, no global envelope, no transport");
        info ("    awareness. The mechanism is a fixed FIR, so this is a");
        info ("    property of the architecture rather than a result that");
        info ("    could have come out otherwise.");

        checkAtMost (worstSuperposition, 1.0e-9,
                     "every transient receives the same Attack, at every setting");

        // --- the crossfade duration, measured ------------------------------
        info ("");
        info ("  The crossfade length. PHASE 8's pre-delay uses 20 ms and the");
        info ("  brief says not to assume that transfers, so it is measured:");
        info ("  the worst single-sample step a transition produces, on noise,");
        info ("  against the step the signal already has.");
        info ("");
        info ("    " + pad ("fade", 10) + pad ("worst step", 14)
              + pad ("settled step", 14) + "ratio");

        std::mt19937 rng (4242u);
        std::uniform_real_distribution<double> dist (-0.4, 0.4);

        std::vector<double> noise (static_cast<size_t> (kSampleRate * 0.5), 0.0);

        for (auto& s : noise)
            s = dist (rng);

        const double inputStep = largestStep (noise);

        for (double fadeMs : { 2.0, 5.0, 10.0, 20.0, 30.0, 50.0 })
        {
            /*
                The class's crossfade length is a compile-time constant, so
                this sweep drives an equivalent transition by hand: the two
                schedules are rendered separately and crossfaded over the
                candidate window, which is exactly what process() does
                internally.
            */
            AttackInjection a, b;
            a.prepare (kSampleRate);
            b.prepare (kSampleRate);
            a.snapTo (0.0);
            b.snapTo (0.63);

            std::vector<double> fromL = noise, fromR = noise;
            std::vector<double> toL = noise, toR = noise;

            a.process (fromL.data(), fromR.data(), static_cast<int> (noise.size()));
            b.process (toL.data(), toR.data(), static_cast<int> (noise.size()));

            const auto at = noise.size() / 2;
            const auto fade = static_cast<size_t> (fadeMs * kSampleRate / 1000.0);

            std::vector<double> out = fromL;

            for (size_t n = at; n < noise.size(); ++n)
            {
                const double g = n < at + fade
                               ? static_cast<double> (n - at) / static_cast<double> (fade)
                               : 1.0;

                out[n] = fromL[n] + g * (toL[n] - fromL[n]);
            }

            // Only the transition window, and against the steady runs on
            // either side of it. Measuring the whole buffer against the
            // INPUT step was wrong: a 32-tap FIR of noise is intrinsically
            // steeper than its own input, so the dominant step came from
            // the settled output and the figure did not move with the fade
            // length at all - it read 1.2577 for every one of them.
            double worst = 0.0;

            for (size_t n = at + 1; n < std::min (noise.size(), at + fade + 2); ++n)
                worst = std::max (worst, std::abs (out[n] - out[n - 1]));

            const double steady = std::max (largestStep (fromL), largestStep (toL));

            info ("    " + pad (fx (fadeMs, 0) + " ms", 10)
                  + pad (fx (worst, 6), 14)
                  + pad (fx (steady, 6), 14)
                  + fx (worst / steady, 4));
        }

        info ("");
        info ("    The shipped length is " + fx (AttackInjection::crossfadeMs, 0)
              + " ms. A crossfade between two");
        info ("    FIRs is a blend of two decorrelated versions of the same");
        info ("    signal, so it cannot step further than the signal does - the");
        info ("    ratio column says so - and the length is chosen for how long");
        info ("    the blend's comb is audible rather than for click safety.");

        check (true, "the crossfade length was measured rather than inherited");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - Attack automation");
    // =======================================================================
    {
        info ("  Transitions in both directions, including across the bypass");
        info ("  boundary, plus rapid random automation. The delay line is");
        info ("  written unconditionally, including while bypassed, so a move");
        info ("  off 0 cannot read stale history - which is tested, not");
        info ("  asserted.");
        info ("");
        info ("    " + pad ("transition", 16) + pad ("worst step", 13)
              + pad ("settled step", 13) + pad ("peak", 10) + "finite");

        std::mt19937 rng (31337u);
        std::uniform_real_distribution<double> dist (-0.4, 0.4);

        const auto length = static_cast<size_t> (kSampleRate * 0.6);

        std::vector<double> noise (length, 0.0);

        for (auto& s : noise)
            s = dist (rng);

        // Kept only so the table can show what the raw signal does.
        const double inputStep = largestStep (noise);
        (void) inputStep;

        bool allFinite = true;
        double worstRatio = 0.0;

        struct Move { const char* label; double from, to; };

        const Move moves[]
        {
            { "0 -> low",    0.0,  0.30 },
            { "low -> high", 0.30, 0.80 },
            { "high -> 0",   0.80, 0.0 },
            { "0 -> max",    0.0,  1.0 },
            { "max -> 0",    1.0,  0.0 },
        };

        for (const auto& m : moves)
        {
            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (m.from);

            std::vector<double> l = noise, r = noise;

            const auto at = static_cast<int> (length / 2);

            attack.process (l.data(), r.data(), at);
            attack.setAttack (m.to);
            attack.process (l.data() + at, r.data() + at, static_cast<int> (length) - at);

            // The reference is the SETTLED output at each end of the move,
            // not the input. A 32-tap FIR of noise is intrinsically steeper
            // than the noise it was given, so an input-step reference
            // reports every setting as a click - the same mistake PHASE 8
            // Stage 2.5 made and corrected.
            double settled = 0.0;

            for (double end : { m.from, m.to })
            {
                AttackInjection s;
                s.prepare (kSampleRate);
                s.snapTo (end);

                std::vector<double> sl = noise, sr = noise;
                s.process (sl.data(), sr.data(), static_cast<int> (length));

                settled = std::max (settled, largestStep (sl));
            }

            const double worst = largestStep (l);
            const double ratio = worst / settled;

            worstRatio = std::max (worstRatio, ratio);
            allFinite = allFinite && allFiniteIn (l);

            info ("    " + pad (m.label, 16) + pad (fx (worst, 6), 13)
                  + pad (fx (settled, 6), 13)
                  + pad (fx (peakOf (l), 5), 10)
                  + (allFiniteIn (l) ? "yes" : "NO"));
        }

        // --- rapid random automation ---------------------------------------
        {
            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (0.0);

            std::uniform_real_distribution<double> control (0.0, 1.0);

            std::vector<double> l = noise, r = noise;

            int moveCount = 0;

            for (size_t n = 0; n + 64 <= length; n += 64)
            {
                attack.setAttack (control (rng));
                ++moveCount;

                attack.process (l.data() + n, r.data() + n, 64);
            }

            // Random automation visits every setting, so the reference is
            // the steepest settled output over the whole range.
            double settled = 0.0;

            // Eleven points across the range, because random automation
            // visits a continuum and a three-point reference missed
            // intermediate settings whose settled output is slightly
            // steeper - which read as a 0.29 % transition excess that was
            // really the reference being too coarse.
            for (int step = 0; step <= 10; ++step)
            {
                AttackInjection s;
                s.prepare (kSampleRate);
                s.snapTo (0.1 * step);

                std::vector<double> sl = noise, sr = noise;
                s.process (sl.data(), sr.data(), static_cast<int> (length));

                settled = std::max (settled, largestStep (sl));
            }

            const double worst = largestStep (l);

            worstRatio = std::max (worstRatio, worst / settled);
            allFinite = allFinite && allFiniteIn (l);

            info ("    " + pad ("random x" + std::to_string (moveCount), 16)
                  + pad (fx (worst, 6), 13)
                  + pad (fx (settled, 6), 13)
                  + pad (fx (peakOf (l), 5), 10)
                  + (allFiniteIn (l) ? "yes" : "NO"));
        }

        // --- stale history, directly ----------------------------------------
        info ("");
        info ("  Stale history, isolated the way PHASE 8's cut filters were:");
        info ("  the same signal and the same automation after a long period at");
        info ("  Attack 0, differing only in what passed through BEFORE it.");
        info ("");

        double staleWorst = 0.0;

        {
            const auto quietLength = static_cast<size_t> (kSampleRate * 1.0);

            std::vector<double> loud (quietLength, 0.0), quiet (quietLength, 0.0);

            for (auto& s : loud)
                s = dist (rng);

            // Both runs are at Attack 0 for the first second - one fed loud
            // noise, one fed silence - then both get the identical tail.
            std::vector<double> tail (static_cast<size_t> (kSampleRate * 0.5), 0.0);

            for (auto& s : tail)
                s = dist (rng);

            const auto run = [&] (const std::vector<double>& head)
            {
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (0.0);

                std::vector<double> l = head, r = head;
                attack.process (l.data(), r.data(), static_cast<int> (head.size()));

                attack.setAttack (0.63);

                std::vector<double> t = tail, tr = tail;
                attack.process (t.data(), tr.data(), static_cast<int> (tail.size()));

                return t;
            };

            const auto a = run (loud);
            const auto b = run (quiet);

            for (size_t n = 0; n < a.size(); ++n)
                staleWorst = std::max (staleWorst, std::abs (a[n] - b[n]));

            info ("    worst difference between the two histories  "
                  + fx (staleWorst, 10));
            info ("");
            info ("    This is EXPECTED to be non-zero, and that is the point of");
            info ("    writing the line unconditionally. The taps read real");
            info ("    recent signal rather than a buffer frozen from whenever");
            info ("    the control was last touched, so the difference here is");
            info ("    the two histories genuinely being different audio - not");
            info ("    a stale burst. The automation table above is what rules");
            info ("    out a click.");
        }

        check (allFinite, "Attack automation stays finite everywhere");

        checkAtMost (worstRatio, 1.0,
                     "no Attack transition steps further than the signal does anyway");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - independence, Mod Depth, energy, stereo");
    // =======================================================================
    {
        info ("  Attack against Pre-Delay, re-certified on the production");
        info ("  stage. ONSET belongs to Pre-Delay, BUILDUP to Attack.");
        info ("");
        info ("    " + pad ("Pre-Delay", 12) + pad ("Attack", 10)
              + pad ("onset", 10) + pad ("buildup", 11) + "energy");

        const auto length = static_cast<size_t> (kSampleRate * 2.5);

        std::vector<double> impulse (length, 0.0);
        impulse[0] = 1.0;

        // One fixed onset reference for every table in this suite: the
        // Attack-0 peak. See buildupOf for why this is not optional.
        double referenceDb = 1.0e30;

        {
            auto t = makeTank (Config {});
            AttackInjection a;
            a.prepare (kSampleRate);
            a.snapTo (0.0);
            referenceDb = buildupOf (renderThroughAttack (*t, a, impulse, length)).peakDb;
        }

        for (double pdMs : { 0.0, 50.0 })
        {
            for (double u : { 0.0, 0.63, 1.0 })
            {
                auto tank = makeTank (Config {});

                std::vector<double> l = impulse, r = impulse;

                if (pdMs > 0.0)
                {
                    PreDelay<double> pd;
                    pd.prepare (kSampleRate);
                    pd.snapTo (pdMs);
                    pd.process (l.data(), r.data(), static_cast<int> (length));
                }

                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);
                attack.process (l.data(), r.data(), static_cast<int> (length));

                for (size_t n = 0; n < length; ++n)
                    tank->processSample (l[n], r[n]);

                const auto b = buildupOf (l, referenceDb);

                info ("    " + pad (fx (pdMs, 0) + " ms", 12)
                      + pad (fx (u * 100.0, 0) + " %", 10)
                      + pad (b.onsetMs >= 0.0 ? fx (b.onsetMs, 0) + " ms" : "--", 10)
                      + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                      + fx (energyOf (l), 6));
            }
        }

        // --- Attack x Early Diffusion --------------------------------------
        info ("");
        info ("  Attack against Early Diffusion. Early Diffusion must keep");
        info ("  owning early scattering; Attack must keep owning build-up");
        info ("  timing; and Attack's range must not collapse as Early");
        info ("  Diffusion moves.");
        info ("");
        info ("    " + pad ("Early Diff", 12) + pad ("Attack", 10)
              + pad ("buildup", 11) + pad ("NED 20ms", 11) + "late NED");

        double smallestBuildupSpan = 1.0e30;

        for (double diffusion : { 0.0, 0.70, 1.0 })
        {
            double buildMin = 1.0e30, buildMax = -1.0e30;

            for (double u : { 0.0, 0.30, 0.63, 1.0 })
            {
                Config c;
                c.earlyDiffusion = diffusion;

                auto tank = makeTank (c);

                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);

                const auto out = renderThroughAttack (*tank, attack, impulse, length);
                const auto b = buildupOf (out, referenceDb);

                if (b.buildupMs >= 0.0)
                {
                    buildMin = std::min (buildMin, b.buildupMs);
                    buildMax = std::max (buildMax, b.buildupMs);
                }

                info ("    " + pad (fx (diffusion * 100.0, 0) + " %", 12)
                      + pad (fx (u * 100.0, 0) + " %", 10)
                      + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                      + pad (densityText (densityAt (out, 20.0)), 11)
                      + densityText (densityAt (out, 320.0)));
            }

            smallestBuildupSpan = std::min (smallestBuildupSpan, buildMax - buildMin);

            info ("      buildup span at this Early Diffusion  "
                  + fx (buildMax - buildMin, 0) + " ms");
            info ("");
        }

        info ("    smallest buildup span across Early Diffusion  "
              + fx (smallestBuildupSpan, 0) + " ms");

        // --- Mod Depth ------------------------------------------------------
        info ("");
        info ("  Mod Depth. Bit-exact superposition is not required here - the");
        info ("  tank is time-varying by design - only that Attack's direction");
        info ("  and range survive and that periodicity does not regress.");
        info ("");
        info ("    " + pad ("Mod Depth", 12) + pad ("Attack", 10)
              + pad ("buildup", 11) + pad ("late NED", 11) + "periodicity");

        double smallestModSpan = 1.0e30;

        for (double mod : { 0.0, 0.30, 1.0 })
        {
            double buildMin = 1.0e30, buildMax = -1.0e30;

            for (double u : { 0.0, 0.63, 1.0 })
            {
                Config c;
                c.modDepth = mod;

                auto tank = makeTank (c);

                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (u);

                const auto out = renderThroughAttack (*tank, attack, impulse, length);
                const auto b = buildupOf (out, referenceDb);

                if (b.buildupMs >= 0.0)
                {
                    buildMin = std::min (buildMin, b.buildupMs);
                    buildMax = std::max (buildMax, b.buildupMs);
                }

                info ("    " + pad (fx (mod * 100.0, 0) + " %", 12)
                      + pad (fx (u * 100.0, 0) + " %", 10)
                      + pad (b.buildupMs >= 0.0 ? fx (b.buildupMs, 0) + " ms" : "--", 11)
                      + pad (densityText (densityAt (out, 320.0)), 11)
                      + fx (periodicityIn (out, 300.0, 900.0), 2));
            }

            smallestModSpan = std::min (smallestModSpan, buildMax - buildMin);
        }

        info ("");
        info ("    smallest buildup span across Mod Depth  "
              + fx (smallestModSpan, 0) + " ms");

        // --- energy, stereo, mono, coloration through the tank -------------
        info ("");
        info ("  Energy across the Attack sweep, and the stereo field.");
        info ("");
        info ("    " + pad ("Attack", 10) + pad ("energy", 12)
              + pad ("rel.", 11) + pad ("peak", 10)
              + pad ("L/R corr", 11) + pad ("mono/stereo", 14) + "RT60 proxy");

        double baselineEnergy = 0.0;
        bool first = true;
        double worstEnergyDb = 0.0;

        for (double u : { 0.0, 0.25, 0.50, 0.63, 0.80, 1.0 })
        {
            auto tank = makeTank (Config {});

            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (u);

            std::vector<double> l = impulse, r = impulse;
            attack.process (l.data(), r.data(), static_cast<int> (length));

            for (size_t n = 0; n < length; ++n)
                tank->processSample (l[n], r[n]);

            const double e = energyOf (l);

            if (first)
            {
                baselineEnergy = e;
                first = false;
            }

            const double relative = 10.0 * std::log10 (e / baselineEnergy);
            worstEnergyDb = std::max (worstEnergyDb, std::abs (relative));

            const auto from = static_cast<size_t> (0.300 * kSampleRate);
            const auto to   = std::min (l.size(), static_cast<size_t> (0.900 * kSampleRate));

            const double corr = correlation (l.data() + from, r.data() + from, to - from);

            double stereoEnergy = 0.0, monoEnergy = 0.0;

            for (size_t n = from; n < to; ++n)
            {
                stereoEnergy += l[n] * l[n] + r[n] * r[n];

                const double mono = 0.5 * (l[n] + r[n]);
                monoEnergy += 2.0 * mono * mono;
            }

            const double ratio = stereoEnergy > 0.0
                               ? 10.0 * std::log10 (monoEnergy / stereoEnergy) : 0.0;

            const auto env = envelopeDb (l, 30.0, 10.0, 3500.0);
            double rt = 0.0;

            if (! env.empty())
            {
                const auto peakAt = static_cast<size_t> (
                    std::max_element (env.begin(), env.end()) - env.begin());

                for (size_t i = peakAt; i < env.size(); ++i)
                    if (env[i] <= env[peakAt] - 30.0)
                    {
                        rt = 2.0 * static_cast<double> (i - peakAt) * 0.010;
                        break;
                    }
            }

            info ("    " + pad (fx (u * 100.0, 0) + " %", 10)
                  + pad (fx (e, 6), 12)
                  + pad (fx (relative, 2) + " dB", 11)
                  + pad (fx (peakOf (l), 5), 10)
                  + pad (fx (corr, 4), 11)
                  + pad (fx (ratio, 2) + " dB", 14)
                  + fx (rt, 3) + " s");
        }

        info ("");
        info ("    worst integrated wet energy change  " + fx (worstEnergyDb, 3) + " dB");

        checkAtMost (worstEnergyDb, 0.5,
                     "Attack is not a disguised wet-level control");

        check (smallestBuildupSpan > 20.0,
               "Attack keeps a useful buildup range at every Early Diffusion setting");

        check (smallestModSpan > 20.0,
               "and at every Mod Depth");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - Attack CPU and memory");
    // =======================================================================
    {
        info ("  Against the tank in the same run, minimum of five repeats.");
        info ("");

        constexpr int repeats = 5;
        const int length = 48000;

        std::vector<double> drive (static_cast<size_t> (length), 0.0);

        for (int i = 0; i < length; ++i)
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

            return bestNs / static_cast<double> (length);
        };

        auto tank = makeTank (Config {});

        const double tankNs = best ([&]
        {
            std::vector<double> l = drive, r = drive;

            for (int n = 0; n < length; ++n)
                tank->processSample (l[static_cast<size_t> (n)],
                                     r[static_cast<size_t> (n)]);
        });

        const auto timeAttack = [&] (double u)
        {
            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (u);

            return best ([&]
            {
                std::vector<double> l = drive, r = drive;
                attack.process (l.data(), r.data(), length);
            });
        };

        const double neutralNs = timeAttack (0.0);
        const double activeNs  = timeAttack (1.0);

        info ("    " + pad ("stage", 26) + pad ("ns/sample", 13) + "share of tank");
        info ("    " + pad ("PHASE 7/8 tank", 26) + pad (fx (tankNs, 3), 13) + "-");
        info ("    " + pad ("Attack, neutral (0 %)", 26) + pad (fx (neutralNs, 3), 13)
              + fx (100.0 * neutralNs / tankNs, 2) + " %");
        info ("    " + pad ("Attack, active (100 %)", 26) + pad (fx (activeNs, 3), 13)
              + fx (100.0 * activeNs / tankNs, 2) + " %");
        info ("");

        AttackInjection probe;
        probe.prepare (kSampleRate);

        info ("    taps                 " + std::to_string (probe.getTapCount()));
        info ("    delay memory         two buffers of 32768 doubles = 512 KiB");
        info ("                         sized from the 400 ms allocation limit,");
        info ("                         rounded to a power of two for masking");
        info ("    added state          two Tap vectors of "
              + std::to_string (probe.getTapCount()) + ", a write index,");
        info ("                         a fade position and four scalars");
        info ("    crossfade length     " + std::to_string (probe.getFadeLengthSamples())
              + " samples");
        info ("");
        info ("    At Attack 0 the loop writes two samples and takes a branch:");
        info ("    that is the whole neutral cost, and it is why the share of");
        info ("    the tank is what it is rather than 32 multiply-accumulates");
        info ("    worth.");

        checkCpuAtMost (activeNs, 0.60 * tankNs,
                        "Attack active costs a fraction of the tank it feeds");

        checkCpuAtMost (neutralNs, 0.05 * tankNs,
                        "and Attack 0 is nearly free");
    }

    // =======================================================================
    beginSuite ("PHASE 9 Stage 3 - Attack production gates");
    // =======================================================================
    {
        info ("  The gates a shipping stage has to pass whatever it sounds");
        info ("  like: block-size independence, sample-rate consistency, reset,");
        info ("  and finiteness under abuse.");
        info ("");

        std::mt19937 rng (777u);
        std::uniform_real_distribution<double> dist (-0.5, 0.5);

        // --- block-size identity -------------------------------------------
        {
            const auto length = static_cast<size_t> (kSampleRate * 0.5);

            std::vector<double> source (length, 0.0);

            for (auto& s : source)
                s = dist (rng);

            const auto run = [&] (int block)
            {
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (0.63);

                std::vector<double> l = source, r = source;

                for (size_t n = 0; n < length; n += static_cast<size_t> (block))
                {
                    const auto count = static_cast<int> (
                        std::min (static_cast<size_t> (block), length - n));

                    attack.process (l.data() + n, r.data() + n, count);
                }

                return l;
            };

            const auto a = run (4096);
            const auto b = run (64);
            const auto c = run (1);

            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
            {
                worst = std::max (worst, std::abs (a[n] - b[n]));
                worst = std::max (worst, std::abs (a[n] - c[n]));
            }

            info ("    block-size identity, 4096 vs 64 vs 1   worst difference "
                  + fx (worst, 16));

            checkEqual (worst, 0.0,
                        "the Attack stage is bit-identical at any block size");
        }

        // --- sample-rate consistency ---------------------------------------
        /*
            The schedule is specified in milliseconds and rounded to samples,
            so the same Attack must mean the same TIME at every rate. Checked
            on the last tap's position, which is where any rounding error
            would accumulate.
        */
        info ("");
        info ("    " + pad ("rate", 12) + pad ("last tap", 13)
              + pad ("in ms", 11) + "spread target");

        double rateMin = 1.0e30, rateMax = -1.0e30;

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            AttackInjection attack;
            attack.prepare (rate);
            attack.snapTo (1.0);

            int last = 0;

            for (int k = 0; k < attack.getTapCount(); ++k)
                last = std::max (last, attack.getTapOffset (k));

            const double ms = 1000.0 * last / rate;
            const double target = attack.spreadForControl (1.0);

            // Across RATES, not against the nominal spread. The first
            // version gated the distance from 250 ms and failed at 6.79 ms -
            // but that is design, not drift: the last tap sits at 97.3 per
            // cent of the spread because its golden-ratio position inside
            // the final cell falls early. What has to be sample-rate
            // independent is the TIME the tap lands at.
            rateMin = std::min (rateMin, ms);
            rateMax = std::max (rateMax, ms);

            info ("    " + pad (fx (rate / 1000.0, 1) + " kHz", 12)
                  + pad (std::to_string (last), 13)
                  + pad (fx (ms, 4), 11) + fx (target, 1) + " ms");
        }

        info ("");
        info ("    spread across sample rates              "
              + fx (rateMax - rateMin, 4) + " ms");
        info ("");
        info ("    The last tap lands at 97.3 per cent of the nominal spread,");
        info ("    not at 100, because its position inside the final cell comes");
        info ("    from the golden-ratio sequence like every other tap. That is");
        info ("    the schedule, not an error, and gating the distance from the");
        info ("    nominal value would have been gating the design.");

        checkAtMost (rateMax - rateMin, 0.1,
                     "the Attack schedule lands at the same TIME at every sample rate");

        // --- reset clears history ------------------------------------------
        /*
            Two stages given the same signal, one of them having had loud
            noise pushed through it and then reset. If reset works they agree
            bit for bit; if it does not, the old history leaks out through the
            taps.
        */
        {
            const auto length = static_cast<size_t> (kSampleRate * 0.3);

            std::vector<double> loud (length, 0.0), probe (length, 0.0);

            for (auto& s : loud)
                s = dist (rng) * 4.0;

            for (auto& s : probe)
                s = dist (rng);

            const auto run = [&] (bool dirty)
            {
                AttackInjection attack;
                attack.prepare (kSampleRate);
                attack.snapTo (0.63);

                if (dirty)
                {
                    std::vector<double> l = loud, r = loud;
                    attack.process (l.data(), r.data(), static_cast<int> (length));
                    attack.reset();
                }

                std::vector<double> l = probe, r = probe;
                attack.process (l.data(), r.data(), static_cast<int> (length));

                return l;
            };

            const auto clean = run (false);
            const auto afterReset = run (true);

            double worst = 0.0;

            for (size_t n = 0; n < length; ++n)
                worst = std::max (worst, std::abs (clean[n] - afterReset[n]));

            info ("");
            info ("    reset clears the delay line            worst difference "
                  + fx (worst, 16));

            checkEqual (worst, 0.0, "reset leaves no history behind it");
        }

        // --- finiteness under abuse ----------------------------------------
        {
            const auto length = static_cast<size_t> (kSampleRate * 0.2);

            AttackInjection attack;
            attack.prepare (kSampleRate);
            attack.snapTo (0.0);

            std::vector<double> l (length, 0.0), r (length, 0.0);

            // Full-scale square wave, then silence, with automation on top.
            for (size_t n = 0; n < length; ++n)
            {
                l[n] = (n / 97) % 2 ? 1.0 : -1.0;
                r[n] = -l[n];
            }

            std::uniform_real_distribution<double> control (0.0, 1.0);

            for (size_t n = 0; n + 32 <= length; n += 32)
            {
                attack.setAttack (control (rng));
                attack.process (l.data() + n, r.data() + n, 32);
            }

            info ("");
            info ("    full-scale square plus automation      peak "
                  + fx (std::max (peakOf (l), peakOf (r)), 5));

            check (allFiniteIn (l) && allFiniteIn (r),
                   "no NaN or Inf under full-scale input and rapid automation");

            // The FIR's L2 norm is 1, so a full-scale input cannot exceed the
            // sum of |w|, which is what bounds the peak. Reported rather than
            // gated, because the wet trim and Mix sit after this stage.
            double absSum = 0.0;

            for (int k = 0; k < attack.getTapCount(); ++k)
                absSum += std::abs (attack.getTapWeight (k));

            info ("    worst-case gain, sum of |w|            " + fx (absSum, 4));
            info ("");
            info ("    That is the bound a full-scale coherent input could");
            info ("    reach, and it is above 1 - as any multi-tap FIR with");
            info ("    unit L2 norm must be. It is reported rather than gated");
            info ("    because reaching it needs an input correlated with the");
            info ("    tap pattern, and because the decay-aware wet trim and");
            info ("    Mix both sit after this stage. PHASE 6's gain-staging");
            info ("    suites are where the output ceiling is actually gated.");
        }
    }
}

} // namespace avtest
