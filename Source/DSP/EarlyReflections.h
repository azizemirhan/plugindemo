#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "EarlyReflectionTuning.h"
#include "SmoothedParameter.h"

namespace av::dsp
{

/**
    PHASE 10. Early reflections: an explicit sparse tap layer beside the tank.

    WHAT IT IS
    ----------
    A fixed stereo FIR on the pre-delayed wet signal, running in PARALLEL with
    the late network rather than in front of it:

        er_L[n] = sum_k  w_Lk * x_L[n - d_Lk]
        er_R[n] = sum_k  w_Rk * x_R[n - d_Rk]

    Eight arrivals per channel, sixteen taps, over a span that follows Size.
    Each channel has its OWN times and its OWN signs; the two schedules are
    decorrelated by construction. Weights are L2-normalised per channel, so
    moving the span changes when the energy lands and not how much of it does.

    WHY IT IS NOT ANY OF THE THINGS IT RESEMBLES
    --------------------------------------------
    PHASE 10 Stage 1 licensed this by elimination, and the distinctions are
    load-bearing rather than decorative.

    It is NOT another diffuser. A diffuser spreads energy until arrivals stop
    being countable; this layer exists to be countable. Stage 1 measured the
    tank's first 80 ms and found zero resolvable arrivals at every Size and
    Early Diffusion setting - the FDN goes straight to a dense field, so there
    is nothing here for a diffusion control to be a substitute for.

    It is NOT the first 100 ms of the late output relabelled. It is a separate
    signal computed from a separate branch, and at balance 0 it contributes
    nothing at all.

    It is NOT a wet gain envelope and NOT a second pre-delay. It has structure
    in time - discrete arrivals at irregular intervals - which neither of those
    can produce.

    And it is NOT a copied room. See EarlyReflectionTuning.h: the timing is a
    deterministic synthetic distribution, no impulse response was measured and
    no product's pattern was referenced.

    WHERE IT SITS, AND WHAT THAT GUARANTEES
    ---------------------------------------
        input -> Low Cut -> High Cut -> Pre-Delay -+-> Attack -> tank -> trim -+-> balance
                                                   |                           |
                                                   +-> early reflections ------+

    The split is AFTER the pre-delay, deliberately. An early reflection is
    still a reflection of a sound that has already been delayed, so pre-delay
    moves both branches by exactly the same amount and the Early/Late
    relationship is invariant under it. Splitting before the pre-delay would
    have made Pre-Delay a hidden early/late timing control.

    Four properties follow from the topology rather than from care taken here:

      - Attack cannot reach the ER layer, because Attack is on the other
        branch. An Attack sweep leaves the ER samples bit-identical.
      - Early Diffusion cannot reach it either: input diffusion lives inside
        the tank, downstream of the split.
      - Late damping stays a late-feedback property. The ER layer has no
        feedback to damp, and nothing applies the shelves to it.
      - The decay-aware trim applies to the tank only. It compensates the
        TANK's level for Decay and Size; multiplying the ER by it would make
        the ER level follow Decay, which is a late property the early layer
        does not have.

    THE TAP SCHEDULE
    ----------------
    Velvet noise. The span is divided into equal cells, one event per cell per
    channel, and each event is placed inside the middle 70 % of its cell by a
    fixed-seed pseudo-random draw. The two channels draw independently, so
    their arrivals interleave rather than doubling.

    Each tap also carries a deterministic +1 or -1, which flattens the FIR's
    magnitude response. Sixteen equally spaced taps would be a comb; sixteen
    irregular signed taps are not.

    THE PLACEMENT IS NOT PHASE 9'S, AND THAT IS DELIBERATE. Attack places its
    taps by the golden-ratio sequence inside equal cells. Stage 3's schedule
    test showed that construction yields only TWO distinct arrival gaps - the
    merged stereo sequence repeated exactly every seven arrivals - because
    frac(phi*k) advances by a constant mod 1. EarlyReflectionTuning.h carries
    the measurement and the reason PHASE 9 is left alone.

    Deterministic end to end. Same build, same sample rate, same taps, every
    time. Nothing is seeded from the clock and nothing survives a reset.

    SIZE
    ----
    The span is windowMs * sizeScaleRatio^0.25 - a 1.68x travel against the
    late network's 8x. The two move in the same direction without moving as
    one object, which is what makes Size read as one room growing rather than
    as two independent spaces.

    Tap positions are integer samples and they NEVER move while they are being
    read, because the derivative of a delay time is a pitch shift applied to
    whatever is passing through it. A Size change builds a second schedule and
    crossfades to it; a request arriving mid-fade overwrites a single pending
    slot and is applied when the fade ends. Coalesced, not queued, exactly as
    PHASE 8's pre-delay and PHASE 9's Attack do it.

    Two spans that round to the same integer sample count produce an identical
    schedule, so the comparison is made on that integer. A Size smoother
    creeping by a fraction of a sample does not restart the fade.

    THE DELAY LINE IS WRITTEN UNCONDITIONALLY
    -----------------------------------------
    Including while the layer is silent. A line that stops being written goes
    stale, and the first move away from Early/Late 0 would read whatever was
    passing through when the control was last touched. `writeOnly` exists for
    exactly that case: it keeps the history current for the price of two
    stores per sample and skips the sixteen multiply-accumulates.
*/
class EarlyReflections
{
public:
    /** Crossfade between tap schedules on a Size change. */
    static constexpr double crossfadeMs = 30.0;

