#include "SizeTrack.h"

#include "ModalMap.h"

#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

/*
    Phase 6 / FU-1: offline re-track of an existing Size sweep.

    No render. The peak lists in size-sweep-raw.csv are the measurement and are
    read unchanged; tracking is the part that was wrong and it is the only part
    redone. The input directory is never written to.

    Three trackings are produced from the same peak lists so that "better" is a
    comparison and not an assertion:

      as-recorded   the trajectories the sweep itself wrote, read back from
                    size-sweep.csv when it is there
      replica       the old cost function driven over the same frames and the
                    same anchor peaks as the new one, which removes the anchor
                    set as an explanation for any difference
      new           the q-space tracker

    Every metric is computed by the same routine over all three.
*/
namespace avtest
{

namespace
{
    using av::dsp::mapping::sizeScaleFor;

    constexpr double notMeasured() { return std::numeric_limits<double>::quiet_NaN(); }

    /** The number of strongest anchor-frame peaks followed, as the sweep does. */
    constexpr size_t detailedModes = 60;

    /** The shipping default. The anchor is the sweep point nearest to it. */
    constexpr double defaultSizeControl = 0.60;


    // -----------------------------------------------------------------------

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

    [[nodiscard]] std::vector<std::string> splitCsv (const std::string& line)
    {
        std::vector<std::string> fields;
        std::string current;
        bool quoted = false;

        for (char c : line)
        {
            if (c == '"')            { quoted = ! quoted; continue; }
            if (c == ',' && ! quoted) { fields.push_back (current); current.clear(); continue; }

            current.push_back (c);
        }

        fields.push_back (current);

        return fields;
    }

    [[nodiscard]] double toDouble (const std::string& s)
    {
        if (s.empty())
            return notMeasured();

        try                  { return std::stod (s); }
        catch (...)          { return notMeasured(); }
    }

    // -----------------------------------------------------------------------
    //  Reading the raw peak lists
    // -----------------------------------------------------------------------

    [[nodiscard]] std::vector<SizeFrame> readRawFrames (const std::string& path, std::string& error)
    {
        std::vector<SizeFrame> frames;

        std::ifstream in (path);

        if (! in.is_open())
        {
            error = "could not open " + path;
            return frames;
        }

        std::string line;

        if (! std::getline (in, line))
        {
            error = "empty file: " + path;
            return frames;
        }

        // Column order is taken from the header rather than assumed, so a
        // future sweep that adds a field does not silently shift the numbers.
        std::map<std::string, size_t> column;

        {
            const auto header = splitCsv (line);

            for (size_t i = 0; i < header.size(); ++i)
                column[header[i]] = i;
        }

        const char* required[] = { "size_control", "mean_modal_spacing_hz", "frequency_hz",
                                   "prominence_db", "bandwidth_hz", "q", "width_resolved" };

        for (const char* name : required)
        {
            if (column.find (name) == column.end())
            {
                error = std::string ("missing column '") + name + "' in " + path;
                return frames;
            }
        }

        std::vector<double> order;

        while (std::getline (in, line))
        {
            if (line.empty())
                continue;

            const auto fields = splitCsv (line);

            if (fields.size() <= column["width_resolved"])
                continue;

            const double control = toDouble (fields[column["size_control"]]);

            if (! std::isfinite (control))
                continue;

            SizeFrame* frame = nullptr;

            for (auto& f : frames)
                if (std::abs (f.sizeControl - control) < 1.0e-9)
                    frame = &f;

            if (frame == nullptr)
            {
                SizeFrame created;

                created.sizeControl = control;
                created.sizeScale = sizeScaleFor (control);
                created.meanModalSpacingHz = toDouble (fields[column["mean_modal_spacing_hz"]]);

                frames.push_back (created);
                order.push_back (control);
                frame = &frames.back();
            }

            SizePeak peak;

            peak.frequencyHz = toDouble (fields[column["frequency_hz"]]);
            peak.prominenceDb = toDouble (fields[column["prominence_db"]]);
            peak.bandwidthHz = toDouble (fields[column["bandwidth_hz"]]);
            peak.qFactor = toDouble (fields[column["q"]]);
            peak.widthResolved = toDouble (fields[column["width_resolved"]]) > 0.5;

            if (std::isfinite (peak.frequencyHz))
                frame->peaks.push_back (peak);
        }

        std::sort (frames.begin(), frames.end(),
                   [] (const SizeFrame& a, const SizeFrame& b)
                   { return a.sizeControl < b.sizeControl; });

        for (auto& f : frames)
            std::sort (f.peaks.begin(), f.peaks.end(),
                       [] (const SizePeak& a, const SizePeak& b)
                       { return a.frequencyHz < b.frequencyHz; });

        return frames;
    }

