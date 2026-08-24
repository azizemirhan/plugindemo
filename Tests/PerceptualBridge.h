#pragma once

#include <string>

/**
    Phase 6 / FU-1: the targeted listening set.

    Turns the FU-1 classification into something a listener can answer. Renders
    only; nothing here measures, tracks or classifies, and the production DSP is
    driven at its shipping defaults through the same interfaces the plugin uses.

    Round one is six candidates in three pairs, one pair per frequency region.
    The set is blind: letters are permuted, the permutation differs between the
    two sources, and the mapping and every per-file measurement live in a key
    file that is meant to stay unread until the listening is done.
*/
namespace avtest
{

/**
    Writes round one of the listening set into `directory`.

    Returns the number of files written, or a negative value on failure.
*/
int exportPerceptualBridgeSet (const std::string& directory, double sampleRate = 48000.0);

/**
    Writes round 1b, the modulation test, into `directory`.

    Three pairs, each one mode heard twice: Mod Depth 0 against the shipping
    30%, with the same band and the same excitation sample on both sides. The
    round-one frequency confound does not exist here.

    Returns the number of files written, or a negative value on failure.
*/
int exportModulationPerceptualSet (const std::string& directory, double sampleRate = 48000.0);

/**
    Writes round 1c, the counterbalanced repeat, into `directory`.

    Six pairs: three modes, each heard twice on a different excitation with the
    treatment on opposite sides of the pair. Round 1b could not separate a real
    modulation effect from a fixed preference for one side of the pair; this can,
    because following the modulation and following the position now predict
    different answers on the same data.

    Returns the number of files written, or a negative value on failure.
*/
int exportCounterbalancedModulationSet (const std::string& directory,
                                        double sampleRate = 48000.0);

/**
    Writes round 2, the high band, into `directory`.

    Eight single-file rating trials - four modes, each rendered twice on a
    different excitation - plus two whole-tail context renders. Nothing is
    compared against anything: each trial is rated on its own for how much bell
    or metallic character it carries, whether that character is objectionable,
    and whether the tail is pleasant, recorded as three separate endpoints.

    Mod Depth is not under test and sits at the shipping value throughout.

    Returns the number of files written, or a negative value on failure.
*/
int exportHighBandListeningSet (const std::string& directory, double sampleRate = 48000.0);

/**
    Writes round 2b, the stimulus-alone control, into `directory`.

    Round 2s eight targeted excitations with no tank: the same seeds, the same
    centre frequencies, the same ERB widths and the same duration, so that how
    much of round 2s metallicity belongs to the excitation rather than to the
    reverb can be read off directly.

    Returns the number of files written, or a negative value on failure.
*/
int exportStimulusAnchorSet (const std::string& directory, double sampleRate = 48000.0);

/**
    Writes round 2c, the decision gate, into `directory`.

    Three full-band sources - no band limiting anywhere - each rendered dry, at
    100% wet and at the shipping Mix. Nine single-file rating trials. It asks
    only whether the metallic character is objectionable enough in real use to
    justify a voicing intervention, and localises nothing.

    Returns the number of files written, or a negative value on failure.
*/
int exportDecisionGateSet (const std::string& directory, double sampleRate = 48000.0);

} // namespace avtest
