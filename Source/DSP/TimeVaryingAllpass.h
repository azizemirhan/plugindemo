#pragma once

#include <algorithm>
#include <cmath>

#include "DelayLine.h"

namespace av::dsp
{

/**
    A short allpass section whose coefficient moves, in three realisations.

    ---------------------------------------------------------------------------
    WHY THE REALISATION MATTERS
    ---------------------------------------------------------------------------

    All three modes below have the same transfer function for a fixed
    coefficient:

                 -g + z^-M
        H(z) = --------------
                1 - g * z^-M

    which is allpass: |H| = 1 at every frequency. That is a statement about a
    *constant* g, and it says nothing about what happens while g is moving. The
    three modes differ only in how the state is stored, and that difference is
    exactly what decides whether a moving coefficient conserves energy or pumps
    it.

    ---------------------------------------------------------------------------
    MODE A — ENERGY PRESERVING (rotation / normalised lattice)
    ---------------------------------------------------------------------------

    Read the stored value d, apply a plane rotation to (x, d), and write the
    second output back into the delay:

        y = -s * x + c * d
        w =  c * x + s * d          with c^2 + s^2 = 1

    Derivation of the transfer function, with D = z^-M W:

        W = c X + s z^-M W    ->    W = c X / (1 - s z^-M)
        Y = -s X + c z^-M W
          = X * [ -s (1 - s z^-M) + c^2 z^-M ] / (1 - s z^-M)
          = X * [ -s + (s^2 + c^2) z^-M ] / (1 - s z^-M)
          = X * ( -s + z^-M ) / (1 - s z^-M)

    which is exactly the Schroeder allpass with g = s.

    The energy argument is one line: (x, d) -> (y, w) is a plane rotation, so

        x^2 + d^2 = y^2 + w^2

    exactly, at every sample, for any s and any rate of change of s. Input
    energy plus stored energy equals output energy plus stored energy. There is
    no coefficient trajectory — however fast, however adversarial — that can
    make this structure generate energy, because the identity holds sample by
    sample rather than on average. That is the whole reason for preferring it,
    and the tests verify the residual directly rather than inferring it from a
    long-run level.

    ---------------------------------------------------------------------------
    MODE C — SCHROEDER REFERENCE (direct form)
    ---------------------------------------------------------------------------

        v = x + g * d
        y = d - g * v
        write v

    Same transfer function, different state. What is stored is v, and v carries
    the gain 1/(1 - g z^-M): near g = 0.8 the stored value is roughly five times
    the input. When g moves, that inflated state is reinterpreted under a new
    coefficient, and the mismatch appears as energy the structure did not
    receive. Included as a reference so the advantage of the rotation form is a
    measured number rather than a claim; deliberately not "fixed" with a gain
    correction, because a failing reference that has been patched is no longer a
    reference.

    ---------------------------------------------------------------------------
    MODE B — THIRAN (first-order fractional delay, direct form)
    ---------------------------------------------------------------------------

        y[n] = a * x[n] + x[n-1] - a * y[n-1]

    with a = (1 - d) / (1 + d) for a target fractional delay d. Its magnitude
    response is exactly flat for |a| < 1 and its group delay is d at low
    frequencies, which makes it the natural way to move a fraction of a sample
    of delay. It is a direct-form realisation, so like mode C it has no
    time-varying energy guarantee; whether that matters at the coefficient
    speeds this project uses is measured rather than assumed.

    ---------------------------------------------------------------------------
    GROUP DELAY
    ---------------------------------------------------------------------------

    For modes A and C the group delay at DC is

        tau(0) = M * (1 + g) / (1 - g)

    so moving g moves the delay, and the derivative of that delay is the pitch
    shift the section imposes. dtau/dg = 2M / (1 - g)^2, which grows quickly as
    g approaches 1 — the reason the useful coefficient range sits well below it.
    For mode B the group delay is simply d.

    Nothing here allocates or branches unpredictably after prepare().
*/
class TimeVaryingAllpass
{
public:
    enum class Mode
    {
        energyPreserving,   /**< Rotation form. Conserves energy at any coefficient speed. */
        thiran,             /**< First-order fractional delay, direct form. */
        schroederReference  /**< Classic direct form. Reference, not for shipping. */
    };

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate, double maxDelayMs)
    {
        currentSampleRate = sampleRate;
        capacitySamples = std::max (1, static_cast<int> (std::ceil (maxDelayMs * 0.001 * sampleRate)) + 2);

        line.prepareSamples (capacitySamples + 1);
        delaySamples = std::clamp (delaySamples, 1, capacitySamples);

        reset();
    }

    void reset() noexcept
    {
        line.reset();
        thiranPreviousInput = 0.0;
        thiranPreviousOutput = 0.0;
        lastRead = 0.0;
        lastWrite = 0.0;
    }

    // -----------------------------------------------------------------------
    //  Configuration — real-time safe.
    // -----------------------------------------------------------------------

    void setMode (Mode m) noexcept { mode = m; }
    [[nodiscard]] Mode getMode() const noexcept { return mode; }

    /** Loop delay in whole samples. Modes A and C only; Thiran is always 1. */
    void setDelaySamples (int samples) noexcept
    {
        delaySamples = std::clamp (samples, 1, std::max (1, capacitySamples));
    }

    void setDelayMilliseconds (double ms) noexcept
    {
        setDelaySamples (static_cast<int> (std::lround (ms * 0.001 * currentSampleRate)));
    }