    // -----------------------------------------------------------------------
    //  Reading back what the sweep wrote
    // -----------------------------------------------------------------------

    /**
        The as-recorded trajectories from size-sweep.csv.

        Frequencies only: the summary carries no residual and no spacing, which
        is part of why the identity loss was invisible in it. The same forensics
        are applied by recomputing both from the raw frames.
    */
    [[nodiscard]] std::vector<SizeTrajectory> readRecorded (const std::string& path,
                                                            const std::vector<SizeFrame>& frames)
    {
        std::vector<SizeTrajectory> out;

        std::ifstream in (path);

        if (! in.is_open())
            return out;

        std::string line;

        if (! std::getline (in, line))
            return out;

        std::vector<size_t> frameForColumn;   // column index -> frame index
        std::vector<size_t> columnIndices;

        {
            const auto header = splitCsv (line);

            for (size_t i = 0; i < header.size(); ++i)
            {
                const std::string prefix = "f_at_size_";

                if (header[i].rfind (prefix, 0) != 0)
                    continue;

                const double control = toDouble (header[i].substr (prefix.size()));

                for (size_t f = 0; f < frames.size(); ++f)
                {
                    if (std::abs (frames[f].sizeControl - control) < 1.0e-6)
                    {
                        columnIndices.push_back (i);
                        frameForColumn.push_back (f);
                        break;
                    }
                }
            }
        }

        if (columnIndices.empty())
            return out;

        while (std::getline (in, line))
        {
            if (line.empty())
                continue;

            const auto fields = splitCsv (line);

            SizeTrajectory t;

            t.id = static_cast<int> (out.size());
            t.anchorFrame = -1;
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
            }

            for (size_t k = 0; k < columnIndices.size(); ++k)
            {
                const size_t c = columnIndices[k];

                if (c >= fields.size())
                    continue;

                const double hz = toDouble (fields[c]);

                if (! std::isfinite (hz) || hz <= 0.0)
                    continue;

                auto& p = t.points[frameForColumn[k]];

                p.status = SizePointStatus::matched;
                p.frequencyHz = hz;
                p.modalCoordinate = hz * p.sizeScale;
            }

            out.push_back (std::move (t));
        }

        return out;
    }

    // -----------------------------------------------------------------------
    //  The old cost function, driven over the same frames
    // -----------------------------------------------------------------------

