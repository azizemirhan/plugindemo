#pragma once

#include <string>
#include <vector>

/**
    Phase 6 / FU-1: peak tracking along the Size axis.

    Measurement only. Nothing here touches the production DSP, and nothing here
    renders: the tracker consumes peak lists and returns trajectories, so the
    same code runs on synthetic lists with a known answer and on the raw CSV a
    sweep already wrote.

    Why this exists separately from ModalMap's matchPeaks
    -----------------------------------------------------
    The generic tracker measures distance as raw Hz over the local modal
    spacing, and gates a pair out above three spacings. That is the right
    measure on the Decay and Mod Depth axes, where a mode is supposed to stand
    still. On the Size axis it is wrong in the strongest possible way.

    Size scales the delay set, so every loop mode is supposed to move, and to
    move a long way:

        f(scale) = q / scale

    The sweep steps the scale by a fixed ratio of 2^(1/3), so between adjacent
    Size points a mode at 11.5 kHz is expected to move about 2.4 kHz - roughly
    five hundred local modal spacings. A gate at three spacings calls that
    impossible, and the only pairs left inside it are peaks that happen to sit
    near the old frequency and belong to other modes. The tracker then reports
    identities with high confidence, and they are swaps.

    What replaces it
    ----------------
    The expected movement is subtracted first, using physics rather than
    history. Under uniform scaling the modal coordinate

        q = frequencyHz * sizeScale

    is invariant, so from any accepted point the prediction at the next Size is

        f_pred = q_last / scale_new

    which is available from the very first step - no two-point velocity estimate
    is needed, and no error accumulates through a chain of extrapolations
    because the prediction is anchored to the last accepted measurement rather
    than to the last prediction.

    Local modal spacing then does the job it is good at, on the residual: after
    the expected movement is removed, what is left has to be small compared with
    the distance to the next mode, or identity cannot be claimed.

    Deliberately kept from the generic tracker, because they are what make it
    honest rather than what made it wrong:

      - a real global assignment (Jonker-Volgenant), not a greedy pass
      - a dummy column per track, so "no match" competes inside the optimisation
      - an ambiguity margin read from the dual potentials, applied after the
        solve, which refuses an identity rather than guessing it
      - an impossible-pair gate

    Three things it refuses to do:

      - use raw Hz displacement as evidence of identity loss
      - fill a gap by taking the nearest surviving neighbour
      - trade confidence for coverage

    On the choice of normalisation
    ------------------------------
    The cost is in units of the local spacing between detected peaks, not of
    the mean modal spacing implied by the delay set. The confusion the tracker
    has to survive is with the peaks that are actually there to be confused
    with, and that distance is measured, not derived.

    It is also, deliberately, not normalised by anything as tight as the
    observed inverse-scale residual. A tolerance of a few parts in 10^5 would
    track the modes that obey f = q/scale beautifully and would silently drop
    every mode that does not - which would make "all surviving modes are
    inverse-scale consistent" a property of the gate rather than of the tank.
    The gate is set by what identity requires - stay nearer your own prediction
    than the next mode's slot - and the exponent is then measured, not assumed.
*/
namespace avtest
{

// ===========================================================================
//  Input
// ===========================================================================

/**
    One detected peak on the Size axis.

    Exactly the fields the size raw CSV carries. There is no L/R energy and no
    band correlation on this axis; they are not invented here, and nothing
    downstream may use them.
*/
struct SizePeak
{
    double frequencyHz = 0.0;
    double prominenceDb = 0.0;
    double bandwidthHz = 0.0;
    double qFactor = 0.0;
    bool   widthResolved = false;
};

/** One Size point: the control value, the delay scale it maps to, its peaks. */
struct SizeFrame
{
    double sizeControl = 0.0;
    double sizeScale = 1.0;

    /** From the delay set. An average density, never a local spacing. */
    double meanModalSpacingHz = 0.0;

    /** Ascending in frequency. */
    std::vector<SizePeak> peaks;
};

/**
    Median gap between detected peaks in a neighbourhood of centreHz.

    Measured rather than derived, because the mean modal spacing of the delay
    set counts modes the detector never separated. Falls back to the frame's
    mean spacing where the neighbourhood is too sparse to have a median.
*/
[[nodiscard]] double localPeakSpacingHz (const SizeFrame& frame, double centreHz,
                                         double windowHz, double floorHz);

// ===========================================================================
//  Options
// ===========================================================================

struct SizeTrackOptions
{
    /** The detector's band. A prediction outside it is a boundary exit and is
        reported as such, not as a lost mode. */
    double bandLowHz = 40.0;
    double bandHighHz = 12000.0;

