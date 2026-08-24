#pragma once

#include <string>
#include <vector>

#include "DiagnosticTypes.h"

namespace av::diag
{

/**
    Turning counters into a verdict, on the reporting thread.

    Nothing in this header runs on the audio thread. It builds strings, does
    division, sorts things and formats JSON — all of which is exactly why it
    lives on the other side of the queue.

    The verdict is technical only. Nothing here decides whether the reverb
    sounds good; it decides whether the plugin behaved correctly, which is a
    question with a defensible answer.
*/

/** Every threshold in one place, with the reason it has the value it does. */
namespace thresholds
{
    /**
        Fraction of the block budget the p99 callback may use.

        A callback that takes the whole block period leaves nothing for the rest
        of the session, and hosts run many plugins. A fifth is a conventional
        working figure for a single effect and leaves an order of magnitude of
        margin over what PHASE 5C measured (about 1.2 % of a core).
    */
    inline constexpr double cpuP99BudgetFraction = 0.20;

    /** Absolute peak above this is a warning: the output is over full scale. */
    inline constexpr double outputPeakWarning = 1.0;

    /**
        Worst third-octave mono cancellation, in dB.

        The same 4 dB the DSP phases were held to, so the live verdict and the
        offline measurements are talking about the same thing.
    */
    inline constexpr double monoCancellationWarningDb = 4.0;

    /**
        DC offset as a fraction of RMS.

        A reverb should not produce DC. A few thousandths is rounding; a
        hundredth is a fault worth looking at.
    */
    inline constexpr double dcFractionWarning = 0.01;

    /** Morph timeouts above this fraction of morph starts is a warning. */
    inline constexpr double morphTimeoutFractionWarning = 0.25;

    /**
        Diagnostic overhead over the same DSP with diagnostics off.

        Telemetry that changes what it measures is not telemetry. Ten per cent
        of the DSP cost is the point at which the numbers stop describing the
        plugin and start describing the instrumentation.
    */
    inline constexpr double diagnosticOverheadWarningFraction = 0.10;
}

enum class Verdict : int
{
    pass = 0,
    passWithWarnings = 1,
    fail = 2
};

[[nodiscard]] const char* describe (Verdict verdict) noexcept;
[[nodiscard]] const char* describe (EventType type) noexcept;
[[nodiscard]] const char* describe (ParameterId parameter) noexcept;
[[nodiscard]] const char* describe (MorphReason reason) noexcept;
[[nodiscard]] const char* describe (SeekReason reason) noexcept;
[[nodiscard]] const char* describe (Level level) noexcept;

/** Derived statistics the audio thread was not allowed to compute. */
struct DiagnosticStatistics
{
    double meanProcessingMicroseconds = 0.0;
    double medianProcessingMicroseconds = 0.0;
    double p95ProcessingMicroseconds = 0.0;
    double p99ProcessingMicroseconds = 0.0;
    double worstProcessingMicroseconds = 0.0;

    /*
        Two budgets, reported separately because conflating them is what made
        the first session's figures misleading.

        The prepare budget is the block period the host announced. The runtime
        budget is the period of the block a callback was actually given. When a
        host announces 512 and delivers 1, those differ by a factor of 512, and
        a ratio against the announced figure says a callback used 0.3 % of its
        budget while it was in fact 40 % over.
    */
    double prepareBudgetMicroseconds = 0.0;
    double meanBudgetFraction = 0.0;      /**< Against the prepare budget. */
    double p99BudgetFraction = 0.0;

    double worstRuntimeBudgetRatio = 0.0; /**< Against the block's own period. */
    int    worstRuntimeBudgetBlockSize = 0;
    double worstRuntimeBudgetMicroseconds = 0.0;

    double lastBlockBudgetMicroseconds = 0.0;
    double lastBlockRatio = 0.0;
    int    lastBlockSize = 0;

    double inputRmsLeft = 0.0;
    double inputRmsRight = 0.0;
    double outputRmsLeft = 0.0;
    double outputRmsRight = 0.0;

    double inputPeakDbLeft = 0.0;
    double inputPeakDbRight = 0.0;
    double outputPeakDbLeft = 0.0;
    double outputPeakDbRight = 0.0;