    [[nodiscard]] std::vector<SizeTrajectory> trackLegacy (const std::vector<SizeFrame>& frames,
                                                           size_t anchorFrame,
                                                           const std::vector<int>& anchorPeaks)
    {
        MatchOptions options;

        // As the sweep sets it: the reference frame's mean modal spacing.
        options.localSpacingHz = std::max (1.0, frames[anchorFrame].meanModalSpacingHz);

        const auto toModal = [] (const SizePeak& p)
        {
            ModalPeak m;
            m.frequencyHz = p.frequencyHz;
            m.prominenceDb = p.prominenceDb;
            m.bandwidthHz = p.bandwidthHz;
            m.q = p.qFactor;
            return m;
        };

        const size_t count = anchorPeaks.size();

        std::vector<SizeTrajectory> out (count);
        std::vector<ModalPeak> anchorModal;

        for (size_t i = 0; i < count; ++i)
        {
            const auto& peak = frames[anchorFrame].peaks[static_cast<size_t> (anchorPeaks[i])];

            out[i].id = static_cast<int> (i);
            out[i].anchorFrame = static_cast<int> (anchorFrame);
            out[i].anchorHz = peak.frequencyHz;
            out[i].points.resize (frames.size());

            for (size_t f = 0; f < frames.size(); ++f)
            {
                auto& p = out[i].points[f];

                p.frameIndex = static_cast<int> (f);
                p.sizeControl = frames[f].sizeControl;
                p.sizeScale = frames[f].sizeScale;
                p.status = SizePointStatus::unmatched;
                p.peakIndex = -1;
                p.frequencyHz = notMeasured();
            }

            auto& seed = out[i].points[anchorFrame];

            seed.status = SizePointStatus::matched;
            seed.peakIndex = anchorPeaks[i];
            seed.frequencyHz = peak.frequencyHz;
            seed.prominenceDb = peak.prominenceDb;
            seed.modalCoordinate = peak.frequencyHz * frames[anchorFrame].sizeScale;

            anchorModal.push_back (toModal (peak));
        }

        const auto walk = [&] (int step)
        {
            std::vector<ModalPeak> current = anchorModal;
            std::vector<double> last (count), previous (count);

            for (size_t i = 0; i < count; ++i)
                last[i] = previous[i] = anchorModal[i].frequencyHz;

            for (int f = static_cast<int> (anchorFrame) + step;
                 f >= 0 && f < static_cast<int> (frames.size());
                 f += step)
            {
                std::vector<ModalPeak> to;

                for (const auto& p : frames[static_cast<size_t> (f)].peaks)
                    to.push_back (toModal (p));

                std::vector<double> predicted (count);

                for (size_t i = 0; i < count; ++i)
                    predicted[i] = 2.0 * last[i] - previous[i];

                const auto match = matchPeaks (current, to, options, &predicted);

                for (size_t i = 0; i < count; ++i)
                {
                    const int j = match.assignment[i];

                    if (j < 0)
                        continue;

                    auto& point = out[i].points[static_cast<size_t> (f)];

                    point.status = SizePointStatus::matched;
                    point.peakIndex = j;
                    point.frequencyHz = to[static_cast<size_t> (j)].frequencyHz;
                    point.prominenceDb = to[static_cast<size_t> (j)].prominenceDb;
                    point.bandwidthHz = to[static_cast<size_t> (j)].bandwidthHz;
                    point.qFactor = to[static_cast<size_t> (j)].q;
                    point.modalCoordinate = point.frequencyHz
                                          * frames[static_cast<size_t> (f)].sizeScale;

                    previous[i] = last[i];
                    last[i] = point.frequencyHz;
                    current[i] = to[static_cast<size_t> (j)];
                }
            }
        };

        walk (+1);
        walk (-1);

        return out;
    }

    // -----------------------------------------------------------------------
    //  One residual definition for all three trackings
    // -----------------------------------------------------------------------

    // Moved to SizeTrack: recomputeInverseScaleResiduals, so the sweep and the
    // re-track report the same residual rather than each their own.
    using avtest::recomputeInverseScaleResiduals;


    // -----------------------------------------------------------------------
    //  Agreement between two trackings of the same trajectory
    // -----------------------------------------------------------------------

    using Agreement = SizeAgreement;

    [[nodiscard]] Agreement compare (const SizeTrajectory& a, const SizeTrajectory& b,
                                     int fromFrame, int toFrame)
    {
        Agreement result;

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
    }

    // -----------------------------------------------------------------------

    struct Summary
    {
        int trajectories = 0;
        int matchedPoints = 0;
        int ambiguousPoints = 0;
        int unmatchedPoints = 0;
        int outOfBandPoints = 0;

        int withReversals = 0;
        int totalReversals = 0;
        int withBigJumps = 0;
        int totalBigJumps = 0;

        int cvBelowLimit = 0;
        int cvMeasured = 0;
        double meanCv = 0.0;
        double medianCv = 0.0;

        int fitReliable = 0;
        double meanRSquared = 0.0;
        double meanExponent = 0.0;
        double meanResidualRmsHz = 0.0;
    };

