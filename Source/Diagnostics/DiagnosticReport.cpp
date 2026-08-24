#include "DiagnosticReport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace av::diag
{

namespace
{
    double toDb (double linear) noexcept
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear) : -200.0;
    }

    std::string fixedText (double value, int decimals)
    {
        // snprintf with the C locale rather than a stream: a stream picks up
        // whatever locale the host has installed, and a German host would write
        // "0,31" into a JSON file that every parser would then reject.
        char buffer[64] {};
        std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, value);
        return buffer;
    }

    std::string pad (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : text + std::string (width - text.size(), ' ');
    }
}

const char* describe (Verdict verdict) noexcept
{
    switch (verdict)
    {
        case Verdict::pass:             return "PASS";
        case Verdict::passWithWarnings: return "PASS WITH WARNINGS";
        case Verdict::fail:             return "FAIL";
    }

    return "UNKNOWN";
}

const char* describe (EventType type) noexcept
{
    switch (type)
    {
        case EventType::unknown:            return "unknown";
        case EventType::parameterChange:    return "parameter_change";
        case EventType::stateRestore:       return "state_restore";
        case EventType::prepareSampleRate:  return "prepare_sample_rate";
        case EventType::prepareBlockSize:   return "prepare_block_size";
        case EventType::reset:              return "reset";
        case EventType::modulationEngaged:  return "modulation_engaged";
        case EventType::modulationBypassed: return "modulation_bypassed";
        case EventType::morphStart:         return "morph_start";
        case EventType::morphComplete:      return "morph_complete";
        case EventType::morphTimeout:       return "morph_timeout";
        case EventType::bankSwap:           return "bank_swap";
        case EventType::compensationChange: return "compensation_change";
        case EventType::nonFiniteDetected:  return "non_finite_detected";
        case EventType::clipWarning:        return "clip_warning";
        case EventType::queueOverflow:      return "queue_overflow";
        case EventType::captureOverflow:    return "capture_overflow";
        case EventType::uncalibratedRegion: return "uncalibrated_region";
        case EventType::sessionStart:       return "session_start";
        case EventType::sessionStop:        return "session_stop";
        case EventType::deadlineExceeded:   return "deadline_exceeded";
        case EventType::captureLimitReached:return "capture_limit_reached";
        case EventType::transportChanged:   return "transport_changed";
        case EventType::transportSeek:      return "transport_seek";
        case EventType::bypassChanged:      return "bypass_changed";
    }

    return "unknown";
}

const char* describe (ParameterId parameter) noexcept
{
    switch (parameter)
    {
        case ParameterId::none:           return "none";
        case ParameterId::decay:          return "decay";
        case ParameterId::size:           return "size";
        case ParameterId::modRate:        return "mod_rate";
        case ParameterId::modDepth:       return "mod_depth";
        case ParameterId::earlyDiffusion: return "early_diffusion";
        case ParameterId::mix:            return "mix";
        case ParameterId::width:          return "width";
        case ParameterId::other:          return "other";
    }

    return "unknown";
}

const char* describe (SeekReason reason) noexcept
{
    switch (reason)
    {
        case SeekReason::unknown:  return "unknown";
        case SeekReason::userSeek: return "user_seek";
        case SeekReason::loopWrap: return "loop_wrap";
    }

    return "none";
}

const char* describe (MorphReason reason) noexcept
{
    switch (reason)
    {
        case MorphReason::none:         return "none";
        case MorphReason::levelSettled: return "level_settled";
        case MorphReason::silence:      return "silence";
        case MorphReason::timedOut:     return "timed_out";
    }

    return "none";
}

const char* describe (Level level) noexcept
{
    switch (level)
    {
        case Level::off:         return "off";
        case Level::lightweight: return "lightweight";
        case Level::full:        return "full";
    }

    return "off";
}

double percentileMicroseconds (const DiagnosticCounters& counters, double fraction)
{
    int64_t total = 0;

    for (int bin = 0; bin < histogramBins; ++bin)
        total += counters.histogram[bin];

    if (total <= 0)
        return 0.0;

    const auto target = static_cast<int64_t> (std::ceil (fraction * static_cast<double> (total)));

    int64_t running = 0;

    for (int bin = 0; bin < histogramBins; ++bin)
    {
        running += counters.histogram[bin];

        if (running >= target)
        {
            // Upper edge of the bin: the honest bound, not an interpolation
            // into precision the histogram does not carry.
            return histogramBinLowerMicroseconds (bin) * 1.5;
        }
    }

    return histogramBinLowerMicroseconds (histogramBins - 1) * 1.5;
}

