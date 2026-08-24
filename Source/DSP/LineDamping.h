#pragma once

#include "DampingTuning.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    One FDN line's damping: a first-order low shelf followed by a first-order
    high shelf.

    Both shelves are built from a one-pole lowpass, which keeps the arithmetic
    to two multiply-adds each and gives the whole thing the properties argued
    for in DampingTuning.h — |H| <= 1 unconditionally, and no ringing when a
    coefficient changes.

        low shelf:   y = x - cLow * LP(x, aLow)
                     = 1 - cLow*LP,  cLow = 1 - gainLow

        high shelf:  y = x - cHigh * (x - LP(x, aHigh))
                     = gainHigh + cHigh*LP,  cHigh = 1 - gainHigh

    SMOOTHING
    ---------
    The shelf gains are what the ear hears, so they ramp at audio rate through
    LinearSmoother — the law SmoothedParameter.h names for "normalised 0..1
    coefficients such as diffusion or damping amount". The one-pole
    coefficients are set at control rate and applied directly: a one-pole has
    no resonant memory, so a coefficient step produces no transient, which is
    why the corner frequencies do not need an audio-rate ramp of their own.

    TRANSPARENCY
    ------------
    With both shelf gains at 1 the filter is bypassed on a flag rather than
    computed with coefficients that happen to be zero, so Damp 0 % is a
    bit-exact identity and costs one predictable branch.
*/
class LineDamping
{
public:
    void prepare (double sampleRate, double gainRampSeconds) noexcept
    {
        shelfLow .prepare (sampleRate, gainRampSeconds);
        shelfHigh.prepare (sampleRate, gainRampSeconds);

        shelfLow .snapTo (0.0);
        shelfHigh.snapTo (0.0);

        aLow  = 0.0;
        aHigh = 0.0;

        reset();
    }

    /** Clears the filter memory. Coefficients and targets are left alone. */
    void reset() noexcept
    {
        zLow  = 0.0;
        zHigh = 0.0;
    }

    /**
        New shelf targets for this line.

        `immediate` snaps the gain ramps, for prepare() and for a reset where
        gliding from a stale value would be wrong.
    */
    void setShelves (const damping::ShelfSpec& spec, bool immediate) noexcept
    {
        aLow  = spec.coeffLow;
        aHigh = spec.coeffHigh;

        const double cLow  = 1.0 - spec.gainLow;
        const double cHigh = 1.0 - spec.gainHigh;

        if (immediate)
        {
            shelfLow .snapTo (cLow);
            shelfHigh.snapTo (cHigh);
        }
        else
        {
            shelfLow .setTarget (cLow);
            shelfHigh.setTarget (cHigh);
        }

        transparent = spec.isTransparent();

        // A line that has just become transparent still has to ramp down to it
        // before the bypass can be taken, or the gain step would click.
        bypassed = transparent
                   && ! shelfLow.isSmoothing()
                   && ! shelfHigh.isSmoothing();
    }

    /** One sample through both shelves. */
    [[nodiscard]] double process (double x) noexcept
    {
        if (bypassed)
            return x;

        const double cLow  = shelfLow .getNextValue();
        const double cHigh = shelfHigh.getNextValue();

        zLow += (1.0 - aLow) * (x - zLow);
        const double afterLow = x - cLow * zLow;

        zHigh += (1.0 - aHigh) * (afterLow - zHigh);
        const double out = afterLow - cHigh * (afterLow - zHigh);

        // Taking the bypass only once both ramps have parked keeps the
        // transition from transparent to bypassed inaudible.
        if (transparent && ! shelfLow.isSmoothing() && ! shelfHigh.isSmoothing())
        {
            bypassed = true;
            zLow  = 0.0;
            zHigh = 0.0;
        }

        return out;
    }

    [[nodiscard]] bool isBypassed() const noexcept { return bypassed; }

private:
    LinearSmoother<double> shelfLow;
    LinearSmoother<double> shelfHigh;

    double aLow  = 0.0;
    double aHigh = 0.0;

    double zLow  = 0.0;
    double zHigh = 0.0;

    bool transparent = true;
    bool bypassed    = true;
};

} // namespace av::dsp
