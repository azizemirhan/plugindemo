#pragma once

#include "GainStaging.h"

#include <string>
#include <vector>

/**
    Phase 7: the damping default A/B listening set.

    The automated regression cannot answer the question this set exists for.
    It says the new default operating point is measurably healthier - the worst
    500 Hz - 2 kHz deviation from the requested Decay falls from 7.90 % to
    2.32 %, and every band's tail stays single-slope - and it says nothing at
    all about whether the result is a better product. That is a listening
    question, and this renders the material for it.

    Two settings, nothing else different:

        A   Low Damp 20 %, Low Damp Freq 200 Hz, High Damp 50 %, High Damp Freq  6 kHz
        B   Low Damp 20 %, Low Damp Freq 200 Hz, High Damp 45 %, High Damp Freq 10 kHz

    Decay, Size, Mix, Width, Mod Depth, Mod Rate, the diffusion stage, the
    preview trim and the width law are identical on both sides, because a
    listening verdict is only about damping if nothing else moved.

    What it renders
    ---------------
    Five sources, covering the four classes the listening gate asks for: a
    percussive source, two harmonic sustained sources, a full-band musical
    source and a vocal. Each is rendered at the shipping Mix and at 100 % wet -
    the first is what a user hears, the second magnifies the tail so a
    difference that is real but subtle in context can be confirmed.

    Each pair is written twice: as rendered, and loudness-matched. The
    unmatched pair is the truth, because a damping change legitimately changes
    level and hiding that would hide part of the effect. The matched pair exists
    so that tone can be judged without level standing in for it, and the gain
    applied to produce it is recorded in the key rather than folded in quietly.

    Blinding
    --------
    The two sides of every pair are written as letters, the letter mapping is
    permuted, and the permutation differs between sources so an order learned
    on one does not carry to the next. The mapping and every per-file
    measurement live in a key file that is meant to stay unread until the
    answers are written down. Per-file loudness is in the key and not the
    manifest for the same reason: a level column is the mapping in another
    alphabet.

    Developer tooling. It lives only in the test target, never runs as part of
    a suite - writing files is not something a test run should do as a side
    effect - and nothing here is reachable from the shipped VST3.
*/
namespace avtest
{

/**
    Writes the damping default A/B set into `directory`.

    Returns the number of files written, or a negative value on failure.
*/
int exportDampingListeningSet (const std::string& directory, double sampleRate = 48000.0);

/**
    The bright-percussion source this set added, exposed for reuse.

    A hi-hat / shaker stand-in: short noise transients with the low end
    differenced away, at a steady interval, with no resonator in it. It was
    added here because none of the established sources carries enough energy
    above 6 kHz for a change to the high damping corner to act on.

    Visible rather than file-local so that a later set needing a bright source
    uses this one instead of growing a second copy that would drift from it.
    The arithmetic is unchanged, so every render made from it before remains
    reproducible.
*/
[[nodiscard]] std::vector<StereoFrame> makeBrightPercussion (double sampleRate, double seconds);

} // namespace avtest
