#pragma once

#include <string>
#include <vector>

/**
    Phase 6 / FU-1: modal map, peak tracking and parameter sweeps.

    Measurement only. Nothing here changes a gain, a coefficient or a topology,
    and the production DSP is read through the same interfaces the plugin uses.

    What this exists to answer is narrower than "where are the resonances". A
    reverb that is lush and blooming is not flat, so a peak list on its own says
    nothing about whether a peak is character or defect. What separates them is
    behaviour: how long a mode rings relative to the tail around it, whether it
    moves with Size the way a loop mode must, and whether the modulation reaches
    it. This measures those, and deliberately stops short of judging them.

    Three things it refuses to do, because each would manufacture a finding:

      - report zero-padded bin width as resolving power
      - force a match between peaks that cannot be told apart
      - call a size-insensitive peak "not a mode"
*/
namespace avtest
{

// ===========================================================================
//  Resolution bookkeeping
// ===========================================================================

/**
    The three numbers that must never be conflated.

    `binWidthHz` shrinks with zero-padding and is a property of the display.
    `resolutionHz` does not: two components closer than that were not separated
    by the measurement, however many bins sit between them.
*/
struct AnalysisResolution
{
    double sampleRate = 0.0;
    size_t windowSamples = 0;    /**< Untapered data length. */
    size_t fftSize = 0;          /**< After zero-padding. */

    double windowSeconds = 0.0;
    double binWidthHz = 0.0;     /**< sampleRate / fftSize. Display only. */
    double resolutionHz = 0.0;   /**< About 2 / windowSeconds for Hann. */

    /** Average modal density from the delay set: an estimate, not a spacing. */
    double meanModalSpacingHz = 0.0;

    [[nodiscard]] std::string describe() const;
};

[[nodiscard]] AnalysisResolution resolutionFor (double sampleRate, size_t windowSamples,
                                                size_t fftSize, double sizeScale);

// ===========================================================================
//  One detected peak
// ===========================================================================

struct ModalPeak
{
    double frequencyHz = 0.0;
    double magnitudeDb = 0.0;

    /** Height above the median of a local neighbourhood, in dB. */
    double prominenceDb = 0.0;

    /** -3 dB width and the Q it implies. Zero when the peak is not resolved. */
    double bandwidthHz = 0.0;
    double q = 0.0;
    bool   widthResolved = false;

    /** Decay time of this peak alone, and its ratio to the broadband RT60. */
    double decaySeconds = 0.0;
    double decayRatioToBroadband = 0.0;
    bool   decayMeasured = false;
    std::string decayStatus = "not attempted";
    std::string decayReason;
    double decayWindowSeconds = 0.0;
    bool   decayFeasible = false;

    /** Per-channel energy in the peak's neighbourhood, and their correlation. */
    double energyLeftDb = 0.0;
    double energyRightDb = 0.0;
    double bandCorrelation = 0.0;
    bool   stereoMeasured = false;

    /** How confident the detector is that this is one peak and not two. */
    bool   ambiguous = false;
    std::string ambiguityReason;
};

/**
    Peaks in one spectrum.

    `localMedianWindowHz` sets the neighbourhood prominence is measured against.
    It is expressed in Hz rather than bins so that zero-padding cannot change
    the answer.
*/
[[nodiscard]] std::vector<ModalPeak> detectPeaks (const std::vector<double>& magnitude,
                                                  const AnalysisResolution& res,
                                                  double lowHz, double highHz,
                                                  double localMedianWindowHz,
                                                  double minProminenceDb);

// ===========================================================================
//  Tracking peaks across a parameter axis
// ===========================================================================

/**
    One peak followed across a sweep.

    A slot may be unmatched at some points. That is reported rather than
    interpolated: a mode that could not be found at one Size is not the same
    statement as a mode that was found at a different frequency.
*/
struct TrackedMode
{
    int id = 0;

    /** Indexed by sweep point. NaN where the mode was not matched. */
    std::vector<double> frequencyHz;
    std::vector<double> prominenceDb;
    std::vector<double> decaySeconds;

    /** Sweep points where assignment was refused rather than guessed. */
    std::vector<int> ambiguousAt;

    int matchedPoints = 0;
};

/**
    Association between two frames' peaks.

    Deliberately not nearest-neighbour. Greedy matching by frequency alone swaps
    identities whenever two peaks approach, which is exactly the case a modal
    sweep is full of. The cost combines frequency distance normalised by the
    local spacing, prominence difference, and - when a previous step is known -
    continuity with the trajectory so far. Assignment is then chosen globally
    over the pair.

    `ambiguityMargin` is how much better the best assignment must be than the
    runner-up before it is accepted. Below that the pair is left unmatched.
*/
struct MatchOptions
{
    double localSpacingHz = 3.0;
    double prominenceWeight = 0.25;
    double continuityWeight = 0.5;
    double maxNormalisedDistance = 3.0;

