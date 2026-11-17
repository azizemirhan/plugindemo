#pragma once

#include "Phase12ModeRig.h"
#include "Phase12ResearchCore.h"

#include "DSP/FeedbackMatrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

/**
    PHASE 12 - STAGE 3. The structural research rig.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

    Stage 2's rig could vary DATA: times, counts, one selection. It found that
    data is not enough - nearly every such dimension is one a user control
    already owns. This rig varies STRUCTURE. Each field of `Structure` below is
    one isolated lever, named after the item of the Stage 3 brief it answers:

        A  matrix              the static scattering matrix
        B  diffuserStages      input diffuser stage count
        C  diffuser            input diffuser topology
        D  diffuser            ... and its delay-ratio structure
        E  injection           how the input reaches the lines
        F  matrix = coupled    late-network connectivity
        G  loopAllpasses,      what else sits in the feedback path
           loopDispersion
        H  diffuser = velvet   feed-forward instead of feedback diffusion
        I  readout, erFeedsTank  how early structure relates to the late field
        J  preDispersion       a dispersive stage ahead of the tank

    A default-constructed Structure IS the shipping Hall, and the rig then
    runs exactly the statements Stage 2's rig runs, on ResearchCore instead of
    FdnCore. Both equalities are tested, bit for bit, before anything else.

    Levers are meant to be moved ONE AT A TIME first. Composite candidates are
    only built from levers that showed leverage on their own.
*/
namespace avtest::stage3
{

using mode12::Stereo;
using mode12::kRate;
using mode12::msToSamples;

inline constexpr int N = 8;

enum class MatrixKind
{
    hadamard,          // shipping
    householder,       // production type
    identity,          // production type; POSITIVE CONTROL: no mixing at all
    ring,              // POSITIVE CONTROL: a permutation, one long loop
    randomDense,       // seeded dense orthogonal
    hadamardPermuted,  // Hadamard with rows signed and columns permuted
    sparseChain,       // two non-zeros per row, all lines connected in a chain
    coupledBlocks      // two 4-line Hadamard blocks joined by an angle
};

enum class DiffuserKind
{
    series,     // shipping: series allpasses, shipping delay pattern
    none,
    velvet,     // sparse FIR: feed-forward only
    clustered,  // series allpasses, delays within a 1.8:1 range
    wide        // series allpasses, delays in a 2.7:1 progression
};

enum class InjectionKind
{
    all,        // shipping sign vectors into all eight lines
    pair,       // L into line 0 only, R into line 1 only
    staggered,  // shipping vectors, each line fed through its own delay
    split,      // L into lines 0-3, R into lines 4-7
    blockA      // both channels into lines 0-3
};

enum class ReadoutKind
{
    ends,       // shipping sign vectors on the eight line outputs
    multiTap,   // ... plus two taps taken inside every line
    split,      // L from lines 0-3, R from lines 4-7
    blockB      // both channels from lines 4-7
};

struct Structure
{
    std::string name = "Hall";

    std::array<double, 8> delaysMs
    {
        av::dsp::fdn::delaysMs[0], av::dsp::fdn::delaysMs[1], av::dsp::fdn::delaysMs[2],
        av::dsp::fdn::delaysMs[3], av::dsp::fdn::delaysMs[4], av::dsp::fdn::delaysMs[5],
        av::dsp::fdn::delaysMs[6], av::dsp::fdn::delaysMs[7]
    };

    // A, F
    MatrixKind   matrix     = MatrixKind::hadamard;
    double       coupling   = 0.0;      // coupledBlocks: radians. pi/4 = full mix
    unsigned int matrixSeed = 1u;

    /** coupledBlocks only. RT60 of block B as a fraction of the requested
        one. 1 = lossless coupling. Below 1 the matrix is contractive, not
        orthogonal: a controlled-loss structure, and the one way this engine
        can produce a decay that is not a single exponential. */
    double blockDecayRatio = 1.0;

    // B, C, D, H
    DiffuserKind diffuser       = DiffuserKind::series;
    int          diffuserStages = 4;
    double       diffuserScale  = 1.0;

    // E
    InjectionKind injection = InjectionKind::all;
    double        staggerMs = 0.0;

    // I
    ReadoutKind readout     = ReadoutKind::ends;
    bool        erFeedsTank = false;