    [[nodiscard]] Summary summarise (const std::vector<SizeForensics>& all)
    {
        Summary s;

        s.trajectories = static_cast<int> (all.size());

        std::vector<double> cvs;
        double rSquaredSum = 0.0, exponentSum = 0.0, residualSum = 0.0;
        int fitted = 0;

        for (const auto& f : all)
        {
            s.matchedPoints += f.matchedPoints;
            s.ambiguousPoints += f.ambiguousPoints;
            s.unmatchedPoints += f.unmatchedPoints;
            s.outOfBandPoints += f.outOfBandPoints;

            s.totalReversals += f.reversals;
            s.totalBigJumps += f.bigJumps;

            if (f.reversals > 0) ++s.withReversals;
            if (f.bigJumps > 0)  ++s.withBigJumps;

            if (f.coordinateCvMeasured)
            {
                ++s.cvMeasured;
                cvs.push_back (f.coordinateCv);

                if (f.coordinateCv < inverseScaleCvLimit)
                    ++s.cvBelowLimit;
            }

            if (f.fitPoints >= 3)
            {
                ++fitted;
                rSquaredSum += f.rSquared;
                exponentSum += f.exponent;
            }

            if (f.fitReliable)
                ++s.fitReliable;

            residualSum += f.residualRmsHz;
        }

        if (! cvs.empty())
        {
            s.meanCv = std::accumulate (cvs.begin(), cvs.end(), 0.0)
                     / static_cast<double> (cvs.size());

            std::sort (cvs.begin(), cvs.end());
            s.medianCv = cvs[cvs.size() / 2];
        }

        if (fitted > 0)
        {
            s.meanRSquared = rSquaredSum / fitted;
            s.meanExponent = exponentSum / fitted;
        }

        if (! all.empty())
            s.meanResidualRmsHz = residualSum / static_cast<double> (all.size());

        return s;
    }

    void writeSummary (std::ostringstream& out, const std::string& name, const Summary& s)
    {
        out << "  " << padR (name, 16)
            << padL (std::to_string (s.trajectories), 6)
            << padL (std::to_string (s.matchedPoints), 9)
            << padL (std::to_string (s.ambiguousPoints), 7)
            << padL (std::to_string (s.unmatchedPoints), 7)
            << padL (std::to_string (s.outOfBandPoints), 7)
            << padL (std::to_string (s.withReversals) + "/" + std::to_string (s.trajectories), 9)
            << padL (std::to_string (s.withBigJumps) + "/" + std::to_string (s.trajectories), 9)
            << padL (fx (100.0 * s.meanCv, 2) + "%", 10)
            << padL (fx (100.0 * s.medianCv, 2) + "%", 10)
            << padL (std::to_string (s.cvBelowLimit) + "/" + std::to_string (s.cvMeasured), 9)
            << "\n";
    }

    // -----------------------------------------------------------------------

    using Label = SizeLabel;

    [[nodiscard]] const char* describeLabel (Label l)
    {
        switch (l)
        {
            case Label::inverseScaleConsistent:   return "identity-consistent / inverse-scale-consistent";
            case Label::nonInverseScaleCandidate: return "identity-consistent / non-inverse-scale candidate";
            case Label::identityUncertain:        return "identity-uncertain";
        }

        return "unknown";
    }
}

// ===========================================================================

std::vector<SizeFrame> readSizeFramesCsv (const std::string& path, std::string& error)
{
    return readRawFrames (path, error);
}

// ===========================================================================

/*
    The tracking half of the re-track, on its own.

    Split out so the cross-validation runs against exactly these trajectories
    and exactly these labels rather than against its own re-derivation of them.
*/
SizeRetrackResult retrackSizeFromCsv (const std::string& rawCsvPath,
                                      const SizeTrackOptions& options)
{
    SizeRetrackResult result;

    result.frames = readRawFrames (rawCsvPath, result.error);

    if (result.frames.size() < 3)
    {
        if (result.error.empty())
            result.error = "fewer than three Size points in the raw CSV";

        return result;
    }

    const auto& frames = result.frames;

    // The forensic path has no reference render to anchor on, so it anchors on
    // the sweep point nearest the shipping default and takes its strongest
    // peaks. The sweep itself passes its reference frame instead; both go
    // through the same trackSizeAxis.
    size_t anchorFrame = 0;

    for (size_t f = 1; f < frames.size(); ++f)
        if (std::abs (frames[f].sizeControl - defaultSizeControl)
            < std::abs (frames[anchorFrame].sizeControl - defaultSizeControl))
            anchorFrame = f;

    {
        std::vector<int> byProminence (frames[anchorFrame].peaks.size());

        for (size_t i = 0; i < byProminence.size(); ++i)
            byProminence[i] = static_cast<int> (i);

        std::sort (byProminence.begin(), byProminence.end(),
                   [&] (int a, int b)
                   {
                       return frames[anchorFrame].peaks[static_cast<size_t> (a)].prominenceDb
                            > frames[anchorFrame].peaks[static_cast<size_t> (b)].prominenceDb;
                   });

        if (byProminence.size() > detailedModes)
            byProminence.resize (detailedModes);

        // Back into frequency order, so the mode ids read sensibly.
        std::sort (byProminence.begin(), byProminence.end());

        result.anchorPeaks = byProminence;
    }

    const auto axis = trackSizeAxis (frames, nullptr, result.anchorPeaks,
                                     defaultSizeControl, options);

    result.anchorFrame = axis.anchorFrame;
    result.trajectories = axis.trajectories;
    result.forwardBackward = axis.forwardBackward;
    result.anchorUp = axis.anchorUp;
    result.anchorDown = axis.anchorDown;

    // --- forensics and labels ------------------------------------------------

    // (count is no longer needed: labelTrajectories sizes its own result)

    for (auto& t : result.trajectories)
    {
        recomputeInverseScaleResiduals (t, frames, options);
        result.forensics.push_back (analyseTrajectory (t));
    }

    // The same labelling the sweep uses, from the same function. A second copy
    // of this rule is exactly how the two would come to disagree.
    result.labels = labelTrajectories (result.forensics, axis);

    result.valid = true;

    return result;
}

