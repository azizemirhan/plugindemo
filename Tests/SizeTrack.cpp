#include "SizeTrack.h"

#include "ModalMap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>

namespace avtest
{

namespace
{
    constexpr double notMeasured() { return std::numeric_limits<double>::quiet_NaN(); }

    [[nodiscard]] double medianOf (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;

        const auto middle = v.begin() + static_cast<long> (v.size() / 2);
        std::nth_element (v.begin(), middle, v.end());

        if (v.size() % 2 == 1)
            return *middle;

        const double upper = *middle;
        const auto below = std::max_element (v.begin(), middle);

        return 0.5 * (upper + *below);
    }
}

const char* describe (SizePointStatus s) noexcept
{
    switch (s)
    {
        case SizePointStatus::matched:    return "matched";
        case SizePointStatus::ambiguous:  return "ambiguous";
        case SizePointStatus::unmatched:  return "unmatched";
        case SizePointStatus::outOfBand:  return "out of band";
    }

    return "unknown";
}

// ===========================================================================

double localPeakSpacingHz (const SizeFrame& frame, double centreHz,
                           double windowHz, double floorHz)
{
    const double half = 0.5 * std::max (1.0, windowHz);

    std::vector<double> gaps;

    for (size_t i = 1; i < frame.peaks.size(); ++i)
    {
        const double a = frame.peaks[i - 1].frequencyHz;
        const double b = frame.peaks[i].frequencyHz;

        // A gap belongs to the neighbourhood when its midpoint does. Using the
        // endpoints instead would count the two gaps that straddle the edge
        // twice at one centre and not at all at the next.
        const double midpoint = 0.5 * (a + b);

        if (std::abs (midpoint - centreHz) <= half)
            gaps.push_back (b - a);
    }

    double spacing = 0.0;

    if (gaps.size() >= 3)
        spacing = medianOf (std::move (gaps));

    // Too sparse for a median of its own. The delay set's average density is
    // the honest fallback, and it is stated as such rather than blended in.
    if (! (spacing > 0.0))
        spacing = frame.meanModalSpacingHz;

    return std::max (spacing, std::max (1.0e-9, floorHz));
}

// ===========================================================================

namespace
{
    /** Mutable per-track state while a chain is walked. */
    struct ChainState
    {
        size_t trajectory = 0;
        double lastCoordinate = 0.0;   /**< q of the last accepted point. */
        double lastProminence = 0.0;
        bool   alive = false;
    };
}

std::vector<SizeTrajectory> trackSize (const std::vector<SizeFrame>& frames,
                                       size_t anchorFrame,
                                       const std::vector<int>& anchorPeakIndices,
                                       const SizeTrackOptions& options)
{
    std::vector<SizeTrajectory> out;

    if (frames.empty() || anchorFrame >= frames.size())
        return out;

    const SizeFrame& anchor = frames[anchorFrame];

    // --- seed ---------------------------------------------------------------

    for (size_t k = 0; k < anchorPeakIndices.size(); ++k)
    {
        const int index = anchorPeakIndices[k];

        if (index < 0 || static_cast<size_t> (index) >= anchor.peaks.size())
            continue;

        const auto& peak = anchor.peaks[static_cast<size_t> (index)];

        SizeTrajectory t;
        t.id = static_cast<int> (out.size());
        t.anchorFrame = static_cast<int> (anchorFrame);
        t.anchorHz = peak.frequencyHz;
        t.points.resize (frames.size());

        for (size_t f = 0; f < frames.size(); ++f)
        {
            auto& p = t.points[f];

            p.frameIndex = static_cast<int> (f);
            p.sizeControl = frames[f].sizeControl;
            p.sizeScale = frames[f].sizeScale;
            p.status = SizePointStatus::unmatched;
            p.peakIndex = -1;
            p.frequencyHz = notMeasured();
            p.predictedHz = notMeasured();
            p.residualHz = notMeasured();
            p.logResidual = notMeasured();
            p.localSpacingHz = notMeasured();
            p.residualSpacings = notMeasured();
            p.modalCoordinate = notMeasured();
            p.prominenceDb = notMeasured();
            p.bandwidthHz = notMeasured();
            p.qFactor = notMeasured();
            p.regret = notMeasured();
        }

        auto& seed = t.points[anchorFrame];

        seed.status = SizePointStatus::matched;
        seed.peakIndex = index;
        seed.frequencyHz = peak.frequencyHz;
        seed.predictedHz = peak.frequencyHz;
        seed.residualHz = 0.0;
        seed.logResidual = 0.0;
        seed.localSpacingHz = localPeakSpacingHz (anchor, peak.frequencyHz,
                                                  options.spacingWindowHz,
                                                  options.spacingFloorHz);
        seed.residualSpacings = 0.0;
        seed.modalCoordinate = peak.frequencyHz * anchor.sizeScale;
        seed.prominenceDb = peak.prominenceDb;
        seed.bandwidthHz = peak.bandwidthHz;
        seed.qFactor = peak.qFactor;
        seed.regret = std::numeric_limits<double>::infinity();

        out.push_back (std::move (t));
    }

    if (out.empty())
        return out;

    // --- walk ---------------------------------------------------------------

    // Two chains, each seeded from the anchor and neither aware of the other.
    // A min-to-max walk would let one bad step at the small-Size end travel the
    // whole axis; this bounds the damage to one side of the anchor.
    const auto walk = [&] (int step)
    {
        std::vector<ChainState> state (out.size());

        for (size_t i = 0; i < out.size(); ++i)
        {
            state[i].trajectory = i;
            state[i].lastCoordinate = out[i].points[anchorFrame].modalCoordinate;
            state[i].lastProminence = out[i].points[anchorFrame].prominenceDb;
            state[i].alive = true;
        }

        for (int f = static_cast<int> (anchorFrame) + step;
             f >= 0 && f < static_cast<int> (frames.size());
             f += step)
        {
            const SizeFrame& frame = frames[static_cast<size_t> (f)];

            // --- predictions, and the boundary ------------------------------

            struct Row
            {
                size_t trajectory = 0;
                double predicted = 0.0;
                double spacing = 0.0;
                double gate = 0.0;
                double lastProminence = 0.0;
            };

            std::vector<Row> rows;

            for (auto& s : state)
            {
                if (! s.alive)
                    continue;

                const double predicted = s.lastCoordinate / frame.sizeScale;

                if (predicted < options.bandLowHz || predicted > options.bandHighHz)
                {
                    // The scale is monotone along the walk, so once a mode's
                    // prediction leaves the band it stays out. Reported as a
                    // boundary exit for every remaining point in this
                    // direction: absence there is the detector's edge, not a
                    // failure to find the mode.
                    for (int g = f; g >= 0 && g < static_cast<int> (frames.size()); g += step)
                    {
                        auto& p = out[s.trajectory].points[static_cast<size_t> (g)];

                        p.status = SizePointStatus::outOfBand;
                        p.predictedHz = s.lastCoordinate / frames[static_cast<size_t> (g)].sizeScale;
                    }

                    s.alive = false;
                    continue;
                }

                Row r;

                r.trajectory = s.trajectory;
                r.predicted = predicted;
                r.spacing = localPeakSpacingHz (frame, predicted, options.spacingWindowHz,
                                                options.spacingFloorHz);
                r.gate = options.maxResidualSpacings * r.spacing;
                r.lastProminence = s.lastProminence;

                rows.push_back (r);
            }

            if (rows.empty())
                continue;

            // --- candidate columns ------------------------------------------

            // Only peaks inside somebody's gate can appear in the assignment.
            // The solve is then over a few hundred columns rather than a few
            // thousand, and the optimum is identical: an excluded column was
            // infeasible for every row.
            std::vector<int> columns;

            for (const auto& r : rows)
            {
                const auto begin = std::lower_bound (frame.peaks.begin(), frame.peaks.end(),
                                                     r.predicted - r.gate,
                                                     [] (const SizePeak& p, double v)
                                                     { return p.frequencyHz < v; });

                for (auto it = begin; it != frame.peaks.end(); ++it)
                {
                    if (it->frequencyHz > r.predicted + r.gate)
                        break;

                    columns.push_back (static_cast<int> (std::distance (frame.peaks.begin(), it)));
                }
            }

            std::sort (columns.begin(), columns.end());
            columns.erase (std::unique (columns.begin(), columns.end()), columns.end());

            const size_t realColumns = columns.size();
            const size_t totalColumns = realColumns + rows.size();

            std::vector<std::vector<double>> matrix (rows.size(),
                                                     std::vector<double> (totalColumns, 0.0));

            for (size_t i = 0; i < rows.size(); ++i)
            {
                const auto& r = rows[i];

                for (size_t j = 0; j < realColumns; ++j)
                {
                    const auto& peak = frame.peaks[static_cast<size_t> (columns[j])];
                    const double residual = std::abs (peak.frequencyHz - r.predicted);

                    if (residual > r.gate)
                    {
                        matrix[i][j] = infeasibleCost;
                        continue;
                    }

                    const double prominence =
                        std::min (options.maxProminencePenalty,
                                  options.prominenceWeight
                                      * std::abs (peak.prominenceDb - r.lastProminence));

                    matrix[i][j] = residual / r.spacing + prominence;
                }

                for (size_t j = realColumns; j < totalColumns; ++j)
                    matrix[i][j] = options.rejectionCost;
            }

            const auto solved = solveMinCostAssignment (matrix);

            for (size_t i = 0; i < rows.size(); ++i)
            {
                const auto& r = rows[i];

                auto& point = out[r.trajectory].points[static_cast<size_t> (f)];

                point.predictedHz = r.predicted;
                point.localSpacingHz = r.spacing;

                const int j = solved.assignment[i];

                if (j < 0 || static_cast<size_t> (j) >= realColumns)
                {
                    // The solver preferred its dummy. A gap, and the chain
                    // stays alive: the prediction for the next Size comes from
                    // the last accepted measurement, so a hole costs coverage
                    // and nothing else.
                    point.status = SizePointStatus::unmatched;
                    continue;
                }

                double regret = std::numeric_limits<double>::infinity();

                for (size_t k = 0; k < totalColumns; ++k)
                {
                    if (k == static_cast<size_t> (j))
                        continue;

                    const double reduced = matrix[i][k] - solved.u[i] - solved.v[k];

                    regret = std::min (regret, std::max (0.0, reduced));
                }

                point.regret = std::isfinite (regret) ? regret : infeasibleCost;

                const auto& peak = frame.peaks[static_cast<size_t> (columns[static_cast<size_t> (j)])];

                point.peakIndex = columns[static_cast<size_t> (j)];
                point.frequencyHz = peak.frequencyHz;
                point.residualHz = peak.frequencyHz - r.predicted;
                point.logResidual = std::log (peak.frequencyHz / r.predicted);
                point.residualSpacings = std::abs (point.residualHz) / r.spacing;
                point.modalCoordinate = peak.frequencyHz * frame.sizeScale;
                point.prominenceDb = peak.prominenceDb;
                point.bandwidthHz = peak.bandwidthHz;
                point.qFactor = peak.qFactor;

                if (point.regret < options.ambiguityMargin)
                {
                    // The optimum was not clearly better than the alternative.
                    // The measurement is kept and reported, the identity is
                    // not: the trajectory does not advance on it.
                    point.status = SizePointStatus::ambiguous;
                    continue;
                }

                point.status = SizePointStatus::matched;
            }

            // Advance only on accepted identities. Ambiguous and unmatched
            // points leave the predictor where it was, which is what stops one
            // uncertain step from dragging the rest of the chain with it.
            for (auto& s : state)
            {
                if (! s.alive)
                    continue;

                const auto& point = out[s.trajectory].points[static_cast<size_t> (f)];

                if (point.status == SizePointStatus::matched)
                {
                    s.lastCoordinate = point.modalCoordinate;
                    s.lastProminence = point.prominenceDb;
                }
            }
        }
    };

    walk (+1);
    walk (-1);

    for (auto& t : out)
    {
        for (const auto& p : t.points)
        {
            switch (p.status)
            {
                case SizePointStatus::matched:   ++t.matchedPoints;   break;
                case SizePointStatus::ambiguous: ++t.ambiguousPoints; break;
                case SizePointStatus::unmatched: ++t.unmatchedPoints; break;
                case SizePointStatus::outOfBand: ++t.outOfBandPoints; break;
            }
        }
    }

    return out;
}

// ===========================================================================
//  Peak evidence
// ===========================================================================

const char* describe (PeakEvidence e) noexcept
{
    switch (e)
    {
        case PeakEvidence::confirmed: return "confirmed";
        case PeakEvidence::weak:      return "weak-evidence";
        case PeakEvidence::suspect:   return "suspect";
        case PeakEvidence::absent:    return "absent";
    }

    return "unknown";
}

SizeEvidence evidenceFor (const SizeTrajectory& t, const SizeEvidenceOptions& options)
{
    SizeEvidence e;

    e.points.assign (t.points.size(), PeakEvidence::absent);

    // The trajectory's own median, so "reshaped" is measured against what this
    // mode looked like rather than against a number from somewhere else.
    {
        std::vector<double> widths, prominences;

        for (const auto& p : t.points)
        {
            if (p.status != SizePointStatus::matched)
                continue;

            if (std::isfinite (p.bandwidthHz) && p.bandwidthHz > 0.0)
                widths.push_back (p.bandwidthHz);

            if (std::isfinite (p.prominenceDb))
                prominences.push_back (p.prominenceDb);
        }

        e.medianBandwidthHz = medianOf (widths);
        e.medianProminenceDb = medianOf (prominences);
    }

    for (size_t i = 0; i < t.points.size(); ++i)
    {
        const auto& p = t.points[i];

        if (p.status != SizePointStatus::matched)
            continue;   // ambiguous, gap and boundary exit are not peak evidence

        PeakEvidence verdict = PeakEvidence::confirmed;

        if (std::isfinite (p.residualSpacings)
            && std::abs (p.residualSpacings) > options.suspectResidualSpacings)
            verdict = PeakEvidence::suspect;

        if (verdict == PeakEvidence::confirmed
            && std::isfinite (p.prominenceDb)
            && p.prominenceDb < options.weakProminenceDb)
            verdict = PeakEvidence::weak;

        if (verdict == PeakEvidence::confirmed
            && e.medianBandwidthHz > 0.0
            && std::isfinite (p.bandwidthHz) && p.bandwidthHz > 0.0)
        {
            const double ratio = p.bandwidthHz / e.medianBandwidthHz;

            if (ratio > options.signatureRatioLimit || ratio < 1.0 / options.signatureRatioLimit)
                verdict = PeakEvidence::weak;
        }

        e.points[i] = verdict;

        switch (verdict)
        {
            case PeakEvidence::confirmed: ++e.confirmedPoints; break;
            case PeakEvidence::weak:      ++e.weakPoints;      break;
            case PeakEvidence::suspect:   ++e.suspectPoints;   break;
            case PeakEvidence::absent:                         break;
        }
    }

    if (e.suspectPoints > 0)      e.worst = PeakEvidence::suspect;
    else if (e.weakPoints > 0)    e.worst = PeakEvidence::weak;
    else if (e.confirmedPoints)   e.worst = PeakEvidence::confirmed;

    return e;
}

// ===========================================================================
//  The Size axis
// ===========================================================================

SizeAxisTracking trackSizeAxis (const std::vector<SizeFrame>& frames,
                                const SizeFrame* referenceFrame,
                                const std::vector<int>& anchorPeakIndices,
                                double defaultSizeControl,
                                const SizeTrackOptions& options)
{
    SizeAxisTracking out;

    if (frames.size() < 2 || anchorPeakIndices.empty())
        return out;

    // --- where the walk starts ----------------------------------------------

    // With a reference frame the anchor is the reference itself, carried into
    // the walk at its own scale. The sweep defines its mode identities at the
    // shipping default, which is not one of the Size points, and projecting
    // those identities onto the nearest point first would spend a tracking step
    // before the tracking has started.
    std::vector<SizeFrame> combined;
    size_t anchorIndex = 0;
    int insertedAt = -1;

    if (referenceFrame != nullptr)
    {
        size_t position = frames.size();

        for (size_t f = 0; f < frames.size(); ++f)
        {
            if (frames[f].sizeScale > referenceFrame->sizeScale)
            {
                position = f;
                break;
            }
        }

        combined.reserve (frames.size() + 1);

        for (size_t f = 0; f < position; ++f)
            combined.push_back (frames[f]);

        combined.push_back (*referenceFrame);

        for (size_t f = position; f < frames.size(); ++f)
            combined.push_back (frames[f]);

        anchorIndex = position;
        insertedAt = static_cast<int> (position);

        // The frame the two chains part at, in the caller's numbering: the
        // sweep point nearest the reference in scale.
        size_t nearest = 0;

        for (size_t f = 1; f < frames.size(); ++f)
            if (std::abs (std::log (frames[f].sizeScale / referenceFrame->sizeScale))
                < std::abs (std::log (frames[nearest].sizeScale / referenceFrame->sizeScale)))
                nearest = f;

        out.anchorFrame = nearest;
    }
    else
    {
        combined = frames;

        size_t nearest = 0;

        for (size_t f = 1; f < frames.size(); ++f)
            if (std::abs (frames[f].sizeControl - defaultSizeControl)
                < std::abs (frames[nearest].sizeControl - defaultSizeControl))
                nearest = f;

        anchorIndex = nearest;
        out.anchorFrame = nearest;
    }

    // --- the tracking itself -------------------------------------------------

    auto tracked = trackSize (combined, anchorIndex, anchorPeakIndices, options);

    // Drop the inserted reference column and renumber, so what comes back is
    // indexed by sweep frame and nothing downstream has to know about it.
    const auto strip = [&] (SizeTrajectory& t)
    {
        if (insertedAt < 0)
            return;

        t.points.erase (t.points.begin() + insertedAt);

        t.matchedPoints = t.ambiguousPoints = t.unmatchedPoints = t.outOfBandPoints = 0;

        for (size_t f = 0; f < t.points.size(); ++f)
        {
            t.points[f].frameIndex = static_cast<int> (f);

            switch (t.points[f].status)
            {
                case SizePointStatus::matched:   ++t.matchedPoints;   break;
                case SizePointStatus::ambiguous: ++t.ambiguousPoints; break;
                case SizePointStatus::unmatched: ++t.unmatchedPoints; break;
                case SizePointStatus::outOfBand: ++t.outOfBandPoints; break;
            }
        }
    };

    for (auto& t : tracked)
        strip (t);

    out.trajectories = std::move (tracked);

    // --- path independence ---------------------------------------------------

    // Each trajectory is re-tracked from its own extremes, from the lowest Size
    // it reached walking up and from the highest walking down. If the identity
    // is real the traversal direction cannot change it.
    const size_t count = out.trajectories.size();

    out.forwardBackward.assign (count, {});
    out.anchorUp.assign (count, {});
    out.anchorDown.assign (count, {});

    const auto compare = [] (const SizeTrajectory& a, const SizeTrajectory& b,
                             int fromFrame, int toFrame)
    {
        SizeAgreement result;

        for (size_t f = 0; f < a.points.size() && f < b.points.size(); ++f)
        {
            const int frame = static_cast<int> (f);

            if (frame < fromFrame || frame > toFrame)
                continue;

            if (a.points[f].status != SizePointStatus::matched
                || b.points[f].status != SizePointStatus::matched)
                continue;

            ++result.comparable;

            if (a.points[f].peakIndex == b.points[f].peakIndex)
                ++result.same;
        }

        return result;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const auto& t = out.trajectories[i];

        int lowFrame = -1, highFrame = -1;

        for (size_t f = 0; f < t.points.size(); ++f)
        {
            if (t.points[f].status != SizePointStatus::matched)
                continue;

            if (lowFrame < 0)
                lowFrame = static_cast<int> (f);

            highFrame = static_cast<int> (f);
        }

        if (lowFrame < 0 || highFrame < 0 || lowFrame == highFrame)
            continue;

        const auto low = trackSize (frames, static_cast<size_t> (lowFrame),
                                    { t.points[static_cast<size_t> (lowFrame)].peakIndex },
                                    options);

        const auto high = trackSize (frames, static_cast<size_t> (highFrame),
                                     { t.points[static_cast<size_t> (highFrame)].peakIndex },
                                     options);

        if (low.empty() || high.empty())
            continue;

        out.forwardBackward[i] = compare (low.front(), high.front(), 0,
                                          static_cast<int> (frames.size()) - 1);

        out.anchorUp[i] = compare (t, low.front(), static_cast<int> (out.anchorFrame),
                                   static_cast<int> (frames.size()) - 1);

        out.anchorDown[i] = compare (t, high.front(), 0, static_cast<int> (out.anchorFrame));
    }

    return out;
}

// ===========================================================================

SizeForensics analyseTrajectory (const SizeTrajectory& t)
{
    SizeForensics f;

    std::vector<double> logScale, logFrequency, coordinate, residualHz, residualSpacings;
    std::vector<double> frequency;

    for (const auto& p : t.points)
    {
        switch (p.status)
        {
            case SizePointStatus::matched:   ++f.matchedPoints;   break;
            case SizePointStatus::ambiguous: ++f.ambiguousPoints; break;
            case SizePointStatus::unmatched: ++f.unmatchedPoints; break;
            case SizePointStatus::outOfBand: ++f.outOfBandPoints; break;
        }

        if (p.status != SizePointStatus::matched)
            continue;

        if (! (std::isfinite (p.frequencyHz) && p.frequencyHz > 0.0 && p.sizeScale > 0.0))
            continue;

        frequency.push_back (p.frequencyHz);
        logScale.push_back (std::log (p.sizeScale));
        logFrequency.push_back (std::log (p.frequencyHz));
        coordinate.push_back (p.frequencyHz * p.sizeScale);

        if (std::isfinite (p.residualHz))
            residualHz.push_back (p.residualHz);

        if (std::isfinite (p.residualSpacings))
        {
            residualSpacings.push_back (std::abs (p.residualSpacings));

            if (std::abs (p.residualSpacings) > 3.0)
                ++f.bigJumps;
        }
    }

    // --- reversals ----------------------------------------------------------

    // The points are already in frame order, which is ascending Size, so a
    // mode under any monotone size law walks one way and only one way.
    for (size_t i = 2; i < frequency.size(); ++i)
    {
        const double a = frequency[i - 1] - frequency[i - 2];
        const double b = frequency[i] - frequency[i - 1];

        if (a * b < 0.0)
            ++f.reversals;
    }

    // --- coefficient of variation of q --------------------------------------

    if (coordinate.size() >= 2)
    {
        const double n = static_cast<double> (coordinate.size());
        const double mean = std::accumulate (coordinate.begin(), coordinate.end(), 0.0) / n;

        double variance = 0.0;

        for (double v : coordinate)
            variance += (v - mean) * (v - mean);

        variance /= n;

        if (mean > 0.0)
        {
            f.coordinateCv = std::sqrt (variance) / mean;
            f.coordinateCvMeasured = true;
        }
    }

    // --- residual size ------------------------------------------------------

    if (! residualHz.empty())
    {
        double sum = 0.0;

        for (double v : residualHz)
            sum += v * v;

        f.residualRmsHz = std::sqrt (sum / static_cast<double> (residualHz.size()));
    }

    if (! residualSpacings.empty())
        f.meanAbsResidualSpacings =
            std::accumulate (residualSpacings.begin(), residualSpacings.end(), 0.0)
            / static_cast<double> (residualSpacings.size());

    // --- log f against log scale --------------------------------------------

    // The same fit and the same reliability rule the sweep already uses. The
    // R-squared threshold is not touched here.
    f.fitPoints = static_cast<int> (logScale.size());

    if (f.fitPoints >= 3)
    {
        const double n = static_cast<double> (f.fitPoints);
        const double mx = std::accumulate (logScale.begin(), logScale.end(), 0.0) / n;
        const double my = std::accumulate (logFrequency.begin(), logFrequency.end(), 0.0) / n;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;

        for (size_t i = 0; i < logScale.size(); ++i)
        {
            const double dx = logScale[i] - mx;
            const double dy = logFrequency[i] - my;

            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }

        if (sxx > 0.0)
        {
            f.exponent = sxy / sxx;

            double residual = 0.0;

            for (size_t i = 0; i < logScale.size(); ++i)
            {
                const double predicted = my + f.exponent * (logScale[i] - mx);
                const double e = logFrequency[i] - predicted;

                residual += e * e;
            }

            f.rSquared = syy > 0.0 ? 1.0 - residual / syy : 0.0;
            f.fitResidualRms = std::sqrt (residual / n);
            f.fitReliable = f.fitPoints >= 4 && f.rSquared > 0.90;
        }
    }

    return f;
}

// ===========================================================================
//  Labels
// ===========================================================================

const char* describe (SizeLabel l) noexcept
{
    switch (l)
    {
        case SizeLabel::inverseScaleConsistent:   return "inverse-scale-consistent";
        case SizeLabel::nonInverseScaleCandidate: return "non-inverse-scale-candidate";
        case SizeLabel::identityUncertain:        return "identity-uncertain";
    }

    return "unknown";
}

std::vector<SizeLabel> labelTrajectories (const std::vector<SizeForensics>& forensics,
                                          const SizeAxisTracking& axis)
{
    std::vector<SizeLabel> labels (forensics.size(), SizeLabel::identityUncertain);

    for (size_t i = 0; i < forensics.size(); ++i)
    {
        const auto& f = forensics[i];

        const bool enoughPoints = f.matchedPoints >= 4;
        const bool clean = f.reversals == 0 && f.bigJumps == 0;

        const bool haveAgreement = i < axis.forwardBackward.size()
                                && i < axis.anchorUp.size()
                                && i < axis.anchorDown.size();

        if (! haveAgreement)
            continue;

        const bool noDisagreement = axis.forwardBackward[i].consistent()
                                 && axis.anchorUp[i].consistent()
                                 && axis.anchorDown[i].consistent();

        // At least two frames somewhere were re-derived by an independently
        // anchored chain and came back the same. Without that the trajectory
        // has not been cross-checked at all, whatever its fit looks like.
        const bool corroborated = axis.forwardBackward[i].comparable
                                + axis.anchorUp[i].comparable
                                + axis.anchorDown[i].comparable >= 2;

        if (! (enoughPoints && clean && noDisagreement && corroborated))
            continue;

        labels[i] = f.coordinateCvMeasured && f.coordinateCv < inverseScaleCvLimit
                  ? SizeLabel::inverseScaleConsistent
                  : SizeLabel::nonInverseScaleCandidate;
    }

    return labels;
}

// ===========================================================================
//  Outputs
// ===========================================================================

namespace
{
    /** Writes a value, or nothing at all where it was not measured. */
    void field (std::ostream& out, double v)
    {
        if (std::isfinite (v))
            out << v;
    }
}

bool writeSizeSweepCsv (const std::string& directory, const SizeOutputs& o)
{
    if (! o.complete())
        return false;

    std::ofstream out (directory + "/size-sweep.csv", std::ios::trunc);

    if (! out.is_open())
        return false;

    out << std::setprecision (10);

    // The original columns are kept in place and in order, so anything reading
    // this file still reads it. The forensic columns are added after them.
    out << "mode_id,reference_hz";

    for (double s : o.sizeControls)
        out << ",f_at_size_" << s;

    out << ",matched_points,fit_exponent,fit_r_squared,fit_residual_rms,"
           "fit_points,fit_reliable,"
           "ambiguous_points,gap_points,boundary_exit_points,"
           "q_cv,residual_rms_hz,mean_residual_spacings,reversals,jumps_over_3_spacings,"
           "fb_comparable,fb_same,anchor_up_comparable,anchor_up_same,"
           "anchor_down_comparable,anchor_down_same,"
           "identity_label,evidence,confirmed_points,weak_points,suspect_points\n";

    const auto& tracks = *o.trajectories;
    const auto& forensics = *o.forensics;
    const auto& evidence = *o.evidence;
    const auto& labels = *o.labels;
    const auto& axis = *o.axis;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& f = forensics[i];

        out << i << ',';
        field (out, i < o.referenceHz.size() ? o.referenceHz[i] : tracks[i].anchorHz);

        for (size_t k = 0; k < o.sizeControls.size(); ++k)
        {
            out << ',';

            if (k < tracks[i].points.size()
                && tracks[i].points[k].status == SizePointStatus::matched)
                out << tracks[i].points[k].frequencyHz;
        }

        out << ',' << f.matchedPoints << ',' << f.exponent << ',' << f.rSquared
            << ',' << f.fitResidualRms << ',' << f.fitPoints << ','
            << (f.fitReliable ? 1 : 0)
            << ',' << f.ambiguousPoints << ',' << f.unmatchedPoints
            << ',' << f.outOfBandPoints << ',';

        field (out, f.coordinateCvMeasured ? f.coordinateCv
                                           : std::numeric_limits<double>::quiet_NaN());

        out << ',' << f.residualRmsHz << ',' << f.meanAbsResidualSpacings
            << ',' << f.reversals << ',' << f.bigJumps
            << ',' << axis.forwardBackward[i].comparable << ',' << axis.forwardBackward[i].same
            << ',' << axis.anchorUp[i].comparable << ',' << axis.anchorUp[i].same
            << ',' << axis.anchorDown[i].comparable << ',' << axis.anchorDown[i].same
            << ',' << describe (labels[i])
            << ',' << describe (evidence[i].worst)
            << ',' << evidence[i].confirmedPoints
            << ',' << evidence[i].weakPoints
            << ',' << evidence[i].suspectPoints << "\n";
    }