DiagnosticStatistics computeStatistics (const DiagnosticSnapshot& snapshot)
{
    DiagnosticStatistics s;

    const auto& counters = snapshot.counters;
    const auto& audio = snapshot.audio;

    if (counters.callbackCount > 0)
        s.meanProcessingMicroseconds =
            static_cast<double> (counters.totalProcessingNanoseconds)
            / static_cast<double> (counters.callbackCount) * 1.0e-3;

    s.medianProcessingMicroseconds = percentileMicroseconds (counters, 0.50);
    s.p95ProcessingMicroseconds    = percentileMicroseconds (counters, 0.95);
    s.p99ProcessingMicroseconds    = percentileMicroseconds (counters, 0.99);
    s.worstProcessingMicroseconds  = static_cast<double> (counters.worstProcessingNanoseconds) * 1.0e-3;

    if (snapshot.host.sampleRate > 0.0 && snapshot.host.prepareBlockSize > 0)
        s.prepareBudgetMicroseconds = 1.0e6 * snapshot.host.prepareBlockSize / snapshot.host.sampleRate;

    if (s.prepareBudgetMicroseconds > 0.0)
    {
        s.meanBudgetFraction = s.meanProcessingMicroseconds / s.prepareBudgetMicroseconds;
        s.p99BudgetFraction  = s.p99ProcessingMicroseconds  / s.prepareBudgetMicroseconds;
    }

    s.worstRuntimeBudgetRatio = counters.worstBudgetRatio;
    s.worstRuntimeBudgetBlockSize = counters.worstBudgetRatioBlockSize;
    s.worstRuntimeBudgetMicroseconds =
        static_cast<double> (counters.worstBudgetRatioNanoseconds) * 1.0e-3;

    s.lastBlockSize = counters.lastBlockSize;
    s.lastBlockBudgetMicroseconds = static_cast<double> (counters.lastBudgetNanoseconds) * 1.0e-3;

    if (counters.lastBudgetNanoseconds > 0)
        s.lastBlockRatio = static_cast<double> (counters.lastProcessingNanoseconds)
                         / static_cast<double> (counters.lastBudgetNanoseconds);

    if (audio.sampleCount > 0)
    {
        const auto n = static_cast<double> (audio.sampleCount);

        s.inputRmsLeft   = std::sqrt (audio.inputSumSquaresLeft / n);
        s.inputRmsRight  = std::sqrt (audio.inputSumSquaresRight / n);
        s.outputRmsLeft  = std::sqrt (audio.outputSumSquaresLeft / n);
        s.outputRmsRight = std::sqrt (audio.outputSumSquaresRight / n);

        s.dcLeft  = audio.outputSumLeft / n;
        s.dcRight = audio.outputSumRight / n;

        const double meanRms = 0.5 * (s.outputRmsLeft + s.outputRmsRight);

        if (meanRms > 0.0)
            s.dcFractionOfRms = std::max (std::abs (s.dcLeft), std::abs (s.dcRight)) / meanRms;

        // Correlation with the means taken as zero. For audio that is what is
        // wanted: subtracting a mean would remove any DC the signal has, and DC
        // is reported separately as a fault rather than normalised away.
        const double denominator = std::sqrt (audio.outputSumSquaresLeft
                                              * audio.outputSumSquaresRight);

        if (denominator > 0.0)
            s.stereoCorrelation = audio.outputSumProduct / denominator;

        if (s.outputRmsLeft > 0.0 && s.outputRmsRight > 0.0)
            s.stereoBalanceDb = 20.0 * std::log10 (s.outputRmsLeft / s.outputRmsRight);

        const double monoRms = std::sqrt (audio.outputSumMonoSquares / n);

        if (monoRms > 0.0 && meanRms > 0.0)
            s.monoDownmixDb = 20.0 * std::log10 (monoRms / meanRms);

        const double peak = std::max (audio.outputPeakLeft, audio.outputPeakRight);

        if (meanRms > 0.0)
            s.crestFactor = peak / meanRms;

        if (snapshot.host.sampleRate > 0.0)
            s.sessionSeconds = n / snapshot.host.sampleRate;
    }

    s.inputPeakDbLeft   = toDb (audio.inputPeakLeft);
    s.inputPeakDbRight  = toDb (audio.inputPeakRight);
    s.outputPeakDbLeft  = toDb (audio.outputPeakLeft);
    s.outputPeakDbRight = toDb (audio.outputPeakRight);

    return s;
}