    /**
        What it costs to leave a track unmatched.

        A real column in the assignment rather than a threshold applied
        afterwards, so the solver trades a bad match against no match instead of
        being forced into one.
    */
    double rejectionCost = 4.0;

    /**
        How much better the optimum must be than the alternative for a track's
        identity to be trusted, in cost units.

        Applied after the global solve, from the dual potentials. A track whose
        regret falls below this keeps its assignment out of the tracked
        trajectory and is reported as ambiguous instead.
    */
    double ambiguityMargin = 0.5;
};

struct MatchResult
{
    /** For each peak in `from`, the index in `to`, or -1 when unmatched. */
    std::vector<int> assignment;
    std::vector<bool> ambiguous;
    int matched = 0;
    int refused = 0;

    /** Total cost of the chosen assignment, dummies included. */
    double totalCost = 0.0;

    /**
        Per-track lower bound on what its identity would cost to change.

        Taken from the solver's dual potentials: for a track assigned to j, the
        smallest reduced cost over every other column is a lower bound on the
        increase in total cost from forcing it elsewhere. Small means the
        optimum was not much better than its alternative, which is what
        ambiguity means here.
    */
    std::vector<double> regret;
};

/**
    Minimum-cost assignment over a rectangular cost matrix.

    A real global optimum - the Jonker-Volgenant form of the Hungarian
    algorithm - not a greedy pass. Exposed so the test suite can check it
    against brute force rather than against itself.

    Rows may exceed columns or the reverse; the matrix is padded internally.
    `INFEASIBLE` marks a pair the gate excluded.

    `u` and `v` are the dual potentials at the optimum. For any pair, the
    reduced cost `cost[i][j] - u[i] - v[j]` is non-negative and is a lower bound
    on how much worse the total would be if that pair were forced.
*/
struct AssignmentResult
{
    std::vector<int> assignment;   /**< Column per row, or -1. */
    std::vector<double> u, v;      /**< Dual potentials. */
    double totalCost = 0.0;
};

inline constexpr double infeasibleCost = 1.0e9;

[[nodiscard]] AssignmentResult solveMinCostAssignment (const std::vector<std::vector<double>>& cost);

[[nodiscard]] MatchResult matchPeaks (const std::vector<ModalPeak>& from,
                                      const std::vector<ModalPeak>& to,
                                      const MatchOptions& options,
                                      const std::vector<double>* predictedTo = nullptr);

/** Follows peaks across an ordered sequence of frames. */
[[nodiscard]] std::vector<TrackedMode> trackAcross (const std::vector<std::vector<ModalPeak>>& frames,
                                                    const MatchOptions& options);

// ===========================================================================
//  Structural classification - never perceptual
// ===========================================================================

enum class ModeClass
{
    sizeTrackingModulationMoving = 0,   /**< A */
    sizeTrackingModulationResistant,    /**< B */
    sizeInsensitiveFixedOrMixed,        /**< C */
    ambiguousUnresolved                 /**< D */
};

[[nodiscard]] const char* describe (ModeClass c) noexcept;

/**
    Classifies from measured behaviour alone.

    No peak is ever labelled good or bad here. Class C in particular says
    "size-insensitive, investigate fixed or mixed structure" and not "not a
    mode": fixed delays, filters, the allpass bank and interaction between modes
    can all hold a feature still while Size moves everything else.
*/
[[nodiscard]] ModeClass classify (double sizeExponent, bool sizeExponentMeasured,
                                  double modulationExcursionHz, double excursionNoiseFloorHz,
                                  bool ambiguous);

/**
    The A/B/C/D rule exactly as the sweep applies it.

    Extracted from ModalSweep so that the offline regeneration applies the same
    one rather than a second copy that can drift. Nothing about the rule has
    changed: an unreliable Size fit or absent modulation evidence is still D,
    an exponent between -1.25 and -0.75 is still A or B according to whether
    the neighbourhood redistributes, and everything else is still C.

    `redistributes` is the caller's modulation finding, not recomputed here:
    spread up and concentration down together, or a matched Mod Depth peak that
    moved by more than three times the analysis variability floor.
*/
[[nodiscard]] ModeClass classifySweepMode (double sizeExponent, bool sizeFitReliable,
                                           bool modulationEvidence,
                                           bool redistributes) noexcept;

// ===========================================================================
//  Render side: one tank setting, deterministically
// ===========================================================================

/** One point on a sweep. Everything not named here is the shipping default. */
struct SweepPoint
{
    double sizeControl = 0.60;
    double decaySeconds = 2.5;
    double modDepthControl = 0.30;

