#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "ConfigResult.h"
#include "FdnCore.h"
#include "FdnTuning.h"
#include "StereoDiffusionNetwork.h"

namespace av::dsp
{

/**
    A true-stereo reverb tank: a short input diffuser, two injection vectors,
    an N x N FDN, and two readout vectors.

        L, R -> input diffusion -> b_L*L + b_R*R -> FDN -> c_L.s , c_R.s

    WHY VECTORS AND NOT CHANNELS
    ----------------------------
    A naive stereo FDN runs two independent tanks, which doubles the cost and
    produces two reverbs rather than one stereo reverb. Here a single tank is
    driven by two different directions in its state space and read out along two
    more. Left and right therefore share the same modes, the same decay and the
    same density — they are the same room — while differing in how that room is
    excited and observed, which is exactly what two ears in one room do.

    The four vectors are sign vectors scaled to unit norm, chosen mutually
    orthogonal so that:

      - the two injections excite independent directions, so a stereo input is
        not collapsed to mono before it reaches the tank;
      - the two readouts observe independent directions, so the outputs
        decorrelate without any mid/side trickery;
      - readout is not a copy of injection, so neither output is dominated by
        the signal that was just pushed in.

    Which four sign vectors is a measured decision — see FdnStereoTests.cpp.
    Orthogonality alone does not make a good choice: the vectors interact with
    the mixing matrix, and some orthogonal sets excite the tank far less evenly
    than others.

    NO WIDENING
    -----------
    There is no mid/side processing anywhere in this class. The stereo image is
    produced by the input diffuser's differing delay patterns, the injection and
    readout geometry, and the delay set. Anything wider than that would be a
    later, explicit choice rather than something baked into the tank.
*/
template <int N = fdn::order>
class StereoFdn
{
public:
    static constexpr int size = N;

    using Core   = FdnCore<N>;
    using Matrix = typename Core::Matrix;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    ConfigResult prepare (double sampleRate, double smoothingSeconds = 0.02)
    {
        if (sampleRate <= 0.0)
            return ConfigResult::invalidArgument;

        currentSampleRate = sampleRate;

        // Capacity covers the longest delay at the widest Size the tank allows,
        // so a later Size change is a configuration call rather than an
        // allocation.
        double longestMs = 0.0;

        for (int i = 0; i < N; ++i)
            longestMs = std::max (longestMs, patternMs[static_cast<size_t> (i)]);

        const auto result = core.prepare (sampleRate, longestMs * fdn::maxSizeScale, smoothingSeconds);

        if (! succeeded (result))
            return result;

        inputDiffusion.setBasePattern (diffusion::fdnInputPattern);
        inputDiffusion.prepare (sampleRate, smoothingSeconds);
        inputDiffusion.setStageCount (4, true);
        inputDiffusion.setDiffusion (0.7, true);

        return applyPattern (sizeScale);
    }

    // -----------------------------------------------------------------------
    //  Configuration — real-time safe.
    // -----------------------------------------------------------------------

    /** Replaces the delay pattern. Changes the loop topology; see FdnCore. */
    [[nodiscard]] ConfigResult setDelayPattern (const double* millisecondsPerLine) noexcept
    {
        if (millisecondsPerLine == nullptr)
            return ConfigResult::invalidArgument;

        std::array<double, static_cast<size_t> (N)> candidate {};

        for (int i = 0; i < N; ++i)
            candidate[static_cast<size_t> (i)] = millisecondsPerLine[i];

        const auto result = core.setDelaysMilliseconds (candidate.data(), sizeScale);

        if (succeeded (result))
            patternMs = candidate;

        return result;
    }

    /** Scales every delay. Steps the tank; MorphingStereoFdn hides that. */
    [[nodiscard]] ConfigResult setSizeScale (double scale) noexcept
    {
        const double clamped = std::clamp (scale, fdn::minSizeScale, fdn::maxSizeScale);
        const auto result = applyPattern (clamped);

        if (succeeded (result))
            sizeScale = clamped;

        return result;
    }

    void setRt60Seconds (double seconds, bool immediate = false) noexcept
    {
        core.setRt60Seconds (seconds, immediate);
    }

    void setDamping (double lowRatio, double highRatio,
                     double lowCornerHz, double highCornerHz,
                     bool immediate = false) noexcept
    {
        core.setDamping (lowRatio, highRatio, lowCornerHz, highCornerHz, immediate);
    }

    [[nodiscard]] bool   isDampingActive()      const noexcept { return core.isDampingActive(); }
    [[nodiscard]] double getDampingLowRatio()   const noexcept { return core.getDampingLowRatio(); }
    [[nodiscard]] double getDampingHighRatio()  const noexcept { return core.getDampingHighRatio(); }

    void setMatrixType (typename Matrix::Type type) noexcept { core.setMatrixType (type); }