    return true;
}

bool writeSizeTrajectoryCsv (const std::string& directory, const SizeOutputs& o)
{
    if (! o.complete())
        return false;

    std::ofstream out (directory + "/size-trajectories.csv", std::ios::trunc);

    if (! out.is_open())
        return false;

    out << std::setprecision (10);
    out << "mode_id,reference_hz,anchor_frame,matched,ambiguous,gap,boundary_exit,"
           "reversals,jumps_over_3_spacings,q_cv,fit_exponent,fit_r_squared,"
           "fit_residual_rms,fit_points,fit_reliable,residual_rms_hz,"
           "mean_residual_spacings,fb_comparable,fb_same,anchor_up_comparable,"
           "anchor_up_same,anchor_down_comparable,anchor_down_same,identity_label,"
           "evidence,confirmed_points,weak_points,suspect_points,"
           "median_prominence_db,median_bandwidth_hz\n";

    const auto& tracks = *o.trajectories;
    const auto& forensics = *o.forensics;
    const auto& evidence = *o.evidence;
    const auto& labels = *o.labels;
    const auto& axis = *o.axis;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& f = forensics[i];

        out << i << ',';
        field (out, i < o.referenceHz.size() ? o.referenceHz[i] : tracks[i].anchorHz);

        out << ',' << axis.anchorFrame
            << ',' << f.matchedPoints << ',' << f.ambiguousPoints
            << ',' << f.unmatchedPoints << ',' << f.outOfBandPoints
            << ',' << f.reversals << ',' << f.bigJumps << ',';

        field (out, f.coordinateCvMeasured ? f.coordinateCv
                                           : std::numeric_limits<double>::quiet_NaN());

        out << ',' << f.exponent << ',' << f.rSquared << ',' << f.fitResidualRms
            << ',' << f.fitPoints << ',' << (f.fitReliable ? 1 : 0)
            << ',' << f.residualRmsHz << ',' << f.meanAbsResidualSpacings
            << ',' << axis.forwardBackward[i].comparable << ',' << axis.forwardBackward[i].same
            << ',' << axis.anchorUp[i].comparable << ',' << axis.anchorUp[i].same
            << ',' << axis.anchorDown[i].comparable << ',' << axis.anchorDown[i].same
            << ',' << describe (labels[i])
            << ',' << describe (evidence[i].worst)
            << ',' << evidence[i].confirmedPoints
            << ',' << evidence[i].weakPoints
            << ',' << evidence[i].suspectPoints
            << ',' << evidence[i].medianProminenceDb
            << ',' << evidence[i].medianBandwidthHz << "\n";
    }

    return true;
}

