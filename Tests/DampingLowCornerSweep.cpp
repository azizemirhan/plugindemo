#include "DampingSweepHarness.h"
#include "Suites.h"
#include "TestFramework.h"

/*
    PHASE 7 - TEST-ONLY sweep of the default Low Damp Freq.

    Nothing in Source/ changes. No default, no mapping, no parameter range is
    touched, and High Damp Freq 10 kHz is a provisional study candidate, not a
    production default.

    THE QUESTION

    The High Damp Freq sweep took Problem A from 7.90 % to 2.46 % at a 10 kHz
    corner and then stopped improving, because the worst band changed
    identity: 2 kHz kept recovering as the corner rose while 500 Hz drifted
    the other way, and at 10 kHz the two were level at 2.25 % and 2.46 %. The
    remaining error is therefore a LOW-end error. This study moves the low
    corner down and asks whether that removes it.

    FREQUENCY ONLY. Low Damp amount stays at the shipping 20 %. Amount and
    frequency are not tuned in the same study, so that whichever one matters
    can be attributed.

    WHY PROBLEM B IS NOT RE-RUN PER CANDIDATE

    The registered High-Damp Problem B test sets Low Damp 0, and
    shelfGainFor returns exactly 1.0 for ratio >= 1, so the low shelf is an
    identity and coeffLow cannot reach the output at all. Changing the low
    corner cannot change that test. Suite 2 proves this as a bit-identical
    render rather than asserting it, and the certified 10 kHz Problem B
    result is then reused.

    The low side gets its own curvature diagnostic instead - Low Damp 100 %,
    High Damp 0 - because moving a corner can relocate a curvature problem
    rather than remove it, and the second-order shelf study showed in-band
    RT60 spread is the mechanism to watch.
*/