    void setModulationDepth (double d, bool immediate = false) noexcept { core.setModulationDepth (d, immediate); }
    void setModulationRateHz (double hz) noexcept                       { core.setModulationRateHz (hz); }
    void setModulationShape (LfoShape shape) noexcept                   { core.setModulationShape (shape); }
    void setModulationSchedule (const modulation::Schedule& s) noexcept { core.setModulationSchedule (s); }
    void setModulationUpdateMode (typename Matrix::UpdateMode m) noexcept { core.setModulationUpdateMode (m); }
    void resetModulation() noexcept                                     { core.resetModulation(); }

    void setAllpassDepth (double d, bool immediate = false) noexcept    { core.setAllpassDepth (d, immediate); }
    void setAllpassRateHz (double hz) noexcept                          { core.setAllpassRateHz (hz); }
    void setAllpassShape (LfoShape shape) noexcept                      { core.setAllpassShape (shape); }
    void setAllpassMode (TimeVaryingAllpass::Mode m) noexcept           { core.setAllpassMode (m); }
    void setAllpassActiveLines (int count) noexcept                     { core.setAllpassActiveLines (count); }
    void setAllpassSectionsPerLine (int count) noexcept                 { core.setAllpassSectionsPerLine (count); }
    void setLoopDelayCorrectionEnabled (bool enabled) noexcept          { core.setLoopDelayCorrectionEnabled (enabled); }
    [[nodiscard]] bool isLoopDelayCorrectionEnabled() const noexcept    { return core.isLoopDelayCorrectionEnabled(); }

    void setInputDiffusionEnabled (bool enabled) noexcept { diffusionEnabled = enabled; }
    void setInputDiffusion (double amount, bool immediate = false) noexcept
    {
        inputDiffusion.setDiffusion (amount, immediate);
    }

    /** Sets the four stereo vectors. Each is a sign pattern; norms are applied
        here, so callers pass +/-1 values. */
    void setStereoVectors (const double* injectL, const double* injectR,
                           const double* readL,   const double* readR) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            injectionL[static_cast<size_t> (i)] = injectL[i] * fdn::signVectorNorm;
            injectionR[static_cast<size_t> (i)] = injectR[i] * fdn::signVectorNorm;
            readoutL  [static_cast<size_t> (i)] = readL[i]   * fdn::signVectorNorm;
            readoutR  [static_cast<size_t> (i)] = readR[i]   * fdn::signVectorNorm;
        }
    }

    void reset() noexcept
    {
        core.reset();
        inputDiffusion.reset();
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    void processSample (double& l, double& r) noexcept
    {
        if (diffusionEnabled)
            inputDiffusion.processSample (l, r);

        double injection[N];
        double state[N];

        for (int i = 0; i < N; ++i)
            injection[i] = injectionL[static_cast<size_t> (i)] * l
                         + injectionR[static_cast<size_t> (i)] * r;

        core.processSample (injection, state);

        double outL = 0.0, outR = 0.0;

        for (int i = 0; i < N; ++i)
        {
            outL += readoutL[static_cast<size_t> (i)] * state[i];
            outR += readoutR[static_cast<size_t> (i)] * state[i];
        }

        l = outL;
        r = outR;
    }

    void processBlock (double* leftData, double* rightData, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            processSample (leftData[n], rightData[n]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] const Core& getCore() const noexcept { return core; }
    [[nodiscard]] Core&       getCore()       noexcept { return core; }

    [[nodiscard]] double getSizeScale()  const noexcept { return sizeScale; }
    [[nodiscard]] double getRt60Seconds() const noexcept { return core.getRt60Seconds(); }
    [[nodiscard]] double getSampleRate() const noexcept { return currentSampleRate; }
    [[nodiscard]] bool   isInputDiffusionEnabled() const noexcept { return diffusionEnabled; }

    [[nodiscard]] double getDelayMilliseconds (int i) const noexcept
    {
        return patternMs[static_cast<size_t> (i)] * sizeScale;
    }

private:
    ConfigResult applyPattern (double scale) noexcept
    {
        return core.setDelaysMilliseconds (patternMs.data(), scale);
    }

    Core core;
    StereoDiffusionNetwork<double> inputDiffusion;

    std::array<double, static_cast<size_t> (N)> patternMs {};
    std::array<double, static_cast<size_t> (N)> injectionL {};
    std::array<double, static_cast<size_t> (N)> injectionR {};
    std::array<double, static_cast<size_t> (N)> readoutL {};
    std::array<double, static_cast<size_t> (N)> readoutR {};

    double sizeScale         = 1.0;
    double currentSampleRate = 48000.0;
    bool   diffusionEnabled  = true;

public:
    /** Fills the pattern and vectors with the shipped tuning. Called by the
        constructor; exposed so tests can restore the defaults. */
    void loadDefaultTuning() noexcept
    {
        for (int i = 0; i < N; ++i)
            patternMs[static_cast<size_t> (i)] =
                fdn::delaysMs[static_cast<size_t> (std::min (i, fdn::order - 1))];

        setStereoVectors (fdn::injectionLeft, fdn::injectionRight,
                          fdn::readoutLeft, fdn::readoutRight);
    }

    StereoFdn() { loadDefaultTuning(); }
};

} // namespace av::dsp
