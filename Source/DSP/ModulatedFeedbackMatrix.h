#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "FeedbackMatrix.h"
#include "GivensRotation.h"
#include "Lfo.h"
#include "ModulationTuning.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    A feedback matrix that moves, and is orthogonal at every instant.

        A(t) = R_1(t) * R_2(t) * ... * R_k(t) * H

    H is the fixed normalised Hadamard matrix; each R_i is a Givens rotation on
    one pair of delay lines, driven by its own LFO. Every factor is orthogonal,
    so the product is orthogonal exactly — not approximately, not on average,
    and not subject to drift.

    WHY NOT PERTURB THE MATRIX DIRECTLY
    -----------------------------------
    The obvious approach is to add a little noise to the Hadamard coefficients.
    It does not work: an orthogonal matrix plus a small arbitrary matrix is not
    orthogonal, so the network would gain or lose a little energy on every
    circulation and the error would compound once per sample forever. Building
    the matrix from rotations makes orthogonality structural.

    WHY THE STATE IS REBUILT, NOT UPDATED
    -------------------------------------
    The rotation coefficients are recomputed from the current angles every time,
    never nudged from their previous values. Iteratively modifying a matrix is
    how floating-point drift accumulates in a system that runs for hours; a
    fresh (cos, sin) from a fresh angle cannot drift no matter how long it runs.
    The long-running orthogonality test exists to prove that.

    WHAT IT DOES TO THE SOUND
    -------------------------
    The delay lengths never change, so this is not a pitch modulation. What
    moves is how energy is shared between the lines, which moves where the
    network's modes sit and how long any one of them persists. The intended
    audible result is that the tail stops ringing at fixed pitches; the intended
    inaudible result is that nothing else changes, which is why energy and RT60
    are measured as carefully as periodicity.

    Real-time safe after prepare(): no allocation, no locking, no branching on
    anything the audio thread cannot predict.
*/
template <int N = fdn::order>
class ModulatedFeedbackMatrix
{
public:
    using Base = FeedbackMatrix<N>;
    using Type = typename Base::Type;

    /** How often the rotation angles are recomputed. */
    enum class UpdateMode
    {
        perSample,   /**< Exact, two trigonometric calls per rotation per sample. */
        controlRate  /**< Recomputed every N samples, interpolated and renormalised. */
    };

    // -----------------------------------------------------------------------
    //  Setup
    // -----------------------------------------------------------------------

    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (int i = 0; i < modulation::maxRotations; ++i)
        {
            auto& lane = lanes[static_cast<size_t> (i)];

            lane.prepare (currentSampleRate);
            lane.setShape (shape);
            lane.setSeed (modulation::laneSeeds[i]);
            lane.setPhaseOffset (modulation::lanePhaseOffsets[i]);
            lane.reset();
        }

        angleSmoother.prepare (currentSampleRate, 0.02);
        angleSmoother.snapTo (modulation::angleForDepth (depth));