    // G
    int    loopAllpasses   = 0;         // static Schroeder sections per line
    double loopAllpassGain = 0.5;
    int    loopDispersion  = 0;         // first-order sections per line
    double dispersionCoeff = -0.7;

    // J
    int    preDispersion      = 0;      // first-order sections per channel
    double preDispersionCoeff = -0.7;

    // Stage 2 data, kept so a Stage 2 candidate can be re-scored unchanged.
    int    allpassSections = 1;
    double modDepthFactor  = 1.0;
    av::dsp::er::Pattern er {};
    double erChannelSpread = 1.0;

    /** Multiplies the late path. Set by calibrate() so that a structure's
        integrated wet energy equals Hall's at the product defaults. Loudness
        is thereby removed as a way to differ; 1 for Hall, exactly. */
    double lateCorrection = 1.0;
};

/** The user's controls. Width is added to Stage 2's six, because Stage 3
    measures a lateral family and Width is the user's lateral control. */
struct User : mode12::User
{
    double width = 1.0;     // 0 .. 2, the product's 0 .. 200 %
};

// ===========================================================================
//  Matrices
// ===========================================================================

/** Writes the static matrix, row-major. Returns false when the kind is one
    production already implements and the production path should be used. */
inline bool buildMatrix (const Structure& s, double* m)
{
    for (int i = 0; i < N * N; ++i)
        m[i] = 0.0;

    auto at = [m] (int r, int c) -> double& { return m[r * N + c]; };

    switch (s.matrix)
    {
        case MatrixKind::hadamard:
        case MatrixKind::householder:
        case MatrixKind::identity:
            return false;

        case MatrixKind::ring:
            for (int r = 0; r < N; ++r)
                at (r, (r + 1) % N) = 1.0;

            return true;

        case MatrixKind::randomDense:
        {
            unsigned int state = s.matrixSeed * 2654435761u | 1u;

            auto next01 = [&state]
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;

                return (static_cast<double> (state & 0xffffffu) + 0.5) / 16777216.0;
            };

            for (int i = 0; i < N * N; ++i)
                m[i] = std::sqrt (-2.0 * std::log (next01()))
                     * std::cos (6.283185307179586 * next01());

            // Gram-Schmidt on the rows, twice for orthogonality to rounding.
            for (int pass = 0; pass < 2; ++pass)
                for (int r = 0; r < N; ++r)
                {
                    for (int p = 0; p < r; ++p)
                    {
                        double dot = 0.0;

                        for (int c = 0; c < N; ++c)
                            dot += at (r, c) * at (p, c);

                        for (int c = 0; c < N; ++c)
                            at (r, c) -= dot * at (p, c);
                    }

                    double norm = 0.0;

                    for (int c = 0; c < N; ++c)
                        norm += at (r, c) * at (r, c);

                    norm = std::sqrt (norm);

                    for (int c = 0; c < N; ++c)
                        at (r, c) /= norm;
                }

            return true;
        }

        case MatrixKind::hadamardPermuted:
        {
            av::dsp::FeedbackMatrix<N> h;
            double base[N * N];
            h.getCoefficients (base);

            constexpr int    column[N] { 3, 6, 1, 4, 7, 2, 5, 0 };
            constexpr double sign[N]   { 1, -1, 1, 1, -1, 1, -1, -1 };

            for (int r = 0; r < N; ++r)
                for (int c = 0; c < N; ++c)
                    at (r, c) = sign[r] * base[r * N + column[c]];

            return true;
        }

        case MatrixKind::sparseChain:
        {
            // 45 degree rotations on (0,1) (2,3) (4,5) (6,7), then a cyclic
            // shift by one line, so each pair hands on to the next.
            const double h = std::sqrt (0.5);

            for (int k = 0; k < N / 2; ++k)
            {
                const int a = 2 * k, b = 2 * k + 1;

                at ((a + 1) % N, a) =  h;
                at ((a + 1) % N, b) =  h;
                at ((b + 1) % N, a) =  h;
                at ((b + 1) % N, b) = -h;
            }

            return true;
        }

        case MatrixKind::coupledBlocks:
        {
            constexpr double h4[4][4] { { 1, 1, 1, 1 }, { 1, -1, 1, -1 },
                                        { 1, 1, -1, -1 }, { 1, -1, -1, 1 } };

            const double c = std::cos (s.coupling), sn = std::sin (s.coupling);

            // u = blockdiag (H4, H4) v, then rotate line i against line i+4.
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                {
                    const double u = 0.5 * h4[i][j];

                    at (i,     j)     =  c  * u;
                    at (i,     j + 4) =  sn * u;
                    at (i + 4, j)     = -sn * u;
                    at (i + 4, j + 4) =  c  * u;
                }

            return true;
        }
    }

