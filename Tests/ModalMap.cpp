#include "ModalMap.h"

#include "DspAnalysis.h"
#include "FdnAnalysis.h"

#include "DSP/Denormal.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int N = fdn::order;
    using Morph = MorphingStereoFdn<N>;

    constexpr double quietDb = -300.0;

    [[nodiscard]] double toDb (double linear) noexcept
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear) : quietDb;
    }

    [[nodiscard]] double medianOf (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;

        const size_t mid = v.size() / 2;
        std::nth_element (v.begin(), v.begin() + static_cast<long> (mid), v.end());

        return v[mid];
    }
}

// ===========================================================================
//  Resolution
// ===========================================================================

AnalysisResolution resolutionFor (double sampleRate, size_t windowSamples,
                                  size_t fftSize, double sizeScale)
{
    AnalysisResolution r;

    r.sampleRate = sampleRate;
    r.windowSamples = windowSamples;
    r.fftSize = fftSize;

    r.windowSeconds = sampleRate > 0.0 ? static_cast<double> (windowSamples) / sampleRate : 0.0;
    r.binWidthHz = fftSize > 0 ? sampleRate / static_cast<double> (fftSize) : 0.0;

    // Two over the window length, not one. The Hann window's main lobe is four
    // bins wide, so two tones a single 1/T apart are not separated by it. Using
    // 1/T here would overstate what the measurement can do by a factor of two,
    // in the direction that invents modes.
    r.resolutionHz = r.windowSeconds > 0.0 ? 2.0 / r.windowSeconds : 0.0;

    // Average density from the delay set. An estimate of how many modes per Hz
    // the tank has, not the gap between any particular pair.
    double totalSeconds = 0.0;

    for (int i = 0; i < N; ++i)
        totalSeconds += fdn::delaysMs[i] * sizeScale * 0.001;

    r.meanModalSpacingHz = totalSeconds > 0.0 ? 1.0 / totalSeconds : 0.0;

    return r;
}

std::string AnalysisResolution::describe() const
{
    std::ostringstream out;

    out << "window " << windowSeconds << " s"
        << ", resolution " << resolutionHz << " Hz"
        << ", bin width " << binWidthHz << " Hz (display only)"
        << ", mean modal spacing estimate " << meanModalSpacingHz << " Hz";

    return out.str();
}

// ===========================================================================
//  Peak detection
// ===========================================================================

