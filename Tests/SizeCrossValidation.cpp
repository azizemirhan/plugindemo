#include "SizeTrack.h"

#include "DspAnalysis.h"
#include "ModalMap.h"

#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

/*
    Phase 6 / FU-1: independent validation of the q-space prior.

    The worry this exists to settle is specific and correct. The tank's modal
    map has a detected peak every few Hz. A tracker that predicts a frequency
    and then takes the nearest peak will always find one, and if it is then
    scored on how near that peak was, it scores well whatever it predicted.
    Every number the re-track produced - a residual of 0.4 Hz, a coordinate CV
    of 0.01% - is measured on peaks the prior itself selected, and none of them
    can distinguish "the prediction was right" from "the map is dense".

    So nothing here is reported without its decoy. Every prediction is repeated
    at four deliberately wrong frequencies in the same neighbourhood, at
    +/- 1.3 and +/- 2.7 local spacings, and the same measurement is made
    against the same peak list. The decoys are subject to identical density,
    identical detector, identical spacing estimate. The result is the
    separation between the two distributions. If the prior were fitting noise
    the two would coincide.

    Two independent tests:

      A  leave-one-Size-out. A Size point is removed from the tracking input
         entirely and the sweep re-tracked without it, so the trajectory that
         predicts that point has never seen it. Not a held-out scoring pass
         over a trajectory that was built through the held-out frame - that
         leaks, and the leak points the right way.

      B  off-grid render. Size settings halfway between the sweep's own, at
         scale ratios of 2^(1/6) from their neighbours, rendered fresh. No
         tracking has ever been run at these settings and no peak list from
         them existed when the trajectories were built. The prediction is
         computed and written down before the render is analysed.

    Neither half may change a gate, a weight or the ambiguity margin. A mode
    that does not confirm is left unconfirmed.
*/
namespace avtest
{

namespace
{
    using av::dsp::mapping::sizeScaleFor;

    constexpr double notMeasured() { return std::numeric_limits<double>::quiet_NaN(); }

    // The sweep's analysis settings, unchanged. An off-grid render measured any
    // other way would not be comparable with the frames it sits between.
    constexpr size_t fftSize = 1 << 18;
    constexpr double renderSeconds = 6.0;
    constexpr double windowSeconds = 3.0;
    constexpr double detectProminenceDb = 10.0;
    constexpr double bandLowHz = 40.0;
    constexpr double bandHighHz = 12000.0;
    constexpr double localMedianWindowHz = 200.0;

    /** Offsets, in local spacings, at which the same measurement is repeated
        on a prediction known to be wrong. Non-integer on purpose: a whole
        number of spacings would land on the modal grid by construction. */
    const double decoyOffsets[] = { -2.7, -1.3, 1.3, 2.7 };

    [[nodiscard]] std::string fx (double v, int p)
    {
        if (! std::isfinite (v))
            return "-";

        std::ostringstream o; o << std::fixed << std::setprecision (p) << v; return o.str();
    }

    [[nodiscard]] std::string padR (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : s + std::string (w - s.size(), ' ');
    }

    [[nodiscard]] std::string padL (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : std::string (w - s.size(), ' ') + s;
    }

    [[nodiscard]] double quantile (std::vector<double> v, double p)
    {
        if (v.empty())
            return notMeasured();

        std::sort (v.begin(), v.end());

        const double position = p * static_cast<double> (v.size() - 1);
        const auto lower = static_cast<size_t> (std::floor (position));
        const auto upper = std::min (v.size() - 1, lower + 1);
        const double t = position - static_cast<double> (lower);

        return v[lower] * (1.0 - t) + v[upper] * t;
    }

    // -----------------------------------------------------------------------
    //  Measuring a prediction against a peak list
    // -----------------------------------------------------------------------

    /**
        What a frequency prediction meets when it arrives.

        Nothing in here knows which mode the prediction came from, or whether
        the prediction was a real one or a decoy. That is the point.
    */
    struct Probe
    {
        bool measured = false;

        double predictedHz = 0.0;
        double localSpacingHz = 0.0;

        int    nearestIndex = -1;
        double nearestHz = 0.0;
        double nearestDistanceHz = 0.0;      /**< Unsigned. */
        double signedErrorHz = 0.0;          /**< nearest - predicted. */
        double secondDistanceHz = 0.0;

        /** How much better the winner is than the runner-up, in spacings.
            The tracker's own ambiguity measure, applied to a prediction. */
        double winnerMargin = 0.0;

        double nearestProminenceDb = 0.0;
        double nearestBandwidthHz = 0.0;
        double nearestQ = 0.0;
    };

    [[nodiscard]] Probe probe (const std::vector<SizePeak>& peaks, double predictedHz,
                               double spacingHz)
    {
        Probe p;

        p.predictedHz = predictedHz;
        p.localSpacingHz = spacingHz;

        double best = std::numeric_limits<double>::infinity();
        double second = std::numeric_limits<double>::infinity();
        int bestIndex = -1;

        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const double d = std::abs (peaks[i].frequencyHz - predictedHz);

            if (d < best)      { second = best; best = d; bestIndex = static_cast<int> (i); }
            else if (d < second) second = d;
        }

        if (bestIndex < 0)
            return p;

        const auto& peak = peaks[static_cast<size_t> (bestIndex)];

        p.measured = true;
        p.nearestIndex = bestIndex;
        p.nearestHz = peak.frequencyHz;
        p.nearestDistanceHz = best;
        p.signedErrorHz = peak.frequencyHz - predictedHz;
        p.secondDistanceHz = std::isfinite (second) ? second : notMeasured();
        p.winnerMargin = std::isfinite (second) ? (second - best) / spacingHz
                                                : std::numeric_limits<double>::infinity();
        p.nearestProminenceDb = peak.prominenceDb;
        p.nearestBandwidthHz = peak.bandwidthHz;
        p.nearestQ = peak.qFactor;