    std::string label;
};

/**
    Impulse response of the wet path at one setting.

    Deterministic by construction: the modulation phase is reset with the tank,
    the source is a unit impulse, and the render length is fixed by the caller.
    Two calls with the same SweepPoint return bit-identical vectors, which is
    what makes the repeatability figure a measurement of the analysis rather
    than of the renderer.
*/
struct ImpulseRender
{
    std::vector<double> left, right;
    double sampleRate = 0.0;
    double sizeScale = 1.0;
    double broadbandRt60 = 0.0;
    bool   rt60Measured = false;
};

[[nodiscard]] ImpulseRender renderImpulse (const SweepPoint& point, double sampleRate,
                                           double seconds);

// ===========================================================================
//  The sweep
// ===========================================================================

/**
    Runs the whole FU-1 sweep and writes the CSVs.

    Raw per-setting measurements are kept alongside the summary: the mode table
    is derived from them and cannot replace them.

    Returns the number of files written, or a negative value on failure.
*/
int runModalSweep (const std::string& directory, double sampleRate = 48000.0);

// ===========================================================================
//  Modal decay estimation
// ===========================================================================

enum class DecayQuality { measured = 0, unresolved, contaminated, insufficientData };

/** A neighbour the caller already knows about, relative to the target. */
struct NeighbourPeak { double frequencyHz = 0.0; double relativeLevelDb = 0.0; };


[[nodiscard]] const char* describe (DecayQuality q) noexcept;

struct ModalDecayEstimate
{
    double rt60FromT20 = 0.0;
    double rt60FromT30 = 0.0;
    double rSquared = 0.0;
    double residualRms = 0.0;   /**< dB, about the fitted line. */
    int    framesUsed = 0;
    double envelopeBandwidthHz = 0.0;
    DecayQuality quality = DecayQuality::insufficientData;
    std::string reason;
};

struct DecayOptions
{
    double windowSeconds = 0.25;
    double hopSeconds = 0.03125;
    double minRSquared = 0.98;
    double maxResidualDb = 1.5;
    int    minFrames = 8;

    /**
        Neighbours the caller knows about, so contamination can be judged
        before the fit rather than inferred from it.

        A high R-squared is not on its own evidence of a clean measurement:
        two unresolved modes at some phase and amplitude combinations produce
        an envelope that is close to a straight line in dB while the slope it
        fits belongs to neither of them. The residual cannot see that. Knowing
        a comparable neighbour sits inside the analysis bandwidth can.
    */
    std::vector<NeighbourPeak> neighbours;

    /**
        How far below the target a neighbour must sit before it is treated as
        harmless. Derived from the adversarial validation, not chosen.
    */
    double contaminationLevelDb = -20.0;
};

/**
    Decay of one mode, from the envelope of a single-frequency DFT bin.

    A sliding Hann-windowed DFT evaluated exactly at the centre frequency is a
    bandpass of about 2/window Hz that adds no decay of its own, which is what
    the previous attempt got wrong. Two modes inside that bandwidth beat, the
    envelope stops being a straight line in dB, and the fit residual rises -
    which is how the estimator says unresolved instead of inventing a number.
*/
[[nodiscard]] ModalDecayEstimate estimateModalDecay (const std::vector<double>& signal,
                                                     double sampleRate, double centreHz,
                                                     const DecayOptions& options = {});

/**
    Window and hop for a mode, chosen from what is being measured.

    Two requirements pull opposite ways. Separating a neighbour needs the
    analysis bandwidth below the local spacing, which wants a long window.
    Fitting a short decay needs enough frames inside it, which wants a short
    one. Where both can be met this returns the window that meets them; where
    they cannot, `feasible` is false and the caller is expected to report
    unresolved rather than pick one and hope.
*/
struct WindowPolicy
{
    double windowSeconds = 0.25;
    double hopSeconds = 0.03125;
    double bandwidthHz = 8.0;
    int    expectedFrames = 0;
    bool   feasible = true;
    std::string note;
};

[[nodiscard]] WindowPolicy windowPolicyFor (double expectedRt60Seconds,
                                            double localSpacingHz);

// ===========================================================================
//  Local modulation response
// ===========================================================================

/**
    What happened in a neighbourhood, when no single peak can be followed.

    Modulation exists to spread a fixed modal peak, so failing to establish a
    1:1 identity across a Mod Depth change is often the modulation working
    rather than the tracker failing. Measuring the neighbourhood instead keeps
    that information: one peak becoming three is a redistribution, and it has
    a signature these fields carry.
*/
struct LocalResponse
{
    double centreHz = 0.0;
    double halfWidthHz = 0.0;      /**< From spacing and expected excursion. */

    double integratedEnergyDb = 0.0;
    double maxProminenceDb = 0.0;
    double spectralCentroidHz = 0.0;
    double spectralSpreadHz = 0.0;
    double energyConcentration = 0.0;  /**< Peak bin over total. 1 = a line. */
    int    peakCount = 0;
    bool   measured = false;
};

[[nodiscard]] LocalResponse measureLocalResponse (const std::vector<double>& magnitude,
                                                  const AnalysisResolution& res,
                                                  double centreHz, double halfWidthHz);

} // namespace avtest