    // -----------------------------------------------------------------------
    //  Setup - allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------
    void prepare (double sampleRate, const er::Pattern& p = er::defaultPattern)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        pattern = p;
        pattern.events = std::clamp (pattern.events, 1, er::maxEvents);

        // Sized for the longest span any pattern may ask for, not for the
        // current one, so a Size change never needs to allocate.
        const auto capacity = static_cast<size_t> (
            std::ceil (er::maxSpanMs * currentSampleRate / 1000.0)) + 4;

        size_t rounded = 1;

        while (rounded < capacity)
            rounded <<= 1;

        left .assign (rounded, 0.0);
        right.assign (rounded, 0.0);
        mask = rounded - 1;
        writeIndex = 0;

        fadeLength = std::max (1, static_cast<int> (
            std::lround (crossfadeMs * currentSampleRate / 1000.0)));

        active .resize (static_cast<size_t> (pattern.events));
        pending.resize (static_cast<size_t> (pattern.events));

        snapToSizeRatio (1.0);
    }

    void reset() noexcept
    {
        std::fill (left .begin(), left .end(), 0.0);
        std::fill (right.begin(), right.end(), 0.0);

        writeIndex   = 0;
        fading       = false;
        fadePosition = 0;
        pendingSpan  = currentSpan;
        pending      = active;
    }

    // -----------------------------------------------------------------------
    //  Parameters - real-time safe.
    // -----------------------------------------------------------------------
    /** `ratio` is sizeScaleFor(size) / sizeScaleFor(default size). */
    void setSizeScaleRatio (double ratio) noexcept
    {
        const int target = spanSamplesFor (ratio);

        if (target == pendingSpan && (fading || target == currentSpan))
            return;

        pendingSpan = target;

        if (! fading && target != currentSpan)
            beginFade (target);
    }

    /** Snaps to a span with no crossfade. For prepare and for reset. */
    void snapToSizeRatio (double ratio) noexcept
    {
        currentSpan = spanSamplesFor (ratio);
        pendingSpan = currentSpan;

        buildSchedule (currentSpan, active);
        pending = active;

        fading       = false;
        fadePosition = 0;
    }

    // -----------------------------------------------------------------------
    //  Process - real-time safe.
    // -----------------------------------------------------------------------
    /** Keeps the history current without producing output. See the class note. */
    void writeOnly (const double* inL, const double* inR, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            writeIndex = (writeIndex + 1) & mask;
            left [writeIndex] = inL[i];
            right[writeIndex] = inR[i];
        }

