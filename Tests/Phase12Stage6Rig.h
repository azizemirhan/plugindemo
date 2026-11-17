#pragma once

#include "DSP/AllpassPhaseBank.h"
#include "DSP/DampingTuning.h"
#include "DSP/FdnTuning.h"
#include "DSP/LineDamping.h"
#include "DSP/ModulationArchitecture.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#if defined (_M_X64) || defined (__SSE2__)
 #include <emmintrin.h>
 #define AV_S6_SSE2 1
#else
 #define AV_S6_SSE2 0
#endif

/**
    PHASE 12 - STAGE 6. Research stages that do not fit the eight-line core.

    RESEARCH ONLY. NOTHING HERE SHIPS. Source/ IS NOT CHANGED.

      OpenTank        any number of lines, a sparse scattering matrix, static
                      and nested allpasses inside a line, delayed feeds,
                      delayed and interior readouts. Decay gain, damping
                      shelves and modulation are production's own classes,
                      applied per line as the research core applies them.
      PipelinedChain  the Plate dispersion chain cut into sub-chains that do
                      not wait for each other.
      Multirate       the same dispersion fitted band by band at reduced rates.

    Gates and hypotheses: Docs/Phase12-Stage6-Preregistration.md.
*/
namespace avtest::stage6
{

/** A ring buffer; tap (d) is the value written d writes before the latest. */
class Ring
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

    [[nodiscard]] double tap (size_t delay) const noexcept { return buffer[(index - delay) & mask]; }
    [[nodiscard]] size_t size() const noexcept { return buffer.size(); }

private:
    std::vector<double> buffer;
    size_t mask = 0, index = 0;
};

// ===========================================================================
//  The open tank
// ===========================================================================
struct OpenSpec
{
    /** A Schroeder allpass inside a line, its length a fraction of the line.
        innerFraction > 0 nests a second allpass inside its delay. */
    struct Allpass { double fraction = 0.3, g = 0.5, innerFraction = 0.0, innerG = 0.0; };

    struct Entry { int to = 0, from = 0; double v = 0.0; };

    /** Input reaches a line through its own delay. */
    struct Feed { int line = 0; double delayMs = 0.0, l = 0.0, r = 0.0; };

    /** fraction >= 1 hears the line's end (the state after the decay gain);
        below 1, a tap that far along the line. delayMs follows either. */
    struct Read { int line = 0; double fraction = 1.0, delayMs = 0.0, l = 0.0, r = 0.0; };

    int lines = 0;
    std::vector<double> delayMs;                   // at Size scale 1
    std::vector<Entry> matrix;
    std::vector<std::vector<Allpass>> allpass;     // per line; may be shorter than `lines`
    std::vector<Feed> feeds;
    std::vector<Read> reads;

    /** Largest |M Mt - I| entry. */
    [[nodiscard]] double orthogonalityError() const
    {
        std::vector<double> m (static_cast<size_t> (lines * lines), 0.0);

        for (const auto& e : matrix)
            m[static_cast<size_t> (e.to * lines + e.from)] += e.v;

        double worst = 0.0;

        for (int a = 0; a < lines; ++a)
            for (int b = 0; b < lines; ++b)
            {
                double dot = 0.0;

                for (int c = 0; c < lines; ++c)
                    dot += m[static_cast<size_t> (a * lines + c)] * m[static_cast<size_t> (b * lines + c)];

                worst = std::max (worst, std::abs (dot - (a == b ? 1.0 : 0.0)));
            }

        return worst;
    }
};

class OpenTank
{
public:
    struct Settings
    {
        double sampleRate = 48000.0, scale = 1.0, rt60 = 2.0;
        double lowRatio = 1.0, highRatio = 1.0, lowHz = 200.0, highHz = 6000.0;
        double modDepth = 0.0, modRateScale = 1.0;
        int allpassSections = av::dsp::allpassMod::defaultSectionsPerLine;
    };