    /**
        The moving parameter.

        For modes A and C this is the allpass coefficient g (mode A additionally
        derives c = sqrt(1 - g^2) so the rotation stays exact). For mode B it is
        the target fractional delay in samples, converted to the Thiran
        coefficient here.
    */
    void setCoefficient (double value) noexcept
    {
        if (mode == Mode::thiran)
        {
            // Kept clear of d = 0, where a -> 1 and the pole sits on the unit
            // circle, and of very large d, where the first-order approximation
            // stops being a delay at all.
            const double d = std::clamp (value, thiranMinDelay, thiranMaxDelay);
            coefficient = (1.0 - d) / (1.0 + d);
            fractionalDelay = d;
        }
        else
        {
            coefficient = std::clamp (value, -maxCoefficient, maxCoefficient);
            complement  = std::sqrt (std::max (0.0, 1.0 - coefficient * coefficient));
        }
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    [[nodiscard]] double processSample (double x) noexcept
    {
        switch (mode)
        {
            case Mode::energyPreserving:
            {
                const double d = line.readAt (delaySamples - 1);

                const double y = -coefficient * x + complement * d;
                const double w =  complement  * x + coefficient * d;

                lastRead = d;
                lastWrite = w;

                line.write (w);
                return y;
            }

            case Mode::schroederReference:
            {
                const double d = line.readAt (delaySamples - 1);

                const double v = x + coefficient * d;
                const double y = d - coefficient * v;

                lastRead = d;
                lastWrite = v;

                line.write (v);
                return y;
            }

            case Mode::thiran:
            default:
            {
                const double y = coefficient * x + thiranPreviousInput
                               - coefficient * thiranPreviousOutput;

                lastRead = thiranPreviousOutput;
                lastWrite = y;

                thiranPreviousInput = x;
                thiranPreviousOutput = y;

                return y;
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    /** The value read from storage on the last sample. */
    [[nodiscard]] double getLastRead()  const noexcept { return lastRead; }

    /** The value written back to storage on the last sample. */
    [[nodiscard]] double getLastWrite() const noexcept { return lastWrite; }

    [[nodiscard]] double getCoefficient()     const noexcept { return coefficient; }
    [[nodiscard]] double getComplement()      const noexcept { return complement; }
    [[nodiscard]] double getFractionalDelay() const noexcept { return fractionalDelay; }
    [[nodiscard]] int    getDelaySamples()    const noexcept { return delaySamples; }
    [[nodiscard]] int    getCapacitySamples() const noexcept { return capacitySamples; }

    /** Group delay at DC, in samples, for the current settings. */
    [[nodiscard]] double getGroupDelayAtDc() const noexcept
    {
        if (mode == Mode::thiran)
            return fractionalDelay;

        return delaySamples * (1.0 + coefficient) / std::max (1.0e-9, 1.0 - coefficient);
    }

    /**
        Frequency-averaged group delay, in samples.

        For a stable allpass the group delay integrates over frequency to
        exactly the number of poles:

            (1/2pi) * integral of tau(omega) d(omega) = number of poles

        A section of the form (-g + z^-M)/(1 - g z^-M) has M poles — the roots of
        z^M = g, all at radius g^(1/M) — so its mean group delay is M samples
        whatever the coefficient is. A first-order Thiran section has one pole,
        so its mean is 1 sample whatever its fractional delay is.

        That independence is what makes this the right quantity for a broadband
        RT60 correction. The delay a modulated section adds to the feedback loop
        is not a function of the modulation depth: modulation redistributes group
        delay across frequency but cannot change its integral. Correcting from
        the DC group delay instead would be depth-dependent, frequency-blind and
        wrong — tau(0) = M(1+g)/(1-g) is the maximum over frequency, not the
        average.
    */
    [[nodiscard]] double getMeanGroupDelay() const noexcept
    {
        return mode == Mode::thiran ? 1.0 : static_cast<double> (delaySamples);
    }

    /** d(group delay)/d(coefficient) at DC, in samples. */
    [[nodiscard]] double getGroupDelaySensitivity() const noexcept
    {
        if (mode == Mode::thiran)
            return 1.0;

        const double denominator = std::max (1.0e-9, 1.0 - coefficient);
        return 2.0 * delaySamples / (denominator * denominator);
    }

    /** Highest |g| the section will accept. Well below the unit circle. */
    static constexpr double maxCoefficient = 0.85;

    /** Thiran fractional-delay bounds, chosen to keep the pole away from z = -1
        and to stay inside the range where a first-order design is accurate. */
    static constexpr double thiranMinDelay = 0.20;
    static constexpr double thiranMaxDelay = 1.80;

    [[nodiscard]] static constexpr const char* describe (Mode m) noexcept
    {
        switch (m)
        {
            case Mode::energyPreserving:   return "energy preserving";
            case Mode::thiran:             return "thiran";
            case Mode::schroederReference: return "schroeder reference";
        }

        return "unknown";
    }

private:
    DelayLine<double> line;

    Mode mode = Mode::energyPreserving;

    double coefficient = 0.0;
    double complement  = 1.0;
    double fractionalDelay = 1.0;

    double thiranPreviousInput  = 0.0;
    double thiranPreviousOutput = 0.0;

    double lastRead  = 0.0;
    double lastWrite = 0.0;

    double currentSampleRate = 48000.0;
    int    delaySamples      = 1;
    int    capacitySamples   = 1;
};

} // namespace av::dsp