std::vector<ModalPeak> detectPeaks (const std::vector<double>& magnitude,
                                    const AnalysisResolution& res,
                                    double lowHz, double highHz,
                                    double localMedianWindowHz,
                                    double minProminenceDb)
{
    std::vector<ModalPeak> peaks;

    if (magnitude.size() < 3 || res.binWidthHz <= 0.0)
        return peaks;

    const auto binOf = [&res] (double hz) { return static_cast<size_t> (hz / res.binWidthHz); };

    const size_t first = std::max<size_t> (1, binOf (lowHz));
    const size_t last  = std::min (magnitude.size() - 2, binOf (highHz));

    // The neighbourhood is expressed in Hz and converted here, so zero-padding
    // changes how many bins it spans without changing what it measures.
    const auto halfWindow = std::max<size_t> (2, binOf (localMedianWindowHz * 0.5));

    // Two components closer than the resolution were not separated, so a peak
    // is only accepted if it is the local maximum over at least that span.
    const auto guard = std::max<size_t> (1, binOf (res.resolutionHz * 0.5));

    for (size_t i = first; i <= last; ++i)
    {
        bool isLocalMax = true;

        for (size_t k = 1; k <= guard && isLocalMax; ++k)
        {
            if (i < k || i + k >= magnitude.size())
                continue;

            if (magnitude[i] < magnitude[i - k] || magnitude[i] < magnitude[i + k])
                isLocalMax = false;
        }

        if (! isLocalMax)
            continue;

        const size_t lo = i > halfWindow ? i - halfWindow : 0;
        const size_t hi = std::min (magnitude.size() - 1, i + halfWindow);

        std::vector<double> neighbourhood;
        neighbourhood.reserve (hi - lo + 1);

        for (size_t k = lo; k <= hi; ++k)
            neighbourhood.push_back (magnitude[k]);

        const double median = medianOf (neighbourhood);
        const double prominence = toDb (magnitude[i]) - toDb (median);

        if (! (prominence >= minProminenceDb))
            continue;

        ModalPeak p;

        // Parabolic interpolation on the log magnitude. This refines the centre
        // within a bin; it does not separate two components inside one, and is
        // not claimed to.
        const double yl = toDb (magnitude[i - 1]);
        const double y0 = toDb (magnitude[i]);
        const double yr = toDb (magnitude[i + 1]);

        const double denom = yl - 2.0 * y0 + yr;
        const double delta = std::abs (denom) > 1.0e-12 ? 0.5 * (yl - yr) / denom : 0.0;

        p.frequencyHz = (static_cast<double> (i) + std::clamp (delta, -0.5, 0.5)) * res.binWidthHz;
        p.magnitudeDb = y0;
        p.prominenceDb = prominence;

        // -3 dB width, walked out from the peak in both directions.
        {
            const double target = y0 - 3.0;

            size_t l = i, r = i;

            while (l > lo && toDb (magnitude[l]) > target) --l;
            while (r < hi && toDb (magnitude[r]) > target) ++r;

            const bool bounded = toDb (magnitude[l]) <= target && toDb (magnitude[r]) <= target;

            p.bandwidthHz = static_cast<double> (r - l) * res.binWidthHz;
            p.widthResolved = bounded && p.bandwidthHz >= res.resolutionHz;
            p.q = p.bandwidthHz > 0.0 ? p.frequencyHz / p.bandwidthHz : 0.0;

            // A width at or below the resolution limit is the window's shape,
            // not the mode's.
            if (! p.widthResolved)
            {
                p.ambiguous = true;
                p.ambiguityReason = "width at or below the resolution limit";
            }
        }

        peaks.push_back (p);
    }

    // A peak within one resolution width of a taller neighbour is not a
    // separate measurement. Rather than dropping it silently, the pair is kept
    // and both are flagged, because "there might be two here" is information.
    for (size_t a = 0; a < peaks.size(); ++a)
    {
        for (size_t b = a + 1; b < peaks.size(); ++b)
        {
            if (std::abs (peaks[a].frequencyHz - peaks[b].frequencyHz) < res.resolutionHz)
            {
                peaks[a].ambiguous = peaks[b].ambiguous = true;
                peaks[a].ambiguityReason = peaks[b].ambiguityReason =
                    "closer to a neighbour than the window can resolve";
            }
        }
    }

    return peaks;
}

// ===========================================================================
//  Matching
// ===========================================================================