    OpenTank (const OpenSpec& s, const Settings& c) : spec (s), settings (c)
    {
        using namespace av::dsp;

        const auto count = static_cast<size_t> (spec.lines);
        const double perMs = 0.001 * c.sampleRate;

        delay.resize (count);
        lines.resize (count);
        stages.resize (count);
        gain.assign (count, 0.0);
        dampers.resize (count);
        v.assign (count, 0.0);
        w.assign (count, 0.0);

        for (size_t b = 0; b * 8 < count; ++b)
        {
            banks.push_back (std::make_unique<AllpassPhaseBank<8>>());
            banks.back()->prepare (c.sampleRate);
        }

        architecture::applyWithDepth (*this, architecture::defaultCandidate, c.modDepth, c.modRateScale, true);

        if (c.allpassSections != allpassMod::defaultSectionsPerLine)
            setAllpassSectionsPerLine (c.allpassSections);

        for (size_t b = 0; b < banks.size(); ++b)
        {
            banks[b]->reset();

            // The banks share production's eight lanes. Each later bank is
            // started further along, so line i and line i + 8 do not move
            // together.
            for (size_t n = 0; n < b * 48017; ++n)
                banks[b]->advance();
        }

        const double aLow  = damping::onePoleCoefficient (c.lowHz,  c.sampleRate);
        const double aHigh = damping::onePoleCoefficient (c.highHz, c.sampleRate);
        const bool damped = c.lowRatio < 1.0 || c.highRatio < 1.0;

        for (size_t i = 0; i < count; ++i)
        {
            delay[i] = static_cast<size_t> (std::max (2.0, std::round (spec.delayMs[i] * c.scale * perMs)));
            lines[i].prepare (delay[i]);

            double loop = static_cast<double> (delay[i])
                        + banks[i / 8]->getAddedLoopDelaySamples (static_cast<int> (i % 8));

            if (i < spec.allpass.size())
                for (const auto& a : spec.allpass[i])
                {
                    Stage stage;

                    stage.g = a.g;
                    stage.length = static_cast<size_t> (std::max (2.0, std::round (a.fraction * delay[i])));
                    stage.outer.prepare (stage.length);

                    loop += static_cast<double> (stage.length);

                    if (a.innerFraction > 0.0)
                    {
                        stage.nested = true;
                        stage.innerG = a.innerG;
                        stage.innerLength = static_cast<size_t> (std::max (2.0, std::round (a.innerFraction * delay[i])));
                        stage.inner.prepare (stage.innerLength);

                        loop += static_cast<double> (stage.innerLength);
                    }

                    stages[i].push_back (std::move (stage));
                }

            const double seconds = loop / c.sampleRate;

            gain[i] = std::min (fdn::maxFeedbackGain, fdn::feedbackGainForRt60 (seconds, c.rt60));
            loopSeconds.push_back (seconds);

            damping::ShelfSpec shelf;

            shelf.coeffLow  = aLow;
            shelf.coeffHigh = aHigh;
            shelf.gainLow   = damped ? damping::shelfGainFor (seconds, c.rt60, c.lowRatio)  : 1.0;
            shelf.gainHigh  = damped ? damping::shelfGainFor (seconds, c.rt60, c.highRatio) : 1.0;

            dampers[i].prepare (c.sampleRate, 0.02);
            dampers[i].setShelves (shelf, damping::normalisationFor (shelf), true);
            dampers[i].reset();
        }

        double longestFeed = 0.0;

        for (const auto& f : spec.feeds)
        {
            feedDelay.push_back (static_cast<size_t> (std::round (f.delayMs * c.scale * perMs)));
            longestFeed = std::max (longestFeed, static_cast<double> (feedDelay.back()));
        }

        inL.prepare (static_cast<size_t> (longestFeed) + 2);
        inR.prepare (static_cast<size_t> (longestFeed) + 2);

        for (const auto& r : spec.reads)
        {
            const auto line = static_cast<size_t> (r.line);

            readTap.push_back (r.fraction >= 1.0 ? 0
                               : std::clamp<size_t> (static_cast<size_t> (std::round (r.fraction * delay[line])),
                                                     1, delay[line] - 1));
            readDelay.push_back (static_cast<size_t> (std::round (r.delayMs * c.scale * perMs)));
            readLine.emplace_back();
            readLine.back().prepare (readDelay.back() + 2);
        }
    }

