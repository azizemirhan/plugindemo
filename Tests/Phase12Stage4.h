#pragma once

#include <string>

/**
    PHASE 12 - STAGE 4. The compact blind pack for human structural validation.

    RESEARCH ONLY. Twelve files. Each file is ONE comparison: the same source
    through one reverb, half a second of silence, then the same source through
    another. The filenames are numbers and say nothing; KEY.txt says what each
    file is and must not be opened before listening.

      6 files  Hall against Room B   (4 centred source, 2 hard-left source)
      4 files  Hall against Plate A  (centred source)
      1 file   Room B against Plate A
      1 file   Hall against Hall: a catch trial, the two halves are identical

    100 % wet, Early/Late at 0, every other control equal within a file, and
    the second reverb level-matched to the first by integrated energy.
    Rendered at 48 kHz; the sample-rate argument is accepted for interface
    parity and unused.

    Returns the number of audio files written, or a negative value on failure.
*/
namespace avtest
{

int exportPhase12Stage4Pack (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
