#pragma once

#include <algorithm>
#include <cmath>

namespace av::dsp
{

/**
    Parameter smoothing.

    Different kinds of parameter need different smoothing laws. Using one
    smoother for everything is the usual cause of "the filter sweep sounds
    wrong" and "the delay time glide is uneven at the bottom of the range".

    LinearSmoother — audio rate, fixed duration
        A straight ramp that reaches the target in a known number of samples.
        Correct for level-like quantities where the ear responds to the value
        itself and where arriving exactly on target matters: mix, width, output
        gains, and normalised 0..1 coefficients such as diffusion or damping
        amount. Cheap: one add per sample.

    OnePoleSmoother — audio rate, exponential
        y += a * (target - y). Never overshoots, and its derivative is
        continuous, which is what a moving delay tap needs: the derivative of a
        delay time *is* the pitch shift applied to the signal passing through
        it, so a ramp's sudden start and stop would be heard as two clicks of
        detune. Correct for delay times in samples, modulation depths, and
        feedback coefficients. Costs one multiply-add per sample plus a settle
        test that parks the state exactly on target, which also prevents the
        asymptotic tail from decaying into the subnormal range.

    LogSmoother — CONTROL rate, exponential in the log domain
        Frequencies and times are perceived multiplicatively: 100 -> 200 Hz is
        the same musical distance as 1000 -> 2000 Hz. Smoothing them linearly
        makes a sweep crawl at the top of the range and lurch at the bottom.
        This class glides in log domain, so equal time means equal ratio.

        It is deliberately control rate. Recovering the linear value costs an
        exp(), which is not worth paying per sample for a cutoff frequency or a
        decay time — neither is read by the ear at audio rate. advance(n) moves
        the smoother n samples and returns the value at the end of that span;
        the reverb engine calls it once per sub-block of at most 32 samples,
        which at 48 kHz is a 0.67 ms control grid. Anything that genuinely needs
        per-sample resolution belongs in one of the two classes above.

    All three are real-time safe: no allocation, no locking, no exceptions.
*/

// ===========================================================================

/** Fixed-duration linear ramp. Audio rate. */
template <typename T = double>
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        stepsTotal = std::max (1, static_cast<int> (std::round (sampleRate * rampSeconds)));
        stepsLeft  = 0;
        increment  = T (0);
        current    = target;
    }

    void setTarget (T newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;

        if (stepsTotal <= 1)
        {
            snapToTarget();
            return;
        }

        stepsLeft = stepsTotal;
        increment = (target - current) / static_cast<T> (stepsTotal);
    }

    void snapTo (T value) noexcept
    {
        target = value;
        snapToTarget();
    }

    void snapToTarget() noexcept
    {
        current   = target;
        increment = T (0);
        stepsLeft = 0;
    }

    [[nodiscard]] T getNextValue() noexcept
    {
        if (stepsLeft <= 0)
            return target;

        current += increment;

        if (--stepsLeft == 0)
            current = target;

        return current;
    }

    /** Advances n samples without producing values. */
    void skip (int n) noexcept
    {
        if (n <= 0 || stepsLeft <= 0)
            return;

        if (n >= stepsLeft)
        {
            snapToTarget();
            return;
        }

        current   += increment * static_cast<T> (n);
        stepsLeft -= n;
    }

    [[nodiscard]] T    getCurrentValue() const noexcept { return stepsLeft > 0 ? current : target; }
    [[nodiscard]] T    getTargetValue()  const noexcept { return target; }
    [[nodiscard]] bool isSmoothing()     const noexcept { return stepsLeft > 0; }
    [[nodiscard]] int  getRampLengthSamples() const noexcept { return stepsTotal; }

private:
    T current { 0 };
    T target { 0 };
    T increment { 0 };
    int stepsTotal = 1;
    int stepsLeft  = 0;
};

// ===========================================================================