std::vector<DiagnosticWarning> evaluate (const DiagnosticSnapshot& snapshot,
                                         const DiagnosticStatistics& statistics)
{
    std::vector<DiagnosticWarning> warnings;

    const auto& audio = snapshot.audio;
    const auto& counters = snapshot.counters;

    const auto add = [&warnings] (const char* rule, const std::string& detail, bool fatal)
    {
        warnings.push_back ({ rule, detail, fatal });
    };

    // --- Fatal ---------------------------------------------------------
    if (audio.nanCount > 0)
        add ("nan_detected", std::to_string (audio.nanCount) + " NaN samples", true);

    if (audio.infCount > 0)
        add ("inf_detected", std::to_string (audio.infCount) + " Inf samples", true);

    if (counters.callbackCount > 0 && audio.sampleCount == 0)
        add ("no_audio_observed",
             "callbacks ran but no samples were observed", true);

    // --- Warnings ------------------------------------------------------
    const double outputPeak = std::max (audio.outputPeakLeft, audio.outputPeakRight);

    if (outputPeak > thresholds::outputPeakWarning)
        add ("output_over_full_scale",
             "peak " + fixedText (outputPeak, 4) + " ("
             + fixedText (toDb (outputPeak), 2) + " dBFS)", false);

    if (audio.clippedCount > 0)
        add ("clipped_samples", std::to_string (audio.clippedCount) + " samples above 1.0", false);

    if (audio.rawInternalPeak > 1.0)
        add ("raw_internal_over_unity",
             "internal peak " + fixedText (audio.rawInternalPeak, 4)
             + " before the output gain", false);

    if (statistics.prepareBudgetMicroseconds > 0.0
        && statistics.p99BudgetFraction > thresholds::cpuP99BudgetFraction)
        add ("cpu_p99_over_budget",
             "p99 " + fixedText (100.0 * statistics.p99BudgetFraction, 1)
             + " % of the prepare block budget, limit "
             + fixedText (100.0 * thresholds::cpuP99BudgetFraction, 1) + " %", false);

    if (counters.deadlineExceededCount > 0)
        add ("deadline_exceeded",
             std::to_string (counters.deadlineExceededCount)
             + " of " + std::to_string (counters.callbackCount)
             + " callbacks longer than their own block period; worst ratio "
             + fixedText (statistics.worstRuntimeBudgetRatio, 2) + "x on a "
             + std::to_string (statistics.worstRuntimeBudgetBlockSize) + "-sample block", false);

    if (counters.captureLimitReached)
        add ("capture_limit_reached",
             "audio capture stopped at the configured limit of "
             + std::to_string (counters.captureLimitFrames)
             + " frames; telemetry continued", false);

    if (counters.eventsDropped > 0)
        add ("event_queue_overflow",
             std::to_string (counters.eventsDropped) + " events dropped", false);

    if (counters.captureFramesDropped > 0)
        add ("capture_overflow",
             std::to_string (counters.captureFramesDropped) + " frames dropped from capture", false);

    if (counters.snapshotsDropped > 0)
        add ("snapshot_queue_overflow",
             std::to_string (counters.snapshotsDropped)
             + " snapshots dropped; the consumer fell behind", false);

    if (statistics.dcFractionOfRms > thresholds::dcFractionWarning)
        add ("dc_offset",
             "DC is " + fixedText (100.0 * statistics.dcFractionOfRms, 2) + " % of RMS", false);

    if (audio.subnormalCount > 0)
        add ("subnormals",
             std::to_string (audio.subnormalCount) + " subnormal samples", false);

    /*
        Morph timeouts.

        The fraction rule below needs a denominator, so it is guarded by
        morphStartCount > 0. That guard was silently fatal while nothing wrote
        morphStartCount: it was always zero, the branch was never entered, and a
        rule meant to catch real timeouts could not fire at all. The counters
        are fed now, but a guard that turns a safety rule off when its input is
        missing is the wrong shape.

        So a timeout is reported whether or not the start count is available.
        With starts known it is a percentage, which is what actually matters -
        two timeouts out of three is a different finding from two out of two
        hundred. Without them it is still reported, as a bare count, because a
        timeout that happened is worth saying even when its share is unknown.
    */
    if (snapshot.dsp.morphTimeoutCount > 0)
    {
        if (snapshot.dsp.morphStartCount > 0)
        {
            const double fraction = static_cast<double> (snapshot.dsp.morphTimeoutCount)
                                  / static_cast<double> (snapshot.dsp.morphStartCount);

            if (fraction > thresholds::morphTimeoutFractionWarning)
                add ("morph_timeouts",
                     std::to_string (snapshot.dsp.morphTimeoutCount) + " of "
                     + std::to_string (snapshot.dsp.morphStartCount)
                     + " Size morphs timed out ("
                     + fixedText (100.0 * fraction, 1) + " %)", false);
        }
        else
        {
            add ("morph_timeouts",
                 std::to_string (snapshot.dsp.morphTimeoutCount)
                 + " Size morphs timed out; the start count was not recorded, so"
                   " the share cannot be given", false);
        }
    }

    if (! snapshot.dsp.decayInsideCalibration)
        add ("uncalibrated_region",
             "Decay " + fixedText (snapshot.dsp.decayTargetSeconds, 2)
             + " s is outside the Size compensation calibration", false);

    return warnings;
}

Verdict verdictFor (const std::vector<DiagnosticWarning>& warnings) noexcept
{
    for (const auto& warning : warnings)
        if (warning.fatal)
            return Verdict::fail;

    return warnings.empty() ? Verdict::pass : Verdict::passWithWarnings;
}

std::string jsonNumber (double value, int decimals)
{
    if (std::isnan (value) || std::isinf (value))
        return "null";

    return fixedText (value, decimals);
}