    /** Neighbourhood the local spacing is measured over. */
    double spacingWindowHz = 200.0;

    /** The analysis floor: 3x the 0.1278 Hz window-shift variability. No
        tolerance is ever claimed tighter than this. */
    double spacingFloorHz = 0.3834;

    /** Impossible-pair gate, in local spacings of residual. */
    double maxResidualSpacings = 3.0;

    /** Weight on the prominence difference, per dB, in the same units as the
        residual term, and a hard cap on what it can ever contribute.

        Both are small, and the cap is the important one. Prominence on this
        axis moves a lot for reasons that have nothing to do with identity:
        between two adjacent Size points a peak routinely changes by ten dB or
        more as modes merge and separate, while the inverse-scale residual of
        the same mode stays around 0.2 Hz. At 0.05 per dB an eight dB swing was
        worth 0.4 of a local spacing, which was enough to make a wrong
        neighbour 1 Hz from the prediction cheaper than the right peak 0.006 Hz
        from it. Capped here at a tenth of a spacing, prominence can break a
        near-tie in position and can never overrule one. */
    double prominenceWeight = 0.01;
    double maxProminencePenalty = 0.10;

    /** What it costs to leave a track unmatched, in local spacings.

        Below one spacing by design: a candidate a whole spacing away from the
        prediction is as near the next mode's slot as it is to this one, and
        taking it is how a gap becomes a swap. */
    double rejectionCost = 0.75;

    /** How much better the optimum must be than its alternative, in local
        spacings, before the identity is trusted. Read from the dual
        potentials after the global solve. */
    double ambiguityMargin = 0.25;
};

// ===========================================================================
//  Output
// ===========================================================================

enum class SizePointStatus
{
    matched = 0,    /**< Assigned, and the assignment was clearly best. */
    ambiguous,      /**< Assigned by the solver, refused by the margin. */
    unmatched,      /**< The solver preferred no match to the best one. */
    outOfBand       /**< The prediction left the detector's band. */
};

[[nodiscard]] const char* describe (SizePointStatus s) noexcept;

struct SizePoint
{
    int frameIndex = 0;
    SizePointStatus status = SizePointStatus::unmatched;

    /** Index into the frame's peak list, or -1. */
    int peakIndex = -1;

    double sizeControl = 0.0;
    double sizeScale = 1.0;

    double frequencyHz = 0.0;       /**< NaN unless matched or ambiguous. */
    double predictedHz = 0.0;       /**< Inverse-scale prediction. */
    double residualHz = 0.0;        /**< observed - predicted. */
    double logResidual = 0.0;       /**< log (observed / predicted). */
    double localSpacingHz = 0.0;
    double residualSpacings = 0.0;  /**< abs (residual) / local spacing. */

    double modalCoordinate = 0.0;   /**< q = frequency * sizeScale. */

    double prominenceDb = 0.0;
    double bandwidthHz = 0.0;
    double qFactor = 0.0;

    /** Lower bound on what changing this identity would cost, in spacings. */
    double regret = 0.0;
};

struct SizeTrajectory
{
    int id = 0;
    int anchorFrame = 0;
    double anchorHz = 0.0;

    /** One entry per frame, in frame order. */
    std::vector<SizePoint> points;

