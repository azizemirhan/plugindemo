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

/** Bumped whenever parameters are appended. Never decreases. */
inline constexpr int versionHint = 1;

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