    // --- what architecture::applyWithDepth asks of a tank ---------------------
    void setAllpassMode (av::dsp::TimeVaryingAllpass::Mode m) noexcept { for (auto& b : banks) b->setMode (m); }
    void setAllpassActiveLines (int n) noexcept                        { for (auto& b : banks) b->setActiveLines (n); }
    void setAllpassSectionsPerLine (int n) noexcept                    { for (auto& b : banks) b->setSectionsPerLine (n); }
    void setAllpassRateHz (double hz) noexcept                         { for (auto& b : banks) b->setRateHz (hz); }
    void setAllpassDepth (double d, bool now) noexcept                 { for (auto& b : banks) b->setDepth (d, now); }
    void setModulationRateHz (double) noexcept {}
    void setModulationDepth (double, bool) noexcept {}

    void process (double l, double r, double& outL, double& outR) noexcept
    {
        const auto count = static_cast<size_t> (spec.lines);

        inL.write (l);
        inR.write (r);

        for (auto& b : banks)
            b->advance();

        for (size_t i = 0; i < count; ++i)
        {
            double x = lines[i].tap (delay[i] - 1);

            for (auto& s : stages[i])
                x = s.process (x);

            x = banks[i / 8]->process (x, static_cast<int> (i % 8));
            x = dampers[i].process (x);

            v[i] = x * gain[i];
            w[i] = 0.0;
        }

        for (const auto& e : spec.matrix)
            w[static_cast<size_t> (e.to)] += e.v * v[static_cast<size_t> (e.from)];

        for (size_t k = 0; k < spec.feeds.size(); ++k)
        {
            const auto& f = spec.feeds[k];

            w[static_cast<size_t> (f.line)] += f.l * inL.tap (feedDelay[k]) + f.r * inR.tap (feedDelay[k]);
        }

        for (size_t i = 0; i < count; ++i)
            lines[i].write (w[i]);

        outL = 0.0;
        outR = 0.0;

        for (size_t k = 0; k < spec.reads.size(); ++k)
        {
            const auto& rd = spec.reads[k];
            const auto line = static_cast<size_t> (rd.line);

            double s = readTap[k] == 0 ? v[line] : lines[line].tap (readTap[k]);

            if (readDelay[k] > 0)
            {
                readLine[k].write (s);
                s = readLine[k].tap (readDelay[k]);
            }

            outL += rd.l * s;
            outR += rd.r * s;
        }
    }

    /** Energy held in the lines' last outputs; for the conservation test. */
    [[nodiscard]] const std::vector<double>& state() const noexcept { return v; }

    [[nodiscard]] double meanLoopSeconds() const noexcept
    {
        double sum = 0.0;

        for (double s : loopSeconds)
            sum += s;

        return loopSeconds.empty() ? 0.0 : sum / static_cast<double> (loopSeconds.size());
    }

    /** Doubles of delay memory actually allocated. */
    [[nodiscard]] size_t memoryDoubles() const noexcept
    {
        size_t total = inL.size() + inR.size();

        for (const auto& l : lines)    total += l.size();
        for (const auto& r : readLine) total += r.size();

        for (const auto& line : stages)
            for (const auto& s : line)
                total += s.outer.size() + s.inner.size();

        return total;
    }

private:
    struct Stage
    {
        Ring outer, inner;
        size_t length = 2, innerLength = 2;
        double g = 0.5, innerG = 0.0;
        bool nested = false;

