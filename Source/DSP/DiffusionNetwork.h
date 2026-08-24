#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "ConfigResult.h"
#include "DiffusionStage.h"
#include "DiffusionTuning.h"

namespace av::dsp
{

/**
    A series chain of allpass diffusion stages.

    Each stage is magnitude-flat, so the chain is too: the network changes the
    time structure of a signal without changing its spectrum. Cascading is what
    builds density — a single allpass emits echoes at multiples of its own
    delay, while N cascaded allpasses emit them at every sum of multiples of all
    N delays, which is why the delay set matters more than the stage count.

    CAPACITY AND STAGE COUNT
    ------------------------
    Storage for the maximum stage count is reserved in prepare(), so changing
    the stage count at run time never allocates. Only the active stages are
    processed, so the CPU cost does scale with the count. A stage being switched
    out keeps being processed until its crossfade has finished, tracked by a
    sample counter rather than by polling every stage.

    BLOCK SIZE
    ----------
    Processing is strictly per sample with no per-block storage, so any block
    size works — including one larger than the size announced to prepare(), and
    one that changes on every callback.
*/
template <typename SampleType = double, Interpolation kernel = Interpolation::lagrange3>
class DiffusionNetwork
{
public:
    static constexpr int maxStages = diffusion::maxStages;

    using Stage = DiffusionStage<SampleType, kernel>;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    /**
        Reserves storage.

        `capacityMsPerStage` states the longest delay each stage will ever be
        asked for. When null, the capacity is the pattern currently held times
        diffusion::maxDelayScale. Any later setDelayPattern() beyond that is
        refused outright rather than clamped — see ConfigResult.
    */
    void prepare (double sampleRate, double smoothingSeconds = 0.02,
                  const double* capacityMsPerStage = nullptr)
    {
        currentSampleRate  = sampleRate;
        smoothingTime      = smoothingSeconds;

        for (int i = 0; i < maxStages; ++i)
            capacityMs[static_cast<size_t> (i)] =
                capacityMsPerStage != nullptr ? capacityMsPerStage[i]
                                              : delaysMs[static_cast<size_t> (i)] * diffusion::maxDelayScale;

        // The engagement crossfade is four times the parameter smoothing time
        // (see DiffusionStage); the shrink countdown must outlast it.
        engagementRampSamples = static_cast<int> (std::ceil (sampleRate * smoothingSeconds * 4.0)) + 2;

        for (int i = 0; i < maxStages; ++i)
            stages[static_cast<size_t> (i)].prepare (sampleRate,
                                                     capacityMs[static_cast<size_t> (i)] * 0.001,
                                                     smoothingSeconds);

        isPrepared = true;

        applyTuning (true);
        applyEngagement (true);

        processedStages = stageCount;
        shrinkCountdown = 0;
    }

    /** Clears every stage without reallocating. Real-time safe. */
    void reset() noexcept
    {
        for (auto& s : stages)
            s.reset();

        processedStages = stageCount;
        shrinkCountdown = 0;
    }

    /**
        Replaces the delay pattern. Values are in milliseconds and are applied
        through the stages' delay smoothers, so this is click-free and safe on
        the audio thread.

        A pattern that would exceed the capacity reserved by prepare() is
        refused: nothing is applied and the previous pattern stays in effect.
        Before prepare() the pattern is simply recorded, and prepare() will size
        the storage from it.
    */
    [[nodiscard]] ConfigResult setDelayPattern (const double* millisecondsPerStage,
                                                const double* signsPerStage,
                                                bool immediate = false) noexcept
    {
        if (millisecondsPerStage == nullptr)
            return ConfigResult::invalidArgument;

        if (isPrepared)
        {
            for (int i = 0; i < maxStages; ++i)
                if (millisecondsPerStage[i] * delayScale > capacityMs[static_cast<size_t> (i)])
                    return ConfigResult::exceedsCapacity;
        }

        for (int i = 0; i < maxStages; ++i)
        {
            delaysMs[static_cast<size_t> (i)] = millisecondsPerStage[i];
            signs[static_cast<size_t> (i)]    = signsPerStage != nullptr ? signsPerStage[i] : 1.0;
        }

        applyTuning (immediate);
        return ConfigResult::ok;
    }

