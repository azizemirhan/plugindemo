#pragma once

#include "DspAnalysis.h"
#include "FdnAnalysis.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/DiffusionTuning.h"
#include "DSP/EarlyReflectionTuning.h"
#include "DSP/EarlyReflections.h"
#include "DSP/FdnCore.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/ParameterMapping.h"
#include "DSP/SizeCompensation.h"
#include "DSP/StereoDiffusionNetwork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 2. The research rig.

    RESEARCH ONLY. NOTHING HERE SHIPS, AND NOTHING IN Source/ WAS CHANGED TO
    MAKE IT POSSIBLE. The five Mode candidates of the Stage 1 report are
    instantiated entirely from public calls that already exist.

    WHY A RIG AND NOT THE PRODUCTION TANK. Stage 1 found three things a Mode
    needs that the processor's own object cannot reach: the input diffuser is
    a private member of StereoFdn, the delay capacity is fixed from the
    shipping pattern at prepare, and the ER layer's channel geometry is a
    hard-coded seed. Rather than open any of those in production for a
    research question, the rig composes the same stages one level down:

        input -+-> research ER tap list ---------------------------+
               |                                                   |
               +-> AttackInjection -> StereoDiffusionNetwork       |
                   -> injection vectors -> FdnCore<8> -> readout   |
                   -> SizeLevelCompensation -> wet trim -----------+-> balance

    FdnCore, StereoDiffusionNetwork, AttackInjection and
    SizeLevelCompensation are the PRODUCTION classes, used through their
    public interfaces. The injection/readout arithmetic is the eight lines of
    StereoFdn::processSample, restated. The ER tap list is the one stage that
    is re-implemented, because the production schedule builder has no seam for
    channel geometry; it is the production algorithm with one added parameter,
    and the Hall control proves the restatement is exact.

    THE HALL CONTROL IS WHAT MAKES ANY OF THIS MEAN ANYTHING. A rig that does
    not reproduce the shipping engine bit for bit when given the shipping data
    is measuring itself. That is tested before anything else is.

    NO PROCESSING STAGE IS ADDED. Every candidate runs the same stages as
    Hall; they differ in times, counts and one selection (allpass sections per
    line, storage for which production already reserves).
*/
namespace avtest::mode12
{

inline constexpr int    N     = av::dsp::fdn::order;
inline constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<double> l, r;
};

// ===========================================================================
//  The Mode-owned data, and the user's controls
// ===========================================================================

/** Everything a Mode owns. Times, counts, one selection. No tonal data. */
struct ModeCandidate
{
    std::string name;

    std::array<double, 8> delaysMs {};

    int    diffuserStages = 4;
    double diffuserScale  = 1.0;

    int    allpassSections = 1;

    double modDepthFactor = 1.0;
    double modRateFactor  = 1.0;

    av::dsp::er::Pattern er {};

    /** 1 = the production draws, independent per channel. 0 = identical
        positions in both channels. The one piece of ER geometry production
        cannot express. */
    double erChannelSpread = 1.0;

    /** 0 hadamard (shipping), 1 householder, 2 identity. NOT part of any
        Stage 1 candidate: every candidate leaves it at 0. It exists for the
        exploratory leverage probe and for the positive control, both added
        after the pre-registered results were in and labelled as such. */
    int matrixType = 0;
};

/** The user's controls, at the product defaults. Damping is here because the
    engine needs it; it is never Mode data and never searched. */
struct User
{
    double size      = 0.60;
    double decay     = 2.5;
    double earlyLate = 0.0;
    double earlyDiff = 0.70;
    double attack    = 0.0;
    double modDepth  = 0.30;
    double modRate   = 0.5;

    double lowDamp   = 0.20;
    double highDamp  = 0.45;
    double lowDampHz  = 200.0;
    double highDampHz = 10000.0;
};

/** Which wet trim the rig applies. */
enum class Trim
{
    /** The shipping function, derived from the shipping delay set, whatever
        the Mode. This is "one shared calibration". */
    shared,

    /** The same analytic formula evaluated on the Mode's own delay set, with
        the shipping reference in the numerator. */
    own
};

// ===========================================================================
//  The wet trim, generalised to a delay set
// ===========================================================================