    double stereoCorrelation = 0.0;
    double stereoBalanceDb = 0.0;
    double monoDownmixDb = 0.0;
    double crestFactor = 0.0;

    double dcLeft = 0.0;
    double dcRight = 0.0;
    double dcFractionOfRms = 0.0;

    double sessionSeconds = 0.0;
};

/**
    Percentile from the logarithmic histogram, in microseconds.

    Returns the upper edge of the bin the percentile falls in, which is an
    over-estimate bounded by the bin width — 50 % at these ratios. Reported as
    what it is rather than interpolated into a precision the histogram does not
    have: the alternative is inventing a number between two edges and presenting
    it as measured.
*/
[[nodiscard]] double percentileMicroseconds (const DiagnosticCounters& counters, double fraction);

[[nodiscard]] DiagnosticStatistics computeStatistics (const DiagnosticSnapshot& snapshot);

/** One technical problem found in a session. */
struct DiagnosticWarning
{
    std::string rule;      /**< Stable identifier, for machine readers. */
    std::string detail;    /**< What was measured. */
    bool fatal = false;    /**< True makes the whole session FAIL. */
};

/**
    Applies the rules to a finished session.

    Every rule is explicit and testable, and every threshold comes from the
    namespace above rather than from a number typed in here.
*/
[[nodiscard]] std::vector<DiagnosticWarning> evaluate (const DiagnosticSnapshot& snapshot,
                                                       const DiagnosticStatistics& statistics);

[[nodiscard]] Verdict verdictFor (const std::vector<DiagnosticWarning>& warnings) noexcept;

/** Metadata the audio thread never sees, gathered once on the message thread. */
struct DiagnosticSessionInfo
{
    std::string pluginName = "AuroraVerb";
    std::string pluginVersion;
    std::string buildConfiguration;
    std::string architectureName;

    std::string hostName = "unavailable";
    std::string hostVersion = "unavailable";
    std::string pluginFormat = "unavailable";

    std::string instanceId;
    std::string startTimestamp;
    std::string sessionPath;

    Level level = Level::off;
    bool captureEnabled = false;

    /** The reverb engine is part of every build. Kept as a reported field
        rather than assumed, because for a long time it was not: the engine was
        compiled in only under a development flag, and no report said so in a
        way that would have caught a default build shipping a silent wet path. */
    bool engineActive = true;

    /** production / development / validation. */
    std::string buildProfile;

    bool captureCompiledIn = false;
    bool diagnosticsCompiledIn = false;
};

/**
    How the host is named in every report that names it.

    One function rather than one line per file: summary.txt and environment.txt
    described the same host differently in the first session, because each
    formatted it itself.
*/
[[nodiscard]] std::string describeHost (const DiagnosticSessionInfo& info);

/** JSON for session.json. Deterministic field order, UTF-8, C-locale decimals. */
[[nodiscard]] std::string buildSessionJson (const DiagnosticSessionInfo& info,
                                            const DiagnosticSnapshot& snapshot,
                                            const DiagnosticStatistics& statistics,
                                            const std::vector<DiagnosticWarning>& warnings);

/** The human-readable summary.txt. */
[[nodiscard]] std::string buildSummaryText (const DiagnosticSessionInfo& info,
                                            const DiagnosticSnapshot& snapshot,
                                            const DiagnosticStatistics& statistics,
                                            const std::vector<DiagnosticWarning>& warnings);

/** Header row for timeline.csv. */
[[nodiscard]] std::string timelineHeader();

/** One timeline.csv row from a snapshot. */
[[nodiscard]] std::string timelineRow (const DiagnosticSnapshot& snapshot,
                                       const DiagnosticStatistics& statistics);

/** One events.jsonl line. */
[[nodiscard]] std::string eventJsonLine (const DiagnosticEvent& event, double sampleRate);

/** A double in a form every JSON parser and every locale reads the same way. */
[[nodiscard]] std::string jsonNumber (double value, int decimals = 6);

/** Escapes a string for JSON. */
[[nodiscard]] std::string jsonString (const std::string& text);

} // namespace av::diag
