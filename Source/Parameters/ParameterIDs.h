#pragma once

#include <array>
#include <string_view>

/**
    Central registry of parameter identifiers, algorithm modes and colour modes.

    Parameter IDs are part of the saved state and of every host's automation
    map. Once a plugin has shipped they must never be renamed or reordered —
    only appended to, with a bumped version hint.
*/
namespace av::params
{

/*
    Version hints.

    WHAT THIS ACTUALLY DOES, read off JUCE 8.0.15 rather than assumed, because
    the comment that used to sit here - "bumped whenever parameters are
    appended" - described the wrong mechanism and would have caused the
    problem it was trying to avoid.

    `ParameterID`'s hint is consumed in exactly one functional place: the AU
    wrapper's parameter-list ordering, where the list is sorted by identifier
    and then STABLE-SORTED by hint, so lower hints come first
    (juce_audio_plugin_client_AU_1.mm). There is also an AU-only debug
    assertion that fires if any parameter has hint 0. It is not referenced
    anywhere in the VST3 wrapper, and it plays no part in state save or load -
    APVTS keys state by the ID string.

    So appending a parameter does NOT mean bumping one shared constant. A
    shared constant gives the new parameter the SAME hint as the existing
    ones, which places it among them in the identifier sort - exactly what the
    mechanism exists to prevent. And raising the shared constant would change
    every existing parameter's hint at once, which reorders the AU list for
    hosts that have cached it.

    The rule is therefore: each generation of parameters gets its own
    constant, earlier constants NEVER change, and a new parameter uses the
    newest one. Append constants here; never edit one.
*/

/** The nineteen parameters the plugin shipped with. Never change this. */
inline constexpr int versionHintV1 = 1;

/** PHASE 10 and later additions. */
inline constexpr int versionHintV2 = 2;

/** The newest generation, for parameters being added now. */
inline constexpr int versionHintLatest = versionHintV2;

/** Deprecated spelling of versionHintV1, kept so no call site changes
    meaning. Do not use in new code: it reads as "the current hint" and it is
    not - it is the first generation's. */
inline constexpr int versionHint = versionHintV1;

// --- Parameter IDs -------------------------------------------------------

namespace id
{
    inline constexpr const char* mode            = "mode";
    inline constexpr const char* color           = "color";
    inline constexpr const char* preDelay        = "preDelay";
    inline constexpr const char* decay           = "decay";
    inline constexpr const char* size            = "size";
    inline constexpr const char* earlyDiffusion  = "earlyDiffusion";
    inline constexpr const char* lateDiffusion   = "lateDiffusion";
    inline constexpr const char* attack          = "attack";
    inline constexpr const char* modRate         = "modRate";
    inline constexpr const char* modDepth        = "modDepth";
    inline constexpr const char* density         = "density";
    inline constexpr const char* lowDamp         = "lowDamp";
    inline constexpr const char* highDamp        = "highDamp";
    inline constexpr const char* lowDampFreq     = "lowDampFreq";
    inline constexpr const char* highDampFreq    = "highDampFreq";
    inline constexpr const char* width           = "width";
    inline constexpr const char* lowCut          = "lowCut";
    inline constexpr const char* highCut         = "highCut";
    inline constexpr const char* mix             = "mix";
}

// --- Algorithm bank ------------------------------------------------------

/**
    The thirteen algorithm families. Each of these is an independent DSP
    topology, not a preset of a shared engine. The order is part of the saved
    state: append only.
*/
enum class Mode
{
    largeHall = 0,
    brightHall,
    room,
    chamber,
    plate,
    smoothPlate,
    randomSpace,
    chorusSpace,
    ambient,
    cathedral,
    nonlinear,
    chaotic,
    experimental,

    numModes
};

inline constexpr std::array<std::string_view, static_cast<size_t> (Mode::numModes)> modeNames
{
    "Large Hall",
    "Bright Hall",
    "Room",
    "Chamber",
    "Plate",
    "Smooth Plate",
    "Random Space",
    "Chorus Space",
    "Ambient",
    "Cathedral",
    "Nonlinear",
    "Chaotic",
    "Experimental"
};

// --- Colour engine -------------------------------------------------------

/**
    Tonal character. Colour is not an EQ curve: each algorithm is free to map
    it onto its own damping, modulation, diffusion, saturation and noise
    behaviour. See PHASE 17.
*/
enum class Color
{
    classic = 0,
    retro,
    modern,

    numColors
};

inline constexpr std::array<std::string_view, static_cast<size_t> (Color::numColors)> colorNames
{
    "Classic",
    "Retro",
    "Modern"
};

} // namespace av::params
