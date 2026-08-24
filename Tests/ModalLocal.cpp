#include "ModalMap.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace avtest
{

WindowPolicy windowPolicyFor (double expectedRt60Seconds, double localSpacingHz)
{
    WindowPolicy p;

    // Separating a neighbour needs the analysis bandwidth below the local
    // spacing. Half of it, so a neighbour sits outside the main lobe rather
    // than on its edge.
    const double windowForSeparation = localSpacingHz > 0.0 ? 4.0 / localSpacingHz : 0.25;

    // Fitting a decay needs frames inside it. A T30 fit spans 30/60 of the
    // RT60, and eight frames across that span with a hop of window/8 means the
    // window can be at most a quarter of the fitted span.
    const double windowForFit = expectedRt60Seconds > 0.0 ? expectedRt60Seconds * 0.5 * 0.25
                                                          : 0.25;

    p.windowSeconds = windowForSeparation;
    p.hopSeconds = p.windowSeconds * 0.125;
    p.bandwidthHz = 2.0 / p.windowSeconds;

    if (windowForSeparation > windowForFit)
    {
        // The two requirements cannot both be met. Saying so is the point: a
        // window picked here would either merge the neighbour into the fit or
        // leave too few frames to fit at all, and both produce a number that
        // looks like a measurement.
        p.feasible = false;
        p.windowSeconds = windowForFit;
        p.hopSeconds = p.windowSeconds * 0.125;
        p.bandwidthHz = 2.0 / p.windowSeconds;

        p.note = "separation needs " + std::to_string (windowForSeparation)
               + " s but the fit allows at most " + std::to_string (windowForFit) + " s";
    }
    else
    {
        p.note = "separation and fit are both satisfiable";
    }

    p.expectedFrames = static_cast<int> (expectedRt60Seconds * 0.5 / p.hopSeconds);

    return p;
}

LocalResponse measureLocalResponse (const std::vector<double>& magnitude,
                                    const AnalysisResolution& res,
                                    double centreHz, double halfWidthHz)
{
    LocalResponse r;

    r.centreHz = centreHz;
    r.halfWidthHz = halfWidthHz;

    if (magnitude.empty() || res.binWidthHz <= 0.0 || halfWidthHz <= 0.0)
        return r;

    const auto binOf = [&] (double hz)
    {
        return static_cast<size_t> (std::max (0.0, hz) / res.binWidthHz);
    };

    const size_t lo = binOf (centreHz - halfWidthHz);
    const size_t hi = std::min (magnitude.size() - 1, binOf (centreHz + halfWidthHz));

    if (hi <= lo + 2)
        return r;

    double energy = 0.0, weighted = 0.0, peak = 0.0;

    for (size_t i = lo; i <= hi; ++i)
    {
        const double power = magnitude[i] * magnitude[i];
        const double hz = static_cast<double> (i) * res.binWidthHz;

        energy += power;
        weighted += power * hz;
        peak = std::max (peak, power);
    }

    if (energy <= 0.0)
        return r;

    r.integratedEnergyDb = 10.0 * std::log10 (energy);
    r.spectralCentroidHz = weighted / energy;

    double spread = 0.0;

    for (size_t i = lo; i <= hi; ++i)
    {
        const double power = magnitude[i] * magnitude[i];
        const double hz = static_cast<double> (i) * res.binWidthHz;
        const double d = hz - r.spectralCentroidHz;

        spread += power * d * d;
    }

    r.spectralSpreadHz = std::sqrt (spread / energy);

    // One line concentrates all its energy in a bin; a smeared cluster spreads
    // it. This is the number that separates "the mode moved" from "the mode was
    // redistributed", which a peak identity alone cannot say.
    r.energyConcentration = peak / energy;

    // Median of the neighbourhood, so prominence here means the same thing it
    // means in the peak detector.
    std::vector<double> window (magnitude.begin() + static_cast<long> (lo),
                                magnitude.begin() + static_cast<long> (hi) + 1);

    std::vector<double> sorted = window;
    std::nth_element (sorted.begin(), sorted.begin() + static_cast<long> (sorted.size() / 2),
                      sorted.end());

    const double median = sorted[sorted.size() / 2];

    r.maxProminenceDb = (median > 0.0 && peak > 0.0)
                      ? 10.0 * std::log10 (peak / (median * median)) : 0.0;

    // Local maxima above the median, at the resolution's guard width.
    const auto guard = std::max<size_t> (1, binOf (res.resolutionHz * 0.5));

    for (size_t i = lo + guard; i + guard <= hi; ++i)
    {
        if (magnitude[i] <= median)
            continue;

        bool isMax = true;

        for (size_t k = 1; k <= guard && isMax; ++k)
            if (magnitude[i] < magnitude[i - k] || magnitude[i] < magnitude[i + k])
                isMax = false;

        if (isMax)
            ++r.peakCount;
    }

    r.measured = true;

    return r;
}

} // namespace avtest