AssignmentResult solveMinCostAssignment (const std::vector<std::vector<double>>& cost)
{
    AssignmentResult result;

    const size_t rows = cost.size();

    if (rows == 0)
        return result;

    const size_t cols = cost[0].size();

    if (cols == 0)
    {
        result.assignment.assign (rows, -1);
        return result;
    }

    // The Jonker-Volgenant form of the Hungarian algorithm needs rows <= cols.
    // Transposing rather than padding with a huge block keeps the potentials
    // meaningful, which is what the ambiguity measure is read from.
    const bool transposed = rows > cols;

    const size_t n = transposed ? cols : rows;
    const size_t m = transposed ? rows : cols;

    const auto at = [&] (size_t i, size_t j)
    {
        return transposed ? cost[j][i] : cost[i][j];
    };

    constexpr double inf = std::numeric_limits<double>::infinity();

    // 1-based, with row 0 and column 0 as the algorithm's sentinels.
    std::vector<double> u (n + 1, 0.0), v (m + 1, 0.0);
    std::vector<size_t> p (m + 1, 0), way (m + 1, 0);

    for (size_t i = 1; i <= n; ++i)
    {
        p[0] = i;
        size_t j0 = 0;

        std::vector<double> minv (m + 1, inf);
        std::vector<char> used (m + 1, 0);

        do
        {
            used[j0] = 1;

            const size_t i0 = p[j0];
            double delta = inf;
            size_t j1 = 0;

            for (size_t j = 1; j <= m; ++j)
            {
                if (used[j])
                    continue;

                const double cur = at (i0 - 1, j - 1) - u[i0] - v[j];

                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }

            if (! std::isfinite (delta))
                break;

            for (size_t j = 0; j <= m; ++j)
            {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else         { minv[j] -= delta; }
            }

            j0 = j1;
        }
        while (p[j0] != 0);

        while (j0 != 0)
        {
            const size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        }
    }

    std::vector<int> rowToCol (n, -1);

    for (size_t j = 1; j <= m; ++j)
        if (p[j] != 0 && p[j] <= n)
            rowToCol[p[j] - 1] = static_cast<int> (j) - 1;

    result.assignment.assign (rows, -1);
    result.u.assign (rows, 0.0);
    result.v.assign (cols, 0.0);

    for (size_t i = 0; i < n; ++i)
    {
        if (rowToCol[i] < 0)
            continue;

        const size_t j = static_cast<size_t> (rowToCol[i]);

        if (transposed) result.assignment[j] = static_cast<int> (i);
        else            result.assignment[i] = static_cast<int> (j);

        result.totalCost += at (i, j);
    }

    for (size_t i = 0; i < n; ++i)
        (transposed ? result.v : result.u)[i] = u[i + 1];

    for (size_t j = 0; j < m; ++j)
        (transposed ? result.u : result.v)[j] = v[j + 1];

    return result;
}

MatchResult matchPeaks (const std::vector<ModalPeak>& from,
                        const std::vector<ModalPeak>& to,
                        const MatchOptions& options,
                        const std::vector<double>* predictedTo)
{
    MatchResult result;

    result.assignment.assign (from.size(), -1);
    result.ambiguous.assign (from.size(), false);
    result.regret.assign (from.size(), 0.0);

    if (from.empty() || to.empty())
        return result;

    const double spacing = std::max (1.0e-6, options.localSpacingHz);

    // Cost of pairing track i with peak j.
    //
    // Three terms, kept separate on purpose. Distance is measured from where
    // the track actually was, so a large jump is penalised however well it fits
    // a prediction. Continuity is measured from where the trajectory said it
    // would be, which is the term that survives a crossing: two peaks that meet
    // are equidistant from each other's current positions but not from each
    // other's predictions.
    const auto costOf = [&] (size_t i, size_t j) -> double
    {
        const double distance = std::abs (to[j].frequencyHz - from[i].frequencyHz) / spacing;

        if (distance > options.maxNormalisedDistance)
            return infeasibleCost;

        const double prominence = std::abs (to[j].prominenceDb - from[i].prominenceDb);

        double continuity = 0.0;

        if (predictedTo != nullptr && i < predictedTo->size())
            continuity = std::abs (to[j].frequencyHz - (*predictedTo)[i]) / spacing;

        return distance
             + options.prominenceWeight * prominence
             + options.continuityWeight * continuity;
    };

    // One dummy column per track, so "no match" competes with every real match
    // inside the same optimisation rather than being decided afterwards.
    const size_t realColumns = to.size();
    const size_t columns = realColumns + from.size();

    std::vector<std::vector<double>> matrix (from.size(), std::vector<double> (columns, 0.0));

    for (size_t i = 0; i < from.size(); ++i)
    {
        for (size_t j = 0; j < realColumns; ++j)
            matrix[i][j] = costOf (i, j);

        for (size_t j = realColumns; j < columns; ++j)
            matrix[i][j] = options.rejectionCost;
    }

    const auto solved = solveMinCostAssignment (matrix);

    result.totalCost = solved.totalCost;

    for (size_t i = 0; i < from.size(); ++i)
    {
        const int j = solved.assignment[i];

        if (j < 0 || static_cast<size_t> (j) >= realColumns)
            continue;   // unmatched, by the solver's own choice

        // How much would it cost, at least, to give this track a different
        // identity? The smallest reduced cost over the other columns. Reduced
        // costs are non-negative at the optimum, so this is a genuine lower
        // bound rather than a heuristic.
        double regret = std::numeric_limits<double>::infinity();

        for (size_t k = 0; k < columns; ++k)
        {
            if (k == static_cast<size_t> (j))
                continue;

            const double reduced = matrix[i][k] - solved.u[i] - solved.v[k];

            regret = std::min (regret, std::max (0.0, reduced));
        }

        result.regret[i] = std::isfinite (regret) ? regret : infeasibleCost;

        if (result.regret[i] < options.ambiguityMargin)
        {
            // The optimum was not clearly better than its alternative. Leaving
            // the track unassigned is the honest outcome; forcing it is how a
            // tracker reports a mode that moved when two modes merely swapped.
            result.ambiguous[i] = true;
            ++result.refused;
            continue;
        }

        result.assignment[i] = j;
        ++result.matched;
    }

    return result;
}

