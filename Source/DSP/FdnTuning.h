#pragma once

#include <cmath>

/**
    Tuning constants for the feedback delay network.

    As in DiffusionTuning.h, every number that shapes the sound lives here with
    the measurement that chose it, so the tank can be re-tuned and re-measured
    without touching the DSP.
*/
namespace av::dsp::fdn
{

/** Order of the network. Only 8 is production quality in this phase. */
inline constexpr int order = 8;

// ===========================================================================
//  Delay set
// ===========================================================================
/*
    Eight delays in milliseconds, converted to whole samples at prepare time.

    Choosing them is the same problem as the diffuser's delay set but with an
    extra failure mode. The modes of an FDN sit at frequencies set by the
    delays, so a delay set with common factors produces modes that pile up on
    top of each other and leave gaps between: audibly, a tank that rings at a
    few pitches instead of sounding like a room. Three things are therefore
    scored, not two:

      - Pairwise GCD in samples. Two delays sharing a large common factor share
        a whole family of modes.
      - Subset-sum collisions, as in the diffuser.
      - Modal spacing regularity, measured as the spread of the gaps between the
        combined mode frequencies of all eight lines.

    "Use prime numbers of samples" is the folklore answer and it is not enough:
    primes guarantee a pairwise GCD of 1 but say nothing about modal spacing or
    sums, and a prime count of samples stops being prime the moment the sample
    rate changes. The set below is the output of the directed search in
    FdnTests.cpp, run over millisecond values with the sample-rate conversion
    included in the scoring.
*/

/** FDN loop delays in milliseconds, before the Size scale is applied.
    Total 388.5 ms of delay memory; mean modal spacing about 2.6 Hz. */
inline constexpr double delaysMs[order]
{
    26.27, 30.38, 38.10, 42.58, 47.06, 54.20, 66.99, 82.92
};

/** Widest Size scale the tank reserves capacity for. */
inline constexpr double maxSizeScale = 2.0;

/** Narrowest Size scale accepted. */
inline constexpr double minSizeScale = 0.25;

// ===========================================================================
//  Stereo injection and readout
// ===========================================================================
/*
    Four sign vectors, each scaled by 1/sqrt(8) so it has unit norm.

    Sign vectors are used rather than arbitrary unit vectors because they spread
    the input across every delay line equally — no line is favoured — while
    still allowing the four vectors to be mutually orthogonal. Balanced vectors
    (four positive, four negative) are additionally orthogonal to the all-ones
    direction, which matters because the all-ones direction is special to both
    candidate matrices.

    Which four are shipped is decided by measurement in FdnStereoTests.cpp, not
    by picking rows out of a Hadamard matrix and hoping.
*/

inline constexpr double injectionLeft [order] { +1, +1, +1, +1, -1, -1, -1, -1 };
inline constexpr double injectionRight[order] { +1, +1, -1, -1, +1, +1, -1, -1 };
inline constexpr double readoutLeft   [order] { -1, -1, -1, +1, +1, +1, +1, -1 };
inline constexpr double readoutRight  [order] { +1, -1, -1, +1, -1, -1, +1, +1 };

/*
    The first attempt at these also demanded that each readout be orthogonal to
    its own injection, on the theory that a readout should never be a copy of
    an injection. That was wrong, and measurably so: it makes the left output
    blind to the left input, and the hard-panned separation collapsed below
    1 dB. Same-side overlap is what carries the stereo image. What has to be
    orthogonal is the cross terms — readout L against injection R, and readout R
    against injection L — so a right-only input does not arrive at the left
    output. The shipped set has an overlap of 4 out of 8 on each same-side pair
    and exactly zero on both cross pairs.

    The measured ceiling on hard-panned separation across the whole population
    of valid quadruples was 1.40 dB. That is what a shared tank can offer:
    both outputs observe the same modes, so channel identity survives as a bias
    rather than as isolation. Anything more would need two tanks.
*/

/** Scale that turns a sign vector into a unit vector. */
inline constexpr double signVectorNorm = 0.35355339059327376220; // 1/sqrt(8)

// ===========================================================================
//  Decay
// ===========================================================================
/*
    Per-line feedback gain for a target RT60.

    A signal circulating in line i passes through it once every d_i seconds, and
    is multiplied by g_i each time. After t seconds it has made t/d_i passes, so
    its level is g_i^(t/d_i). RT60 is the time to fall 60 dB, a factor of 10^-3:

        g_i^(RT60/d_i) = 10^-3
        (RT60/d_i) * log10(g_i) = -3
        g_i = 10^(-3 * d_i / RT60)

    Every line reaches -60 dB at the same time regardless of its length, which
    is what makes the tail decay as one thing rather than as eight. The formula
    assumes the mixing matrix is orthogonal — otherwise energy would leak
    between lines at a rate the formula knows nothing about.
*/

[[nodiscard]] inline double feedbackGainForRt60 (double delaySeconds, double rt60Seconds) noexcept
{
    if (rt60Seconds <= 0.0 || delaySeconds <= 0.0)
        return 0.0;

    return std::pow (10.0, -3.0 * delaySeconds / rt60Seconds);
}

/** Inverse: the RT60 a given per-line gain corresponds to. For tests. */
[[nodiscard]] inline double rt60ForFeedbackGain (double delaySeconds, double gain) noexcept
{
    if (gain <= 0.0 || gain >= 1.0 || delaySeconds <= 0.0)
        return 0.0;

    return -3.0 * delaySeconds / std::log10 (gain);
}

/** Shortest decay the parameter range exposes. */
inline constexpr double minRt60Seconds = 0.1;

/** Longest decay the parameter range exposes. */
inline constexpr double maxRt60Seconds = 100.0;

/**
    Hard ceiling on any per-line gain.

    Stability needs max(g_i) < 1 given an orthogonal matrix, and 1.0 exactly is
    a lossless tank that rings forever. The margin below is not arbitrary: at
    the shortest delay in the set (23.53 ms) a gain of 0.9995 corresponds to an
    RT60 of about 325 seconds, comfortably past the 100 s the parameter reaches,
    so the clamp never engages in normal use and exists only to make an
    out-of-range request safe rather than catastrophic.
*/
inline constexpr double maxFeedbackGain = 0.9995;

// ===========================================================================
//  Size morphing
// ===========================================================================

/*
    Size transitions have two stages: priming, then a crossfade.

    A tank that has just been reset is empty, and an empty tank is silent for
    its shortest delay and quiet for several circulations after that. Crossfading
    straight into it fades from a full reverb into a nearly silent one: measured
    with a 120 ms equal-power fade and no priming, the level dropped 9.8 dB in
    the middle of the transition — a dropout, not a Size change.

    Priming fixes that by feeding the incoming bank while it contributes nothing
    to the output. How long it needs is the question, and a fixed time is the
    wrong answer, because a tank fills at a rate set by its decay:

        prime time    level variation across the transition (RT60 = 4 s)
          0 ms            9.76 dB
         60 ms            7.15 dB
        120 ms            5.98 dB
        180 ms            4.95 dB
        250 ms            4.39 dB
        350 ms            3.98 dB
        500 ms            3.55 dB

    Even half a second is not enough at a four-second decay, and half a second
    would be absurdly sluggish at a quarter-second one. So the fade is triggered
    by level rather than by the clock: the two banks' output envelopes are
    tracked, and the crossfade starts once the incoming bank has reached a set
    fraction of the outgoing one. That adapts to the decay automatically and
    needs no measurement of RT60 at all.

    The cap exists so a transition always completes: with a silent or very quiet
    input the envelopes never converge, and there is nothing to hear anyway.
*/

/*
    A level trigger on its own is not enough, because there are inputs for which
    the envelopes never converge and the transition would hang:

      - silence, where both envelopes sit at zero;
      - a level so low the ratio is dominated by the anti-denormal guard;
      - a single short impulse, where the outgoing bank is decaying away while
        the incoming one is still filling, so the ratio can cross from either
        direction;
      - a very short decay, where the incoming bank fills in a few milliseconds
        and the trigger would fire before its shortest delay had even elapsed;
      - any Size change large enough that the incoming bank's steady level is
        below the threshold fraction of the outgoing one, which cannot converge
        at all — a Size increase lowers the steady level, as measured.

    So the trigger is bracketed on both sides. A minimum warm-up guarantees the
    new tank has produced real output before it is faded in, and a maximum wait
    guarantees the transition completes in bounded time no matter what the input
    does. Both bounds are measured in FdnMorphTests.cpp rather than guessed.
*/

/*
    The trigger asks whether the incoming bank has stopped filling, not whether
    it has reached some fraction of the outgoing one.

    The first version compared the two banks and required the incoming one to
    reach 75 % of the outgoing level. That threshold is unreachable by
    construction for any Size increase: a longer delay set has smaller per-line
    gains, so the new tank settles *below* the old one — measured at -2.86 dB
    for Size 1.0 to 1.8, and about -12 dB across the full Size range. The
    trigger could only ever fire on a Size decrease, and every increase fell
    through to the timeout.

    Comparing the incoming bank against its own recent history has no such
    blind spot. Two envelope followers with different time constants run on it:
    while the tank is filling the fast one leads the slow one, and when it has
    settled they converge. The condition has to hold continuously for a short
    window so a downward fluctuation in a noise-driven tail cannot trigger it.
*/

/** Time constants of the two followers watching the incoming bank. */
inline constexpr double morphFastEnvelopeSeconds = 0.40;
inline constexpr double morphSlowEnvelopeSeconds = 1.20;

/** How close the fast follower must sit to the slow one to count as settled. */
inline constexpr double morphSettledTolerance = 0.05;

/** How long that has to hold before the fade starts. */
inline constexpr double morphSettledHoldSeconds = 0.02;

/*
    The ceiling is derived from the decay rather than fixed.

    A tank fills with the same time constant it empties with. RT60 is the time
    to fall 60 dB, so the amplitude time constant is RT60/6.9, and 2.3 of those
    puts the tank within a decibel of its asymptote. That is the longest waiting
    can usefully be, so it is the ceiling:

        cap = clamp(0.333 * RT60, minimum warm-up, absolute maximum)

    At a 0.3 s decay that is the 200 ms warm-up and nothing more; at 4 s it is
    1.33 s; past 4.5 s it saturates at the absolute maximum, because waiting
    longer than a second and a half for a Size change to take effect stops being
    a transition and starts being a fault.
*/
inline constexpr double morphPrimeDecayFactor = 0.333;

/*
    Measured priming stage against the decay-derived ceiling:

        decay      priming stage    ceiling
        0.3 s        200 ms          200 ms
        1.0 s        333 ms          333 ms
        2.0 s        666 ms          666 ms
        4.0 s       1332 ms         1332 ms
       10.0 s       1500 ms         1500 ms
       60.0 s       1500 ms         1500 ms

    Worth stating plainly: in every case above it is the ceiling that ends the
    priming, not the settling trigger. The trigger only fires for silence, where
    there is nothing to fill. A reverb tail driven by real audio fluctuates
    enough that a "has it stopped rising" test on envelope followers does not
    hold its condition continuously before the decay-derived ceiling arrives
    anyway. The trigger is kept because it costs four multiply-adds and covers
    the case where a tank fills faster than its decay implies, but the ceiling
    is what governs in practice — and the ceiling is the part derived from
    something measurable rather than tuned.
*/

/**
    Shortest priming stage.

    Below the longest delay in the set the incoming tank has not yet produced
    anything from its longest line, so fading in would cross-fade into a
    partial tank. The longest delay at the widest Size is 82.92 * 2 = 166 ms;
    the warm-up covers it with a little margin.
*/
inline constexpr double morphPrimeMinSeconds = 0.20;

/** Longest the priming stage may last before the fade starts regardless. */
inline constexpr double morphPrimeMaxSeconds = 1.5;

/** Output crossfade time once the incoming bank has been primed. */
inline constexpr double morphSeconds = 0.12;

/** Size changes smaller than this are ignored, so that a slow automation ramp
    does not start a new morph on every block. */
inline constexpr double sizeMorphThreshold = 0.005;

} // namespace av::dsp::fdn