/** mapping::tankPowerGain, with the delay set as an argument. Identical
    arithmetic in identical order, so for the shipping set it returns the
    shipping value exactly. */
[[nodiscard]] inline double tankPowerGainFor (const double* delaysMs,
                                              double rt60Seconds, double sizeScale) noexcept
{
    using namespace av::dsp;

    const double rt60  = mapping::rt60For (rt60Seconds);
    const double scale = std::clamp (sizeScale, fdn::minSizeScale, fdn::maxSizeScale);

    double total = 0.0;

    for (int i = 0; i < fdn::order; ++i)
    {
        const double delaySeconds = delaysMs[i] * scale * 0.001;
        const double g = std::min (fdn::maxFeedbackGain,
                                   fdn::feedbackGainForRt60 (delaySeconds, rt60));

        total += 1.0 / std::max (1.0e-6, 1.0 - g * g);
    }

    return total;
}

[[nodiscard]] inline double ownWetTrim (const double* delaysMs,
                                        double rt60Seconds, double sizeScale) noexcept
{
    using namespace av::dsp;

    const double reference = mapping::tankPowerGain (mapping::previewReferenceRt60,
                                                     mapping::previewReferenceSize);
    const double current   = tankPowerGainFor (delaysMs, rt60Seconds, sizeScale);

    if (current <= 0.0 || reference <= 0.0)
        return mapping::previewBaseTrim;

    return mapping::previewBaseTrim * std::sqrt (reference / current);
}

// ===========================================================================
//  The research ER tap list
// ===========================================================================
/**
    EarlyReflections::buildSchedule, with the channel spread exposed.

    The draw order, the cell arithmetic, the envelope and the L2 normalisation
    are the production ones, statement for statement. `spread` scales how far
    the right channel's position departs from the left's inside its cell. At 1
    the right position is the production draw itself - selected, not
    recomputed, because `fl + 1.0 * (fr - fl)` is not `fr` in floating point.
*/
class ResearchEr
{
public:
    struct Tap { int offsetL = 1, offsetR = 1; double weightL = 0.0, weightR = 0.0; };

    void prepare (double sampleRate, const av::dsp::er::Pattern& p,
                  double channelSpread, double sizeScaleRatio)
    {
        using namespace av::dsp;

        rate = sampleRate;
        pattern = p;
        pattern.events = std::clamp (pattern.events, 1, er::maxEvents);

        const auto capacity = static_cast<size_t> (
            std::ceil (er::maxSpanMs * rate / 1000.0)) + 4;

        size_t rounded = 1;

        while (rounded < capacity)
            rounded <<= 1;

        left .assign (rounded, 0.0);
        right.assign (rounded, 0.0);
        mask = rounded - 1;
        writeIndex = 0;

        const double ms = er::spanFor (pattern, sizeScaleRatio);

        span = std::max (pattern.events, static_cast<int> (
            std::lround (ms * rate / 1000.0)));

        build (channelSpread);
    }

    void process (double inL, double inR, double& outL, double& outR) noexcept
    {
        writeIndex = (writeIndex + 1) & mask;
        left [writeIndex] = inL;
        right[writeIndex] = inR;

        double sumL = 0.0;

        for (const auto& t : taps)
            sumL += t.weightL * left[(writeIndex - static_cast<size_t> (t.offsetL)) & mask];

        double sumR = 0.0;

        for (const auto& t : taps)
            sumR += t.weightR * right[(writeIndex - static_cast<size_t> (t.offsetR)) & mask];

        outL = sumL;
        outR = sumR;
    }

