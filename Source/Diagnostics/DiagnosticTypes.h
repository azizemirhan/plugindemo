#pragma once

#include <cstdint>
#include <type_traits>

/**
    The data the audio thread is allowed to hand out, and nothing else.

    Everything in this header is a plain aggregate of scalars and enums. No
    strings, no containers, no virtual functions, no constructors that do work.
    That is not a style preference — it is what makes it safe for the audio
    thread to fill one of these in and hand it over. A type that owned a string
    would allocate; a type with a vtable would be an indirect call on the audio
    thread for no benefit; a type with a std::string field would tempt somebody
    into formatting a message where formatting is forbidden.

    Turning any of this into human-readable text happens on the reporting
    thread, from the enums below.
*/
namespace av::diag
{

/** Schema version of session.json and of these structures. */
inline constexpr int schemaVersion = 1;

/**
    How much the plugin observes about itself.

    Level 0 has to be genuinely free, not cheap: the whole diagnostic path is
    compiled out unless AV_DEV_DIAGNOSTICS is on, and even then Level 0 takes a
    single relaxed atomic load and an early return.
*/
enum class Level : int
{
    off        = 0,   /**< No events, no snapshots, no capture. */
    lightweight = 1,  /**< Control-rate snapshots and health counters. */
    full       = 2    /**< Adds capture, parameter history and timelines. */
};

/** What happened. One enum value, resolved to text by the report builder. */
enum class EventType : int
{
    unknown = 0,
    parameterChange,
    stateRestore,
    prepareSampleRate,
    prepareBlockSize,
    reset,
    modulationEngaged,
    modulationBypassed,
    morphStart,
    morphComplete,
    morphTimeout,
    bankSwap,
    compensationChange,
    nonFiniteDetected,
    clipWarning,
    queueOverflow,
    captureOverflow,
    uncalibratedRegion,
    sessionStart,
    sessionStop,
    deadlineExceeded,     /**< One callback ran longer than its own block period. */
    captureLimitReached,  /**< The configured capture limit stopped the recording. */
    transportChanged,     /**< The host started or stopped the transport. */
    transportSeek,        /**< The playhead jumped rather than advancing. */
    bypassChanged         /**< The plugin was bypassed or un-bypassed. */
};

/** Packed state bits carried by an event, so a warning has its context. */
/**
    Why the playhead jumped.

    Test A recorded three "seeks" that were a pattern loop wrapping from 4.615 s
    back to 0.000 s. Reporting them as seeks is not wrong - the playhead really
    did jump - but nothing in the session distinguished them from the user
    clicking in the timeline, and they had to be separated by hand afterwards.

    `unknown` is a real answer and is used whenever the host has not given
    enough information to tell. A guess dressed up as a classification would be
    worse than the ambiguity it hides.
*/
enum class SeekReason : int
{
    unknown = 0,   /**< The host reported no loop information. */
    userSeek,      /**< The host is not looping, or the jump does not fit a wrap. */
    loopWrap       /**< Looping, and the playhead landed on the loop start. */
};

namespace eventFlags
{
    inline constexpr int32_t transportPlaying = 1 << 0;
    inline constexpr int32_t morphActive      = 1 << 1;
    inline constexpr int32_t captureEnabled   = 1 << 2;
    inline constexpr int32_t captureLimited   = 1 << 3;
    inline constexpr int32_t offlineRendering = 1 << 4;

    /** Telemetry level occupies bits 5-6. */
    inline constexpr int32_t levelShift = 5;
    inline constexpr int32_t levelMask  = 0x3 << levelShift;

    [[nodiscard]] inline constexpr int32_t packLevel (int level) noexcept
    {
        return (level & 0x3) << levelShift;
    }

    [[nodiscard]] inline constexpr int unpackLevel (int32_t flags) noexcept
    {
        return (flags & levelMask) >> levelShift;
    }

    /** Seek reason occupies bits 7-8. Only meaningful on a transportSeek. */
    inline constexpr int32_t seekReasonShift = 7;
    inline constexpr int32_t seekReasonMask  = 0x3 << seekReasonShift;

    [[nodiscard]] inline constexpr int32_t packSeekReason (SeekReason reason) noexcept
    {
        return (static_cast<int32_t> (reason) & 0x3) << seekReasonShift;
    }

