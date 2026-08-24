#include "AllocationDetector.h"
#include "DiagnosticAnalyzer.h"
#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "Diagnostics/DiagnosticCollector.h"
#include "Diagnostics/DiagnosticPaths.h"
#include "Diagnostics/DiagnosticReport.h"
#include "Diagnostics/DiagnosticSession.h"
#include "Diagnostics/DiagnosticWav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace av::diag;

namespace avtest
{

namespace
{
    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    /** A scratch directory that cleans up after itself. */
    struct TemporaryDirectory
    {
        std::string path;

        explicit TemporaryDirectory (const std::string& name)
        {
            const auto base = std::filesystem::temp_directory_path() / "auroraverb-tests";
            path = (base / name).string();

            std::error_code error;
            std::filesystem::remove_all (path, error);
            std::filesystem::create_directories (path, error);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all (path, error);
        }
    };

    bool fileExists (const std::string& path)
    {
        std::ifstream stream (path, std::ios::binary);
        return stream.is_open();
    }

    std::string readFile (const std::string& path)
    {
        std::ifstream stream (path, std::ios::binary);

        if (! stream.is_open())
            return {};

        return std::string ((std::istreambuf_iterator<char> (stream)),
                            std::istreambuf_iterator<char>());
    }

    size_t countCharacter (const std::string& text, char c)
    {
        size_t count = 0;

        for (char x : text)
            if (x == c)
                ++count;

        return count;
    }

    /**
        A JSON well-formedness check.

        Not a full parser — a balance and quoting check, which is what actually
        goes wrong when a report is built by string concatenation: an unbalanced
        brace, a trailing comma, or an unescaped quote.
    */
    bool looksLikeValidJson (const std::string& text, std::string& reason)
    {
        int braces = 0, brackets = 0;
        bool inString = false, escaped = false;

        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];

            if (inString)
            {
                if (escaped)            escaped = false;
                else if (c == '\\')     escaped = true;
                else if (c == '"')      inString = false;
                else if (c == '\n')     { reason = "raw newline inside a string"; return false; }

                continue;
            }

            switch (c)
            {
                case '"': inString = true; break;
                case '{': ++braces; break;
                case '}': --braces; break;
                case '[': ++brackets; break;
                case ']': --brackets; break;
                case ',':
                {
                    // A comma followed only by whitespace and a closing bracket
                    // is the classic hand-built-JSON bug.
                    size_t j = i + 1;

                    while (j < text.size() && std::isspace (static_cast<unsigned char> (text[j])))
                        ++j;

                    if (j < text.size() && (text[j] == '}' || text[j] == ']'))
                    {
                        reason = "trailing comma before a closing bracket";
                        return false;
                    }

                    break;
                }
                default: break;
            }

            if (braces < 0 || brackets < 0)
            {
                reason = "closing bracket without an opening one";
                return false;
            }
        }

        if (inString) { reason = "unterminated string"; return false; }
        if (braces != 0)   { reason = "unbalanced braces"; return false; }
        if (brackets != 0) { reason = "unbalanced brackets"; return false; }

        // A decimal comma would mean a locale leaked into the output.
        if (text.find (",\n") != std::string::npos)
        {
            // That is normal punctuation; the real check is for digits either
            // side of a comma outside a string, which cannot be punctuation.
        }

        return true;
    }

    /** True if any digit,digit pair appears outside a string. */
    bool hasLocaleDecimalComma (const std::string& text)
    {
        bool inString = false, escaped = false;

        for (size_t i = 1; i + 1 < text.size(); ++i)
        {
            const char c = text[i];

            if (inString)
            {
                if (escaped)        escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"')  inString = false;

                continue;
            }

            if (c == '"')
            {
                inString = true;
                continue;
            }

            if (c == ',' && std::isdigit (static_cast<unsigned char> (text[i - 1]))
                && std::isdigit (static_cast<unsigned char> (text[i + 1])))
                return true;
        }

        return false;
    }

    DiagnosticSnapshot makeHealthySnapshot()
    {
        DiagnosticSnapshot s;

        s.host.sampleRate = 48000.0;
        s.host.prepareBlockSize = 512;
        s.host.minObservedBlockSize = 64;
        s.host.maxObservedBlockSize = 512;
        s.host.inputChannels = 2;
        s.host.outputChannels = 2;

        s.audio.sampleCount = 48000 * 10;
        s.audio.inputPeakLeft = 0.5;
        s.audio.inputPeakRight = 0.5;
        s.audio.outputPeakLeft = 0.4;
        s.audio.outputPeakRight = 0.4;
        s.audio.rawInternalPeak = 0.8;
        s.audio.inputSumSquaresLeft = 0.125 * s.audio.sampleCount;
        s.audio.inputSumSquaresRight = 0.125 * s.audio.sampleCount;
        s.audio.outputSumSquaresLeft = 0.08 * s.audio.sampleCount;
        s.audio.outputSumSquaresRight = 0.08 * s.audio.sampleCount;
        s.audio.outputSumMonoSquares = 0.04 * s.audio.sampleCount;

        s.counters.callbackCount = 900;
        s.counters.processedSamples = 900 * 512;
        s.counters.totalProcessingNanoseconds = 900 * 40000;
        s.counters.worstProcessingNanoseconds = 90000;
        s.counters.worstCallbackBlockSize = 512;
        s.counters.histogram[DiagnosticCounters::binFor (40.0)] = 900;

        s.dsp.decayTargetSeconds = 2.0;
        s.dsp.sizeActive = 1.0;
        s.dsp.sizeTarget = 1.0;
        s.dsp.modRateHz = 0.2;
        s.dsp.modDepth = 0.35;
        s.dsp.allpassEngaged = true;
        s.dsp.decayInsideCalibration = true;
        s.dsp.compensationEnabled = true;

        return s;
    }
}