    [[nodiscard]] const std::vector<Tap>& getTaps() const noexcept { return taps; }
    [[nodiscard]] int getSpanSamples() const noexcept { return span; }

private:
    void build (double channelSpread)
    {
        using namespace av::dsp;

        const int    n      = pattern.events;
        const double spanD  = static_cast<double> (span);
        const double spanMs = spanD * 1000.0 / rate;
        const double floorF = std::clamp (pattern.earliestFraction, 0.0, 0.5);
        const double fill   = std::clamp (pattern.cellOccupancy, 0.05, 1.0);
        const double guard  = 0.5 * (1.0 - fill);

        unsigned int state = 1664525u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        auto nextSign = [&next01] { return next01() < 0.5 ? -1.0 : 1.0; };

        taps.assign (static_cast<size_t> (n), Tap {});

        double sumSqL = 0.0, sumSqR = 0.0;

        for (int k = 0; k < n; ++k)
        {
            const double cellLow  = floorF + (1.0 - floorF) * k       / n;
            const double cellHigh = floorF + (1.0 - floorF) * (k + 1) / n;
            const double width    = cellHigh - cellLow;

            const double fl     = guard + fill * next01();
            const double frDraw = guard + fill * next01();

            const double fr = channelSpread >= 1.0
                                ? frDraw
                                : fl + std::max (0.0, channelSpread) * (frDraw - fl);

            const int dl = std::max (1, static_cast<int> (
                std::lround ((cellLow + fl * width) * spanD)));

            const int dr = std::max (1, static_cast<int> (
                std::lround ((cellLow + fr * width) * spanD)));

            const double gl = er::envelopeAt (pattern, dl * 1000.0 / rate, spanMs);
            const double gr = er::envelopeAt (pattern, dr * 1000.0 / rate, spanMs);

            const double wl = nextSign() * gl;
            const double wr = nextSign() * gr;

            taps[static_cast<size_t> (k)] = { dl, dr, wl, wr };

            sumSqL += wl * wl;
            sumSqR += wr * wr;
        }

        const double normL = sumSqL > 0.0 ? 1.0 / std::sqrt (sumSqL) : 1.0;
        const double normR = sumSqR > 0.0 ? 1.0 / std::sqrt (sumSqR) : 1.0;

        for (auto& e : taps)
        {
            e.weightL *= normL;
            e.weightR *= normR;
        }
    }

    av::dsp::er::Pattern pattern {};
    std::vector<Tap> taps;
    std::vector<double> left, right;
    size_t mask = 0, writeIndex = 0;
    double rate = 48000.0;
    int span = 1;
};

// ===========================================================================
//  The rig
// ===========================================================================

