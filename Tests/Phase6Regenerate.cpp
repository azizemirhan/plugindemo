#include "SizeTrack.h"

#include "ModalMap.h"

#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/*
    Phase 6 / FU-1: regenerating the analysis layer from raw data already on
    disk.

    No render happens here and none is needed. The Size tracking was the part
    that was wrong; the peak lists it ran on were always correct, and they are
    read back unchanged. What is rebuilt is everything downstream of the
    tracking: the Size trajectories, the Size columns of the mode table, and the
    classification those columns feed.

    Everything that does not depend on Size tracking is carried across as it
    was measured - modal decay, stereo, the Mod Depth local response, the decay
    sweep. Recomputing them would mean re-rendering, and there is nothing wrong
    with them to fix.

    The classifier is not touched. Its thresholds, its class definitions and its
    treatment of an unmeasurable per-mode decay are the ones already agreed. It
    is simply given Size evidence that is no longer wrong, and whatever it then
    says is the answer. To prove that is all that happened, the run first feeds
    the classifier the old Size columns and checks it reproduces the old classes
    exactly; only a run that passes that check reports a class change as
    meaningful.
*/
namespace avtest
{

namespace
{
    using av::dsp::mapping::sizeScaleFor;

    constexpr double notMeasured() { return std::numeric_limits<double>::quiet_NaN(); }

    /** The sweep's reference setting. Not one of the Size points. */
    constexpr double referenceSizeControl = 0.60;

    /** From the sweep: movement is only movement above three times this. */
    constexpr double variabilityFloorHz = 0.1278;

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
            if (c == '"')             { quoted = ! quoted; continue; }
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

        try         { return std::stod (s); }
        catch (...) { return notMeasured(); }
    }

    [[nodiscard]] std::string quotedField (const std::string& s) { return "\"" + s + "\""; }

    /** A CSV read into a header map and a list of row field vectors. */
    struct Table
    {
        std::vector<std::string> header;
        std::map<std::string, size_t> column;
        std::vector<std::vector<std::string>> rows;

        [[nodiscard]] bool has (const std::string& name) const
        {
            return column.find (name) != column.end();
        }

        [[nodiscard]] std::string text (size_t row, const std::string& name) const
        {
            const auto it = column.find (name);

            if (it == column.end() || row >= rows.size() || it->second >= rows[row].size())
                return {};

            return rows[row][it->second];
        }

        [[nodiscard]] double number (size_t row, const std::string& name) const
        {
            return toDouble (text (row, name));
        }
    };

    [[nodiscard]] bool readTable (const std::string& path, Table& table)
    {
        std::ifstream in (path);

        if (! in.is_open())
            return false;

        std::string line;

        if (! std::getline (in, line))
            return false;

        table.header = splitCsv (line);

        for (size_t i = 0; i < table.header.size(); ++i)
            table.column[table.header[i]] = i;

        while (std::getline (in, line))
            if (! line.empty())
                table.rows.push_back (splitCsv (line));

        return true;
    }

    /** Byte-for-byte copy, so a carried-across file is provably the same file. */
    [[nodiscard]] bool copyFile (const std::string& from, const std::string& to)
    {
        std::ifstream in (from, std::ios::binary);

        if (! in.is_open())
            return false;

        std::ofstream out (to, std::ios::binary | std::ios::trunc);

        if (! out.is_open())
            return false;

        out << in.rdbuf();

        return true;
    }

    /** The reference render's 60 detailed modes, from modal-map-raw.csv. */
    struct Reference
    {
        bool valid = false;

        SizeFrame frame;                 /**< Peaks in frequency order. */
        std::vector<int> anchorPeaks;    /**< mode_id -> index into frame.peaks. */
        std::vector<double> frequency;   /**< By mode_id. */
        double broadbandRt60 = 0.0;
        double meanModalSpacingHz = 0.0;
    };