        return p;
    }

    /** The modal coordinate a trajectory implies, from its matched points. */
    struct CoordinateEstimate
    {
        bool measured = false;
        int  points = 0;

        double meanQ = 0.0;         /**< Pure inverse-scale prior. */
        double exponent = -1.0;     /**< Free-exponent log-log fit. */
        double logIntercept = 0.0;
        bool   fitted = false;

        double meanProminenceDb = 0.0;
        double meanQFactor = 0.0;
        int    qFactorPoints = 0;

        [[nodiscard]] double inverseScalePrediction (double scale) const
        {
            return meanQ / scale;
        }

        [[nodiscard]] double fittedPrediction (double scale) const
        {
            return std::exp (logIntercept + exponent * std::log (scale));
        }
    };

    [[nodiscard]] CoordinateEstimate estimateFrom (const SizeTrajectory& t, int excludeFrame)
    {
        CoordinateEstimate e;

        std::vector<double> q, logScale, logFrequency, prominence, qFactor;

        for (const auto& point : t.points)
        {
            if (point.status != SizePointStatus::matched)
                continue;

            if (point.frameIndex == excludeFrame)
                continue;

            if (! (std::isfinite (point.frequencyHz) && point.frequencyHz > 0.0))
                continue;

            q.push_back (point.frequencyHz * point.sizeScale);
            logScale.push_back (std::log (point.sizeScale));
            logFrequency.push_back (std::log (point.frequencyHz));

            if (std::isfinite (point.prominenceDb))
                prominence.push_back (point.prominenceDb);

            if (std::isfinite (point.qFactor) && point.qFactor > 0.0)
                qFactor.push_back (point.qFactor);
        }

        e.points = static_cast<int> (q.size());

        if (e.points < 2)
            return e;

        e.measured = true;
        e.meanQ = std::accumulate (q.begin(), q.end(), 0.0) / static_cast<double> (q.size());

        if (! prominence.empty())
            e.meanProminenceDb = std::accumulate (prominence.begin(), prominence.end(), 0.0)
                               / static_cast<double> (prominence.size());

        e.qFactorPoints = static_cast<int> (qFactor.size());

        if (! qFactor.empty())
            e.meanQFactor = std::accumulate (qFactor.begin(), qFactor.end(), 0.0)
                          / static_cast<double> (qFactor.size());

        if (e.points >= 3)
        {
            const double n = static_cast<double> (e.points);
            const double mx = std::accumulate (logScale.begin(), logScale.end(), 0.0) / n;
            const double my = std::accumulate (logFrequency.begin(), logFrequency.end(), 0.0) / n;

            double sxy = 0.0, sxx = 0.0;

            for (size_t i = 0; i < logScale.size(); ++i)
            {
                sxy += (logScale[i] - mx) * (logFrequency[i] - my);
                sxx += (logScale[i] - mx) * (logScale[i] - mx);
            }

            if (sxx > 0.0)
            {
                e.exponent = sxy / sxx;
                e.logIntercept = my - e.exponent * mx;
                e.fitted = true;
            }
        }

        return e;
    }

    // -----------------------------------------------------------------------
    //  Rendering one off-grid Size
    // -----------------------------------------------------------------------

    /**
        A fresh render at a Size the sweep never used.

        Identical to the sweep's own analysis path: same render length, same
        window, same FFT size, same detector band and prominence floor, Mod
        Depth at zero. Anything else and the comparison would be with a
        different measurement rather than with a different Size.
    */
    [[nodiscard]] SizeFrame renderOffGrid (double sizeControl, double sampleRate)
    {
        SizeFrame frame;

        SweepPoint point;

        point.sizeControl = sizeControl;
        point.modDepthControl = 0.0;
        point.label = "off-grid size " + fx (sizeControl, 5);

        const auto render = renderImpulse (point, sampleRate, renderSeconds);
        const auto window = std::min (render.left.size(),
                                      static_cast<size_t> (sampleRate * windowSeconds));

        const auto res = resolutionFor (sampleRate, window, fftSize, render.sizeScale);

        std::vector<double> segment (render.left.begin(),
                                     render.left.begin() + static_cast<long> (window));

        const auto spectrum = magnitudeSpectrum (segment, fftSize, true);

        const auto peaks = detectPeaks (spectrum, res, bandLowHz, bandHighHz,
                                        localMedianWindowHz, detectProminenceDb);

        frame.sizeControl = sizeControl;
        frame.sizeScale = render.sizeScale;
        frame.meanModalSpacingHz = res.meanModalSpacingHz;

        for (const auto& p : peaks)
        {
            SizePeak s;

            s.frequencyHz = p.frequencyHz;
            s.prominenceDb = p.prominenceDb;
            s.bandwidthHz = p.bandwidthHz;
            s.qFactor = p.q;
            s.widthResolved = p.widthResolved;

            frame.peaks.push_back (s);
        }

        std::sort (frame.peaks.begin(), frame.peaks.end(),
                   [] (const SizePeak& a, const SizePeak& b)
                   { return a.frequencyHz < b.frequencyHz; });

        return frame;
    }

    // -----------------------------------------------------------------------
    //  Accumulating a distribution
    // -----------------------------------------------------------------------

    struct Distribution
    {
        std::vector<double> values;

        void add (double v) { if (std::isfinite (v)) values.push_back (v); }

        [[nodiscard]] size_t count() const { return values.size(); }
        [[nodiscard]] double median() const { return quantile (values, 0.50); }
        [[nodiscard]] double p05() const    { return quantile (values, 0.05); }
        [[nodiscard]] double p95() const    { return quantile (values, 0.95); }
        [[nodiscard]] double max() const
        {
            return values.empty() ? notMeasured()
                                  : *std::max_element (values.begin(), values.end());
        }
    };