        applyRates();
        reset();
    }

    /** Restarts the LFOs and the rotation state. Does not touch the tank. */
    void reset() noexcept
    {
        for (auto& lane : lanes)
            lane.reset();

        sampleCounter = 0;

        for (int i = 0; i < modulation::maxRotations; ++i)
        {
            cosines[static_cast<size_t> (i)] = 1.0;
            sines[static_cast<size_t> (i)]   = 0.0;
            targetCosines[static_cast<size_t> (i)] = 1.0;
            targetSines[static_cast<size_t> (i)]   = 0.0;
            angles[static_cast<size_t> (i)] = 0.0;
        }

        angleSmoother.snapToTarget();
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setType (Type t) noexcept { base.setType (t); }
    [[nodiscard]] Type getType() const noexcept { return base.getType(); }

    void setSchedule (const modulation::Schedule& s) noexcept
    {
        schedule = s;
        activeRotations = std::min (s.count, modulation::maxRotations);
    }

    [[nodiscard]] const modulation::Schedule& getSchedule() const noexcept { return schedule; }

    /** 0..1. Exactly zero is a true bypass. */
    void setDepth (double zeroToOne, bool immediate = false) noexcept
    {
        depth = std::clamp (zeroToOne, 0.0, 1.0);

        const double target = modulation::angleForDepth (depth);

        if (immediate) angleSmoother.snapTo (target);
        else           angleSmoother.setTarget (target);
    }

    /** Base rate of the slowest lane. The others are fixed multiples of it. */
    void setRateHz (double hz) noexcept
    {
        baseRate = std::clamp (hz, modulation::minRateHz, modulation::maxRateHz);
        applyRates();
    }

    /** Changing the shape keeps every lane's phase, so it cannot click. */
    void setShape (LfoShape s) noexcept
    {
        shape = s;

        for (auto& lane : lanes)
            lane.setShape (s);
    }

    void setUpdateMode (UpdateMode mode) noexcept { updateMode = mode; }
    [[nodiscard]] UpdateMode getUpdateMode() const noexcept { return updateMode; }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Advances the modulation one sample and applies A(t) in place. */
    void process (double* v) noexcept
    {
        advance();
        applyCurrent (v);
    }

    /** Applies the current A(t) without advancing. For analysis. */
    void applyCurrent (double* v) const noexcept
    {
        base.process (v);

        if (bypassed)
            return;

        // A(t) = R_1 * R_2 * ... * R_k * H, so H acts first and the rotations
        // are applied from R_k down to R_1.
        for (int i = activeRotations - 1; i >= 0; --i)
            applyGivens (v, schedule.pairs[i],
                         cosines[static_cast<size_t> (i)],
                         sines[static_cast<size_t> (i)]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    /** The current A(t) as N*N coefficients, row-major. */
    void getCurrentMatrix (double* out) const noexcept
    {
        for (int column = 0; column < N; ++column)
        {
            double basis[N] {};
            basis[column] = 1.0;

            applyCurrent (basis);

            for (int row = 0; row < N; ++row)
                out[row * N + column] = basis[row];
        }
    }

    [[nodiscard]] double getDepth()          const noexcept { return depth; }
    [[nodiscard]] double getRateHz()         const noexcept { return baseRate; }
    [[nodiscard]] LfoShape getShape()        const noexcept { return shape; }
    [[nodiscard]] int    getActiveRotations() const noexcept { return activeRotations; }
    [[nodiscard]] double getCurrentAngle (int i) const noexcept { return angles[static_cast<size_t> (i)]; }
    [[nodiscard]] double getMaxAngle()       const noexcept { return angleSmoother.getCurrentValue(); }
    [[nodiscard]] double getLaneRateHz (int i) const noexcept
    {
        return baseRate * modulation::laneRateFactors[i];
    }

    /** The fixed base matrix, unmodulated. For comparison in tests. */
    void getStaticCoefficients (double* out) const noexcept { base.getCoefficients (out); }

    [[nodiscard]] static constexpr const char* describe (Type t) noexcept
    {
        return Base::describe (t);
    }

private:
    void applyRates() noexcept
    {
        for (int i = 0; i < modulation::maxRotations; ++i)
            lanes[static_cast<size_t> (i)].setRateHz (baseRate * modulation::laneRateFactors[i]);
    }

    void advance() noexcept
    {
        const double maxAngle = angleSmoother.getNextValue();

        // A genuine bypass at zero depth, not just an angle of zero. Every lane
        // still advances its phase so that coming back out of bypass does not
        // jump, but the waveform is not evaluated and no rotation is applied:
        // the trigonometry was about to be multiplied by zero, and applying a
        // rotation of zero radians is eight multiply-adds that change nothing.
        bypassed = maxAngle <= 0.0;

        if (bypassed)
        {
            for (int i = 0; i < activeRotations; ++i)
            {
                lanes[static_cast<size_t> (i)].skipSample();

                angles[static_cast<size_t> (i)] = 0.0;
                cosines[static_cast<size_t> (i)] = 1.0;
                sines[static_cast<size_t> (i)] = 0.0;
                targetCosines[static_cast<size_t> (i)] = 1.0;
                targetSines[static_cast<size_t> (i)] = 0.0;
            }

            ++sampleCounter;
            return;
        }

        // The LFOs run every sample whatever the update mode, so a change of
        // update rate cannot jump their phase.
        for (int i = 0; i < activeRotations; ++i)
            angles[static_cast<size_t> (i)] =
                maxAngle * lanes[static_cast<size_t> (i)].getNextValue();

        // The counter is free-running, not per-block, so a control-rate update
        // lands on the same absolute sample no matter how the host segments
        // the stream.
        const bool recompute = updateMode == UpdateMode::perSample
                            || (sampleCounter % modulation::controlRatePeriod) == 0;

        ++sampleCounter;

        if (recompute)
        {
            for (int i = 0; i < activeRotations; ++i)
            {
                const double angle = angles[static_cast<size_t> (i)];

                targetCosines[static_cast<size_t> (i)] = std::cos (angle);
                targetSines[static_cast<size_t> (i)]   = std::sin (angle);

                if (updateMode == UpdateMode::perSample)
                {
                    cosines[static_cast<size_t> (i)] = targetCosines[static_cast<size_t> (i)];
                    sines[static_cast<size_t> (i)]   = targetSines[static_cast<size_t> (i)];
                }
            }
        }

        if (updateMode == UpdateMode::controlRate)
        {
            constexpr double step = 1.0 / static_cast<double> (modulation::controlRatePeriod);

            for (int i = 0; i < activeRotations; ++i)
            {
                auto& c = cosines[static_cast<size_t> (i)];
                auto& s = sines[static_cast<size_t> (i)];

                c += step * (targetCosines[static_cast<size_t> (i)] - c);
                s += step * (targetSines[static_cast<size_t> (i)] - s);

                // Interpolating between two points on the unit circle lands
                // inside it, and an under-unit pair is a contraction rather
                // than a rotation. One reciprocal square root puts it back.
                normaliseRotation (c, s);
            }
        }
    }

    Base base;

    modulation::Schedule schedule = modulation::defaultSchedule;
    int activeRotations = modulation::defaultSchedule.count;

    std::array<Lfo, static_cast<size_t> (modulation::maxRotations)> lanes;

    std::array<double, static_cast<size_t> (modulation::maxRotations)> angles {};
    std::array<double, static_cast<size_t> (modulation::maxRotations)> cosines {};
    std::array<double, static_cast<size_t> (modulation::maxRotations)> sines {};
    std::array<double, static_cast<size_t> (modulation::maxRotations)> targetCosines {};
    std::array<double, static_cast<size_t> (modulation::maxRotations)> targetSines {};

    LinearSmoother<double> angleSmoother;

    UpdateMode updateMode = UpdateMode::perSample;
    LfoShape   shape      = LfoShape::sine;
    bool       bypassed   = true;

    double depth             = 0.0;
    double baseRate          = modulation::defaultRateHz;
    double currentSampleRate = 48000.0;
    long long sampleCounter  = 0;
};

} // namespace av::dsp