    return false;
}

/** The effective static matrix of a structure, whatever path applies it. */
inline void effectiveMatrix (const Structure& s, double* m)
{
    if (buildMatrix (s, m))
        return;

    av::dsp::FeedbackMatrix<N> base;

    base.setType (s.matrix == MatrixKind::householder ? av::dsp::FeedbackMatrix<N>::Type::householder
                : s.matrix == MatrixKind::identity    ? av::dsp::FeedbackMatrix<N>::Type::identity
                                                      : av::dsp::FeedbackMatrix<N>::Type::hadamard);
    base.getCoefficients (m);
}

/** Group delay, in samples, of one first-order allpass at a frequency. */
[[nodiscard]] inline double dispersionDelay (double a, double hz, double rate) noexcept
{
    const double w = 6.283185307179586 * hz / rate;

    return (1.0 - a * a) / (1.0 + a * a + 2.0 * a * std::cos (w));
}

// ===========================================================================
//  Small research stages
// ===========================================================================

/** A cascade of first-order allpasses. Magnitude-flat; delays low
    frequencies more than high ones when the coefficient is negative. */
class Dispersion
{
public:
    void prepare (int sections, double coefficient)
    {
        a = coefficient;
        state.assign (static_cast<size_t> (std::max (0, sections)), 0.0);
    }

    [[nodiscard]] double process (double x) noexcept
    {
        for (auto& s : state)
        {
            const double y = a * x + s;
            s = x - a * y;
            x = y;
        }

        return x;
    }

private:
    std::vector<double> state;
    double a = 0.0;
};

/** A plain ring buffer with a tap at any past sample. */
class TapLine
{
public:
    void prepare (size_t maxDelay)
    {
        size_t rounded = 1;

        while (rounded < maxDelay + 2)
            rounded <<= 1;

        buffer.assign (rounded, 0.0);
        mask = rounded - 1;
        index = 0;
    }

    void write (double x) noexcept
    {
        index = (index + 1) & mask;
        buffer[index] = x;
    }

    /** The value written `delay` samples before the latest write. */
    [[nodiscard]] double tap (size_t delay) const noexcept
    {
        return buffer[(index - delay) & mask];
    }

private:
    std::vector<double> buffer;
    size_t mask = 0, index = 0;
};

/** Feed-forward diffusion: a direct tap blended against a sparse signed tap
    sequence of unit energy. Early Diffusion is the blend angle, so the user
    control keeps its meaning: 0 is no diffusion, 1 is all of it. */
class VelvetDiffuser
{
public:
    void prepare (double rate, unsigned int seed, double spanMs, int taps, double amount)
    {
        line.prepare (msToSamples (spanMs) + 4);

        unsigned int state = seed | 1u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        const double span = spanMs * 0.001 * rate;

        delays.clear();
        weights.clear();

        double sumSq = 0.0;

        for (int k = 0; k < taps; ++k)
        {
            const double position = (k + 0.1 + 0.8 * next01()) / taps;
            const double weight   = (next01() < 0.5 ? -1.0 : 1.0) * std::exp (-2.0 * position);

            delays.push_back (std::max<size_t> (1, static_cast<size_t> (position * span)));
            weights.push_back (weight);
            sumSq += weight * weight;
        }

        for (auto& w : weights)
            w /= std::sqrt (sumSq);

        const double angle = std::clamp (amount, 0.0, 1.0) * 1.5707963267948966;

        direct = std::cos (angle);
        spread = std::sin (angle);
    }

    [[nodiscard]] double process (double x) noexcept
    {
        line.write (x);

        double sum = 0.0;

        for (size_t k = 0; k < delays.size(); ++k)
            sum += weights[k] * line.tap (delays[k]);

        return direct * x + spread * sum;
    }

private:
    TapLine line;
    std::vector<size_t> delays;
    std::vector<double> weights;
    double direct = 1.0, spread = 0.0;
};

