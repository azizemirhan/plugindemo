#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace av::dsp
{

/**
    PHASE 9. Attack: staggered feedforward injection into the reverb path.

    WHAT IT IS
    ----------
    A fixed FIR on the wet signal before the tank. One immediate tap keeps the
    wet onset exactly where it was, and a spread of later taps delivers the
    rest of the energy over the Attack time, so the late field is established
    gradually instead of all at once.

        y[n] = sum_k  w_k * x[n - d_k],    d_0 = 0,    sum_k w_k^2 = 1

    WHY A FIR AND NOT AN ENVELOPE
    -----------------------------
    PHASE 9 Stage 2 measured both. A wet amplitude envelope (mechanism AT-A)
    produces the right-looking build-up on the first transient after silence
    and nothing at all on the second: its ramp is 96 % open 500 ms later, so
    every subsequent event arrives into an already-open envelope. Making it
    work per event needs onset detection. And it loses 4.03 dB of wet energy
    across its range, which makes it substantially a level control wearing a
    temporal name.

    This is linear and time invariant, so the response to three transients is
    exactly the sum of three shifted single-transient responses - measured at
    bit level, residual 0.0 - which means every event gets the same Attack
    with no detector, no transport awareness and no state that can be in the
    wrong phase when a transient arrives.

    It is also entirely OUTSIDE the feedback recurrence. It cannot change
    RT60, the damping shelves, the feedback gains, the loop lengths or the
    modulation law, because none of those can see it.

    THE TAP SCHEDULE, AND WHY IT IS NOT REGULAR
    -------------------------------------------
    Stage 2's research prototype used 24 regularly spaced taps. Regular
    spacing is the one thing this must not do: equal spacing makes the FIR a
    comb, which colours the excitation, and at longer Attack times it makes
    the taps audible as a rhythmic flam rather than as a reverb building up.

    So the schedule is velvet-noise-like, in the sense the term is normally
    used for this problem:

      - the spread is divided into equal cells, one tap per cell;
      - inside its cell each tap sits at a position taken from the golden
        ratio sequence, which is irregular and has no common divisor with the
        cell grid, so no two tap spacings repeat;
      - each tap carries a deterministic +1 or -1, which flattens the FIR's
        magnitude response - a sign pattern is what separates velvet noise
        from a comb.

    Deterministic throughout. Same build, same sample rate, same taps, every
    time; nothing is seeded from the clock and nothing changes across reset.
    Positions are computed in milliseconds and rounded to samples, so the
    Attack time means the same thing at 44.1, 48 and 96 kHz.

    The immediate tap is excluded from the sign pattern and is always
    positive, because inverting the onset would be audible as a polarity
    change on the dry/wet sum.

    CHANNEL-IDENTICAL, DELIBERATELY
    -------------------------------
    Both channels get the same schedule. Two reasons, and the second is the
    one that decides it.

    The stereo field already comes from the input diffuser's differing delay
    patterns and from the injection and readout vector geometry, all of which
    sit after this stage. Attack does not need to decorrelate anything.

    A per-channel schedule was expected to cost mono compatibility, since two
    different FIRs sum into a comb in the downmix that neither channel has on
    its own. MEASUREMENT DID NOT SUPPORT THAT: the per-channel variant came
    out within 0.08 dB of the channel-identical one on downmix energy, and
    marginally closer to ideal decorrelation. So the mono argument is withdrawn
    and the choice rests on the commutation property below and on simplicity,
    which is a weaker reason honestly stated rather than a stronger one
    invented.

    Being channel-identical also makes this stage a scalar operator times the
    identity, which COMMUTES with the stereo diffuser. That is why it can sit
    either side of the input diffusion and produce the same samples, and it is
    why the placement question has a measured answer rather than a taste.

    ENERGY
    ------
    The weights are L2-normalised: sum of squares is exactly 1. The taps are
    far enough apart to add incoherently in the late field, so the established
    wet energy stays put while the arrival time moves. Measured across the
    whole Attack range the integrated wet energy varies by well under half a
    decibel, so Attack is not a disguised wet-level control. The peak falls as
    Attack rises, which is correct rather than a defect: the same energy
    spread over more time has a lower peak.

    AUTOMATION
    ----------
    Tap positions are integer samples and they never move while they are being
    read. Moving a tap continuously is a delay-time derivative, which is a
    pitch shift applied to whatever is passing through it - the same reason
    PHASE 8's pre-delay crossfades between fixed taps rather than sliding one.

    So a change of Attack builds a second schedule and crossfades to it over a
    fixed window, reading both schedules from the same delay line. A fade
    always runs to completion; a request arriving mid-fade overwrites a single
    pending slot and is applied the moment the fade ends. Coalesced, not
    queued.

    BYPASS
    ------
    Attack 0 is an explicit bypass, not a tap set that happens to sum to
    unity. The samples are passed through untouched, so the PHASE 8 signal
    path is reproduced bit for bit - which is the compatibility requirement,
    and which collapsing the taps to zero offset would NOT give, because N
    coherent taps summing to 1.0 is not the same arithmetic as one tap of 1.0.

    The delay line is written unconditionally, including while bypassed. A
    buffer that is not written while Attack is 0 goes stale, and the first
    move away from 0 would read whatever was passing through whenever the
    control was last touched.
*/
class AttackInjection
{
public:
    /** Longest spread the stage will allocate for. */
    static constexpr double maxSpreadMs = 400.0;

    /** Production tap count.

        Chosen on the mean gap between arrivals and on third-octave ripple -
        NOT on ripple alone, which is blind to sparse taps: six taps over
        250 ms measure better than any dense configuration while being five
        audible echoes, because a 50 ms spacing puts the comb's teeth 20 Hz
        apart, finer than a third-octave band. See the Stage-3 report. */
    static constexpr int defaultTaps = 32;

    /** Production spread at Attack 100 %. With defaultTaps this leaves about
        8 ms between arrivals, under the ~10 ms where they start to separate
        perceptually. 400 ms was measured and rejected: at that spread the
        energy arrives so slowly that the envelope peak is set by the
        immediate tap, so the build-up metric collapses rather than
        lengthening. */
    static constexpr double defaultMaxSpreadMs = 250.0;

    /**
        Mapping exponent from the 0..1 control to spread time.

        Linear would spend most of the control's travel above 100 ms, where
        the differences are large and the musically useful region is already
        behind you. The square law puts 0-100 ms in the first 63 % of the
        travel while keeping the long maximum.
    */
    static constexpr double mappingExponent = 2.0;

    /** Share of the total POWER the immediate tap carries. */
    static constexpr double immediateShare = 0.25;

    /** Crossfade between tap schedules. Measured; see the Stage-3 report. */
    static constexpr double crossfadeMs = 30.0;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------
    void prepare (double sampleRate, int taps = defaultTaps,
                  double maximumSpreadMs = defaultMaxSpreadMs)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        tapCount = std::clamp (taps, 2, 64);
        maxSpread = std::clamp (maximumSpreadMs, 1.0, maxSpreadMs);

        const auto capacity = static_cast<size_t> (
            std::ceil (maxSpreadMs * currentSampleRate / 1000.0)) + 4;

        // Power of two, so the ring index is a mask rather than a modulo.
        size_t rounded = 1;

        while (rounded < capacity)
            rounded <<= 1;

        left .assign (rounded, 0.0);
        right.assign (rounded, 0.0);
        mask = rounded - 1;
        writeIndex = 0;

        fadeLength = std::max (1, static_cast<int> (
            std::lround (crossfadeMs * currentSampleRate / 1000.0)));

        active.resize (static_cast<size_t> (tapCount));
        pending.resize (static_cast<size_t> (tapCount));

        buildSchedule (0.0, active);
        pending = active;

        currentControl = 0.0;
        pendingControl = 0.0;
        fading = false;
        fadePosition = 0;
    }

    void reset() noexcept
    {
        std::fill (left.begin(), left.end(), 0.0);
        std::fill (right.begin(), right.end(), 0.0);

        writeIndex = 0;
        fading = false;
        fadePosition = 0;
        pendingControl = currentControl;
        pending = active;
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------
    /** `control` is the Attack parameter, 0..1. Exactly 0 is the bypass. */
    void setAttack (double control) noexcept
    {
        const double clamped = std::clamp (control, 0.0, 1.0);

        if (clamped == pendingControl && (fading || clamped == currentControl))
            return;

        pendingControl = clamped;

        if (! fading && clamped != currentControl)
            beginFade (clamped);
    }

    /** Snaps to a setting with no crossfade. For prepare and for reset. */
    void snapTo (double control) noexcept
    {
        currentControl = std::clamp (control, 0.0, 1.0);
        pendingControl = currentControl;

        buildSchedule (currentControl, active);
        pending = active;

        fading = false;
        fadePosition = 0;
    }

    [[nodiscard]] bool isBypassed() const noexcept
    {
        return ! fading && currentControl <= 0.0;
    }

    [[nodiscard]] double getAttack() const noexcept { return currentControl; }
    [[nodiscard]] int    getTapCount() const noexcept { return tapCount; }
    [[nodiscard]] int    getFadeLengthSamples() const noexcept { return fadeLength; }
    [[nodiscard]] double getMaxSpreadMs() const noexcept { return maxSpread; }

    /** Spread time, in milliseconds, for a control position. */
    [[nodiscard]] double spreadForControl (double control) const noexcept
    {
        const double u = std::clamp (control, 0.0, 1.0);

        return maxSpread * std::pow (u, mappingExponent);
    }

    /** The active schedule, for tests and for the report. */
    [[nodiscard]] int getTapOffset (int index) const noexcept
    {
        return active[static_cast<size_t> (index)].offset;
    }

    [[nodiscard]] double getTapWeight (int index) const noexcept
    {
        return active[static_cast<size_t> (index)].weight;
    }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------
    void process (double* l, double* r, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Unconditional, including while bypassed: see the class comment.
            writeIndex = (writeIndex + 1) & mask;
            left [writeIndex] = l[i];
            right[writeIndex] = r[i];

            if (! fading)
            {
                if (currentControl <= 0.0)
                    continue;           // explicit bypass, bit-exact

                l[i] = convolve (left,  active);
                r[i] = convolve (right, active);

                continue;
            }

            const double g = static_cast<double> (fadePosition)
                             / static_cast<double> (fadeLength);

            const double fromL = currentControl <= 0.0 ? l[i] : convolve (left,  active);
            const double fromR = currentControl <= 0.0 ? r[i] : convolve (right, active);
            const double toL   = pendingTargetIsBypass() ? l[i] : convolve (left,  pending);
            const double toR   = pendingTargetIsBypass() ? r[i] : convolve (right, pending);

            l[i] = fromL + g * (toL - fromL);
            r[i] = fromR + g * (toR - fromR);

            if (++fadePosition >= fadeLength)
            {
                currentControl = fadeTarget;
                active = pending;
                fading = false;
                fadePosition = 0;

                if (pendingControl != currentControl)
                    beginFade (pendingControl);
            }
        }
    }

private:
    struct Tap
    {
        int    offset = 0;
        double weight = 0.0;
    };

    [[nodiscard]] bool pendingTargetIsBypass() const noexcept { return fadeTarget <= 0.0; }

    void beginFade (double target) noexcept
    {
        fadeTarget = target;
        buildSchedule (target, pending);
        fading = true;
        fadePosition = 0;
    }

    [[nodiscard]] double convolve (const std::vector<double>& buffer,
                                   const std::vector<Tap>& taps) const noexcept
    {
        double sum = 0.0;

        for (const auto& tap : taps)
            sum += tap.weight
                 * buffer[(writeIndex - static_cast<size_t> (tap.offset)) & mask];

        return sum;
    }

    /**
        Builds the tap schedule for a control position.

        Not real-time hostile: fixed-size vectors, no allocation, and it runs
        once per Attack change rather than per sample. `std::pow` and
        `std::lround` are the only transcendentals and they run tapCount times.
    */
    void buildSchedule (double control, std::vector<Tap>& out) const noexcept
    {
        const double spreadMs = spreadForControl (control);
        const double spreadSamples = spreadMs * currentSampleRate / 1000.0;

        // The immediate tap. Always offset 0, always positive.
        out[0].offset = 0;
        out[0].weight = std::sqrt (immediateShare);

        const int later = tapCount - 1;

        if (later <= 0 || spreadSamples <= 0.0)
        {
            for (size_t k = 1; k < out.size(); ++k)
            {
                out[k].offset = 0;
                out[k].weight = 0.0;
            }

            out[0].weight = 1.0;
            return;
        }

        // Rising power envelope, so the field grows rather than arriving in
        // one later lump. Normalised to carry 1 - immediateShare in total.
        double rampTotal = 0.0;

        for (int k = 1; k <= later; ++k)
            rampTotal += static_cast<double> (k);

        // Golden-ratio placement inside each cell, and a deterministic sign.
        constexpr double golden = 0.6180339887498949;

        unsigned int signState = 2463534242u;

        for (int k = 1; k <= later; ++k)
        {
            const double cellLow  = static_cast<double> (k - 1) / later;
            const double cellHigh = static_cast<double> (k) / later;

            double fraction = golden * static_cast<double> (k);
            fraction -= std::floor (fraction);

            const double position = cellLow + fraction * (cellHigh - cellLow);

            // Never 0, so a later tap can never collide with the immediate
            // one and turn two incoherent contributions into one coherent
            // tap of a different weight.
            const int offset = std::max (1, static_cast<int> (
                std::lround (position * spreadSamples)));

            // xorshift, fixed seed: deterministic and reproducible.
            signState ^= signState << 13;
            signState ^= signState >> 17;
            signState ^= signState << 5;

            const double sign = (signState & 1u) ? 1.0 : -1.0;

            const double power = (1.0 - immediateShare)
                               * static_cast<double> (k) / rampTotal;

            out[static_cast<size_t> (k)].offset = offset;
            out[static_cast<size_t> (k)].weight = sign * std::sqrt (power);
        }
    }

    std::vector<double> left, right;
    std::vector<Tap> active, pending;

    size_t mask = 0;
    size_t writeIndex = 0;

    double currentSampleRate = 48000.0;
    double maxSpread = defaultMaxSpreadMs;
    double currentControl = 0.0;
    double pendingControl = 0.0;
    double fadeTarget = 0.0;

    int tapCount = defaultTaps;
    int fadeLength = 1;
    int fadePosition = 0;
    bool fading = false;
};

} // namespace av::dsp