bool writeSizePointsCsv (const std::string& directory, const SizeOutputs& o)
{
    if (! o.complete())
        return false;

    std::ofstream out (directory + "/size-points.csv", std::ios::trunc);

    if (! out.is_open())
        return false;

    out << std::setprecision (10);
    out << "mode_id,reference_hz,frame,ui_size,size_scale,status,evidence,peak_index,"
           "frequency_hz,modal_coordinate_q,predicted_hz,residual_hz,log_residual,"
           "local_spacing_hz,residual_over_spacing,prominence_db,bandwidth_hz,q,regret\n";

    const auto& tracks = *o.trajectories;
    const auto& evidence = *o.evidence;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        for (size_t k = 0; k < tracks[i].points.size(); ++k)
        {
            const auto& p = tracks[i].points[k];

            out << i << ',';
            field (out, i < o.referenceHz.size() ? o.referenceHz[i] : tracks[i].anchorHz);

            out << ',' << p.frameIndex << ',' << p.sizeControl << ',' << p.sizeScale
                << ',' << describe (p.status) << ','
                << (k < evidence[i].points.size() ? describe (evidence[i].points[k]) : "absent")
                << ',' << p.peakIndex << ',';

            field (out, p.frequencyHz);      out << ',';
            field (out, p.modalCoordinate);  out << ',';
            field (out, p.predictedHz);      out << ',';
            field (out, p.residualHz);       out << ',';
            field (out, p.logResidual);      out << ',';
            field (out, p.localSpacingHz);   out << ',';
            field (out, p.residualSpacings); out << ',';
            field (out, p.prominenceDb);     out << ',';
            field (out, p.bandwidthHz);      out << ',';
            field (out, p.qFactor);          out << ',';
            field (out, p.regret);

            out << "\n";
        }
    }

    return true;
}