namespace patterns
{
    inline constexpr double clusteredL[8] { 2.11, 2.29, 2.47, 2.71, 2.93, 3.19, 3.43, 3.71 };
    inline constexpr double clusteredR[8] { 2.11, 2.37, 2.53, 2.63, 3.01, 3.11, 3.53, 3.61 };
    inline constexpr double wideL[8]      { 0.71, 1.93, 5.23, 14.17, 3.37, 8.89, 23.3, 37.1 };
    inline constexpr double wideR[8]      { 0.71, 2.09, 4.81, 15.01, 3.11, 9.43, 22.1, 39.7 };

    inline constexpr av::dsp::diffusion::StereoPattern clustered
    {
        clusteredL, av::dsp::diffusion::inputLeftSigns, clusteredR, av::dsp::diffusion::inputRightSigns
    };

    inline constexpr av::dsp::diffusion::StereoPattern wide
    {
        wideL, av::dsp::diffusion::inputLeftSigns, wideR, av::dsp::diffusion::inputRightSigns
    };

    // Unit-norm sign vectors for the four-line blocks.
    inline constexpr double quadA[4] {  0.5,  0.5, -0.5, -0.5 };
    inline constexpr double quadB[4] {  0.5, -0.5,  0.5, -0.5 };
    inline constexpr double quadC[4] {  0.5, -0.5, -0.5,  0.5 };
    inline constexpr double quadD[4] { -0.5,  0.5,  0.5,  0.5 };

    // Where the multi-tap readout looks inside a line, as fractions of it.
    inline constexpr double tapFraction[2] { 0.37, 0.71 };
    inline constexpr double tapWeight      = 0.8;

    // Static in-loop allpass lengths, as fractions of the line they sit in.
    inline constexpr double loopFraction[3] { 0.113, 0.071, 0.047 };
}

// ===========================================================================
//  The rig
// ===========================================================================