    /** Longest delay stage `i` can be asked for, in milliseconds. */
    [[nodiscard]] double getCapacityMilliseconds (int stage) const noexcept
    {
        return capacityMs[static_cast<size_t> (stage)];
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setStageCount (int count, bool immediate = false) noexcept
    {
        const int clamped = std::clamp (count, 0, maxStages);

        if (clamped == stageCount)
            return;

        const bool shrinking = clamped < stageCount;
        stageCount = clamped;

        applyEngagement (immediate);

        if (immediate)
        {
            processedStages = stageCount;
            shrinkCountdown = 0;
        }
        else if (shrinking)
        {
            // Keep processing the outgoing stages until they have faded out.
            shrinkCountdown = engagementRampSamples;
        }
        else
        {
            processedStages = std::max (processedStages, stageCount);
            shrinkCountdown = 0;
        }
    }

    /** 0..1. See DiffusionTuning.h for the mapping and why it is not linear. */
    void setDiffusion (double zeroToOne, bool immediate = false) noexcept
    {
        diffusionAmount = std::clamp (zeroToOne, 0.0, 1.0);
        applyCoefficients (immediate);
    }

    /** Multiplies every delay. Refused if it would exceed the reserved capacity. */
    [[nodiscard]] ConfigResult setDelayScale (double scale, bool immediate = false) noexcept
    {
        const double clamped = std::clamp (scale, 0.05, diffusion::maxDelayScale);

        if (isPrepared)
            for (int i = 0; i < maxStages; ++i)
                if (delaysMs[static_cast<size_t> (i)] * clamped > capacityMs[static_cast<size_t> (i)])
                    return ConfigResult::exceedsCapacity;

        delayScale = clamped;
        applyDelays (immediate);
        return ConfigResult::ok;
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Advances the network's own per-sample bookkeeping. Call once per sample
        before processStages(), or use processSample() which does both. */
    void advanceSample() noexcept
    {
        if (shrinkCountdown > 0 && --shrinkCountdown == 0)
            processedStages = stageCount;
    }

    /** Runs stages [first, last) on one sample. */
    [[nodiscard]] SampleType processStages (SampleType x, int first, int last) noexcept
    {
        for (int i = first; i < last; ++i)
            x = stages[static_cast<size_t> (i)].processSample (x);

        return x;
    }

    [[nodiscard]] SampleType processSample (SampleType x) noexcept
    {
        advanceSample();
        return processStages (x, 0, processedStages);
    }

    void processBlock (SampleType* data, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            data[n] = processSample (data[n]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] int    getStageCount()          const noexcept { return stageCount; }
    [[nodiscard]] int    getProcessedStageCount() const noexcept { return processedStages; }
    [[nodiscard]] double getDiffusion()           const noexcept { return diffusionAmount; }
    [[nodiscard]] double getDelayScale()          const noexcept { return delayScale; }
    [[nodiscard]] double getSampleRate()          const noexcept { return currentSampleRate; }
    [[nodiscard]] double getSmoothingSeconds()    const noexcept { return smoothingTime; }

    [[nodiscard]] const Stage& getStage (int i) const noexcept { return stages[static_cast<size_t> (i)]; }
    [[nodiscard]] Stage&       getStage (int i)       noexcept { return stages[static_cast<size_t> (i)]; }

    /** Sum of the active stages' loop delays — the chain's group delay floor. */
    [[nodiscard]] double getTotalDelaySamples() const noexcept
    {
        double total = 0.0;

        for (int i = 0; i < stageCount; ++i)
            total += stages[static_cast<size_t> (i)].getDelaySamples();

        return total;
    }

    [[nodiscard]] double getDelayMilliseconds (int stage) const noexcept
    {
        return delaysMs[static_cast<size_t> (stage)] * delayScale;
    }

    [[nodiscard]] bool isSmoothing() const noexcept
    {
        for (const auto& s : stages)
            if (s.isSmoothing())
                return true;

        return shrinkCountdown > 0;
    }

private:
    void applyTuning (bool immediate) noexcept
    {
        applyDelays (immediate);
        applyCoefficients (immediate);
    }

    void applyDelays (bool immediate) noexcept
    {
        for (int i = 0; i < maxStages; ++i)
            stages[static_cast<size_t> (i)].setDelayMilliseconds (
                delaysMs[static_cast<size_t> (i)] * delayScale, immediate);
    }

    void applyCoefficients (bool immediate) noexcept
    {
        const double magnitude = diffusion::coefficientForDiffusion (diffusionAmount);

        for (int i = 0; i < maxStages; ++i)
            stages[static_cast<size_t> (i)].setCoefficient (
                magnitude * signs[static_cast<size_t> (i)], immediate);
    }

    void applyEngagement (bool immediate) noexcept
    {
        for (int i = 0; i < maxStages; ++i)
            stages[static_cast<size_t> (i)].setEngaged (i < stageCount, immediate);
    }

    std::array<Stage, static_cast<size_t> (maxStages)> stages;

    std::array<double, static_cast<size_t> (maxStages)> delaysMs
    {
        diffusion::leftDelaysMs[0], diffusion::leftDelaysMs[1],
        diffusion::leftDelaysMs[2], diffusion::leftDelaysMs[3],
        diffusion::leftDelaysMs[4], diffusion::leftDelaysMs[5],
        diffusion::leftDelaysMs[6], diffusion::leftDelaysMs[7]
    };

    std::array<double, static_cast<size_t> (maxStages)> signs
    {
        diffusion::leftSigns[0], diffusion::leftSigns[1],
        diffusion::leftSigns[2], diffusion::leftSigns[3],
        diffusion::leftSigns[4], diffusion::leftSigns[5],
        diffusion::leftSigns[6], diffusion::leftSigns[7]
    };

    std::array<double, static_cast<size_t> (maxStages)> capacityMs {};

    bool   isPrepared      = false;
    int    stageCount      = diffusion::defaultStageCount;
    int    processedStages = diffusion::defaultStageCount;
    int    shrinkCountdown = 0;
    int    engagementRampSamples = 2;

    double diffusionAmount   = 0.7;
    double delayScale        = 1.0;
    double currentSampleRate = 48000.0;
    double smoothingTime     = 0.02;
};

} // namespace av::dsp