std::vector<TrackedMode> trackAcross (const std::vector<std::vector<ModalPeak>>& frames,
                                      const MatchOptions& options)
{
    std::vector<TrackedMode> modes;

    if (frames.empty())
        return modes;

    const size_t points = frames.size();

    // Seed one slot per peak in the first frame.
    for (size_t i = 0; i < frames[0].size(); ++i)
    {
        TrackedMode m;
        m.id = static_cast<int> (i);
        m.frequencyHz.assign (points, std::numeric_limits<double>::quiet_NaN());
        m.prominenceDb.assign (points, std::numeric_limits<double>::quiet_NaN());
        m.decaySeconds.assign (points, std::numeric_limits<double>::quiet_NaN());

        m.frequencyHz[0] = frames[0][i].frequencyHz;
        m.prominenceDb[0] = frames[0][i].prominenceDb;
        m.decaySeconds[0] = frames[0][i].decaySeconds;
        m.matchedPoints = 1;

        modes.push_back (m);
    }

    // Which peak in the previous frame each live slot is sitting on.
    std::vector<int> slotPeak (modes.size());
    std::iota (slotPeak.begin(), slotPeak.end(), 0);

    for (size_t f = 1; f < points; ++f)
    {
        std::vector<ModalPeak> previous;
        std::vector<double> predicted;
        std::vector<size_t> slotOf;

        for (size_t s = 0; s < modes.size(); ++s)
        {
            if (slotPeak[s] < 0)
                continue;

            previous.push_back (frames[f - 1][static_cast<size_t> (slotPeak[s])]);
            slotOf.push_back (s);

            // Linear extrapolation from the last two matched points, when there
            // are two. This is what lets crossing trajectories keep their
            // identities: the prediction, not the current position, is matched.
            double p = modes[s].frequencyHz[f - 1];

            if (f >= 2 && std::isfinite (modes[s].frequencyHz[f - 2])
                       && std::isfinite (modes[s].frequencyHz[f - 1]))
            {
                p = 2.0 * modes[s].frequencyHz[f - 1] - modes[s].frequencyHz[f - 2];
            }

            predicted.push_back (p);
        }

        const auto match = matchPeaks (previous, frames[f], options, &predicted);

        std::vector<int> nextSlotPeak (modes.size(), -1);

        for (size_t k = 0; k < previous.size(); ++k)
        {
            const size_t s = slotOf[k];

            if (match.ambiguous[k])
            {
                modes[s].ambiguousAt.push_back (static_cast<int> (f));
                continue;
            }

            const int j = match.assignment[k];

            if (j < 0)
                continue;

            modes[s].frequencyHz[f] = frames[f][static_cast<size_t> (j)].frequencyHz;
            modes[s].prominenceDb[f] = frames[f][static_cast<size_t> (j)].prominenceDb;
            modes[s].decaySeconds[f] = frames[f][static_cast<size_t> (j)].decaySeconds;
            ++modes[s].matchedPoints;

            nextSlotPeak[s] = j;
        }

        slotPeak = nextSlotPeak;
    }

    return modes;
}

// ===========================================================================
//  Classification
// ===========================================================================

