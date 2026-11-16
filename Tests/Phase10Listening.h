#pragma once

#include <string>

/**
    PHASE 10 - STAGE 3. The reduced early-reflection listening set.

    FOUR CASES, EIGHT FILES. An earlier 26-file set was prepared and is not
    what gets auditioned: a listener asked to review 26 renders reviews none
    of them carefully, and most of those files asked questions the
    measurements had already answered.

    WHY THE PRIMARY COMPARISON CHANGED
    ----------------------------------
    The set was first built to ask whether a 6.02 dB handoff valley at Size
    100 % reads as a hole. That figure was stale - it predated the placement
    law changing - and the real one is 16.47 dB. Re-measuring also separated
    the cause:

      - the envelope constant moves it 18.88 -> 16.47 dB (decay 0.6 -> 1.5)
      - `cellOccupancy` moves it 16.47 -> 7.26 dB (0.70 -> 0.20)

    and 0.20 is just as aperiodic as 0.70: 0 % repetition at every period,
    fifteen distinct arrival gaps out of fifteen. So the two envelope
    candidates sit inside the same structural valley and would be a poor use
    of a human's attention, while `cellOccupancy` is the axis that decides
    whether the valley exists at all.

    Cases A and B are therefore a blind A/B between the shipped 0.70 and the
    candidate 0.20, on a transient and on a musical source.

    NOTHING IS SHIPPED ON THIS BASIS YET. The production constant stays at
    0.70; 0.20 exists here only as render-time Pattern data. Which value
    becomes the shipping candidate is decided after listening, not before.

    WHAT IS HELD FIXED
    ------------------
    Everything except Size, Early/Late and - in cases A and B - the one
    Pattern field under test. Decay, Early Diffusion, Attack, Pre-Delay, both
    damping shelves, both cut filters, Width, Mod Rate, Mod Depth and the
    envelope decay candidate of 1.5 are the same in every render.

    NO LEVEL NORMALISATION. Within each A/B pair the source, the gain, the
    sample rate and the render length are identical, and the balance law is
    energy-calibrated, so there is nothing worth matching away and matching
    would only hide whatever is really there.

    BLINDING. The A/B mapping is randomised, differs between case A and case
    B so an order learned on one does not carry to the other, and appears in
    no filename. It lives in KEY.txt, which is meant to stay unread until the
    answers are written down. Cases C and D are not A/B and are labelled.

    Developer tooling. It lives only in the test target, never runs as part of
    a suite - writing files is not something a test run should do as a side
    effect - and nothing here is reachable from the shipped VST3.
*/
namespace avtest
{

/**
    Writes the PHASE 10 Stage 3 reduced listening set into `directory`.

    Eight WAV files, plus LISTENING.md and KEY.txt. Returns the number of
    files written, or a negative value on failure.
*/
int exportPhase10ListeningSet (const std::string& directory, double sampleRate = 48000.0);

/**
    The final occupancy A/B: cellOccupancy 0.20 against 0.40.

    FOUR WAV FILES, TWO CASES. A separate set from the one above, and the
    reason is that the earlier pack asks a question the research has since
    overtaken. It compared the shipped 0.70 against 0.20, and 0.70 is no
    longer a structural candidate: it carries a 16.47 dB large-Size handoff
    valley against 7.26 dB at 0.20, with no gain in aperiodicity to show for
    it.

    The measurement finalists are 0.20 and 0.40, and between them the
    measurements disagree rather than decide. 0.20 has the better handoff -
    7.26 dB against 8.86 dB at Size 100 % - and 0.40 the better third-octave
    flatness, 3.76 dB of ripple against 5.18 dB. Both are fully aperiodic,
    both are indistinguishable on correlation and mono, and neither can cost
    more CPU than the other. That is where measurement stops.

    NEITHER IS A SHIPPING CANDIDATE YET and the production constant is
    untouched at 0.70. 0.20 and 0.40 exist here only as render-time Pattern
    data.

    Blind: the A/B mapping is randomised independently per case, appears in no
    filename, and lives in KEY.txt.
*/
int exportPhase10OccupancyAb (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
