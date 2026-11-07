#include "DampingSweepHarness.h"
#include "Suites.h"
#include "TestFramework.h"

/*
    PHASE 7 - TEST-ONLY sweep of the default Low Damp AMOUNT.

    Nothing in Source/ changes. No default, no mapping, no Option A semantic
    is touched. High Damp Freq 10 kHz and Low Damp Freq 200 Hz are the
    provisional study candidates the two frequency sweeps settled on, not
    shipped values.

    THE QUESTION

    Both corner frequencies are now closed as levers. The High Damp sweep
    bottomed out at 2.46 % limited by 500 Hz; the Low Damp Freq sweep bottomed
    out at the same 2.46 % at the shipping 200 Hz, limited by 2 kHz. The two
    are coupled through Option A's composite-maximum normalisation - one
    scalar per line - so the 500 Hz to 2 kHz window cannot be optimised one
    corner at a time. What is left on this axis is the AMOUNT.

    AMOUNT ONLY. Both corners are held fixed.

    WHAT TO EXPECT, AND WHY IT IS NOT OBVIOUS

    Reducing Low Damp should release 500 Hz. It will not only do that. With
    Low Damp 0 the low shelf is an identity, so max|H_pair| is max|H_high|,
    which peaks at exactly 1 at DC, so the normalisation is exactly 1. With
    Low Damp 20 % the maximum sits slightly below 1, so the normalisation is
    slightly ABOVE 1 and quietly lengthens the whole mid and high region.
    Taking the amount away removes that lengthening, which shortens 2 kHz and
    pushes its deviation the wrong way - the same coupling that made lowering
    the corner backfire. The full curve is therefore reported for every
    amount rather than just the 500 Hz column.

    PROBLEM B IS NOT RE-RUN, AND HERE THERE IS NOTHING TO PROVE

    The registered High-Damp extreme sets Low Damp 0 itself. The swept
    variable IS Low Damp, so that test does not depend on this study's
    parameter at all - not by an identity that needs demonstrating, but
    because the value is overwritten. The certified result stands: High Damp
    Freq 10 kHz, worst measured Problem B mismatch 5.51 % at 14254 Hz, and
    the 5 % investigation gate is unchanged and still not passed.
*/

