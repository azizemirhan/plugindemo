#pragma once

#include <string>

/**
    PHASE 9 - STAGE 4. The targeted Attack listening set.

    This is not a research set and it is deliberately small. Stage 3 accepted
    the Attack production candidate with exactly one open risk, and this set
    exists to answer it:

        At Mod Depth 0 and high Attack, late-field envelope periodicity rises
        from 16.72 to 37.52 - about 6.9 times the measurement floor of 3.03.
        At the shipping Mod Depth 30 % the effect is much smaller and
        non-monotone. Is the Mod Depth 0 case an audible product defect?

    Three cases, three sources, no more.

      A  the primary risk      100 % wet, Mod Depth 0 and 30, Attack 0/50/100
      B  normal product use    Mod Depth 30, Mix 30 %, Attack 0/25/50/75/100
      C  Attack against Pre-Delay, three files

    BLINDING, and only where it is needed. The high-Attack pair - Mod Depth 0
    against Mod Depth 30 - is written unlabelled, because the whole point is
    that a listener who knows which render the periodicity measurement
    complained about will hear what the measurement predicted. Everything else
    is labelled, because for an ordinary Attack sweep knowing the setting does
    not bias the judgement.

    NO LEVEL NORMALISATION. Stage 3 measured the Attack sweep's integrated wet
    energy variation at 0.096 dB, so there is nothing worth matching away and
    matching would only hide whatever is really there. Identical source gain,
    identical downstream gain, every render.

    Developer tooling. It lives only in the test target, never runs as part of
    a suite, and nothing here is reachable from the shipped VST3.
*/
namespace avtest
{

/**
    Writes the PHASE 9 Stage 4 listening set into `directory`.

    Returns the number of files written, or a negative value on failure.
*/
int exportPhase9ListeningSet (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