class Rig
{
public:
    Rig (const Structure& s, const User& u, double sampleRate = kRate)
        : structure (s)
    {
        using namespace av::dsp;

        const double scale = mapping::sizeScaleFor (u.size);
        const double rt60  = mapping::rt60For (u.decay);

        // --- tank: the statements of mode12::Rig, in their order ------------
        const double longest = *std::max_element (s.delaysMs.begin(), s.delaysMs.end());

        configured = succeeded (core.setDelaysMilliseconds (s.delaysMs.data(), scale));

        capacityMs = longest * fdn::maxSizeScale;

        configured = configured && succeeded (core.prepare (sampleRate, capacityMs, 0.02));
        configured = configured && succeeded (core.setDelaysMilliseconds (s.delaysMs.data(), scale));

        core.setRt60Seconds (rt60, true);

        core.reset();
        core.resetModulation();

        core.setDamping (mapping::lowDampRatioFor  (u.lowDamp),
                         mapping::highDampRatioFor (u.highDamp),
                         u.lowDampHz, u.highDampHz, true);

        const double depth = mapping::modDepthFor (u.modDepth) * s.modDepthFactor;
        const double rate  = mapping::modRateScaleFor (u.modRate);

        architecture::applyWithDepth (core, architecture::defaultCandidate, depth, rate, true);

        if (s.allpassSections != allpassMod::defaultSectionsPerLine)
            core.setAllpassSectionsPerLine (s.allpassSections);

        // --- A, F: the matrix -----------------------------------------------
        if (s.matrix == MatrixKind::householder)
            core.setMatrixType (ResearchCore<N>::Matrix::Type::householder);
        else if (s.matrix == MatrixKind::identity)
            core.setMatrixType (ResearchCore<N>::Matrix::Type::identity);
        else if (buildMatrix (s, core.hooks.matrix))
        {
            core.hooks.customMatrix = true;
            core.setMatrixType (ResearchCore<N>::Matrix::Type::identity);
        }

        // --- G: in-loop stages ----------------------------------------------
        bool loopChanged = false;

        if (s.loopDispersion > 0)
        {
            core.hooks.dispersionSections = std::min (s.loopDispersion,
                                                      ResearchHooks<N>::maxDispersion);
            core.hooks.dispersionCoeff = s.dispersionCoeff;

            // Calibrated at 1 kHz, where a decay time is judged.
            const double each = dispersionDelay (s.dispersionCoeff, 1000.0, sampleRate);

            for (int i = 0; i < N; ++i)
                core.hooks.extraLoopDelay[i] += each * core.hooks.dispersionSections;

            loopChanged = true;
        }

        if (s.loopAllpasses > 0)
        {
            core.hooks.loopAllpassCount = std::min (s.loopAllpasses,
                                                    ResearchHooks<N>::maxLoopAllpasses);

            for (int i = 0; i < N; ++i)
                for (int k = 0; k < core.hooks.loopAllpassCount; ++k)
                {
                    const auto length = static_cast<size_t> (std::max (2.0, std::round (
                        core.getDelaySamples (i) * patterns::loopFraction[k])));

                    auto& a = core.hooks.loopAllpass[i][k];

                    a.buffer.assign (length, 0.0);
                    a.position = 0;
                    a.g = s.loopAllpassGain;

                    // A Schroeder allpass of length M delays by M on average.
                    core.hooks.extraLoopDelay[i] += static_cast<double> (length);
                }

            loopChanged = true;
        }

        if (loopChanged)
            core.refreshGains();

        // --- controlled loss in block B -------------------------------------
        if (s.matrix == MatrixKind::coupledBlocks && s.blockDecayRatio < 1.0)
        {
            const double rtB = rt60 * std::max (0.05, s.blockDecayRatio);

            for (int r = 4; r < N; ++r)
            {
                const double seconds = core.getEffectiveLoopDelaySamples (r) / sampleRate;

                // The extra per-pass loss that takes this line from rt60 to rtB.
                const double extra = std::pow (10.0, -3.0 * seconds * (1.0 / rtB - 1.0 / rt60));

                for (int c = 0; c < N; ++c)
                    core.hooks.matrix[r * N + c] *= extra;
            }
        }

        // --- B, C, D, H: the diffuser ---------------------------------------
        if (s.diffuser == DiffuserKind::velvet)
        {
            velvetL.prepare (sampleRate, 0x51ED270Bu, 30.0 * s.diffuserScale, 16, u.earlyDiff);
            velvetR.prepare (sampleRate, 0x2545F491u, 30.0 * s.diffuserScale, 16, u.earlyDiff);
        }
        else if (s.diffuser != DiffuserKind::none)
        {
            diffuser.setBasePattern (s.diffuser == DiffuserKind::clustered ? patterns::clustered
                                   : s.diffuser == DiffuserKind::wide      ? patterns::wide
                                                                           : diffusion::fdnInputPattern);
            diffuser.prepare (sampleRate, 0.02);
            diffuser.setStageCount (s.diffuserStages, true);
            diffuser.setDiffusion (0.7, true);
            diffuser.setDiffusion (u.earlyDiff, true);

            if (s.diffuserScale != 1.0)
                configured = configured
                          && succeeded (diffuser.setDelayScale (s.diffuserScale, true));

            diffuser.reset();
        }

        // --- E, I: vectors ---------------------------------------------------
        for (int i = 0; i < N; ++i)
        {
            injectionL[i] = fdn::injectionLeft [i] * fdn::signVectorNorm;
            injectionR[i] = fdn::injectionRight[i] * fdn::signVectorNorm;
            readoutL  [i] = fdn::readoutLeft   [i] * fdn::signVectorNorm;
            readoutR  [i] = fdn::readoutRight  [i] * fdn::signVectorNorm;
        }

        if (s.injection == InjectionKind::pair)
        {
            for (int i = 0; i < N; ++i)
                injectionL[i] = injectionR[i] = 0.0;

            injectionL[0] = 1.0;
            injectionR[1] = 1.0;
        }
        else if (s.injection == InjectionKind::split)
        {
            for (int i = 0; i < 4; ++i)
            {
                injectionL[i]     = patterns::quadA[i];
                injectionR[i]     = 0.0;
                injectionL[i + 4] = 0.0;
                injectionR[i + 4] = patterns::quadB[i];
            }
        }
        else if (s.injection == InjectionKind::blockA)
        {
            for (int i = 0; i < 4; ++i)
            {
                injectionL[i]     = patterns::quadA[i];
                injectionR[i]     = patterns::quadB[i];
                injectionL[i + 4] = 0.0;
                injectionR[i + 4] = 0.0;
            }
        }
        else if (s.injection == InjectionKind::staggered)
        {
            constexpr int order[N] { 0, 5, 2, 7, 4, 1, 6, 3 };

            for (int i = 0; i < N; ++i)
            {
                staggerDelay[i] = msToSamples (s.staggerMs * scale * order[i] / (N - 1));
                staggerLine[i].prepare (staggerDelay[i] + 2);
            }
        }

        if (s.readout == ReadoutKind::split)
        {
            for (int i = 0; i < 4; ++i)
            {
                readoutL[i]     = patterns::quadC[i];
                readoutR[i]     = 0.0;
                readoutL[i + 4] = 0.0;
                readoutR[i + 4] = patterns::quadD[i];
            }
        }
        else if (s.readout == ReadoutKind::blockB)
        {
            for (int i = 0; i < 4; ++i)
            {
                readoutL[i]     = 0.0;
                readoutR[i]     = 0.0;
                readoutL[i + 4] = patterns::quadC[i];
                readoutR[i + 4] = patterns::quadD[i];
            }
        }
        else if (s.readout == ReadoutKind::multiTap)
        {
            for (int i = 0; i < N; ++i)
            {
                const int length = core.getDelaySamples (i);

                tapLine[i].prepare (static_cast<size_t> (length));

                for (int k = 0; k < 2; ++k)
                    tapDelay[i][k] = static_cast<size_t> (std::max (1.0, std::round (
                        length * patterns::tapFraction[k])));
            }

            // Three taps per line of weights 1, w, w: unit energy overall.
            tapNorm = 1.0 / std::sqrt (1.0 + 2.0 * patterns::tapWeight * patterns::tapWeight);
        }

        // --- J ---------------------------------------------------------------
        if (s.preDispersion > 0)
        {
            preL.prepare (s.preDispersion, s.preDispersionCoeff);
            preR.prepare (s.preDispersion, s.preDispersionCoeff);
        }

        // --- level: mode12::Rig, own trim ------------------------------------
        compensation.prepare (sampleRate);
        compensation.setEnabled (true, true);
        compensation.setSize (scale, true);
        compensation.setRt60Seconds (rt60, true);
        compensation.setAllpassEngaged (
            architecture::settingsFor (architecture::defaultCandidate).allpassDepth * depth > 0.0,
            true);

        trim = mode12::ownWetTrim (s.delaysMs.data(), rt60, scale);

        attack.prepare (sampleRate);
        attack.snapTo (u.attack);

        const double ratio = scale / mapping::sizeScaleFor (er::referenceSizeControl);

        early.prepare (sampleRate, s.er, s.erChannelSpread, ratio);

        if (s.erFeedsTank)
            feed.prepare (sampleRate, s.er, s.erChannelSpread, ratio);

        const auto gains = er::gainsForBalance (u.earlyLate, er::calibrationFor (rt60, scale));

        lateGain    = gains.late;
        earlyGain   = gains.early;
        earlyActive = u.earlyLate > 0.0;

        width = u.width;
    }

