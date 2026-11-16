#pragma once

#include <string>

/**
    PHASE 12 - STAGE 2. The impostor listening pack.

    NOT A MODE IDENTIFICATION TEST. No Stage 1 candidate survived the
    objective analysis, so there is nothing yet to identify. This pack tests
    the negative result instead.

    For each of the five candidates it renders the candidate at the product
    defaults beside its closest impostor: a DIFFERENT Mode whose ordinary user
    controls were searched to imitate it. The measurement says each pair is
    the same reverb. A listener who agrees confirms the candidates are
    presets; a listener who hears a clear difference has found something the
    yardstick cannot see, which is where a real Mode dimension would be.

    100 % wet and level-matched by integrated energy. Five pairs, five
    sources, two files each. Rendered at 48 kHz, the rate the analysis runs
    at; the sample-rate argument is accepted for interface parity and unused.

    Returns the number of files written, or a negative value on failure.
*/
namespace avtest
{

int exportPhase12ImpostorPack (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