class Rig
{
public:
    Rig (const ModeCandidate& m, const User& u, Trim trimMode = Trim::own,
         double sampleRate = kRate)
    {
        using namespace av::dsp;

        const double scale = mapping::sizeScaleFor (u.size);
        const double rt60  = mapping::rt60For (u.decay);

        // --- tank ----------------------------------------------------------
        const double longest = *std::max_element (m.delaysMs.begin(), m.delaysMs.end());

        // Recorded before prepare, so prepare applies THIS set rather than the
        // core's default one, which a short Mode's capacity could not hold.
        configured = succeeded (core.setDelaysMilliseconds (m.delaysMs.data(), scale));

        capacityMs = longest * fdn::maxSizeScale;

        configured = configured
                  && succeeded (core.prepare (sampleRate, capacityMs, 0.02));

        configured = configured
                  && succeeded (core.setDelaysMilliseconds (m.delaysMs.data(), scale));

        core.setRt60Seconds (rt60, true);

        // Cleared HERE, between the decay and the damping, because that is
        // where MorphingStereoFdn::beginMorph clears the bank it is about to
        // bring in. The order is part of what the Hall control compares.
        core.reset();
        core.resetModulation();

        core.setDamping (mapping::lowDampRatioFor  (u.lowDamp),
                         mapping::highDampRatioFor (u.highDamp),
                         u.lowDampHz, u.highDampHz, true);

        const double depth = mapping::modDepthFor (u.modDepth) * m.modDepthFactor;
        const double rate  = mapping::modRateScaleFor (u.modRate) * m.modRateFactor;

        architecture::applyWithDepth (core, architecture::defaultCandidate, depth, rate, true);

        if (m.allpassSections != allpassMod::defaultSectionsPerLine)
            core.setAllpassSectionsPerLine (m.allpassSections);

        if (m.matrixType == 1)
            core.setMatrixType (FdnCore<N>::Matrix::Type::householder);
        else if (m.matrixType == 2)
            core.setMatrixType (FdnCore<N>::Matrix::Type::identity);

        // --- diffuser: the sequence StereoFdn::prepare runs -----------------
        diffuser.setBasePattern (diffusion::fdnInputPattern);
        diffuser.prepare (sampleRate, 0.02);
        diffuser.setStageCount (m.diffuserStages, true);
        diffuser.setDiffusion (0.7, true);
        diffuser.setDiffusion (u.earlyDiff, true);

        if (m.diffuserScale != 1.0)
            configured = configured
                      && succeeded (diffuser.setDelayScale (m.diffuserScale, true));

        diffuser.reset();

        // --- vectors: StereoFdn::loadDefaultTuning, all Modes ---------------
        for (int i = 0; i < N; ++i)
        {
            injectionL[i] = fdn::injectionLeft [i] * fdn::signVectorNorm;
            injectionR[i] = fdn::injectionRight[i] * fdn::signVectorNorm;
            readoutL  [i] = fdn::readoutLeft   [i] * fdn::signVectorNorm;
            readoutR  [i] = fdn::readoutRight  [i] * fdn::signVectorNorm;
        }

        // --- level ----------------------------------------------------------
        compensation.prepare (sampleRate);
        compensation.setEnabled (true, true);
        compensation.setSize (scale, true);
        compensation.setRt60Seconds (rt60, true);
        compensation.setAllpassEngaged (
            architecture::settingsFor (architecture::defaultCandidate).allpassDepth * depth > 0.0,
            true);

        trim = trimMode == Trim::shared ? mapping::previewWetTrim (rt60, scale)
                                        : ownWetTrim (m.delaysMs.data(), rt60, scale);

        // --- attack ---------------------------------------------------------
        attack.prepare (sampleRate);
        attack.snapTo (u.attack);

        // --- early layer ----------------------------------------------------
        const double ratio = scale / mapping::sizeScaleFor (er::referenceSizeControl);

        early.prepare (sampleRate, m.er, m.erChannelSpread, ratio);

        const auto gains = er::gainsForBalance (u.earlyLate, er::calibrationFor (rt60, scale));

        lateGain    = gains.late;
        earlyGain   = gains.early;
        earlyActive = u.earlyLate > 0.0;
    }

    [[nodiscard]] bool isConfigured() const noexcept { return configured; }
    [[nodiscard]] double getCapacityMs() const noexcept { return capacityMs; }
    [[nodiscard]] double getTrim() const noexcept { return trim; }

    /** Overrides the early gain. For the calibration study, where K is the
        thing being measured rather than assumed. */
    void setEarlyGain (double g) noexcept { earlyGain = g; }
    void setLateGain  (double g) noexcept { lateGain = g; }
    void setEarlyActive (bool a) noexcept { earlyActive = a; }

    av::dsp::FdnCore<N>& getCore() noexcept { return core; }

    void processSample (double& l, double& r) noexcept
    {
        double erL = 0.0, erR = 0.0;

        // Fed unconditionally, as production's writeOnly does.
        early.process (l, r, erL, erR);

        attack.process (&l, &r, 1);
        diffuser.processSample (l, r);

        double injection[N];
        double state[N];

        for (int i = 0; i < N; ++i)
            injection[i] = injectionL[i] * l + injectionR[i] * r;

        core.processSample (injection, state);

        double outL = 0.0, outR = 0.0;

        for (int i = 0; i < N; ++i)
        {
            outL += readoutL[i] * state[i];
            outR += readoutR[i] * state[i];
        }

        l = outL;
        r = outR;

        compensation.process (l, r);

        l *= trim;
        r *= trim;

        if (earlyActive)
        {
            l = lateGain * l + earlyGain * erL;
            r = lateGain * r + earlyGain * erR;
        }
    }

    /** `silence` samples of nothing first, so a rig can be put on the same
        modulation phase as a reference that had to settle. */
    [[nodiscard]] Stereo render (const std::vector<double>& driveL,
                                 const std::vector<double>& driveR,
                                 size_t length, size_t silence = 0)
    {
        av::dsp::ScopedFtz ftz;

        for (size_t n = 0; n < silence; ++n)
        {
            double l = 0.0, r = 0.0;
            processSample (l, r);
        }

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = n < driveL.size() ? driveL[n] : 0.0;
            double r = n < driveR.size() ? driveR[n] : 0.0;

            processSample (l, r);

            out.l[n] = l;
            out.r[n] = r;
        }