    [[nodiscard]] bool isConfigured() const noexcept { return configured; }
    [[nodiscard]] double getCapacityMs() const noexcept { return capacityMs; }

    ResearchCore<N>& getCore() noexcept { return core; }

    /** Energy held in each line's feedback at this instant: the last state
        vector, squared. For the state-distribution measurement. */
    [[nodiscard]] const double* getLastState() const noexcept { return lastState; }

    void processSample (double& l, double& r) noexcept
    {
        double erL = 0.0, erR = 0.0;

        early.process (l, r, erL, erR);

        attack.process (&l, &r, 1);

        if (structure.erFeedsTank)
        {
            double fl = 0.0, fr = 0.0;
            feed.process (l, r, fl, fr);
            l = fl;
            r = fr;
        }

        if (structure.diffuser == DiffuserKind::velvet)
        {
            l = velvetL.process (l);
            r = velvetR.process (r);
        }
        else if (structure.diffuser != DiffuserKind::none)
        {
            diffuser.processSample (l, r);
        }

        if (structure.preDispersion > 0)
        {
            l = preL.process (l);
            r = preR.process (r);
        }

        double injection[N];

        for (int i = 0; i < N; ++i)
            injection[i] = injectionL[i] * l + injectionR[i] * r;

        if (structure.injection == InjectionKind::staggered)
            for (int i = 0; i < N; ++i)
            {
                staggerLine[i].write (injection[i]);
                injection[i] = staggerLine[i].tap (staggerDelay[i]);
            }

        core.processSample (injection, lastState);

        double outL = 0.0, outR = 0.0;

        for (int i = 0; i < N; ++i)
        {
            outL += readoutL[i] * lastState[i];
            outR += readoutR[i] * lastState[i];
        }

        if (structure.readout == ReadoutKind::multiTap)
        {
            for (int i = 0; i < N; ++i)
            {
                tapLine[i].write (core.hooks.written[i]);

                for (int k = 0; k < 2; ++k)
                {
                    const double x = patterns::tapWeight * tapLine[i].tap (tapDelay[i][k]);

                    // The shipping sign vectors, rotated, so the interior
                    // taps do not simply double the end taps.
                    outL += readoutL[(i + 2 * k + 1) % N] * x;
                    outR += readoutR[(i + 2 * k + 3) % N] * x;
                }
            }

            outL *= tapNorm;
            outR *= tapNorm;
        }

        l = outL;
        r = outR;

        compensation.process (l, r);

        l *= trim;
        r *= trim;

        if (structure.lateCorrection != 1.0)
        {
            l *= structure.lateCorrection;
            r *= structure.lateCorrection;
        }

        if (earlyActive)
        {
            l = lateGain * l + earlyGain * erL;
            r = lateGain * r + earlyGain * erR;
        }

        // The product's Width: mid/side on the composed wet signal. Skipped
        // at exactly 100 %, where the product's arithmetic is the identity in
        // intent but not in floating point.
        if (width != 1.0)
        {
            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * width;

            l = mid + side;
            r = mid - side;
        }
    }

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

private:
    Structure structure;