std::string jsonString (const std::string& text)
{
    std::string out = "\"";

    for (char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char> (c) < 0x20)
                {
                    char buffer[8] {};
                    std::snprintf (buffer, sizeof (buffer), "\\u%04x",
                                   static_cast<unsigned> (static_cast<unsigned char> (c)));
                    out += buffer;
                }
                else
                {
                    out += c;
                }
        }
    }

    return out + "\"";
}

std::string describeHost (const DiagnosticSessionInfo& info)
{
    // "unavailable" is what the field holds when nothing filled it in, and
    // appending it reads as a version string rather than as its absence.
    const bool haveVersion = ! info.hostVersion.empty()
                             && info.hostVersion != "unavailable";

    return haveVersion ? info.hostName + " " + info.hostVersion
                       : info.hostName + "  (version unavailable)";
}

std::string buildSessionJson (const DiagnosticSessionInfo& info,
                              const DiagnosticSnapshot& snapshot,
                              const DiagnosticStatistics& statistics,
                              const std::vector<DiagnosticWarning>& warnings)
{
    const auto& audio = snapshot.audio;
    const auto& counters = snapshot.counters;
    const auto& dsp = snapshot.dsp;
    const auto& host = snapshot.host;

    std::string j;
    j.reserve (8192);

    const auto field = [&j] (const char* name, const std::string& value, bool last = false)
    {
        j += "    ";
        j += jsonString (name);
        j += ": ";
        j += value;
        j += last ? "\n" : ",\n";
    };

    j += "{\n";
    j += "  \"schema_version\": " + std::to_string (schemaVersion) + ",\n";

    j += "  \"plugin\": {\n";
    field ("name", jsonString (info.pluginName));
    field ("version", jsonString (info.pluginVersion));
    field ("format", jsonString (info.pluginFormat));
    field ("instance_id", jsonString (info.instanceId), true);
    j += "  },\n";

    j += "  \"build\": {\n";
    field ("configuration", jsonString (info.buildConfiguration));
    field ("diagnostics_compiled_in", info.diagnosticsCompiledIn ? "true" : "false");
    field ("engine_active", info.engineActive ? "true" : "false");
    field ("build_profile", jsonString (info.buildProfile));
    field ("capture_compiled_in", info.captureCompiledIn ? "true" : "false");
    field ("architecture", jsonString (info.architectureName), true);
    j += "  },\n";

    j += "  \"host\": {\n";
    field ("name", jsonString (info.hostName));
    field ("version", jsonString (info.hostVersion));
    field ("transport_playing", host.transportAvailable
                                ? (host.transportPlaying ? "true" : "false") : "null");
    field ("offline_rendering", host.transportAvailable
                                ? (host.offlineRendering ? "true" : "false") : "null");
    field ("bpm", host.bpmAvailable ? jsonNumber (host.bpm, 3) : "null");
    field ("time_signature", host.timeSignatureAvailable
           ? jsonString (std::to_string (host.timeSignatureNumerator) + "/"
                         + std::to_string (host.timeSignatureDenominator))
           : "null", true);
    j += "  },\n";

    j += "  \"audio_configuration\": {\n";
    field ("sample_rate", jsonNumber (host.sampleRate, 1));
    field ("prepare_block_size", std::to_string (host.prepareBlockSize));
    field ("min_observed_block_size", std::to_string (host.minObservedBlockSize));
    field ("max_observed_block_size", std::to_string (host.maxObservedBlockSize));
    field ("input_channels", std::to_string (host.inputChannels));
    field ("output_channels", std::to_string (host.outputChannels));
    field ("double_precision", host.doublePrecision ? "true" : "false", true);
    j += "  },\n";

    j += "  \"session\": {\n";
    field ("start_timestamp", jsonString (info.startTimestamp));
    field ("path", jsonString (info.sessionPath));
    field ("telemetry_level", jsonString (describe (info.level)));
    field ("capture_enabled", info.captureEnabled ? "true" : "false");
    field ("duration_seconds", jsonNumber (statistics.sessionSeconds, 3));
    field ("callbacks", std::to_string (counters.callbackCount));
    field ("processed_samples", std::to_string (counters.processedSamples), true);
    j += "  },\n";

    j += "  \"dsp_configuration\": {\n";
    field ("decay_target_seconds", jsonNumber (dsp.decayTargetSeconds, 4));
    field ("decay_inside_calibration", dsp.decayInsideCalibration ? "true" : "false");
    field ("effective_loop_delay_samples", jsonNumber (dsp.effectiveLoopDelaySamples, 3));
    field ("size_target", jsonNumber (dsp.sizeTarget, 4));
    field ("size_active", jsonNumber (dsp.sizeActive, 4));
    field ("mod_rate_hz", jsonNumber (dsp.modRateHz, 4));
    field ("mod_depth", jsonNumber (dsp.modDepth, 4));
    field ("mix", jsonNumber (dsp.mix, 4));
    field ("width", jsonNumber (dsp.width, 4));
    field ("allpass_engaged", dsp.allpassEngaged ? "true" : "false");
    field ("matrix_engaged", dsp.matrixEngaged ? "true" : "false");
    field ("min_feedback_gain", jsonNumber (dsp.minFeedbackGain, 6));
    field ("max_feedback_gain", jsonNumber (dsp.maxFeedbackGain, 6));
    field ("input_diffuser_stages", std::to_string (dsp.inputDiffuserStages), true);
    j += "  },\n";

    j += "  \"performance\": {\n";
    field ("mean_microseconds", jsonNumber (statistics.meanProcessingMicroseconds, 3));
    field ("median_microseconds", jsonNumber (statistics.medianProcessingMicroseconds, 3));
    field ("p95_microseconds", jsonNumber (statistics.p95ProcessingMicroseconds, 3));
    field ("p99_microseconds", jsonNumber (statistics.p99ProcessingMicroseconds, 3));
    field ("worst_microseconds", jsonNumber (statistics.worstProcessingMicroseconds, 3));
    field ("worst_callback_block_size", std::to_string (counters.worstCallbackBlockSize));
    field ("prepare_budget_microseconds", jsonNumber (statistics.prepareBudgetMicroseconds, 3));
    field ("worst_runtime_budget_ratio", jsonNumber (statistics.worstRuntimeBudgetRatio, 5));
    field ("worst_runtime_budget_block_size",
           std::to_string (statistics.worstRuntimeBudgetBlockSize));
    field ("worst_runtime_budget_microseconds",
           jsonNumber (statistics.worstRuntimeBudgetMicroseconds, 3));
    field ("mean_budget_fraction", jsonNumber (statistics.meanBudgetFraction, 5));
    field ("p99_budget_fraction", jsonNumber (statistics.p99BudgetFraction, 5));
    field ("deadline_exceeded", std::to_string (counters.deadlineExceededCount), true);
    j += "  },\n";

    j += "  \"audio_health\": {\n";
    field ("input_peak_left", jsonNumber (audio.inputPeakLeft, 6));
    field ("input_peak_right", jsonNumber (audio.inputPeakRight, 6));
    field ("output_peak_left", jsonNumber (audio.outputPeakLeft, 6));
    field ("output_peak_right", jsonNumber (audio.outputPeakRight, 6));
    field ("raw_internal_peak", jsonNumber (audio.rawInternalPeak, 6));
    field ("input_rms_left", jsonNumber (statistics.inputRmsLeft, 6));
    field ("input_rms_right", jsonNumber (statistics.inputRmsRight, 6));
    field ("output_rms_left", jsonNumber (statistics.outputRmsLeft, 6));
    field ("output_rms_right", jsonNumber (statistics.outputRmsRight, 6));
    field ("nan_count", std::to_string (audio.nanCount));
    field ("inf_count", std::to_string (audio.infCount));
    field ("subnormal_count", std::to_string (audio.subnormalCount));
    field ("clipped_count", std::to_string (audio.clippedCount));
    field ("over_unity_blocks", std::to_string (audio.overUnityCount));
    field ("dc_left", jsonNumber (statistics.dcLeft, 8));
    field ("dc_right", jsonNumber (statistics.dcRight, 8));
    field ("crest_factor", jsonNumber (statistics.crestFactor, 4));
    field ("silent_blocks", std::to_string (audio.silentBlockCount));
    field ("longest_silence_samples", std::to_string (audio.longestSilenceSamples), true);
    j += "  },\n";

    j += "  \"stereo\": {\n";
    field ("correlation", jsonNumber (statistics.stereoCorrelation, 6));
    field ("balance_db", jsonNumber (statistics.stereoBalanceDb, 4));
    field ("mono_downmix_db", jsonNumber (statistics.monoDownmixDb, 4), true);
    j += "  },\n";

    j += "  \"morphing\": {\n";
    field ("morph_starts", std::to_string (dsp.morphStartCount));
    field ("morph_timeouts", std::to_string (dsp.morphTimeoutCount));
    field ("bank_swaps", std::to_string (dsp.bankSwapCount));
    field ("coalesced_targets", std::to_string (dsp.coalescedTargetCount));
    field ("pending_targets", std::to_string (dsp.pendingTargetCount));
    field ("active", dsp.morphActive ? "true" : "false");
    field ("last_reason", jsonString (describe (dsp.lastMorphReason)), true);
    j += "  },\n";

    j += "  \"compensation\": {\n";
    field ("enabled", dsp.compensationEnabled ? "true" : "false");
    field ("gain_db", jsonNumber (dsp.compensationGainDb, 4), true);
    j += "  },\n";

    j += "  \"event_counters\": {\n";
    field ("pushed", std::to_string (counters.eventsPushed));
    field ("dropped", std::to_string (counters.eventsDropped));
    field ("snapshots_published", std::to_string (counters.snapshotsPublished));
    field ("snapshots_dropped", std::to_string (counters.snapshotsDropped), true);
    j += "  },\n";

    j += "  \"capture\": {\n";
    field ("frames_written", std::to_string (counters.captureFramesWritten));
    field ("frames_dropped", std::to_string (counters.captureFramesDropped), true);
    j += "  },\n";

    j += "  \"warnings\": [\n";

    for (size_t i = 0; i < warnings.size(); ++i)
    {
        j += "    { \"rule\": " + jsonString (warnings[i].rule)
             + ", \"detail\": " + jsonString (warnings[i].detail)
             + ", \"fatal\": " + (warnings[i].fatal ? "true" : "false") + " }";
        j += (i + 1 < warnings.size()) ? ",\n" : "\n";
    }

    j += "  ],\n";

    j += "  \"validation\": {\n";
    field ("verdict", jsonString (describe (verdictFor (warnings))));
    field ("warning_count", std::to_string (warnings.size()), true);
    j += "  }\n";

    j += "}\n";

    return j;
}