        [[nodiscard]] double process (double x) noexcept
        {
            double delayed = outer.tap (length - 1);

            if (nested)
            {
                // The delay element is z^-length followed by an allpass.
                const double d2 = inner.tap (innerLength - 1);
                const double w2 = delayed - innerG * d2;

                inner.write (w2);
                delayed = innerG * w2 + d2;
            }

            const double w1 = x - g * delayed;

            outer.write (w1);

            return g * w1 + delayed;
        }
    };

    OpenSpec spec;
    Settings settings;

    std::vector<size_t> delay, feedDelay, readTap, readDelay;
    std::vector<Ring> lines, readLine;
    std::vector<std::vector<Stage>> stages;
    std::vector<double> gain, v, w, loopSeconds;
    std::vector<av::dsp::LineDamping> dampers;
    std::vector<std::unique_ptr<av::dsp::AllpassPhaseBank<8>>> banks;
    Ring inL, inR;
};

// ===========================================================================
//  The pipelined dispersion chain
// ===========================================================================
/**
    K first-order allpasses, y = a x + s, s = x - a y, cut into `split`
    sub-chains of equal length. Sub-chain c is fed what sub-chain c - 1
    produced ONE SAMPLE EARLIER, so within a sample no sub-chain waits for
    another: the processor can run them side by side.

    The transfer function is the plain chain's times z^-(split - 1). With
    split = 1 this is the plain chain, operation for operation.
*/
class PipelinedChain
{
public:
    /** Returns false (and stays a pass-through) if K is not a multiple of split. */
    bool prepare (const std::vector<double>& coefficients, int split, bool vector, bool inRegisters = false)
    {
        const int k = static_cast<int> (coefficients.size());

        chains = std::max (1, split);

        if (k == 0 || k % chains != 0)
        {
            depth = 0;
            return false;
        }

        depth = k / chains;
        simd = vector && AV_S6_SSE2 != 0;
        registers = inRegisters && AV_S6_SSE2 != 0 && (chains == 4 || chains == 8 || chains == 16);

        const auto lanes = static_cast<size_t> (2 * chains);

        a.assign (static_cast<size_t> (depth) * lanes, 0.0);
        s.assign (static_cast<size_t> (depth) * lanes, 0.0);
        x.assign (lanes, 0.0);
        previous.assign (lanes, 0.0);

        for (int c = 0; c < chains; ++c)
            for (int j = 0; j < depth; ++j)
                for (int ch = 0; ch < 2; ++ch)
                    a[static_cast<size_t> (j) * lanes + static_cast<size_t> (2 * c + ch)]
                        = coefficients[static_cast<size_t> (c * depth + j)];

        return true;
    }

    void process (double& l, double& r) noexcept
    {
        if (depth == 0)
            return;

        const auto lanes = static_cast<size_t> (2 * chains);

       #if AV_S6_SSE2
        if (registers)
        {
            switch (chains)
            {
                case 4:  run<4>  (l, r); return;
                case 8:  run<8>  (l, r); return;
                default: run<16> (l, r); return;
            }
        }
       #endif

        x[0] = l;
        x[1] = r;

        for (size_t i = 2; i < lanes; ++i)
            x[i] = previous[i - 2];

        double* state = s.data();
        const double* coefficient = a.data();

       #if AV_S6_SSE2
        if (simd)
        {
            for (int j = 0; j < depth; ++j)
            {
                for (size_t i = 0; i < lanes; i += 2)
                {
                    const __m128d av = _mm_loadu_pd (coefficient + i);
                    const __m128d xv = _mm_loadu_pd (x.data() + i);
                    const __m128d yv = _mm_add_pd (_mm_mul_pd (av, xv), _mm_loadu_pd (state + i));

                    _mm_storeu_pd (state + i, _mm_sub_pd (xv, _mm_mul_pd (av, yv)));
                    _mm_storeu_pd (x.data() + i, yv);
                }

                state += lanes;
                coefficient += lanes;
            }
        }
        else
       #endif
        {
            for (int j = 0; j < depth; ++j)
            {
                for (size_t i = 0; i < lanes; ++i)
                {
                    const double y = coefficient[i] * x[i] + state[i];

                    state[i] = x[i] - coefficient[i] * y;
                    x[i] = y;
                }

                state += lanes;
                coefficient += lanes;
            }
        }

        for (size_t i = 0; i < lanes; ++i)
            previous[i] = x[i];

        l = x[lanes - 2];
        r = x[lanes - 1];
    }