    ResearchCore<N> core;
    av::dsp::StereoDiffusionNetwork<double> diffuser;
    av::dsp::AttackInjection attack;
    av::dsp::SizeLevelCompensation compensation;
    mode12::ResearchEr early, feed;

    VelvetDiffuser velvetL, velvetR;
    Dispersion preL, preR;
    TapLine staggerLine[N], tapLine[N];
    size_t  staggerDelay[N] {}, tapDelay[N][2] {};

    double injectionL[N] {}, injectionR[N] {}, readoutL[N] {}, readoutR[N] {};
    double lastState[N] {};

    double trim = 1.0, lateGain = 1.0, earlyGain = 0.0, capacityMs = 0.0;
    double tapNorm = 1.0, width = 1.0;
    bool   earlyActive = false, configured = false;
};

/** The wet response to a unit impulse. `left` and `right` choose which input
    channels carry it: both for a centred source, one for a panned one. */
[[nodiscard]] inline Stereo impulseResponse (const Structure& s, const User& u, double seconds,
                                             bool left = true, bool right = true)
{
    auto rig = std::make_unique<Rig> (s, u);

    std::vector<double> one (1, 1.0), none (1, 0.0);

    return rig->render (left ? one : none, right ? one : none,
                        static_cast<size_t> (seconds * kRate));
}

[[nodiscard]] inline double energyOf (const Stereo& x)
{
    double sum = 0.0;

    for (size_t n = 0; n < x.l.size(); ++n)
        sum += x.l[n] * x.l[n] + x.r[n] * x.r[n];

    return sum;
}

/** Sets lateCorrection so the structure's integrated wet energy at the
    product defaults equals Hall's. Hall itself is returned untouched. */
[[nodiscard]] inline Structure calibrate (Structure s)
{
    const User u;

    s.lateCorrection = 1.0;

    const double reference = energyOf (impulseResponse (Structure {}, u, 3.0));
    const double own       = energyOf (impulseResponse (s, u, 3.0));

    const double correction = own > 0.0 ? std::sqrt (reference / own) : 1.0;

    // Left at exactly 1 when it is 1 to rounding, so Hall stays bit-exact.
    s.lateCorrection = std::abs (correction - 1.0) < 1.0e-9 ? 1.0 : correction;

    return s;
}

} // namespace avtest::stage3
