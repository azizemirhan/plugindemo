#pragma once

#include <string>

/**
    PHASE 11 - STAGE 4. The narrow-band mono listening pack.

    SIX FILES, TWO CASES, AND THE QUESTION IS NOT A PREFERENCE. It is whether
    a measured stereo-to-mono cancellation in the late path is audible as a
    product problem.

    WHY THIS TEST HAD TO EXIST. Stage 3 measured that the median mono
    fold-down is healthy at every Size and that only 10-18 % of pure tones
    lose more than 6 dB, and recommended reclassifying the condition as
    non-blocking. That recommendation was premature and the brief was right to
    refuse it: PHASE 10's pad listening tested a LEVEL characteristic through
    a Mix, not a stereo-to-mono collapse of the late path alone, so nobody had
    heard the actual failure.

    AND STAGE 4 THEN CONTRADICTED STAGE 3's RECOMMENDATION. The null does not
    stay confined to pure tones. At the PRODUCT DEFAULT - Size 60 %, Decay
    2.5 s - a piano note at D4 loses 7.76 dB when folded to mono, against the
    3.01 dB that summing two decorrelated channels costs. At a small-room
    setting an A3 piano note loses 12.60 dB. Adjacent semitones differ by
    about 4 dB, so a chromatic line in mono would have some notes keep their
    reverb and others lose it.

    So the measurements now say this is a real musical condition rather than a
    sine-generator curiosity. What they cannot say is whether it is
    objectionable, and that is what these files are for.

    WHAT IS DELIBERATELY NOT DONE. The mono renders are NOT normalised back up
    to the stereo renders. That would hide the whole effect. Each mono file is
    the honest downmix of its own stereo file - sum, halve, both channels -
    and the level difference is the measurement made audible.

    Early/Late is 0 throughout, because the condition is in the late path and
    the early layer would mask it. Mix is 100 % for the same reason: at a
    musical Mix most of what reaches the listener is dry signal that folds
    down perfectly, which is realistic and would make the test insensitive.
    A 60 % Mix reference is included for one case so the effect can be judged
    in context as well as in isolation.
*/
namespace avtest
{

/**
    Writes the PHASE 11 narrow-band mono pack into `directory`.

    Six WAVs plus LISTENING.md. Returns the number of files written, or a
    negative value on failure.
*/
int exportPhase11MonoPack (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