        // The fade is a property of the taps, not of the output, so it must
        // keep running while the layer is silent. Otherwise a Size change made
        // at Early/Late 0 would still be half-faded when the layer came back.
        advanceFade (numSamples);
    }

    /** Input may alias nothing here: `out` must not be `in`. */
    void process (const double* inL, const double* inR,
                  double* outL, double* outR, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            writeIndex = (writeIndex + 1) & mask;
            left [writeIndex] = inL[i];
            right[writeIndex] = inR[i];

            if (! fading)
            {
                outL[i] = convolveL (active);
                outR[i] = convolveR (active);
                continue;
            }

            const double g = static_cast<double> (fadePosition)
                             / static_cast<double> (fadeLength);

            const double fromL = convolveL (active),  toL = convolveL (pending);
            const double fromR = convolveR (active),  toR = convolveR (pending);

            outL[i] = fromL + g * (toL - fromL);
            outR[i] = fromR + g * (toR - fromR);

            stepFade();
        }
    }

    // -----------------------------------------------------------------------
    //  Introspection, for the suites and the report.
    // -----------------------------------------------------------------------
    [[nodiscard]] int    getEventCount()   const noexcept { return pattern.events; }
    [[nodiscard]] int    getSpanSamples()  const noexcept { return currentSpan; }
    [[nodiscard]] double getSpanMs()       const noexcept
        { return currentSpan * 1000.0 / currentSampleRate; }
    [[nodiscard]] bool   isFading()        const noexcept { return fading; }
    [[nodiscard]] int    getFadeLengthSamples() const noexcept { return fadeLength; }
    [[nodiscard]] const er::Pattern& getPattern() const noexcept { return pattern; }

    [[nodiscard]] int    getOffsetL (int k) const noexcept { return active[idx (k)].offsetL; }
    [[nodiscard]] int    getOffsetR (int k) const noexcept { return active[idx (k)].offsetR; }
    [[nodiscard]] double getWeightL (int k) const noexcept { return active[idx (k)].weightL; }
    [[nodiscard]] double getWeightR (int k) const noexcept { return active[idx (k)].weightR; }

