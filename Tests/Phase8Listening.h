#pragma once

#include <string>

/**
    PHASE 8 - STAGE 3. The Pre-Delay / Low Cut / High Cut listening set.

    WHAT THIS IS FOR
    ----------------
    The engineering case is already made and is not the question here. The
    neutral path is bit-identical to PHASE 7, pre-delay timing is exact at
    44.1 / 48 / 96 kHz, endpoint automation adds no step the signal lacks,
    nothing survives a bypass, and the focused regression found no new
    failure. None of that says whether the three controls are musically
    useful, so this renders the material that can.

    ONE QUESTION PER CASE
    ---------------------
      1  Pre-Delay      0 / 20 / 80 ms          transient separation
      2  Low Cut        20 / 200 / 400 Hz       low-end cleanup
      3  High Cut       20k / 10k / 6k Hz       tonal darkening
      4  Cut vs Damp    three versions          are the roles distinguishable

    Case 4 is the conceptually important one. High Cut and High Damp both make
    a reverb darker, and if they are perceptually redundant then one of them
    is a control the product does not need. It is the only blind case, because
    it is the only one where the answer could be influenced by knowing which
    file is which.

    SOURCES
    -------
    Four, one per class the stage asks for:

      snare   percussive          the established FU-2 snare stand-in
      vocal   vocal-like          the export set's formant reference
      hats    bright percussion   PHASE 7's noise-based hi-hat stand-in
      bass    bass-heavy          NEW for this set, see makeBassHeavySource

    `bass` is an addition and is named as one in the manifest. The reason is
    the same reason PHASE 7 added `hats`: Low Cut at 200 and 400 Hz can only
    act on energy below those frequencies, and the lowest content anywhere in
    the established material is a 220 Hz fundamental. A gate that cannot
    reveal the change it gates is not a gate.

    LEVEL
    -----
    Nothing is normalised. Low Cut and High Cut legitimately change how much
    energy the tank receives, and matching that away would hide the effect
    rather than isolate it, so every render in cases 1 to 3 uses the same
    source gain and the same downstream gain, and the level differences are
    real.

    Case 4 is the one exception and the exception is the point: that
    comparison is about the ROLE of two controls, and a level difference is
    the fastest way for a listener to mistake "louder" for "different in
    kind". So case 4 is written twice, as rendered and loudness-matched, with
    the gain recorded in the key. Judge level on the unmatched set and role on
    the matched one.

    MIX
    ---
    Mostly the shipping Mix of 30 %, because that is what the controls will be
    used at. A small wet-only subset exists where the tail is what is being
    judged - pre-delay separation, low-end in the tail, decay evolution - and
    those files are labelled, not substituted for the Mix 30 % ones.

    Developer tooling. It lives only in the test target, never runs as part of
    a suite - writing files is not something a test run should do as a side
    effect - and nothing here is reachable from the shipped VST3.
*/
namespace avtest
{

/**
    Writes the PHASE 8 Stage 3 listening set into `directory`.

    Returns the number of files written, or a negative value on failure.
*/
int exportPhase8ListeningSet (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
