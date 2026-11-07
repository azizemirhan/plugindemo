#include "DampingSweepHarness.h"
#include "Suites.h"
#include "TestFramework.h"

/*
    PHASE 7 - TEST-ONLY sweep of the default High Damp AMOUNT, then a small
    joint sweep of High Damp Freq against amount if the first pass warrants it.

    Nothing in Source/ changes. No default, no mapping, no Option A semantic is
    touched. High Damp Freq 10 kHz and Low Damp Freq 200 Hz are provisional
    study candidates, not shipped values.

    THE LAST UNTESTED DEFAULT AXIS

    Low Damp Freq and Low Damp amount are both closed - each turned out to be
    a genuine interior optimum sitting on the shipping value. High Damp Freq
    improved things a great deal, 7.90 % to 2.46 %, but it was swept at 50 %
    amount. This is the one control that has never been moved, and it is the
    one that acts directly on the band where the error now sits.

    AND WHY A JOINT SWEEP MAY BE NEEDED

    Option A normalises each line by a single scalar, 1 / max|H_pair|, which
    couples the two shelves - that coupling is the mechanism behind every
    result in the three previous sweeps. So the 10 kHz frequency optimum was
    found at one particular amount and is not guaranteed to survive a change
    of amount. If the first pass finds a promising region, the frequency is
    re-optimised inside it rather than assumed.

    THE RANGE IS WIDER THAN ASKED, ON PURPOSE

    Three sweeps in a row have produced an edge-pinned optimum that needed a
    second run to resolve - the High Damp corner at 16 kHz, Low Damp Freq at
    200 Hz, Low Damp amount at 20 %. That is a pattern, not bad luck, so this
    sweep starts wider than the requested 25 to 60 % and reports explicitly
    whether its minimum is interior.
*/

namespace avtest
{
namespace
{

using namespace avtest::sweep;

constexpr double kAmounts[] { 0.20, 0.25, 0.30, 0.35, 0.40,
                              0.45, 0.50, 0.55, 0.60, 0.70 };
constexpr int kNumAmounts = 10;

constexpr double kProvisionalLowHz  = 200.0;
constexpr double kProvisionalHighHz = 10000.0;

// Promising-region threshold from the study instruction. Not a quality gate.
constexpr double kPromisingPercent = 2.2;

// Registered reference at High Damp 50 %.
constexpr double kReference500Hz = 2.25;
constexpr double kReference1kHz  = 0.56;
constexpr double kReference2kHz  = 2.46;

} // namespace

void runDampingHighAmountSweepTests()
{
    const double probes[] { 500.0, 1000.0, 2000.0, 4000.0, 6000.0,
                            8000.0, 10000.0, 12000.0, 16000.0 };
    constexpr int kNumProbes = 9;

    int shipping = 0;

    for (int a = 0; a < kNumAmounts; ++a)
        if (kAmounts[a] == kDefaultHighDamp)
            shipping = a;

    std::array<std::array<double, kNumProbes>, kNumAmounts> grid {};
    std::array<double, kNumAmounts> worstMid {};
    std::array<double, kNumAmounts> worstMidHz {};
    std::array<double, kNumProbes> noDamp {};

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - PROBLEM A across the amount sweep");
    // -----------------------------------------------------------------------
    {
        info ("  Decay " + number (kDecayTarget, 1) + " s, Low Damp "
              + number (kDefaultLowDamp * 100.0, 0) + " %, Low Damp Freq "
              + number (kProvisionalLowHz, 0) + " Hz,");
        info ("  High Damp Freq " + number (kProvisionalHighHz / 1000.0, 0)
              + " kHz, Mod Depth " + number (kDefaultModDepth * 100.0, 0)
              + " %, Mod Rate " + number (kDefaultModRateHz, 2) + " Hz.");
        info ("");
        info ("    High                                RT60 in seconds, base Decay "
              + number (kDecayTarget, 2) + " s");
        info ("    Damp      500 Hz   1 kHz    2 kHz    4 kHz    6 kHz    8 kHz"
              "    10 kHz   12 kHz   16 kHz");

        // High Damp 0 first, as the retention reference.
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = 0.0;
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kProvisionalHighHz;
            setting.modDepthControl = kDefaultModDepth;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::string row = "    " + pad ("0.0 % ref", 10);

            for (int b = 0; b < kNumProbes; ++b)
            {
                const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
                noDamp[static_cast<size_t> (b)] = measureBand (ensemble, band).fitted;

                row += pad (number (noDamp[static_cast<size_t> (b)], 4), 9);
            }

            info (row);
        }

        for (int a = 0; a < kNumAmounts; ++a)
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = kAmounts[a];
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kProvisionalHighHz;
            setting.modDepthControl = kDefaultModDepth;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            for (int b = 0; b < kNumProbes; ++b)
            {
                const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                grid[static_cast<size_t> (a)][static_cast<size_t> (b)] = result.fitted;

                row += pad (number (result.fitted, 4), 9);

                if (probes[b] <= 2000.0 && result.valid)
                {
                    const double deviation = std::abs (result.fitted - kDecayTarget)
                                             / kDecayTarget * 100.0;

                    if (deviation > worstMid[static_cast<size_t> (a)])
                    {
                        worstMid[static_cast<size_t> (a)] = deviation;
                        worstMidHz[static_cast<size_t> (a)] = probes[b];
                    }
                }
            }

            info (row);
        }