    [[nodiscard]] Reference readReference (const std::string& path)
    {
        Reference r;

        Table t;

        if (! readTable (path, t))
            return r;

        if (! (t.has ("setting_index") && t.has ("frequency_hz") && t.has ("peak_index")))
            return r;

        struct Entry { int modeId; double hz, prominence, bandwidth, q; bool resolved; };

        std::vector<Entry> entries;

        // Which rows are the reference's.
        //
        // Not by setting_index. Sweeps up to and including v3 wrote that column
        // as zero for both the reference and the Mod Depth map, because neither
        // frame had its axis value set - so "setting_index == 0" selects 300
        // rows from two different settings whose peak indices restart halfway
        // through, and the second block silently overwrites the first. The
        // sweep now labels them properly; this reads either shape by using the
        // label, and falls back to the point where peak_index restarts.
        std::vector<size_t> referenceRows;

        {
            const bool haveLabel = t.has ("label");
            int previousIndex = -1;

            for (size_t i = 0; i < t.rows.size(); ++i)
            {
                const int index = static_cast<int> (t.number (i, "peak_index"));

                if (haveLabel)
                {
                    if (t.text (i, "label").rfind ("reference", 0) != 0)
                        continue;
                }
                else if (index <= previousIndex)
                {
                    break;   // the indices restarted: a different setting
                }

                previousIndex = index;
                referenceRows.push_back (i);
            }
        }

        for (size_t i : referenceRows)
        {
            Entry e;

            e.modeId = static_cast<int> (t.number (i, "peak_index"));
            e.hz = t.number (i, "frequency_hz");
            e.prominence = t.number (i, "prominence_db");
            e.bandwidth = t.number (i, "bandwidth_hz");
            e.q = t.number (i, "q");
            e.resolved = t.number (i, "width_resolved") > 0.5;

            if (! std::isfinite (e.hz))
                continue;

            entries.push_back (e);

            r.broadbandRt60 = t.number (i, "broadband_rt60_s");
            r.meanModalSpacingHz = t.number (i, "mean_modal_spacing_hz");
        }

        if (entries.empty())
            return r;

        // The mode table's row order is the reference's peak order, which is by
        // prominence. The tracker wants frequency order, so both are kept and
        // the mapping between them is explicit rather than assumed.
        auto byFrequency = entries;

        std::sort (byFrequency.begin(), byFrequency.end(),
                   [] (const Entry& a, const Entry& b) { return a.hz < b.hz; });

        r.frame.sizeControl = referenceSizeControl;
        r.frame.sizeScale = sizeScaleFor (referenceSizeControl);
        r.frame.meanModalSpacingHz = r.meanModalSpacingHz;

        for (const auto& e : byFrequency)
        {
            SizePeak p;

            p.frequencyHz = e.hz;
            p.prominenceDb = e.prominence;
            p.bandwidthHz = e.bandwidth;
            p.qFactor = e.q;
            p.widthResolved = e.resolved;

            r.frame.peaks.push_back (p);
        }

        int highest = 0;

        for (const auto& e : entries)
            highest = std::max (highest, e.modeId);

        r.anchorPeaks.assign (static_cast<size_t> (highest) + 1, -1);
        r.frequency.assign (static_cast<size_t> (highest) + 1, notMeasured());

        for (size_t i = 0; i < byFrequency.size(); ++i)
        {
            const auto id = static_cast<size_t> (byFrequency[i].modeId);

            if (id < r.anchorPeaks.size())
            {
                r.anchorPeaks[id] = static_cast<int> (i);
                r.frequency[id] = byFrequency[i].hz;
            }
        }

        r.valid = true;

        return r;
    }
}

// ===========================================================================

