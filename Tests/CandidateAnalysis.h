#pragma once

#include <string>
#include <vector>

#include "DSP/ModulationArchitecture.h"

/**
    One measurement pass over a tank modulation architecture.

    PHASE 5C has to choose a default architecture, and the choice is not allowed
    to rest on a single number. Both the selection suite and the hybrid-analysis
    suite need the same set of figures for the same candidates, measured the same
    way, so the measurement lives here and is called from both. If it lived in
    one suite and were copied into the other, the two could drift apart and the
    comparison would stop meaning anything.
*/
namespace avtest
{

struct CandidateMetrics
{
    // --- Movement ---
    double modalPersistence  = 0.0;  /**< Fraction of peaks surviving a frame. Lower is better. */
    double latePeriodicity   = 0.0;  /**< Envelope autocorrelation peak/median. Lower is better. */
    double peakLifetime      = 0.0;  /**< Mean seconds a mode survives. Lower is better. */
    double modalMovementHz   = 0.0;  /**< Mean drift of a surviving peak. Higher is better. */
    double spectralFlux      = 0.0;  /**< Mean positive spectral change. Higher is better. */

    // --- Cost ---
    double amDepthDb         = 0.0;  /**< Envelope wobble on a sustained tone. Lower is better. */
    double amSlowDb          = 0.0;  /**< The part below 1 Hz — a swell, not a tremolo. */
    double amFastDb          = 0.0;  /**< The part above 1 Hz — the part that sounds like tremolo. */
    double amDominantHz      = 0.0;  /**< Where the envelope's strongest component sits. */
    double pitchCents        = 0.0;  /**< Peak pitch excursion. Lower is better. */
    double concentrationDb   = 0.0;  /**< Energy on the tone vs its skirt. Higher is better. */

    // --- Decay ---
    double rt60              = 0.0;
    double rt60ErrorPercent  = 0.0;
    double decayLinearity    = 0.0;  /**< R^2 of the Schroeder fit. Higher is better. */
    double energyDriftDb     = 0.0;  /**< Over whole modulation cycles. */

    // --- Stereo ---
    double correlation       = 0.0;
    double balanceDb         = 0.0;
    double monoDownmixDb     = 0.0;
    double worstThirdOctaveDb = 0.0;

    // --- Health ---
    bool   finite            = true;
};

/** Measures one architecture on a fresh tank. Deterministic. */
CandidateMetrics measureCandidate (av::dsp::architecture::Candidate candidate,
                                   double sampleRate, double rt60Seconds = 2.0);

/**
    Whether a candidate is allowed to be the default at all.

    These are the PHASE 5C hard constraints. A candidate that fails any of them
    is out regardless of how well it scores on movement — which is the point of
    separating them from the ranking.
*/
struct ConstraintCheck
{
    bool driftOk        = false;   /**< Added long-run drift within +/- 0.1 dB. */
    bool rt60Ok         = false;   /**< Within 3 % of target. */
    bool monoOk         = false;   /**< Worst third-octave cancellation under 4 dB. */
    bool pitchOk        = false;   /**< Peak pitch under 3 cents. */
    bool finiteOk       = false;   /**< No NaN or Inf. */
    bool chorusOk       = false;   /**< Spectral concentration not collapsed. */

    [[nodiscard]] bool passes() const noexcept
    {
        return driftOk && rt60Ok && monoOk && pitchOk && finiteOk && chorusOk;
    }

    [[nodiscard]] std::string firstFailure() const;
};

/**
    How far spectral concentration may fall below the static tank's.

    An absolute floor would be meaningless: the number spectralConcentrationDb
    returns depends on the core and skirt widths chosen for the analysis, so a
    threshold written as a bare decibel figure would be a threshold on the
    analysis settings rather than on the reverb. What matters is how much of the
    static tank's concentration a candidate gives away, and PHASE 5B established
    the scale — concentration held steady up to 0.40 Hz and collapsed by tens of
    decibels once the modulation became chorus.
*/
inline constexpr double chorusConcentrationDropDb = 10.0;

/** `staticConcentrationDb` is candidate A's, measured in the same run. */
ConstraintCheck checkConstraints (const CandidateMetrics& m, double staticConcentrationDb);

} // namespace avtest