std::string buildSummaryText (const DiagnosticSessionInfo& info,
                              const DiagnosticSnapshot& snapshot,
                              const DiagnosticStatistics& statistics,
                              const std::vector<DiagnosticWarning>& warnings)
{
    const auto& audio = snapshot.audio;
    const auto& counters = snapshot.counters;
    const auto& host = snapshot.host;

    std::ostringstream out;

    const auto line = [&out] (const char* label, const std::string& value)
    {
        out << pad (label, 26) << value << "\n";
    };

    out << "AuroraVerb Diagnostic Report\n";
    out << "============================\n\n";

    line ("Build:", info.buildConfiguration
                    + (info.engineActive ? " (FDN engine active)" : " (NO ENGINE - identity wet path)"));
    line ("Build profile:", info.buildProfile);
    line ("Architecture:", info.architectureName);
    // The version is only appended when it is actually known. "pluginval
    // unavailable" reads as a version string rather than as its absence.
    line ("Host:", describeHost (info));
    line ("Format:", info.pluginFormat);
    line ("Telemetry level:", describe (info.level));
    line ("Sample rate:", fixedText (host.sampleRate, 1) + " Hz");
    line ("Block sizes:", std::to_string (host.minObservedBlockSize) + " to "
                          + std::to_string (host.maxObservedBlockSize)
                          + " (prepared for " + std::to_string (host.prepareBlockSize) + ")");
    line ("Session duration:", fixedText (statistics.sessionSeconds, 2) + " s");
    line ("Callbacks:", std::to_string (counters.callbackCount));

    out << "\nDSP Status\n----------\n";
    line ("Decay:", fixedText (snapshot.dsp.decayTargetSeconds, 3) + " s"
                    + (snapshot.dsp.decayInsideCalibration ? "" : "  (outside calibration)"));
    line ("Size:", fixedText (snapshot.dsp.sizeActive, 3));
    line ("Mod Rate:", fixedText (snapshot.dsp.modRateHz, 3) + " Hz");
    line ("Mod Depth:", fixedText (snapshot.dsp.modDepth, 3));
    // Spelled out as a percentage as well as a fraction: a test plan that says
    // "Mix 100% wet" should be checkable against this line without arithmetic.
    line ("Mix:", fixedText (snapshot.dsp.mix, 4)
                  + "  (" + fixedText (100.0 * snapshot.dsp.mix, 1) + "% wet)");
    line ("Width:", fixedText (snapshot.dsp.width, 4)
                    + "  (" + fixedText (100.0 * snapshot.dsp.width, 1) + "%)");
    line ("Allpass:", snapshot.dsp.allpassEngaged ? "engaged" : "bypassed");
    line ("Matrix:", snapshot.dsp.matrixEngaged ? "engaged" : "bypassed");
    line ("Compensation:", fixedText (snapshot.dsp.compensationGainDb, 2) + " dB"
                           + (snapshot.dsp.compensationEnabled ? "" : "  (disabled)"));

    out << "\nCPU\n---\n";
    line ("Mean:", fixedText (statistics.meanProcessingMicroseconds, 2) + " us");
    line ("Median:", fixedText (statistics.medianProcessingMicroseconds, 2) + " us");
    line ("p95:", fixedText (statistics.p95ProcessingMicroseconds, 2) + " us");
    line ("p99:", fixedText (statistics.p99ProcessingMicroseconds, 2) + " us");
    line ("Max:", fixedText (statistics.worstProcessingMicroseconds, 2) + " us  (block "
                  + std::to_string (counters.worstCallbackBlockSize) + ")");

    out << "\n  against the block size the host announced (" << host.prepareBlockSize
        << " samples, " << fixedText (statistics.prepareBudgetMicroseconds, 1) << " us):\n";
    line ("  mean:", fixedText (100.0 * statistics.meanBudgetFraction, 2) + " %");
    line ("  p99:", fixedText (100.0 * statistics.p99BudgetFraction, 2) + " %");

    out << "\n  against each callback's own block period, which is the deadline\n";
    out << "  that actually has to be met:\n";
    line ("  worst ratio:", fixedText (statistics.worstRuntimeBudgetRatio, 3) + "x"
                            + (statistics.worstRuntimeBudgetRatio > 1.0 ? "  (over)" : ""));
    line ("  on block size:", std::to_string (statistics.worstRuntimeBudgetBlockSize)
                              + " samples, " + fixedText (statistics.worstRuntimeBudgetMicroseconds, 2)
                              + " us");
    line ("  deadline exceeded:", std::to_string (counters.deadlineExceededCount)
                                  + " of " + std::to_string (counters.callbackCount));

    out << "\nAudio Health\n------------\n";
    line ("Input peak:", fixedText (statistics.inputPeakDbLeft, 2) + " / "
                         + fixedText (statistics.inputPeakDbRight, 2) + " dBFS");
    line ("Output peak:", fixedText (statistics.outputPeakDbLeft, 2) + " / "
                          + fixedText (statistics.outputPeakDbRight, 2) + " dBFS");
    line ("Raw internal peak:", fixedText (audio.rawInternalPeak, 4));
    line ("Output RMS:", fixedText (toDb (statistics.outputRmsLeft), 2) + " / "
                         + fixedText (toDb (statistics.outputRmsRight), 2) + " dBFS");
    line ("NaN / Inf:", std::to_string (audio.nanCount) + " / " + std::to_string (audio.infCount));
    line ("Subnormals:", std::to_string (audio.subnormalCount));
    line ("Clipped samples:", std::to_string (audio.clippedCount));
    line ("DC offset:", fixedText (statistics.dcLeft, 8) + " / "
                        + fixedText (statistics.dcRight, 8));
    line ("Crest factor:", fixedText (statistics.crestFactor, 2));

    out << "\nStereo\n------\n";
    line ("Correlation:", fixedText (statistics.stereoCorrelation, 4));
    line ("Balance:", fixedText (statistics.stereoBalanceDb, 2) + " dB");
    line ("Mono downmix:", fixedText (statistics.monoDownmixDb, 2) + " dB");

    out << "\nMorphing and Telemetry\n----------------------\n";
    line ("Morph operations:", std::to_string (snapshot.dsp.morphStartCount));
    line ("Morph timeouts:", std::to_string (snapshot.dsp.morphTimeoutCount));
    line ("Bank swaps:", std::to_string (snapshot.dsp.bankSwapCount));
    line ("Events pushed:", std::to_string (counters.eventsPushed));
    line ("Queue drops:", std::to_string (counters.eventsDropped));
    line ("Snapshots published:", std::to_string (counters.snapshotsPublished));
    line ("Snapshot drops:", std::to_string (counters.snapshotsDropped));
    line ("Capture frames:", std::to_string (counters.captureFramesWritten));
    line ("Capture drops:", std::to_string (counters.captureFramesDropped));
    line ("Capture limit:", counters.captureLimitReached
                            ? ("reached at " + std::to_string (counters.captureLimitFrames)
                               + " frames - telemetry continued")
                            : "not reached");

    out << "\nWarnings\n--------\n";

    if (warnings.empty())
    {
        out << "  none\n";
    }
    else
    {
        for (const auto& warning : warnings)
            out << "  [" << (warning.fatal ? "FAIL" : "WARN") << "] "
                << pad (warning.rule, 26) << warning.detail << "\n";
    }

    out << "\nOverall technical result: " << describe (verdictFor (warnings)) << "\n";

    // ASCII only. summary.txt is opened in whatever editor the user has to
    // hand, and a UTF-8 em dash read as ANSI turns into mojibake in the one
    // line that matters most. session.json is UTF-8 and stays that way.
    out << "\nThis verdict is technical only. It says whether the plugin behaved correctly,\n";
    out << "not whether the reverb sounds good - nothing here judges that.\n";

    return out.str();
}