        return out;
    }

    [[nodiscard]] Stereo render (const std::vector<double>& drive, size_t length,
                                 size_t silence = 0)
    {
        return render (drive, drive, length, silence);
    }

private:
    av::dsp::FdnCore<N> core;
    av::dsp::StereoDiffusionNetwork<double> diffuser;
    av::dsp::AttackInjection attack;
    av::dsp::SizeLevelCompensation compensation;
    ResearchEr early;

    double injectionL[N] {}, injectionR[N] {}, readoutL[N] {}, readoutR[N] {};

    double trim = 1.0, lateGain = 1.0, earlyGain = 0.0, capacityMs = 0.0;
    bool   earlyActive = false, configured = false;
};

/** The wet impulse response of a Mode at a setting. */
[[nodiscard]] inline Stereo impulseResponse (const ModeCandidate& m, const User& u,
                                             double seconds, Trim trim = Trim::own)
{
    auto rig = std::make_unique<Rig> (m, u, trim);

    std::vector<double> drive (1, 1.0);

    return rig->render (drive, static_cast<size_t> (seconds * kRate));
}

// ===========================================================================
//  Features
// ===========================================================================
/*
    Every feature is a RATIO, a TIME or a CORRELATION, so every one is
    invariant to level. Loudness cannot enter the distance; that is tested.
*/

struct Features
{
    // A - buildup and timing, ms
    double t10 = 0.0, t50 = 0.0, tPeak = 0.0, earlyCentroid = 0.0, earlySpread = 0.0;

    // B - energy profile, dB
    double profile[6] {};
    double c50 = 0.0, c80 = 0.0;

    // C - echo density
    double tNed50 = 0.0, tNed90 = 0.0, nedAt10 = 0.0, nedAt40 = 0.0;

    // D - stereo
    double corr[3] {};
    double sideMid[3] {};

    // E - decay
    double rt60 = 0.0, centreTime = 0.0;

    // F - recurrence
    double recurrence = 0.0;
};

inline constexpr double kProfileEdgesMs[7] { 0.0, 10.0, 20.0, 40.0, 80.0, 160.0, 320.0 };
inline constexpr double kStereoEdgesMs[4]  { 0.0, 50.0, 200.0, 600.0 };

[[nodiscard]] inline size_t msToSamples (double ms) noexcept
{
    return static_cast<size_t> (std::lround (ms * 0.001 * kRate));
}

/** RT60 from the Schroeder curve of an energy sequence, -5 to -20 dB. Falls
    back to whatever span the response actually covers. */
[[nodiscard]] inline double rt60FromEnergy (const std::vector<double>& e)
{
    const size_t n = e.size();

    if (n < 16)
        return 0.0;

    std::vector<double> edc (n);
    double running = 0.0;

    for (size_t i = n; i-- > 0;)
    {
        running += e[i];
        edc[i] = running;
    }

    const double total = edc[0];

    if (total <= 0.0)
        return 0.0;

    auto timeAt = [&] (double db)
    {
        const double level = total * std::pow (10.0, db / 10.0);

        for (size_t i = 0; i < n; ++i)
            if (edc[i] <= level)
                return static_cast<double> (i) / kRate;

        return -1.0;
    };

    const double t5 = timeAt (-5.0);

    if (t5 < 0.0)
        return 0.0;

    for (double lower : { -20.0, -15.0, -10.0 })
    {
        const double t = timeAt (lower);

        // Not trusted inside the last fifth of the render, where the backward
        // integral has run out of tail to integrate.
        if (t > t5 && t < 0.8 * static_cast<double> (n) / kRate)
            return 60.0 / (-5.0 - lower) * (t - t5);
    }

    return 0.0;
}

/** Peak autocorrelation of the detrended 1 ms energy envelope, lags 3-120 ms,
    over the first 600 ms. 0 = no preferred interval, 1 = a perfect repeat. */