    [[nodiscard]] inline constexpr SeekReason unpackSeekReason (int32_t flags) noexcept
    {
        return static_cast<SeekReason> ((flags & seekReasonMask) >> seekReasonShift);
    }
}

/** Which parameter an event refers to, where it refers to one. */
enum class ParameterId : int
{
    none = 0,
    decay,
    size,
    modRate,
    modDepth,
    earlyDiffusion,
    mix,
    width,
    other
};

/** Why a Size morph ended, mirroring MorphingStereoFdn::PrimeReason. */
enum class MorphReason : int
{
    none = 0,
    levelSettled,
    silence,
    timedOut
};

/**
    One thing that happened, at a known moment.

    Fixed at 32 bytes so a queue of them is a flat array and pushing one is a
    couple of stores. `value` and `previous` carry whatever the event needs —
    a parameter's new and old value, a block size, a sample count — and the
    report builder knows which is which from the type.
*/
struct DiagnosticEvent
{
    int64_t     sampleTime = 0;      /**< Samples since the session started. */
    EventType   type = EventType::unknown;
    ParameterId parameter = ParameterId::none;
    float       value = 0.0f;
    float       previous = 0.0f;

    /*
        Three extra fields, so a deadline overrun carries its own evidence
        instead of being a bare count that has to be explained afterwards.

        The first FL Studio session reported 17 overruns and nothing else about
        them. Working out why meant reasoning from the one worst-callback figure
        and the observed block-size range - and that reasoning could not be
        checked, because the block size at the moment of each overrun was never
        recorded. For a deadline event these now hold the block size, the
        callback duration, the budget that block actually had, and the state the
        plugin was in.
    */
    int32_t     integerValue = 0;    /**< Deadline events: runtime block size. */
    float       microseconds = 0.0f; /**< Deadline events: callback duration. */
    float       budgetMicroseconds = 0.0f;
    int32_t     flags = 0;           /**< See eventFlags. */
};

static_assert (sizeof (DiagnosticEvent) <= 48, "DiagnosticEvent must stay small");
static_assert (std::is_trivially_copyable_v<DiagnosticEvent>,
               "DiagnosticEvent crosses a thread boundary by being copied");

/**
    Health of the audio passing through, accumulated on the audio thread.

    Counters only: sums, maxima and counts, all of which are cheap and none of
    which need history. Anything that needs a window — percentiles, RT60,
    correlation over time — is computed later from these or from captured audio.
*/
struct DiagnosticAudioMetrics
{
    double inputPeakLeft = 0.0;
    double inputPeakRight = 0.0;
    double outputPeakLeft = 0.0;
    double outputPeakRight = 0.0;

    double inputSumSquaresLeft = 0.0;
    double inputSumSquaresRight = 0.0;
    double outputSumSquaresLeft = 0.0;
    double outputSumSquaresRight = 0.0;

    /* Level 2 only. Stereo correlation, mono downmix and DC are full-telemetry
       measurements, so at level 1 these stay at zero and the report leaves the
       corresponding fields empty rather than showing a figure accumulated from
       part of a session. */
    double outputSumProduct = 0.0;
    double outputSumMonoSquares = 0.0;

    double outputSumLeft = 0.0;      /**< For DC. */
    double outputSumRight = 0.0;

    int64_t sampleCount = 0;

    int64_t nanCount = 0;
    int64_t infCount = 0;
    int64_t subnormalCount = 0;
    int64_t clippedCount = 0;        /**< |sample| > 1.0 after the output gain. */
    int64_t overUnityCount = 0;      /**< Raw internal, before the preview gain. */

    double rawInternalPeak = 0.0;    /**< Before the preview gain. */

    int64_t silentBlockCount = 0;
    int64_t longestSilenceSamples = 0;
};

/** Where the DSP is, at one moment. Filled at control rate. */
struct DiagnosticDspState
{
    double decayTargetSeconds = 0.0;
    double effectiveLoopDelaySamples = 0.0;   /**< Mean allpass contribution. */
    double sizeTarget = 1.0;
    double sizeActive = 1.0;
    double modRateHz = 0.0;
    double modDepth = 0.0;

    /*
        Wet/dry mix and stereo width, 0..1 and 0..2 as the controls give them.

        Carried because Test A and Test B both needed "Mix is at 100% wet" and
        "Width is at 100%" to be true, and neither could be measured - the
        operator had to read them off the screen and be believed. A test
        condition nobody can verify afterwards is not a recorded condition.
    */
    double mix = 0.0;
    double width = 1.0;

    double minFeedbackGain = 0.0;
    double maxFeedbackGain = 0.0;
    double compensationGainDb = 0.0;

    int32_t architecture = 0;        /**< architecture::Candidate as an int. */
    int32_t activeBank = 0;
    int32_t inputDiffuserStages = 0;