std::string timelineHeader()
{
    return "sample_time,seconds,callbacks,mean_us,worst_us,budget_fraction,"
           "block_size,block_min,block_max,callback_us,block_budget_us,block_ratio,"
           "deadline_flag,deadline_count,worst_ratio,worst_ratio_block,"
           "input_peak,output_peak,raw_peak,output_rms_l,output_rms_r,"
           "nan,inf,subnormal,clipped,correlation,mono_db,"
           "decay,size,mod_rate,mod_depth,mix,width,compensation_db,morph_active,"
           "events_dropped,capture_dropped,capture_limited\n";
}

std::string timelineRow (const DiagnosticSnapshot& snapshot,
                         const DiagnosticStatistics& statistics)
{
    const auto& audio = snapshot.audio;
    const auto& dsp = snapshot.dsp;

    std::string row;
    row.reserve (256);

    const auto add = [&row] (const std::string& value, bool last = false)
    {
        row += value;
        row += last ? "\n" : ",";
    };

    add (std::to_string (snapshot.sampleTime));
    add (jsonNumber (statistics.sessionSeconds, 3));
    add (std::to_string (snapshot.counters.callbackCount));
    add (jsonNumber (statistics.meanProcessingMicroseconds, 3));
    add (jsonNumber (statistics.worstProcessingMicroseconds, 3));
    add (jsonNumber (statistics.p99BudgetFraction, 5));

    // Per-callback deadline detail, so a plot can show which block sizes the
    // host delivered and which of them missed.
    add (std::to_string (statistics.lastBlockSize));
    add (std::to_string (snapshot.host.minObservedBlockSize));
    add (std::to_string (snapshot.host.maxObservedBlockSize));
    add (jsonNumber (static_cast<double> (snapshot.counters.lastProcessingNanoseconds) * 1.0e-3, 3));
    add (jsonNumber (statistics.lastBlockBudgetMicroseconds, 3));
    add (jsonNumber (statistics.lastBlockRatio, 5));
    add (statistics.lastBlockRatio > 1.0 ? "1" : "0");
    add (std::to_string (snapshot.counters.deadlineExceededCount));
    add (jsonNumber (statistics.worstRuntimeBudgetRatio, 5));
    add (std::to_string (statistics.worstRuntimeBudgetBlockSize));

    add (jsonNumber (std::max (audio.inputPeakLeft, audio.inputPeakRight), 6));
    add (jsonNumber (std::max (audio.outputPeakLeft, audio.outputPeakRight), 6));
    add (jsonNumber (audio.rawInternalPeak, 6));
    add (jsonNumber (statistics.outputRmsLeft, 6));
    add (jsonNumber (statistics.outputRmsRight, 6));
    add (std::to_string (audio.nanCount));
    add (std::to_string (audio.infCount));
    add (std::to_string (audio.subnormalCount));
    add (std::to_string (audio.clippedCount));
    add (jsonNumber (statistics.stereoCorrelation, 5));
    add (jsonNumber (statistics.monoDownmixDb, 3));
    add (jsonNumber (dsp.decayTargetSeconds, 4));
    add (jsonNumber (dsp.sizeActive, 4));
    add (jsonNumber (dsp.modRateHz, 4));
    add (jsonNumber (dsp.modDepth, 4));
    add (jsonNumber (dsp.mix, 4));
    add (jsonNumber (dsp.width, 4));
    add (jsonNumber (dsp.compensationGainDb, 3));
    add (dsp.morphActive ? "1" : "0");
    add (std::to_string (snapshot.counters.eventsDropped));
    add (std::to_string (snapshot.counters.captureFramesDropped));
    add (snapshot.counters.captureLimitReached ? "1" : "0", true);

    return row;
}