[[nodiscard]] inline double recurrenceIndex (const std::vector<double>& e)
{
    const size_t binSamples = msToSamples (1.0);
    const size_t bins = std::min<size_t> (600, e.size() / binSamples);

    if (bins < 250)
        return 0.0;

    std::vector<double> env (bins, 0.0);

    for (size_t b = 0; b < bins; ++b)
        for (size_t i = 0; i < binSamples; ++i)
            env[b] += e[b * binSamples + i];

    // Detrend by a 40 ms moving average, in the log domain, so the decay
    // itself is not read as structure.
    std::vector<double> x (bins, 0.0);

    for (size_t b = 0; b < bins; ++b)
    {
        const size_t lo = b >= 20 ? b - 20 : 0;
        const size_t hi = std::min (bins - 1, b + 20);

        double mean = 0.0;

        for (size_t k = lo; k <= hi; ++k)
            mean += env[k];

        mean /= static_cast<double> (hi - lo + 1);

        x[b] = mean > 0.0 ? env[b] / mean - 1.0 : 0.0;
    }

    double zero = 0.0;

    for (double v : x)
        zero += v * v;

    if (zero <= 0.0)
        return 0.0;

    double best = 0.0;

    for (size_t lag = 3; lag <= 120; ++lag)
    {
        double sum = 0.0;

        for (size_t b = 0; b + lag < bins; ++b)
            sum += x[b] * x[b + lag];

        best = std::max (best, sum / zero);
    }

    return best;
}

[[nodiscard]] inline Features analyse (const Stereo& ir)
{
    Features f;

    const size_t n = ir.l.size();

    std::vector<double> e (n);
    double total = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        e[i] = ir.l[i] * ir.l[i] + ir.r[i] * ir.r[i];
        total += e[i];
    }

    if (total <= 0.0)
        return f;

    auto energyBetween = [&] (double fromMs, double toMs)
    {
        const size_t a = std::min (n, msToSamples (fromMs));
        const size_t b = std::min (n, msToSamples (toMs));

        double sum = 0.0;

        for (size_t i = a; i < b; ++i)
            sum += e[i];

        return sum;
    };

    auto db = [] (double ratio) { return 10.0 * std::log10 (std::max (1.0e-12, ratio)); };

    // --- A ------------------------------------------------------------------
    {
        double running = 0.0, moment = 0.0;
        bool got10 = false, got50 = false;

        for (size_t i = 0; i < n; ++i)
        {
            running += e[i];
            moment  += e[i] * static_cast<double> (i);

            if (! got10 && running >= 0.10 * total) { f.t10 = i * 1000.0 / kRate; got10 = true; }
            if (! got50 && running >= 0.50 * total) { f.t50 = i * 1000.0 / kRate; got50 = true; }
        }

        f.centreTime = moment / total * 1000.0 / kRate;

        const size_t window = msToSamples (10.0);
        const size_t limit  = std::min (n, msToSamples (1000.0));

        double sum = 0.0, best = -1.0;

        for (size_t i = 0; i < limit; ++i)
        {
            sum += e[i];

            if (i >= window)
                sum -= e[i - window];

            if (sum > best)
            {
                best = sum;
                f.tPeak = (static_cast<double> (i) - 0.5 * static_cast<double> (window))
                          * 1000.0 / kRate;
            }
        }

        f.tPeak = std::max (0.0, f.tPeak);

        const size_t early = std::min (n, msToSamples (100.0));

        double e0 = 0.0, e1 = 0.0, e2 = 0.0;

        for (size_t i = 0; i < early; ++i)
        {
            const double t = i * 1000.0 / kRate;

            e0 += e[i];
            e1 += e[i] * t;
            e2 += e[i] * t * t;
        }

        if (e0 > 0.0)
        {
            f.earlyCentroid = e1 / e0;
            f.earlySpread   = std::sqrt (std::max (0.0, e2 / e0 - f.earlyCentroid * f.earlyCentroid));
        }
    }

    // --- B ------------------------------------------------------------------
    {
        const double first = energyBetween (0.0, 320.0);

        for (int k = 0; k < 6; ++k)
            f.profile[k] = std::max (-40.0, db (energyBetween (kProfileEdgesMs[k],
                                                               kProfileEdgesMs[k + 1])
                                                / std::max (1.0e-300, first)));

        const double e50 = energyBetween (0.0, 50.0);
        const double e80 = energyBetween (0.0, 80.0);

        f.c50 = std::clamp (db (e50 / std::max (1.0e-300, total - e50)), -40.0, 40.0);
        f.c80 = std::clamp (db (e80 / std::max (1.0e-300, total - e80)), -40.0, 40.0);
    }

    // --- C ------------------------------------------------------------------
    {
        const size_t window = msToSamples (20.0);
        const size_t step   = msToSamples (2.0);
        const size_t limit  = std::min (n > window ? n - window : 0, msToSamples (400.0));

        f.tNed50 = f.tNed90 = 400.0;

        bool got50 = false, got90 = false;

        for (size_t start = 0; start < limit; start += step)
        {
            const double d = normalisedEchoDensity (ir.l.data(), n, start, window);
            const double t = start * 1000.0 / kRate;

            if (! got50 && d >= 0.5) { f.tNed50 = t; got50 = true; }
            if (! got90 && d >= 0.9) { f.tNed90 = t; got90 = true; }

            if (start == msToSamples (10.0)) f.nedAt10 = d;
            if (start == msToSamples (40.0)) f.nedAt40 = d;

            if (got90 && t > 40.0)
                break;
        }
    }

    // --- D ------------------------------------------------------------------
    for (int k = 0; k < 3; ++k)
    {
        const size_t a = std::min (n, msToSamples (kStereoEdgesMs[k]));
        const size_t b = std::min (n, msToSamples (kStereoEdgesMs[k + 1]));

        double ll = 0.0, rr = 0.0, lr = 0.0;

        for (size_t i = a; i < b; ++i)
        {
            ll += ir.l[i] * ir.l[i];
            rr += ir.r[i] * ir.r[i];
            lr += ir.l[i] * ir.r[i];
        }

        f.corr[k] = (ll > 0.0 && rr > 0.0) ? lr / std::sqrt (ll * rr) : 0.0;

        const double mid  = ll + rr + 2.0 * lr;
        const double side = ll + rr - 2.0 * lr;

        f.sideMid[k] = std::clamp (db (std::max (1.0e-300, side) / std::max (1.0e-300, mid)),
                                   -30.0, 30.0);
    }

    // --- E, F ---------------------------------------------------------------
    f.rt60       = rt60FromEnergy (e);
    f.recurrence = recurrenceIndex (e);

    return f;
}

