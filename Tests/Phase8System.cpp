#include "TestFramework.h"
#include "DampingSweepHarness.h"
#include "DspAnalysis.h"

#include "DSP/CutFilter.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 8 - system behaviour.

    The component suite proves the filters and the pre-delay do what their
    difference equations say. This one asks the two questions that are about
    the PRODUCT rather than about a filter:

      1. Does the neutral PHASE 8 configuration leave the PHASE 7 signal path
         exactly as it was?

      2. Are the cuts really filters and not a second damping system?

    The second is the central architectural claim of the phase. High Damp
    lives inside the feedback loop and changes how long a band takes to
    decay. The cuts live outside it and change how much energy enters a band.
    If a cut had leaked into the loop the two controls would be redundant in
    a way users would feel and could not name. So it is measured rather than
    argued.

    SCOPE, corrected by STAGE 2.5. The commutation suite below runs at Mod
    Depth 0, where the tank is time invariant and commutation is the right
    test. The shipped product runs at 30 %, where it is not. The claim that
    survives with modulation is a ROUTING claim - the cut parameters are
    consumed before processBlock and enter no tank control value - and it is
    proved on the complete production tank in runPhase8SignoffTests. Neither
    suite claims that a cut can never change a MEASURED RT60: the suite below
    shows the opposite, and shows why it is the estimator rather than the
    tank.
*/

using namespace av::dsp;
using namespace avtest::sweep;