std::string eventJsonLine (const DiagnosticEvent& event, double sampleRate)
{
    std::string line = "{";

    line += "\"sample_time\": " + std::to_string (event.sampleTime);
    line += ", \"seconds\": " + jsonNumber (sampleRate > 0.0
                                            ? static_cast<double> (event.sampleTime) / sampleRate
                                            : 0.0, 4);
    line += ", \"type\": " + jsonString (describe (event.type));
    line += ", \"parameter\": " + jsonString (describe (event.parameter));
    line += ", \"value\": " + jsonNumber (static_cast<double> (event.value), 6);
    line += ", \"previous\": " + jsonNumber (static_cast<double> (event.previous), 6);
    line += ", \"integer_value\": " + std::to_string (event.integerValue);

    // A deadline overrun carries the numbers that explain it, so the report
    // never has to guess which block size caused it.
    if (event.type == EventType::deadlineExceeded)
    {
        line += ", \"block_size\": " + std::to_string (event.integerValue);
        line += ", \"callback_us\": " + jsonNumber (static_cast<double> (event.microseconds), 3);
        line += ", \"budget_us\": " + jsonNumber (static_cast<double> (event.budgetMicroseconds), 3);
        line += ", \"budget_ratio\": " + jsonNumber (static_cast<double> (event.value), 4);
    }

    // A seek says where it went, which way, and - when the host gave enough to
    // tell - whether it was the user or a loop wrapping.
    if (event.type == EventType::transportSeek)
    {
        line += ", \"direction\": " + std::string (event.integerValue > 0 ? "\"forward\""
                                                                         : "\"backward\"");
        line += ", \"seek_reason\": "
                + jsonString (describe (eventFlags::unpackSeekReason (event.flags)));
    }

    line += ", \"transport_playing\": "
            + std::string ((event.flags & eventFlags::transportPlaying) ? "true" : "false");
    line += ", \"offline_rendering\": "
            + std::string ((event.flags & eventFlags::offlineRendering) ? "true" : "false");
    line += ", \"morph_active\": "
            + std::string ((event.flags & eventFlags::morphActive) ? "true" : "false");
    line += ", \"capture_enabled\": "
            + std::string ((event.flags & eventFlags::captureEnabled) ? "true" : "false");
    line += ", \"capture_limited\": "
            + std::string ((event.flags & eventFlags::captureLimited) ? "true" : "false");
    line += ", \"telemetry_level\": "
            + jsonString (describe (static_cast<Level> (eventFlags::unpackLevel (event.flags))));

    line += "}";

    return line;
}

} // namespace av::diag