// ===========================================================================
//  Distance
// ===========================================================================
/*
    PRE-REGISTERED. These scales and the thresholds below were committed
    before any candidate was scored - see the Stage 2 report for the commit.

    Each feature difference is divided by a scale: a published
    just-noticeable difference where one exists, and a stated proxy where
    none does. A family's distance is the RMS of its features in those units;
    the total is the RMS over the six families, so a family with many
    features cannot outvote one with few.

        ISO 3382-1 JNDs: reverberation time 5 %, clarity 1 dB, centre time
        10 ms, interaural cross-correlation 0.075.

    Everything else is a PROXY and is labelled as one. The proxies do not
    need to be right in absolute terms, because the thresholds are anchored
    to a measured same-family floor rather than to the number 1.
*/

inline constexpr int kFamilies = 6;

inline constexpr const char* kFamilyNames[kFamilies]
{
    "buildup", "energy profile", "echo density", "stereo", "decay", "recurrence"
};

struct Distance
{
    double family[kFamilies] {};
    double total = 0.0;
};

[[nodiscard]] inline Distance distance (const Features& a, const Features& b)
{
    auto rel = [] (double x, double y, double floorUnit, double fraction)
    {
        return (x - y) / std::max (floorUnit, fraction * std::abs (x));
    };

    auto rms = [] (std::initializer_list<double> v)
    {
        double sum = 0.0;

        for (double x : v)
            sum += x * x;

        return std::sqrt (sum / static_cast<double> (v.size()));
    };

    Distance d;

    // A - timing. Proxies: 10-15 % with an absolute floor.
    d.family[0] = rms ({ rel (a.t10, b.t10, 2.0, 0.10),
                         rel (a.t50, b.t50, 5.0, 0.10),
                         rel (a.tPeak, b.tPeak, 5.0, 0.15),
                         rel (a.earlyCentroid, b.earlyCentroid, 3.0, 0.10),
                         rel (a.earlySpread, b.earlySpread, 3.0, 0.10) });

    // B - clarity at its 1 dB JND; window energies at a 1.5 dB proxy.
    d.family[1] = rms ({ (a.profile[0] - b.profile[0]) / 1.5,
                         (a.profile[1] - b.profile[1]) / 1.5,
                         (a.profile[2] - b.profile[2]) / 1.5,
                         (a.profile[3] - b.profile[3]) / 1.5,
                         (a.profile[4] - b.profile[4]) / 1.5,
                         (a.profile[5] - b.profile[5]) / 1.5,
                         (a.c50 - b.c50) / 1.0,
                         (a.c80 - b.c80) / 1.0 });

    // C - proxies throughout.
    d.family[2] = rms ({ rel (a.tNed50, b.tNed50, 5.0, 0.20),
                         rel (a.tNed90, b.tNed90, 5.0, 0.20),
                         (a.nedAt10 - b.nedAt10) / 0.10,
                         (a.nedAt40 - b.nedAt40) / 0.10 });

    // D - correlation near its 0.075 JND, widened because a windowed
    // estimate from one impulse response is noisier than a listener's.
    d.family[3] = rms ({ (a.corr[0] - b.corr[0]) / 0.15,
                         (a.corr[1] - b.corr[1]) / 0.10,
                         (a.corr[2] - b.corr[2]) / 0.10,
                         (a.sideMid[0] - b.sideMid[0]) / 1.5,
                         (a.sideMid[1] - b.sideMid[1]) / 1.5,
                         (a.sideMid[2] - b.sideMid[2]) / 1.5 });

    // E - both at their JNDs.
    d.family[4] = rms ({ rel (a.rt60, b.rt60, 0.02, 0.05),
                         rel (a.centreTime, b.centreTime, 10.0, 0.10) });

    // F - proxy.
    d.family[5] = std::abs (a.recurrence - b.recurrence) / 0.10;

    double sum = 0.0;

    for (double v : d.family)
        sum += v * v;

    d.total = std::sqrt (sum / kFamilies);

    return d;
}