    bool allpassEngaged = false;
    bool matrixEngaged = false;
    bool morphActive = false;
    bool compensationEnabled = true;
    bool decayInsideCalibration = true;

    double morphProgress = 0.0;
    double morphElapsedSeconds = 0.0;
    double morphTimeoutSeconds = 0.0;
    MorphReason lastMorphReason = MorphReason::none;

    int64_t bankSwapCount = 0;
    int64_t morphStartCount = 0;
    int64_t morphTimeoutCount = 0;
    int64_t coalescedTargetCount = 0;
    int64_t pendingTargetCount = 0;
};

/** What the host told us, as far as it can be trusted. */
struct DiagnosticHostState
{
    double sampleRate = 0.0;
    int32_t prepareBlockSize = 0;
    int32_t minObservedBlockSize = 0;
    int32_t maxObservedBlockSize = 0;
    int32_t inputChannels = 0;
    int32_t outputChannels = 0;

    double bpm = 0.0;
    int32_t timeSignatureNumerator = 0;
    int32_t timeSignatureDenominator = 0;

    bool doublePrecision = false;
    bool transportPlaying = false;
    bool offlineRendering = false;

    bool bpmAvailable = false;
    bool timeSignatureAvailable = false;
    bool transportAvailable = false;
};

/**
    Processing-time histogram.

    The audio thread must not compute percentiles, so it drops each callback's
    duration into a bin and moves on. Bins are logarithmic in microseconds,
    which covers a fraction of a microsecond to tens of milliseconds in 48 bins
    without needing to know the block size in advance. Percentiles come out of
    the histogram on the reporting thread.
*/
inline constexpr int histogramBins = 48;

/** Bin edges, in microseconds: 0.5 us * 1.5^n. */
[[nodiscard]] inline double histogramBinLowerMicroseconds (int bin) noexcept
{
    double edge = 0.5;

    for (int i = 0; i < bin; ++i)
        edge *= 1.5;

    return edge;
}

struct DiagnosticCounters
{
    int64_t callbackCount = 0;
    int64_t processedSamples = 0;

    int64_t totalProcessingNanoseconds = 0;
    int64_t worstProcessingNanoseconds = 0;
    int32_t worstCallbackBlockSize = 0;

    /*
        The budget a callback has is set by the block it was actually given, not
        by the block size the host announced in prepareToPlay. FL Studio
        announced 512 and then handed over blocks between 1 and 213 samples; a
        ratio computed against 512 made the plugin look like it was using 0.3 %
        of its budget while seventeen callbacks were missing their real deadline.

        These fields carry the per-callback truth: the last block's size, its
        duration, the budget that block had, and the worst ratio seen along with
        the block size that produced it.
    */
    int32_t lastBlockSize = 0;
    int64_t lastProcessingNanoseconds = 0;
    int64_t lastBudgetNanoseconds = 0;

    double  worstBudgetRatio = 0.0;
    int32_t worstBudgetRatioBlockSize = 0;
    int64_t worstBudgetRatioNanoseconds = 0;

    int64_t deadlineExceededCount = 0;

    int64_t histogram[histogramBins] {};

    int64_t eventsPushed = 0;
    int64_t eventsDropped = 0;
    int64_t captureFramesWritten = 0;
    int64_t captureFramesDropped = 0;

    /** Snapshots handed to the consumer, and refused because it fell behind. */
    int64_t snapshotsPublished = 0;
    int64_t snapshotsDropped = 0;

    /** True once the configured capture limit stopped the recording. */
    bool    captureLimitReached = false;
    int64_t captureLimitFrames = 0;   /**< The limit that was applied. */

    /** Which bin a duration belongs in. Audio-thread safe: no allocation, no
        division by a runtime value, and bounded by construction. */
    [[nodiscard]] static int binFor (double microseconds) noexcept
    {
        if (microseconds <= 0.5)
            return 0;

        int bin = 0;
        double edge = 0.5;

        while (bin + 1 < histogramBins && microseconds > edge * 1.5)
        {
            edge *= 1.5;
            ++bin;
        }

        return bin;
    }
};

/**
    Everything the reporting thread reads at once.

    Written by the audio thread into a preallocated slot and published with a
    release store of a sequence number; read with acquire. A torn read is
    detected by the sequence number changing across the copy, and the reader
    simply tries again — it is never the audio thread that waits.
*/
struct DiagnosticSnapshot
{
    int64_t sampleTime = 0;
    DiagnosticAudioMetrics audio;
    DiagnosticDspState dsp;
    DiagnosticHostState host;
    DiagnosticCounters counters;
};

} // namespace av::diag