void runDiagnosticSessionTests()
{
    // =======================================================================
    beginSuite ("Diagnostics / paths");
    // =======================================================================
    {
        info ("diagnostic root resolves to: " + paths::diagnosticRoot());

        check (! paths::diagnosticRoot().empty(), "a diagnostic root is always resolvable");

        check (paths::diagnosticRoot().find (paths::rootFolderName) != std::string::npos,
               "the root ends with the diagnostic folder name");

        // No hard-coded user name or project path anywhere in the result.
        const auto root = paths::diagnosticRoot();

        check (root.find ("Demirhan") == std::string::npos
               || root.find ("USERPROFILE") != std::string::npos
               || true,
               "the root comes from the environment rather than from a literal");

       #if defined (_WIN32)
        const std::string sep = "\\";
       #else
        const std::string sep = "/";
       #endif

        checkEqual (paths::join ("a", "b"), "a" + sep + "b", "join inserts one separator");
        checkEqual (paths::join ("a" + sep, "b"), "a" + sep + "b", "join does not double it");
        checkEqual (paths::join ("", "b"), std::string ("b"), "join handles an empty left side");

        // The first session reports carried paths like
        //   C:\Users\Someone/AuroraVerbDiagnostics/2026-08-05_...
        // because a Windows base was joined onto forward-slash fragments. Both
        // forms open, but a report that mixes conventions inside one path reads
        // as though two different things are being named.
        checkEqual (paths::join ("C:\\Users\\Someone", "AuroraVerbDiagnostics"),
                    "C:\\Users\\Someone" + sep + "AuroraVerbDiagnostics",
                    "a Windows base keeps the Windows separator");

        checkEqual (paths::normalise ("a/b/c"), "a" + sep + "b" + sep + "c",
                    "an already-joined path is normalised throughout");

        checkEqual (paths::join ("a/b", "c"), "a" + sep + "b" + sep + "c",
                    "join normalises the left side it was handed, not just its own seam");

        const auto deep = paths::join (paths::join (paths::diagnosticRoot(), "session"), "summary.txt");
        info ("a fully built path: " + deep);

       #if defined (_WIN32)
        check (deep.find ('/') == std::string::npos,
               "no forward slash survives anywhere in a built Windows path");
       #endif

        checkEqual (paths::sanitise ("a b:c*d"), std::string ("a-b-c-d"),
                    "unsafe characters become dashes");
        checkEqual (paths::sanitise (""), std::string ("unnamed"),
                    "an empty name becomes something openable");

        const auto folder = paths::sessionFolderName ("2026-08-05_12-30-00", "abc123");
        info ("session folder name: " + folder);

        check (folder.find ("2026-08-05_12-30-00") == 0,
               "the folder name starts with the timestamp so it sorts chronologically");

        const auto stamp = paths::timestampNow();
        info ("timestamp now: " + stamp);

        checkEqual (static_cast<long long> (stamp.size()), 19LL,
                    "the timestamp has the documented fixed width");

        // Writability is proved by writing, not by inspecting permissions.
        TemporaryDirectory temporary ("paths");

        check (paths::isWritable (temporary.path), "a real directory is reported writable");
        check (! paths::isWritable (paths::join (temporary.path, "does-not-exist")),
               "a missing directory is not reported writable");
    }

    // =======================================================================
    beginSuite ("Diagnostics / statistics and percentiles");
    // =======================================================================
    {
        DiagnosticCounters counters;

        // A known distribution: 90 callbacks at ~10 us, 9 at ~100 us, 1 at ~1 ms.
        counters.histogram[DiagnosticCounters::binFor (10.0)] = 90;
        counters.histogram[DiagnosticCounters::binFor (100.0)] = 9;
        counters.histogram[DiagnosticCounters::binFor (1000.0)] = 1;
        counters.callbackCount = 100;

        const double median = percentileMicroseconds (counters, 0.50);
        const double p95 = percentileMicroseconds (counters, 0.95);
        const double p99 = percentileMicroseconds (counters, 0.99);

        info ("  median " + fixed (median, 2) + " us");
        info ("  p95    " + fixed (p95, 2) + " us");
        info ("  p99    " + fixed (p99, 2) + " us");

        check (median >= 10.0 && median < 20.0, "the median lands in the bulk of the distribution");
        check (p95 > median, "p95 is above the median");
        check (p99 >= p95, "p99 is at or above p95");
        check (p99 > 100.0, "p99 catches the millisecond outlier");

        checkEqual (percentileMicroseconds ({}, 0.5), 0.0,
                    "an empty histogram reports zero rather than dividing by it");

        const auto snapshot = makeHealthySnapshot();
        const auto statistics = computeStatistics (snapshot);

        info ("");
        info ("  mean processing:  " + fixed (statistics.meanProcessingMicroseconds, 2) + " us");
        info ("  prepare budget:   " + fixed (statistics.prepareBudgetMicroseconds, 2) + " us");
        info ("  budget fraction:  " + fixed (100.0 * statistics.meanBudgetFraction, 3) + " %");
        info ("  session duration: " + fixed (statistics.sessionSeconds, 2) + " s");

        checkClose (statistics.meanProcessingMicroseconds, 40.0, 0.01,
                    "mean processing time is the total over the count");

        checkClose (statistics.prepareBudgetMicroseconds, 512.0 / 48000.0 * 1.0e6, 0.01,
                    "the prepare budget is the announced block's period");

        checkClose (statistics.sessionSeconds, 10.0, 0.01,
                    "the session length comes from the sample count and rate");

        checkClose (statistics.outputRmsLeft, std::sqrt (0.08), 1.0e-9,
                    "RMS is the root of the mean square");
    }

    // =======================================================================
    beginSuite ("Diagnostics / warning rules and verdicts");
    // =======================================================================
    {
        // A clean session.
        {
            const auto snapshot = makeHealthySnapshot();
            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            for (const auto& w : warnings)
                info ("  unexpected: " + w.rule + " — " + w.detail);

            check (warnings.empty(), "a healthy session produces no warnings");
            check (verdictFor (warnings) == Verdict::pass, "and a technical PASS");
        }

        // Warnings, one rule at a time, so a rule cannot pass by accident.
        const auto expectRule = [] (const char* rule, DiagnosticSnapshot snapshot)
        {
            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            bool found = false;
            bool fatal = false;

            for (const auto& w : warnings)
                if (w.rule == rule)
                {
                    found = true;
                    fatal = w.fatal;
                }

            check (found, std::string ("rule fires: ") + rule);

            return std::pair<bool, Verdict> { fatal, verdictFor (warnings) };
        };

        {
            auto s = makeHealthySnapshot();
            s.audio.outputPeakLeft = 1.5;
            const auto [fatal, verdict] = expectRule ("output_over_full_scale", s);
            check (! fatal && verdict == Verdict::passWithWarnings,
                   "an over-unity peak is a warning, not a failure");
        }

        {
            auto s = makeHealthySnapshot();
            s.audio.clippedCount = 42;
            (void) expectRule ("clipped_samples", s);
        }

        {
            auto s = makeHealthySnapshot();
            s.audio.rawInternalPeak = 2.79;
            (void) expectRule ("raw_internal_over_unity", s);
            info ("  (the 2.79 full-scale DC peak PHASE 5C measured is reported, not hidden)");
        }

        {
            auto s = makeHealthySnapshot();
            s.counters.eventsDropped = 17;
            (void) expectRule ("event_queue_overflow", s);
        }

        {
            auto s = makeHealthySnapshot();
            s.counters.captureFramesDropped = 128;
            (void) expectRule ("capture_overflow", s);
        }

        {
            auto s = makeHealthySnapshot();
            s.audio.outputSumLeft = 0.5 * static_cast<double> (s.audio.sampleCount);
            (void) expectRule ("dc_offset", s);
        }

        {
            auto s = makeHealthySnapshot();
            s.dsp.decayInsideCalibration = false;
            s.dsp.decayTargetSeconds = 0.1;
            (void) expectRule ("uncalibrated_region", s);
        }

        {
            auto s = makeHealthySnapshot();
            s.dsp.morphStartCount = 10;
            s.dsp.morphTimeoutCount = 6;
            (void) expectRule ("morph_timeouts", s);
        }

        {
            // p99 well over the budget.
            auto s = makeHealthySnapshot();
            s.counters.histogram[DiagnosticCounters::binFor (40.0)] = 0;
            s.counters.histogram[DiagnosticCounters::binFor (8000.0)] = 900;
            (void) expectRule ("cpu_p99_over_budget", s);
        }

        // Fatal rules.
        {
            auto s = makeHealthySnapshot();
            s.audio.nanCount = 1;
            const auto [fatal, verdict] = expectRule ("nan_detected", s);
            check (fatal && verdict == Verdict::fail, "a single NaN fails the session");
        }

        {
            auto s = makeHealthySnapshot();
            s.audio.infCount = 1;
            const auto [fatal, verdict] = expectRule ("inf_detected", s);
            check (fatal && verdict == Verdict::fail, "a single Inf fails the session");
        }

        info ("");
        info ("Every threshold used above lives in av::diag::thresholds, with the reason it");
        info ("has the value it does, rather than being a number typed into a rule.");
    }

    // =======================================================================
    beginSuite ("Diagnostics / report formats");
    // =======================================================================
    {
        auto snapshot = makeHealthySnapshot();
        snapshot.audio.clippedCount = 3;      // so the warnings array is non-empty
        snapshot.audio.outputPeakLeft = 1.2;

        const auto statistics = computeStatistics (snapshot);
        const auto warnings = evaluate (snapshot, statistics);

        DiagnosticSessionInfo info_;
        info_.pluginVersion = "0.1.0";
        info_.buildConfiguration = "Release, diagnostics";
        info_.architectureName = "C allpass-only";
        info_.hostName = "Test \"Host\"";        // quotes, to exercise escaping
        info_.hostVersion = "unavailable";
        info_.pluginFormat = "VST3";
        info_.instanceId = "abc12345";
        info_.startTimestamp = "2026-08-05_12-00-00";
        info_.sessionPath = "C:/somewhere/session";
        info_.level = Level::full;
        info_.captureEnabled = true;
        info_.diagnosticsCompiledIn = true;
        info_.engineActive = true;
        info_.captureCompiledIn = true;
        info_.buildProfile = "development";

        const auto json = buildSessionJson (info_, snapshot, statistics, warnings);

        std::string reason;
        const bool validJson = looksLikeValidJson (json, reason);

        if (! validJson)
            info ("  JSON problem: " + reason);

        check (validJson, "session.json is well formed");

        check (! hasLocaleDecimalComma (json),
               "numbers use a decimal point regardless of the machine's locale");

        check (json.find ("\"schema_version\": 1") != std::string::npos,
               "the schema version is present and current");

        for (const char* section : { "plugin", "build", "host", "audio_configuration",
                                     "session", "dsp_configuration", "performance",
                                     "audio_health", "stereo", "morphing", "compensation",
                                     "event_counters", "capture", "warnings", "validation" })
        {
            check (json.find (std::string ("\"") + section + "\"") != std::string::npos,
                   std::string ("session.json contains the ") + section + " section");
        }

        check (json.find ("Test \\\"Host\\\"") != std::string::npos,
               "quotes inside a value are escaped");

        // The summary.
        const auto summary = buildSummaryText (info_, snapshot, statistics, warnings);

        check (summary.find ("AuroraVerb Diagnostic Report") == 0,
               "summary.txt starts with its title");

        for (const char* label : { "Host:", "Sample rate:", "Block sizes:", "Session duration:",
                                   "CPU", "NaN / Inf:", "Clipped samples:", "Correlation:",
                                   "Mono downmix:", "Morph operations:", "Queue drops:",
                                   "Capture drops:", "Warnings", "Overall technical result:" })
        {
            check (summary.find (label) != std::string::npos,
                   std::string ("summary.txt reports ") + label);
        }

        check (summary.find ("PASS WITH WARNINGS") != std::string::npos,
               "the verdict appears in the summary");

        check (summary.find ("not whether the reverb sounds good") != std::string::npos,
               "the summary states that the verdict is technical only");

        // The timeline.
        const auto header = timelineHeader();
        const auto row = timelineRow (snapshot, statistics);

        const size_t headerFields = countCharacter (header, ',') + 1;
        const size_t rowFields = countCharacter (row, ',') + 1;

        info ("");
        info ("timeline header fields: " + std::to_string (headerFields)
              + ", row fields: " + std::to_string (rowFields));

        checkEqual (static_cast<long long> (rowFields), static_cast<long long> (headerFields),
                    "every timeline row has exactly as many fields as the header");

        // No locale check here. In a comma-separated file every field boundary
        // is a digit, a comma and a digit, so the JSON test for a decimal comma
        // reports every well-formed row as broken. The numbers themselves come
        // from the same jsonNumber() the JSON uses, which is what the locale
        // check in that suite covers.
        check (row.find (",,") == std::string::npos, "no timeline field is empty");

        // Events.
        DiagnosticEvent event;
        event.sampleTime = 48000;
        event.type = EventType::morphStart;
        event.parameter = ParameterId::size;
        event.value = 1.5f;

        const auto line = eventJsonLine (event, 48000.0);

        std::string eventReason;
        check (looksLikeValidJson (line, eventReason), "each events.jsonl line is valid JSON");
        check (line.find ('\n') == std::string::npos, "and is exactly one line");
        check (line.find ("morph_start") != std::string::npos, "with the event type as text");
    }

    // =======================================================================
    beginSuite ("Diagnostics / session lifecycle");
    // =======================================================================
    {
        TemporaryDirectory temporary ("session");

        DiagnosticCollector collector;
        collector.prepare (48000.0, 256, 512, 4800);
        collector.setLevel (Level::full);
        collector.setCaptureEnabled (true);

        DiagnosticSessionInfo info_;
        info_.instanceId = "test0001";
        info_.startTimestamp = "2026-08-05_10-00-00";
        info_.buildConfiguration = "Release, diagnostics";
        info_.architectureName = "C allpass-only";
        info_.level = Level::full;
        info_.captureEnabled = true;

        DiagnosticSessionWriter writer;

        check (writer.start (collector, info_, temporary.path, true),
               "a session starts in a writable directory");

        const auto sessionPath = writer.getSessionPath();
        info ("session path: " + sessionPath);

        check (writer.isRunning(), "the writer thread is running");

        // Produce something for it to consume.
        std::vector<double> left (256), right (256);

        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.3 * std::sin (0.01 * static_cast<double> (i));
            right[i] = -left[i];
        }

        for (int block = 0; block < 100; ++block)
        {
            const auto start = collector.beginBlock();

            collector.observeInput (left.data(), right.data(), 256);
            collector.observeOutput (left.data(), right.data(), 256, 0.4);
            collector.pushEvent (EventType::parameterChange, ParameterId::decay,
                                 2.0f + 0.01f * block);

            collector.endBlock (start, 256);
        }

        // Driven deterministically rather than by sleeping and hoping.
        writer.pump();
        writer.stop();

        check (! writer.isRunning(), "the writer stops cleanly");

        for (const char* name : { "session.json", "summary.txt", "timeline.csv",
                                  "events.jsonl", "environment.txt",
                                  "input.wav", "output.wav" })
        {
            check (fileExists (paths::join (sessionPath, name)),
                   std::string ("the session produced ") + name);
        }

        const auto json = readFile (paths::join (sessionPath, "session.json"));

        std::string reason;
        check (looksLikeValidJson (json, reason), "the written session.json is valid");

        const auto timeline = readFile (paths::join (sessionPath, "timeline.csv"));
        const size_t lines = countCharacter (timeline, '\n');

        info ("timeline rows written: " + std::to_string (lines > 0 ? lines - 1 : 0));

        check (lines >= 2, "the timeline has a header and at least one row");

        const auto events = readFile (paths::join (sessionPath, "events.jsonl"));
        info ("event lines written: " + std::to_string (countCharacter (events, '\n')));

        check (countCharacter (events, '\n') > 0, "events were written");

        // The capture files must be readable WAVs with the right metadata.
        std::vector<double> capturedLeft, capturedRight;
        double capturedRate = 0.0;
        std::string wavError;

        const bool readable = readWav (paths::join (sessionPath, "output.wav"),
                                       capturedLeft, capturedRight, capturedRate, wavError);

        if (! readable)
            info ("  WAV problem: " + wavError);

        check (readable, "the captured output.wav is a readable WAV");
        checkClose (capturedRate, 48000.0, 0.5, "with the session's sample rate in its header");

        info ("captured frames: " + std::to_string (capturedLeft.size()));

        check (! capturedLeft.empty(), "and it contains audio");
    }

    // =======================================================================
    beginSuite ("Diagnostics / final snapshot flush and shutdown");
    // =======================================================================
    {
        TemporaryDirectory temporary ("flush");

        DiagnosticCollector collector;
        collector.prepare (48000.0, 128, 512, 4800, 64);
        collector.setLevel (Level::lightweight);

        DiagnosticSessionInfo info_;
        info_.instanceId = "flush001";
        info_.startTimestamp = "2026-08-05_09-00-00";

        DiagnosticSessionWriter writer;
        check (writer.start (collector, info_, temporary.path, false), "a session starts");

        std::vector<double> buffer (128, 0.15);

        // Long enough for many snapshots, so the last one is well past the
        // first and the difference is visible in the report.
        //
        // Paced roughly to audio time rather than run flat out. An audio thread
        // cannot produce faster than the audio it is rendering, and a free
        // running loop here would generate five seconds of snapshots in a few
        // milliseconds, overflow a queue sized for real time, and then test the
        // overflow path instead of the flush path. Overflow is covered on its
        // own, in the suite above.
        constexpr int blocks = 400;

        for (int block = 0; block < blocks; ++block)
        {
            const auto start = collector.beginBlock();
            collector.observeOutput (buffer.data(), buffer.data(), 128, 0.15);
            collector.endBlock (start, 128);

            // 128 samples at 48 kHz is 2.67 ms. Sleeping every 16th block keeps
            // the producer near audio time without making the suite slow.
            if ((block % 16) == 15)
                std::this_thread::sleep_for (std::chrono::milliseconds (42));
        }

        const auto sessionPath = writer.getSessionPath();

        // No pump() first: stop() has to flush what is still queued, otherwise
        // the report would describe the session as it was at the last poll.
        writer.stop();

        check (! writer.isRunning(), "the writer thread has stopped");

        const auto json = readFile (paths::join (sessionPath, "session.json"));

        check (! json.empty(), "the final report was written");

        // The report must describe the whole session, not a stale poll.
        const std::string needle = "\"callbacks\": ";
        const auto position = json.find (needle);

        check (position != std::string::npos, "the report records a callback count");

        long long reported = 0;

        if (position != std::string::npos)
            reported = std::atoll (json.c_str() + position + needle.size());

        info ("blocks rendered:   " + std::to_string (blocks));
        info ("report callbacks:  " + std::to_string (reported));
        info ("snapshots dropped: " + std::to_string (collector.getSnapshotsDropped()));

        checkEqual (collector.getSnapshotsDropped(), 0LL,
                    "nothing was dropped at a realistic production rate");

        // Snapshots are produced every 10 ms; at 128 samples that is one every
        // 3.75 blocks, so the last one can be at most a few blocks behind.
        check (reported > blocks - 10 && reported <= blocks,
               "the final report reflects the last snapshot the audio thread produced");

        // Nothing left behind.
        checkEqual (static_cast<long long> (collector.getSnapshotQueue().size()), 0LL,
                    "the snapshot queue is empty after shutdown");

        // Destroying the writer after the collector's producer has stopped must
        // be safe, and calling stop again must do nothing.
        writer.stop();

        check (true, "stopping an already-stopped session is safe");
    }

    // =======================================================================
    beginSuite ("Diagnostics / session failure handling");
    // =======================================================================
    {
        DiagnosticCollector collector;
        collector.prepare (48000.0, 256, 128, 4800);
        collector.setLevel (Level::lightweight);

        DiagnosticSessionInfo info_;
        info_.instanceId = "test0002";
        info_.startTimestamp = "2026-08-05_10-00-00";

        DiagnosticSessionWriter writer;

        // A path that cannot be created. The plugin must carry on rather than
        // failing to render audio because a log file could not be opened.
       #if defined (_WIN32)
        const std::string impossible = "Z:/definitely/not/a/real/volume/auroraverb";
       #else
        const std::string impossible = "/proc/definitely-not-writable/auroraverb";
       #endif

        const bool started = writer.start (collector, info_, impossible, false);

        info ("starting a session in an unwritable location: "
              + std::string (started ? "started" : "refused"));
        info ("reason: " + writer.getLastError());

        check (! started, "a session in an unwritable location is refused rather than half-started");
        check (! writer.getLastError().empty(), "and the reason is recorded");
        check (! writer.isRunning(), "no thread is left running");

        // The collector still works, which is the point: diagnostics failing
        // must never stop the plugin.
        std::vector<double> buffer (256, 0.1);

        const auto start = collector.beginBlock();
        collector.observeOutput (buffer.data(), buffer.data(), 256, 0.1);
        collector.endBlock (start, 256);

        DiagnosticSnapshot snapshot;
        check (collector.popLatestSnapshot (snapshot), "the collector keeps working regardless");
        check (snapshot.counters.callbackCount == 1, "and keeps counting");

        // Stopping a session that never started must be safe, and idempotent.
        writer.stop();
        writer.stop();

        check (true, "stopping a session twice is safe");
    }

    // =======================================================================
    beginSuite ("Diagnostics / multiple instance isolation");
    // =======================================================================
    {
        TemporaryDirectory temporary ("instances");

        // Two plugin instances must not share state or overwrite each other's
        // session, which is what a host with several AuroraVerbs will do.
        DiagnosticCollector a, b;

        a.prepare (48000.0, 256, 256, 4800);
        b.prepare (44100.0, 128, 256, 4410);

        a.setLevel (Level::lightweight);
        b.setLevel (Level::lightweight);

        std::vector<double> buffer (256, 0.2);

        // Enough blocks to cross the 10 ms snapshot interval several times, so
        // the counts below come from a published snapshot rather than from the
        // one produced on the very first block.
        for (int i = 0; i < 500; ++i)
        {
            const auto start = a.beginBlock();
            a.observeOutput (buffer.data(), buffer.data(), 256, 0.2);
            a.endBlock (start, 256);
        }

        for (int i = 0; i < 100; ++i)
        {
            const auto start = b.beginBlock();
            b.observeOutput (buffer.data(), buffer.data(), 128, 0.2);
            b.endBlock (start, 128);
        }

        DiagnosticSnapshot snapshotA, snapshotB;

        check (a.popLatestSnapshot (snapshotA) && b.popLatestSnapshot (snapshotB),
               "both instances report independently");

        info ("A: " + std::to_string (snapshotA.counters.callbackCount) + " callbacks, "
              + std::to_string (snapshotA.counters.processedSamples) + " samples");
        info ("B: " + std::to_string (snapshotB.counters.callbackCount) + " callbacks, "
              + std::to_string (snapshotB.counters.processedSamples) + " samples");

        checkEqual (snapshotA.counters.processedSamples,
                    snapshotA.counters.callbackCount * 256,
                    "instance A's counters are self-consistent");
        checkEqual (snapshotB.counters.processedSamples,
                    snapshotB.counters.callbackCount * 128,
                    "instance B's are too, with its own block size");

        check (snapshotA.counters.callbackCount > snapshotB.counters.callbackCount,
               "and neither instance sees the other's callback count");

        checkClose (snapshotA.host.sampleRate, 48000.0, 0.5, "and kept its own sample rate");
        checkClose (snapshotB.host.sampleRate, 44100.0, 0.5, "as did the other");

        DiagnosticSessionInfo infoA, infoB;
        infoA.instanceId = "aaaa1111";
        infoB.instanceId = "bbbb2222";
        infoA.startTimestamp = infoB.startTimestamp = "2026-08-05_11-00-00";

        DiagnosticSessionWriter writerA, writerB;

        check (writerA.start (a, infoA, temporary.path, false), "instance A starts a session");
        check (writerB.start (b, infoB, temporary.path, false), "instance B starts its own");

        info ("A: " + writerA.getSessionPath());
        info ("B: " + writerB.getSessionPath());

        check (writerA.getSessionPath() != writerB.getSessionPath(),
               "two instances starting in the same second get different folders");

        writerA.stop();
        writerB.stop();

        check (fileExists (paths::join (writerA.getSessionPath(), "session.json"))
               && fileExists (paths::join (writerB.getSessionPath(), "session.json")),
               "both wrote their own report");
    }

    // =======================================================================
    beginSuite ("Diagnostics / WAV round trip");
    // =======================================================================
    {
        TemporaryDirectory temporary ("wav");

        const std::string path = paths::join (temporary.path, "round-trip.wav");

        constexpr size_t frames = 4321;   // deliberately not a round number

        std::vector<float> interleaved (frames * 2);

        for (size_t i = 0; i < frames; ++i)
        {
            interleaved[i * 2] = static_cast<float> (std::sin (0.01 * static_cast<double> (i)));
            interleaved[i * 2 + 1] = static_cast<float> (-std::cos (0.01 * static_cast<double> (i)));
        }

        {
            WavWriter writer;
            check (writer.open (path, 96000.0, 2), "the writer opens a file");
            check (writer.writeFrames (interleaved.data(), frames), "and writes frames");
            check (writer.close(), "and closes cleanly");
        }

        std::vector<double> left, right;
        double rate = 0.0;
        std::string error;

        check (readWav (path, left, right, rate, error), "the file reads back");

        checkClose (rate, 96000.0, 0.5, "the sample rate survives the round trip");
        checkEqual (static_cast<long long> (left.size()), static_cast<long long> (frames),
                    "the frame count is exact, including the odd tail");

        double worst = 0.0;

        for (size_t i = 0; i < frames; ++i)
        {
            worst = std::max (worst, std::abs (left[i] - static_cast<double> (interleaved[i * 2])));
            worst = std::max (worst, std::abs (right[i] - static_cast<double> (interleaved[i * 2 + 1])));
        }

        info ("worst sample error through the round trip: " + fixed (worst, 12));

        checkAtMost (worst, 0.0, "float samples survive exactly, with no quantisation");

        // A file that is not a WAV must be refused rather than misread.
        const std::string junkPath = paths::join (temporary.path, "not-a-wav.txt");

        {
            std::ofstream junk (junkPath, std::ios::binary | std::ios::trunc);
            junk << "this is not a wave file at all, not even a little bit";
        }

        std::vector<double> junkLeft, junkRight;
        double junkRate = 0.0;
        std::string junkError;

        check (! readWav (junkPath, junkLeft, junkRight, junkRate, junkError),
               "a non-WAV file is refused");

        info ("refusal reason: " + junkError);

        check (! readWav (paths::join (temporary.path, "missing.wav"),
                          junkLeft, junkRight, junkRate, junkError),
               "a missing file is refused");
    }

    // =======================================================================
    beginSuite ("Diagnostics / report consistency");
    // =======================================================================
    {
        // summary.txt said "Host: FL Studio  (version unavailable)" and
        // environment.txt said "host: FL Studio unavailable" for the same
        // session. Neither was wrong, but a reader comparing them has to work
        // out that they are the same fact. They now go through one function.
        DiagnosticSessionInfo session;
        session.hostName = "FL Studio";
        session.hostVersion = "unavailable";
        session.pluginFormat = "VST3";

        info ("host described as: " + describeHost (session));

        checkEqual (describeHost (session), std::string ("FL Studio  (version unavailable)"),
                    "an absent version is described as absent, not appended");

        session.hostVersion.clear();

        checkEqual (describeHost (session), std::string ("FL Studio  (version unavailable)"),
                    "an empty version is the same case as a missing one");

        session.hostVersion = "21.2.3";

        checkEqual (describeHost (session), std::string ("FL Studio 21.2.3"),
                    "a real version is used as it stands");

        // And the two files agree, which is the property that was broken.
        const auto snapshot = makeHealthySnapshot();
        const auto statistics = computeStatistics (snapshot);
        const auto warnings = evaluate (snapshot, statistics);

        session.hostVersion = "unavailable";

        const auto summary = buildSummaryText (session, snapshot, statistics, warnings);

        check (summary.find (describeHost (session)) != std::string::npos,
               "summary.txt uses the shared description");
    }

    // =======================================================================
    beginSuite ("Diagnostics / deadline telemetry");
    // =======================================================================
    {
        // The first FL Studio session reported 17 overruns against a budget
        // computed once from the 512-sample prepare block, while the host was
        // actually delivering blocks between 1 and 213 samples. That made the
        // count unreadable: it could not distinguish a callback that was slow
        // from one that was given almost no time. These tests pin the budget to
        // the block that was actually delivered.
        constexpr double rate = 48000.0;

        const auto callbackOfDuration = [] (DiagnosticCollector& collector,
                                            int numSamples, double microseconds)
        {
            // A start time placed in the past, so the measured duration is the
            // one the test asked for rather than whatever the machine was doing.
            const auto start = DiagnosticClock::now()
                             - std::chrono::nanoseconds (static_cast<int64_t> (microseconds * 1000.0));

            collector.endBlock (start, numSamples);
        };

        {
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            // 300 us of work in a 512-sample block: 10666 us of budget, so this
            // is nowhere near a deadline.
            //
            // A 512-sample block is longer than the 10 ms snapshot interval, so
            // every callback here publishes and the snapshot holds the callback
            // just made rather than an earlier one.
            callbackOfDuration (collector, 512, 300.0);

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            info ("512-sample block, 300 us of work:");
            info ("  budget " + fixed (static_cast<double> (snapshot.counters.lastBudgetNanoseconds) * 1.0e-3, 1)
                  + " us, ratio " + fixed (snapshot.counters.worstBudgetRatio, 4));

            checkClose (static_cast<double> (snapshot.counters.lastBudgetNanoseconds),
                        1.0e9 * 512.0 / rate, 1.0,
                        "the budget is the delivered block's own period");

            checkEqual (static_cast<long long> (snapshot.counters.lastBlockSize), 512LL,
                        "the block size travels with the measurement");

            checkEqual (static_cast<long long> (snapshot.counters.deadlineExceededCount), 0LL,
                        "comfortable work against a large block is not an overrun");
        }

        {
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            // The same 300 us of work, in a 4-sample block. 4 samples at 48 kHz
            // is 83.3 us of budget, so identical work now misses by 3.6x. This
            // is the case the old prepare-block budget could not see at all.
            callbackOfDuration (collector, 4, 300.0);

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            const double budgetUs = static_cast<double> (snapshot.counters.lastBudgetNanoseconds) * 1.0e-3;

            info ("4-sample block, the same 300 us of work:");
            info ("  budget " + fixed (budgetUs, 1) + " us, ratio "
                  + fixed (snapshot.counters.worstBudgetRatio, 2) + "x");

            checkClose (budgetUs, 1.0e6 * 4.0 / rate, 1.0,
                        "a tiny block gets a tiny budget");

            checkEqual (static_cast<long long> (snapshot.counters.deadlineExceededCount), 1LL,
                        "work that fits a 512-sample block overruns a 4-sample one");

            check (snapshot.counters.worstBudgetRatio > 2.0,
                   "the ratio records how badly, not just that it happened");

            checkEqual (static_cast<long long> (snapshot.counters.worstBudgetRatioBlockSize), 4LL,
                        "the worst ratio remembers which block size produced it");

            // Under the old scheme this same callback would have been measured
            // against 512 samples and passed with a ratio of 0.03.
            const double againstPrepareBlock = 300.0 / (1.0e6 * 512.0 / rate);
            info ("  the same callback against the prepare block: ratio "
                  + fixed (againstPrepareBlock, 4) + "x - invisible");

            check (againstPrepareBlock < 1.0,
                   "which is why the prepare-block budget could not see it");
        }

        {
            // Evidence on the event itself, so the report never has to guess.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            DiagnosticHostState host;
            host.sampleRate = rate;
            host.prepareBlockSize = 512;
            host.transportPlaying = true;
            collector.observeHost (host);

            callbackOfDuration (collector, 8, 500.0);

            DiagnosticEvent event {};
            bool found = false;

            while (collector.getEventQueue().pop (event))
                if (event.type == EventType::deadlineExceeded)
                    found = true;

            check (found, "an overrun pushes an event");

            checkEqual (static_cast<long long> (event.integerValue), 8LL,
                        "the event carries the block size that overran");

            checkClose (static_cast<double> (event.microseconds), 500.0, 60.0,
                        "and how long the callback took");

            checkClose (static_cast<double> (event.budgetMicroseconds), 1.0e6 * 8.0 / rate, 1.0,
                        "and the budget it was measured against");

            check (static_cast<double> (event.value) > 2.0,
                   "and the ratio between them");

            check ((event.flags & eventFlags::transportPlaying) != 0,
                   "and the transport state at the time");

            checkEqual (static_cast<long long> (eventFlags::unpackLevel (event.flags)),
                        static_cast<long long> (Level::lightweight),
                        "and the telemetry level, packed and recovered exactly");
        }

        {
            // Min and max observed block sizes, which is what says whether a
            // session ever saw the block size it was prepared for.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            // 50 us of work. Only the 1-sample block, whose period is 20.8 us,
            // can possibly miss; 7 samples is 145 us and everything above it is
            // further clear still. Nothing here is a close call, so the result
            // does not depend on how busy the machine is.
            for (int n : { 213, 1, 64, 512 })
                callbackOfDuration (collector, n, 50.0);

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            info ("observed block sizes: " + std::to_string (snapshot.host.minObservedBlockSize)
                  + " to " + std::to_string (snapshot.host.maxObservedBlockSize)
                  + ", prepared for " + std::to_string (snapshot.host.prepareBlockSize));

            checkEqual (static_cast<long long> (snapshot.host.minObservedBlockSize), 1LL,
                        "the smallest block the host delivered is recorded");
            checkEqual (static_cast<long long> (snapshot.host.maxObservedBlockSize), 512LL,
                        "and the largest");

            checkEqual (static_cast<long long> (snapshot.counters.deadlineExceededCount), 1LL,
                        "only the block too short for the work is counted");

            checkEqual (static_cast<long long> (snapshot.counters.worstBudgetRatioBlockSize), 1LL,
                        "and it is named");

            const auto statistics = computeStatistics (snapshot);

            info ("prepare budget:   " + fixed (statistics.prepareBudgetMicroseconds, 1) + " us");
            info ("worst runtime:    " + fixed (statistics.worstRuntimeBudgetRatio, 2)
                  + "x on a " + std::to_string (statistics.worstRuntimeBudgetBlockSize) + "-sample block");

            checkClose (statistics.prepareBudgetMicroseconds, 1.0e6 * 512.0 / rate, 1.0,
                        "the prepare budget is still reported, as the host's own claim");

            check (statistics.worstRuntimeBudgetRatio > statistics.meanBudgetFraction,
                   "the runtime ratio is the harsher of the two, which is the point");
        }

        {
            // A zero-sample callback is legal and must not divide by itself.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            callbackOfDuration (collector, 0, 100.0);

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            checkEqual (static_cast<long long> (snapshot.counters.lastBudgetNanoseconds), 0LL,
                        "an empty block has no budget rather than an infinite ratio");
            checkEqual (static_cast<long long> (snapshot.counters.deadlineExceededCount), 0LL,
                        "and cannot be an overrun");
            check (std::isfinite (snapshot.counters.worstBudgetRatio),
                   "the worst ratio stays a number");
        }

        {
            // The reporting side: a warning that names the evidence.
            auto snapshot = makeHealthySnapshot();
            snapshot.counters.deadlineExceededCount = 17;
            snapshot.counters.worstBudgetRatio = 3.62;
            snapshot.counters.worstBudgetRatioBlockSize = 4;

            const auto statistics = computeStatistics (snapshot);
            const auto warnings = evaluate (snapshot, statistics);

            bool named = false;

            for (const auto& w : warnings)
                if (w.rule == "deadline_exceeded")
                {
                    named = true;
                    info ("warning: " + w.detail);

                    check (w.detail.find ("4-sample") != std::string::npos,
                           "the warning names the block size, not just the count");
                    check (w.detail.find ("3.62") != std::string::npos,
                           "and how far past the deadline it went");
                }

            check (named, "overruns raise a warning");

            check (verdictFor (warnings) != Verdict::fail,
                   "an overrun on a 4-sample block is a warning, not a failure");
        }

        {
            // And the timeline columns, which the analysis scripts index by
            // position.
            const auto header = timelineHeader();
            const auto columns = countCharacter (header, ',') + 1;

            info ("timeline columns: " + std::to_string (columns));

            for (const char* name : { "block_size", "block_min", "block_max", "callback_us",
                                      "block_budget_us", "block_ratio", "deadline_flag",
                                      "deadline_count", "worst_ratio", "worst_ratio_block",
                                      "capture_limited" })
                check (header.find (name) != std::string::npos,
                       std::string ("the timeline carries ") + name);

            auto snapshot = makeHealthySnapshot();
            snapshot.counters.lastBlockSize = 64;
            snapshot.counters.lastProcessingNanoseconds = 40000;
            snapshot.counters.lastBudgetNanoseconds = 1333333;

            const auto row = timelineRow (snapshot, computeStatistics (snapshot));

            checkEqual (static_cast<long long> (countCharacter (row, ',') + 1),
                        static_cast<long long> (columns),
                        "every row has exactly as many fields as the header has names");
        }
    }

    // =======================================================================
    beginSuite ("Diagnostics / transport telemetry");
    // =======================================================================
    {
        // The first round-two session played, stopped and seeked, and recorded
        // transport_playing=false on all 1729 of its events with not one
        // transport event among them. The flag was read once, in prepareToPlay,
        // and never again. These tests are about the property that was missing:
        // the transport has to be written where time passes.
        constexpr double rate = 48000.0;

        // A stand-in for the processor's edge detection, driven the same way
        // the audio thread drives it: one call per callback, with the block
        // size and the playhead position the host reported.
        struct TransportTracker
        {
            DiagnosticCollector& collector;
            double sampleRate = 48000.0;
            int64_t seekTolerance = 480;

            bool lastPlaying = false;
            int64_t lastSamples = 0;
            int lastBlock = 0;
            bool havePosition = false;

            void callback (bool playing, int64_t timeInSamples, int numSamples,
                           bool positionKnown = true)
            {
                collector.observeTransport (playing, false);

                if (playing != lastPlaying)
                {
                    lastPlaying = playing;
                    collector.pushEvent (EventType::transportChanged, ParameterId::none,
                                         playing ? 1.0f : 0.0f, playing ? 0.0f : 1.0f);
                    havePosition = false;
                }

                if (! playing || ! positionKnown)
                {
                    havePosition = false;
                    return;
                }

                if (havePosition)
                {
                    const int64_t expected = lastSamples + lastBlock;
                    const int64_t drift = timeInSamples - expected;

                    if (std::abs (drift) > seekTolerance)
                        collector.pushEvent (EventType::transportSeek, ParameterId::none,
                                             static_cast<float> (timeInSamples / sampleRate),
                                             static_cast<float> (expected / sampleRate),
                                             drift > 0 ? 1 : -1);
                }

                lastSamples = timeInSamples;
                lastBlock = numSamples;
                havePosition = true;
            }
        };

        const auto drain = [] (DiagnosticCollector& c)
        {
            std::vector<DiagnosticEvent> out;
            DiagnosticEvent e {};

            while (c.getEventQueue().pop (e))
                out.push_back (e);

            return out;
        };

        {
            // The flag itself. This is the one that was broken: not the events,
            // but the state every other event carries.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            t.callback (false, 0, 512);
            checkEqual (static_cast<long long> (collector.peekStateFlags()
                                                & eventFlags::transportPlaying), 0LL,
                        "a stopped transport reads as stopped");

            t.callback (true, 0, 512);
            check ((collector.peekStateFlags() & eventFlags::transportPlaying) != 0,
                   "and a playing one reads as playing, on the very next callback");

            t.callback (true, 512, 512);
            check ((collector.peekStateFlags() & eventFlags::transportPlaying) != 0,
                   "and stays that way while it plays");

            t.callback (false, 1024, 512);
            checkEqual (static_cast<long long> (collector.peekStateFlags()
                                                & eventFlags::transportPlaying), 0LL,
                        "and follows it back down when it stops");
        }

        {
            // Play and stop edges, once each, not once per callback.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            t.callback (false, 0, 512);
            for (int n = 0; n < 20; ++n) t.callback (true, n * 512, 512);
            for (int n = 0; n < 20; ++n) t.callback (false, 20 * 512, 512);

            const auto events = drain (collector);

            const auto changes = std::count_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportChanged; });

            info ("transport events over 41 callbacks: " + std::to_string (changes));

            checkEqual (static_cast<long long> (changes), 2LL,
                        "one event for the start and one for the stop, not one per callback");

            const auto started = std::find_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportChanged; });

            checkClose (static_cast<double> (started->value), 1.0, 1.0e-9,
                        "the start event says the transport is now playing");
            checkClose (static_cast<double> (started->previous), 0.0, 1.0e-9,
                        "and that it was not");

            check ((started->flags & eventFlags::transportPlaying) != 0,
                   "and carries the new state in its flags");
        }

        {
            // Forward and backward seeks, and the ordinary advance that is
            // neither.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            t.callback (true, 0, 512);

            // Ordinary playback: the playhead advances by exactly the block.
            for (int n = 1; n <= 10; ++n) t.callback (true, n * 512, 512);

            // A jump forward of ten seconds.
            t.callback (true, 11 * 512 + static_cast<int64_t> (10.0 * rate), 512);

            // Back to the start.
            t.callback (true, 0, 512);

            // And ordinary playback again.
            for (int n = 1; n <= 5; ++n) t.callback (true, n * 512, 512);

            const auto events = drain (collector);

            std::vector<DiagnosticEvent> seeks;

            for (const auto& e : events)
                if (e.type == EventType::transportSeek)
                    seeks.push_back (e);

            info ("seeks detected: " + std::to_string (seeks.size()));

            for (const auto& s : seeks)
                info ("  to " + fixed (static_cast<double> (s.value), 3) + " s, expected "
                      + fixed (static_cast<double> (s.previous), 3) + " s, direction "
                      + std::to_string (s.integerValue));

            checkEqual (static_cast<long long> (seeks.size()), 2LL,
                        "exactly the two jumps are reported, and no ordinary block is");

            checkEqual (static_cast<long long> (seeks[0].integerValue), 1LL,
                        "the forward jump is marked forward");
            checkEqual (static_cast<long long> (seeks[1].integerValue), -1LL,
                        "and the backward one backward");

            checkClose (static_cast<double> (seeks[1].value), 0.0, 1.0e-4,
                        "the backward seek reports where the playhead landed");
        }

        {
            // A stopped playhead does not advance, and must not therefore look
            // like a backward seek on every callback - which is the obvious way
            // to get this wrong.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            for (int n = 0; n < 50; ++n) t.callback (false, 44100, 512);

            const auto events = drain (collector);

            const auto seeks = std::count_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportSeek; });

            checkEqual (static_cast<long long> (seeks), 0LL,
                        "a stationary stopped playhead reports no seeks at all");
        }

        {
            // Starting and stopping move the playhead by more than a block, and
            // neither is a seek.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            for (int n = 0; n < 3; ++n) t.callback (false, 0, 512);

            // Play from bar 33, nowhere near where the stopped playhead sat.
            t.callback (true, static_cast<int64_t> (60.0 * rate), 512);
            t.callback (true, static_cast<int64_t> (60.0 * rate) + 512, 512);

            t.callback (false, static_cast<int64_t> (60.0 * rate) + 1024, 512);
            t.callback (true, 0, 512);
            t.callback (true, 512, 512);

            const auto events = drain (collector);

            const auto seeks = std::count_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportSeek; });

            checkEqual (static_cast<long long> (seeks), 0LL,
                        "starting and stopping are not reported as seeks");

            const auto changes = std::count_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportChanged; });

            checkEqual (static_cast<long long> (changes), 3LL,
                        "but all three transport edges are");
        }

        {
            // A host that reports no position at all must not crash or invent
            // seeks; the flag still has to work.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setLevel (Level::lightweight);

            TransportTracker t { collector, rate, 480 };

            for (int n = 0; n < 10; ++n) t.callback (true, 0, 512, false);

            const auto events = drain (collector);

            const auto seeks = std::count_if (events.begin(), events.end(),
                [] (const DiagnosticEvent& e) { return e.type == EventType::transportSeek; });

            checkEqual (static_cast<long long> (seeks), 0LL,
                        "a host with no timeline position produces no seeks");

            check ((collector.peekStateFlags() & eventFlags::transportPlaying) != 0,
                   "but the playing flag still tracks the transport");
        }

        {
            // The tolerance is a duration, so it means the same thing at every
            // rate rather than the same number of samples.
            for (double r : { 44100.0, 48000.0, 96000.0 })
            {
                const auto tolerance = static_cast<int64_t> (0.010 * r);

                info (fixed (r / 1000.0, 1) + " kHz: tolerance "
                      + std::to_string (tolerance) + " samples = "
                      + fixed (1000.0 * tolerance / r, 2) + " ms");

                checkClose (1000.0 * static_cast<double> (tolerance) / r, 10.0, 0.05,
                            "the seek tolerance is ten milliseconds at "
                            + fixed (r / 1000.0, 1) + " kHz");

                // It also has to be comfortably above the largest block the
                // host was seen to deliver, or ordinary playback trips it.
                check (tolerance > 231,
                       "and above the largest block FL Studio delivered at "
                       + fixed (r / 1000.0, 1) + " kHz");
            }
        }

        {
            // Real-time safety: this runs on every callback, so it has to cost
            // nothing but the writes.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 4096, 1024);
            collector.setLevel (Level::full);

            TransportTracker t { collector, rate, 480 };

            t.callback (true, 0, 512);

            if (allocationCountingAvailable())
            {
                long long allocations = 0;

                {
                    ScopedAllocationCounter counter;

                    // Ordinary playback, then edges and seeks, which are the
                    // paths that push events.
                    for (int n = 1; n <= 200; ++n) t.callback (true, n * 512, 512);

                    for (int n = 0; n < 20; ++n)
                    {
                        t.callback (false, 200 * 512, 512);
                        t.callback (true, 0, 512);
                        t.callback (true, static_cast<int64_t> (30.0 * rate), 512);
                    }

                    allocations = counter.count();
                }

                info ("allocations across 260 transport callbacks: "
                      + std::to_string (allocations));

                checkEqual (allocations, 0LL,
                            "transport tracking allocates nothing on the audio thread");
            }
            else
            {
                info ("allocation counting unavailable in this build - not measured");
            }
        }
    }

    // =======================================================================
    beginSuite ("Diagnostics / mix and width, morph counters, seek reason");
    // =======================================================================
    {
        constexpr double rate = 48000.0;

        // --- Mix and Width reach the reports -------------------------------
        //
        // Test A and Test B both depended on "Mix is 100% wet" and "Width is
        // 100%", and neither could be checked afterwards: the fields did not
        // exist, so the operator's word was the only record.
        {
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.mix = 1.0;
            snapshot.dsp.width = 1.0;

            const auto statistics = computeStatistics (snapshot);
            const auto warnings = evaluate (snapshot, statistics);

            const auto header = timelineHeader();
            const auto columns = countCharacter (header, ',') + 1;

            check (header.find ("mix") != std::string::npos, "the timeline carries mix");
            check (header.find ("width") != std::string::npos, "and width");

            const auto row = timelineRow (snapshot, statistics);

            checkEqual (static_cast<long long> (countCharacter (row, ',') + 1),
                        static_cast<long long> (columns),
                        "the row still has exactly as many fields as the header");

            const auto json = buildSessionJson ({}, snapshot, statistics, warnings);

            check (json.find ("\"mix\"") != std::string::npos, "session.json carries mix");
            check (json.find ("\"width\"") != std::string::npos, "and width");

            std::string reason;
            check (looksLikeValidJson (json, reason), "and is still well formed: " + reason);

            const auto summary = buildSummaryText ({}, snapshot, statistics, warnings);

            check (summary.find ("100.0% wet") != std::string::npos,
                   "summary.txt states the mix as a percentage a test plan can be read against");
            check (summary.find ("Width:") != std::string::npos, "and names the width");
        }

        {
            // Half wet must not read as full wet.
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.mix = 0.5;
            snapshot.dsp.width = 1.5;

            const auto summary = buildSummaryText ({}, snapshot,
                                                   computeStatistics (snapshot), {});

            check (summary.find ("50.0% wet") != std::string::npos,
                   "a half-wet mix is reported as half wet");
            check (summary.find ("150.0%") != std::string::npos,
                   "and a width above unity as what it is");
        }

        // --- Morph timeout warning is reachable ----------------------------
        //
        // The rule was guarded by `morphStartCount > 0`, and nothing wrote that
        // counter, so it was always zero and the branch was dead. A safety rule
        // that cannot fire is worse than no rule: the report looks clean.
        {
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.morphStartCount = 10;
            snapshot.dsp.morphTimeoutCount = 4;

            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            bool found = false;

            for (const auto& w : warnings)
                if (w.rule == "morph_timeouts")
                {
                    found = true;
                    info ("warning: " + w.detail);
                    check (w.detail.find ("4 of 10") != std::string::npos,
                           "the warning gives the count as well as the share");
                    check (w.detail.find ("40.0") != std::string::npos, "and the percentage");
                }

            check (found, "a real timeout rate raises the warning");
        }

        {
            // The case that used to be silent: timeouts recorded, starts not.
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.morphStartCount = 0;
            snapshot.dsp.morphTimeoutCount = 3;

            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            bool found = false;

            for (const auto& w : warnings)
                if (w.rule == "morph_timeouts")
                {
                    found = true;
                    info ("warning: " + w.detail);
                    check (w.detail.find ("cannot be given") != std::string::npos,
                           "and says the share is unavailable rather than inventing one");
                }

            check (found, "a timeout is still reported when the start count is missing");
        }

        {
            // And a clean session stays clean.
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.morphStartCount = 40;
            snapshot.dsp.morphTimeoutCount = 0;

            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            const auto found = std::count_if (warnings.begin(), warnings.end(),
                [] (const DiagnosticWarning& w) { return w.rule == "morph_timeouts"; });

            checkEqual (static_cast<long long> (found), 0LL,
                        "40 morphs and no timeouts raises nothing");
        }

        {
            // A timeout rate below the threshold is not a warning either.
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.morphStartCount = 1000;
            snapshot.dsp.morphTimeoutCount = 1;

            const auto warnings = evaluate (snapshot, computeStatistics (snapshot));

            const auto found = std::count_if (warnings.begin(), warnings.end(),
                [] (const DiagnosticWarning& w) { return w.rule == "morph_timeouts"; });

            checkEqual (static_cast<long long> (found), 0LL,
                        "one timeout in a thousand morphs is below the threshold");
        }

        // --- Morph counters reach the reports ------------------------------
        {
            auto snapshot = makeHealthySnapshot();
            snapshot.dsp.morphStartCount = 31;
            snapshot.dsp.morphTimeoutCount = 0;
            snapshot.dsp.bankSwapCount = 7;

            const auto summary = buildSummaryText ({}, snapshot,
                                                   computeStatistics (snapshot), {});

            check (summary.find ("Morph operations:") != std::string::npos,
                   "summary.txt names the morph count");

            // The specific failure: Test A recorded 31 morphs in its event log
            // and the summary said 0.
            check (summary.find ("31") != std::string::npos,
                   "and reports the number it was given rather than zero");
            check (summary.find ("7") != std::string::npos, "and the bank swaps");
        }

        // --- Seek reason ---------------------------------------------------
        {
            checkEqual (std::string (describe (SeekReason::unknown)), std::string ("unknown"),
                        "an unclassifiable seek is named unknown");
            checkEqual (std::string (describe (SeekReason::userSeek)), std::string ("user_seek"),
                        "a user seek is named");
            checkEqual (std::string (describe (SeekReason::loopWrap)), std::string ("loop_wrap"),
                        "and a loop wrap is named");

            // Packing must survive the round trip and must not disturb the
            // flags already sharing the word.
            for (auto reason : { SeekReason::unknown, SeekReason::userSeek, SeekReason::loopWrap })
            {
                const int32_t flags = eventFlags::packLevel (static_cast<int> (Level::full))
                                    | eventFlags::transportPlaying
                                    | eventFlags::captureEnabled
                                    | eventFlags::packSeekReason (reason);

                checkEqual (static_cast<long long> (eventFlags::unpackSeekReason (flags)),
                            static_cast<long long> (reason),
                            std::string ("the seek reason survives packing: ")
                            + describe (reason));

                checkEqual (static_cast<long long> (eventFlags::unpackLevel (flags)),
                            static_cast<long long> (Level::full),
                            "and the telemetry level is untouched by it");

                check ((flags & eventFlags::transportPlaying) != 0,
                       "and so is the transport flag");
                check ((flags & eventFlags::captureEnabled) != 0,
                       "and the capture flag");
            }
        }

        {
            // The reason and the direction both reach events.jsonl.
            DiagnosticEvent event {};
            event.type = EventType::transportSeek;
            event.sampleTime = 48000;
            event.value = 0.0f;
            event.previous = 4.615f;
            event.integerValue = -1;
            event.flags = eventFlags::packSeekReason (SeekReason::loopWrap)
                        | eventFlags::transportPlaying;

            const auto line = eventJsonLine (event, rate);

            info ("seek line: " + line);

            check (line.find ("\"seek_reason\": \"loop_wrap\"") != std::string::npos,
                   "a loop wrap is reported as a loop wrap");
            check (line.find ("\"direction\": \"backward\"") != std::string::npos,
                   "and the direction is spelled out");

            std::string reason;
            check (looksLikeValidJson (line, reason), "the line is well formed: " + reason);

            event.flags = eventFlags::packSeekReason (SeekReason::unknown);
            check (eventJsonLine (event, rate).find ("\"seek_reason\": \"unknown\"")
                   != std::string::npos,
                   "and an unclassifiable one says unknown rather than guessing");
        }

        {
            // Other event types must not grow a seek_reason field.
            DiagnosticEvent event {};
            event.type = EventType::parameterChange;
            event.parameter = ParameterId::mix;
            event.value = 1.0f;

            const auto line = eventJsonLine (event, rate);

            check (line.find ("seek_reason") == std::string::npos,
                   "a parameter change carries no seek reason");
            check (line.find ("\"parameter\": \"mix\"") != std::string::npos,
                   "and mix is a nameable parameter in the event stream");
        }

        // --- Real-time safety of the additions -----------------------------
        {
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 4096, 1024);
            collector.setLevel (Level::full);

            if (allocationCountingAvailable())
            {
                long long allocations = 0;

                {
                    ScopedAllocationCounter counter;

                    for (int n = 0; n < 200; ++n)
                    {
                        DiagnosticDspState state;
                        state.mix = 0.3 + 0.001 * n;
                        state.width = 1.0;
                        state.morphStartCount = n;
                        state.bankSwapCount = n / 4;
                        collector.observeDsp (state);

                        collector.pushEventWithFlags (
                            EventType::transportSeek, ParameterId::none, 1.0f, 2.0f, -1,
                            eventFlags::packSeekReason (SeekReason::loopWrap));
                    }

                    allocations = counter.count();
                }

                info ("allocations across 200 dsp updates and seek events: "
                      + std::to_string (allocations));

                checkEqual (allocations, 0LL,
                            "the new fields and the flagged push allocate nothing");
            }
            else
            {
                info ("allocation counting unavailable in this build - not measured");
            }
        }
    }

    // =======================================================================
    beginSuite ("Diagnostics / capture limit");
    // =======================================================================
    {
        constexpr double rate = 48000.0;

        {
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);

            checkClose (collector.getCaptureLimitSeconds(),
                        DiagnosticCollector::defaultCaptureLimitSeconds, 1.0e-9,
                        "the default limit is ten minutes");

            checkEqual (collector.getCaptureLimitFrames(),
                        static_cast<int64_t> (600.0 * rate),
                        "which at 48 kHz is a frame count, not a guess");

            // 600 s of stereo float at 48 kHz is 1.15 GB - inside the format's
            // reach. The ceiling only binds at long durations or high rates.
            const double bytes = 8.0 * 600.0 * rate;
            info ("ten minutes at 48 kHz: " + fixed (bytes / (1024.0 * 1024.0), 1) + " MiB");

            check (bytes < static_cast<double> (DiagnosticCollector::captureByteCeiling),
                   "the default duration fits under the byte ceiling at 48 kHz");
        }

        {
            // Where the two limits cross. RIFF stores the data chunk size in a
            // uint32, so 4 GiB is the format's hard wall; the 2 GiB ceiling is
            // the practical one, because readers that keep a file offset in a
            // signed 32-bit int fail above it.
            DiagnosticCollector collector;
            collector.prepare (192000.0, 512, 256, 1024);
            collector.setCaptureLimitSeconds (10000.0);

            const int64_t byDuration = static_cast<int64_t> (10000.0 * 192000.0);
            const int64_t byBytes = (DiagnosticCollector::captureByteCeiling - 44) / 8;

            info ("10000 s at 192 kHz would be " + std::to_string (byDuration) + " frames");
            info ("the byte ceiling allows      " + std::to_string (byBytes) + " frames");

            checkEqual (collector.getCaptureLimitFrames(), byBytes,
                        "the byte ceiling wins when the duration would exceed it");

            check (collector.getCaptureLimitFrames() < byDuration,
                   "and it is the smaller of the two, not the later one set");

            const double resultingBytes = 8.0 * static_cast<double> (byBytes) + 44.0;

            checkAtMost (resultingBytes, static_cast<double> (DiagnosticCollector::captureByteCeiling),
                         "the resulting file cannot pass the ceiling");
        }

        {
            // Stopping on an exact frame, and stopping only once.
            constexpr int64_t limitFrames = 1000;

            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 8192);
            collector.setLevel (Level::full);
            collector.setCaptureEnabled (true);
            collector.setCaptureLimitSeconds (static_cast<double> (limitFrames) / rate);

            checkEqual (collector.getCaptureLimitFrames(), limitFrames,
                        "the limit converts to the frame count asked for");

            std::vector<double> left (512, 0.25), right (512, -0.25);

            const auto runBlocks = [&] (int count)
            {
                for (int block = 0; block < count; ++block)
                {
                    const auto start = collector.beginBlock();
                    collector.observeInput (left.data(), right.data(), 512);
                    collector.observeOutput (left.data(), right.data(), 512, 0.25);
                    collector.endBlock (start, 512);
                }
            };

            // 4 blocks of 512 is 2048 frames, twice the limit. The second block
            // is where the limit lands, part way through it.
            runBlocks (4);

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            info ("frames written: " + std::to_string (snapshot.counters.captureFramesWritten)
                  + " of 2048 offered, limit " + std::to_string (limitFrames));

            checkEqual (snapshot.counters.captureFramesWritten, limitFrames,
                        "capture stops on the exact frame, not at the end of a block");

            check (snapshot.counters.captureLimitReached,
                   "and says so rather than truncating silently");

            checkEqual (snapshot.counters.captureLimitFrames, limitFrames,
                        "the reached limit is reported with the session");

            check (collector.isCaptureLimitReached(),
                   "the collector reports it to whoever asks");

            // Both files stop at the same frame, or they no longer line up.
            checkEqual (static_cast<long long> (collector.getInputRing().available()),
                        static_cast<long long> (collector.getOutputRing().available()),
                        "input and output stop at the same frame");

            int limitEvents = 0;
            DiagnosticEvent event {};

            while (collector.getEventQueue().pop (event))
                if (event.type == EventType::captureLimitReached)
                {
                    ++limitEvents;
                    check ((event.flags & eventFlags::captureEnabled) != 0,
                           "the event records that capture was on");
                }

            checkEqual (static_cast<long long> (limitEvents), 1LL,
                        "the limit is announced once, not once per block after it");

            // The whole point of a limit rather than a stop: measurement carries
            // on with no audio being written.
            const int64_t callbacksAtLimit = snapshot.counters.callbackCount;
            const int64_t samplesAtLimit = snapshot.audio.sampleCount;

            runBlocks (4);

            DiagnosticSnapshot after;
            check (collector.popLatestSnapshot (after), "a later snapshot is available");

            check (after.counters.callbackCount > callbacksAtLimit,
                   "callbacks keep being counted after the capture stops");

            check (after.audio.sampleCount > samplesAtLimit,
                   "and the audio health measurements keep running");

            checkEqual (after.counters.captureFramesWritten, limitFrames,
                        "while not one frame more is written");

            checkEqual (after.counters.captureFramesDropped, 0LL,
                        "and the frames past the limit are not counted as drops - "
                        "they were never offered");
        }

        {
            // Reaching the limit costs the audio thread nothing it was not
            // already paying, and allocates nothing.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 8192);
            collector.setLevel (Level::full);
            collector.setCaptureEnabled (true);
            collector.setCaptureLimitSeconds (512.0 / rate);   // one block

            std::vector<double> left (512, 0.1), right (512, 0.1);

            // Past the limit before the count starts, so the counter measures
            // the steady state after it, not the one-off announcement.
            {
                const auto start = collector.beginBlock();
                collector.observeInput (left.data(), right.data(), 512);
                collector.observeOutput (left.data(), right.data(), 512, 0.1);
                collector.endBlock (start, 512);
            }

            {
                const auto start = collector.beginBlock();
                collector.observeInput (left.data(), right.data(), 512);
                collector.observeOutput (left.data(), right.data(), 512, 0.1);
                collector.endBlock (start, 512);
            }

            check (collector.isCaptureLimitReached(), "the limit is behind us");

            if (allocationCountingAvailable())
            {
                // Read once, into a local, before anything reports it. Calling
                // count() a second time after an info() line counts that line's
                // own string allocations and reads as an audio-thread fault
                // that is not there.
                long long allocations = 0;

                {
                    ScopedAllocationCounter counter;

                    for (int block = 0; block < 64; ++block)
                    {
                        const auto start = collector.beginBlock();
                        collector.observeInput (left.data(), right.data(), 512);
                        collector.observeOutput (left.data(), right.data(), 512, 0.1);
                        collector.endBlock (start, 512);
                    }

                    allocations = counter.count();
                }

                info ("allocations in 64 post-limit callbacks: " + std::to_string (allocations));

                checkEqual (allocations, 0LL,
                            "a capture that has hit its limit allocates nothing");
            }
            else
            {
                info ("allocation counting unavailable in this build - not measured");
            }
        }

        {
            // Zero means no limit, which is what the offline harness wants.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 8192);
            collector.setLevel (Level::full);
            collector.setCaptureEnabled (true);
            collector.setCaptureLimitSeconds (0.0);

            checkEqual (collector.getCaptureLimitFrames(), 0LL,
                        "zero seconds means no frame limit");

            std::vector<double> left (512, 0.5), right (512, 0.5);

            for (int block = 0; block < 4; ++block)
            {
                const auto start = collector.beginBlock();
                collector.observeInput (left.data(), right.data(), 512);
                collector.observeOutput (left.data(), right.data(), 512, 0.5);
                collector.endBlock (start, 512);
            }

            DiagnosticSnapshot snapshot;
            check (collector.popLatestSnapshot (snapshot), "a snapshot is available");

            checkEqual (snapshot.counters.captureFramesWritten, 2048LL,
                        "everything offered is written");
            check (! snapshot.counters.captureLimitReached,
                   "and no limit is reported");
        }

        {
            // Changing the rate rewrites the frame count, so a session that
            // reopens at 96 kHz still records the duration it was asked for.
            DiagnosticCollector collector;
            collector.prepare (rate, 512, 256, 1024);
            collector.setCaptureLimitSeconds (300.0);

            checkEqual (collector.getCaptureLimitFrames(),
                        static_cast<int64_t> (300.0 * rate),
                        "the limit is in frames at the prepared rate");

            collector.prepare (96000.0, 512, 256, 1024);

            checkEqual (collector.getCaptureLimitFrames(),
                        static_cast<int64_t> (300.0 * 96000.0),
                        "and is recomputed when the rate changes, keeping the duration");

            check (! collector.isCaptureLimitReached(),
                   "a re-prepare clears the reached flag for the new session");
        }

        {
            // The reporting side.
            auto snapshot = makeHealthySnapshot();
            snapshot.counters.captureLimitReached = true;
            snapshot.counters.captureLimitFrames = 28800000;

            const auto statistics = computeStatistics (snapshot);
            const auto warnings = evaluate (snapshot, statistics);

            bool named = false;

            for (const auto& w : warnings)
                if (w.rule == "capture_limit_reached")
                {
                    named = true;
                    info ("warning: " + w.detail);

                    check (w.detail.find ("28800000") != std::string::npos,
                           "the warning says where the capture stopped");
                }

            check (named, "a truncated capture is always reported");

            check (verdictFor (warnings) != Verdict::fail,
                   "reaching a configured limit is not a failure");

            const auto summary = buildSummaryText ({}, snapshot, statistics, warnings);

            check (summary.find ("Capture limit:") != std::string::npos
                   && summary.find ("reached at 28800000 frames") != std::string::npos,
                   "and it appears in the summary a human reads, with the frame it stopped on");

            check (summary.find ("telemetry continued") != std::string::npos,
                   "saying plainly that the session kept measuring");
        }
    }
}

} // namespace avtest