/*
    THE THRESHOLDS, as functions of the measured same-family floor.

    The floor is the distance between the shipping Hall and things that are
    by construction the SAME family: Hall with Size moved by a hair, and Hall
    on a second delay set searched to the same sum and spread. Whatever that
    distance is, it is what "the same reverb, a different realisation" looks
    like through these features.

        REPRODUCED   residual <= max (1.5, 1.25 x floor)
        DISTINCT     residual >= max (3.0, 2.00 x floor)
        WEAK         in between

    A Mode whose best impostor lands in REPRODUCED at ANY target is a preset
    of the Mode that reproduced it.
*/
[[nodiscard]] inline double reproducedThreshold (double floorDistance) noexcept
{
    return std::max (1.5, 1.25 * floorDistance);
}

[[nodiscard]] inline double distinctThreshold (double floorDistance) noexcept
{
    return std::max (3.0, 2.0 * floorDistance);
}

/** Families whose distance alone clears the distinct threshold. */
[[nodiscard]] inline int distinctFamilies (const Distance& d, double floorDistance) noexcept
{
    int count = 0;

    for (double v : d.family)
        if (v >= distinctThreshold (floorDistance))
            ++count;

    return count;
}

// ===========================================================================
//  The five candidates
// ===========================================================================

/** The delay-set search: FdnDelaySetTests' objective, aimed at a family. */
[[nodiscard]] std::array<double, 8> searchDelaySet (double sumMs, double ratio,
                                                    unsigned int seed);

/** The five Stage 1 candidates. Hall is the shipping data, untouched. */
[[nodiscard]] const std::vector<ModeCandidate>& candidates();

/** A second delay set searched to Hall's own sum and spread. Same family by
    construction; used only to measure the floor. */
[[nodiscard]] const ModeCandidate& hallSibling();

enum ModeIndex { room = 0, hall, chamber, plate, ambient, modeCount };

} // namespace avtest::mode12