// ===========================================================================

void recomputeInverseScaleResiduals (SizeTrajectory& t, const std::vector<SizeFrame>& frames,
                                     const SizeTrackOptions& options)
{
    bool havePrevious = false;
    double previousCoordinate = 0.0;

    for (size_t f = 0; f < t.points.size() && f < frames.size(); ++f)
    {
        auto& p = t.points[f];

        if (p.status != SizePointStatus::matched)
            continue;

        if (! havePrevious)
        {
            p.predictedHz = p.frequencyHz;
            p.residualHz = 0.0;
            p.logResidual = 0.0;
            p.localSpacingHz = localPeakSpacingHz (frames[f], p.frequencyHz,
                                                   options.spacingWindowHz,
                                                   options.spacingFloorHz);
            p.residualSpacings = 0.0;

            havePrevious = true;
            previousCoordinate = p.modalCoordinate;
            continue;
        }

        const double predicted = previousCoordinate / frames[f].sizeScale;
        const double spacing = localPeakSpacingHz (frames[f], predicted,
                                                   options.spacingWindowHz,
                                                   options.spacingFloorHz);

        p.predictedHz = predicted;
        p.residualHz = p.frequencyHz - predicted;
        p.logResidual = predicted > 0.0 && p.frequencyHz > 0.0
                      ? std::log (p.frequencyHz / predicted) : notMeasured();
        p.localSpacingHz = spacing;
        p.residualSpacings = std::abs (p.residualHz) / spacing;

        previousCoordinate = p.modalCoordinate;
    }
}

} // namespace avtest
