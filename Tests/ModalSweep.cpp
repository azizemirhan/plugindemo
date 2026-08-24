#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModalMap.h"
#include "SizeTrack.h"

#include "DSP/Denormal.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    [[nodiscard]] double toDbLocal (double linear) noexcept
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear) : -300.0;
    }

    /**
        Decay, stereo energy and correlation for one peak.

        Zero-phase band-limiting around the peak, then the same Schroeder
        integration the broadband figure uses. An IIR filter would add its own
        decay to the answer, which is why bandLimited is FFT-based.
    */
    /**
        Decay and stereo for one peak.

        The decay goes through the validated estimator, with the window chosen
        from the expected RT60 and the local spacing and the neighbours passed
        in so contamination can be judged. Where no window can both separate
        the neighbour and fit the decay, the policy says so and no number is
        produced - the adversarial validation showed that guessing there costs
        up to 46% error while reporting a confident result.
    */
    void measurePeakDetail (ModalPeak& p, const ImpulseRender& render,
                            const AnalysisResolution& res,
                            const std::vector<ModalPeak>& allPeaks)
    {
        const double spacing = std::max (0.5, res.meanModalSpacingHz);
        const auto policy = windowPolicyFor (render.broadbandRt60, spacing);

        p.decayWindowSeconds = policy.windowSeconds;
        p.decayFeasible = policy.feasible;

        if (! policy.feasible)
        {
            p.decayStatus = "infeasible";
            p.decayReason = policy.note;
        }
        else
        {
            DecayOptions o;
            o.windowSeconds = policy.windowSeconds;
            o.hopSeconds = policy.hopSeconds;

            const double bandwidth = 2.0 / policy.windowSeconds;

            for (const auto& n : allPeaks)
            {
                const double d = std::abs (n.frequencyHz - p.frequencyHz);

                if (d > 1.0e-9 && d < 2.0 * bandwidth)
                    o.neighbours.push_back ({ n.frequencyHz,
                                              n.prominenceDb - p.prominenceDb });
            }

            const auto e = estimateModalDecay (render.left, render.sampleRate,
                                               p.frequencyHz, o);

            p.decayStatus = describe (e.quality);
            p.decayReason = e.reason;

            if (e.quality == DecayQuality::measured)
            {
                p.decaySeconds = e.rt60FromT30 > 0.0 ? e.rt60FromT30 : e.rt60FromT20;
                p.decayMeasured = true;
                p.decayRatioToBroadband = render.broadbandRt60 > 0.0
                                        ? p.decaySeconds / render.broadbandRt60 : 0.0;
            }
        }

        // Stereo is unaffected by the decay question and still uses a
        // band-limited pair, which is valid for energy and correlation even
        // where it was not valid for decay.
        const double half = std::max (res.resolutionHz, p.bandwidthHz) * 1.5;
        const double lo = std::max (10.0, p.frequencyHz - half);
        const double hi = std::min (render.sampleRate * 0.45, p.frequencyHz + half);

        if (hi <= lo) return;

        const auto bandL = bandLimited (render.left, render.sampleRate, lo, hi);
        const auto bandR = bandLimited (render.right, render.sampleRate, lo, hi);

        double el = 0.0, er = 0.0, cross = 0.0;

        for (size_t n = 0; n < bandL.size() && n < bandR.size(); ++n)
        {
            el += bandL[n] * bandL[n];
            er += bandR[n] * bandR[n];
            cross += bandL[n] * bandR[n];
        }

        const auto count = std::max<size_t> (1, bandL.size());

        p.energyLeftDb = toDbLocal (std::sqrt (el / static_cast<double> (count)));
        p.energyRightDb = toDbLocal (std::sqrt (er / static_cast<double> (count)));

        const double denom = std::sqrt (el * er);

        p.bandCorrelation = denom > 0.0 ? cross / denom : 0.0;
        p.stereoMeasured = denom > 0.0;
    }

    struct Frame
    {
        std::string label;
        double axisValue = 0.0;
        AnalysisResolution res;
        std::vector<ModalPeak> peaks;
        double broadbandRt60 = 0.0;
    };

    [[nodiscard]] Frame analyseSetting (const SweepPoint& point, double sampleRate,
                                        double windowSeconds, double renderSeconds,
                                        double minProminenceDb, size_t fftSize)
    {
        Frame f;

        f.label = point.label;

        const auto render = renderImpulse (point, sampleRate, renderSeconds);
        const auto window = std::min (render.left.size(),
                                      static_cast<size_t> (sampleRate * windowSeconds));

        f.res = resolutionFor (sampleRate, window, fftSize, render.sizeScale);
        f.broadbandRt60 = render.broadbandRt60;

        std::vector<double> segment (render.left.begin(),
                                     render.left.begin() + static_cast<long> (window));

        const auto spectrum = magnitudeSpectrum (segment, fftSize, true);

        f.peaks = detectPeaks (spectrum, f.res, 40.0, 12000.0, 200.0, minProminenceDb);

        return f;
    }


    [[nodiscard]] std::string quoted (const std::string& s) { return "\"" + s + "\""; }
}