    [[nodiscard]] int latencySamples() const noexcept { return chains - 1; }
    [[nodiscard]] size_t stateDoubles() const noexcept { return s.size() + previous.size(); }

private:
   #if AV_S6_SSE2
    /** PV3. The same arithmetic as the two-wide loop above, with the count
        of sub-chains fixed at compile time, so each sub-chain's running value
        is a register and never goes through memory between sections. */
    template <int C>
    void run (double& l, double& r) noexcept
    {
        __m128d v[C];

        v[0] = _mm_set_pd (r, l);

        for (int c = 1; c < C; ++c)
            v[c] = _mm_loadu_pd (previous.data() + 2 * (c - 1));

        double* state = s.data();
        const double* coefficient = a.data();

        for (int j = 0; j < depth; ++j)
        {
            for (int c = 0; c < C; ++c)
            {
                const __m128d av = _mm_loadu_pd (coefficient + 2 * c);
                const __m128d yv = _mm_add_pd (_mm_mul_pd (av, v[c]), _mm_loadu_pd (state + 2 * c));

                _mm_storeu_pd (state + 2 * c, _mm_sub_pd (v[c], _mm_mul_pd (av, yv)));
                v[c] = yv;
            }

            state += 2 * C;
            coefficient += 2 * C;
        }

        for (int c = 0; c < C; ++c)
            _mm_storeu_pd (previous.data() + 2 * c, v[c]);

        l = _mm_cvtsd_f64 (v[C - 1]);
        r = _mm_cvtsd_f64 (_mm_unpackhi_pd (v[C - 1], v[C - 1]));
    }
   #endif

    std::vector<double> a, s, x, previous;
    int chains = 1, depth = 0;
    bool simd = false, registers = false;
};

// ===========================================================================
//  Multirate dispersion
// ===========================================================================
/** Group delay, in samples of its own rate, of one first-order allpass. */
[[nodiscard]] inline double sectionDelay (double a, double radians) noexcept
{
    return (1.0 - a * a) / (1.0 + a * a + 2.0 * a * std::cos (radians));
}

/** K sections of coefficient a at a rate, standing in for a delay curve
    over a band:  target (f)  ~  K x delay (a, f) + offset. */
struct BandFit
{
    double a = 0.0;
    int    sections = 0;
    double offset = 0.0;        // samples at the full rate
    double rmsError = 0.0;      // samples at the full rate
};

/** `target` gives the wanted delay in full-rate samples at a frequency in Hz.
    The sections run at fullRate / rateDivide. */
