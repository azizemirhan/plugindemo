#include "DiagnosticPaths.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace av::diag::paths
{

namespace
{
    /** getenv, but returning an empty string rather than a null pointer. */
    std::string environmentVariable (const char* name)
    {
       #if defined (_MSC_VER)
        // getenv is deprecated under MSVC's secure CRT warnings and _dupenv_s
        // is the sanctioned replacement.
        char* value = nullptr;
        size_t length = 0;

        if (_dupenv_s (&value, &length, name) != 0 || value == nullptr)
            return {};

        std::string result (value);
        std::free (value);
        return result;
       #else
        const char* value = std::getenv (name);
        return value != nullptr ? std::string (value) : std::string {};
       #endif
    }
}

std::string userBaseDirectory()
{
    if (auto override_ = environmentVariable ("AV_DIAGNOSTIC_ROOT"); ! override_.empty())
        return override_;

    if (auto profile = environmentVariable ("USERPROFILE"); ! profile.empty())
        return profile;

    if (auto home = environmentVariable ("HOME"); ! home.empty())
        return home;

    return ".";
}

std::string diagnosticRoot()
{
    return join (userBaseDirectory(), rootFolderName);
}

std::string normalise (const std::string& path)
{
   #if defined (_WIN32)
    // One separator throughout. Joining a Windows base onto a forward-slash
    // fragment worked, but produced paths like
    //   C:\Users\Someone/AuroraVerbDiagnostics/2026-08-05_.../session.json
    // in the reports, which reads as though two different conventions are in
    // play and is awkward to paste into anything.
    std::string out = path;

    for (char& c : out)
        if (c == '/')
            c = '\\';

    return out;
   #else
    return path;
   #endif
}

std::string join (const std::string& a, const std::string& b)
{
    if (a.empty())
        return normalise (b);

    if (b.empty())
        return normalise (a);

    const char last = a.back();
    const bool hasSeparator = (last == '/' || last == '\\');

   #if defined (_WIN32)
    constexpr const char* separator = "\\";
   #else
    constexpr const char* separator = "/";
   #endif

    return normalise (hasSeparator ? a + b : a + separator + b);
}

std::string sanitise (const std::string& text)
{
    std::string out;
    out.reserve (text.size());

    for (char c : text)
    {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                       || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';

        out += safe ? c : '-';
    }

    return out.empty() ? std::string ("unnamed") : out;
}

std::string sessionFolderName (const std::string& timestamp, const std::string& instanceId)
{
    return sanitise (timestamp) + "_" + sanitise (instanceId);
}

bool createDirectories (const std::string& path)
{
    std::error_code error;

    // create_directories returns false when the directory already exists, which
    // is not a failure — the error code is what distinguishes the two.
    std::filesystem::create_directories (std::filesystem::path (path), error);

    if (error)
        return false;

    return std::filesystem::is_directory (std::filesystem::path (path), error);
}

bool isWritable (const std::string& path)
{
    std::error_code error;

    if (! std::filesystem::is_directory (std::filesystem::path (path), error))
        return false;

    // Proved by writing, not by inspecting permissions: a read-only share, a
    // full disk and a missing ACL all look different to a permission check and
    // identical to a failed open.
    const std::string probe = join (path, ".auroraverb-write-probe");

    {
        std::ofstream stream (probe, std::ios::binary | std::ios::trunc);

        if (! stream.is_open())
            return false;

        stream << "probe";

        if (! stream.good())
            return false;
    }

    std::filesystem::remove (std::filesystem::path (probe), error);
    return true;
}

std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t (now);

    std::tm local {};

   #if defined (_MSC_VER)
    localtime_s (&local, &time);
   #else
    localtime_r (&time, &local);
   #endif

    char buffer[32] {};
    std::snprintf (buffer, sizeof (buffer), "%04d-%02d-%02d_%02d-%02d-%02d",
                   local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                   local.tm_hour, local.tm_min, local.tm_sec);

    return buffer;
}

} // namespace av::diag::paths