namespace avtest
{
namespace
{

using namespace avtest::sweep;

/*
    The sweep, with 200 Hz as the shipping baseline.

    The first pass ran 80 to 200 Hz and found the best figure at 200 Hz, the
    TOP of the range, improving monotonically toward it. That is an
    edge-pinned optimum and cannot be reported as an optimum - it is the same
    fault the High Damp sweep caught, where three candidates' worst band sat
    on the last centre of the ladder. So the range is extended upward until
    the best figure turns around and the minimum is interior.
*/
constexpr double kCandidates[] { 80.0, 100.0, 125.0, 150.0, 175.0,
                                 200.0, 225.0, 250.0, 300.0, 400.0 };
constexpr int kNumCandidates = 10;

// Registered figures from the corner sweep, for the faithfulness check.
constexpr double kCornerSweep500Hz = 2.25;
constexpr double kCornerSweep1kHz  = 0.56;
constexpr double kCornerSweep2kHz  = 2.46;

} // namespace

void runDampingLowCornerSweepTests()
{
    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - the extracted harness is faithful");
    // -----------------------------------------------------------------------
    {
        info ("  The harness moved into DampingSweepHarness.h for this study, and");
        info ("  an extraction is only trustworthy if it still produces the numbers");
        info ("  the original produced. So the shipping baseline point is measured");
        info ("  again here and compared against what the corner sweep recorded for");
        info ("  the same configuration: High Damp Freq 10 kHz, Low Damp Freq");
        info ("  200 Hz, Mod Depth 30 %.");
        info ("");

        Setting setting;
        setting.lowDamp = kDefaultLowDamp;
        setting.highDamp = kDefaultHighDamp;
        setting.lowHz = kDefaultLowHz;
        setting.highHz = kCandidateHighHz;
        setting.modDepthControl = kDefaultModDepth;

        Tank tank (setting);
        const auto ensemble = renderEnsemble (tank);

        const double probes[] { 500.0, 1000.0, 2000.0 };
        const double expected[] { kCornerSweep500Hz, kCornerSweep1kHz, kCornerSweep2kHz };

        double worstDrift = 0.0;

        info ("    band      here      corner sweep   drift");

        for (int b = 0; b < 3; ++b)
        {
            const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
            const auto result = measureBand (ensemble, band);

            const double deviation = std::abs (result.fitted - kDecayTarget)
                                     / kDecayTarget * 100.0;

            const double drift = std::abs (deviation - expected[b]);
            worstDrift = std::max (worstDrift, drift);

            info ("    " + pad (number (probes[b], 0) + " Hz", 10)
                  + pad (number (deviation, 2) + " %", 10)
                  + pad (number (expected[b], 2) + " %", 15)
                  + number (drift, 3) + " pp");
        }

        checkAtMost (worstDrift, 0.10,
                     "the extracted harness reproduces the corner-sweep baseline");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - Low Damp Freq cannot touch the Problem B test");
    // -----------------------------------------------------------------------
    {
        info ("  The registered Problem B test uses Low Damp 0. shelfGainFor returns");
        info ("  exactly 1.0 for ratio >= 1, so gainLow is exactly 1, the low shelf");
        info ("  is an identity and coeffLow never reaches the output. Changing the");
        info ("  low corner therefore cannot change that test at all - not to within");
        info ("  a tolerance, but exactly.");
        info ("");
        info ("  Proved as a render comparison rather than asserted, so the 10 kHz");
        info ("  Problem B result can be reused for every candidate below instead of");
        info ("  re-running the suite six times.");
        info ("");

        Setting a;
        a.lowDamp = 0.0;
        a.highDamp = 1.0;
        a.lowHz = 80.0;
        a.highHz = kCandidateHighHz;

        Setting b = a;
        b.lowHz = 200.0;

        Tank tankA (a), tankB (b);

        const auto drive = makeExcitation (1);
        const int length = static_cast<int> (kSampleRate * 2.0);

        const auto outA = tankA.render (drive, length);
        const auto outB = tankB.render (drive, length);

        double worst = 0.0;

        for (size_t n = 0; n < outA.size() && n < outB.size(); ++n)
            worst = std::max (worst, std::abs (outA[n] - outB[n]));

        // And the shelf gain itself, which is the reason.
        const double gainAtZero = damping::shelfGainFor (0.05, kDecayTarget,
                                                         mapping::lowDampRatioFor (0.0));

        info ("    samples compared                    " + std::to_string (outA.size()));
        info ("    worst sample difference             " + number (worst, 18));
        info ("    gainLow at Low Damp 0               " + number (gainAtZero, 18));
        info ("");
        info ("  Reused for every candidate: the certified 10 kHz Problem B result,");
        info ("  worst measured mismatch 5.51 % at 14254 Hz, interior to the valid");
        info ("  one-octave analysis range. The 5 % investigation gate is unchanged");
        info ("  and still not passed.");

        check (worst == 0.0,
               "the two low corners give a bit-identical render at Low Damp 0");
        check (gainAtZero == 1.0,
               "gainLow is exactly 1 at Low Damp 0, which is why");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - PROBLEM A across the sweep");
    // -----------------------------------------------------------------------

    std::array<double, kNumCandidates> worstMid {};
    std::array<std::array<double, 13>, kNumCandidates> grid {};

    const double probes[] { 60.0, 80.0, 100.0, 125.0, 160.0, 250.0, 500.0,
                            1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0 };

    {
        info ("  Decay " + number (kDecayTarget, 1) + " s, Low Damp "
              + number (kDefaultLowDamp * 100.0, 0) + " %, High Damp "
              + number (kDefaultHighDamp * 100.0, 0) + " %,");
        info ("  High Damp Freq " + number (kCandidateHighHz / 1000.0, 0)
              + " kHz (provisional study candidate), Mod Depth "
              + number (kDefaultModDepth * 100.0, 0) + " %,");
        info ("  Mod Rate " + number (kDefaultModRateHz, 2)
              + " Hz. Low Damp amount unchanged.");
        info ("");
        info ("    low                                      RT60 in seconds, base Decay "
              + number (kDecayTarget, 2) + " s");
        info ("    corner    60 Hz    80 Hz    100 Hz   125 Hz   160 Hz   250 Hz   500 Hz"
              "   1 kHz    2 kHz    4 kHz    8 kHz    12 kHz   16 kHz");

        for (int c = 0; c < kNumCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kCandidates[c];
            setting.highHz = kCandidateHighHz;
            setting.modDepthControl = kDefaultModDepth;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::string row = "    " + pad (number (kCandidates[c], 0) + " Hz", 10);

            for (int b = 0; b < 13; ++b)
            {
                const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                grid[static_cast<size_t> (c)][static_cast<size_t> (b)] = result.fitted;

                row += pad (number (result.fitted, 4), 9);

                if (probes[b] >= 500.0 && probes[b] <= 2000.0 && result.valid)
                    worstMid[static_cast<size_t> (c)] =
                        std::max (worstMid[static_cast<size_t> (c)],
                                  std::abs (result.fitted - kDecayTarget)
                                  / kDecayTarget * 100.0);
            }

            info (row);
        }

        info ("");
        info ("    low       deviation from base Decay, per cent");
        info ("    corner    250 Hz   500 Hz   1 kHz    2 kHz    worst 500 Hz - 2 kHz");

        for (int c = 0; c < kNumCandidates; ++c)
        {
            std::string row = "    " + pad (number (kCandidates[c], 0) + " Hz", 10);

            for (int b = 5; b < 9; ++b)
                row += pad (number (std::abs (grid[static_cast<size_t> (c)]
                                              [static_cast<size_t> (b)] - kDecayTarget)
                                    / kDecayTarget * 100.0, 2), 9);

            row += number (worstMid[static_cast<size_t> (c)], 2) + " %";

            if (worstMid[static_cast<size_t> (c)] <= kGateMidDeviation)
                row += "   MEETS TARGET";

            info (row);
        }

        info ("");
        info ("    target " + number (kGateMidDeviation, 1) + " %"
              "   (pre-registered, unchanged)");
        info ("    the 200 Hz row is the shipping baseline every other row is"
              " judged against");

        check (true, "Problem A measured across the low corner sweep");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - low-frequency damping retention");
    // -----------------------------------------------------------------------
    {
        info ("  A candidate that fixes 500 Hz by moving Low Damp so far down that");
        info ("  it stops doing useful work is not a solution. The reference for");
        info ("  what the control is worth is Low Damp 0 - the same tank with no low");
        info ("  damping at all - and the reference for what we currently ship is");
        info ("  the 200 Hz corner.");
        info ("");
        info ("  No retention threshold is proposed here. The tradeoff is exposed.");
        info ("");

        Setting none;
        none.lowDamp = 0.0;
        none.highDamp = kDefaultHighDamp;
        none.lowHz = kDefaultLowHz;
        none.highHz = kCandidateHighHz;
        none.modDepthControl = kDefaultModDepth;

        Tank bare (none);
        const auto bareEnsemble = renderEnsemble (bare);

        std::array<double, 6> noDamp {};

        info ("    reference RT60 with Low Damp 0 (so the low shelf is an identity)");
        std::string bareRow = "      ";

        for (int b = 0; b < 6; ++b)
        {
            const auto band = decay::makeBand (probes[b], kGateBandOctaves, kSampleRate);
            noDamp[static_cast<size_t> (b)] = measureBand (bareEnsemble, band).fitted;

            bareRow += pad (number (probes[b], 0) + " Hz " + number (noDamp[static_cast<size_t> (b)], 3), 15);
        }

        info (bareRow);
        info ("");
        info ("    low       fraction of the SHIPPING low-damping effect retained");
        info ("    corner    60 Hz    80 Hz    100 Hz   125 Hz   160 Hz   250 Hz   mean");

        // Found by value: 200 Hz is no longer the last entry in the sweep.
        int baseline = 0;

        for (int c = 0; c < kNumCandidates; ++c)
            if (kCandidates[c] == kDefaultLowHz)
                baseline = c;

        for (int c = 0; c < kNumCandidates; ++c)
        {
            std::string row = "    " + pad (number (kCandidates[c], 0) + " Hz", 10);

            double sum = 0.0;
            int counted = 0;

            for (int b = 0; b < 6; ++b)
            {
                const double shippingEffect = noDamp[static_cast<size_t> (b)]
                                              - grid[static_cast<size_t> (baseline)]
                                                    [static_cast<size_t> (b)];

                const double candidateEffect = noDamp[static_cast<size_t> (b)]
                                               - grid[static_cast<size_t> (c)]
                                                     [static_cast<size_t> (b)];

                if (std::abs (shippingEffect) < 1.0e-6)
                {
                    row += pad ("n/a", 9);
                    continue;
                }

                const double retained = candidateEffect / shippingEffect * 100.0;

                row += pad (number (retained, 1) + " %", 9);

                sum += retained;
                ++counted;
            }

            row += (counted > 0) ? number (sum / counted, 1) + " %" : std::string ("n/a");

            info (row);
        }

        info ("");
        info ("  100 % means the candidate removes as much low-frequency decay as the");
        info ("  shipping 200 Hz corner does; 0 % means it has stopped working. A");
        info ("  value above 100 % at a band means the candidate damps that band");
        info ("  harder than the shipping default, which a lower corner can do at the");
        info ("  very bottom even while it releases 500 Hz.");

        check (true, "low-frequency retention measured against both references");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - low-side curvature diagnostic");
    // -----------------------------------------------------------------------
    {
        info ("  Mod Depth 0, Decay " + number (kDecayTarget, 1) + " s,"
              " Low Damp 100 %, High Damp 0 - the low-side mirror of the");
        info ("  registered High-Damp extreme. This asks whether lowering the");
        info ("  corner relocates a curvature problem into the bass rather than");
        info ("  removing it.");
        info ("");
        info ("  These numbers do NOT create a quality gate. They are compared");
        info ("  against the 200 Hz shipping corner and nothing else.");
        info ("");

        const double bands[] { 63.0, 80.0, 125.0, 180.0, 250.0, 355.0, 500.0 };

        info ("    low       T20/T30 mismatch, per cent");
        info ("    corner    63 Hz    80 Hz    125 Hz   180 Hz   250 Hz   355 Hz   500 Hz"
              "   WORST    at");

        std::array<double, kNumCandidates> worstLow {};
        std::array<double, kNumCandidates> worstLowHz {};
        std::array<std::array<decay::Result, 7>, kNumCandidates> fits {};

        for (int c = 0; c < kNumCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = 1.0;
            setting.highDamp = 0.0;
            setting.lowHz = kCandidates[c];
            setting.highHz = kCandidateHighHz;
            setting.modDepthControl = 0.0;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::string row = "    " + pad (number (kCandidates[c], 0) + " Hz", 10);

            for (int b = 0; b < 7; ++b)
            {
                const auto band = decay::makeBand (bands[b], kGateBandOctaves, kSampleRate);
                const auto result = measureBand (ensemble, band);

                fits[static_cast<size_t> (c)][static_cast<size_t> (b)] = result;

                const double m = mismatchPercent (result);

                row += pad (number (m, 2), 9);

                if (result.valid && m > worstLow[static_cast<size_t> (c)])
                {
                    worstLow[static_cast<size_t> (c)] = m;
                    worstLowHz[static_cast<size_t> (c)] = bands[b];
                }
            }

            row += pad (number (worstLow[static_cast<size_t> (c)], 2), 9)
                   + number (worstLowHz[static_cast<size_t> (c)], 0) + " Hz";

            info (row);
        }

        info ("");
        info ("    full fits at each candidate's worst low band");
        info ("    corner    band      RT60     EDT      T20      T30      R^2       mismatch");

        for (int c = 0; c < kNumCandidates; ++c)
        {
            int which = 0;

            for (int b = 0; b < 7; ++b)
                if (bands[b] == worstLowHz[static_cast<size_t> (c)])
                    which = b;

            const auto& r = fits[static_cast<size_t> (c)][static_cast<size_t> (which)];

            info ("    " + pad (number (kCandidates[c], 0) + " Hz", 10)
                  + pad (number (worstLowHz[static_cast<size_t> (c)], 0) + " Hz", 10)
                  + pad (number (r.fitted, 4), 9)
                  + pad (number (r.edt, 4), 9)
                  + pad (number (r.t20, 4), 9)
                  + pad (number (r.t30, 4), 9)
                  + pad (number (r.r2, 5), 10)
                  + number (mismatchPercent (r), 2) + " %");
        }

        info ("");
        info ("    change against the 200 Hz shipping corner");

        int baseline = 0;

        for (int c = 0; c < kNumCandidates; ++c)
            if (kCandidates[c] == kDefaultLowHz)
                baseline = c;

        for (int c = 0; c < kNumCandidates; ++c)
            if (c != baseline)
                info ("      " + pad (number (kCandidates[c], 0) + " Hz", 10)
                      + number (worstLow[static_cast<size_t> (c)]
                                - worstLow[static_cast<size_t> (baseline)], 2) + " pp");

        /*
            THE NOISE FLOOR OF THIS COMPARISON, measured.

            The analytic gradient is all but identical across candidates -
            slope 0.6398 to 0.6406 s/oct, in-band ratio 1.548 to 1.550 - so
            the low transition is TRANSLATING in log frequency and not
            changing shape. If in-band RT60 spread is what drives curvature,
            as the second-order shelf study established, the curvature has to
            be essentially the same for every candidate, and the roughly 1 pp
            scatter in the measured column is variance rather than a trend.

            That is a claim about noise, so the noise is measured instead of
            asserted: the same baseline is analysed on realisations 0-15 and
            16-31 separately. If the split-half difference is comparable to
            the spread between candidates, the between-candidate differences
            carry no information.
        */
        {
            Setting setting;
            setting.lowDamp = 1.0;
            setting.highDamp = 0.0;
            setting.lowHz = kDefaultLowHz;
            setting.highHz = kCandidateHighHz;
            setting.modDepthControl = 0.0;

            Tank tank (setting);
            const auto ensemble = renderEnsemble (tank);

            std::vector<std::vector<double>> firstHalf (ensemble.begin(),
                                                        ensemble.begin() + kEnsembleSize / 2);
            std::vector<std::vector<double>> secondHalf (ensemble.begin() + kEnsembleSize / 2,
                                                         ensemble.end());

            info ("");
            info ("    split-half control at the 200 Hz baseline, same tank, same");
            info ("    settings, 16 realisations each - this is the measurement noise");
            info ("      band      first 16   second 16   difference");

            double worstSplit = 0.0;

            for (int b = 0; b < 7; ++b)
            {
                const auto band = decay::makeBand (bands[b], kGateBandOctaves, kSampleRate);

                const double a = mismatchPercent (measureBand (firstHalf, band));
                const double d = mismatchPercent (measureBand (secondHalf, band));

                worstSplit = std::max (worstSplit, std::abs (a - d));

                info ("      " + pad (number (bands[b], 0) + " Hz", 10)
                      + pad (number (a, 2) + " %", 11)
                      + pad (number (d, 2) + " %", 12)
                      + number (std::abs (a - d), 2) + " pp");
            }

            double spread = 0.0;

            for (int c = 0; c < kNumCandidates; ++c)
                spread = std::max (spread, std::abs (worstLow[static_cast<size_t> (c)]
                                                     - worstLow[static_cast<size_t> (baseline)]));

            info ("");
            info ("    worst split-half difference        " + number (worstSplit, 2) + " pp");
            info ("    worst between-candidate difference " + number (spread, 2) + " pp");
            info ("");

            if (worstSplit >= 0.5 * spread)
                info ("  The noise is the same size as the effect, so the"
                      " between-candidate");
            else
                info ("  The noise is smaller than the effect, so the"
                      " between-candidate");

            info ("  differences in the table above "
                  + std::string (worstSplit >= 0.5 * spread ? "carry no information."
                                                            : "are real."));
        }

        check (true, "low-side curvature measured for every candidate");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - gradient diagnostic and Option A");
    // -----------------------------------------------------------------------
    {
        info ("  Analytic aggregate RT60(f) over the low transition, 30 Hz to 1 kHz,");
        info ("  2000 points. Slope in seconds per OCTAVE because the analysis bands");
        info ("  are constant-Q. The in-band ratio is the spread a single one-octave");
        info ("  band has to fit with one exponential - the mechanism the");
        info ("  second-order shelf study tied to tail curvature.");
        info ("");
        info ("    low       worst slope   at        worst in-band ratio   at        max|Hn|");
        info ("              at Low Damp 20 % (the default operating point)");

        double worstMagnitude = 0.0;

        for (int c = 0; c < kNumCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = kDefaultLowDamp;
            setting.highDamp = kDefaultHighDamp;
            setting.lowHz = kCandidates[c];
            setting.highHz = kCandidateHighHz;

            const AnalyticDamping analytic (setting);
            const auto g = measureGradient (analytic, 30.0, 1000.0);
            const double magnitude = analytic.worstNormalisedMagnitude();

            worstMagnitude = std::max (worstMagnitude, magnitude);

            info ("    " + pad (number (kCandidates[c], 0) + " Hz", 10)
                  + pad (number (g.worstSlope, 4) + " s/oct", 14)
                  + pad (number (g.worstSlopeHz, 0) + " Hz", 10)
                  + pad (number (g.worstBandRatio, 3) + " : 1", 22)
                  + pad (number (g.worstBandHz, 0) + " Hz", 10)
                  + number (magnitude, 12));
        }

        info ("");
        info ("              at Low Damp 100 % (the curvature diagnostic point)");

        for (int c = 0; c < kNumCandidates; ++c)
        {
            Setting setting;
            setting.lowDamp = 1.0;
            setting.highDamp = 0.0;
            setting.lowHz = kCandidates[c];
            setting.highHz = kCandidateHighHz;

            const AnalyticDamping analytic (setting);
            const auto g = measureGradient (analytic, 30.0, 1000.0);
            const double magnitude = analytic.worstNormalisedMagnitude();

            worstMagnitude = std::max (worstMagnitude, magnitude);

            info ("    " + pad (number (kCandidates[c], 0) + " Hz", 10)
                  + pad (number (g.worstSlope, 4) + " s/oct", 14)
                  + pad (number (g.worstSlopeHz, 0) + " Hz", 10)
                  + pad (number (g.worstBandRatio, 3) + " : 1", 22)
                  + pad (number (g.worstBandHz, 0) + " Hz", 10)
                  + number (magnitude, 12));
        }

        info ("");
        info ("    max|Hn| must stay at or below 1 for Option A to hold - damping");
        info ("    subtractive, decay never extended above base Decay.");

        checkAtMost (worstMagnitude, 1.0 + 1.0e-12,
                     "Option A semantics hold at every low corner candidate");
    }

    // -----------------------------------------------------------------------
    beginSuite ("PHASE 7 low corner - the Pareto frontier");
    // -----------------------------------------------------------------------
    {
        info ("  If no frequency reaches the target the instruction is to stop and");
        info ("  show the frontier rather than reach for Low Damp amount, so the");
        info ("  four axes are put side by side here whatever the outcome.");
        info ("");
        info ("    low corner   Problem A worst   target met   see the tables above for");
        info ("                 500 Hz - 2 kHz                 retention and curvature");

        bool anyMet = false;
        int best = 0;

        for (int c = 0; c < kNumCandidates; ++c)
        {
            const bool met = worstMid[static_cast<size_t> (c)] <= kGateMidDeviation;
            anyMet = anyMet || met;

            if (worstMid[static_cast<size_t> (c)] < worstMid[static_cast<size_t> (best)])
                best = c;

            info ("    " + pad (number (kCandidates[c], 0) + " Hz", 13)
                  + pad (number (worstMid[static_cast<size_t> (c)], 2) + " %", 18)
                  + (met ? "yes" : "no"));
        }

        info ("");
        info ("    best candidate " + number (kCandidates[static_cast<size_t> (best)], 0)
              + " Hz at " + number (worstMid[static_cast<size_t> (best)], 2) + " %"
              + (anyMet ? "   target reached"
                        : "   TARGET NOT REACHED BY ANY FREQUENCY"));
        info ("");

        if (! anyMet)
        {
            info ("  No frequency reaches the target, so this study stops here. Low");
            info ("  Damp AMOUNT is not touched: amount and frequency in one study");
            info ("  would make the result unattributable.");
        }

        check (true, "the frontier is recorded whether or not the target is reached");
    }
}

} // namespace avtest