private:
    struct Event
    {
        int    offsetL = 0, offsetR = 0;
        double weightL = 0.0, weightR = 0.0;
    };

    [[nodiscard]] static size_t idx (int k) noexcept { return static_cast<size_t> (k); }

    [[nodiscard]] int spanSamplesFor (double ratio) const noexcept
    {
        const double ms = er::spanFor (pattern, ratio);

        return std::max (pattern.events, static_cast<int> (
            std::lround (ms * currentSampleRate / 1000.0)));
    }

    void beginFade (int targetSpan) noexcept
    {
        fadeTargetSpan = targetSpan;
        buildSchedule (targetSpan, pending);
        fading = true;
        fadePosition = 0;
    }

    void stepFade() noexcept
    {
        if (++fadePosition < fadeLength)
            return;

        currentSpan  = fadeTargetSpan;
        active       = pending;
        fading       = false;
        fadePosition = 0;

        if (pendingSpan != currentSpan)
            beginFade (pendingSpan);
    }

    void advanceFade (int numSamples) noexcept
    {
        for (int i = 0; i < numSamples && fading; ++i)
            stepFade();
    }

    [[nodiscard]] double convolveL (const std::vector<Event>& taps) const noexcept
    {
        double sum = 0.0;

        for (const auto& t : taps)
            sum += t.weightL * left[(writeIndex - static_cast<size_t> (t.offsetL)) & mask];

        return sum;
    }

    [[nodiscard]] double convolveR (const std::vector<Event>& taps) const noexcept
    {
        double sum = 0.0;

        for (const auto& t : taps)
            sum += t.weightR * right[(writeIndex - static_cast<size_t> (t.offsetR)) & mask];

        return sum;
    }

    /**
        Builds a schedule for a span.

        Not real-time hostile: fixed-size vectors, no allocation, and it runs
        once per Size change rather than per sample. Everything in it is
        O(events).
    */
    void buildSchedule (int spanSamples, std::vector<Event>& out) const noexcept
    {
        const int    n      = pattern.events;
        const double span   = static_cast<double> (spanSamples);
        const double spanMs = span * 1000.0 / currentSampleRate;
        const double floorF = std::clamp (pattern.earliestFraction, 0.0, 0.5);
        const double fill   = std::clamp (pattern.cellOccupancy, 0.05, 1.0);
        const double guard  = 0.5 * (1.0 - fill);

        // xorshift, fixed seed: deterministic, reproducible, and the same on
        // every build and every sample rate. Four draws per event - a
        // position and a sign for each channel - taken in a fixed order, so
        // the stream is a property of the pattern and not of the call site.
        unsigned int state = 1664525u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        auto nextSign = [&next01] { return next01() < 0.5 ? -1.0 : 1.0; };

        double sumSqL = 0.0, sumSqR = 0.0;

        for (int k = 0; k < n; ++k)
        {
            const double cellLow  = floorF + (1.0 - floorF) * k       / n;
            const double cellHigh = floorF + (1.0 - floorF) * (k + 1) / n;
            const double width    = cellHigh - cellLow;

            // One event per cell, placed inside the middle `fill` of it. See
            // EarlyReflectionTuning.h for why this is a draw rather than the
            // golden-ratio sequence PHASE 9 uses: in equal cells that
            // sequence can only produce two distinct arrival gaps, which is
            // a comb with a rhythm in it.
            const double fl = guard + fill * next01();
            const double fr = guard + fill * next01();

            // Never 0: an ER tap at zero offset would be the direct wet
            // signal re-added at a different gain, which is a level change
            // wearing the name of a reflection.
            const int dl = std::max (1, static_cast<int> (
                std::lround ((cellLow + fl * width) * span)));

            const int dr = std::max (1, static_cast<int> (
                std::lround ((cellLow + fr * width) * span)));

            const double gl = er::envelopeAt (pattern, dl * 1000.0 / currentSampleRate, spanMs);
            const double gr = er::envelopeAt (pattern, dr * 1000.0 / currentSampleRate, spanMs);

            const double wl = nextSign() * gl;
            const double wr = nextSign() * gr;

            out[idx (k)] = { dl, dr, wl, wr };

            sumSqL += wl * wl;
            sumSqR += wr * wr;
        }

        // L2 per channel: sum of squares is exactly 1, so a span change moves
        // WHEN the energy lands and not HOW MUCH of it does. The taps are far
        // enough apart to add incoherently, so this holds for the rendered
        // layer and not only for the coefficient list.
        const double normL = sumSqL > 0.0 ? 1.0 / std::sqrt (sumSqL) : 1.0;
        const double normR = sumSqR > 0.0 ? 1.0 / std::sqrt (sumSqR) : 1.0;

        for (auto& e : out)
        {
            e.weightL *= normL;
            e.weightR *= normR;
        }
    }

    std::vector<double> left, right;
    std::vector<Event>  active, pending;

    er::Pattern pattern {};

    size_t mask = 0;
    size_t writeIndex = 0;

    double currentSampleRate = 48000.0;

    int currentSpan = 0;
    int pendingSpan = 0;
    int fadeTargetSpan = 0;
    int fadeLength = 1;
    int fadePosition = 0;
    bool fading = false;
};

// ===========================================================================