template <typename Target>
[[nodiscard]] inline BandFit fitBand (Target&& target, double fullRate, int rateDivide, double fromHz, double toHz,
                                      double toleranceSamples = 0.0)
{
    constexpr int points = 96;
    constexpr double twoPi = 6.283185307179586;

    double t[points], f[points];

    for (int i = 0; i < points; ++i)
    {
        f[i] = fromHz + (toHz - fromHz) * (i + 0.5) / points;
        t[i] = target (f[i]);
    }

    // With a tolerance: the FEWEST sections whose curve is within it, the
    // coefficient kept between -0.95 and -0.05. Without: the smallest error.
    BandFit best, fewest;
    best.rmsError = 1.0e30;
    fewest.sections = -1;

    for (int step = -95; step <= (toleranceSamples > 0.0 ? -5 : 60); ++step)
    {
        const double a = step * 0.01;

        double g[points], sg = 0.0, st = 0.0, sgg = 0.0, sgt = 0.0;

        for (int i = 0; i < points; ++i)
        {
            g[i] = rateDivide * sectionDelay (a, twoPi * f[i] * rateDivide / fullRate);

            sg += g[i];
            st += t[i];
            sgg += g[i] * g[i];
            sgt += g[i] * t[i];
        }

        const double variance = sgg - sg * sg / points;

        if (variance <= 1.0e-12)
            continue;

        const int k = std::max (0, static_cast<int> (std::lround ((sgt - sg * st / points) / variance)));
        const double offset = (st - k * sg) / points;

        double error = 0.0;

        for (int i = 0; i < points; ++i)
            error += (k * g[i] + offset - t[i]) * (k * g[i] + offset - t[i]);

        error = std::sqrt (error / points);

        if (error < best.rmsError)
        {
            best.a = a;
            best.sections = k;
            best.offset = offset;
            best.rmsError = error;
        }

        if (toleranceSamples > 0.0 && error <= toleranceSamples
            && (fewest.sections < 0 || k < fewest.sections
                || (k == fewest.sections && error < fewest.rmsError)))
        {
            fewest.a = a;
            fewest.sections = k;
            fewest.offset = offset;
            fewest.rmsError = error;
        }
    }

    return fewest.sections >= 0 ? fewest : best;
}

/** One split: what is below the cut goes down by `divide` and is handled by
    `inner` (another split) or by the `low` chain; the rest stays at this
    rate and is handled by the `high` chain. */
struct MultirateSpec
{
    int divide = 4;
    int halfLength = 11;        // the split filter has 2 x divide x halfLength + 1 taps
    BandFit high, low;
    std::shared_ptr<const MultirateSpec> inner;
};

class Multirate
{
public:
    /** `rateDivide`: this node's own rate is fullRate / rateDivide. */
    void prepare (const MultirateSpec& spec, int rateDivide = 1)
    {
        constexpr double pi = 3.14159265358979323846;

        divide = spec.divide;
        taps = 2 * divide * spec.halfLength + 1;

        // Linear phase, Blackman window. Pass band to a quarter of the low
        // rate, stop band from half of it: the low band is sampled at twice
        // its width.
        h.assign (static_cast<size_t> (taps), 0.0);

        const double cutoff = 0.75 / (2.0 * divide);     // cycles per sample, -6 dB
        double sum = 0.0;

        for (int n = 0; n < taps; ++n)
        {
            const double m = n - 0.5 * (taps - 1);
            const double sinc = m == 0.0 ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * m) / (pi * m);
            const double window = 0.42 - 0.5 * std::cos (2.0 * pi * n / (taps - 1))
                                + 0.08 * std::cos (4.0 * pi * n / (taps - 1));

            h[static_cast<size_t> (n)] = sinc * window;
            sum += h[static_cast<size_t> (n)];
        }

        for (auto& c : h)
            c /= sum;

        input.prepare (static_cast<size_t> (taps) + 2);
        plain.prepare (static_cast<size_t> (2 * spec.halfLength) + 4);
        worked.prepare (static_cast<size_t> (2 * spec.halfLength) + 4);
        phase = 0;

        // --- alignment ------------------------------------------------------
        // Every branch must come out as  target + one common latency.
        const double own = static_cast<double> (rateDivide);          // full-rate samples per own sample
        const double lowStep = own * divide;

        double lowLatency = 0.0;        // full-rate samples the low branch adds beyond the target

        highState.assign (static_cast<size_t> (std::max (0, spec.high.sections)), 0.0);
        highA = spec.high.a;