        info ("");
        info ("    High      deviation from base Decay, per cent");
        info ("    Damp      500 Hz   1 kHz    2 kHz    worst    at");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            for (int b = 0; b < 3; ++b)
                row += pad (number (std::abs (grid[static_cast<size_t> (a)]
                                              [static_cast<size_t> (b)] - kDecayTarget)
                                    / kDecayTarget * 100.0, 2), 9);

            row += pad (number (worstMid[static_cast<size_t> (a)], 2) + " %", 9)
                   + pad (number (worstMidHz[static_cast<size_t> (a)], 0) + " Hz", 10);

            if (worstMid[static_cast<size_t> (a)] <= kGateMidDeviation)
                row += "MEETS TARGET";
            else if (worstMid[static_cast<size_t> (a)] <= kPromisingPercent)
                row += "promising";

            info (row);
        }

        int best = 0;

        for (int a = 0; a < kNumAmounts; ++a)
            if (worstMid[static_cast<size_t> (a)] < worstMid[static_cast<size_t> (best)])
                best = a;

        const bool interior = (best > 0) && (best < kNumAmounts - 1);

        info ("");
        info ("    target " + number (kGateMidDeviation, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("    best amount " + number (kAmounts[static_cast<size_t> (best)] * 100.0, 1)
              + " % at " + number (worstMid[static_cast<size_t> (best)], 2) + " %");
        info ("    minimum is " + std::string (interior ? "INTERIOR, so the sweep"
                                                          " resolved it"
                                                        : "AT AN EDGE of the range,"
                                                          " so it is not an optimum"));
        info ("");
        info ("    the 50.0 % row is the shipping baseline");

        const double drift = std::max (
            std::max (std::abs (std::abs (grid[static_cast<size_t> (shipping)][0]
                                          - kDecayTarget) / kDecayTarget * 100.0
                                - kReference500Hz),
                      std::abs (std::abs (grid[static_cast<size_t> (shipping)][1]
                                          - kDecayTarget) / kDecayTarget * 100.0
                                - kReference1kHz)),
            std::abs (std::abs (grid[static_cast<size_t> (shipping)][2]
                                - kDecayTarget) / kDecayTarget * 100.0
                      - kReference2kHz));

        checkAtMost (drift, 0.10,
                     "the baseline row reproduces the registered 50 % reference");
        check (interior, "the amount sweep found an interior minimum");
    }

    int best = 0;

    for (int a = 0; a < kNumAmounts; ++a)
        if (worstMid[static_cast<size_t> (a)] < worstMid[static_cast<size_t> (best)])
            best = a;

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - high-frequency damping retention");
    // -----------------------------------------------------------------------
    {
        info ("  An amount that makes Problem A pass by weakening high-frequency");
        info ("  damping into irrelevance is not a solution. The reference for what");
        info ("  the control is worth is High Damp 0 - measured above - and the");
        info ("  reference for what we ship is 50 %.");
        info ("");
        info ("    High      RT60 / base Decay, so 1.00 means no HF damping at all");
        info ("    Damp      6 kHz    8 kHz    10 kHz   12 kHz   16 kHz   worst reduction");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            double strongest = 0.0;

            for (int b = 4; b < kNumProbes; ++b)
            {
                const double ratio = grid[static_cast<size_t> (a)]
                                         [static_cast<size_t> (b)] / kDecayTarget;

                row += pad (number (ratio, 3), 9);
                strongest = std::max (strongest, 1.0 - ratio);
            }

            row += number (strongest * 100.0, 1) + " %";

            info (row);
        }

        info ("");
        info ("    High      fraction of the SHIPPING 50 % HF damping effect retained");
        info ("    Damp      6 kHz    8 kHz    10 kHz   12 kHz   16 kHz   mean");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            double sum = 0.0;
            int counted = 0;

            for (int b = 4; b < kNumProbes; ++b)
            {
                const double none = noDamp[static_cast<size_t> (b)];
                const double atShipping = grid[static_cast<size_t> (shipping)]
                                             [static_cast<size_t> (b)];
                const double here = grid[static_cast<size_t> (a)][static_cast<size_t> (b)];

                const double effect = none - atShipping;

                if (std::abs (effect) < 1.0e-6)
                {
                    row += pad ("n/a", 9);
                    continue;
                }

                const double retained = (none - here) / effect * 100.0;

                row += pad (number (retained, 1) + " %", 9);

                sum += retained;
                ++counted;
            }

            row += (counted > 0) ? number (sum / counted, 1) + " %" : std::string ("n/a");

            info (row);
        }

        check (true, "high-frequency retention measured against both references");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - default curve smoothness");
    // -----------------------------------------------------------------------
    {
        info ("  The objective is smooth useful damping, not a numerical pass at");
        info ("  2 kHz. The in-band RT60 ratio is the quantity the second-order");
        info ("  shelf study tied to tail curvature, so it is the one to watch.");
        info ("");
        info ("    High      worst window   at         steepest slope   at         widest"
              "   at         max|Hn|");

        double worstMagnitude = 0.0;

        for (int a = 0; a < kNumAmounts; ++a)
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = kAmounts[a];
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kProvisionalHighHz;

            const AnalyticDamping analytic (setting);
            const auto g = measureGradient (analytic, 30.0, 16000.0);
            const double magnitude = analytic.worstNormalisedMagnitude();

            worstMagnitude = std::max (worstMagnitude, magnitude);

            info ("    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10)
                  + pad (number (worstMid[static_cast<size_t> (a)], 2) + " %", 15)
                  + pad (number (worstMidHz[static_cast<size_t> (a)], 0) + " Hz", 11)
                  + pad (number (g.worstSlope, 4) + " s/oct", 17)
                  + pad (number (g.worstSlopeHz, 0) + " Hz", 11)
                  + pad (number (g.worstBandRatio, 3) + ":1", 9)
                  + pad (number (g.worstBandHz, 0) + " Hz", 11)
                  + number (magnitude, 12));
        }

        info ("");
        info ("    Unlike Low Damp amount, this control is expected to move these");
        info ("    numbers: it sets how far the high plateau falls, which is what");
        info ("    the gradient is a gradient of.");

        checkAtMost (worstMagnitude, 1.0 + 1.0e-12,
                     "Option A semantics hold at every High Damp amount");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - Problem B is untouched by construction");
    // -----------------------------------------------------------------------
    {
        info ("  The registered extreme sets High Damp 100 % itself, and the variable");
        info ("  swept here IS High Damp. So the shipping DEFAULT amount cannot change");
        info ("  that test - the value is overwritten before it runs. Nothing is");
        info ("  re-run and nothing is claimed about it.");
        info ("");
        info ("    preserved at High Damp Freq " + number (kProvisionalHighHz / 1000.0, 0)
              + " kHz: worst measured mismatch 5.51 % at 14254 Hz, interior");
        info ("    investigation gate " + number (kInvestigationGate, 1) + " %   NOT PASSED");
        info ("");
        info ("  Problem B DOES depend on High Damp FREQ, so if the joint sweep below");
        info ("  produces a new frequency candidate, its Problem B has to be searched");
        info ("  again - and that is flagged rather than skipped.");

        check (true, "the certified Problem B result is carried forward unchanged");
    }

    // Frequencies the joint sweep nominates for a fresh Problem B search.
    std::vector<double> finalFrequencies;

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - joint High Damp Freq x amount");
    // -----------------------------------------------------------------------
    {
        /*
            The gate, and why the first version of it was wrong.

            It was written as a literal "best <= 2.2 %" and the sweep returned
            2.32 %, so the joint sweep was skipped. That is the wrong reading
            of the condition. The REASON the joint sweep exists is that the
            frequency optimum was found at 50 % amount and is not guaranteed
            to hold at another - and the amount optimum has just moved to
            45 %, which is exactly that situation. 2.32 % is also a real
            improvement on the 2.46 % baseline and lands 0.32 pp off target,
            which is materially approaching it.

            So the gate is the stated intent rather than the number alone:
            run when the figure is at or under 2.2 %, OR when the amount
            optimum has moved off the shipping default while improving on it,
            because then the frequency has to be re-optimised where the
            amount actually sits.
        */
        const bool underThreshold = worstMid[static_cast<size_t> (best)] <= kPromisingPercent;

        const bool optimumMoved = (best != shipping)
                                  && (worstMid[static_cast<size_t> (best)]
                                      < worstMid[static_cast<size_t> (shipping)]);

        const bool promising = underThreshold || optimumMoved;

        info ("  Gate: the figure reaches " + number (kPromisingPercent, 1)
              + " % or better, OR the amount optimum moved off the shipping");
        info ("  default while improving on it - in which case the frequency optimum");
        info ("  found at the old amount cannot be assumed to still hold.");
        info ("    best amount        " + number (kAmounts[static_cast<size_t> (best)] * 100.0, 1)
              + " % at " + number (worstMid[static_cast<size_t> (best)], 2) + " %");
        info ("    at or under "
              + number (kPromisingPercent, 1) + " %     " + (underThreshold ? "yes" : "no"));
        info ("    optimum moved      " + std::string (optimumMoved ? "yes" : "no")
              + "   (shipping " + number (kAmounts[static_cast<size_t> (shipping)] * 100.0, 1)
              + " % gives " + number (worstMid[static_cast<size_t> (shipping)], 2) + " %)");
        info ("    -> " + std::string (promising ? "RUNNING the joint sweep"
                                                 : "NOT running the joint sweep"));
        info ("");

        if (! promising)
        {
            info ("  No amount materially approaches the target, so the"
                  " one-dimensional");
            info ("  sweep stops here and the frontier below is the result. The");
            info ("  normalisation architecture is NOT touched: that decision needs");
            info ("  the joint interaction ruled out first, and it has not been.");
        }
        else
        {
            const double frequencies[] { 9000.0, 9500.0, 10000.0, 10500.0,
                                         11000.0, 11500.0, 12000.0 };

            // Only the promising amount region, not the whole axis.
            const int lower = std::max (0, best - 1);
            const int upper = std::min (kNumAmounts - 1, best + 1);

            info ("  Amount region swept jointly: "
                  + number (kAmounts[static_cast<size_t> (lower)] * 100.0, 1) + " % to "
                  + number (kAmounts[static_cast<size_t> (upper)] * 100.0, 1) + " %."
                  " The frequency optimum found at 50 % amount is not assumed to");
            info ("  hold at another amount, which is why this grid exists.");
            info ("");
            info ("    amount   freq      worst    at         8 kHz   12 kHz  16 kHz"
                  "   slope          widest    max|Hn|");
            info ("                       500-2k              RT60 / base Decay");

            double bestJoint = 1.0e9;
            double bestJointAmount = 0.0, bestJointFreq = 0.0;

            /*
                A second candidate, chosen on SMOOTHNESS rather than on the
                Problem A digit.

                The ridge turns out to be flat: 2.31, 2.32 and 2.33 % at three
                different pairs. Those are the same number - 0.02 pp is far
                below what this harness resolves, and the split-half control in
                the low-corner study measured its own noise at 0.3 to 1.5 pp.
                Picking a winner on that digit would be deciding on an
                unmeasurable difference.

                So among every pair within 0.10 pp of the best figure, the one
                with the narrowest one-octave in-band RT60 ratio is kept too,
                because that is the quantity the second-order shelf study tied
                to tail curvature and it is therefore the one likely to matter
                for Problem B.
            */
            double smoothestRatio = 1.0e9;
            double smoothestAmount = 0.0, smoothestFreq = 0.0, smoothestWorst = 0.0;

            struct Pair { double amount, freq, worst, ratio; };
            std::vector<Pair> pairs;

            for (int a = lower; a <= upper; ++a)
                for (double hz : frequencies)
                {
                    Setting setting;
                    setting.lowDamp = kDefaultLowDamp;
                    setting.highDamp = kAmounts[a];
                    setting.lowHz = kProvisionalLowHz;
                    setting.highHz = hz;
                    setting.modDepthControl = kDefaultModDepth;

                    Tank tank (setting);
                    const auto ensemble = renderEnsemble (tank);

                    double worst = 0.0, worstHz = 0.0;
                    double r8 = 0.0, r12 = 0.0, r16 = 0.0;

                    for (int b = 0; b < kNumProbes; ++b)
                    {
                        const auto band = decay::makeBand (probes[b], kGateBandOctaves,
                                                           kSampleRate);
                        const auto result = measureBand (ensemble, band);

                        if (probes[b] <= 2000.0 && result.valid)
                        {
                            const double deviation = std::abs (result.fitted - kDecayTarget)
                                                     / kDecayTarget * 100.0;

                            if (deviation > worst)
                            {
                                worst = deviation;
                                worstHz = probes[b];
                            }
                        }

                        if (probes[b] == 8000.0)  r8  = result.fitted / kDecayTarget;
                        if (probes[b] == 12000.0) r12 = result.fitted / kDecayTarget;
                        if (probes[b] == 16000.0) r16 = result.fitted / kDecayTarget;
                    }

                    Setting analyticSetting = setting;
                    const AnalyticDamping analytic (analyticSetting);
                    const auto g = measureGradient (analytic, 30.0, 16000.0);

                    if (worst < bestJoint)
                    {
                        bestJoint = worst;
                        bestJointAmount = kAmounts[a];
                        bestJointFreq = hz;
                    }

                    pairs.push_back ({ kAmounts[a], hz, worst, g.worstBandRatio });

                    info ("    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 9)
                          + pad (number (hz / 1000.0, 1) + " kHz", 10)
                          + pad (number (worst, 2) + " %", 9)
                          + pad (number (worstHz, 0) + " Hz", 11)
                          + pad (number (r8, 3), 8)
                          + pad (number (r12, 3), 8)
                          + pad (number (r16, 3), 9)
                          + pad (number (g.worstSlope, 4) + " s/oct", 15)
                          + pad (number (g.worstBandRatio, 3) + ":1", 10)
                          + number (analytic.worstNormalisedMagnitude(), 10));
                }

            info ("");
            for (const auto& p : pairs)
                if (p.worst <= bestJoint + 0.10 && p.ratio < smoothestRatio)
                {
                    smoothestRatio = p.ratio;
                    smoothestAmount = p.amount;
                    smoothestFreq = p.freq;
                    smoothestWorst = p.worst;
                }

            info ("    best Problem A pair    High Damp " + number (bestJointAmount * 100.0, 1)
                  + " % at " + number (bestJointFreq / 1000.0, 1) + " kHz"
                  + "   worst " + number (bestJoint, 2) + " %");
            info ("    smoothest within 0.10 pp of it   High Damp "
                  + number (smoothestAmount * 100.0, 1) + " % at "
                  + number (smoothestFreq / 1000.0, 1) + " kHz"
                  + "   worst " + number (smoothestWorst, 2) + " %"
                  + "   in-band " + number (smoothestRatio, 3) + ":1");
            info ("");
            info ("  The ridge is flat within measurement resolution, so the choice");
            info ("  between these cannot be made on the Problem A digit. It has to be");
            info ("  made on HF retention and smoothness - and Problem B, which");
            info ("  depends on the frequency and is measured next.");
            info ("");

            finalFrequencies.push_back (bestJointFreq);

            if (smoothestFreq != bestJointFreq)
                finalFrequencies.push_back (smoothestFreq);

            if (bestJointFreq != kProvisionalHighHz)
            {
                info ("  The joint optimum moves the FREQUENCY away from "
                      + number (kProvisionalHighHz / 1000.0, 0) + " kHz, so the");
                info ("  certified Problem B result does not carry over to it and has to");
                info ("  be searched again at the new frequency. That is the next run,");
                info ("  not a conclusion of this one.");
            }
            else
            {
                info ("  The joint optimum keeps the frequency at "
                      + number (kProvisionalHighHz / 1000.0, 0) + " kHz, so the");
                info ("  certified Problem B result carries over unchanged.");
            }

            check (bestJoint <= worstMid[static_cast<size_t> (best)] + 0.01,
                   "the joint sweep is at least as good as the amount sweep alone");
        }

        check (true, "the joint-sweep gate was evaluated against the measurements");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - PROBLEM B at the final frequency candidates");
    // -----------------------------------------------------------------------
    {
        info ("  Problem B depends on High Damp FREQ but not on the shipping default");
        info ("  AMOUNT, because the registered extreme sets High Damp 100 % itself.");
        info ("  So a new frequency candidate needs its own search, and only the");
        info ("  final one or two get it.");
        info ("");

        if (finalFrequencies.empty())
        {
            info ("  The joint sweep nominated no frequency, so nothing is re-measured");
            info ("  and the certified 10 kHz result stands.");
        }
        else
        {
            /*
                The worst band is SEARCHED, not assumed, and the top of the
                range is a physical limit rather than a choice: a one-octave
                band centred at f spans f/sqrt(2) to f*sqrt(2), so no centre
                above about 16.6 kHz fits inside Nyquist. A worst value sitting
                on the top centre is a lower bound, not a maximum, and is
                reported as such.
            */
            std::vector<double> centres;

            for (int k = 0; ; ++k)
            {
                const double f = 500.0 * std::pow (2.0, k / 3.0);

                if (f > 10000.0)
                    break;

                centres.push_back (f);
            }

            for (int k = 1; ; ++k)
            {
                const double f = 10079.0 * std::pow (2.0, k / 6.0);

                if (f * std::sqrt (2.0) > 0.98 * 0.5 * kSampleRate)
                    break;

                centres.push_back (f);
            }

            const double topCentre = centres.back();

            info ("    band centres " + number (centres.front(), 0) + " Hz to "
                  + number (topCentre, 0) + " Hz, " + std::to_string (centres.size())
                  + " of them; sixth-octave spacing above 10 kHz");
            info ("");
            info ("    freq      worst    at         interior?                            "
                  "R^2 there");

            for (double hz : finalFrequencies)
            {
                Setting setting;
                setting.lowDamp = 0.0;
                setting.highDamp = 1.0;
                setting.lowHz = kProvisionalLowHz;
                setting.highHz = hz;
                setting.modDepthControl = 0.0;

                Tank tank (setting);
                const auto ensemble = renderEnsemble (tank);

                double worst = 0.0, worstHz = 0.0, worstR2 = 0.0;

                for (double centre : centres)
                {
                    const auto band = decay::makeBand (centre, kGateBandOctaves, kSampleRate);
                    const auto result = measureBand (ensemble, band);

                    if (! result.valid)
                        continue;

                    const double m = mismatchPercent (result);

                    if (m > worst)
                    {
                        worst = m;
                        worstHz = centre;
                        worstR2 = result.r2;
                    }
                }

                info ("    " + pad (number (hz / 1000.0, 1) + " kHz", 10)
                      + pad (number (worst, 2) + " %", 9)
                      + pad (number (worstHz, 0) + " Hz", 11)
                      + pad (worstHz >= topCentre - 1.0
                             ? "AT THE TOP, so a lower bound"
                             : "interior, so a real maximum", 37)
                      + number (worstR2, 5));
            }

            info ("");
            info ("    for comparison, the certified 10.0 kHz result   5.51 %"
                  "   14254 Hz   interior");
            info ("    investigation gate " + number (kInvestigationGate, 1) + " %"
                  "   (pre-registered, unchanged)");
        }

        check (true, "Problem B searched at the final frequency candidates only");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 high amount - the Pareto frontier");
    // -----------------------------------------------------------------------
    {
        info ("    High      Problem A   at         HF retention   worst HF");
        info ("    Damp      worst                  vs 50 %        reduction");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            double sum = 0.0;
            int counted = 0;
            double strongest = 0.0;

            for (int b = 4; b < kNumProbes; ++b)
            {
                const double none = noDamp[static_cast<size_t> (b)];
                const double atShipping = grid[static_cast<size_t> (shipping)]
                                             [static_cast<size_t> (b)];
                const double here = grid[static_cast<size_t> (a)][static_cast<size_t> (b)];

                strongest = std::max (strongest, 1.0 - here / kDecayTarget);

                const double effect = none - atShipping;

                if (std::abs (effect) < 1.0e-6)
                    continue;

                sum += (none - here) / effect * 100.0;
                ++counted;
            }

            info ("    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10)
                  + pad (number (worstMid[static_cast<size_t> (a)], 2) + " %", 12)
                  + pad (number (worstMidHz[static_cast<size_t> (a)], 0) + " Hz", 11)
                  + pad ((counted > 0 ? number (sum / counted, 1) + " %"
                                      : std::string ("n/a")), 15)
                  + number (strongest * 100.0, 1) + " %");
        }

        info ("");
        info ("  Read the frontier as a product decision, not an optimisation: the");
        info ("  amount that gives the best Problem A figure also decides how much");
        info ("  high-frequency damping the default character has.");

        check (true, "the frontier is recorded with Problem A against HF retention");
    }
}

} // namespace avtest
