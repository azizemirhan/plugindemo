#pragma once

#include <string>

/**
    PHASE 12 - STAGE 7, track A. The Ambient blind listening pack.

    RESEARCH ONLY. Source/ IS NOT CHANGED. Nothing here is a candidate and
    nothing here moves a gate: Ambient A1b's objective tonal gate (octave
    bands within 1 dB of Hall) stays FAILED at 1.82 dB whatever is heard.

    Twenty-seven files, s7-01.wav to s7-27.wav. Each file is ONE comparison:
    the same source through one reverb, half a second of silence, then the
    same source through another. Filenames are numbers; KEY.txt says what
    each file is and must not be opened before listening.

      10  Hall against Ambient A1b, centred source
       3  the same, folded to mono
       2  the same, hard-left source
       4  A1b against AT5a (the tonally repaired Ambient, 8 % direct)
       3  TONE ONLY: Hall against Hall with A1b's octave fingerprint put on
          it by a linear-phase filter
       3  STRUCTURE ONLY: Hall against A1b with that fingerprint taken off
       2  catch trials: the two halves are identical

    The two filtered groups are LISTENING CONTROLS. An output equaliser is
    not an Ambient candidate (pre-registration, Stage 6, 3.2) and the
    filtered A1b is not one either; it exists to separate what the tone does
    from what the structure does.

    100 % wet, Early/Late at 0, every visible control equal within a file,
    the second reverb level-matched to the first by integrated energy.
    Rendered at 48 kHz; the sample-rate argument is accepted for interface
    parity and unused.

    Returns the number of audio files written, or a negative value on failure.
*/
namespace avtest
{

int exportPhase12Stage7AmbientPack (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
