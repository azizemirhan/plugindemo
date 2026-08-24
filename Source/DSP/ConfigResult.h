#pragma once

namespace av::dsp
{

/**
    Outcome of a configuration change that could exceed what prepare() reserved.

    Structures whose delay lengths are chosen at run time — a diffuser pattern,
    an FDN size — can be asked for more than their allocated capacity. Growing
    the buffer would mean allocating on the audio thread, and silently clamping
    the request would mean the reverb quietly stops being the reverb that was
    asked for. Neither is acceptable, so the call reports what happened and
    leaves the object untouched.

    Callers on the audio thread should treat anything other than `ok` as "keep
    the previous configuration"; the object is guaranteed to still be in its
    last valid state.
*/
enum class ConfigResult
{
    ok = 0,

    /** One or more delays were longer than the capacity reserved by prepare().
        Nothing was applied. Reserve more capacity in prepare() instead. */
    exceedsCapacity,

    /** The object has not been prepared yet, or the arguments were invalid
        (null pointer, non-positive sample rate, zero-length pattern). */
    invalidArgument
};

[[nodiscard]] inline constexpr bool succeeded (ConfigResult r) noexcept
{
    return r == ConfigResult::ok;
}

[[nodiscard]] inline constexpr const char* describe (ConfigResult r) noexcept
{
    switch (r)
    {
        case ConfigResult::ok:              return "ok";
        case ConfigResult::exceedsCapacity: return "delay exceeds the reserved capacity";
        case ConfigResult::invalidArgument: return "invalid argument or not prepared";
    }

    return "unknown";
}

} // namespace av::dsp