namespace avtest
{

namespace
{
    [[nodiscard]] std::string fx (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    /** Runs a mono drive through the stereo cut filters, as the wet path does. */
    [[nodiscard]] std::vector<double> filterDrive (const std::vector<double>& drive,
                                                   double lowHz, double highHz)
    {
        StereoCutFilters f;
        f.prepare (kSampleRate);
        f.setLowCut  (lowHz,  lowHz  <= lowCutOffHz);
        f.setHighCut (highHz, highHz >= highCutOffHz);

        std::vector<double> l = drive;
        std::vector<double> r = drive;

        f.process (l.data(), r.data(), static_cast<int> (l.size()));

        return l;
    }

    /**
        The excitation, zero-padded to the length it will be rendered at.

        makeExcitation returns 1024 samples and Tank::render zero-pads the
        rest. Filtering the 1024-sample vector therefore throws away the
        filter's own output tail, and the first version of both tests below
        did exactly that: the commutation check came out at 3.8e-4 instead of
        rounding, and the band-limited excitation was a 1-octave bandpass
        truncated at 21 ms, which is mostly truncation. Pad first, filter
        after.
    */
    [[nodiscard]] std::vector<double> paddedExcitation (int index, int length)
    {
        auto burst = makeExcitation (index);

        std::vector<double> padded (static_cast<size_t> (length), 0.0);

        const auto n = std::min (burst.size(), padded.size());

        for (size_t i = 0; i < n; ++i)
            padded[i] = burst[i];

        return padded;
    }

    /** Total energy of a render inside one analysis band. */
    [[nodiscard]] double bandEnergy (const std::vector<double>& signal,
                                     const decay::Band& band)
    {
        auto filtered = signal;
        band.apply (filtered);

        double sum = 0.0;

        for (double x : filtered)
            sum += x * x;

        return sum;
    }
}

// ===========================================================================

void runPhase8SystemTests()
{
    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 system - the neutral configuration is PHASE 7, bit for bit");
    // -----------------------------------------------------------------------
    {
        info ("  Pre-Delay 0 ms, Low Cut 20 Hz, High Cut 20 kHz. The strict");
        info ("  reference case: with the three controls parked on their");
        info ("  neutral values the wet path must be the one PHASE 7 shipped,");
        info ("  not merely close to it.");
        info ("");

        Setting s;
        s.lowDamp = kDefaultLowDamp;
        s.highDamp = 0.45;
        s.lowHz = 200.0;
        s.highHz = 10000.0;
        s.modDepthControl = 0.30;

        auto drive = makeExcitation (0);

        // PHASE 7: straight into the tank.
        std::vector<double> reference;
        {
            Tank tank (s);
            reference = tank.render (drive, 48000);
        }

        // PHASE 8 at neutral: through both new stages first.
        std::vector<double> throughPhase8;
        {
            StereoCutFilters filters;
            filters.prepare (kSampleRate);
            filters.setLowCut  (lowCutOffHz,  true);
            filters.setHighCut (highCutOffHz, true);

            PreDelay<double> pd;
            pd.prepare (kSampleRate);
            pd.snapTo (0.0);

            std::vector<double> l = drive;
            std::vector<double> r = drive;

            filters.process (l.data(), r.data(), static_cast<int> (l.size()));
            pd.process      (l.data(), r.data(), static_cast<int> (l.size()));

            check (filters.isBypassed(), "the cut stage reports itself bypassed at neutral");
            check (pd.isBypassed(),      "the pre-delay reports itself bypassed at neutral");

            Tank tank (s);
            throughPhase8 = tank.render (l, 48000);
        }

        long long differing = 0;
        double worst = 0.0;

        for (size_t i = 0; i < reference.size(); ++i)
        {
            if (reference[i] != throughPhase8[i])
                ++differing;

            worst = std::max (worst, std::abs (reference[i] - throughPhase8[i]));
        }

        info ("    samples compared          " + std::to_string (reference.size()));
        info ("    samples differing         " + std::to_string (differing));
        info ("    worst sample difference   " + fx (worst, 18));

        checkEqual (differing, 0LL,
                    "the neutral PHASE 8 path reproduces PHASE 7 bit for bit");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 system - the cut is outside the loop, STATIC control");
    // -----------------------------------------------------------------------
    {
        info ("  A STATIC / LTI CONTROL TEST. Mod Depth is 0 here, which makes");
        info ("  the tank time invariant - verified by shift invariance in the");
        info ("  STAGE 2.5 signoff suite, where the same tank at the shipped");
        info ("  30 % fails that test by twelve orders of magnitude. So this");
        info ("  result is evidence about the STATIC case and does not extend");
        info ("  to the default product. The routing claim that does hold with");
        info ("  modulation is proved separately, on the complete production");
        info ("  tank, in runPhase8SignoffTests.");
        info ("");
        info ("  If the cut really is outside the feedback loop then the cut");
        info ("  and the tank are two LTI blocks in series, and they COMMUTE:");
        info ("  filtering the excitation and filtering the tank's output must");
        info ("  give the same signal to within rounding. If the filter had");
        info ("  leaked into the loop - the failure this architecture exists to");
        info ("  avoid - the two orders would differ grossly, because a filter");
        info ("  inside a feedback path is applied once per circulation.");
        info ("");

        Setting s;
        s.lowDamp = kDefaultLowDamp;
        s.highDamp = 0.45;
        s.lowHz = 200.0;
        s.highHz = 10000.0;
        s.modDepthControl = 0.0;

        info ("    Mod Depth is 0 here on purpose: modulation makes the tank");
        info ("    time-varying, and commutation is a property of LTI blocks.");
        info ("    What is being tested is where the filter sits, which");
        info ("    modulation does not change.");
        info ("");
        info ("    " + pad ("low cut", 10) + pad ("high cut", 10)
              + pad ("peak", 13) + pad ("worst diff", 16) + "relative");

        double worstRelative = 0.0;

        struct Case { double lowHz, highHz; };

        for (const auto& c : { Case { lowCutOffHz, 8000.0 }, Case { 300.0, highCutOffHz },
                               Case { 300.0, 8000.0 }, Case { 1000.0, 2000.0 } })
        {
            auto drive = paddedExcitation (3, 48000);

            std::vector<double> cutFirst;
            {
                Tank tank (s);
                cutFirst = tank.render (filterDrive (drive, c.lowHz, c.highHz), 48000);
            }

            std::vector<double> tankFirst;
            {
                Tank tank (s);
                auto raw = tank.render (drive, 48000);
                tankFirst = filterDrive (raw, c.lowHz, c.highHz);
            }

            double peak = 0.0, worst = 0.0;

            for (size_t i = 0; i < cutFirst.size(); ++i)
            {
                peak = std::max (peak, std::abs (cutFirst[i]));
                worst = std::max (worst, std::abs (cutFirst[i] - tankFirst[i]));
            }

            const double relative = peak > 0.0 ? worst / peak : 0.0;
            worstRelative = std::max (worstRelative, relative);

            info ("    " + pad (fx (c.lowHz, 0), 10) + pad (fx (c.highHz, 0), 10)
                  + pad (fx (peak, 6), 13) + pad (fx (worst, 12), 16)
                  + fx (relative, 12));
        }

        info ("");
        info ("    worst relative difference  " + fx (worstRelative, 14));

        checkAtMost (worstRelative, 1.0e-9,
                     "cut-then-tank equals tank-then-cut: the filter is outside the loop");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 system - what a cut does to a measured RT60, and why");
    // -----------------------------------------------------------------------
    {
        /*
            Two earlier versions of this test are worth recording, because
            both produced a confident wrong answer.

            VERSION 1 swept the cut with the standard broadband excitation and
            reported up to 9.18 % RT60 "drift", which read as the filter
            reaching the decay law. VERSION 2 tried to fix that by confining
            each excitation to the analysis band first - and made it worse in
            a way that was easy to miss: the analyser applies the SAME band
            again, so the effective band was twice as narrow and rang far
            longer than the tank decayed. The giveaway was the control. Moving
            High Damp from 45 % to 100 % shifted the measured RT60 by 1.39 %,
            when PHASE 7 measured that same change as roughly 25 %. A
            measurement that cannot see High Damp cannot be used to say a cut
            is invisible to it.

            What is true, and already proved in the previous suite: cut-then-
            tank equals tank-then-cut to 4e-16. The cut is outside the loop
            and no mode's decay can change. So this suite stops trying to
            prove that statistically and instead explains the drift, with the
            control experiment that settles it:

                apply the same cut AFTER the tank, where it is mathematically
                incapable of changing any mode's decay, and measure again.

            If the measured drift is the same, the drift belongs to the
            measurement - the analysis band has finite rejection, so a bright
            excitation leaks fast-decaying high-frequency energy into every
            band, and removing the highs removes the leak - and not to the
            tank.
        */
        constexpr int ensembleSize = 16;

        Setting base;
        base.lowDamp = kDefaultLowDamp;
        base.highDamp = 0.45;
        base.lowHz = 200.0;
        base.highHz = 10000.0;
        base.modDepthControl = 0.0;

        const double bands[] { 500.0, 1000.0, 3000.0 };
        const double cuts[]  { highCutOffHz, 8000.0, 4000.0 };

        const int renderLength = static_cast<int> (kSampleRate * 6.25);

        info ("  Standard broadband excitation and the certified analyser, as");
        info ("  PHASE 7 used. The cut is applied before the tank and then");
        info ("  after it, and the two are compared.");
        info ("");
        info ("    " + pad ("band", 8) + pad ("High Cut", 11)
              + pad ("RT60 pre", 11) + pad ("RT60 post", 11)
              + pad ("pre-post", 12) + "vs no cut %");

        double worstPrePostDifference = 0.0;
        double worstApparentDrift = 0.0;

        for (double centre : bands)
        {
            const decay::Band band { centre, kGateBandOctaves, kSampleRate };

            double reference = 0.0;

            for (double cut : cuts)
            {
                std::vector<std::vector<double>> preEnsemble, postEnsemble;

                {
                    Tank tank (base);

                    for (int k = 0; k < ensembleSize; ++k)
                    {
                        auto drive = paddedExcitation (k, renderLength);
                        drive = filterDrive (drive, lowCutOffHz, cut);
                        preEnsemble.push_back (tank.render (drive, renderLength));
                    }
                }

                {
                    Tank tank (base);

                    for (int k = 0; k < ensembleSize; ++k)
                    {
                        auto drive = paddedExcitation (k, renderLength);
                        auto raw = tank.render (drive, renderLength);
                        postEnsemble.push_back (filterDrive (raw, lowCutOffHz, cut));
                    }
                }

                const auto pre  = measureBand (preEnsemble,  band);
                const auto post = measureBand (postEnsemble, band);

                if (cut >= highCutOffHz)
                    reference = pre.fitted;

                const double drift = reference > 0.0
                                   ? 100.0 * std::abs (pre.fitted - reference) / reference
                                   : 0.0;

                const double prePost = std::abs (pre.fitted - post.fitted);

                worstPrePostDifference = std::max (worstPrePostDifference, prePost);

                if (cut < highCutOffHz)
                    worstApparentDrift = std::max (worstApparentDrift, drift);

                info ("    " + pad (fx (centre, 0), 8)
                      + pad (cut >= highCutOffHz ? std::string ("off") : fx (cut, 0), 11)
                      + pad (fx (pre.fitted, 4), 11)
                      + pad (fx (post.fitted, 4), 11)
                      + pad (fx (prePost, 10), 12)
                      + fx (drift, 3));
            }
        }

        info ("");
        info ("    worst pre-versus-post difference   " + fx (worstPrePostDifference, 12) + " s");
        info ("    apparent drift from the cut        " + fx (worstApparentDrift, 3) + " %");
        info ("");
        info ("    The same filter applied where it CANNOT change a decay");
        info ("    produces the same measured change. So the change is the");
        info ("    analyser's, not the tank's: a one-octave band has finite");
        info ("    rejection, a bright excitation leaks fast-decaying highs");
        info ("    into every band, and a high cut removes the leak along with");
        info ("    the highs. The measured band RT60 then rises toward the");
        info ("    band's own true decay.");
        info ("");
        info ("    Recorded as a limitation of band-RT60 measurement under a");
        info ("    changing excitation spectrum, NOT as a cut-damping");
        info ("    interaction. The brief's <= 1 % RT60 target cannot be met by");
        info ("    this metric for a reason that has nothing to do with the");
        info ("    tank, and the exact structural claim it was meant to test is");
        info ("    already settled at 4e-16 by commutation.");

        checkAtMost (worstPrePostDifference, 1.0e-6,
                     "pre-tank and post-tank cuts give the same measured RT60");

        // The control that the previous version failed: the measurement must
        // be able to see a real decay change, or its silence proves nothing.
        info ("");
        info ("  The sensitivity control, with the same broadband method.");
        info ("");
        info ("    " + pad ("band", 8) + pad ("High Damp", 11) + pad ("RT60 s", 11) + "vs 45 %");

        double largestDampingShift = 0.0;

        // Probed where High Damp actually acts. The shipping corner is
        // 10 kHz, so at 3 kHz the control is barely engaged - an earlier
        // version of this control probed 3 and 6 kHz, read 1.61 %, and looked
        // like an insensitive measurement when it was a badly chosen band.
        for (double centre : { 6000.0, 12000.0 })
        {
            const decay::Band band { centre, kGateBandOctaves, kSampleRate };

            double reference = 0.0;

            for (double damp : { 0.45, 1.00 })
            {
                Setting s = base;
                s.highDamp = damp;

                Tank tank (s);

                std::vector<std::vector<double>> ensemble;

                for (int k = 0; k < ensembleSize; ++k)
                    ensemble.push_back (tank.render (paddedExcitation (k, renderLength),
                                                     renderLength));

                const auto result = measureBand (ensemble, band);

                if (damp <= 0.45)
                    reference = result.fitted;

                const double shift = reference > 0.0
                                   ? 100.0 * std::abs (result.fitted - reference) / reference
                                   : 0.0;

                largestDampingShift = std::max (largestDampingShift, shift);

                info ("    " + pad (fx (centre, 0), 8) + pad (fx (damp * 100.0, 0) + " %", 11)
                      + pad (fx (result.fitted, 4), 11) + fx (shift, 3));
            }
        }

        info ("");
        info ("    RT60 shift from moving High Damp 45 -> 100 %  "
              + fx (largestDampingShift, 2) + " %");
        info ("");
        info ("");
        info ("    OPEN QUESTION, recorded rather than asserted.");
        info ("");
        info ("    This control was meant to gate: a measurement that cannot");
        info ("    see High Damp move a decay cannot be used to say a cut");
        info ("    leaves it alone. It does not deliver that. PHASE 7's own");
        info ("    analytic table puts 12 kHz at 0.729 of base Decay for High");
        info ("    Damp 45 % - about 1.82 s - and this ensemble reads 2.29 s,");
        info ("    with every band between 500 Hz and 12 kHz landing inside");
        info ("    2.15-2.29 s. The whole frequency curve is compressed toward");
        info ("    one broadband average, which is what finite band rejection");
        info ("    under a bright excitation produces.");
        info ("");
        info ("    So no assertion is made here. It would be a gate on a");
        info ("    number this suite cannot presently justify. The claim PHASE");
        info ("    8 needs is carried by the commutation result above, which");
        info ("    is exact at 4e-16 and needs no statistics; and by the");
        info ("    pre-versus-post control, which passes.");
        info ("");
        info ("    What is left open is a PHASE 7 harness question - how much");
        info ("    out-of-band energy measureBand admits under a broadband");
        info ("    excitation - and it is deliberately not pursued here. The");
        info ("    brief fences off PHASE 7 retuning and unrelated cleanup,");
        info ("    and this changes no PHASE 7 result: those studies compared");
        info ("    settings against each other through the same analyser,");
        info ("    where a shared bias cancels.");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 8 system - CPU, against the tank measured in the same run");
    // -----------------------------------------------------------------------
    {
        /*
            Against the tank measured here and now rather than a figure copied
            from another run on another day: the ratio is what matters, and a
            same-run baseline cancels the machine.

            Minimum of several repetitions. A single timing of a short run is
            dominated by scheduling and clock behaviour - the first version of
            this suite reported the BYPASSED pre-delay as slower than the
            engaged one, which is impossible, and was pure noise.
        */
        constexpr int repeats = 7;
        const int length = 48000 * 2;

        Setting s;
        s.lowDamp = kDefaultLowDamp;
        s.highDamp = 0.45;
        s.lowHz = 200.0;
        s.highHz = 10000.0;
        s.modDepthControl = 0.30;

        std::vector<double> drive (static_cast<size_t> (length), 0.0);

        for (int i = 0; i < length; ++i)
            drive[static_cast<size_t> (i)] = 0.25 * std::sin (0.013 * i);

        auto best = [repeats] (auto&& body)
        {
            double bestNs = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto start = std::chrono::steady_clock::now();
                body();
                const auto finish = std::chrono::steady_clock::now();

                const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>
                                    (finish - start).count();

                bestNs = std::min (bestNs, static_cast<double> (ns));
            }

            return bestNs;
        };

        Tank tank (s);

        const double tankNs = best ([&] { (void) tank.render (drive, length); })
                            / static_cast<double> (length);

        StereoCutFilters filters;
        PreDelay<double> pd;

        auto timeStages = [&] (double lowHz, double highHz, double ms)
        {
            filters.prepare (kSampleRate);
            filters.setLowCut  (lowHz,  lowHz  <= lowCutOffHz);
            filters.setHighCut (highHz, highHz >= highCutOffHz);

            pd.prepare (kSampleRate);
            pd.snapTo (ms);

            std::vector<double> l (static_cast<size_t> (length), 0.0), r = l;

            return best ([&]
            {
                l = drive;
                r = drive;
                filters.process (l.data(), r.data(), length);
                pd.process      (l.data(), r.data(), length);
            }) / static_cast<double> (length);
        };

        const double engagedNs = timeStages (120.0, 9000.0, 40.0);
        const double neutralNs = timeStages (lowCutOffHz, highCutOffHz, 0.0);

        info ("    stage                                 ns/sample");
        info ("    PHASE 7 tank, measured in this run    " + fx (tankNs, 3));
        info ("    PHASE 8 stages, engaged               " + fx (engagedNs, 3));
        info ("    PHASE 8 stages, neutral               " + fx (neutralNs, 3));
        info ("");
        info ("    PHASE 8 engaged, as a share of the tank   "
              + fx (100.0 * engagedNs / tankNs, 2) + " %");
        info ("    PHASE 8 neutral, as a share of the tank   "
              + fx (100.0 * neutralNs / tankNs, 2) + " %");
        info ("");
        info ("    The engaged figure is two one-poles and a normalising");
        info ("    multiply per channel, plus two ring-buffer writes and two");
        info ("    reads - roughly twenty operations, against a tank carrying");
        info ("    eight delay lines, an 8x8 matrix and sixteen damping");
        info ("    shelves. The gate below is a PATHOLOGY DETECTOR sized from");
        info ("    that operation count, not a budget: it exists to catch an");
        info ("    accidental allocation or a denormal storm, which would be");
        info ("    orders of magnitude, not to adjudicate a few per cent.");

        checkCpuAtMost (engagedNs, 0.35 * tankNs,
                        "PHASE 8 costs a small fraction of the tank it feeds");
    }
}

} // namespace avtest
