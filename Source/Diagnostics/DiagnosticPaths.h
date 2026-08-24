#pragma once

#include <string>

namespace av::diag
{

/**
    Where diagnostic sessions are written.

    Deliberately free of JUCE and of any hard-coded user name or project path.
    The location comes from the environment the process is already running in,
    so it is correct on any machine and testable without one.

    The chosen root is reported in every session.json and in summary.txt rather
    than being something the user has to guess.
*/
namespace paths
{

/** Folder name under the user's documents area. */
inline constexpr const char* rootFolderName = "AuroraVerbDiagnostics";

/**
    The user's writable base directory.

    Tried in order:
      AV_DIAGNOSTIC_ROOT   an explicit override, which is what the tests use
      USERPROFILE          the Windows home directory
      HOME                 the same thing elsewhere
      .                    last resort, the working directory

    Never a hard-coded absolute path: a diagnostic tool that writes to somebody
    else's machine layout is worse than one that writes nowhere.
*/
[[nodiscard]] std::string userBaseDirectory();

/** userBaseDirectory() + the root folder name. */
[[nodiscard]] std::string diagnosticRoot();

/**
    A session folder name: YYYY-MM-DD_HH-mm-ss_instance-id.

    The timestamp is passed in rather than read from the clock here, so the
    naming can be tested deterministically and so the caller controls which
    clock it comes from.
*/
[[nodiscard]] std::string sessionFolderName (const std::string& timestamp,
                                             const std::string& instanceId);

/** Joins two path fragments with a single separator, normalised for the platform. */
[[nodiscard]] std::string join (const std::string& a, const std::string& b);

/** Converts a path to the platform's own separator throughout. */
[[nodiscard]] std::string normalise (const std::string& path);

/** Creates a directory and any missing parents. Returns false on failure. */
[[nodiscard]] bool createDirectories (const std::string& path);

/** True if a directory exists and a file can be created inside it. */
[[nodiscard]] bool isWritable (const std::string& path);

/** Local time as YYYY-MM-DD_HH-mm-ss. Message thread only. */
[[nodiscard]] std::string timestampNow();

/** Removes characters that are not safe in a file name. */
[[nodiscard]] std::string sanitise (const std::string& text);

} // namespace paths
} // namespace av::diag
