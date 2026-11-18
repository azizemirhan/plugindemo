#pragma once

#include "Phase12Stage6.h"

#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 7b. The Plate (PV3) validation tool.

    RESEARCH ONLY. Source/ IS NOT CHANGED. The procedure and every limit are
    in Docs/Phase12-Stage7b-Plate-Validation-Setup.md and its addendum, and
    were fixed before this tool was written. Nothing here tunes Plate.

    Three suites run ONLY when named exactly on the command line, because
    their results depend on the machine being idle and must never be part of
    a regression:

        AuroraVerbTests.exe runPhase12Stage7PlateCpu      48 kHz closure, 31 pairs x 3 points
        AuroraVerbTests.exe runPhase12Stage7PlateStress   stress conditions and the scalar form
        AuroraVerbTests.exe runPhase12Stage7PlateRates    44.1 / 96 / 192 kHz, S0 - S7

    One fast suite (runPhase12Stage7PlateFastTests) is part of the
    regression. It checks the tool's own arithmetic and nothing about CPU.
*/
namespace avtest::stage7
{
    using stage3::Structure;

    inline constexpr int    kPlateSections   = 240;
    inline constexpr int    kPlateSubChains  = 16;
    inline constexpr int    kPipelineSamples = kPlateSubChains - 1;
    inline constexpr double kReferenceRate   = 48000.0;
    inline constexpr double kReferenceCoeff  = -0.7;

    /** THE FITTING LAW. One closed form, the same at every rate, with no
        free number in it: the coefficient at which one section's group
        delay at zero frequency, IN SECONDS, equals the 48 kHz section's.

            D(a) = (1 - a) / (1 + a)   samples at zero frequency
            a(fs) = -(R - 1) / (R + 1),   R = D(-0.7) * fs / 48000

        At 48 kHz it returns the reference coefficient. */
    [[nodiscard]] double plateCoefficient (double rate) noexcept;

    /** Group delay of the whole chain at a frequency, in seconds, WITHOUT
        the pipeline delay. */
    [[nodiscard]] double chainDelaySeconds (double coefficient, double hz, double rate) noexcept;

    /** Diagnostic only, never used to build anything: the coefficient that
        minimises the largest group-delay error against the 48 kHz chain
        over 100 Hz - 10 kHz. Shows how far the law is from the best any
        single coefficient could do. */
    [[nodiscard]] double bestFitCoefficient (double rate);

    /** Largest |chain(fs) - chain(48 kHz)| over 100 Hz - 10 kHz, seconds. */
    [[nodiscard]] double worstDelayError (double coefficient, double rate, double* atHz = nullptr,
                                          bool* withinLimit = nullptr);

    [[nodiscard]] Structure platePV3   (double rate);   // 16 sub-chains, two-wide in registers
    [[nodiscard]] Structure platePV1   (double rate);   // 8 sub-chains, scalar
    [[nodiscard]] Structure platePlain (double rate);   // the reference chain, not pipelined

    // --- the statistic ------------------------------------------------------------
    /** Distribution-free interval for the MEDIAN of n independent values:
        the narrowest symmetric pair of order statistics whose coverage is at
        least 95 %. Ranks are 1-based. lower == 0 when n is too small. */
    struct MedianRanks
    {
        int lower = 0, upper = 0;
        double twoSidedCoverage = 0.0;      // P (x[lower] <= median <= x[upper])
        double upperConfidence  = 0.0;      // P (median <= x[upper]), one-sided
    };

    [[nodiscard]] MedianRanks medianRanks (int n);

    struct PairStats
    {
        int pairs = 0;
        double median = 0.0, lower = 0.0, upper = 0.0;
        double ratioOfMinima = 0.0;
        double hallSpread = 0.0;            // slowest / fastest Hall block: how quiet the machine was
        MedianRanks ranks;
    };

    [[nodiscard]] PairStats pairStats (const std::vector<double>& hallNs, const std::vector<double>& plateNs);

    /** A stereo signal at `rate`, band-limited to 20 kHz and resampled to
        48 kHz, scaled so that an impulse response stays an impulse response. */
    [[nodiscard]] stage3::Stereo to48k (const stage3::Stereo& x, double rate);

    /** High priority, one core. Returns a description of what was achieved. */
    [[nodiscard]] std::string pinAndPrioritise();
}