    int matchedPoints = 0;
    int ambiguousPoints = 0;
    int unmatchedPoints = 0;
    int outOfBandPoints = 0;
};

/**
    Follows the anchor peaks outwards from the anchor frame.

    Two independent chains per anchor - one towards larger Size, one towards
    smaller - so that a mistake at one end cannot poison the other. There is no
    single min-to-max walk anywhere in here.
*/
[[nodiscard]] std::vector<SizeTrajectory> trackSize (const std::vector<SizeFrame>& frames,
                                                     size_t anchorFrame,
                                                     const std::vector<int>& anchorPeakIndices,
                                                     const SizeTrackOptions& options);

// ===========================================================================
//  Peak evidence - a reporting layer, never a tracking decision
// ===========================================================================

/**
    How much the peak an assignment landed on actually looks like the mode.

    Deliberately separate from the assignment. The tracker's gate exists to
    decide identity - is this peak the continuation of that trajectory - and it
    is set wide enough to tolerate a mode genuinely departing from inverse
    scaling. It is not a test of whether a peak is there at all, and using it as
    one is how a detection gap gets reported as a measurement.

    The off-grid validation produced the worked example. Mode 14 at scale
    1.41426 was predicted at 1072.93 Hz; the render had peaks at 1068.54 and
    1078.56 and nothing between them. The tracker took the nearer of the two -
    correctly, by its own rule, and its identity claim is not thereby wrong -
    but that peak was 1.30 local spacings away, 21 dB quieter than the
    trajectory's median and less than 1 dB above the detector's floor. The mode
    had dropped below the detection threshold at that Size. "Tracked" and "peak
    confirmed" are different statements and this is where they come apart.

    Nothing here changes an assignment, a gate, a weight or the ambiguity
    margin.
*/
enum class PeakEvidence
{
    confirmed = 0,   /**< Near the prediction, and clearly a peak. */
    weak,            /**< There, but close to the detector's floor or reshaped. */
    suspect,         /**< Nearer another mode's slot than its own. */
    absent           /**< No assignment at this point. */
};

[[nodiscard]] const char* describe (PeakEvidence e) noexcept;

struct SizeEvidenceOptions
{
    /**
        Residual above which the assignment is called suspect, in local
        spacings.

        One, which is the design's own boundary rather than a chosen number:
        beyond a whole spacing the prediction sits nearer some other mode's
        slot than its own, whatever the gate was willing to accept.
    */
    double suspectResidualSpacings = 1.0;

    /**
        Prominence below which a peak is called weak evidence, in dB.

        Taken from the detector, not chosen: the sweep detects above 10 dB
        prominence, so a peak within 3 dB of that floor is one the detector
        nearly did not have. It is a real peak; it is thin evidence.
    */
    double weakProminenceDb = 13.0;

    /**
        Factor by which the -3 dB width may differ from the trajectory's median
        before the signature is called mismatched.

        A plain factor of two in either direction. A resonance whose width
        doubles is not obviously the same resonance, and saying so is cheaper
        than pretending the question did not arise.
    */
    double signatureRatioLimit = 2.0;
};

/** Per point, and rolled up per trajectory. Fills nothing on the tracker. */
struct SizeEvidence
{
    std::vector<PeakEvidence> points;   /**< One per frame, in frame order. */

    int confirmedPoints = 0;
    int weakPoints = 0;
    int suspectPoints = 0;

    /** The worst status any matched point reached. */
    PeakEvidence worst = PeakEvidence::absent;

    /** Median -3 dB width over matched points, for reference. */
    double medianBandwidthHz = 0.0;
    double medianProminenceDb = 0.0;
};

[[nodiscard]] SizeEvidence evidenceFor (const SizeTrajectory& t,
                                        const SizeEvidenceOptions& options = {});

/** Agreement between two independently anchored trackings of one trajectory. */
struct SizeAgreement
{
    int comparable = 0;   /**< Frames where both matched. */
    int same = 0;         /**< ...and chose the same peak. */

    /**
        No frame where the two disagreed.

        Vacuously true when there is no overlap, which is the honest reading: a
        trajectory whose matches all sit on one side of the anchor has nothing
        there for the opposite chain to contradict, and counting that as a
        failure would report absence of evidence as evidence of absence.
    */
    [[nodiscard]] bool consistent() const { return same == comparable; }
};

// ===========================================================================
//  The Size axis, as the sweep tracks it
// ===========================================================================

/**
    One Size-axis tracking, with its path-independence checks.

    This is the only place the Size axis is tracked. ModalSweep calls it, the
    offline re-track calls it, and the regression suite calls it; there is no
    second copy of the algorithm anywhere, which is the point - the version
    that was validated is the version that runs.
*/
struct SizeAxisTracking
{
    /** One per anchor peak, indexed by sweep frame. */
    std::vector<SizeTrajectory> trajectories;

    /** Frame nearest the reference, where the two chains part. */
    size_t anchorFrame = 0;