/**
    PHASE 10. The Early/Late control, as a pair of branch gains.

    BAL-B. The law is in EarlyReflectionTuning.h; what lives here is the part
    that has to be real-time safe: the two gains are smoothed, the balance is
    not.

    SMOOTHING THE GAINS AND NOT THE TIMING IS THE WHOLE POINT. A balance move
    must change how loud each layer is and nothing else. Smoothing the control
    and recomputing the law per sample would cost a cos and a sin per sample
    for an identical result; re-deriving tap times from the balance would make
    a level control into a timing control, which PHASE 10's brief rules out in
    as many words.

    During a move the two gains interpolate linearly between two points on the
    constant-power circle, so the sum of squares dips marginally below it
    mid-travel. Over a 20 ms ramp that is a fraction of a decibel for a
    fraction of a ramp, and it is what every crossfade in the plugin does.

    BALANCE 0 IS AN EXPLICIT BYPASS. `isBypassed` is asked of the TARGET and
    the smoothing state, never of the current value, for the reason PHASE 8
    established on the cut filters: a smoother that has arrived still has to be
    known to have arrived. At the target, cos(0) is exactly 1 and sin(0)*K is
    exactly 0, so the test is an equality rather than a tolerance - but the
    processor does not multiply by those gains anyway. It takes a branch.
*/
class EarlyLateBalance
{
public:
    /** Matches the Level smoothers in SmoothedParameters. */
    static constexpr double rampSeconds = 0.02;

    void prepare (double sampleRate)
    {
        lateGain .prepare (sampleRate, rampSeconds);
        earlyGain.prepare (sampleRate, rampSeconds);

        // Balance 0, where the early gain is exactly zero whatever K is -
        // so the value passed here cannot matter, and the reference one is
        // passed rather than inventing a sentinel that could be mistaken for
        // a real calibration.
        snapTo (0.0, er::balanceCalibration);
    }

    /**
        `k` is the parameter-aware calibration for the current Decay and Size,
        from `er::calibrationFor`.

        IT ARRIVES AS AN ARGUMENT RATHER THAN BEING LOOKED UP HERE, so that
        this class stays a pair of smoothers and knows nothing about the
        parameter mapping. It also means a K change is handled by exactly the
        same path as a balance change - a new target for the early gain - so
        moving Decay or Size cannot produce a step that moving Early/Late
        would not.
    */
    void setBalance (double balance01, double k) noexcept
    {
        balance = std::clamp (balance01, 0.0, 1.0);

        const auto g = er::gainsForBalance (balance, k);

        lateGain .setTarget (g.late);
        earlyGain.setTarget (g.early);
    }

    void snapTo (double balance01, double k) noexcept
    {
        balance = std::clamp (balance01, 0.0, 1.0);

        const auto g = er::gainsForBalance (balance, k);

        lateGain .snapTo (g.late);
        earlyGain.snapTo (g.early);
    }

    /** True only when the layer is silent AND has finished becoming silent. */
    [[nodiscard]] bool isBypassed() const noexcept
    {
        return earlyGain.getTargetValue() == 0.0
            && ! earlyGain.isSmoothing()
            && lateGain.getTargetValue() == 1.0
            && ! lateGain.isSmoothing();
    }

    [[nodiscard]] double getBalance()   const noexcept { return balance; }
    [[nodiscard]] double nextLate()     noexcept { return lateGain .getNextValue(); }
    [[nodiscard]] double nextEarly()    noexcept { return earlyGain.getNextValue(); }
    [[nodiscard]] double currentLate()  const noexcept { return lateGain .getCurrentValue(); }
    [[nodiscard]] double currentEarly() const noexcept { return earlyGain.getCurrentValue(); }

    /*
        THERE IS DELIBERATELY NO skip(). SmoothedParameters has one because
        its ramps are read by some stages and not others, so the unread ones
        have to be advanced to stay on the same sample clock. These two do not
        need it and must not have it: when the layer is active the render loop
        consumes both every sample, and when it is bypassed both are settled
        at their endpoints by definition - that is what isBypassed() tests.

        A skip() here would be a method with no correct caller, and PHASE 8
        learned what an unused smoother API invites: the obvious thing to do
        with it is the wrong thing.
    */

private:
    LinearSmoother<double> lateGain, earlyGain;
    double balance = 0.0;
};

} // namespace av::dsp
