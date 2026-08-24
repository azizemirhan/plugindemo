#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "DiffusionNetwork.h"
#include "DiffusionTuning.h"

namespace av::dsp
{

/**
    Two diffusion chains that produce a stereo field from the structure of the
    network itself, with no mid/side widening anywhere.

    HOW THE CHANNELS ARE MADE DIFFERENT
    -----------------------------------
    Three independent mechanisms, so that none of them has to be pushed far
    enough to cause trouble on its own:

      1. Different delay sets. The right chain uses its own millisecond values,
         not a scaled copy of the left, so the two chains emit echoes at
         different times from the first sample onwards. This is what actually
         decorrelates a mono input.

      2. Different polarity per stage. Negating a stage's coefficient leaves its
         magnitude response untouched and changes only phase, so it is free
         decorrelation.

      3. An optional energy-preserving rotation between the channels, inserted
         partway along the chain:

             [L']   [ cos t   sin t ] [L]
             [R'] = [-sin t   cos t ] [R]

         The matrix is orthogonal, so the total energy across the pair is
         unchanged at any angle. For a mono input it does nothing on its own —
         both inputs would be identical — which is why it is a refinement of
         mechanisms 1 and 2 rather than a substitute for them.

    MONO COMPATIBILITY
    ------------------
    Any pair of different allpass chains has a downmix problem: both outputs are
    magnitude-flat, but (H_L + H_R)/2 is not, and wherever the two chains happen
    to be in antiphase the sum notches. Sharing the leading stages is the lever
    that controls it: a shared prefix H_C factors out of both chains, so only
    the difference between the non-shared parts survives the sum.

    That lever does not behave the way it looks like it should. Measurement
    (see DiffusionTuning.h) shows the downmix dip is not monotonic in the number
    of shared stages — two and three shared stages measure considerably worse
    than none. Which delays are left differing matters more than how many, so
    the setting is chosen from the measured table rather than from the argument.

    Mono compatibility is judged on third-octave band energy, not on raw FFT
    bins. Any two decorrelated signals produce arbitrarily deep nulls in
    individual bins; what a listener hears is the band average.
*/
template <typename SampleType = double, Interpolation kernel = Interpolation::lagrange3>
class StereoDiffusionNetwork
{
public:
    static constexpr int maxStages = diffusion::maxStages;

    using Network = DiffusionNetwork<SampleType, kernel>;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    /** Selects which tuning table the network is built from. Call before
        prepare(), because prepare() sizes its storage from it. */
    void setBasePattern (const diffusion::StereoPattern& pattern) noexcept
    {
        basePattern = pattern;
    }

    void prepare (double sampleRate, double smoothingSeconds = 0.02)
    {
        // The patterns must be in place before prepare(), because each chain
        // sizes its delay storage from the pattern it holds at that moment.
        (void) rebuildPatterns (true);

        // Both chains reserve for the longer of the two patterns at every
        // stage. setSharedStages() copies left delays into the right chain, so
        // the right chain has to be able to hold them; sizing each chain from
        // its own pattern would make a later sharing change fail for no reason
        // the caller could have predicted.
        std::array<double, static_cast<size_t> (maxStages)> capacity {};

        for (int i = 0; i < maxStages; ++i)
            capacity[static_cast<size_t> (i)] =
                std::max (basePattern.leftMs[i], basePattern.rightMs[i]) * diffusion::maxDelayScale;

        left.prepare (sampleRate, smoothingSeconds, capacity.data());
        right.prepare (sampleRate, smoothingSeconds, capacity.data());

        rotationCos.prepare (sampleRate, smoothingSeconds);
        rotationSin.prepare (sampleRate, smoothingSeconds);
        applyRotation (true);

        currentSampleRate = sampleRate;
    }