    std::vector<SizeAgreement> forwardBackward, anchorUp, anchorDown;
};

/**
    Tracks every anchor peak across the Size axis.

    `referenceFrame` may be null, in which case the anchor is the sweep point
    nearest `defaultSizeControl` and `anchorPeakIndices` index into that
    frame's peaks. When it is given - which is what the sweep does, because its
    mode identities are defined at the shipping default rather than at whichever
    sweep point happens to sit near it - the reference is carried into the walk
    at its own scale and the indices are into its peaks. Either way the
    trajectories that come back are indexed by sweep frame only.
*/
[[nodiscard]] SizeAxisTracking trackSizeAxis (const std::vector<SizeFrame>& frames,
                                              const SizeFrame* referenceFrame,
                                              const std::vector<int>& anchorPeakIndices,
                                              double defaultSizeControl = 0.60,
                                              const SizeTrackOptions& options = {});

// ===========================================================================
//  Forensics on one trajectory
// ===========================================================================

// (SizeForensics and analyseTrajectory follow; the labelling and the CSV
//  writers that consume them are declared after, so that the sweep and the
//  offline regeneration share one definition of each.)

struct SizeForensics
{
    int matchedPoints = 0;
    int ambiguousPoints = 0;
    int unmatchedPoints = 0;
    int outOfBandPoints = 0;

    /** Sign changes in consecutive frequency steps across matched points.
        Under any monotone size law there should be none. */
    int reversals = 0;

    /** Matched points whose residual exceeded three local spacings. Zero by
        construction inside the gate; kept because the same routine is run on
        the old tracker's trajectories, where it is not. */
    int bigJumps = 0;

    /** Coefficient of variation of q = f * scale. Zero for a mode that obeys
        uniform scaling exactly. */
    double coordinateCv = 0.0;
    bool   coordinateCvMeasured = false;

    /** log f against log scale. Same fit and same reliability rule as the
        sweep's: four points and R^2 above 0.90. Not changed here. */
    double exponent = 0.0;
    double rSquared = 0.0;
    double fitResidualRms = 0.0;
    int    fitPoints = 0;
    bool   fitReliable = false;

    double residualRmsHz = 0.0;
    double meanAbsResidualSpacings = 0.0;
};

[[nodiscard]] SizeForensics analyseTrajectory (const SizeTrajectory& t);

/**
    Restates every matched point's residual in ascending Size order.

    The tracker's own residuals are relative to its walk direction, which makes
    the number mean something slightly different on each side of the anchor.
    One definition is used for everything that is reported or compared: predict
    each matched point from the previous matched point by conserving q, and
    measure what is left against the local spacing at the prediction. Across a
    gap that prediction reaches further, and the residual is allowed to say so
    - which is the case the evidence layer exists to catch.

    Changes no assignment. Call it before analyseTrajectory and evidenceFor.
*/
void recomputeInverseScaleResiduals (SizeTrajectory& t,
                                     const std::vector<SizeFrame>& frames,
                                     const SizeTrackOptions& options = {});

// ===========================================================================
//  Labels and outputs, shared by the sweep and the offline regeneration
// ===========================================================================

enum class SizeLabel
{
    inverseScaleConsistent = 0,
    nonInverseScaleCandidate,
    identityUncertain
};

[[nodiscard]] const char* describe (SizeLabel l) noexcept;

/** The forensics' own threshold for "the modal coordinate held still". */
inline constexpr double inverseScaleCvLimit = 0.05;

/**
    Identity first, exponent second.

    A trajectory is identity-consistent only with four or more matched points,
    no reversal, no residual above three local spacings, no frame where an
    independently anchored chain chose a different peak, and at least two
    frames where one did choose the same. Only then is the modal coordinate
    read. One definition, so the sweep and the re-track cannot drift apart.
*/
[[nodiscard]] std::vector<SizeLabel> labelTrajectories (const std::vector<SizeForensics>& forensics,
                                                        const SizeAxisTracking& axis);

/** Everything the Size output files are written from. */
struct SizeOutputs
{
    std::vector<double> sizeControls;
    std::vector<double> referenceHz;

    const std::vector<SizeTrajectory>* trajectories = nullptr;
    const std::vector<SizeForensics>* forensics = nullptr;
    const std::vector<SizeEvidence>* evidence = nullptr;
    const std::vector<SizeLabel>* labels = nullptr;
    const SizeAxisTracking* axis = nullptr;