int runModalSweep (const std::string& directory, double sampleRate)
{
    ScopedFtz ftz;

    constexpr size_t fftSize = 1 << 18;
    constexpr double renderSeconds = 6.0;
    constexpr double windowSeconds = 3.0;
    constexpr double detectProminenceDb = 10.0;

    // The detail measurement is the expensive part - two zero-phase
    // band-limitings and two decay fits per mode - so it is bounded, and the
    // bound is written into the output rather than applied silently.
    constexpr size_t detailedModes = 60;

    // From the validation suite. Named for what it is: a property of the
    // analysis, not of the renderer, which is bit-identical between runs.
    constexpr double variabilityFloorHz = 0.1278;

    int filesWritten = 0;

    std::ostringstream log;

    log << "PHASE 6 / FU-1 - RESOLUTION AND NOISE FLOOR\n";
    log << "===========================================\n\n";
    log << "Window duration sets resolving power. Zero-padded bin width is a\n";
    log << "property of the display and is never resolving power.\n\n";
    log << "Render repeatability: bit-identical, 0 sample difference between two\n";
    log << "renders of the same setting. Verified by the test suite.\n\n";
    log << "Analysis/window-shift variability floor: " << variabilityFloorHz << " Hz.\n";
    log << "  95th percentile of the centre difference when the analysis window is\n";
    log << "  moved 0.25 s over the same response. A property of the analysis.\n";
    log << "  Movement is only called movement above 3x this: "
        << 3.0 * variabilityFloorHz << " Hz.\n\n";
    log << "Detailed measurement (band-limited decay, stereo) limited to the\n";
    log << "strongest " << detailedModes << " modes of the reference setting.\n\n";

    // --- reference: Mod Depth 0, where the map is sharpest -------------------

    SweepPoint reference;
    reference.label = "reference: Size 0.60, Decay 2.5 s, Mod Depth 0";
    reference.modDepthControl = 0.0;

    auto referenceFrame = analyseSetting (reference, sampleRate, windowSeconds,
                                          renderSeconds, detectProminenceDb, fftSize);

    log << "reference: " << referenceFrame.res.describe() << "\n";
    log << "  broadband RT60 " << referenceFrame.broadbandRt60 << " s\n";
    log << "  peaks above " << detectProminenceDb << " dB prominence: "
        << referenceFrame.peaks.size() << "\n\n";

    // The reference's full list, before truncation, for the same reason.
    const auto referenceFullPeaks = referenceFrame.peaks;

    std::sort (referenceFrame.peaks.begin(), referenceFrame.peaks.end(),
               [] (const ModalPeak& a, const ModalPeak& b)
               { return a.prominenceDb > b.prominenceDb; });

    if (referenceFrame.peaks.size() > detailedModes)
        referenceFrame.peaks.resize (detailedModes);

    {
        const auto render = renderImpulse (reference, sampleRate, renderSeconds);

        for (auto& p : referenceFrame.peaks)
            measurePeakDetail (p, render, referenceFrame.res, referenceFrame.peaks);
    }

    const size_t modeCount = referenceFrame.peaks.size();

    // --- Size sweep ----------------------------------------------------------

    const double sizeControls[] = { 0.0, 0.1111, 0.2222, 0.3333, 0.4444,
                                    0.5556, 0.6667, 0.7778, 0.8889, 1.0 };

    std::vector<Frame> sizeFrames;
    std::vector<double> sizeScales;

    for (double s : sizeControls)
    {
        SweepPoint p;
        p.sizeControl = s;
        p.modDepthControl = 0.0;
        p.label = "size control " + std::to_string (s);

        auto f = analyseSetting (p, sampleRate, windowSeconds, renderSeconds,
                                 detectProminenceDb, fftSize);
        f.axisValue = s;

        sizeScales.push_back (mapping::sizeScaleFor (s));
        log << "size " << s << ": " << f.res.describe() << "\n";

        sizeFrames.push_back (std::move (f));
    }

    log << "\n";

    // --- Decay sweep ---------------------------------------------------------

    const double decays[] = { 0.5, 1.0, 2.5, 5.0, 10.0, 20.0 };
    constexpr size_t decayReferenceIndex = 2;

    std::vector<Frame> decayFrames;

    for (double d : decays)
    {
        SweepPoint p;
        p.decaySeconds = d;
        p.modDepthControl = 0.0;
        p.label = "decay " + std::to_string (d) + " s";

        auto f = analyseSetting (p, sampleRate, windowSeconds,
                                 std::max (renderSeconds, d * 0.6),
                                 detectProminenceDb, fftSize);
        f.axisValue = d;

        log << "decay " << d << " s: " << f.res.describe()
            << ", broadband RT60 " << f.broadbandRt60 << " s\n";

        decayFrames.push_back (std::move (f));
    }

    log << "\n";

    // --- Mod Depth A/B -------------------------------------------------------

    SweepPoint modOn;
    modOn.modDepthControl = 0.30;
    modOn.label = "mod depth 0.30, the production default";

    auto modOnFrame = analyseSetting (modOn, sampleRate, windowSeconds,
                                      renderSeconds, detectProminenceDb, fftSize);

    // The full list, kept before any truncation. Matching happens on this and
    // on the reference's full list; selecting the strongest for reporting is
    // done afterwards. The first sweep truncated first, so a counterpart whose
    // prominence had dropped under modulation was simply not in the list to be
    // matched against, and 57 of 60 modes came back unmatched as a result.
    const auto modOnFullPeaks = modOnFrame.peaks;

    std::sort (modOnFrame.peaks.begin(), modOnFrame.peaks.end(),
               [] (const ModalPeak& a, const ModalPeak& b)
               { return a.prominenceDb > b.prominenceDb; });

    if (modOnFrame.peaks.size() > detailedModes * 4)
        modOnFrame.peaks.resize (detailedModes * 4);

    {
        const auto render = renderImpulse (modOn, sampleRate, renderSeconds);

        for (size_t i = 0; i < std::min<size_t> (detailedModes, modOnFrame.peaks.size()); ++i)
            measurePeakDetail (modOnFrame.peaks[i], render, modOnFrame.res, modOnFrame.peaks);
    }

    log << "mod depth 0.30: " << modOnFrame.res.describe() << "\n";
    log << "  peaks: " << modOnFrame.peaks.size() << "\n\n";

    // --- track the reference modes across each axis --------------------------

    MatchOptions options;
    options.localSpacingHz = std::max (1.0, referenceFrame.res.meanModalSpacingHz);

    struct Trajectory
    {
        std::vector<double> frequency, prominence, bandwidth;
        std::vector<bool> matched;
    };

    const auto trackInto = [&] (const std::vector<Frame>& frames, size_t startIndex)
    {
        std::vector<Trajectory> out (modeCount);

        for (auto& t : out)
        {
            t.frequency.assign (frames.size(), std::numeric_limits<double>::quiet_NaN());
            t.prominence.assign (frames.size(), std::numeric_limits<double>::quiet_NaN());
            t.bandwidth.assign (frames.size(), std::numeric_limits<double>::quiet_NaN());
            t.matched.assign (frames.size(), false);
        }

        // Walk outwards from the frame that holds the reference setting, so
        // every step is a small one. Asking the tracker to jump the whole axis
        // at once is how identities get lost.
        const auto walk = [&] (int start, int step)
        {
            std::vector<ModalPeak> current = referenceFrame.peaks;
            std::vector<double> last (modeCount), previous (modeCount);

            for (size_t i = 0; i < modeCount; ++i)
                last[i] = previous[i] = referenceFrame.peaks[i].frequencyHz;

            for (int f = start; f >= 0 && f < static_cast<int> (frames.size()); f += step)
            {
                std::vector<double> predicted (modeCount);

                for (size_t i = 0; i < modeCount; ++i)
                    predicted[i] = 2.0 * last[i] - previous[i];

                const auto match = matchPeaks (current, frames[static_cast<size_t> (f)].peaks,
                                               options, &predicted);

                for (size_t i = 0; i < modeCount; ++i)
                {
                    const int j = match.assignment[i];

                    if (j < 0)
                        continue;

                    const auto& peak =
                        frames[static_cast<size_t> (f)].peaks[static_cast<size_t> (j)];

                    out[i].frequency[static_cast<size_t> (f)] = peak.frequencyHz;
                    out[i].prominence[static_cast<size_t> (f)] = peak.prominenceDb;
                    out[i].bandwidth[static_cast<size_t> (f)] = peak.bandwidthHz;
                    out[i].matched[static_cast<size_t> (f)] = true;

                    previous[i] = last[i];
                    last[i] = peak.frequencyHz;
                    current[i] = peak;
                }
            }
        };

        walk (static_cast<int> (startIndex), +1);
        walk (static_cast<int> (startIndex) - 1, -1);

        return out;
    };

    // The Decay axis keeps the generic tracker: there a mode is supposed to
    // stand still, and raw displacement over the local spacing is the right
    // measure. The Size axis does not, and no longer uses it - see below.
    const auto decayTracks = trackInto (decayFrames, decayReferenceIndex);

    // --- Size axis: the validated q-space tracker ----------------------------

    // Size scales the delay set, so every loop mode is expected to move by a
    // factor of 2^(1/3) between adjacent points - thousands of Hz at the top of
    // the band, hundreds of local spacings. The generic tracker gates that out
    // as impossible and matches whatever sits near the old frequency instead,
    // which is a different mode. That path is gone; what follows is
    // SizeTrack's trackSizeAxis, and there is no second copy of it here.
    const auto toSizeFrame = [] (const Frame& f, double control, double scale)
    {
        SizeFrame s;

        s.sizeControl = control;
        s.sizeScale = scale;
        s.meanModalSpacingHz = f.res.meanModalSpacingHz;

        for (const auto& p : f.peaks)
        {
            SizePeak sp;

            sp.frequencyHz = p.frequencyHz;
            sp.prominenceDb = p.prominenceDb;
            sp.bandwidthHz = p.bandwidthHz;
            sp.qFactor = p.q;
            sp.widthResolved = p.widthResolved;

            s.peaks.push_back (sp);
        }

        std::sort (s.peaks.begin(), s.peaks.end(),
                   [] (const SizePeak& a, const SizePeak& b)
                   { return a.frequencyHz < b.frequencyHz; });

        return s;
    };

    std::vector<SizeFrame> sizeAxisFrames;

    for (size_t k = 0; k < sizeFrames.size(); ++k)
        sizeAxisFrames.push_back (toSizeFrame (sizeFrames[k], sizeControls[k], sizeScales[k]));

    // The mode identities are the reference render's, at the shipping default
    // of Size 0.60, which is not one of the sweep points. The reference is
    // carried into the walk at its own scale rather than projected onto the
    // nearest point first, so no tracking step is spent before tracking starts.
    SizeAxisTracking sizeAxis;

    {
        Frame referenceFull = referenceFrame;
        referenceFull.peaks = referenceFullPeaks;

        const SizeFrame referenceSizeFrame =
            toSizeFrame (referenceFull, reference.sizeControl,
                         mapping::sizeScaleFor (reference.sizeControl));

        // Where each detailed mode sits in the reference's full peak list.
        std::vector<int> anchorPeaks;

        for (size_t i = 0; i < modeCount; ++i)
        {
            const double target = referenceFrame.peaks[i].frequencyHz;

            int best = -1;
            double bestDistance = std::numeric_limits<double>::infinity();

            for (size_t j = 0; j < referenceSizeFrame.peaks.size(); ++j)
            {
                const double d = std::abs (referenceSizeFrame.peaks[j].frequencyHz - target);

                if (d < bestDistance) { bestDistance = d; best = static_cast<int> (j); }
            }

            anchorPeaks.push_back (best);
        }

        sizeAxis = trackSizeAxis (sizeAxisFrames, &referenceSizeFrame, anchorPeaks,
                                  reference.sizeControl);
    }

    auto& sizeTracks = sizeAxis.trajectories;

    std::vector<SizeForensics> sizeForensics;
    std::vector<SizeEvidence> sizeEvidence;

    for (auto& t : sizeTracks)
    {
        // One residual definition everywhere, so the column means the same
        // thing here as it does in the offline re-track. Changes no assignment.
        recomputeInverseScaleResiduals (t, sizeAxisFrames);

        sizeForensics.push_back (analyseTrajectory (t));
        sizeEvidence.push_back (evidenceFor (t));
    }

    std::vector<int> modMatch (modeCount, -1);
    std::vector<double> modRegret (modeCount, 0.0);

    int modFullMatched = 0, modFullAmbiguous = 0, modFullBoundary = 0, modFullUnmatched = 0;

    {
        // The excursion under modulation can be several Hz, well past the gate
        // an adjacent-setting step needs.
        MatchOptions wide = options;
        wide.maxNormalisedDistance = 8.0;

        // Full lists on both sides. The reported modes are a selection made
        // afterwards, by looking their frequency up in the full-list result -
        // never by matching a pre-truncated pair.
        const auto match = matchPeaks (referenceFullPeaks, modOnFullPeaks, wide);

        const double lowEdge = 40.0 + wide.maxNormalisedDistance * wide.localSpacingHz;
        const double highEdge = 12000.0 - wide.maxNormalisedDistance * wide.localSpacingHz;

        for (size_t i = 0; i < referenceFullPeaks.size(); ++i)
        {
            if (match.assignment[i] >= 0)                  ++modFullMatched;
            else if (match.ambiguous[i])                   ++modFullAmbiguous;
            else if (referenceFullPeaks[i].frequencyHz < lowEdge
                     || referenceFullPeaks[i].frequencyHz > highEdge) ++modFullBoundary;
            else                                           ++modFullUnmatched;
        }

        // Carry the full-list outcome across to the reported modes. The lookup
        // is by frequency because the reported list was sorted by prominence
        // and no longer shares an index with the full one.
        for (size_t r = 0; r < modeCount; ++r)
        {
            const double target = referenceFrame.peaks[r].frequencyHz;

            size_t bestFull = 0;
            double bestDistance = std::numeric_limits<double>::infinity();

            for (size_t i = 0; i < referenceFullPeaks.size(); ++i)
            {
                const double d = std::abs (referenceFullPeaks[i].frequencyHz - target);

                if (d < bestDistance) { bestDistance = d; bestFull = i; }
            }

            if (bestDistance > 1.0e-6)
                continue;   // the reported mode is not in the full list: leave unmatched

            const int j = match.assignment[bestFull];

            modRegret[r] = match.regret[bestFull];

            if (j < 0)
                continue;

            // Translate the full-list column back into the reported modOn list,
            // again by frequency, so downstream indices stay valid.
            const double matchedHz = modOnFullPeaks[static_cast<size_t> (j)].frequencyHz;

            for (size_t k = 0; k < modOnFrame.peaks.size(); ++k)
            {
                if (std::abs (modOnFrame.peaks[k].frequencyHz - matchedHz) < 1.0e-6)
                {
                    modMatch[r] = static_cast<int> (k);
                    break;
                }
            }
        }
    }

    log << "Mod Depth matching, on full lists:\n";
    log << "  depth 0 peaks      " << referenceFullPeaks.size() << "\n";
    log << "  depth 0.30 peaks   " << modOnFullPeaks.size() << "\n";
    log << "  matched            " << modFullMatched << "\n";
    log << "  ambiguous          " << modFullAmbiguous << "\n";
    log << "  boundary exit      " << modFullBoundary << "\n";
    log << "  unmatched          " << modFullUnmatched << "\n\n";

    // --- raw CSVs, kept alongside the summary --------------------------------

    const auto writeFrames = [&] (const std::string& name, const std::vector<Frame>& frames,
                                  const std::string& axis)
    {
        std::ofstream out (directory + "/" + name, std::ios::trunc);

        if (! out.is_open())
            return false;

        out << axis << ",label,window_s,resolution_hz,bin_width_hz,mean_modal_spacing_hz,"
            << "broadband_rt60_s,peak_index,frequency_hz,prominence_db,bandwidth_hz,q,"
            << "width_resolved,ambiguous,ambiguity_reason\n";

        for (const auto& f : frames)
            for (size_t i = 0; i < f.peaks.size(); ++i)
            {
                const auto& p = f.peaks[i];

                out << f.axisValue << "," << quoted (f.label) << "," << f.res.windowSeconds
                    << "," << f.res.resolutionHz << "," << f.res.binWidthHz << ","
                    << f.res.meanModalSpacingHz << "," << f.broadbandRt60 << "," << i << ","
                    << p.frequencyHz << "," << p.prominenceDb << "," << p.bandwidthHz << ","
                    << p.q << "," << (p.widthResolved ? 1 : 0) << ","
                    << (p.ambiguous ? 1 : 0) << "," << quoted (p.ambiguityReason) << "\n";
            }

        return true;
    };

    if (writeFrames ("size-sweep-raw.csv", sizeFrames, "size_control")) ++filesWritten;
    if (writeFrames ("decay-sweep-raw.csv", decayFrames, "decay_seconds")) ++filesWritten;

    {
        // Both frames used to be written with axisValue at its default, so the
        // setting_index column read 0 for the reference and 0 again for the Mod
        // Depth map. Anything selecting "setting 0" got 300 rows from two
        // different settings with peak indices that restart in the middle. The
        // column now says which is which.
        std::vector<Frame> maps { referenceFrame, modOnFrame };

        maps[0].axisValue = 0.0;
        maps[1].axisValue = 1.0;

        if (writeFrames ("modal-map-raw.csv", maps, "setting_index")) ++filesWritten;
    }

    // --- size trajectories with fit quality ----------------------------------

    // The fit is the one analyseTrajectory performs - the same least squares and
    // the same reliability rule the sweep has always used, four points and
    // R-squared above 0.90. Neither is changed here, and there is no second
    // implementation of either left in this file.
    const auto sizeLabels = labelTrajectories (sizeForensics, sizeAxis);

    SizeOutputs sizeOutputs;

    sizeOutputs.sizeControls.assign (std::begin (sizeControls), std::end (sizeControls));

    for (size_t i = 0; i < modeCount; ++i)
        sizeOutputs.referenceHz.push_back (referenceFrame.peaks[i].frequencyHz);

    sizeOutputs.trajectories = &sizeTracks;
    sizeOutputs.forensics = &sizeForensics;
    sizeOutputs.evidence = &sizeEvidence;
    sizeOutputs.labels = &sizeLabels;
    sizeOutputs.axis = &sizeAxis;

    if (writeSizeSweepCsv (directory, sizeOutputs))      ++filesWritten;
    if (writeSizeTrajectoryCsv (directory, sizeOutputs)) ++filesWritten;
    if (writeSizePointsCsv (directory, sizeOutputs))     ++filesWritten;

    // --- decay trajectories: centre, width, Q, and the broadband ratio -------

    {
        std::ofstream out (directory + "/decay-sweep.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << "mode_id,reference_hz,decay_seconds,centre_hz,centre_movement_hz,"
                << "bandwidth_hz,q,broadband_rt60_s,matched\n";

            for (size_t i = 0; i < modeCount; ++i)
                for (size_t k = 0; k < decayFrames.size(); ++k)
                {
                    const double f = decayTracks[i].frequency[k];
                    const double bw = decayTracks[i].bandwidth[k];
                    const double reference = referenceFrame.peaks[i].frequencyHz;

                    out << i << "," << reference << "," << decayFrames[k].axisValue << ",";

                    if (std::isfinite (f)) out << f << "," << (f - reference) << ",";
                    else                   out << ",,";

                    if (std::isfinite (bw) && bw > 0.0) out << bw << "," << (f / bw) << ",";
                    else                                out << ",,";

                    out << decayFrames[k].broadbandRt60 << ","
                        << (decayTracks[i].matched[k] ? 1 : 0) << "\n";
                }

            ++filesWritten;
        }
    }

    // --- Mod Depth A/B: movement is only part of the question ----------------

    {
        std::ofstream out (directory + "/moddepth-ab.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << "mode_id,f_depth0_hz,f_default_hz,excursion_hz,"
                << "prominence_depth0_db,prominence_default_db,prominence_change_db,"
                << "bandwidth_depth0_hz,bandwidth_default_hz,bandwidth_change_hz,"
                << "decay_ratio_depth0,decay_ratio_default,decay_ratio_change,"
                << "matched,regret\n";

            for (size_t i = 0; i < modeCount; ++i)
            {
                const auto& a = referenceFrame.peaks[i];

                out << i << "," << a.frequencyHz << ",";

                if (modMatch[i] >= 0)
                {
                    const auto& b = modOnFrame.peaks[static_cast<size_t> (modMatch[i])];

                    out << b.frequencyHz << "," << (b.frequencyHz - a.frequencyHz) << ","
                        << a.prominenceDb << "," << b.prominenceDb << ","
                        << (b.prominenceDb - a.prominenceDb) << ","
                        << a.bandwidthHz << "," << b.bandwidthHz << ","
                        << (b.bandwidthHz - a.bandwidthHz) << ","
                        << a.decayRatioToBroadband << "," << b.decayRatioToBroadband << ","
                        << (b.decayRatioToBroadband - a.decayRatioToBroadband) << ",1,";
                }
                else
                {
                    out << ",,," << a.prominenceDb << ",,," << a.bandwidthHz << ",,,"
                        << a.decayRatioToBroadband << ",,,0,";
                }

                out << modRegret[i] << "\n";
            }

            ++filesWritten;
        }
    }

    // --- local modulation response, for every reference mode -----------------

    std::vector<LocalResponse> localBefore (modeCount), localAfter (modeCount);

    {
        const auto renderA = renderImpulse (reference, sampleRate, renderSeconds);
        const auto renderB = renderImpulse (modOn, sampleRate, renderSeconds);

        const auto window = std::min (renderA.left.size(),
                                      static_cast<size_t> (sampleRate * windowSeconds));

        std::vector<double> segA (renderA.left.begin(), renderA.left.begin() + static_cast<long> (window));
        std::vector<double> segB (renderB.left.begin(), renderB.left.begin() + static_cast<long> (window));

        const auto specA = magnitudeSpectrum (segA, fftSize, true);
        const auto specB = magnitudeSpectrum (segB, fftSize, true);

        // The neighbourhood comes from the measured modal spacing and the
        // excursion being looked for, not from a fixed number of Hz.
        const double spacing = referenceFrame.res.meanModalSpacingHz;
        const double expectedExcursion = 6.0;
        const double halfWidth = std::max (2.0 * spacing, 1.5 * expectedExcursion);

        for (size_t i = 0; i < modeCount; ++i)
        {
            const double f = referenceFrame.peaks[i].frequencyHz;

            localBefore[i] = measureLocalResponse (specA, referenceFrame.res, f, halfWidth);
            localAfter[i]  = measureLocalResponse (specB, modOnFrame.res, f, halfWidth);
        }

        std::ofstream out (directory + "/local-response.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << "mode_id,frequency_hz,half_width_hz,"
                << "energy_depth0_db,energy_default_db,energy_change_db,"
                << "max_prom_depth0_db,max_prom_default_db,"
                << "centroid_depth0_hz,centroid_default_hz,"
                << "spread_depth0_hz,spread_default_hz,spread_ratio,"
                << "concentration_depth0,concentration_default,concentration_ratio,"
                << "peaks_depth0,peaks_default,measured\n";

            for (size_t i = 0; i < modeCount; ++i)
            {
                const auto& a = localBefore[i];
                const auto& b = localAfter[i];
                const bool ok = a.measured && b.measured;

                out << i << "," << referenceFrame.peaks[i].frequencyHz << "," << a.halfWidthHz << ",";

                if (ok)
                    out << a.integratedEnergyDb << "," << b.integratedEnergyDb << ","
                        << (b.integratedEnergyDb - a.integratedEnergyDb) << ","
                        << a.maxProminenceDb << "," << b.maxProminenceDb << ","
                        << a.spectralCentroidHz << "," << b.spectralCentroidHz << ","
                        << a.spectralSpreadHz << "," << b.spectralSpreadHz << ","
                        << (a.spectralSpreadHz > 0.0 ? b.spectralSpreadHz / a.spectralSpreadHz : 0.0) << ","
                        << a.energyConcentration << "," << b.energyConcentration << ","
                        << (a.energyConcentration > 0.0 ? b.energyConcentration / a.energyConcentration : 0.0) << ","
                        << a.peakCount << "," << b.peakCount << ",1\n";
                else
                    out << ",,,,,,,,,,,,,,0\n";
            }

            ++filesWritten;
        }
    }

    // --- the mode table ------------------------------------------------------

    {
        std::ofstream out (directory + "/mode-table.csv", std::ios::trunc);

        if (out.is_open())
        {
            out << "mode_id,frequency_hz,prominence_db,bandwidth_hz,q,width_resolved,"
                << "modal_decay_s,broadband_rt60_s,decay_ratio,decay_status,decay_reason,decay_window_s,"
                << "energy_left_db,energy_right_db,band_correlation,stereo_measured,"
                << "size_fit_exponent,size_fit_r_squared,size_fit_residual_rms,"
                << "size_fit_points,size_fit_reliable,"
                << "mod_excursion_hz,mod_prominence_change_db,mod_bandwidth_change_hz,"
                << "mod_decay_ratio_change,class,confidence,ambiguity_reason\n";

            for (size_t i = 0; i < modeCount; ++i)
            {
                const auto& p = referenceFrame.peaks[i];
                const auto& fit = sizeForensics[i];

                double excursion = 0.0, promChange = 0.0, bwChange = 0.0, decayChange = 0.0;
                const bool modMatched = modMatch[i] >= 0;

                if (modMatched)
                {
                    const auto& b = modOnFrame.peaks[static_cast<size_t> (modMatch[i])];

                    excursion = std::abs (b.frequencyHz - p.frequencyHz);
                    promChange = b.prominenceDb - p.prominenceDb;
                    bwChange = b.bandwidthHz - p.bandwidthHz;
                    decayChange = b.decayRatioToBroadband - p.decayRatioToBroadband;
                }

                // Classification no longer hangs on a 1:1 Mod Depth identity.
                // Modulation exists to spread a fixed peak, so failing to follow
                // one peak is often the modulation working. Local redistribution
                // counts as modulation evidence in its own right.
                const auto& lr0 = localBefore[i];
                const auto& lrD = localAfter[i];

                bool modulationEvidence = false;
                bool redistributes = false;

                if (lr0.measured && lrD.measured)
                {
                    modulationEvidence = true;

                    const double spreadRatio = lr0.spectralSpreadHz > 0.0
                                             ? lrD.spectralSpreadHz / lr0.spectralSpreadHz : 1.0;
                    const double concentrationRatio = lr0.energyConcentration > 0.0
                                                    ? lrD.energyConcentration / lr0.energyConcentration : 1.0;

                    // The signature validated synthetically: spread grows and
                    // concentration falls together. Either alone could be
                    // something else.
                    redistributes = spreadRatio > 1.5 && concentrationRatio < 0.7;
                }

                if (modMatched && excursion > 3.0 * variabilityFloorHz)
                    redistributes = true;

                // The rule itself lives in ModalMap so the offline regeneration
                // applies the identical one. Unchanged: same thresholds, same
                // class definitions, same treatment of an unreliable fit.
                const ModeClass cls = classifySweepMode (fit.exponent, fit.fitReliable,
                                                         modulationEvidence, redistributes);

                out << i << "," << p.frequencyHz << "," << p.prominenceDb << ","
                    << p.bandwidthHz << "," << p.q << "," << (p.widthResolved ? 1 : 0) << ","
                    << (p.decayMeasured ? std::to_string (p.decaySeconds) : std::string()) << ","
                    << referenceFrame.broadbandRt60 << ","
                    << (p.decayMeasured ? std::to_string (p.decayRatioToBroadband) : std::string()) << ","
                    << quoted (p.decayStatus) << "," << quoted (p.decayReason) << ","
                    << p.decayWindowSeconds << ","
                    << p.energyLeftDb << "," << p.energyRightDb << "," << p.bandCorrelation
                    << "," << (p.stereoMeasured ? 1 : 0) << "," << fit.exponent << ","
                    << fit.rSquared << "," << fit.fitResidualRms << "," << fit.fitPoints << ","
                    << (fit.fitReliable ? 1 : 0) << ",";

                if (modMatched)
                    out << excursion << "," << promChange << "," << bwChange << ","
                        << decayChange << ",";
                else
                    out << ",,,,";

                out << quoted (describe (cls)) << "," << modRegret[i] << ","
                    << quoted (p.ambiguityReason.empty()
                               ? (modMatched ? std::string ("none")
                                             : std::string ("not matched across Mod Depth"))
                               : p.ambiguityReason)
                    << "\n";
            }

            ++filesWritten;
        }
    }

    {
        std::ofstream out (directory + "/RESOLUTION.txt", std::ios::trunc);

        if (out.is_open()) { out << log.str(); ++filesWritten; }
    }

    return filesWritten;
}

} // namespace avtest