int regeneratePhase6Outputs (const std::string& sourceDirectory,
                             const std::string& outDirectory)
{
    const SizeTrackOptions options;   // untouched

    std::ostringstream log;

    const auto fail = [&] (const std::string& reason)
    {
        std::ofstream out (outDirectory + "/REGENERATION.txt", std::ios::trunc);

        if (out.is_open())
            out << "Phase 6 regeneration failed: " << reason << "\n";

        return -1;
    };

    // --- inputs ---------------------------------------------------------------

    std::string error;

    const auto frames = readSizeFramesCsv (sourceDirectory + "/size-sweep-raw.csv", error);

    if (frames.size() < 3)
        return fail (error.empty() ? "fewer than three Size points" : error);

    const auto reference = readReference (sourceDirectory + "/modal-map-raw.csv");

    if (! reference.valid)
        return fail ("could not read the reference modes from modal-map-raw.csv");

    Table modeTable, localResponse;

    if (! readTable (sourceDirectory + "/mode-table.csv", modeTable))
        return fail ("could not read mode-table.csv");

    if (! readTable (sourceDirectory + "/local-response.csv", localResponse))
        return fail ("could not read local-response.csv");

    const size_t modeCount = modeTable.rows.size();

    if (modeCount == 0 || reference.anchorPeaks.size() < modeCount)
        return fail ("mode table and reference peak list do not correspond");

    // Every regenerated row keeps its identity from the mode table, so the two
    // have to be the same modes in the same order. Checked rather than assumed:
    // the first version of this reader picked up the wrong block of
    // modal-map-raw.csv and produced a table whose Size evidence belonged to
    // different modes than its frequencies, and nothing else would have said so.
    for (size_t i = 0; i < modeCount; ++i)
    {
        const double fromTable = modeTable.number (i, "frequency_hz");
        const double fromReference = reference.frequency[i];

        if (! (std::isfinite (fromTable) && std::isfinite (fromReference))
            || std::abs (fromTable - fromReference) > 1.0e-3)
        {
            std::ostringstream why;

            why << "mode " << i << ": mode-table says " << fx (fromTable, 4)
                << " Hz and modal-map-raw says " << fx (fromReference, 4)
                << " Hz. The two files are not the same modes in the same order.";

            return fail (why.str());
        }
    }

    // --- the Size axis, through the same function the sweep calls -------------

    std::vector<int> anchorPeaks;

    for (size_t i = 0; i < modeCount; ++i)
        anchorPeaks.push_back (reference.anchorPeaks[i]);

    auto axis = trackSizeAxis (frames, &reference.frame, anchorPeaks,
                               referenceSizeControl, options);

    std::vector<SizeForensics> forensics;
    std::vector<SizeEvidence> evidence;

    for (auto& t : axis.trajectories)
    {
        recomputeInverseScaleResiduals (t, frames, options);

        forensics.push_back (analyseTrajectory (t));
        evidence.push_back (evidenceFor (t));
    }

    const auto labels = labelTrajectories (forensics, axis);

    // --- Size outputs ---------------------------------------------------------

    SizeOutputs outputs;

    for (const auto& f : frames)
        outputs.sizeControls.push_back (f.sizeControl);

    for (size_t i = 0; i < modeCount; ++i)
        outputs.referenceHz.push_back (reference.frequency[i]);

    outputs.trajectories = &axis.trajectories;
    outputs.forensics = &forensics;
    outputs.evidence = &evidence;
    outputs.labels = &labels;
    outputs.axis = &axis;

    int filesWritten = 0;

    if (writeSizeSweepCsv (outDirectory, outputs))      ++filesWritten;
    if (writeSizeTrajectoryCsv (outDirectory, outputs)) ++filesWritten;
    if (writeSizePointsCsv (outDirectory, outputs))     ++filesWritten;

    // --- classification --------------------------------------------------------

    // The modulation half of the rule, read back exactly as the sweep computed
    // it. Nothing here is recomputed from audio.
    struct ModulationEvidence { bool measured = false; bool redistributes = false; };

    std::vector<ModulationEvidence> modulation (modeCount);

    for (size_t i = 0; i < modeCount && i < localResponse.rows.size(); ++i)
    {
        ModulationEvidence m;

        m.measured = localResponse.number (i, "measured") > 0.5;

        if (m.measured)
        {
            const double spreadRatio = localResponse.number (i, "spread_ratio");
            const double concentrationRatio = localResponse.number (i, "concentration_ratio");

            m.redistributes = std::isfinite (spreadRatio) && std::isfinite (concentrationRatio)
                           && spreadRatio > 1.5 && concentrationRatio < 0.7;
        }

        const double excursion = modeTable.number (i, "mod_excursion_hz");

        if (std::isfinite (excursion) && excursion > 3.0 * variabilityFloorHz)
            m.redistributes = true;

        modulation[i] = m;
    }

    // The control. Feed the classifier the Size columns the old run wrote; it
    // has to reproduce the old classes. If it does not, the reclassification
    // below is measuring a change in the classifier and not a change in the
    // evidence, and nothing about it can be believed.
    int controlMatches = 0, controlTested = 0;

    for (size_t i = 0; i < modeCount; ++i)
    {
        const double oldExponent = modeTable.number (i, "size_fit_exponent");
        const bool oldReliable = modeTable.number (i, "size_fit_reliable") > 0.5;
        const std::string oldClass = modeTable.text (i, "class");

        if (oldClass.empty())
            continue;

        ++controlTested;

        const auto reproduced = classifySweepMode (oldExponent, oldReliable,
                                                   modulation[i].measured,
                                                   modulation[i].redistributes);

        if (std::string (describe (reproduced)) == oldClass)
            ++controlMatches;
    }

    const bool controlPassed = controlTested > 0 && controlMatches == controlTested;

    // The regeneration proper: the same rule, the corrected Size evidence.
    std::vector<ModeClass> newClass (modeCount, ModeClass::ambiguousUnresolved);
    std::vector<std::string> oldClassText (modeCount);

    for (size_t i = 0; i < modeCount; ++i)
    {
        oldClassText[i] = modeTable.text (i, "class");

        newClass[i] = classifySweepMode (forensics[i].exponent, forensics[i].fitReliable,
                                         modulation[i].measured,
                                         modulation[i].redistributes);
    }

    // --- the regenerated mode table --------------------------------------------

    {
        std::ofstream out (outDirectory + "/mode-table.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << std::setprecision (10);

            // Original columns, in their original order and meaning, then the
            // Size evidence that was never in this file because the old
            // trajectories could not support it.
            for (size_t c = 0; c < modeTable.header.size(); ++c)
                out << (c == 0 ? "" : ",") << modeTable.header[c];

            out << ",size_matched_points,size_ambiguous_points,size_gap_points,"
                   "size_boundary_exit_points,size_q_cv,size_residual_rms_hz,"
                   "size_mean_residual_spacings,size_reversals,size_jumps_over_3_spacings,"
                   "size_fb_comparable,size_fb_same,size_anchor_up_comparable,"
                   "size_anchor_up_same,size_anchor_down_comparable,size_anchor_down_same,"
                   "size_identity_label,size_evidence,size_confirmed_points,"
                   "size_weak_points,size_suspect_points,class_previous\n";

            // Columns whose value the sweep wrote quoted.
            const auto isQuoted = [] (const std::string& name)
            {
                return name == "decay_status" || name == "decay_reason"
                    || name == "class" || name == "ambiguity_reason";
            };

            for (size_t i = 0; i < modeCount; ++i)
            {
                const auto& f = forensics[i];

                for (size_t c = 0; c < modeTable.header.size(); ++c)
                {
                    const auto& name = modeTable.header[c];
                    const std::string original = c < modeTable.rows[i].size()
                                               ? modeTable.rows[i][c] : std::string();

                    std::string value = original;

                    // The five columns the Size tracking owns, replaced. Every
                    // other column is the measurement as it was made.
                    if (name == "size_fit_exponent")           value = std::to_string (f.exponent);
                    else if (name == "size_fit_r_squared")     value = std::to_string (f.rSquared);
                    else if (name == "size_fit_residual_rms")  value = std::to_string (f.fitResidualRms);
                    else if (name == "size_fit_points")        value = std::to_string (f.fitPoints);
                    else if (name == "size_fit_reliable")      value = f.fitReliable ? "1" : "0";
                    else if (name == "class")                  value = describe (newClass[i]);

                    out << (c == 0 ? "" : ",") << (isQuoted (name) ? quotedField (value) : value);
                }

                out << ',' << f.matchedPoints << ',' << f.ambiguousPoints
                    << ',' << f.unmatchedPoints << ',' << f.outOfBandPoints << ',';

                if (f.coordinateCvMeasured) out << f.coordinateCv;

                out << ',' << f.residualRmsHz << ',' << f.meanAbsResidualSpacings
                    << ',' << f.reversals << ',' << f.bigJumps
                    << ',' << axis.forwardBackward[i].comparable
                    << ',' << axis.forwardBackward[i].same
                    << ',' << axis.anchorUp[i].comparable << ',' << axis.anchorUp[i].same
                    << ',' << axis.anchorDown[i].comparable << ',' << axis.anchorDown[i].same
                    << ',' << describe (labels[i])
                    << ',' << describe (evidence[i].worst)
                    << ',' << evidence[i].confirmedPoints
                    << ',' << evidence[i].weakPoints
                    << ',' << evidence[i].suspectPoints
                    << ',' << quotedField (oldClassText[i]) << "\n";
            }

            ++filesWritten;
        }
    }

    // --- carried across, byte for byte -----------------------------------------

    const char* carried[] = { "size-sweep-raw.csv", "decay-sweep-raw.csv", "decay-sweep.csv",
                              "modal-map-raw.csv", "local-response.csv", "moddepth-ab.csv",
                              "RESOLUTION.txt" };

    std::vector<std::string> copied, missing;

    for (const char* name : carried)
    {
        if (copyFile (sourceDirectory + "/" + name, outDirectory + "/" + name))
        {
            copied.emplace_back (name);
            ++filesWritten;
        }
        else
        {
            missing.emplace_back (name);
        }
    }

    // --- report ------------------------------------------------------------------

    log << "PHASE 6 / FU-1 - REGENERATED ANALYSIS (v4)\n";
    log << "==========================================\n\n";
    log << "Source: " << sourceDirectory << "\n";
    log << "Renders performed: 0. The peak lists are read back unchanged and only\n";
    log << "the Size tracking and what depends on it were recomputed. The\n";
    log << "production DSP was not touched and no sweep was run.\n\n";

    log << "Carried across byte for byte:\n";

    for (const auto& name : copied)
        log << "  " << name << "\n";

    if (! missing.empty())
    {
        log << "\nNot found in the source directory:\n";

        for (const auto& name : missing)
            log << "  " << name << "\n";
    }

    log << "\nRegenerated:\n";
    log << "  size-sweep.csv          Size trajectories, with the forensic columns\n";
    log << "  size-trajectories.csv   one row per mode\n";
    log << "  size-points.csv         one row per mode per Size point\n";
    log << "  mode-table.csv          Size columns and class recomputed, the rest carried\n\n";

    log << "Anchor: the reference render at Size " << fx (referenceSizeControl, 2)
        << ", scale " << fx (reference.frame.sizeScale, 4) << ", carried into the\n";
    log << "walk at its own scale. Chains part at sweep frame " << axis.anchorFrame
        << " (UI " << fx (frames[axis.anchorFrame].sizeControl, 4) << ").\n";

    // --- coverage ------------------------------------------------------------------

    int matched = 0, ambiguous = 0, gap = 0, exit = 0;
    int reversals = 0, jumps = 0, reliable = 0, cvMeasured = 0, cvBelow = 0;
    double cvSum = 0.0, residualSum = 0.0;
    std::vector<double> cvs;

    for (const auto& f : forensics)
    {
        matched += f.matchedPoints;
        ambiguous += f.ambiguousPoints;
        gap += f.unmatchedPoints;
        exit += f.outOfBandPoints;
        reversals += f.reversals;
        jumps += f.bigJumps;

        if (f.fitReliable) ++reliable;

        if (f.coordinateCvMeasured)
        {
            ++cvMeasured;
            cvSum += f.coordinateCv;
            cvs.push_back (f.coordinateCv);

            if (f.coordinateCv < inverseScaleCvLimit)
                ++cvBelow;
        }

        residualSum += f.residualRmsHz;
    }

    std::sort (cvs.begin(), cvs.end());

    log << "\n-----------------------------------------------------------------\n";
    log << "  SIZE TRACKING\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  modes                    " << modeCount << "\n";
    log << "  points                   " << modeCount * frames.size() << "\n";
    log << "  matched                  " << matched << "\n";
    log << "  ambiguous                " << ambiguous << "\n";
    log << "  gap                      " << gap << "\n";
    log << "  boundary exit            " << exit << "\n";
    log << "  reversals                " << reversals << "\n";
    log << "  residual > 3 spacings    " << jumps << "\n";
    log << "  mean residual RMS        " << fx (residualSum / std::max<size_t> (1, modeCount), 4)
        << " Hz\n";
    log << "  q CV, mean               "
        << fx (100.0 * cvSum / std::max (1, cvMeasured), 3) << "%\n";
    log << "  q CV, median             "
        << fx (cvs.empty() ? notMeasured() : 100.0 * cvs[cvs.size() / 2], 3) << "%\n";
    log << "  q CV below "
        << fx (100.0 * inverseScaleCvLimit, 0) << "%           " << cvBelow << " / " << cvMeasured
        << "\n";
    log << "  fits reliable (R2>0.90)  " << reliable << " / " << modeCount
        << "   (threshold unchanged)\n";

    int inverseConsistent = 0, nonInverse = 0, uncertain = 0;
    int evidenceConfirmed = 0, evidenceWeak = 0, evidenceSuspect = 0, evidenceNone = 0;

    for (size_t i = 0; i < modeCount; ++i)
    {
        switch (labels[i])
        {
            case SizeLabel::inverseScaleConsistent:   ++inverseConsistent; break;
            case SizeLabel::nonInverseScaleCandidate: ++nonInverse;        break;
            case SizeLabel::identityUncertain:        ++uncertain;         break;
        }

        switch (evidence[i].worst)
        {
            case PeakEvidence::confirmed: ++evidenceConfirmed; break;
            case PeakEvidence::weak:      ++evidenceWeak;      break;
            case PeakEvidence::suspect:   ++evidenceSuspect;   break;
            case PeakEvidence::absent:    ++evidenceNone;      break;
        }
    }

    log << "\n  identity-consistent / inverse-scale-consistent   "
        << padL (std::to_string (inverseConsistent), 4) << "\n";
    log << "  identity-consistent / non-inverse-scale candidate "
        << padL (std::to_string (nonInverse), 4) << "\n";
    log << "  identity-uncertain                                "
        << padL (std::to_string (uncertain), 4) << "\n";

    log << "\n  Peak evidence is a separate axis from identity. A trajectory can be\n";
    log << "  tracked with confidence through a Size point where the peak itself is\n";
    log << "  thin; the two statements are reported separately and neither\n";
    log << "  overrides the other.\n\n";
    log << "  worst evidence any matched point reached:\n";
    log << "    confirmed              " << evidenceConfirmed << "\n";
    log << "    weak-evidence          " << evidenceWeak << "\n";
    log << "    suspect                " << evidenceSuspect << "\n";
    log << "    no matched point       " << evidenceNone << "\n";

    // --- agreement -------------------------------------------------------------------

    int fbComparable = 0, fbSame = 0, upComparable = 0, upSame = 0;
    int downComparable = 0, downSame = 0, fbFull = 0, anchorFull = 0;
    int fbNoOverlap = 0, anchorNoOverlap = 0;

    for (size_t i = 0; i < modeCount; ++i)
    {
        fbComparable += axis.forwardBackward[i].comparable;
        fbSame += axis.forwardBackward[i].same;
        upComparable += axis.anchorUp[i].comparable;
        upSame += axis.anchorUp[i].same;
        downComparable += axis.anchorDown[i].comparable;
        downSame += axis.anchorDown[i].same;

        if (axis.forwardBackward[i].comparable == 0)       ++fbNoOverlap;
        else if (axis.forwardBackward[i].consistent())     ++fbFull;

        if (axis.anchorUp[i].comparable + axis.anchorDown[i].comparable == 0)
            ++anchorNoOverlap;
        else if (axis.anchorUp[i].consistent() && axis.anchorDown[i].consistent())
            ++anchorFull;
    }

    log << "\n-----------------------------------------------------------------\n";
    log << "  AGREEMENT\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  forward vs backward   " << fbSame << " / " << fbComparable << " points  ("
        << fx (100.0 * fbSame / std::max (1, fbComparable), 2) << "%),  "
        << fbFull << " of " << (static_cast<int> (modeCount) - fbNoOverlap)
        << " trajectories that overlap\n";
    log << "  anchor, up            " << upSame << " / " << upComparable << " points  ("
        << fx (100.0 * upSame / std::max (1, upComparable), 2) << "%)\n";
    log << "  anchor, down          " << downSame << " / " << downComparable << " points  ("
        << fx (100.0 * downSame / std::max (1, downComparable), 2) << "%)\n";
    log << "  anchor, both          " << anchorFull << " of "
        << (static_cast<int> (modeCount) - anchorNoOverlap)
        << " trajectories that overlap\n";

    // --- classification ----------------------------------------------------------------

    log << "\n-----------------------------------------------------------------\n";
    log << "  CLASSIFICATION\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  The rule is unchanged. No threshold moved, no class was redefined and\n";
    log << "  nothing was pushed into A or B. The classifier was given Size evidence\n";
    log << "  that is no longer the product of a broken tracker, and this is what it\n";
    log << "  then said.\n\n";
    log << "  Control - the same classifier fed the OLD Size columns reproduces the\n";
    log << "  old classes: " << controlMatches << " / " << controlTested
        << (controlPassed ? "  (passed)" : "  (FAILED - the change below is not"
                                           " attributable to the evidence)") << "\n";

    log << "\n  " << padR ("class", 46) << padL ("before", 8) << padL ("after", 8) << "\n";

    const ModeClass classes[] = { ModeClass::sizeTrackingModulationMoving,
                                  ModeClass::sizeTrackingModulationResistant,
                                  ModeClass::sizeInsensitiveFixedOrMixed,
                                  ModeClass::ambiguousUnresolved };

    const char* letters[] = { "A", "B", "C", "D" };

    for (size_t c = 0; c < 4; ++c)
    {
        int before = 0, after = 0;

        for (size_t i = 0; i < modeCount; ++i)
        {
            if (oldClassText[i] == describe (classes[c])) ++before;
            if (newClass[i] == classes[c])                ++after;
        }

        log << "  " << padR (std::string (letters[c]) + "  " + describe (classes[c]), 46)
            << padL (std::to_string (before), 8) << padL (std::to_string (after), 8) << "\n";
    }

    int changed = 0;

    for (size_t i = 0; i < modeCount; ++i)
        if (oldClassText[i] != describe (newClass[i]))
            ++changed;

    log << "\n  modes whose class changed: " << changed << " of " << modeCount << "\n";

    if (changed > 0)
    {
        log << "\n  " << padR ("mode", 6) << padL ("ref Hz", 10) << padL ("exp before", 12)
            << padL ("exp after", 11) << padL ("R2 after", 10) << padL ("pts", 5)
            << "  before -> after\n";

        for (size_t i = 0; i < modeCount; ++i)
        {
            if (oldClassText[i] == describe (newClass[i]))
                continue;

            log << "  " << padR (std::to_string (i), 6)
                << padL (fx (reference.frequency[i], 2), 10)
                << padL (fx (modeTable.number (i, "size_fit_exponent"), 4), 12)
                << padL (fx (forensics[i].exponent, 4), 11)
                << padL (fx (forensics[i].rSquared, 4), 10)
                << padL (std::to_string (forensics[i].fitPoints), 5)
                << "  " << oldClassText[i] << " -> " << describe (newClass[i]) << "\n";
        }
    }

    // --- what was carried, and its state ------------------------------------------------

    log << "\n-----------------------------------------------------------------\n";
    log << "  CARRIED ACROSS UNCHANGED\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  Neither was re-measured and neither needed to be. Reported so that the\n";
    log << "  regenerated table is read with its limits visible.\n\n";

    {
        std::map<std::string, int> decayStatus;

        for (size_t i = 0; i < modeCount; ++i)
        {
            auto status = modeTable.text (i, "decay_status");

            if (status.empty())
                status = "(absent)";

            ++decayStatus[status];
        }

        log << "  per-mode modal decay status:\n";

        for (const auto& entry : decayStatus)
            log << "    " << padR (entry.first, 22) << padL (std::to_string (entry.second), 4)
                << "\n";

        log << "\n  An infeasible per-mode decay is not on its own a reason for class D,\n";
        log << "  and is not treated as one here: the rule reads the Size fit and the\n";
        log << "  modulation evidence, not the decay.\n";
    }

    {
        int measured = 0;

        for (size_t i = 0; i < modeCount && i < localResponse.rows.size(); ++i)
            if (localResponse.number (i, "measured") > 0.5)
                ++measured;

        log << "\n  Mod Depth local response measured: " << measured << " / " << modeCount << "\n";

        int modMatchedCount = 0;

        for (size_t i = 0; i < modeCount; ++i)
            if (std::isfinite (modeTable.number (i, "mod_excursion_hz")))
                ++modMatchedCount;

        log << "  Mod Depth 1:1 peak identity:       " << modMatchedCount << " / " << modeCount
            << "   (unchanged; the tracker for that axis was not touched)\n";
    }

    log << "\n-----------------------------------------------------------------\n";
    log << "  PER MODE\n";
    log << "-----------------------------------------------------------------\n\n";
    log << "  " << padR ("id", 4) << padL ("ref Hz", 10) << padL ("match", 7)
        << padL ("amb", 5) << padL ("gap", 5) << padL ("exit", 6) << padL ("q CV", 9)
        << padL ("exponent", 10) << padL ("R2", 8) << padL ("cls", 5)
        << "  identity / evidence\n";

    for (size_t i = 0; i < modeCount; ++i)
    {
        const auto& f = forensics[i];

        const char* letter = "D";

        for (size_t c = 0; c < 4; ++c)
            if (newClass[i] == classes[c])
                letter = letters[c];

        log << "  " << padR (std::to_string (i), 4)
            << padL (fx (reference.frequency[i], 2), 10)
            << padL (std::to_string (f.matchedPoints), 7)
            << padL (std::to_string (f.ambiguousPoints), 5)
            << padL (std::to_string (f.unmatchedPoints), 5)
            << padL (std::to_string (f.outOfBandPoints), 6)
            << padL (f.coordinateCvMeasured ? fx (100.0 * f.coordinateCv, 3) + "%" : "-", 9)
            << padL (fx (f.exponent, 4), 10)
            << padL (fx (f.rSquared, 4), 8)
            << padL (letter, 5)
            << "  " << describe (labels[i]) << " / " << describe (evidence[i].worst) << "\n";
    }

    {
        std::ofstream out (outDirectory + "/REGENERATION.txt", std::ios::trunc);

        if (out.is_open())
        {
            out << log.str();
            ++filesWritten;
        }
    }

    return filesWritten;
}

} // namespace avtest