const char* describe (ModeClass c) noexcept
{
    switch (c)
    {
        case ModeClass::sizeTrackingModulationMoving:
            return "A: size-tracking, modulation-moving";
        case ModeClass::sizeTrackingModulationResistant:
            return "B: size-tracking, modulation-resistant";
        case ModeClass::sizeInsensitiveFixedOrMixed:
            return "C: size-insensitive / investigate fixed or mixed structure";
        case ModeClass::ambiguousUnresolved:
            return "D: ambiguous / unresolved";
    }

    return "unknown";
}

ModeClass classifySweepMode (double sizeExponent, bool sizeFitReliable,
                             bool modulationEvidence, bool redistributes) noexcept
{
    if (! sizeFitReliable || ! modulationEvidence)
        return ModeClass::ambiguousUnresolved;

    if (sizeExponent < -0.75 && sizeExponent > -1.25)
        return redistributes ? ModeClass::sizeTrackingModulationMoving
                             : ModeClass::sizeTrackingModulationResistant;

    return ModeClass::sizeInsensitiveFixedOrMixed;
}

ModeClass classify (double sizeExponent, bool sizeExponentMeasured,
                    double modulationExcursionHz, double excursionNoiseFloorHz,
                    bool ambiguous)
{
    if (ambiguous || ! sizeExponentMeasured)
        return ModeClass::ambiguousUnresolved;

    // A loop mode's frequency goes as 1/scale, so the exponent of frequency
    // against scale is about -1. The band is wide because neighbouring modes
    // pull a measured centre around; outside it, the peak is not following the
    // delays.
    const bool tracksSize = sizeExponent < -0.75 && sizeExponent > -1.25;

    if (! tracksSize)
    {
        // Deliberately not "not a mode". Fixed delays, filters, the allpass
        // bank and interaction between modes can all hold a feature still.
        return ModeClass::sizeInsensitiveFixedOrMixed;
    }

    // Movement counts only when it clears the measurement's own noise floor by
    // a factor, not when it merely exceeds it.
    return modulationExcursionHz > 3.0 * excursionNoiseFloorHz
         ? ModeClass::sizeTrackingModulationMoving
         : ModeClass::sizeTrackingModulationResistant;
}

// ===========================================================================
//  Rendering
// ===========================================================================

ImpulseRender renderImpulse (const SweepPoint& point, double sampleRate, double seconds)
{
    ScopedFtz ftz;

    ImpulseRender out;

    out.sampleRate = sampleRate;
    out.sizeScale = mapping::sizeScaleFor (point.sizeControl);

    auto tank = std::make_unique<Morph>();

    tank->prepare (sampleRate);
    tank->setInputDiffusionEnabled (true);
    tank->setAllpassDepth (0.0, true);
    tank->setModulationDepth (0.0, true);
    tank->setCompensationEnabled (true, true);

    tank->setRt60Seconds (mapping::rt60For (point.decaySeconds), true);
    tank->setSize (out.sizeScale);

    architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                  mapping::modDepthFor (point.modDepthControl),
                                  mapping::modRateScaleFor (0.5),
                                  true);

    // Settle the Size morph on silence, then clear. Without this the response
    // holds a transition into the target size rather than the size itself, and
    // the modal frequencies measured would be of neither.
    for (int n = 0; n < static_cast<int> (sampleRate * 3.0); ++n)
    {
        double l = 0.0, r = 0.0;
        tank->processSample (l, r);
    }

    tank->reset();

    const auto length = static_cast<size_t> (sampleRate * seconds);

    out.left.resize (length);
    out.right.resize (length);

    const double trim = mapping::previewWetTrim (mapping::rt60For (point.decaySeconds),
                                                 out.sizeScale);

    for (size_t n = 0; n < length; ++n)
    {
        double l = n == 0 ? 1.0 : 0.0;
        double r = l;

        tank->processSample (l, r);

        out.left[n] = l * trim;
        out.right[n] = r * trim;
    }

    const auto decay = measureDecay (out.left, sampleRate);

    out.broadbandRt60 = decay.rt60FromT30;
    out.rt60Measured = decay.valid;

    return out;
}

} // namespace avtest