// ===========================================================================

int retrackSizeSweep (const std::string& rawCsvPath, const std::string& outDirectory)
{
    SizeTrackOptions options;

    const auto result = retrackSizeFromCsv (rawCsvPath, options);

    if (! result.valid)
    {
        std::ofstream fail (outDirectory + "/SIZE-RETRACK.txt", std::ios::trunc);

        if (fail.is_open())
            fail << "size re-track failed: " << result.error << "\n";

        return -1;
    }

    const auto& frames = result.frames;
    const auto anchorFrame = result.anchorFrame;
    const auto& anchorPeaks = result.anchorPeaks;
    const auto& tracked = result.trajectories;
    const auto& newForensics = result.forensics;
    const auto& labels = result.labels;
    const auto& forwardBackward = result.forwardBackward;
    const auto& anchorUp = result.anchorUp;
    const auto& anchorDown = result.anchorDown;

    int inverseConsistent = 0, nonInverseCandidate = 0, uncertain = 0;

    for (auto l : labels)
    {
        if (l == Label::inverseScaleConsistent)        ++inverseConsistent;
        else if (l == Label::nonInverseScaleCandidate) ++nonInverseCandidate;
        else                                           ++uncertain;
    }

    // --- the two comparison trackings ----------------------------------------

    auto legacy = trackLegacy (frames, anchorFrame, anchorPeaks);

    std::string recordedPath = rawCsvPath;

    {
        const auto slash = recordedPath.find_last_of ("/\\");

        recordedPath = (slash == std::string::npos ? std::string()
                                                   : recordedPath.substr (0, slash + 1))
                     + "size-sweep.csv";
    }

    auto recorded = readRecorded (recordedPath, frames);

    const auto forensicsFor = [&] (std::vector<SizeTrajectory>& set)
    {
        std::vector<SizeForensics> all;

        for (auto& t : set)
        {
            recomputeInverseScaleResiduals (t, frames, options);
            all.push_back (analyseTrajectory (t));
        }

        return all;
    };

    const auto legacyForensics = forensicsFor (legacy);
    const auto recordedForensics = forensicsFor (recorded);

    // --- output --------------------------------------------------------------

    int filesWritten = 0;

    {
        std::ofstream out (outDirectory + "/size-retrack-points.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);
            out << "mode_id,anchor_hz,frame,ui_size,size_scale,status,peak_index,"
                   "frequency_hz,modal_coordinate_q,predicted_hz,residual_hz,log_residual,"
                   "local_spacing_hz,residual_over_spacing,prominence_db,bandwidth_hz,q,"
                   "regret\n";

            for (size_t i = 0; i < tracked.size(); ++i)
            {
                for (const auto& p : tracked[i].points)
                {
                    out << tracked[i].id << ',' << tracked[i].anchorHz << ','
                        << p.frameIndex << ',' << p.sizeControl << ',' << p.sizeScale << ','
                        << describe (p.status) << ',' << p.peakIndex << ',';

                    const auto field = [&] (double v)
                    {
                        if (std::isfinite (v)) out << v;
                        out << ',';
                    };

                    field (p.frequencyHz);
                    field (p.modalCoordinate);
                    field (p.predictedHz);
                    field (p.residualHz);
                    field (p.logResidual);
                    field (p.localSpacingHz);
                    field (p.residualSpacings);
                    field (p.prominenceDb);
                    field (p.bandwidthHz);
                    field (p.qFactor);

                    if (std::isfinite (p.regret)) out << p.regret;

                    out << "\n";
                }
            }

            ++filesWritten;
        }
    }

    {
        std::ofstream out (outDirectory + "/size-retrack-trajectories.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);
            out << "mode_id,anchor_hz,matched,ambiguous,unmatched,out_of_band,"
                   "reversals,jumps_over_3_spacings,q_cv,fit_exponent,fit_r_squared,"
                   "fit_residual_rms,fit_points,fit_reliable,residual_rms_hz,"
                   "mean_residual_spacings,fb_comparable,fb_same,anchor_up_comparable,"
                   "anchor_up_same,anchor_down_comparable,anchor_down_same,label\n";

            for (size_t i = 0; i < tracked.size(); ++i)
            {
                const auto& f = newForensics[i];

                out << tracked[i].id << ',' << tracked[i].anchorHz << ','
                    << f.matchedPoints << ',' << f.ambiguousPoints << ','
                    << f.unmatchedPoints << ',' << f.outOfBandPoints << ','
                    << f.reversals << ',' << f.bigJumps << ',';

                if (f.coordinateCvMeasured) out << f.coordinateCv;
                out << ',' << f.exponent << ',' << f.rSquared << ',' << f.fitResidualRms
                    << ',' << f.fitPoints << ',' << (f.fitReliable ? 1 : 0) << ','
                    << f.residualRmsHz << ',' << f.meanAbsResidualSpacings << ','
                    << forwardBackward[i].comparable << ',' << forwardBackward[i].same << ','
                    << anchorUp[i].comparable << ',' << anchorUp[i].same << ','
                    << anchorDown[i].comparable << ',' << anchorDown[i].same << ','
                    << describe (labels[i]) << "\n";
            }

            ++filesWritten;
        }
    }

    {
        std::ostringstream log;

        log << "PHASE 6 / FU-1 - SIZE TRACKER RE-TRACK (OFFLINE)\n";
        log << "================================================\n\n";
        log << "Source: " << rawCsvPath << "\n";
        log << "No render. The raw peak lists are unchanged; only the tracking\n";
        log << "was redone. The production DSP was not touched.\n\n";

        log << "Size points and the scale each maps to:\n\n";

        for (size_t f = 0; f < frames.size(); ++f)
            log << "  " << padR ("UI " + fx (frames[f].sizeControl, 4), 12)
                << padR ("scale " + fx (frames[f].sizeScale, 4), 14)
                << padL (std::to_string (frames[f].peaks.size()) + " peaks", 12)
                << padL ("mean modal spacing " + fx (frames[f].meanModalSpacingHz, 3) + " Hz", 34)
                << (f == anchorFrame ? "   <- anchor" : "") << "\n";

        log << "\nAnchor: the sweep point nearest the shipping default of Size "
            << fx (defaultSizeControl, 2) << ".\n";
        log << "Anchor peaks: the " << anchorPeaks.size()
            << " strongest at that point, followed outwards in both\n";
        log << "directions independently. There is no min-to-max chain.\n\n";

        log << "Prediction: q = f * sizeScale is conserved under uniform scaling, so\n";
        log << "f_pred = q_last / scale_new. Residual is measured after that, against\n";
        log << "the median gap between detected peaks in a 200 Hz neighbourhood.\n\n";

        log << "-----------------------------------------------------------------\n";
        log << "  BEFORE AND AFTER\n";
        log << "-----------------------------------------------------------------\n\n";
        log << "  Same raw peak lists, same anchor peaks for replica and new. The\n";
        log << "  residual and the spacing are recomputed identically for all three,\n";
        log << "  so the columns are comparable.\n\n";

        log << "  " << padR ("tracking", 16) << padL ("modes", 6) << padL ("matched", 9)
            << padL ("ambig", 7) << padL ("gap", 7) << padL ("exit", 7)
            << padL ("reversal", 9) << padL (">3 sp", 9)
            << padL ("mean CV", 10) << padL ("med CV", 10) << padL ("CV<5%", 9) << "\n";

        if (! recorded.empty())
            writeSummary (log, "as-recorded", summarise (recordedForensics));

        writeSummary (log, "replica (old)", summarise (legacyForensics));
        writeSummary (log, "new (q-space)", summarise (newForensics));

        if (recorded.empty())
            log << "\n  size-sweep.csv was not found next to the raw CSV, so the\n"
                << "  as-recorded row is absent. The replica row is the old cost\n"
                << "  function over the same frames.\n";

        const auto newSummary = summarise (newForensics);
        const auto legacySummary = summarise (legacyForensics);

        log << "\n  reversal / >3 sp are counts of trajectories with at least one.\n";
        log << "  Totals: replica " << legacySummary.totalReversals << " reversals, "
            << legacySummary.totalBigJumps << " jumps; new "
            << newSummary.totalReversals << " reversals, "
            << newSummary.totalBigJumps << " jumps.\n";
        log << "\n  Read the new tracker's >3 sp column with that in mind: its gate\n";
        log << "  is three local spacings, so it cannot produce a residual above\n";
        log << "  three and a zero there is close to a tautology. Reversals and the\n";
        log << "  coordinate CV are not: nothing in the tracker forbids either, and\n";
        log << "  a chain that lost identity would show both. The mean residual of\n";
        log << "  " << fx (newSummary.meanResidualRmsHz, 3)
            << " Hz against a gate of several Hz is the number that says the\n";
        log << "  matches are not merely inside the gate but nowhere near it.\n";

        log << "\n-----------------------------------------------------------------\n";
        log << "  COVERAGE AND FIT, NEW TRACKER\n";
        log << "-----------------------------------------------------------------\n\n";

        const int totalPoints = static_cast<int> (tracked.size() * frames.size());

        log << "  trajectories            " << tracked.size() << "\n";
        log << "  points                  " << totalPoints << "\n";
        log << "  matched                 " << newSummary.matchedPoints
            << "  (" << fx (100.0 * newSummary.matchedPoints / std::max (1, totalPoints), 1)
            << "%)\n";
        log << "  ambiguous               " << newSummary.ambiguousPoints << "\n";
        log << "  gap (solver refused)    " << newSummary.unmatchedPoints << "\n";
        log << "  boundary exit           " << newSummary.outOfBandPoints << "\n";
        log << "  mean residual RMS       " << fx (newSummary.meanResidualRmsHz, 4) << " Hz\n";
        log << "  fits reliable (R2>0.90) " << newSummary.fitReliable << " / "
            << tracked.size() << "   (threshold unchanged)\n";
        log << "  mean exponent           " << fx (newSummary.meanExponent, 4) << "\n";
        log << "  mean R-squared          " << fx (newSummary.meanRSquared, 4) << "\n";

        // --- agreement -------------------------------------------------------

        int fbComparable = 0, fbSame = 0, fbFull = 0, fbNoOverlap = 0;
        int upComparable = 0, upSame = 0, downComparable = 0, downSame = 0;
        int anchorFull = 0, anchorNoOverlap = 0;

        for (size_t i = 0; i < tracked.size(); ++i)
        {
            fbComparable += forwardBackward[i].comparable;
            fbSame += forwardBackward[i].same;

            if (forwardBackward[i].comparable == 0)  ++fbNoOverlap;
            else if (forwardBackward[i].consistent()) ++fbFull;

            upComparable += anchorUp[i].comparable;
            upSame += anchorUp[i].same;
            downComparable += anchorDown[i].comparable;
            downSame += anchorDown[i].same;

            const int overlap = anchorUp[i].comparable + anchorDown[i].comparable;

            if (overlap == 0)
                ++anchorNoOverlap;
            else if (anchorUp[i].consistent() && anchorDown[i].consistent())
                ++anchorFull;
        }

        log << "\n-----------------------------------------------------------------\n";
        log << "  AGREEMENT\n";
        log << "-----------------------------------------------------------------\n\n";
        log << "  forward vs backward: each trajectory re-tracked from its own lowest\n";
        log << "  Size walking up, and from its own highest walking down. Compared on\n";
        log << "  the frames where both matched.\n\n";
        log << "    points agreeing       " << fbSame << " / " << fbComparable
            << "  (" << fx (100.0 * fbSame / std::max (1, fbComparable), 2) << "%)\n";
        log << "    trajectories agreeing " << fbFull << " / "
            << (static_cast<int> (tracked.size()) - fbNoOverlap)
            << " that overlap at all\n";
        log << "    no overlap to compare " << fbNoOverlap << "\n";

        log << "\n  default anchor vs those two chains, on their own side of the anchor:\n\n";
        log << "    up   points agreeing  " << upSame << " / " << upComparable
            << "  (" << fx (100.0 * upSame / std::max (1, upComparable), 2) << "%)\n";
        log << "    down points agreeing  " << downSame << " / " << downComparable
            << "  (" << fx (100.0 * downSame / std::max (1, downComparable), 2) << "%)\n";
        log << "    trajectories agreeing " << anchorFull << " / "
            << (static_cast<int> (tracked.size()) - anchorNoOverlap)
            << " that overlap at all\n";
        log << "    no overlap to compare " << anchorNoOverlap << "\n";
        log << "\n  An empty overlap is not disagreement and is not counted as one:\n";
        log << "  a trajectory whose matches all sit on one side of the anchor has\n";
        log << "  nothing there for the opposite chain to contradict. It still has\n";
        log << "  to be corroborated somewhere - two agreeing points across the\n";
        log << "  three comparisons - before it can be called identity-consistent.\n";

        log << "\n-----------------------------------------------------------------\n";
        log << "  MEASUREMENT LABELS\n";
        log << "-----------------------------------------------------------------\n\n";
        log << "  Identity first, exponent second. A trajectory is identity-consistent\n";
        log << "  only when it has four or more matched points, no reversal, no\n";
        log << "  residual above three local spacings, no frame where an\n";
        log << "  independently anchored chain chose a different peak, and at least\n";
        log << "  two frames where one did choose the same. Only then is the\n";
        log << "  modal coordinate read, and a CV below "
            << fx (100.0 * inverseScaleCvLimit, 0) << "% called inverse-scale\n";
        log << "  consistent. A low R-squared on its own is never called a tracker\n";
        log << "  error, and a high one is never called a mode.\n\n";

        log << "  " << padR (describeLabel (Label::inverseScaleConsistent), 52)
            << padL (std::to_string (inverseConsistent), 5) << "\n";
        log << "  " << padR (describeLabel (Label::nonInverseScaleCandidate), 52)
            << padL (std::to_string (nonInverseCandidate), 5) << "\n";
        log << "  " << padR (describeLabel (Label::identityUncertain), 52)
            << padL (std::to_string (uncertain), 5) << "\n";

        log << "\n-----------------------------------------------------------------\n";
        log << "  PER TRAJECTORY\n";
        log << "-----------------------------------------------------------------\n\n";
        log << "  " << padR ("id", 4) << padL ("anchor Hz", 11) << padL ("matched", 8)
            << padL ("amb", 5) << padL ("gap", 5) << padL ("exit", 5)
            << padL ("rev", 5) << padL (">3sp", 6) << padL ("q CV", 9)
            << padL ("exponent", 10) << padL ("R2", 8) << padL ("rmsHz", 9)
            << "  label\n";

        for (size_t i = 0; i < tracked.size(); ++i)
        {
            const auto& f = newForensics[i];

            log << "  " << padR (std::to_string (tracked[i].id), 4)
                << padL (fx (tracked[i].anchorHz, 2), 11)
                << padL (std::to_string (f.matchedPoints), 8)
                << padL (std::to_string (f.ambiguousPoints), 5)
                << padL (std::to_string (f.unmatchedPoints), 5)
                << padL (std::to_string (f.outOfBandPoints), 5)
                << padL (std::to_string (f.reversals), 5)
                << padL (std::to_string (f.bigJumps), 6)
                << padL (f.coordinateCvMeasured ? fx (100.0 * f.coordinateCv, 3) + "%" : "-", 9)
                << padL (fx (f.exponent, 4), 10)
                << padL (fx (f.rSquared, 4), 8)
                << padL (fx (f.residualRmsHz, 4), 9)
                << "  " << describe (labels[i]) << "\n";
        }

        log << "\nWritten: size-retrack-points.csv, size-retrack-trajectories.csv\n";

        std::ofstream out (outDirectory + "/SIZE-RETRACK.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << log.str();
            ++filesWritten;
        }
    }

    return filesWritten;
}

} // namespace avtest