    void reset() noexcept
    {
        left.reset();
        right.reset();
        rotationCos.snapToTarget();
        rotationSin.snapToTarget();
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setStageCount (int count, bool immediate = false) noexcept
    {
        left.setStageCount (count, immediate);
        right.setStageCount (count, immediate);
    }

    void setDiffusion (double zeroToOne, bool immediate = false) noexcept
    {
        left.setDiffusion (zeroToOne, immediate);
        right.setDiffusion (zeroToOne, immediate);
    }

    [[nodiscard]] ConfigResult setDelayScale (double scale, bool immediate = false) noexcept
    {
        const auto l = left.setDelayScale (scale, immediate);
        const auto r = right.setDelayScale (scale, immediate);

        return succeeded (l) ? r : l;
    }

    /** Leading stages that are identical in both channels. Trades stereo spread
        for mono compatibility. Rebuilding the pattern is allocation-free. */
    [[nodiscard]] ConfigResult setSharedStages (int count, bool immediate = false) noexcept
    {
        const int clamped = std::clamp (count, 0, maxStages);

        if (clamped == sharedStages)
            return ConfigResult::ok;

        sharedStages = clamped;
        return rebuildPatterns (immediate);
    }

    /** Replaces both channels' patterns. Refused as a whole if either exceeds
        the capacity reserved by prepare(). */
    [[nodiscard]] ConfigResult setDelayPatterns (const double* leftMs, const double* leftSigns,
                                                 const double* rightMs, const double* rightSigns,
                                                 bool immediate = false) noexcept
    {
        const auto l = left.setDelayPattern (leftMs, leftSigns, immediate);

        if (! succeeded (l))
            return l;

        return right.setDelayPattern (rightMs, rightSigns, immediate);
    }

    /** 0 = no coupling, 1 = a 45 degree rotation (equal blend, still lossless). */
    void setCrossCoupling (double amount, bool immediate = false) noexcept
    {
        crossCoupling = std::clamp (amount, 0.0, 1.0);
        applyRotation (immediate);
    }

    /** Stage index after which the rotation is applied. */
    void setCrossCouplingStage (int stage) noexcept
    {
        crossCouplingStage = std::clamp (stage, 0, maxStages);
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    void processSample (SampleType& l, SampleType& r) noexcept
    {
        left.advanceSample();
        right.advanceSample();

        const int activeL = left.getProcessedStageCount();
        const int activeR = right.getProcessedStageCount();
        const int split   = std::min (crossCouplingStage, std::min (activeL, activeR));

        SampleType a = left.processStages (l, 0, split);
        SampleType b = right.processStages (r, 0, split);

        const double c = rotationCos.getNextValue();
        const double s = rotationSin.getNextValue();

        const double rotatedA =  c * static_cast<double> (a) + s * static_cast<double> (b);
        const double rotatedB = -s * static_cast<double> (a) + c * static_cast<double> (b);

        l = left.processStages (static_cast<SampleType> (rotatedA), split, activeL);
        r = right.processStages (static_cast<SampleType> (rotatedB), split, activeR);
    }

    void processBlock (SampleType* leftData, SampleType* rightData, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            processSample (leftData[n], rightData[n]);
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] const Network& getLeft()  const noexcept { return left; }
    [[nodiscard]] const Network& getRight() const noexcept { return right; }
    [[nodiscard]] Network&       getLeft()        noexcept { return left; }
    [[nodiscard]] Network&       getRight()       noexcept { return right; }

    [[nodiscard]] int    getSharedStages()      const noexcept { return sharedStages; }
    [[nodiscard]] int    getCrossCouplingStage() const noexcept { return crossCouplingStage; }
    [[nodiscard]] double getCrossCoupling()     const noexcept { return crossCoupling; }
    [[nodiscard]] double getSampleRate()        const noexcept { return currentSampleRate; }

private:
    ConfigResult rebuildPatterns (bool immediate) noexcept
    {
        // Stack arrays: no allocation, safe to call from the audio thread.
        std::array<double, static_cast<size_t> (maxStages)> rightDelays {};
        std::array<double, static_cast<size_t> (maxStages)> rightSigns {};

        for (int i = 0; i < maxStages; ++i)
        {
            const bool shared = i < sharedStages;
            const auto k = static_cast<size_t> (i);

            rightDelays[k] = shared ? basePattern.leftMs[k]    : basePattern.rightMs[k];
            rightSigns[k]  = shared ? basePattern.leftSigns[k] : basePattern.rightSigns[k];
        }

        const auto l = left.setDelayPattern (basePattern.leftMs, basePattern.leftSigns, immediate);

        if (! succeeded (l))
            return l;

        return right.setDelayPattern (rightDelays.data(), rightSigns.data(), immediate);
    }

    void applyRotation (bool immediate) noexcept
    {
        constexpr double quarterPi = 0.78539816339744830962;

        const double theta = crossCoupling * quarterPi;
        const double c = std::cos (theta);
        const double s = std::sin (theta);

        if (immediate) { rotationCos.snapTo (c); rotationSin.snapTo (s); }
        else           { rotationCos.setTarget (c); rotationSin.setTarget (s); }
    }

    Network left, right;

    diffusion::StereoPattern basePattern = diffusion::longPattern;

    LinearSmoother<double> rotationCos, rotationSin;

    int    sharedStages       = diffusion::defaultSharedStages;
    int    crossCouplingStage = diffusion::defaultCrossCouplingStage;
    double crossCoupling      = diffusion::defaultCrossCoupling;
    double currentSampleRate  = 48000.0;
};

} // namespace av::dsp