/** Exponential glide with a continuous derivative. Audio rate. */
template <typename T = double>
class OnePoleSmoother
{
public:
    /** timeConstantSeconds is the time to close ~63 % of the remaining gap. */
    void prepare (double sampleRate, double timeConstantSeconds) noexcept
    {
        if (sampleRate <= 0.0 || timeConstantSeconds <= 0.0)
            coeff = T (1);
        else
            coeff = static_cast<T> (1.0 - std::exp (-1.0 / (timeConstantSeconds * sampleRate)));

        oneMinusCoeff = T (1) - coeff;
    }

    void setTarget (T newTarget) noexcept { target = newTarget; }

    void snapTo (T value) noexcept { target = current = value; }
    void snapToTarget()   noexcept { current = target; }

    [[nodiscard]] T getNextValue() noexcept
    {
        const T delta = target - current;

        // Parking exactly on target keeps the asymptotic tail out of the
        // subnormal range and makes isSmoothing() eventually false.
        if (std::abs (delta) < settleThreshold())
            return current = target;

        current += coeff * delta;
        return current;
    }

    /** Closed-form advance by n samples. */
    void skip (int n) noexcept
    {
        if (n <= 0)
            return;

        const T delta = target - current;

        if (std::abs (delta) < settleThreshold())
        {
            current = target;
            return;
        }

        current = target - delta * std::pow (oneMinusCoeff, static_cast<T> (n));
    }

    [[nodiscard]] T skipAndGet (int n) noexcept
    {
        skip (n);
        return current;
    }

    [[nodiscard]] T    getCurrentValue() const noexcept { return current; }
    [[nodiscard]] T    getTargetValue()  const noexcept { return target; }
    [[nodiscard]] bool isSmoothing()     const noexcept
    {
        return std::abs (target - current) >= settleThreshold();
    }

private:
    [[nodiscard]] T settleThreshold() const noexcept
    {
        return static_cast<T> (1.0e-9) * std::max (T (1), std::abs (target));
    }

    T current { 0 };
    T target { 0 };
    T coeff { 1 };
    T oneMinusCoeff { 0 };
};

// ===========================================================================

/**
    Exponential glide in the log domain. CONTROL rate — call advance(n) once per
    block or sub-block, not once per sample.

    Values must be positive; anything at or below `floorValue` is treated as
    `floorValue`, so a parameter whose range reaches zero belongs in
    OnePoleSmoother instead.
*/
template <typename T = double>
class LogSmoother
{
public:
    void prepare (double sampleRate, double timeConstantSeconds,
                  T floorValue = static_cast<T> (1.0e-6)) noexcept
    {
        floorVal = std::max (floorValue, static_cast<T> (1.0e-30));
        inner.prepare (sampleRate, timeConstantSeconds);

        currentLinear = std::max (floorVal, currentLinear);
        targetLinear  = std::max (floorVal, targetLinear);

        inner.snapTo (std::log (currentLinear));
        inner.setTarget (std::log (targetLinear));
    }

    void setTarget (T newTarget) noexcept
    {
        targetLinear = std::max (floorVal, newTarget);
        inner.setTarget (std::log (targetLinear));
    }

    void snapTo (T value) noexcept
    {
        targetLinear = currentLinear = std::max (floorVal, value);
        inner.snapTo (std::log (currentLinear));
    }

    void snapToTarget() noexcept
    {
        currentLinear = targetLinear;
        inner.snapTo (std::log (currentLinear));
    }

    /** Advances n samples and returns the value at the end of that span. */
    [[nodiscard]] T advance (int numSamples) noexcept
    {
        return currentLinear = std::exp (inner.skipAndGet (numSamples));
    }

    /** Alias for advance() where the return value is not needed. */
    void skip (int numSamples) noexcept { (void) advance (numSamples); }

    [[nodiscard]] T    getCurrentValue() const noexcept { return currentLinear; }
    [[nodiscard]] T    getTargetValue()  const noexcept { return targetLinear; }
    [[nodiscard]] bool isSmoothing()     const noexcept { return inner.isSmoothing(); }

private:
    OnePoleSmoother<T> inner;
    T currentLinear { 1 };
    T targetLinear { 1 };
    T floorVal { static_cast<T> (1.0e-6) };
};

} // namespace av::dsp