        if (spec.inner != nullptr)
        {
            inner = std::make_unique<Multirate>();
            inner->prepare (*spec.inner, rateDivide * divide);
            lowLatency = inner->latency;
            lowState.clear();
            lowDelay = 0;
        }
        else
        {
            lowState.assign (static_cast<size_t> (std::max (0, spec.low.sections)), 0.0);
            lowA = spec.low.a;

            // The fit says  sections ~ target - offset.  Bring both branches
            // to the same constant by delaying whichever is early.
            const double need = std::max (-spec.low.offset, -spec.high.offset);

            lowDelay = static_cast<size_t> (std::max (0.0, std::round ((need + spec.low.offset) / lowStep)));
            lowLatency = static_cast<double> (lowDelay) * lowStep - spec.low.offset;
        }

        const double highNeed = (lowLatency + spec.high.offset) / own;

        highDelay = static_cast<size_t> (std::max (0.0, std::round (highNeed)));
        postDelay = static_cast<size_t> (std::max (0.0, std::round (-highNeed)));

        lowLine.prepare (lowDelay + 2);
        highLine.prepare (highDelay + 2);
        postLine.prepare (postDelay + 2);

        latency = (taps - 1) * own + std::max (lowLatency, -spec.high.offset);
        filterLatency = (taps - 1);
    }

    /** One sample at this node's rate. */
    [[nodiscard]] double process (double x) noexcept
    {
        input.write (x);

        if (phase == 0)
        {
            double u = 0.0;

            for (int n = 0; n < taps; ++n)
                u += h[static_cast<size_t> (n)] * input.tap (static_cast<size_t> (n));

            plain.write (u);

            if (bypass)
            {
                worked.write (u);
            }
            else if (inner != nullptr)
            {
                worked.write (inner->process (u));
            }
            else
            {
                for (auto& st : lowState)
                {
                    const double y = lowA * u + st;
                    st = u - lowA * y;
                    u = y;
                }

                lowLine.write (u);
                worked.write (lowLine.tap (lowDelay));
            }
        }

        // Interpolation, both the untouched low band (to take it out of the
        // high branch) and the worked one.
        double lowPlain = 0.0, lowWorked = 0.0;

        for (int k = 0, n = phase; n < taps; ++k, n += divide)
        {
            lowPlain  += h[static_cast<size_t> (n)] * plain.tap (static_cast<size_t> (k));
            lowWorked += h[static_cast<size_t> (n)] * worked.tap (static_cast<size_t> (k));
        }

        lowPlain  *= divide;
        lowWorked *= divide;

        if (++phase >= divide)
            phase = 0;

        double high = input.tap (static_cast<size_t> (filterLatency)) - lowPlain;

        if (! bypass)
        {
            for (auto& st : highState)
            {
                const double y = highA * high + st;
                st = high - highA * y;
                high = y;
            }

            highLine.write (high);
            high = highLine.tap (highDelay);

            postLine.write (lowWorked);
            lowWorked = postLine.tap (postDelay);
        }

        return high + lowWorked;
    }

    /** Chains and alignment delays out: what is left is the filter bank. */
    void setBypass (bool b) noexcept
    {
        bypass = b;

        if (inner != nullptr)
            inner->setBypass (b);
    }

    /** Sections weighted by the rate they run at, and filter multiplies per
        own-rate sample: what the arithmetic amounts to. */
    [[nodiscard]] double sectionEquivalents() const noexcept
    {
        return static_cast<double> (highState.size())
             + (inner != nullptr ? inner->sectionEquivalents() : static_cast<double> (lowState.size())) / divide;
    }

    [[nodiscard]] double filterMultiplies() const noexcept
    {
        return 3.0 * taps / divide + (inner != nullptr ? inner->filterMultiplies() / divide : 0.0);
    }

    /** Full-rate samples by which the whole thing is later than the target curve. */
    double latency = 0.0;

private:
    std::vector<double> h, highState, lowState;
    Ring input, plain, worked, lowLine, highLine, postLine;
    std::unique_ptr<Multirate> inner;

    double highA = 0.0, lowA = 0.0;
    size_t lowDelay = 0, highDelay = 0, postDelay = 0;
    int divide = 4, taps = 1, phase = 0, filterLatency = 0;
    bool bypass = false;
};

} // namespace avtest::stage6
