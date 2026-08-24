#pragma once

#include "AllpassDiffuser.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    One stage of a diffusion network: an allpass section plus the controls a
    network needs to change its shape without clicking.

    ENGAGEMENT
    ----------
    A network with a variable stage count has to be able to take a stage out of
    the chain. Simply skipping it would step the signal, so a stage crossfades
    between its input and its allpass output instead:

        y = x + w * (allpass(x) - x)

    At w = 1 the result is exactly the allpass; at w = 0 it is exactly the input,
    bit for bit. In between it is the sum of a dry and an allpassed copy, which
    is a comb filter — unavoidable for any dry/wet blend of an allpass, and the
    reason engagement is used only for structural changes (stage count) and
    never for the Diffusion control itself. Diffusion moves the coefficient,
    which stays magnitude-flat at every setting.

    The allpass keeps running while disengaged. That costs nothing in
    predictability (CPU is constant regardless of stage count, which is what a
    real-time budget wants) and means the state is already warm when the stage
    is engaged again, so re-engaging does not fade in from silence.
*/
template <typename SampleType = double, Interpolation kernel = Interpolation::lagrange3>
class DiffusionStage
{
public:
    using Allpass = AllpassDiffuser<SampleType, kernel>;

    void prepare (double sampleRate, double maxDelaySeconds,
                  double smoothingSeconds = 0.02)
    {
        allpass.prepare (sampleRate, maxDelaySeconds, smoothingSeconds);

        // Engagement is a structural change, and the dry/allpass blend it
        // passes through is a comb filter. A fade four times longer than the
        // parameter smoothers keeps each step of that comb below the signal's
        // own slew rate — measured in the stage-count suite.
        engagement.prepare (sampleRate, smoothingSeconds * 4.0);
        engagement.snapTo (desiredEngagement);
    }

    void reset() noexcept
    {
        allpass.reset();
        engagement.snapToTarget();
    }

    void setDelaySeconds (double s, bool immediate = false) noexcept      { allpass.setDelaySeconds (s, immediate); }
    void setDelayMilliseconds (double ms, bool immediate = false) noexcept { allpass.setDelayMilliseconds (ms, immediate); }
    void setDelaySamples (double s, bool immediate = false) noexcept      { allpass.setDelaySamples (s, immediate); }
    void setCoefficient (double g, bool immediate = false) noexcept       { allpass.setCoefficient (g, immediate); }

    void setQuantisation (typename Allpass::Quantisation q) noexcept      { allpass.setQuantisation (q); }

    void setEngaged (bool engaged, bool immediate = false) noexcept
    {
        desiredEngagement = engaged ? 1.0 : 0.0;

        if (immediate) engagement.snapTo (desiredEngagement);
        else           engagement.setTarget (desiredEngagement);
    }

    [[nodiscard]] SampleType processSample (SampleType x) noexcept
    {
        const double w   = engagement.getNextValue();
        const double wet = static_cast<double> (allpass.processSample (x));

        return static_cast<SampleType> (static_cast<double> (x) + w * (wet - static_cast<double> (x)));
    }

    void processBlock (SampleType* data, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            data[n] = processSample (data[n]);
    }

    [[nodiscard]] const Allpass& getAllpass() const noexcept { return allpass; }
    [[nodiscard]] Allpass&       getAllpass()       noexcept { return allpass; }

    [[nodiscard]] double getEngagement()    const noexcept { return engagement.getCurrentValue(); }
    [[nodiscard]] bool   isFullyEngaged()   const noexcept { return engagement.getCurrentValue() >= 1.0; }
    [[nodiscard]] bool   isFullyBypassed()  const noexcept { return engagement.getCurrentValue() <= 0.0; }
    [[nodiscard]] bool   isSmoothing()      const noexcept { return engagement.isSmoothing() || allpass.isSmoothing(); }

    [[nodiscard]] double getDelaySamples() const noexcept { return allpass.getDelaySamples(); }
    [[nodiscard]] double getCoefficient()  const noexcept { return allpass.getCoefficient(); }

private:
    Allpass allpass;
    LinearSmoother<double> engagement;
    double desiredEngagement = 1.0;
};

} // namespace av::dsp