    [[nodiscard]] bool complete() const
    {
        return trajectories != nullptr && forensics != nullptr && evidence != nullptr
            && labels != nullptr && axis != nullptr;
    }
};

/** The sweep's summary, one row per mode, with the forensic columns added. */
[[nodiscard]] bool writeSizeSweepCsv (const std::string& directory, const SizeOutputs& out);

/** One row per mode: fit, agreement, ambiguity and evidence. */
[[nodiscard]] bool writeSizeTrajectoryCsv (const std::string& directory, const SizeOutputs& out);

/** One row per mode per Size point: the whole measurement, nothing summarised. */
[[nodiscard]] bool writeSizePointsCsv (const std::string& directory, const SizeOutputs& out);

// ===========================================================================
//  Offline re-track of an existing sweep
// ===========================================================================

/** Reads the peak lists out of a size-sweep-raw.csv. Frames and peaks come
    back sorted, by Size control and by frequency. */
[[nodiscard]] std::vector<SizeFrame> readSizeFramesCsv (const std::string& path,
                                                        std::string& error);


/**
    Everything the re-track derived, so a second stage can reuse it exactly.

    Shared rather than recomputed, because a cross-validation that re-derived
    the trajectories with slightly different settings would be validating
    something other than the thing being shipped.
*/
struct SizeRetrackResult
{
    std::vector<SizeFrame> frames;
    size_t anchorFrame = 0;
    std::vector<int> anchorPeaks;

    std::vector<SizeTrajectory> trajectories;
    std::vector<SizeForensics> forensics;
    std::vector<SizeLabel> labels;

    std::vector<SizeAgreement> forwardBackward, anchorUp, anchorDown;

    bool valid = false;
    std::string error;
};

[[nodiscard]] SizeRetrackResult retrackSizeFromCsv (const std::string& rawCsvPath,
                                                    const SizeTrackOptions& options);

/**
    Re-tracks a size-sweep-raw.csv that a previous sweep already wrote.

    No render happens. The raw peak lists are the measurement; the tracking was
    the part that was wrong, and it is the only part redone. Writes its report
    and per-point CSVs into outDirectory, leaving the input untouched.

    Returns the number of files written, or a negative value on failure.
*/
int retrackSizeSweep (const std::string& rawCsvPath, const std::string& outDirectory);

// ===========================================================================
//  Independent validation of the q-space prior
// ===========================================================================

/**
    Leave-one-Size-out, plus a targeted off-grid render.

    Answers one question and no other: does the inverse-scale prior find the
    right peak, or does it find *a* peak? A dense modal map has a peak every
    few Hz, so a prediction landing near one proves nothing on its own. Both
    halves therefore carry a decoy baseline - the same measurement repeated at
    deliberately wrong frequencies in the same neighbourhood - and the result
    is the separation between the two, not the hit rate of either.

    The leave-one-out half removes a Size point from the tracking input
    entirely and re-tracks without it, so the trajectory used to predict that
    point never saw it. The off-grid half renders Size settings halfway between
    the sweep's own, which no tracking has ever been run on.

    Writes its report and CSVs into `outDirectory`. Renders a small number of
    extra impulses; the production DSP is read, never changed. Returns the
    number of files written, or a negative value on failure.
*/
int crossValidateSizeTracker (const std::string& rawCsvPath,
                              const std::string& outDirectory,
                              double sampleRate = 48000.0);

// ===========================================================================
//  Regenerating the analysis layer from raw data already on disk
// ===========================================================================

/**
    Rebuilds the Size outputs and the mode table from an existing sweep folder.

    No render, and none is needed: the peak lists were never the problem. The
    Size tracking is redone through trackSizeAxis, the Size columns of the mode
    table are recomputed from it, and the classifier - unchanged, same
    thresholds, same class definitions - is given that evidence instead of the
    old tracker's. Everything that does not depend on Size tracking is carried
    across byte for byte.

    The source directory is only read. Returns the number of files written into
    `outDirectory`, or a negative value on failure.
*/
int regeneratePhase6Outputs (const std::string& sourceDirectory,
                             const std::string& outDirectory);

} // namespace avtest