namespace avtest
{
namespace
{

using namespace avtest::sweep;

/*
    Low Damp control positions. 0.20 is the current shipping default.

    The first pass ran 0 to 20 % and found the best figure at 20 %, the TOP of
    the range, improving monotonically toward it - the third time this study
    has produced an edge-pinned optimum, and it is the signature of the
    coupling rather than a coincidence. An edge value cannot be reported as an
    optimum, so the range is extended upward until the figure turns around.

    The reason to expect a turn: at 20 % the two ends of the window are nearly
    level, 500 Hz at 2.25 % and 2 kHz at 2.46 %. More amount keeps improving
    2 kHz through the normalisation boost while 500 Hz gets damped harder, so
    the worst-of-window metric has to cross over somewhere just above.
*/
constexpr double kAmounts[] { 0.0, 0.05, 0.10, 0.125, 0.15, 0.175,
                              0.20, 0.225, 0.25, 0.30, 0.40 };
constexpr int kNumAmounts = 11;

constexpr double kProvisionalLowHz = 200.0;

// Registered reference at Low Damp 20 %, for the faithfulness check.
constexpr double kReference500Hz = 2.25;
constexpr double kReference1kHz  = 0.56;
constexpr double kReference2kHz  = 2.46;

} // namespace

void runDampingLowAmountSweepTests()
{
    const double probes[] { 60.0, 80.0, 100.0, 125.0, 160.0, 250.0, 500.0,
                            1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0 };
    constexpr int kNumProbes = 13;

    // Found by value, since 20 % is no longer the last entry in the sweep.
    int shipping = 0;

    for (int a = 0; a < kNumAmounts; ++a)
        if (kAmounts[a] == kDefaultLowDamp)
            shipping = a;

    std::array<std::array<double, kNumProbes>, kNumAmounts> grid {};
    std::array<double, kNumAmounts> worstMid {};
    std::array<double, kNumAmounts> worstMidHz {};

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - PROBLEM A, the full curve per amount");
    // -----------------------------------------------------------------------
    {
        info ("  Decay " + number (kDecayTarget, 1) + " s, High Damp "
              + number (kDefaultHighDamp * 100.0, 0) + " %, High Damp Freq "
              + number (kCandidateHighHz / 1000.0, 0) + " kHz,");
        info ("  Low Damp Freq " + number (kProvisionalLowHz, 0) + " Hz, Mod Depth "
              + number (kDefaultModDepth * 100.0, 0) + " %, Mod Rate "
              + number (kDefaultModRateHz, 2) + " Hz.");
        info ("  Both corners held fixed. Amount only.");
        info ("");
        info ("  The whole curve is reported for every amount, not just the 500 Hz");
        info ("  column, because Option A normalises each line by one scalar and");
        info ("  changing the low section moves the mid and high region with it.");
        info ("");
        info ("    Low                                      RT60 in seconds, base Decay "
              + number (kDecayTarget, 2) + " s");
        info ("    Damp      60 Hz    80 Hz    100 Hz   125 Hz   160 Hz   250 Hz   500 Hz"
              "   1 kHz    2 kHz    4 kHz    8 kHz    12 kHz   16 kHz");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            Setting setting;
            setting.lowDamp = kAmounts[a];
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kCandidateHighHz;
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

                if (probes[b] >= 500.0 && probes[b] <= 2000.0 && result.valid)
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
        info ("    Low       deviation from base Decay, per cent");
        info ("    Damp      60 Hz    160 Hz   250 Hz   500 Hz   1 kHz    2 kHz"
              "    worst    at");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            const int columns[] { 0, 4, 5, 6, 7, 8 };

            for (int k = 0; k < 6; ++k)
                row += pad (number (std::abs (grid[static_cast<size_t> (a)]
                                              [static_cast<size_t> (columns[k])]
                                              - kDecayTarget) / kDecayTarget * 100.0, 2), 9);

            row += pad (number (worstMid[static_cast<size_t> (a)], 2) + " %", 9)
                   + number (worstMidHz[static_cast<size_t> (a)], 0) + " Hz";

            if (worstMid[static_cast<size_t> (a)] <= kGateMidDeviation)
                row += "   MEETS TARGET";

            info (row);
        }

        info ("");
        info ("    target " + number (kGateMidDeviation, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("    the 20.0 % row is the shipping baseline");
        info ("");

        // The faithfulness check: the 20 % row must reproduce the registered point.
        const double here500 = std::abs (grid[static_cast<size_t> (shipping)][6] - kDecayTarget)
                               / kDecayTarget * 100.0;
        const double here1k = std::abs (grid[static_cast<size_t> (shipping)][7] - kDecayTarget)
                              / kDecayTarget * 100.0;
        const double here2k = std::abs (grid[static_cast<size_t> (shipping)][8] - kDecayTarget)
                              / kDecayTarget * 100.0;

        const double drift = std::max (std::max (std::abs (here500 - kReference500Hz),
                                                 std::abs (here1k - kReference1kHz)),
                                       std::abs (here2k - kReference2kHz));

        info ("    baseline against the registered reference");
        info ("      500 Hz  " + number (here500, 2) + " % vs "
              + number (kReference500Hz, 2) + " %");
        info ("      1 kHz   " + number (here1k, 2) + " % vs "
              + number (kReference1kHz, 2) + " %");
        info ("      2 kHz   " + number (here2k, 2) + " % vs "
              + number (kReference2kHz, 2) + " %");

        checkAtMost (drift, 0.10,
                     "the baseline row reproduces the registered 20 % reference");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - low-frequency function retention");
    // -----------------------------------------------------------------------
    {
        info ("  Low Damp 0 is the first row of the sweep, so it is already the");
        info ("  reference for what the control is worth, and 20 % is what we ship.");
        info ("  Retention is expressed against the shipping 20 % effect.");
        info ("");
        info ("    Low       fraction of the SHIPPING low-damping effect retained");
        info ("    Damp      60 Hz    80 Hz    100 Hz   125 Hz   160 Hz   250 Hz   mean");

        std::array<double, kNumAmounts> meanRetention {};

        for (int a = 0; a < kNumAmounts; ++a)
        {
            std::string row = "    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            double sum = 0.0;
            int counted = 0;

            for (int b = 0; b < 6; ++b)
            {
                const double none = grid[0][static_cast<size_t> (b)];
                /*
                    The index here used to be written as a local named
                    `shipping`, which a careless global substitution then
                    turned into a self-initialising declaration - the variable
                    used as its own array index. The compiler accepted it and
                    the retention table came out meaningless, showing 10 % as
                    the 100 % reference instead of 20 %. The local is renamed
                    so the index cannot be shadowed again.
                */
                const double atShipping = grid[static_cast<size_t> (shipping)]
                                              [static_cast<size_t> (b)];
                const double here = grid[static_cast<size_t> (a)][static_cast<size_t> (b)];

                const double shippingEffect = none - atShipping;

                if (std::abs (shippingEffect) < 1.0e-6)
                {
                    row += pad ("n/a", 9);
                    continue;
                }

                const double retained = (none - here) / shippingEffect * 100.0;

                row += pad (number (retained, 1) + " %", 9);

                sum += retained;
                ++counted;
            }

            meanRetention[static_cast<size_t> (a)] = (counted > 0) ? sum / counted : 0.0;

            row += number (meanRetention[static_cast<size_t> (a)], 1) + " %";

            info (row);
        }

        info ("");
        info ("    RT60 in the bass, so the cost is readable in seconds and not only");
        info ("    as a percentage");
        info ("      Low Damp    60 Hz     160 Hz    250 Hz");

        for (int a = 0; a < kNumAmounts; ++a)
            info ("      " + pad (number (kAmounts[a] * 100.0, 1) + " %", 12)
                  + pad (number (grid[static_cast<size_t> (a)][0], 4) + " s", 10)
                  + pad (number (grid[static_cast<size_t> (a)][4], 4) + " s", 10)
                  + number (grid[static_cast<size_t> (a)][5], 4) + " s");

        info ("");
        info ("  No retention threshold is proposed. The tradeoff is exposed and the");
        info ("  product cost is stated in plain terms in the decision suite below.");

        check (true, "low-frequency retention measured against both references");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - the shape of the default curve");
    // -----------------------------------------------------------------------
    {
        info ("  The objective is not only to pass one measurement. A default that");
        info ("  moves the error somewhere else is not an improvement, so the");
        info ("  smoothness of the whole curve is reported alongside the window");
        info ("  figure: the steepest local slope anywhere from 30 Hz to 16 kHz, and");
        info ("  the widest RT60 ratio any single one-octave band has to hold.");
        info ("");
        info ("    Low       worst window   at         steepest slope   at         widest"
              "   at         max|Hn|");
        info ("    Damp      500 Hz - 2 kHz            30 Hz - 16 kHz              in-band");

        double worstMagnitude = 0.0;

        for (int a = 0; a < kNumAmounts; ++a)
        {
            Setting setting;
            setting.lowDamp = kAmounts[a];
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kCandidateHighHz;

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
        info ("    The steepest slope and widest in-band ratio live on the HIGH");
        info ("    transition at this operating point, so they barely move with Low");
        info ("    Damp. A number that does not change across the sweep is evidence");
        info ("    that this control is not where the curve's roughness comes from.");
        info ("");
        info ("    max|Hn| must stay at or below 1 for Option A to hold - damping");
        info ("    subtractive, decay never extended above base Decay.");

        checkAtMost (worstMagnitude, 1.0 + 1.0e-12,
                     "Option A semantics hold at every Low Damp amount");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - low-side curvature, analytic first");
    // -----------------------------------------------------------------------

    int best = 0;

    for (int a = 0; a < kNumAmounts; ++a)
        if (worstMid[static_cast<size_t> (a)] < worstMid[static_cast<size_t> (best)])
            best = a;

    {
        info ("  The previous sweep established that low-side curvature differences");
        info ("  sit at the estimator noise floor - a split-half control gave 1.46 pp");
        info ("  against a 1.73 pp between-candidate spread. So the cheap analytic");
        info ("  diagnostic runs for every amount first, and the ensemble is paid for");
        info ("  only where it can tell something apart.");
        info ("");
        info ("    Low       in-band RT60 ratio over the low transition, 30 Hz - 600 Hz");
        info ("    Damp      widest ratio   at         steepest slope   at");

        for (int a = 0; a < kNumAmounts; ++a)
        {
            Setting setting;
            setting.lowDamp = kAmounts[a];
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kCandidateHighHz;

            const AnalyticDamping analytic (setting);
            const auto g = measureGradient (analytic, 30.0, 600.0);

            info ("    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 10)
                  + pad (number (g.worstBandRatio, 4) + " : 1", 15)
                  + pad (number (g.worstBandHz, 0) + " Hz", 11)
                  + pad (number (g.worstSlope, 4) + " s/oct", 17)
                  + number (g.worstSlopeHz, 0) + " Hz");
        }

        info ("");
        info ("  Ensemble measurement for three points only: the shipping 20 %");
        info ("  baseline, the best Problem-A amount, and a lower control.");
        info ("");

        const double bands[] { 63.0, 80.0, 125.0, 180.0, 250.0, 355.0, 500.0 };

        const int chosen[] { shipping, best, 0 };
        const char* labels[] { "20 % baseline", "best Problem A", "0 % control" };

        info ("    which                 amount    63 Hz    80 Hz    125 Hz   180 Hz"
              "   250 Hz   355 Hz   500 Hz   worst");

        for (int k = 0; k < 3; ++k)
        {
            const int a = chosen[k];

            Setting setting;
            setting.lowDamp = kAmounts[a];
            setting.highDamp = 0.0;
            setting.lowHz = kProvisionalLowHz;
            setting.highHz = kCandidateHighHz;
            setting.modDepthControl = 0.0;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::string row = "    " + pad (labels[k], 22)
                              + pad (number (kAmounts[a] * 100.0, 1) + " %", 10);

            double worst = 0.0;

            for (int b = 0; b < 7; ++b)
            {
                const auto band = decay::makeBand (bands[b], kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                const double m = mismatchPercent (result);

                row += pad (number (m, 2), 9);

                if (result.valid)
                    worst = std::max (worst, m);
            }

            row += number (worst, 2) + " %";

            info (row);
        }

        info ("");
        info ("  High Damp is 0 in these three rows, so this is the low section on its");
        info ("  own. These numbers create NO quality gate; they exist to show whether");
        info ("  reducing the amount moves curvature into the bass.");

        check (true, "low-side curvature measured analytically and at three points");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - Problem B is untouched by construction");
    // -----------------------------------------------------------------------
    {
        info ("  The registered High-Damp extreme sets Low Damp 0 itself, and the");
        info ("  variable swept here IS Low Damp. So that test does not depend on this");
        info ("  study's parameter at all - not through an identity that needs");
        info ("  demonstrating, but because the value is overwritten before the test");
        info ("  runs. Nothing is re-run and nothing is claimed about it.");
        info ("");
        info ("    preserved: High Damp Freq            "
              + number (kCandidateHighHz / 1000.0, 0) + " kHz");
        info ("    preserved: worst Problem B mismatch   5.51 %   at 14254 Hz, interior");
        info ("    investigation gate                    "
              + number (kInvestigationGate, 1) + " %   NOT PASSED");

        check (true, "the certified Problem B result is carried forward unchanged");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low amount - decision, product cost and the frontier");
    // -----------------------------------------------------------------------
    {
        bool anyMet = false;

        for (int a = 0; a < kNumAmounts; ++a)
            anyMet = anyMet || worstMid[static_cast<size_t> (a)] <= kGateMidDeviation;

        info ("    Low Damp   Problem A worst   at         meets target");

        for (int a = 0; a < kNumAmounts; ++a)
            info ("    " + pad (number (kAmounts[a] * 100.0, 1) + " %", 11)
                  + pad (number (worstMid[static_cast<size_t> (a)], 2) + " %", 18)
                  + pad (number (worstMidHz[static_cast<size_t> (a)], 0) + " Hz", 11)
                  + (worstMid[static_cast<size_t> (a)] <= kGateMidDeviation ? "yes" : "no"));

        info ("");
        info ("    best amount " + number (kAmounts[static_cast<size_t> (best)] * 100.0, 1)
              + " % at " + number (worstMid[static_cast<size_t> (best)], 2) + " %"
              + (anyMet ? "   target reached"
                        : "   TARGET NOT REACHED BY ANY AMOUNT"));
        info ("");

        // The product cost, in plain terms.
        double retained = 0.0;
        int counted = 0;

        for (int b = 0; b < 6; ++b)
        {
            const double none = grid[0][static_cast<size_t> (b)];
            const double atShipping = grid[static_cast<size_t> (shipping)]
                                          [static_cast<size_t> (b)];
            const double here = grid[static_cast<size_t> (best)][static_cast<size_t> (b)];

            // Second instance of the same substitution fault: this was
            // none - shipping, subtracting the integer INDEX from an RT60.
            const double effect = none - atShipping;

            if (std::abs (effect) < 1.0e-6)
                continue;

            retained += (none - here) / effect * 100.0;
            ++counted;
        }

        if (counted > 0)
            retained /= counted;

        info ("  PRODUCT COST IN PLAIN TERMS");

        if (kAmounts[static_cast<size_t> (best)] == kDefaultLowDamp)
        {
            info ("    The best amount IS the shipping default, so there is no change to");
            info ("    cost and nothing to trade. Reducing Low Damp does not improve the");
            info ("    Problem A figure.");
        }
        else
        {
            info ("    Low Damp default would be reduced from "
                  + number (kDefaultLowDamp * 100.0, 1) + " % to "
                  + number (kAmounts[static_cast<size_t> (best)] * 100.0, 1) + " %,");
            info ("    which retains " + number (retained, 1)
                  + " % of the current 60-250 Hz damping effect");
            info ("    and takes the worst 500 Hz - 2 kHz deviation from "
                  + number (worstMid[static_cast<size_t> (shipping)], 2) + " % to "
                  + number (worstMid[static_cast<size_t> (best)], 2) + " %.");
            info ("    At 60 Hz the tail goes from "
                  + number (grid[static_cast<size_t> (shipping)][0], 3) + " s to "
                  + number (grid[static_cast<size_t> (best)][0], 3) + " s.");
        }

        info ("");

        if (! anyMet)
        {
            info ("  No amount reaches the target, so this study stops here. The");
            info ("  normalisation architecture is NOT modified: that is a separate");
            info ("  decision about whether the remaining gap warrants it, and folding");
            info ("  it into a defaults study would make neither attributable.");
        }

        check (true, "the frontier and the product cost are recorded either way");
    }
}

} // namespace avtest