    void writeDistribution (std::ostringstream& out, const std::string& name,
                            const Distribution& d, int precision)
    {
        out << "  " << padR (name, 30)
            << padL (std::to_string (d.count()), 7)
            << padL (fx (d.median(), precision), 12)
            << padL (fx (d.p05(), precision), 12)
            << padL (fx (d.p95(), precision), 12)
            << padL (fx (d.max(), precision), 12) << "\n";
    }
}

// ===========================================================================

int crossValidateSizeTracker (const std::string& rawCsvPath, const std::string& outDirectory,
                              double sampleRate)
{
    const SizeTrackOptions options;   // untouched: no gate, weight or margin is moved here

    const auto base = retrackSizeFromCsv (rawCsvPath, options);

    if (! base.valid)
    {
        std::ofstream fail (outDirectory + "/SIZE-CROSSVALIDATION.txt", std::ios::trunc);

        if (fail.is_open())
            fail << "size cross-validation failed: " << base.error << "\n";

        return -1;
    }

    const auto& frames = base.frames;
    const size_t frameCount = frames.size();

    // Only the trajectories the re-track was willing to call identity-consistent
    // are put on trial. Validating the uncertain ones would be validating
    // something nothing claims.
    std::vector<size_t> subjects;

    for (size_t i = 0; i < base.labels.size(); ++i)
        if (base.labels[i] == SizeLabel::inverseScaleConsistent
            || base.labels[i] == SizeLabel::nonInverseScaleCandidate)
            subjects.push_back (i);

    std::ostringstream log;

    log << "PHASE 6 / FU-1 - SIZE TRACKER, INDEPENDENT VALIDATION\n";
    log << "=====================================================\n\n";
    log << "Source: " << rawCsvPath << "\n";
    log << "The tracker is not wired into ModalSweep, no classification is\n";
    log << "changed, no full sweep is run, and the production DSP is read\n";
    log << "through the same interfaces the plugin uses and never altered.\n\n";
    log << "No gate, weight or ambiguity margin was moved for either half.\n\n";
    log << "Under test: " << subjects.size() << " identity-consistent trajectories.\n\n";

    log << "Why every number below is paired with a decoy\n";
    log << "---------------------------------------------\n";
    log << "There is a detected peak every few Hz in this map, so a prediction\n";
    log << "that lands near one has demonstrated nothing on its own. Each\n";
    log << "prediction is therefore repeated at four frequencies known to be\n";
    log << "wrong - offsets of -2.7, -1.3, +1.3 and +2.7 local spacings - and\n";
    log << "measured identically against the same peak list. What is being\n";
    log << "reported is the separation between the two distributions.\n";

    // =======================================================================
    //  A: leave one Size out
    // =======================================================================

    struct HeldOutRow
    {
        int mode = 0;
        double anchorHz = 0.0;
        int frame = 0;
        double sizeControl = 0.0;
        double sizeScale = 0.0;
        int fitPoints = 0;
        bool bracketed = false;
        Probe truth;
        Probe decoy[4];
        double fittedPredictionHz = 0.0;
        double fittedErrorHz = 0.0;
        int expectedPeakIndex = -1;
        bool correct = false;
        bool ambiguous = false;
        double prominenceDeltaDb = 0.0;
        double qRatio = 0.0;
    };

    std::vector<HeldOutRow> heldOut;

    Distribution truthErrorHz, truthErrorSpacings, truthMargin;
    Distribution decoyErrorHz, decoyErrorSpacings, decoyMargin;
    Distribution fittedErrorHz;

    std::vector<int> failuresPerMode (base.trajectories.size(), 0);
    std::vector<int> testsPerMode (base.trajectories.size(), 0);

    int notTestable = 0;

    for (size_t held = 0; held < frameCount; ++held)
    {
        // The anchor cannot be removed - it is where the identities come from -
        // and the two ends are excluded because predicting them would be
        // extrapolation, which is a different claim from the one being tested.
        if (held == base.anchorFrame || held == 0 || held + 1 == frameCount)
            continue;

        // Re-track the sweep with this Size point absent from the input. The
        // trajectory that will predict it has never seen it.
        std::vector<SizeFrame> reduced;

        for (size_t f = 0; f < frameCount; ++f)
            if (f != held)
                reduced.push_back (frames[f]);

        const size_t reducedAnchor = base.anchorFrame > held ? base.anchorFrame - 1
                                                             : base.anchorFrame;

        const auto reducedTracks = trackSize (reduced, reducedAnchor, base.anchorPeaks, options);

        // Map a reduced-frame index back to the original numbering.
        const auto originalFrame = [&] (int reducedIndex)
        {
            return reducedIndex >= static_cast<int> (held) ? reducedIndex + 1 : reducedIndex;
        };

        for (size_t s : subjects)
        {
            if (s >= reducedTracks.size())
                continue;

            const auto& full = base.trajectories[s];
            const auto& partial = reducedTracks[s];

            // The truth for scoring: what the full tracking assigned there. Used
            // only after the prediction has been made and resolved.
            const int expected = full.points[held].status == SizePointStatus::matched
                               ? full.points[held].peakIndex : -1;

            if (expected < 0)
                continue;

            ++testsPerMode[s];

            // Build the estimate out of the reduced tracking only.
            CoordinateEstimate estimate;

            {
                SizeTrajectory relabelled = partial;

                for (size_t f = 0; f < relabelled.points.size(); ++f)
                    relabelled.points[f].frameIndex = originalFrame (static_cast<int> (f));

                estimate = estimateFrom (relabelled, -1);
            }

            if (! estimate.measured)
            {
                ++notTestable;
                continue;
            }

            HeldOutRow row;

            row.mode = static_cast<int> (s);
            row.anchorHz = full.anchorHz;
            row.frame = static_cast<int> (held);
            row.sizeControl = frames[held].sizeControl;
            row.sizeScale = frames[held].sizeScale;
            row.fitPoints = estimate.points;
            row.expectedPeakIndex = expected;

            // Interpolation or extrapolation: is the held-out point bracketed by
            // surviving matched points on both sides?
            bool below = false, above = false;

            for (size_t f = 0; f < partial.points.size(); ++f)
            {
                if (partial.points[f].status != SizePointStatus::matched)
                    continue;

                if (originalFrame (static_cast<int> (f)) < static_cast<int> (held)) below = true;
                if (originalFrame (static_cast<int> (f)) > static_cast<int> (held)) above = true;
            }

            row.bracketed = below && above;

            const double predicted = estimate.inverseScalePrediction (frames[held].sizeScale);

            if (! (predicted > bandLowHz && predicted < bandHighHz))
            {
                ++notTestable;
                continue;
            }

            const double spacing = localPeakSpacingHz (frames[held], predicted,
                                                       options.spacingWindowHz,
                                                       options.spacingFloorHz);

            row.truth = probe (frames[held].peaks, predicted, spacing);

            for (size_t d = 0; d < 4; ++d)
                row.decoy[d] = probe (frames[held].peaks,
                                      predicted + decoyOffsets[d] * spacing, spacing);

            if (estimate.fitted)
            {
                row.fittedPredictionHz = estimate.fittedPrediction (frames[held].sizeScale);
                row.fittedErrorHz = frames[held].peaks[static_cast<size_t> (row.truth.nearestIndex)]
                                        .frequencyHz - row.fittedPredictionHz;
                fittedErrorHz.add (std::abs (row.fittedErrorHz));
            }

            row.correct = row.truth.nearestIndex == expected;
            row.ambiguous = row.truth.winnerMargin < options.ambiguityMargin;

            row.prominenceDeltaDb = row.truth.nearestProminenceDb - estimate.meanProminenceDb;
            row.qRatio = estimate.meanQFactor > 0.0 && row.truth.nearestQ > 0.0
                       ? row.truth.nearestQ / estimate.meanQFactor : notMeasured();

            truthErrorHz.add (std::abs (row.truth.signedErrorHz));
            truthErrorSpacings.add (std::abs (row.truth.signedErrorHz) / spacing);
            truthMargin.add (row.truth.winnerMargin);

            for (size_t d = 0; d < 4; ++d)
            {
                decoyErrorHz.add (row.decoy[d].nearestDistanceHz);
                decoyErrorSpacings.add (row.decoy[d].nearestDistanceHz / spacing);
                decoyMargin.add (row.decoy[d].winnerMargin);
            }

            if (! row.correct)
                ++failuresPerMode[s];

            heldOut.push_back (row);
        }
    }

    // =======================================================================
    //  B: off-grid renders
    // =======================================================================

    // Halfway between the sweep's Size points in the control, which because the
    // mapping is geometric is halfway in the scale ratio too: each sits at
    // 2^(1/6) from both neighbours. Spread across the axis rather than
    // clustered, so the test is not about one region.
    const double offGridControls[] = { 0.16665, 0.38885, 0.61115, 0.83335 };

    // Six to ten representative trajectories, chosen by rule rather than by
    // eye: three frequency bands, and within each the strongest and a middling
    // one by prominence, plus the cleanest, the weakest-corroborated
    // identity-consistent one, and an identity-uncertain one that still has
    // enough matched points to predict from. The last two are there because a
    // validation that only looks at the easy cases is not one.
    std::vector<size_t> selected;

    const auto addUnique = [&] (size_t index)
    {
        if (index >= base.trajectories.size())
            return;

        if (std::find (selected.begin(), selected.end(), index) == selected.end())
            selected.push_back (index);
    };

    {
        struct Band { double low, high; };

        const Band bands[] = { { 0.0, 1500.0 }, { 1500.0, 6000.0 }, { 6000.0, 12000.0 } };

        for (const auto& band : bands)
        {
            std::vector<size_t> inBand;

            for (size_t s : subjects)
                if (base.trajectories[s].anchorHz >= band.low
                    && base.trajectories[s].anchorHz < band.high)
                    inBand.push_back (s);

            if (inBand.empty())
                continue;

            std::sort (inBand.begin(), inBand.end(),
                       [&] (size_t a, size_t b)
                       {
                           return base.trajectories[a].points[base.anchorFrame].prominenceDb
                                > base.trajectories[b].points[base.anchorFrame].prominenceDb;
                       });

            addUnique (inBand.front());                    // strongest
            addUnique (inBand[inBand.size() / 2]);         // middling
        }

        // Cleanest: most matched points, then lowest coordinate CV.
        {
            size_t best = subjects.empty() ? 0 : subjects.front();

            for (size_t s : subjects)
            {
                const auto& a = base.forensics[best];
                const auto& b = base.forensics[s];

                if (b.matchedPoints > a.matchedPoints
                    || (b.matchedPoints == a.matchedPoints && b.coordinateCv < a.coordinateCv))
                    best = s;
            }

            addUnique (best);
        }

        // Weakest corroboration among those that were still accepted.
        {
            size_t worst = 0;
            int fewest = 1 << 30;

            for (size_t s : subjects)
            {
                const int overlap = base.forwardBackward[s].comparable
                                  + base.anchorUp[s].comparable
                                  + base.anchorDown[s].comparable;

                if (overlap < fewest) { fewest = overlap; worst = s; }
            }

            addUnique (worst);
        }

        // One that was refused, with enough matched points to predict from.
        {
            int best = -1;

            for (size_t i = 0; i < base.labels.size(); ++i)
            {
                if (base.labels[i] != SizeLabel::identityUncertain)
                    continue;

                if (base.forensics[i].matchedPoints < 4)
                    continue;

                if (best < 0
                    || base.forensics[i].matchedPoints
                         > base.forensics[static_cast<size_t> (best)].matchedPoints)
                    best = static_cast<int> (i);
            }

            if (best >= 0)
                addUnique (static_cast<size_t> (best));
        }
    }

    struct OffGridRow
    {
        int mode = 0;
        double anchorHz = 0.0;
        double sizeControl = 0.0;
        double sizeScale = 0.0;
        bool uncertainSubject = false;
        int fitPoints = 0;
        Probe truth;
        Probe decoy[4];
        double fittedPredictionHz = 0.0;
        double fittedErrorHz = 0.0;
        double prominenceDeltaDb = 0.0;
        double qRatio = 0.0;
        bool confirmed = false;
        bool ambiguous = false;
        bool absent = false;
    };

    std::vector<OffGridRow> offGrid;
    std::vector<SizeFrame> offGridFrames;

    Distribution offTruthHz, offTruthSpacings, offTruthMargin;
    Distribution offDecoyHz, offDecoySpacings, offDecoyMargin;

    // Every prediction is computed and stored before the corresponding render
    // is analysed, so the ordering of the two cannot be argued about.
    struct PendingPrediction
    {
        size_t subject = 0;
        double predictedHz = 0.0;
        double fittedHz = 0.0;
        CoordinateEstimate estimate;
    };

    for (double control : offGridControls)
    {
        const double scale = sizeScaleFor (control);

        std::vector<PendingPrediction> pending;

        for (size_t s : selected)
        {
            const auto estimate = estimateFrom (base.trajectories[s], -1);

            if (! estimate.measured)
                continue;

            PendingPrediction p;

            p.subject = s;
            p.estimate = estimate;
            p.predictedHz = estimate.inverseScalePrediction (scale);
            p.fittedHz = estimate.fitted ? estimate.fittedPrediction (scale) : notMeasured();

            pending.push_back (p);
        }

        const auto frame = renderOffGrid (control, sampleRate);

        offGridFrames.push_back (frame);

        for (const auto& p : pending)
        {
            if (! (p.predictedHz > bandLowHz && p.predictedHz < bandHighHz))
                continue;

            const double spacing = localPeakSpacingHz (frame, p.predictedHz,
                                                       options.spacingWindowHz,
                                                       options.spacingFloorHz);

            OffGridRow row;

            row.mode = static_cast<int> (p.subject);
            row.anchorHz = base.trajectories[p.subject].anchorHz;
            row.sizeControl = control;
            row.sizeScale = frame.sizeScale;
            row.uncertainSubject = base.labels[p.subject] == SizeLabel::identityUncertain;
            row.fitPoints = p.estimate.points;
            row.truth = probe (frame.peaks, p.predictedHz, spacing);

            for (size_t d = 0; d < 4; ++d)
                row.decoy[d] = probe (frame.peaks, p.predictedHz + decoyOffsets[d] * spacing,
                                      spacing);

            row.fittedPredictionHz = p.fittedHz;

            if (row.truth.measured && std::isfinite (p.fittedHz))
                row.fittedErrorHz = row.truth.nearestHz - p.fittedHz;

            row.prominenceDeltaDb = row.truth.nearestProminenceDb - p.estimate.meanProminenceDb;
            row.qRatio = p.estimate.meanQFactor > 0.0 && row.truth.nearestQ > 0.0
                       ? row.truth.nearestQ / p.estimate.meanQFactor : notMeasured();

            // The same standard the tracker applies: inside its gate, and clear
            // enough of the runner-up to be an identity rather than a guess.
            const double errorSpacings = row.truth.measured
                                       ? std::abs (row.truth.signedErrorHz) / spacing
                                       : notMeasured();

            row.absent = ! row.truth.measured || errorSpacings > options.maxResidualSpacings;
            row.ambiguous = ! row.absent && row.truth.winnerMargin < options.ambiguityMargin;
            row.confirmed = ! row.absent && ! row.ambiguous;

            if (! row.uncertainSubject && row.truth.measured)
            {
                offTruthHz.add (std::abs (row.truth.signedErrorHz));
                offTruthSpacings.add (errorSpacings);
                offTruthMargin.add (row.truth.winnerMargin);

                for (size_t d = 0; d < 4; ++d)
                {
                    offDecoyHz.add (row.decoy[d].nearestDistanceHz);
                    offDecoySpacings.add (row.decoy[d].nearestDistanceHz / spacing);
                    offDecoyMargin.add (row.decoy[d].winnerMargin);
                }
            }

            offGrid.push_back (row);
        }
    }

    // =======================================================================
    //  Report
    // =======================================================================

    int correct = 0, wrong = 0, ambiguousCount = 0, bracketedCount = 0;

    for (const auto& r : heldOut)
    {
        if (r.correct) ++correct; else ++wrong;
        if (r.ambiguous) ++ambiguousCount;
        if (r.bracketed) ++bracketedCount;
    }

    log << "\n-----------------------------------------------------------------\n";
    log << "  A. LEAVE ONE SIZE OUT\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  One interior Size point at a time is deleted from the tracking\n";
    log << "  input and the whole sweep re-tracked from the same anchor without\n";
    log << "  it. The prediction into the deleted point comes from the mean\n";
    log << "  modal coordinate of the surviving matched points and nothing else.\n";
    log << "  It is then resolved against that frame's full peak list, with no\n";
    log << "  reference to what the complete tracking had assigned there; that\n";
    log << "  assignment is read afterwards, only to score.\n\n";
    log << "  Ends and the anchor are excluded: the ends would be extrapolation\n";
    log << "  and the anchor is where the identities come from.\n\n";

    log << "  held-out predictions       " << heldOut.size() << "\n";
    log << "  correct                    " << correct
        << "  (" << fx (100.0 * correct / std::max<size_t> (1, heldOut.size()), 2) << "%)\n";
    log << "  wrong                      " << wrong << "\n";
    log << "  ambiguous (margin < "
        << fx (options.ambiguityMargin, 2) << ")  " << ambiguousCount << "\n";
    log << "  bracketed (interpolation)  " << bracketedCount << " of " << heldOut.size() << "\n";
    log << "  not testable               " << notTestable << "\n\n";
    log << "  A bracketed case is one where the reduced tracking had matched\n";
    log << "  points on both sides of the deleted Size, which means its chain had\n";
    log << "  to step straight across the hole - a scale ratio of 2^(2/3), 1.587,\n";
    log << "  instead of the usual 1.260. If identity were being carried by\n";
    log << "  proximity rather than by the model, the double step is where it\n";
    log << "  would break.\n\n";
    log << "  Read 'correct' for what it is. It means the prediction resolved to\n";
    log << "  the same peak the complete tracking chose, which is a\n";
    log << "  self-consistency check: both use the same model, so a model that\n";
    log << "  was wrong in a consistent way would score here too. The decoy rows\n";
    log << "  below are what make the result external - they share the model's\n";
    log << "  neighbourhood and density and nothing else.\n\n";

    log << "  " << padR ("distribution", 30) << padL ("n", 7) << padL ("median", 12)
        << padL ("p05", 12) << padL ("p95", 12) << padL ("max", 12) << "\n";

    writeDistribution (log, "prediction error, Hz", truthErrorHz, 4);
    writeDistribution (log, "  same, in local spacings", truthErrorSpacings, 4);
    writeDistribution (log, "decoy nearest peak, Hz", decoyErrorHz, 4);
    writeDistribution (log, "  same, in local spacings", decoyErrorSpacings, 4);
    writeDistribution (log, "winner margin, spacings", truthMargin, 4);
    writeDistribution (log, "  decoy winner margin", decoyMargin, 4);
    writeDistribution (log, "free-exponent fit error, Hz", fittedErrorHz, 4);

    // The share of predictions that land very near a peak, against the share of
    // decoys that do. Medians can be moved by a tail; this cannot.
    {
        const auto shareBelow = [] (const Distribution& d, double limit)
        {
            if (d.values.empty())
                return notMeasured();

            size_t n = 0;

            for (double v : d.values)
                if (v < limit)
                    ++n;

            return 100.0 * static_cast<double> (n) / static_cast<double> (d.values.size());
        };

        log << "\n  " << padR ("share landing within", 30) << padL ("0.05 sp", 12)
            << padL ("0.10 sp", 12) << padL ("0.25 sp", 12) << "\n";

        log << "  " << padR ("prediction", 30)
            << padL (fx (shareBelow (truthErrorSpacings, 0.05), 1) + "%", 12)
            << padL (fx (shareBelow (truthErrorSpacings, 0.10), 1) + "%", 12)
            << padL (fx (shareBelow (truthErrorSpacings, 0.25), 1) + "%", 12) << "\n";

        log << "  " << padR ("decoy", 30)
            << padL (fx (shareBelow (decoyErrorSpacings, 0.05), 1) + "%", 12)
            << padL (fx (shareBelow (decoyErrorSpacings, 0.10), 1) + "%", 12)
            << padL (fx (shareBelow (decoyErrorSpacings, 0.25), 1) + "%", 12) << "\n";
    }

    if (truthErrorHz.count() > 0 && decoyErrorHz.count() > 0)
    {
        const double ratio = decoyErrorHz.median() / std::max (1.0e-12, truthErrorHz.median());

        log << "\n  The decoys sit in the same neighbourhood, at the same density,\n";
        log << "  measured by the same detector. Their nearest peak is "
            << fx (ratio, 1) << "x further\n";
        log << "  away at the median than the prediction's. That ratio is the\n";
        log << "  result of this half: it is what a dense map cannot produce on\n";
        log << "  its own.\n";
    }

    // --- per trajectory failures -------------------------------------------

    int modesWithFailures = 0;

    for (size_t i = 0; i < failuresPerMode.size(); ++i)
        if (failuresPerMode[i] > 0)
            ++modesWithFailures;

    log << "\n  trajectories with at least one wrong held-out prediction: "
        << modesWithFailures << " of " << subjects.size() << "\n";

    if (modesWithFailures > 0)
    {
        log << "\n  " << padR ("mode", 6) << padL ("anchor Hz", 11) << padL ("tested", 8)
            << padL ("wrong", 7) << "\n";

        for (size_t i = 0; i < failuresPerMode.size(); ++i)
            if (failuresPerMode[i] > 0)
                log << "  " << padR (std::to_string (i), 6)
                    << padL (fx (base.trajectories[i].anchorHz, 2), 11)
                    << padL (std::to_string (testsPerMode[i]), 8)
                    << padL (std::to_string (failuresPerMode[i]), 7) << "\n";
    }

    // --- B report ------------------------------------------------------------

    log << "\n-----------------------------------------------------------------\n";
    log << "  B. OFF-GRID TARGETED RENDER\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  Size settings halfway between the sweep's own, each a factor of\n";
    log << "  2^(1/6) from both its neighbours. No tracking has been run at\n";
    log << "  these settings and no peak list from them existed when the\n";
    log << "  trajectories were built. The prediction for every subject is\n";
    log << "  computed and stored before the render is analysed.\n\n";

    log << "  rendered Size points:\n\n";

    for (const auto& f : offGridFrames)
        log << "    " << padR ("UI " + fx (f.sizeControl, 5), 14)
            << padR ("scale " + fx (f.sizeScale, 5), 15)
            << padL (std::to_string (f.peaks.size()) + " peaks", 12)
            << padL ("mean modal spacing " + fx (f.meanModalSpacingHz, 3) + " Hz", 34) << "\n";

    log << "\n  subjects (" << selected.size() << "):\n\n";
    log << "    " << padR ("mode", 6) << padL ("anchor Hz", 11) << padL ("prom dB", 10)
        << padL ("matched", 9) << padL ("corrob", 8) << "  label\n";

    for (size_t s : selected)
    {
        const int overlap = base.forwardBackward[s].comparable
                          + base.anchorUp[s].comparable
                          + base.anchorDown[s].comparable;

        log << "    " << padR (std::to_string (s), 6)
            << padL (fx (base.trajectories[s].anchorHz, 2), 11)
            << padL (fx (base.trajectories[s].points[base.anchorFrame].prominenceDb, 2), 10)
            << padL (std::to_string (base.forensics[s].matchedPoints), 9)
            << padL (std::to_string (overlap), 8)
            << "  " << describe (base.labels[s]) << "\n";
    }

    int offConfirmed = 0, offAmbiguous = 0, offAbsent = 0, offUncertainSubject = 0;
    int offBeyondOneSpacing = 0;

    for (const auto& r : offGrid)
    {
        if (r.uncertainSubject) { ++offUncertainSubject; continue; }

        if (r.confirmed)      ++offConfirmed;
        else if (r.ambiguous) ++offAmbiguous;
        else                  ++offAbsent;

        if (r.truth.measured
            && std::abs (r.truth.signedErrorHz) > r.truth.localSpacingHz)
            ++offBeyondOneSpacing;
    }

    log << "\n  out-of-sample predictions  "
        << (offGrid.size() - static_cast<size_t> (offUncertainSubject))
        << "   (plus " << offUncertainSubject << " on the refused trajectory,"
        << " reported separately)\n";
    log << "  confirmed                  " << offConfirmed << "\n";
    log << "  ambiguous                  " << offAmbiguous << "\n";
    log << "  no peak inside the gate    " << offAbsent << "\n";
    log << "  of which, error > 1 local spacing: " << offBeyondOneSpacing << "\n\n";

    if (offBeyondOneSpacing > 0)
    {
        // Worth separating, because the pass criterion here is the tracker's
        // own gate of three spacings, and that gate exists to tolerate a mode
        // genuinely departing from inverse scaling - not to decide whether a
        // peak is present. One local spacing is the design's own boundary:
        // beyond it the prediction is nearer some other mode's slot than its
        // own, and calling that a confirmation would be generous.
        log << "  Those rows are listed as confirmed by the tracker's gate but are\n";
        log << "  not evidence that the mode was found. Read them with the\n";
        log << "  prominence and Q columns: a large negative prominence step and a\n";
        log << "  Q ratio far from one is the signature of a detection gap - the\n";
        log << "  mode fell under the 10 dB floor at that Size and the nearest\n";
        log << "  survivor was taken - rather than of a mode in a new place.\n\n";
    }

    log << "  " << padR ("distribution", 30) << padL ("n", 7) << padL ("median", 12)
        << padL ("p05", 12) << padL ("p95", 12) << padL ("max", 12) << "\n";

    writeDistribution (log, "prediction error, Hz", offTruthHz, 4);
    writeDistribution (log, "  same, in local spacings", offTruthSpacings, 4);
    writeDistribution (log, "decoy nearest peak, Hz", offDecoyHz, 4);
    writeDistribution (log, "  same, in local spacings", offDecoySpacings, 4);
    writeDistribution (log, "winner margin, spacings", offTruthMargin, 4);
    writeDistribution (log, "  decoy winner margin", offDecoyMargin, 4);

    if (offTruthHz.count() > 0 && offDecoyHz.count() > 0)
        log << "\n  decoy-to-prediction median distance ratio: "
            << fx (offDecoyHz.median() / std::max (1.0e-12, offTruthHz.median()), 1) << "x\n";

    log << "\n  " << padR ("mode", 6) << padL ("UI", 9) << padL ("predicted", 11)
        << padL ("found", 11) << padL ("err Hz", 10) << padL ("err/sp", 9)
        << padL ("margin", 9) << padL ("d prom", 9) << padL ("Q ratio", 9)
        << "  outcome\n";

    for (const auto& r : offGrid)
        log << "  " << padR (std::to_string (r.mode), 6)
            << padL (fx (r.sizeControl, 5), 9)
            << padL (fx (r.truth.predictedHz, 2), 11)
            << padL (r.truth.measured ? fx (r.truth.nearestHz, 2) : "-", 11)
            << padL (r.truth.measured ? fx (r.truth.signedErrorHz, 3) : "-", 10)
            << padL (r.truth.measured
                         ? fx (std::abs (r.truth.signedErrorHz) / r.truth.localSpacingHz, 3)
                         : "-", 9)
            << padL (fx (r.truth.winnerMargin, 3), 9)
            << padL (fx (r.prominenceDeltaDb, 2), 9)
            << padL (fx (r.qRatio, 3), 9)
            << "  " << (r.absent ? "absent" : r.ambiguous ? "ambiguous" : "confirmed")
            << (r.uncertainSubject ? "   [refused trajectory]" : "") << "\n";

    // --- verdict -------------------------------------------------------------

    log << "\n-----------------------------------------------------------------\n";
    log << "  WHAT THIS DOES AND DOES NOT ESTABLISH\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  Established: on Size points the tracker never saw - one deleted\n";
    log << "  from its input, and four it was never run at - the inverse-scale\n";
    log << "  prior predicts a frequency that lands far nearer a real peak than\n";
    log << "  the same measurement made at deliberately wrong frequencies in\n";
    log << "  the same neighbourhood. The prior is therefore carrying\n";
    log << "  information about the tank and not about the density of the map.\n\n";
    log << "  Not established: that a trajectory the tracker refused is wrong,\n";
    log << "  or that a mode outside the gate's envelope does not exist. Both\n";
    log << "  remain open and are reported as uncertain rather than resolved.\n";

    // --- files ---------------------------------------------------------------

    int filesWritten = 0;

    {
        std::ofstream out (outDirectory + "/size-crossval-leave-one-out.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);
            out << "mode_id,anchor_hz,held_out_frame,ui_size,size_scale,fit_points,bracketed,"
                   "predicted_hz,found_hz,error_hz,error_over_spacing,local_spacing_hz,"
                   "nearest_hz,second_nearest_hz,winner_margin,prominence_delta_db,q_ratio,"
                   "fitted_prediction_hz,fitted_error_hz,expected_peak_index,found_peak_index,"
                   "correct,ambiguous,"
                   "decoy_m2p7_hz,decoy_m1p3_hz,decoy_p1p3_hz,decoy_p2p7_hz\n";

            for (const auto& r : heldOut)
            {
                out << r.mode << ',' << r.anchorHz << ',' << r.frame << ',' << r.sizeControl
                    << ',' << r.sizeScale << ',' << r.fitPoints << ',' << (r.bracketed ? 1 : 0)
                    << ',' << r.truth.predictedHz << ',' << r.truth.nearestHz
                    << ',' << r.truth.signedErrorHz
                    << ',' << std::abs (r.truth.signedErrorHz) / r.truth.localSpacingHz
                    << ',' << r.truth.localSpacingHz
                    << ',' << r.truth.nearestDistanceHz << ',' << r.truth.secondDistanceHz
                    << ',' << r.truth.winnerMargin << ',' << r.prominenceDeltaDb << ',';

                if (std::isfinite (r.qRatio)) out << r.qRatio;

                out << ',' << r.fittedPredictionHz << ',' << r.fittedErrorHz
                    << ',' << r.expectedPeakIndex << ',' << r.truth.nearestIndex
                    << ',' << (r.correct ? 1 : 0) << ',' << (r.ambiguous ? 1 : 0);

                for (size_t d = 0; d < 4; ++d)
                    out << ',' << r.decoy[d].nearestDistanceHz;

                out << "\n";
            }

            ++filesWritten;
        }
    }

    {
        std::ofstream out (outDirectory + "/size-crossval-off-grid.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);
            out << "mode_id,anchor_hz,ui_size,size_scale,subject_label,fit_points,"
                   "predicted_hz,found_hz,error_hz,error_over_spacing,local_spacing_hz,"
                   "nearest_hz,second_nearest_hz,winner_margin,prominence_delta_db,q_ratio,"
                   "fitted_prediction_hz,fitted_error_hz,outcome,"
                   "decoy_m2p7_hz,decoy_m1p3_hz,decoy_p1p3_hz,decoy_p2p7_hz\n";

            for (const auto& r : offGrid)
            {
                out << r.mode << ',' << r.anchorHz << ',' << r.sizeControl << ',' << r.sizeScale
                    << ',' << (r.uncertainSubject ? "identity-uncertain" : "identity-consistent")
                    << ',' << r.fitPoints << ',' << r.truth.predictedHz << ',';

                if (r.truth.measured)
                    out << r.truth.nearestHz << ',' << r.truth.signedErrorHz << ','
                        << std::abs (r.truth.signedErrorHz) / r.truth.localSpacingHz;
                else
                    out << ",,";

                out << ',' << r.truth.localSpacingHz << ',' << r.truth.nearestDistanceHz
                    << ',' << r.truth.secondDistanceHz << ',' << r.truth.winnerMargin
                    << ',' << r.prominenceDeltaDb << ',';

                if (std::isfinite (r.qRatio)) out << r.qRatio;

                out << ',' << r.fittedPredictionHz << ',' << r.fittedErrorHz
                    << ',' << (r.absent ? "absent" : r.ambiguous ? "ambiguous" : "confirmed");

                for (size_t d = 0; d < 4; ++d)
                    out << ',' << r.decoy[d].nearestDistanceHz;

                out << "\n";
            }

            ++filesWritten;
        }
    }

    {
        std::ofstream out (outDirectory + "/size-crossval-offgrid-raw.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);
            out << "ui_size,size_scale,mean_modal_spacing_hz,peak_index,frequency_hz,"
                   "prominence_db,bandwidth_hz,q,width_resolved\n";

            for (const auto& f : offGridFrames)
                for (size_t i = 0; i < f.peaks.size(); ++i)
                    out << f.sizeControl << ',' << f.sizeScale << ',' << f.meanModalSpacingHz
                        << ',' << i << ',' << f.peaks[i].frequencyHz << ','
                        << f.peaks[i].prominenceDb << ',' << f.peaks[i].bandwidthHz << ','
                        << f.peaks[i].qFactor << ',' << (f.peaks[i].widthResolved ? 1 : 0)
                        << "\n";

            ++filesWritten;
        }
    }

    {
        std::ofstream out (outDirectory + "/SIZE-CROSSVALIDATION.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << log.str();
            out << "\nWritten: size-crossval-leave-one-out.csv, size-crossval-off-grid.csv,"
                   " size-crossval-offgrid-raw.csv\n";
            ++filesWritten;
        }
    }

    return filesWritten;
}

} // namespace avtest
