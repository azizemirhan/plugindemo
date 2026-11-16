#include "Phase12ModeRig.h"

#include "Suites.h"
#include "TestFramework.h"

#include "DSP/MorphingStereoFdn.h"
#include "DSP/StereoFdn.h"

#include <atomic>
#include <complex>
#include <cstring>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <thread>

/*
    PHASE 12 - STAGE 2. Are the five candidates five FAMILIES?

    RESEARCH ONLY. NO PRODUCTION DSP IS CHANGED AND Source/ IS UNTOUCHED. The
    candidates are instantiated by the rig in Phase12ModeRig.h from public
    calls that already exist.

    THE QUESTION IS NOT "CAN A DIFFERENCE BE MEASURED". Any two delay sets
    measure differently. The question the brief sets is whether another Mode
    can reach this one through the controls a user already has - Size, Decay,
    Early/Late, Early Diffusion, Attack, Mod Depth. If it can, the Mode is a
    preset of the other one and not a family.

    So the central experiment is an impostor search: for every ordered pair
    (A, B), B's user controls are optimised to minimise the distance to A.
    What is reported is the residual. Three things keep that honest.

      1. THE SCALES AND THRESHOLDS WERE FIXED FIRST. They live in the rig
         header and were committed before a candidate was scored.

      2. THE THRESHOLDS ARE ANCHORED TO A MEASURED FLOOR, not to the number
         1. Two delay sets differ in fine structure even when they are the
         same family, and features taken from a single impulse response see
         that. The floor is the distance between Hall and things that are
         Hall by construction; "distinct" means clearing a multiple of it.

      3. THE OPTIMISER IS TESTED ON A TARGET IT CAN REACH. An impostor search
         that fails to converge reports a large residual, and a large
         residual is exactly what a Mode needs in order to pass. So before
         its residuals are believed, the search is asked to recover Hall at
         deliberately moved controls starting from the defaults. If it cannot
         find a target that is known to exist, it proves nothing.

    The direction of every remaining bias is against the plan and is stated
    where it applies: a search that stops short flatters the Modes, so its
    residuals are upper bounds on how distinct they are, never lower ones.

    NOTHING HERE IS REQUIRED TO COME OUT AS FIVE.
*/

using namespace av::dsp;

namespace avtest
{

namespace mode12
{

// ===========================================================================
//  Delay-set search
// ===========================================================================
namespace
{
    [[nodiscard]] int gcdOf (int a, int b) noexcept
    {
        while (b != 0)
        {
            const int t = a % b;
            a = b;
            b = t;
        }

        return a;
    }

    [[nodiscard]] std::array<int, 8> toSamples (const std::array<double, 8>& ms, double rate)
    {
        std::array<int, 8> s {};

        for (size_t i = 0; i < 8; ++i)
            s[i] = static_cast<int> (std::lround (ms[i] * 0.001 * rate));

        return s;
    }

    [[nodiscard]] double worstRelativeGcd (const std::array<int, 8>& s)
    {
        double worst = 0.0;

        for (size_t i = 0; i < 8; ++i)
            for (size_t j = i + 1; j < 8; ++j)
                worst = std::max (worst, static_cast<double> (gcdOf (s[i], s[j]))
                                         / std::max (1.0, static_cast<double> (std::min (s[i], s[j]))));

        return worst;
    }

    /** The objective FdnDelaySetTests maximises, unchanged. */
    [[nodiscard]] double setScore (const std::array<double, 8>& ms)
    {
        const auto samples = toSamples (ms, kRate);

        const double subset = worstSubsetSumCollision (ms.data(), 8);
        const double ratio  = worstSimpleRatioProximity (ms.data(), 8);
        const double spread = modalSpacingSpread (samples.data(), 8, kRate, 4000.0);

        return std::min (subset, ratio) / (1.0 + spread);
    }
}

std::array<double, 8> searchDelaySet (double sumMs, double ratio, unsigned int seed)
{
    const double step = std::pow (ratio, 1.0 / 7.0);
    const double minGap = 1.0 + 0.35 * (step - 1.0);

    auto usable = [&] (const std::array<double, 8>& ms)
    {
        for (size_t i = 1; i < 8; ++i)
            if (ms[i] <= ms[i - 1] * minGap)
                return false;

        const double total = std::accumulate (ms.begin(), ms.end(), 0.0);

        if (std::abs (total / sumMs - 1.0) > 0.02)
            return false;

        if (std::abs ((ms[7] / ms[0]) / ratio - 1.0) > 0.08)
            return false;

        for (double rate : { 44100.0, 48000.0, 96000.0 })
            if (worstRelativeGcd (toSamples (ms, rate)) > 0.05)
                return false;

        return true;
    };

    unsigned int state = seed;

    auto nextUniform = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        return static_cast<double> (state) / 4294967296.0;
    };

    std::array<double, 8> best {};
    double bestScore = -1.0;

    const double jitter = std::min (0.14, 0.9 * (step - 1.0));

    for (int trial = 0; trial < 6000; ++trial)
    {
        std::array<double, 8> c {};
        double value = 1.0, total = 0.0;

        for (size_t k = 0; k < 8; ++k)
        {
            c[k] = value * (1.0 + jitter * (nextUniform() - 0.5));
            total += c[k];
            value *= step;
        }

        for (auto& v : c)
            v = std::round (v * sumMs / total * 100.0) / 100.0;

        if (! usable (c))
            continue;

        const double s = setScore (c);

        if (s > bestScore)
        {
            bestScore = s;
            best = c;
        }
    }

    if (bestScore < 0.0)
        return best;

    for (int pass = 0; pass < 6; ++pass)
    {
        bool improved = false;

        for (size_t k = 0; k < 8; ++k)
            for (int delta = -20; delta <= 20; ++delta)
            {
                if (delta == 0)
                    continue;

                auto c = best;
                c[k] = std::round ((c[k] + delta * 0.01) * 100.0) / 100.0;

                if (! usable (c))
                    continue;

                const double s = setScore (c);

                if (s > bestScore)
                {
                    bestScore = s;
                    best = c;
                    improved = true;
                }
            }

        if (! improved)
            break;
    }

    return best;
}

namespace
{
    constexpr double kHallSumMs = 388.50;

    [[nodiscard]] std::vector<ModeCandidate> buildCandidates()
    {
        std::vector<ModeCandidate> m (modeCount);

        // The middle of each Stage 1 range. Nothing here was tuned against a
        // result: the values were written down, then scored.

        {
            auto& c = m[room];
            c.name = "Room";
            c.delaysMs = searchDelaySet (kHallSumMs * 0.52, 3.2, 0x5200u);
            c.diffuserStages = 4;
            c.diffuserScale  = 0.6;
            c.modDepthFactor = 0.7;
            c.er.events = 10;
            c.er.windowMs = 40.0;
            c.er.envelopeDecay = 0.8;
            c.er.sizeAlpha = 0.35;
        }

        {
            auto& c = m[hall];
            c.name = "Hall";

            for (size_t i = 0; i < 8; ++i)
                c.delaysMs[i] = fdn::delaysMs[i];

            // Everything else is the default-constructed shipping value.
        }

        {
            auto& c = m[chamber];
            c.name = "Chamber";
            c.delaysMs = searchDelaySet (kHallSumMs * 0.78, 2.0, 0xC4A0u);
            c.diffuserStages = 6;
            c.modDepthFactor = 0.9;
            c.er.events = 14;
            c.er.windowMs = 55.0;
            c.er.envelopeDecay = 2.5;
        }

        {
            auto& c = m[plate];
            c.name = "Plate";
            c.delaysMs = searchDelaySet (kHallSumMs * 0.58, 1.65, 0x91A7u);
            c.diffuserStages = 8;
            c.diffuserScale  = 0.5;
            c.allpassSections = 2;
            c.modDepthFactor = 0.9;
            c.er.events = 28;
            c.er.windowMs = 25.0;
            c.er.envelopeDecay = 8.0;
            c.er.sizeAlpha = 0.10;
            c.erChannelSpread = 0.25;
        }

        {
            auto& c = m[ambient];
            c.name = "Ambient";
            c.delaysMs = searchDelaySet (kHallSumMs * 1.35, 4.2, 0xA3B1u);
            c.diffuserStages = 8;
            c.diffuserScale  = 1.5;
            c.modDepthFactor = 1.3;
            c.modRateFactor  = 0.7;
            c.er.events = 14;
            c.er.windowMs = 135.0;
            c.er.envelopeDecay = 8.0;
            c.er.earliestFraction = 0.15;
        }

        return m;
    }
}

const std::vector<ModeCandidate>& candidates()
{
    static const std::vector<ModeCandidate> all = buildCandidates();
    return all;
}

const ModeCandidate& hallSibling()
{
    static const ModeCandidate sibling = []
    {
        ModeCandidate c = candidates()[hall];
        c.name = "Hall sibling";
        c.delaysMs = searchDelaySet (kHallSumMs, fdn::delaysMs[7] / fdn::delaysMs[0], 0x4A11u);
        return c;
    }();

    return sibling;
}

} // namespace mode12

using namespace mode12;

namespace
{
    // =======================================================================
    //  Formatting
    // =======================================================================
    [[nodiscard]] std::string fx (double v, int digits = 2)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (digits) << v;
        return s.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    [[nodiscard]] bool identical (const Stereo& a, const Stereo& b)
    {
        return a.l.size() == b.l.size()
            && std::memcmp (a.l.data(), b.l.data(), a.l.size() * sizeof (double)) == 0
            && std::memcmp (a.r.data(), b.r.data(), a.r.size() * sizeof (double)) == 0;
    }

    [[nodiscard]] double worstDifference (const Stereo& a, const Stereo& b)
    {
        double worst = 0.0;

        for (size_t n = 0; n < std::min (a.l.size(), b.l.size()); ++n)
            worst = std::max ({ worst, std::abs (a.l[n] - b.l[n]), std::abs (a.r[n] - b.r[n]) });

        return worst;
    }

    [[nodiscard]] double energyOf (const Stereo& x)
    {
        double sum = 0.0;

        for (size_t n = 0; n < x.l.size(); ++n)
            sum += x.l[n] * x.l[n] + x.r[n] * x.r[n];

        return sum;
    }

    [[nodiscard]] double dB (double ratio)
    {
        return 10.0 * std::log10 (std::max (1.0e-300, ratio));
    }

    /** Long enough for the decay features at this Decay, and no longer. */
    [[nodiscard]] double irSecondsFor (double decay)
    {
        return std::clamp (0.45 * decay + 0.7, 1.0, 6.0);
    }

    [[nodiscard]] Features featuresOf (const ModeCandidate& m, const User& u)
    {
        return analyse (impulseResponse (m, u, irSecondsFor (u.decay)));
    }

    // =======================================================================
    //  Production references, for the Hall control
    // =======================================================================

    /** One production bank, driven exactly as the processor drives it. */
    [[nodiscard]] Stereo productionBank (const User& u, size_t length)
    {
        ScopedFtz ftz;

        const double scale = mapping::sizeScaleFor (u.size);
        const double rt60  = mapping::rt60For (u.decay);

        auto bank = std::make_unique<StereoFdn<N>>();

        (void) bank->prepare (kRate);
        (void) bank->setSizeScale (scale);

        bank->setRt60Seconds (rt60, true);
        bank->reset();
        bank->resetModulation();
        bank->setInputDiffusion (u.earlyDiff, true);

        bank->setDamping (mapping::lowDampRatioFor  (u.lowDamp),
                          mapping::highDampRatioFor (u.highDamp),
                          u.lowDampHz, u.highDampHz, true);

        const double depth = mapping::modDepthFor (u.modDepth);

        architecture::applyWithDepth (*bank, architecture::defaultCandidate, depth,
                                      mapping::modRateScaleFor (u.modRate), true);

        SizeLevelCompensation compensation;
        compensation.prepare (kRate);
        compensation.setEnabled (true, true);
        compensation.setSize (scale, true);
        compensation.setRt60Seconds (rt60, true);
        compensation.setAllpassEngaged (
            architecture::settingsFor (architecture::defaultCandidate).allpassDepth * depth > 0.0,
            true);

        AttackInjection attack;
        attack.prepare (kRate);
        attack.snapTo (u.attack);

        EarlyReflections early;
        early.prepare (kRate);
        early.snapToSizeRatio (scale / mapping::sizeScaleFor (er::referenceSizeControl));

        EarlyLateBalance balance;
        balance.prepare (kRate);
        balance.snapTo (u.earlyLate, er::calibrationFor (rt60, scale));

        const bool   active = ! balance.isBypassed();
        const double trim   = mapping::previewWetTrim (rt60, scale);

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = n == 0 ? 1.0 : 0.0;
            double r = l;

            double erL = 0.0, erR = 0.0;

            if (active) early.process (&l, &r, &erL, &erR, 1);
            else        early.writeOnly (&l, &r, 1);

            attack.process (&l, &r, 1);
            bank->processSample (l, r);
            compensation.process (l, r);

            l *= trim;
            r *= trim;

            if (active)
            {
                const double lateG  = balance.nextLate();
                const double earlyG = balance.nextEarly();

                l = lateG * l + earlyG * erL;
                r = lateG * r + earlyG * erR;
            }

            out.l[n] = l;
            out.r[n] = r;
        }

        return out;
    }

    /** The full dual-bank production tank, allowed to settle its opening Size
        transition on silence first. Reports how long that took, so the rig
        can be put on the same modulation phase. */
    [[nodiscard]] Stereo productionTank (const User& u, size_t length, size_t& settleSamples)
    {
        ScopedFtz ftz;

        auto tank = std::make_unique<MorphingStereoFdn<N>>();

        (void) tank->prepare (kRate);
        tank->setCompensationEnabled (true, true);
        tank->setRt60Seconds (mapping::rt60For (u.decay), true);
        tank->setInputDiffusion (u.earlyDiff, true);
        tank->setSize (mapping::sizeScaleFor (u.size));

        tank->setDamping (mapping::lowDampRatioFor  (u.lowDamp),
                          mapping::highDampRatioFor (u.highDamp),
                          u.lowDampHz, u.highDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (u.modDepth),
                                      mapping::modRateScaleFor (u.modRate), true);

        settleSamples = 0;

        while (tank->isMorphing() && settleSamples < static_cast<size_t> (kRate * 4.0))
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
            ++settleSamples;
        }

        const double trim = mapping::previewWetTrim (mapping::rt60For (u.decay),
                                                     mapping::sizeScaleFor (u.size));

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = n == 0 ? 1.0 : 0.0;
            double r = l;

            tank->processSample (l, r);

            out.l[n] = l * trim;
            out.r[n] = r * trim;
        }

        return out;
    }

    // =======================================================================
    //  The impostor search
    // =======================================================================
    constexpr int kControls = 6;

    constexpr const char* kControlNames[kControls]
    {
        "Size", "Decay", "Early/Late", "Early Diff", "Attack", "Mod Depth"
    };

    struct Controls { double v[kControls] {}; };

    constexpr double kDecayLow = 0.1, kDecayHigh = 20.0;

    [[nodiscard]] User toUser (const Controls& c)
    {
        User u;

        u.size      = std::clamp (c.v[0], 0.0, 1.0);
        u.decay     = kDecayLow * std::pow (kDecayHigh / kDecayLow, std::clamp (c.v[1], 0.0, 1.0));
        u.earlyLate = std::clamp (c.v[2], 0.0, 1.0);
        u.earlyDiff = std::clamp (c.v[3], 0.0, 1.0);
        u.attack    = std::clamp (c.v[4], 0.0, 1.0);
        u.modDepth  = std::clamp (c.v[5], 0.0, 1.0);

        return u;
    }

    [[nodiscard]] Controls toControls (const User& u)
    {
        Controls c;

        c.v[0] = u.size;
        c.v[1] = std::log (u.decay / kDecayLow) / std::log (kDecayHigh / kDecayLow);
        c.v[2] = u.earlyLate;
        c.v[3] = u.earlyDiff;
        c.v[4] = u.attack;
        c.v[5] = u.modDepth;

        return c;
    }

    struct SearchResult
    {
        Controls best;
        Distance distance;
        int evaluations = 0;
    };

    /**
        Minimises the distance from `impostor`, over its user controls, to a
        target feature vector.

        Starts from the target's own knob positions - the null hypothesis that
        the two Modes differ only in hidden data - and from a fixed
        pseudo-random scatter. The two best starts are each refined by a
        pattern search with a halving step. Deterministic: no clock, no
        global state, so it is the same in every run and on every thread.
    */
    [[nodiscard]] SearchResult mimic (const Features& target, const ModeCandidate& impostor,
                                      const Controls& startAt, unsigned int seed)
    {
        SearchResult result;
        result.distance.total = 1.0e9;

        auto evaluate = [&] (const Controls& c)
        {
            ++result.evaluations;
            return distance (target, featuresOf (impostor, toUser (c)));
        };

        struct Scored { Controls c; Distance d; };

        std::vector<Scored> starts;
        starts.push_back ({ startAt, evaluate (startAt) });

        unsigned int state = seed | 1u;

        auto next01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return static_cast<double> (state & 0xffffffu) / 16777215.0;
        };

        for (int i = 0; i < 20; ++i)
        {
            Controls c;

            for (auto& v : c.v)
                v = next01();

            // Attack and Early/Late are at zero in most real use; half the
            // scatter keeps them there so the search does not start biased
            // toward exotic settings.
            if (i % 2 == 0) c.v[4] = 0.0;
            if (i % 4 == 0) c.v[2] = 0.0;

            starts.push_back ({ c, evaluate (c) });
        }

        std::sort (starts.begin(), starts.end(),
                   [] (const Scored& a, const Scored& b) { return a.d.total < b.d.total; });

        for (int s = 0; s < 2; ++s)
        {
            Controls current = starts[static_cast<size_t> (s)].c;
            Distance currentD = starts[static_cast<size_t> (s)].d;

            double step = 0.16;
            int budget = 110;

            while (step >= 0.019 && budget > 0)
            {
                bool improved = false;

                for (int k = 0; k < kControls && budget > 0; ++k)
                    for (double direction : { 1.0, -1.0 })
                    {
                        Controls trial = current;
                        trial.v[k] = std::clamp (trial.v[k] + direction * step, 0.0, 1.0);

                        if (trial.v[k] == current.v[k])
                            continue;

                        const auto d = evaluate (trial);
                        --budget;

                        if (d.total < currentD.total)
                        {
                            current = trial;
                            currentD = d;
                            improved = true;
                        }
                    }

                if (! improved)
                    step *= 0.5;
            }

            if (currentD.total < result.distance.total)
            {
                result.distance = currentD;
                result.best = current;
            }
        }

        return result;
    }

    [[nodiscard]] std::string describe (const Controls& c)
    {
        const auto u = toUser (c);

        return "Size " + fx (u.size * 100.0, 0) + "  Decay " + fx (u.decay, 2)
             + " s  E/L " + fx (u.earlyLate * 100.0, 0) + "  EDiff " + fx (u.earlyDiff * 100.0, 0)
             + "  Atk " + fx (u.attack * 100.0, 0) + "  Mod " + fx (u.modDepth * 100.0, 0);
    }

    /** Runs jobs across the machine's cores. Each job is self-contained. */
    template <typename Job>
    void runParallel (size_t count, Job&& job)
    {
        const unsigned int workers =
            std::max (1u, std::min<unsigned int> (std::thread::hardware_concurrency(), 16u));

        std::atomic<size_t> next { 0 };
        std::vector<std::thread> pool;

        for (unsigned int w = 0; w < workers; ++w)
            pool.emplace_back ([&]
            {
                for (;;)
                {
                    const size_t i = next.fetch_add (1);

                    if (i >= count)
                        return;

                    job (i);
                }
            });

        for (auto& t : pool)
            t.join();
    }

    // =======================================================================
    //  Operating points
    // =======================================================================
    struct Point { const char* label; double size, decay; };

    constexpr Point kGrid[9]
    {
        { "small / short",  0.20, 0.6 }, { "small / medium",  0.20, 2.5 }, { "small / long",  0.20, 6.0 },
        { "medium / short", 0.60, 0.6 }, { "medium / medium", 0.60, 2.5 }, { "medium / long", 0.60, 6.0 },
        { "large / short",  0.90, 0.6 }, { "large / medium",  0.90, 2.5 }, { "large / long",  0.90, 6.0 },
    };

    constexpr double kEarlyLateSteps[3] { 0.0, 0.5, 0.9 };

    [[nodiscard]] User userAt (const Point& p, double earlyLate)
    {
        User u;
        u.size = p.size;
        u.decay = p.decay;
        u.earlyLate = earlyLate;
        return u;
    }

    // =======================================================================
    //  Small analysis helpers
    // =======================================================================
    [[nodiscard]] std::vector<double> tone (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kRate);
        std::vector<double> x (n);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kRate;
            const double fade = std::min (1.0, std::min (t, seconds - t) / 0.02);

            x[i] = 0.25 * fade * std::sin (2.0 * 3.14159265358979323846 * hz * t);
        }

        return x;
    }

    [[nodiscard]] double monoFoldDb (const Stereo& x)
    {
        double total = 0.0, mono = 0.0;

        for (size_t n = 0; n < x.l.size(); ++n)
        {
            total += x.l[n] * x.l[n] + x.r[n] * x.r[n];

            const double m = 0.5 * (x.l[n] + x.r[n]);
            mono += 2.0 * m * m;
        }

        return total > 0.0 ? dB (mono / total) : 0.0;
    }

    /** RT60 of one band: an RBJ band-pass, then the Schroeder fit. */
    [[nodiscard]] double bandRt60 (const Stereo& ir, double hz)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * hz / kRate;
        const double alpha = std::sin (w0) / (2.0 * 2.0);
        const double a0 = 1.0 + alpha;

        const double b0 = alpha / a0, b2 = -alpha / a0;
        const double a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;

        std::vector<double> e (ir.l.size(), 0.0);

        for (const auto* channel : { &ir.l, &ir.r })
        {
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

            for (size_t n = 0; n < channel->size(); ++n)
            {
                const double x = (*channel)[n];
                const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;

                x2 = x1; x1 = x;
                y2 = y1; y1 = y;

                e[n] += y * y;
            }
        }

        return rt60FromEnergy (e);
    }

    /** Fine modal prominence of a late segment: how far the 95th-percentile
        bin stands above the median of its own third-octave neighbourhood. */
    [[nodiscard]] double modalProminenceDb (const Stereo& ir)
    {
        constexpr size_t fftSize = 32768;

        const size_t start = msToSamples (250.0);

        if (ir.l.size() < start + fftSize)
            return 0.0;

        std::vector<std::complex<double>> x (fftSize);

        for (size_t n = 0; n < fftSize; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                                   * static_cast<double> (n) / (fftSize - 1));
            x[n] = ir.l[start + n] * w;
        }

        fft (x);

        const size_t low  = binForFrequency (300.0,  fftSize, kRate);
        const size_t high = binForFrequency (3000.0, fftSize, kRate);

        std::vector<double> mag (high + 400);

        for (size_t k = 0; k < mag.size(); ++k)
            mag[k] = 20.0 * std::log10 (std::max (1.0e-30, std::abs (x[k])));

        std::vector<double> excess;
        std::vector<double> window;

        for (size_t k = low; k < high; k += 3)
        {
            const size_t half = std::max<size_t> (8, static_cast<size_t> (k * 0.115));

            window.assign (mag.begin() + static_cast<long long> (k - half),
                           mag.begin() + static_cast<long long> (k + half));

            std::nth_element (window.begin(), window.begin() + static_cast<long long> (half),
                              window.end());

            excess.push_back (mag[k] - window[half]);
        }

        std::nth_element (excess.begin(),
                          excess.begin() + static_cast<long long> (excess.size() * 95 / 100),
                          excess.end());

        return excess[excess.size() * 95 / 100];
    }

    [[nodiscard]] size_t lineBufferSamples (double capacityMs, double rate)
    {
        // FdnCore::prepare, FdnDelayLine::prepare and DelayLine::prepareSamples,
        // restated: +2, +1, +4, then the next power of two.
        const auto capacity = static_cast<size_t> (std::ceil (capacityMs * 0.001 * rate)) + 2;

        size_t rounded = 1;

        while (rounded < capacity + 1 + 4)
            rounded <<= 1;

        return rounded;
    }
}

void runPhase12ModeFamilyTests()
{
    const auto& modes = candidates();

    // =======================================================================
    beginSuite ("PHASE 12.2.1 - research access and the candidate data");
    // =======================================================================
    {
        info ("  WHAT HAD TO BE REACHED, AND HOW. Nothing in Source/ changed.");
        info ("");
        info ("    diffuser configuration   an external StereoDiffusionNetwork, the");
        info ("                             production class, in place of the one");
        info ("                             StereoFdn keeps private. setStageCount and");
        info ("                             setDelayScale are public on it.");
        info ("    FDN delay configuration  FdnCore<8> directly, the production class,");
        info ("                             prepared with the Mode's own capacity.");
        info ("                             StereoFdn fixes capacity from the shipping");
        info ("                             set, which a longer Mode cannot fit.");
        info ("    ER pattern and timing    er::Pattern, already data.");
        info ("    ER channel geometry      THE ONE RE-IMPLEMENTATION. The production");
        info ("                             schedule builder has no seam for it, so");
        info ("                             the rig restates that function with one");
        info ("                             added parameter.");
        info ("    spatial scale            the pattern's own milliseconds.");
        info ("    injection / readout      restated arithmetic of");
        info ("                             StereoFdn::processSample; shipping vectors");
        info ("                             in every Mode.");
        info ("    allpass sections         FdnCore::setAllpassSectionsPerLine, public.");
        info ("    wet trim                 mapping::tankPowerGain's formula, given the");
        info ("                             delay set as an argument.");
        info ("");
        info ("  NO PROCESSING STAGE WAS ADDED. Stage 1's claim holds for");
        info ("  INSTANTIATING the candidates. Whether they are good enough");
        info ("  without new DSP is what the rest of this file measures.");
        info ("");

        info ("    " + pad ("mode", 10) + pad ("delay set, ms", 62) + pad ("sum", 9) + "max/min");

        bool allFound = true;

        for (const auto& m : modes)
        {
            std::string line;
            double sum = 0.0;

            for (double v : m.delaysMs)
            {
                line += fx (v, 2) + " ";
                sum += v;
            }

            allFound = allFound && sum > 1.0;

            info ("    " + pad (m.name, 10) + pad (line, 62) + pad (fx (sum, 1), 9)
                  + fx (m.delaysMs[7] / m.delaysMs[0], 2));
        }

        {
            const auto& s = hallSibling();
            std::string line;

            for (double v : s.delaysMs)
                line += fx (v, 2) + " ";

            info ("    " + pad ("(sibling)", 10) + pad (line, 62)
                  + pad (fx (std::accumulate (s.delaysMs.begin(), s.delaysMs.end(), 0.0), 1), 9)
                  + fx (s.delaysMs[7] / s.delaysMs[0], 2));
        }

        info ("");
        info ("    " + pad ("mode", 10) + pad ("diffuser", 16) + pad ("sections", 10)
              + pad ("mod depth/rate", 16) + pad ("ER events", 11) + pad ("window", 9)
              + pad ("env", 6) + pad ("alpha", 7) + "L/R spread");

        for (const auto& m : modes)
            info ("    " + pad (m.name, 10)
                  + pad (std::to_string (m.diffuserStages) + " x " + fx (m.diffuserScale, 2), 16)
                  + pad (std::to_string (m.allpassSections), 10)
                  + pad (fx (m.modDepthFactor, 2) + " / " + fx (m.modRateFactor, 2), 16)
                  + pad (std::to_string (m.er.events), 11)
                  + pad (fx (m.er.windowMs, 0) + " ms", 9)
                  + pad (fx (m.er.envelopeDecay, 1), 6)
                  + pad (fx (m.er.sizeAlpha, 2), 7)
                  + fx (m.erChannelSpread, 2));

        info ("");
        info ("    Every non-Hall delay set is the output of the same directed");
        info ("    search that chose the shipping one, aimed at the middle of its");
        info ("    Stage 1 range. The sibling is a SECOND set searched to Hall's");
        info ("    own sum and spread: the same family by construction, and the");
        info ("    yardstick for what 'the same family' measures as.");

        check (allFound, "the delay-set search found a usable set for every Mode");

        bool configured = true;

        for (const auto& m : modes)
            for (double size : { 0.0, 0.6, 1.0 })
            {
                User u;
                u.size = size;
                configured = configured && std::make_unique<Rig> (m, u)->isConfigured();
            }

        check (configured, "every candidate configures at Size 0, 60 and 100 % with no "
                           "refusal");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.2 - the Hall control");
    // =======================================================================
    {
        /*
            If the rig, given the shipping data, is not the shipping engine,
            every number after this section describes the rig.
        */
        const auto& hallMode = modes[hall];
        const auto length = static_cast<size_t> (kRate * 2.0);

        bool data = true;

        for (size_t i = 0; i < 8; ++i)
            data = data && hallMode.delaysMs[i] == fdn::delaysMs[i];

        const er::Pattern shipped {};

        data = data && hallMode.er.events == shipped.events
                    && hallMode.er.windowMs == shipped.windowMs
                    && hallMode.er.envelopeDecay == shipped.envelopeDecay
                    && hallMode.er.sizeAlpha == shipped.sizeAlpha
                    && hallMode.er.cellOccupancy == shipped.cellOccupancy
                    && hallMode.er.earliestFraction == shipped.earliestFraction
                    && hallMode.diffuserStages == 4 && hallMode.diffuserScale == 1.0
                    && hallMode.allpassSections == allpassMod::defaultSectionsPerLine
                    && hallMode.modDepthFactor == 1.0 && hallMode.modRateFactor == 1.0
                    && hallMode.erChannelSpread == 1.0;

        check (data, "the Hall candidate is the shipping data, field for field");

        struct Case { const char* label; User u; };

        std::vector<Case> cases;

        cases.push_back ({ "defaults (late only)", User {} });

        { User u; u.earlyLate = 0.5;                         cases.push_back ({ "Early/Late 50", u }); }
        { User u; u.earlyLate = 1.0;                         cases.push_back ({ "Early/Late 100 (ER only)", u }); }
        { User u; u.size = 0.15; u.decay = 0.4;              cases.push_back ({ "small, short", u }); }
        { User u; u.size = 1.0; u.decay = 8.0; u.earlyLate = 0.3; cases.push_back ({ "large, long, E/L 30", u }); }
        { User u; u.attack = 0.4; u.earlyDiff = 0.2;         cases.push_back ({ "Attack 40, Early Diff 20", u }); }
        { User u; u.modDepth = 0.0;                          cases.push_back ({ "Mod Depth 0", u }); }
        { User u; u.modDepth = 1.0; u.modRate = 1.5;         cases.push_back ({ "Mod Depth 100, Rate 1.5", u }); }

        info ("  A. Against ONE production bank (StereoFdn), with the production");
        info ("     EarlyReflections, EarlyLateBalance, AttackInjection, Size");
        info ("     compensation and wet trim around it:");
        info ("");
        info ("    " + pad ("case", 30) + pad ("identical", 12) + "worst difference");

        bool allIdentical = true;

        for (const auto& c : cases)
        {
            const auto reference = productionBank (c.u, length);

            auto rig = std::make_unique<Rig> (hallMode, c.u, Trim::own);
            const auto mine = rig->render (std::vector<double> (1, 1.0), length);

            const bool same = identical (reference, mine);
            allIdentical = allIdentical && same;

            info ("    " + pad (c.label, 30) + pad (same ? "yes" : "NO", 12)
                  + fx (worstDifference (reference, mine) * 1.0e12, 3) + "e-12");
        }

        check (allIdentical, "the rig's Hall is byte-identical to the production classes "
                             "at every case, including the early layer and the balance");

        info ("");
        info ("  B. Against the full dual-bank production tank, after its opening");
        info ("     Size transition has settled on silence. The rig is given the");
        info ("     same number of silent samples so both sit on the same");
        info ("     modulation phase:");
        info ("");

        bool tankIdentical = true;
        double tankWorst = 0.0;

        for (const auto& u : { User {}, cases[3].u })
        {
            size_t settle = 0;
            const auto reference = productionTank (u, length, settle);

            auto rig = std::make_unique<Rig> (hallMode, u, Trim::own);
            const auto mine = rig->render (std::vector<double> (1, 1.0), length, settle);

            const bool same = identical (reference, mine);
            tankIdentical = tankIdentical && same;
            tankWorst = std::max (tankWorst, worstDifference (reference, mine));

            info ("    Size " + fx (u.size * 100.0, 0) + " %, Decay " + fx (u.decay, 1)
                  + " s: settled after " + std::to_string (settle) + " samples, "
                  + (same ? "byte-identical" : "NOT identical, worst "
                                               + fx (worstDifference (reference, mine) * 1.0e9, 4)
                                               + "e-9"));
        }

        check (tankIdentical, "and byte-identical to the settled dual-bank production tank");

        info ("");
        info ("  C. What follows from A and B without a further measurement. RT60,");
        info ("     the ER layer, the late response, stereo behaviour and the");
        info ("     spectrum are all functions of the samples, and the samples are");
        info ("     the same samples. Width is a mid/side operation on the wet");
        info ("     signal after this point and no Mode touches it.");
        info ("");
        info ("     The processor itself is not linked here - this target does not");
        info ("     link JUCE - so 'the rig equals the plugin' rests on two links:");
        info ("     this section, and the state suite's 12-of-12 legacy byte");
        info ("     comparison of the processor, which is unchanged because Source/");
        info ("     is unchanged.");

        // Determinism: the same render twice, from two fresh rigs.
        const auto once  = impulseResponse (hallMode, User {}, 1.0);
        const auto twice = impulseResponse (hallMode, User {}, 1.0);

        check (identical (once, twice), "a render is state-independent: two fresh rigs "
                                        "produce the same bytes");

        // The trim generalisation must not have moved Hall.
        bool trimSame = true;

        for (double decay : { 0.1, 0.6, 2.5, 20.0 })
            for (double size : { 0.0, 0.6, 1.0 })
                trimSame = trimSame
                        && ownWetTrim (hallMode.delaysMs.data(), decay, mapping::sizeScaleFor (size))
                           == mapping::previewWetTrim (decay, mapping::sizeScaleFor (size));

        check (trimSame, "the delay-set-aware wet trim returns the shipping trim exactly "
                         "for the shipping set");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.3 - the yardstick: floor, invariance, optimiser");
    // =======================================================================

    double floorDistance = 0.0;

    {
        info ("  PRE-REGISTERED, in Phase12ModeRig.h, before any candidate was");
        info ("  scored: six feature families, each difference divided by a JND");
        info ("  or a stated proxy, RMS within a family, RMS across families.");
        info ("");
        info ("      REPRODUCED   residual <= max (1.5, 1.25 x floor)");
        info ("      DISTINCT     residual >= max (3.0, 2.00 x floor)");
        info ("      WEAK         in between");
        info ("");
        info ("  THE FLOOR - Hall against things that are Hall by construction:");
        info ("");

        const User defaults {};
        const auto reference = featuresOf (modes[hall], defaults);

        struct Null { std::string label; Distance d; };
        std::vector<Null> nulls;

        for (double nudge : { -0.02, 0.02 })
        {
            User u;
            u.size = defaults.size + nudge;
            nulls.push_back ({ "Hall, Size " + fx (u.size * 100.0, 0) + " %",
                               distance (reference, featuresOf (modes[hall], u)) });
        }

        {
            User u;
            u.modDepth = 0.34;
            nulls.push_back ({ "Hall, Mod Depth 34 %",
                               distance (reference, featuresOf (modes[hall], u)) });
        }

        nulls.push_back ({ "Hall sibling delay set",
                           distance (reference, featuresOf (hallSibling(), defaults)) });

        for (const auto& p : { kGrid[0], kGrid[8] })
        {
            const auto u = userAt (p, 0.0);

            nulls.push_back ({ std::string ("sibling, ") + p.label,
                               distance (featuresOf (modes[hall], u),
                                         featuresOf (hallSibling(), u)) });
        }

        std::string header = "    " + pad ("same-family pair", 28);

        for (const char* name : kFamilyNames)
            header += pad (name, 16);

        info (header + "TOTAL");

        for (const auto& n : nulls)
        {
            std::string line = "    " + pad (n.label, 28);

            for (double v : n.d.family)
                line += pad (fx (v, 2), 16);

            info (line + fx (n.d.total, 2));

            floorDistance = std::max (floorDistance, n.d.total);
        }

        info ("");
        info ("    floor (the largest of these)   " + fx (floorDistance, 2));
        info ("    REPRODUCED at or below         " + fx (reproducedThreshold (floorDistance), 2));
        info ("    DISTINCT at or above           " + fx (distinctThreshold (floorDistance), 2));
        info ("");
        info ("    The floor is not zero and is not meant to be. It is how far");
        info ("    apart two realisations of ONE family sit when they are read");
        info ("    off single impulse responses. A Mode has to clear a multiple");
        info ("    of it, which is the difference between 'measurably different'");
        info ("    and 'a different family'.");

        // Level invariance: loudness must not be able to enter the distance.
        {
            auto ir = impulseResponse (modes[hall], defaults, irSecondsFor (defaults.decay));
            const auto before = analyse (ir);

            for (auto& v : ir.l) v *= 3.7;
            for (auto& v : ir.r) v *= 3.7;

            const auto scaled = distance (before, analyse (ir));

            info ("");
            info ("    the same response 11.4 dB louder: distance " + fx (scaled.total, 6));

            check (scaled.total < 1.0e-6, "the distance is invariant to level: loudness "
                                          "cannot masquerade as a Mode difference");
        }

        // The optimiser, on a target it can reach.
        {
            User moved;
            moved.size = 0.33;
            moved.decay = 1.4;
            moved.earlyLate = 0.30;
            moved.earlyDiff = 0.45;
            moved.attack = 0.20;
            moved.modDepth = 0.55;

            const auto target = featuresOf (modes[hall], moved);
            const auto found  = mimic (target, modes[hall], toControls (defaults), 0xC0FFEEu);

            info ("");
            info ("  THE OPTIMISER, ON A TARGET KNOWN TO EXIST. Hall at moved");
            info ("  controls, searched for from Hall's defaults:");
            info ("");
            info ("    target     " + describe (toControls (moved)));
            info ("    recovered  " + describe (found.best));
            info ("    residual   " + fx (found.distance.total, 2) + "  after "
                  + std::to_string (found.evaluations) + " evaluations");
            info ("");
            info ("    Starting distance was "
                  + fx (distance (target, featuresOf (modes[hall], defaults)).total, 2) + ".");

            check (found.distance.total <= reproducedThreshold (floorDistance),
                   "the search recovers a reachable target to within the REPRODUCED "
                   "threshold - so a large residual elsewhere is not a failed search");
        }
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.4 - same-knob distance, over Size x Decay x Early/Late");
    // =======================================================================

    // [earlyLate][point][mode]
    std::vector<Features> grid (3 * 9 * modeCount);

    auto at = [&] (int el, int point, int mode) -> Features&
    {
        return grid[static_cast<size_t> ((el * 9 + point) * modeCount + mode)];
    };

    {
        runParallel (grid.size(), [&] (size_t i)
        {
            const int mode  = static_cast<int> (i % modeCount);
            const int point = static_cast<int> ((i / modeCount) % 9);
            const int el    = static_cast<int> (i / (modeCount * 9));

            grid[i] = featuresOf (modes[static_cast<size_t> (mode)],
                                  userAt (kGrid[point], kEarlyLateSteps[el]));
        });

        info ("  Every Mode at the SAME user settings. This is the weaker");
        info ("  question - 'do they differ when nothing else is touched' - and");
        info ("  it is the one the user's first impression answers. The stronger");
        info ("  question is section 12.2.5.");
        info ("");
        info ("  THE FEATURES AT THE PRODUCT DEFAULTS (Size 60, Decay 2.5, E/L 0):");
        info ("");
        info ("    " + pad ("mode", 10) + pad ("t10", 8) + pad ("t50", 8) + pad ("tPeak", 8)
              + pad ("eCent", 8) + pad ("C50", 8) + pad ("C80", 8) + pad ("NED.5", 8)
              + pad ("NED.9", 8) + pad ("NED@10", 8) + pad ("corr0", 8) + pad ("RT60", 8)
              + pad ("Ts", 8) + "recur");

        for (int m = 0; m < modeCount; ++m)
        {
            const auto& f = at (0, 4, m);

            info ("    " + pad (modes[static_cast<size_t> (m)].name, 10)
                  + pad (fx (f.t10, 1), 8) + pad (fx (f.t50, 1), 8) + pad (fx (f.tPeak, 1), 8)
                  + pad (fx (f.earlyCentroid, 1), 8) + pad (fx (f.c50, 1), 8)
                  + pad (fx (f.c80, 1), 8) + pad (fx (f.tNed50, 0), 8) + pad (fx (f.tNed90, 0), 8)
                  + pad (fx (f.nedAt10, 2), 8) + pad (fx (f.corr[0], 2), 8)
                  + pad (fx (f.rt60, 2), 8) + pad (fx (f.centreTime, 0), 8)
                  + fx (f.recurrence, 2));
        }

        info ("");
        info ("    times in ms, clarity in dB. NED.5 / NED.9: when normalised echo");
        info ("    density first reaches 0.5 / 0.9. corr0: L/R correlation, 0-50 ms.");

        for (int el = 0; el < 3; ++el)
        {
            info ("");
            info ("  EARLY/LATE " + fx (kEarlyLateSteps[el] * 100.0, 0)
                  + " - same-knob distance, the MINIMUM and the median over the nine");
            info ("  Size x Decay points (a distinction is only as strong as its");
            info ("  weakest operating point):");
            info ("");

            std::string header = "    " + pad ("", 10);

            for (int b = 0; b < modeCount; ++b)
                header += pad (modes[static_cast<size_t> (b)].name, 16);

            info (header);

            for (int a = 0; a < modeCount; ++a)
            {
                std::string line = "    " + pad (modes[static_cast<size_t> (a)].name, 10);

                for (int b = 0; b < modeCount; ++b)
                {
                    if (a == b)
                    {
                        line += pad ("-", 16);
                        continue;
                    }

                    std::vector<double> d;

                    for (int p = 0; p < 9; ++p)
                        d.push_back (distance (at (el, p, a), at (el, p, b)).total);

                    std::sort (d.begin(), d.end());

                    line += pad (fx (d.front(), 1) + " / " + fx (d[4], 1), 16);
                }

                info (line);
            }
        }

        info ("");
        info ("    DISTINCT threshold " + fx (distinctThreshold (floorDistance), 2)
              + ", REPRODUCED threshold " + fx (reproducedThreshold (floorDistance), 2) + ".");
        info ("    These are distances with every knob held still. They say the");
        info ("    Modes sound different out of the box. They do NOT say one");
        info ("    cannot be dialled into another; that is the next section.");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.5 - the impostor search");
    // =======================================================================

    struct Target { const char* label; User u; bool everyPair; };

    std::vector<Target> targets;

    targets.push_back ({ "defaults, E/L 0",       userAt (kGrid[4], 0.0), true });
    targets.push_back ({ "defaults, E/L 50",      userAt (kGrid[4], 0.5), true });
    targets.push_back ({ "small / short, E/L 0",  userAt (kGrid[0], 0.0), true });
    targets.push_back ({ "large / long, E/L 0",   userAt (kGrid[8], 0.0), false });

    struct Job
    {
        int target = 0, a = 0, b = 0;
        SearchResult result;
        double sameKnob = 0.0;
    };

    std::vector<Job> jobs;

    // [target][a][b] -> index into jobs, or -1
    int jobIndex[4][modeCount][modeCount];

    for (auto& t : jobIndex)
        for (auto& a : t)
            for (auto& b : a)
                b = -1;

    {
        // The large/long target is run only for the pairs the brief names as
        // adversarial, because its impulse responses are the long ones.
        auto adversarial = [] (int a, int b)
        {
            return (a == chamber && (b == room || b == hall))
                || (a == plate   && (b == hall || b == chamber))
                || (a == ambient && b == hall)
                || (a == room    && b == hall);
        };

        for (int t = 0; t < static_cast<int> (targets.size()); ++t)
            for (int a = 0; a < modeCount; ++a)
                for (int b = 0; b < modeCount; ++b)
                {
                    if (a == b)
                        continue;

                    if (! targets[static_cast<size_t> (t)].everyPair && ! adversarial (a, b))
                        continue;

                    jobIndex[t][a][b] = static_cast<int> (jobs.size());
                    jobs.push_back ({ t, a, b, {}, 0.0 });
                }

        runParallel (jobs.size(), [&] (size_t i)
        {
            auto& job = jobs[i];

            const auto& target = targets[static_cast<size_t> (job.target)];
            const auto features = featuresOf (modes[static_cast<size_t> (job.a)], target.u);

            job.sameKnob = distance (features,
                                     featuresOf (modes[static_cast<size_t> (job.b)], target.u)).total;

            job.result = mimic (features, modes[static_cast<size_t> (job.b)],
                                toControls (target.u),
                                0x9E3779B9u ^ static_cast<unsigned int> (i * 2654435761u));
        });

        info ("  For every ordered pair: the TARGET Mode held at a setting, and");
        info ("  the IMPOSTOR Mode's six user controls searched for the closest");
        info ("  response. Mode-owned data is never touched by the search.");
        info ("");
        info ("  A residual is an UPPER BOUND on how far apart the two really");
        info ("  are. A better optimiser could only lower it. So every bias left");
        info ("  in this table runs in the Modes' favour, and a pass here is");
        info ("  weaker evidence than a failure.");

        for (int t = 0; t < static_cast<int> (targets.size()); ++t)
        {
            info ("");
            info ("  TARGET: " + std::string (targets[static_cast<size_t> (t)].label)
                  + (targets[static_cast<size_t> (t)].everyPair ? ""
                                                                : "   (adversarial pairs only)"));
            info ("  rows = the Mode being imitated, columns = the impostor.");
            info ("  each cell: residual after the search (same-knob distance before).");
            info ("");

            std::string header = "    " + pad ("", 10);

            for (int b = 0; b < modeCount; ++b)
                header += pad (modes[static_cast<size_t> (b)].name, 16);

            info (header);

            for (int a = 0; a < modeCount; ++a)
            {
                std::string line = "    " + pad (modes[static_cast<size_t> (a)].name, 10);

                for (int b = 0; b < modeCount; ++b)
                {
                    const int index = jobIndex[t][a][b];

                    if (index < 0)
                    {
                        line += pad (a == b ? "-" : ".", 16);
                        continue;
                    }

                    const auto& job = jobs[static_cast<size_t> (index)];

                    line += pad (fx (job.result.distance.total, 2) + " ("
                                 + fx (job.sameKnob, 1) + ")", 16);
                }

                info (line);
            }
        }

        info ("");
        info ("  THE BEST IMPOSTOR FOR EVERY ORDERED PAIR, at the target where it");
        info ("  came closest:");
        info ("");
        info ("    " + pad ("imitated <- impostor", 24) + pad ("residual", 10)
              + pad ("target", 24) + "impostor settings");

        for (int a = 0; a < modeCount; ++a)
            for (int b = 0; b < modeCount; ++b)
            {
                if (a == b)
                    continue;

                const Job* best = nullptr;

                for (int t = 0; t < static_cast<int> (targets.size()); ++t)
                {
                    const int index = jobIndex[t][a][b];

                    if (index < 0)
                        continue;

                    const auto& job = jobs[static_cast<size_t> (index)];

                    if (best == nullptr || job.result.distance.total < best->result.distance.total)
                        best = &job;
                }

                if (best == nullptr)
                    continue;

                info ("    " + pad (modes[static_cast<size_t> (a)].name + " <- "
                                    + modes[static_cast<size_t> (b)].name, 24)
                      + pad (fx (best->result.distance.total, 2), 10)
                      + pad (targets[static_cast<size_t> (best->target)].label, 24)
                      + describe (best->result.best));
            }

        info ("");
        info ("  WHICH FAMILIES OF FEATURE SURVIVE THE BEST IMPOSTOR. For each");
        info ("  Mode, its closest impostor anywhere, and that residual split by");
        info ("  family. A family near zero is one the impostor matched.");
        info ("");

        std::string header = "    " + pad ("imitated", 10) + pad ("by", 10);

        for (const char* name : kFamilyNames)
            header += pad (name, 16);

        info (header + "TOTAL");

        const double reproduced = reproducedThreshold (floorDistance);
        const double distinct   = distinctThreshold (floorDistance);

        // The verdict for a Mode is set by its closest impostor at ANY target.
        for (int a = 0; a < modeCount; ++a)
        {
            const Job* closest = nullptr;

            for (const auto& job : jobs)
                if (job.a == a
                    && (closest == nullptr
                        || job.result.distance.total < closest->result.distance.total))
                    closest = &job;

            if (closest == nullptr)
                continue;

            std::string line = "    " + pad (modes[static_cast<size_t> (a)].name, 10)
                             + pad (modes[static_cast<size_t> (closest->b)].name, 10);

            for (double v : closest->result.distance.family)
                line += pad (fx (v, 2), 16);

            info (line + fx (closest->result.distance.total, 2));
        }

        info ("");
        info ("  VERDICTS. REPRODUCED at or below " + fx (reproduced, 2)
              + ", DISTINCT at or above " + fx (distinct, 2) + ".");
        info ("");

        for (int a = 0; a < modeCount; ++a)
        {
            double closest = 1.0e9;
            int by = -1, where = -1;

            for (const auto& job : jobs)
                if (job.a == a && job.result.distance.total < closest)
                {
                    closest = job.result.distance.total;
                    by = job.b;
                    where = job.target;
                }

            const char* verdict = closest <= reproduced ? "FAMILY NOT PROVEN"
                                : closest >= distinct   ? "DISTINCT"
                                                        : "WEAK";

            info ("    " + pad (modes[static_cast<size_t> (a)].name, 10) + pad (verdict, 20)
                  + "closest impostor " + modes[static_cast<size_t> (by)].name + " at "
                  + fx (closest, 2) + "  (" + targets[static_cast<size_t> (where)].label + ")");

            check (closest > reproduced,
                   modes[static_cast<size_t> (a)].name
                   + " cannot be reproduced by another Mode's user controls");

            check (closest >= distinct,
                   modes[static_cast<size_t> (a)].name
                   + " clears the DISTINCT threshold against its closest impostor at "
                     "every target");
        }

        // The brief's named requirement, stated on its own.
        {
            double worst = 1.0e9;

            for (int t : { 0, 2, 3 })
            {
                const int index = jobIndex[t][room][hall];

                if (index >= 0)
                    worst = std::min (worst, jobs[static_cast<size_t> (index)].result.distance.total);
            }

            info ("");
            info ("  ROOM AT EARLY/LATE 0, AGAINST HALL. The target has no early");
            info ("  layer at all; Hall may use every control it has, including its");
            info ("  own early layer. Closest Hall gets: " + fx (worst, 2));

            check (worst >= distinct,
                   "Room at Early/Late 0 is not reachable from Hall - its late "
                   "geometry carries the identity");
        }
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.6 - energy, loudness and what calibration is needed");
    // =======================================================================
    {
        /*
            Stage 1 said every Mode needs its own calibration tables. That was
            an assumption. The measurement below asks the narrower question
            the brief asks: is ONE shared calibration valid, and if not, how
            little is needed.
        */
        constexpr double sizes[3]  { 0.0, 0.5, 1.0 };
        constexpr double decays[3] { 0.5, 2.5, 10.0 };

        // [trim][mode][decay][size] late energy
        double energy[2][modeCount][3][3] {};

        runParallel (2 * modeCount * 9, [&] (size_t i)
        {
            const int s = static_cast<int> (i % 3);
            const int d = static_cast<int> ((i / 3) % 3);
            const int m = static_cast<int> ((i / 9) % modeCount);
            const int t = static_cast<int> (i / (9 * modeCount));

            User u;
            u.size = sizes[s];
            u.decay = decays[d];

            energy[t][m][d][s] = energyOf (impulseResponse (
                modes[static_cast<size_t> (m)], u, std::clamp (0.9 * u.decay + 0.5, 1.0, 9.5),
                t == 0 ? Trim::shared : Trim::own));
        });

        for (int t = 0; t < 2; ++t)
        {
            info (t == 0
                    ? "  A. ONE SHARED CALIBRATION - Hall's wet trim and Hall's Size table,"
                    : "  B. THE SAME TRIM FORMULA GIVEN EACH MODE'S OWN DELAY SET, and");
            info (t == 0
                    ? "     applied to every Mode. Late energy relative to Hall, dB:"
                    : "     still Hall's Size table. Late energy relative to Hall, dB:");
            info ("");
            info ("    " + pad ("mode", 10) + pad ("Decay", 8) + pad ("Size 0", 10)
                  + pad ("Size 50", 10) + "Size 100");

            double worstAny = 0.0;

            for (int m = 0; m < modeCount; ++m)
            {
                if (m == hall)
                    continue;

                for (int d = 0; d < 3; ++d)
                {
                    std::string line = "    " + pad (d == 0 ? modes[static_cast<size_t> (m)].name : "", 10)
                                     + pad (fx (decays[d], 1) + " s", 8);

                    for (int s = 0; s < 3; ++s)
                    {
                        const double delta = dB (energy[t][m][d][s] / energy[t][hall][d][s]);
                        worstAny = std::max (worstAny, std::abs (delta));
                        line += pad (fx (delta, 2), 10);
                    }

                    info (line);
                }
            }

            info ("");
            info ("    worst departure from Hall: " + fx (worstAny, 2) + " dB");
            info ("");

            if (t == 0)
                check (worstAny <= 1.0, "one shared calibration keeps every Mode within "
                                        "1 dB of Hall's late energy");
            else
                check (worstAny <= 1.0, "the trim formula on each Mode's own delay set, "
                                        "with Hall's Size table, keeps every Mode within 1 dB");
        }

        info ("  WHAT THIS MEANS FOR BAL-4. The early layer's energy is fixed by");
        info ("  L2 normalisation - exactly 1 per channel for every pattern - so");
        info ("  the K that balances the two branches depends on the Mode ONLY");
        info ("  through the late energy above. A Mode whose late energy matches");
        info ("  Hall's can use Hall's K; the K error is half the dB figure.");
        info ("");

        // The split the brief asks for, at the defaults, at a mid balance.
        info ("  C. THE ENERGY SPLIT at the defaults with Early/Late 50, own trim:");
        info ("");
        info ("    " + pad ("mode", 10) + pad ("total dB", 11) + pad ("0-80 ms dB", 13)
              + pad ("80 ms+ dB", 12) + pad ("early share", 13) + "rms dB (1 s)");

        const double reference = [&]
        {
            User u;
            u.earlyLate = 0.5;
            return energyOf (impulseResponse (modes[hall], u, 3.0));
        }();

        for (int m = 0; m < modeCount; ++m)
        {
            User u;
            u.earlyLate = 0.5;

            const auto ir = impulseResponse (modes[static_cast<size_t> (m)], u, 3.0);

            double early = 0.0, late = 0.0, firstSecond = 0.0;

            for (size_t n = 0; n < ir.l.size(); ++n)
            {
                const double e = ir.l[n] * ir.l[n] + ir.r[n] * ir.r[n];

                (n < msToSamples (80.0) ? early : late) += e;

                if (n < static_cast<size_t> (kRate))
                    firstSecond += e;
            }

            info ("    " + pad (modes[static_cast<size_t> (m)].name, 10)
                  + pad (fx (dB ((early + late) / reference), 2), 11)
                  + pad (fx (dB (early / reference), 2), 13)
                  + pad (fx (dB (late / reference), 2), 12)
                  + pad (fx (100.0 * early / (early + late), 1) + " %", 13)
                  + fx (dB (firstSecond / (2.0 * kRate)), 2));
        }

        info ("");
        info ("    Totals are relative to Hall. The early SHARE is not matched and");
        info ("    must not be: it is part of what makes the Modes different.");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.7 - control semantics per Mode");
    // =======================================================================
    {
        info ("  DECAY MUST MEAN DECAY. Measured RT60 against the control, E/L 0,");
        info ("  Size 60 %:");
        info ("");
        info ("    " + pad ("mode", 10) + pad ("0.6 s", 12) + pad ("2.5 s", 12) + "6.0 s");

        double worstDecayError = 0.0;

        for (int m = 0; m < modeCount; ++m)
        {
            std::string line = "    " + pad (modes[static_cast<size_t> (m)].name, 10);

            for (int p : { 3, 4, 5 })
            {
                const double measured = at (0, p, m).rt60;
                const double error = 100.0 * (measured / kGrid[p].decay - 1.0);

                worstDecayError = std::max (worstDecayError, std::abs (error));
                line += pad (fx (measured, 2) + " (" + fx (error, 0) + "%)", 12);
            }

            info (line);
        }

        // Hall's own error is the reference: the damping shelves pull the
        // broadband figure away from the mid-band one in every Mode alike.
        double worstVersusHall = 0.0;

        for (int m = 0; m < modeCount; ++m)
            for (int p : { 3, 4, 5 })
                worstVersusHall = std::max (worstVersusHall,
                                            std::abs (at (0, p, m).rt60 / at (0, p, hall).rt60 - 1.0));

        info ("");
        info ("    These are BROADBAND figures with the default damping engaged,");
        info ("    so they sit below the control in every Mode including Hall.");
        info ("    What matters for a Mode is its departure from Hall at the same");
        info ("    setting: worst " + fx (100.0 * worstVersusHall, 1) + " %.");

        check (worstVersusHall <= 0.10, "every Mode's RT60 is within 10 % of Hall's at the "
                                        "same Decay - Decay means the same thing in each");

        info ("");
        info ("  SIZE MUST MEAN SIZE. Centre time and early centroid, Decay 2.5 s,");
        info ("  E/L 0, at Size 20 / 60 / 90 %:");
        info ("");
        info ("    " + pad ("mode", 10) + pad ("t50, ms", 26) + "early centroid, ms");

        bool monotone = true;

        for (int m = 0; m < modeCount; ++m)
        {
            const auto& small  = at (0, 1, m);
            const auto& medium = at (0, 4, m);
            const auto& large  = at (0, 7, m);

            monotone = monotone && small.t50 <= medium.t50 && medium.t50 <= large.t50
                                && small.earlyCentroid <= medium.earlyCentroid
                                && medium.earlyCentroid <= large.earlyCentroid;

            info ("    " + pad (modes[static_cast<size_t> (m)].name, 10)
                  + pad (fx (small.t50, 1) + " -> " + fx (medium.t50, 1) + " -> "
                         + fx (large.t50, 1), 26)
                  + fx (small.earlyCentroid, 1) + " -> " + fx (medium.earlyCentroid, 1)
                  + " -> " + fx (large.earlyCentroid, 1));
        }

        check (monotone, "Size is monotone in every Mode: larger is later, never earlier");

        info ("");
        info ("  MODE MUST NOT BE TONE. Band RT60 relative to the 1 kHz band, at");
        info ("  the defaults. If this differed by Mode, a Mode would have");
        info ("  acquired a tonal signature, which belongs to PHASE 13:");
        info ("");
        info ("    " + pad ("mode", 10) + pad ("250 Hz", 10) + pad ("1 kHz", 10)
              + pad ("4 kHz", 10) + "RT 1 kHz, s");

        double worstTilt = 0.0;
        double hallLow = 0.0, hallHigh = 0.0;

        for (int pass = 0; pass < 2; ++pass)
            for (int m = 0; m < modeCount; ++m)
            {
                if ((pass == 0) != (m == hall))
                    continue;

                const auto ir = impulseResponse (modes[static_cast<size_t> (m)], User {}, 3.0);

                const double mid  = bandRt60 (ir, 1000.0);
                const double low  = bandRt60 (ir, 250.0)  / std::max (1.0e-9, mid);
                const double high = bandRt60 (ir, 4000.0) / std::max (1.0e-9, mid);

                if (m == hall)
                {
                    hallLow = low;
                    hallHigh = high;
                }
                else
                {
                    worstTilt = std::max ({ worstTilt, std::abs (low / hallLow - 1.0),
                                            std::abs (high / hallHigh - 1.0) });
                }

                if (pass == 1 && m == room)
                    info ("    " + pad ("Hall", 10) + pad (fx (hallLow, 2), 10) + pad ("1.00", 10)
                          + fx (hallHigh, 2));

                if (m != hall)
                    info ("    " + pad (modes[static_cast<size_t> (m)].name, 10)
                          + pad (fx (low, 2), 10) + pad ("1.00", 10) + pad (fx (high, 2), 10)
                          + fx (mid, 2));
            }

        info ("");
        info ("    worst departure of a band ratio from Hall's: "
              + fx (100.0 * worstTilt, 1) + " %");

        check (worstTilt <= 0.10, "no Mode's spectral decay shape departs from Hall's by "
                                  "more than 10 % - Mode carries no tonal signature");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.8 - Room in the small-space corner");
    // =======================================================================
    {
        /*
            PHASE 11's accepted characteristics are not reopened here. The
            question is narrower: does the Room configuration make them
            MATERIALLY WORSE than the Hall configuration at the same settings.
        */
        struct Corner { const char* label; double size, decay; };

        constexpr Corner corners[3]
        {
            { "Size 15 %, Decay 0.3 s", 0.15, 0.3 },
            { "Size 20 %, Decay 0.6 s", 0.20, 0.6 },
            { "Size 60 %, Decay 2.5 s", 0.60, 2.5 },
        };

        constexpr int tones = 48;

        // [corner][mode] -> fold per tone
        std::vector<double> fold (3 * modeCount * tones, 0.0);

        runParallel (fold.size(), [&] (size_t i)
        {
            const int k = static_cast<int> (i % tones);
            const int m = static_cast<int> ((i / tones) % modeCount);
            const int c = static_cast<int> (i / (tones * modeCount));

            User u;
            u.size = corners[c].size;
            u.decay = corners[c].decay;

            const double hz = 100.0 * std::pow (40.0, (k + 0.37) / tones);

            auto rig = std::make_unique<Rig> (modes[static_cast<size_t> (m)], u);

            fold[i] = monoFoldDb (rig->render (tone (hz, 0.8), static_cast<size_t> (kRate * 0.8)));
        });

        info ("  NARROW-BAND MONO FOLD-DOWN, the PHASE 11 measurement: 48 tones,");
        info ("  100 Hz - 4 kHz, late path only. -3.01 dB is the decorrelated ideal.");
        info ("");
        info ("    " + pad ("setting", 26) + pad ("mode", 10) + pad ("median", 10)
              + pad ("< -6 dB", 10) + pad ("< -9 dB", 10) + "worst");

        double roomFraction[3] {}, hallFraction[3] {}, roomWorst[3] {}, hallWorst[3] {};

        for (int c = 0; c < 3; ++c)
            for (int m = 0; m < modeCount; ++m)
            {
                std::vector<double> v (fold.begin() + (c * modeCount + m) * tones,
                                       fold.begin() + (c * modeCount + m + 1) * tones);

                std::sort (v.begin(), v.end());

                const double below6 = 100.0 * static_cast<double> (
                    std::count_if (v.begin(), v.end(), [] (double x) { return x < -6.0; })) / tones;

                const double below9 = 100.0 * static_cast<double> (
                    std::count_if (v.begin(), v.end(), [] (double x) { return x < -9.0; })) / tones;

                if (m == room) { roomFraction[c] = below6; roomWorst[c] = v.front(); }
                if (m == hall) { hallFraction[c] = below6; hallWorst[c] = v.front(); }

                info ("    " + pad (m == 0 ? corners[c].label : "", 26)
                      + pad (modes[static_cast<size_t> (m)].name, 10)
                      + pad (fx (v[tones / 2], 2), 10) + pad (fx (below6, 1) + " %", 10)
                      + pad (fx (below9, 1) + " %", 10) + fx (v.front(), 2));
            }

        bool noWorse = true;

        for (int c = 0; c < 3; ++c)
            noWorse = noWorse && roomFraction[c] <= hallFraction[c] + 10.0
                              && roomWorst[c] >= hallWorst[c] - 3.0;

        info ("");
        info ("    'Materially worse' is fixed here as: more than 10 percentage");
        info ("    points more tones below -6 dB than Hall, or a worst tone more");
        info ("    than 3 dB deeper, at any of the three settings.");

        check (noWorse, "Room does not make the accepted PHASE 11 narrow-band mono "
                        "characteristic materially worse than Hall at the same settings");

        info ("");
        info ("  TRANSIENT STRUCTURE in the same corner, E/L 0:");
        info ("");
        info ("    " + pad ("setting", 26) + pad ("mode", 10) + pad ("NED 0.9 at", 13)
              + pad ("recurrence", 13) + pad ("modal prom.", 14) + pad ("corr 50-200", 14)
              + "shortest loop");

        double roomRecurrence = 0.0, hallRecurrence = 0.0;

        for (int c = 0; c < 2; ++c)
            for (int m : { room, hall, chamber, plate })
            {
                User u;
                u.size = corners[c].size;
                u.decay = corners[c].decay;

                const auto ir = impulseResponse (modes[static_cast<size_t> (m)], u, 1.2);
                const auto f  = analyse (ir);

                const double shortest = modes[static_cast<size_t> (m)].delaysMs[0]
                                      * mapping::sizeScaleFor (u.size);

                if (m == room) roomRecurrence = std::max (roomRecurrence, f.recurrence);
                if (m == hall) hallRecurrence = std::max (hallRecurrence, f.recurrence);

                info ("    " + pad (m == room ? corners[c].label : "", 26)
                      + pad (modes[static_cast<size_t> (m)].name, 10)
                      + pad (fx (f.tNed90, 0) + " ms", 13) + pad (fx (f.recurrence, 3), 13)
                      + pad (fx (modalProminenceDb (ir), 2) + " dB", 14)
                      + pad (fx (f.corr[1], 3), 14)
                      + fx (shortest, 2) + " ms = " + fx (1000.0 / shortest, 0) + " Hz");
            }

        info ("");
        info ("    The last column is the Stage 1 warning made concrete: a loop");
        info ("    that returns faster than about 20 ms is heard as a pitch, and");
        info ("    the column gives the pitch. Hall is already inside that region");
        info ("    at these Sizes. Room is deeper inside it.");

        check (roomRecurrence <= hallRecurrence + 0.10,
               "Room's onset recurrence in the small corner is not materially above "
               "Hall's - no added flutter");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.9 - Plate: is eight lines dense enough");
    // =======================================================================
    {
        info ("  Not assumed. Echo-density growth, onset recurrence and modal");
        info ("  prominence, E/L 0, at three settings. Plate's target is density");
        info ("  from the first instant:");
        info ("");
        info ("    " + pad ("setting", 20) + pad ("mode", 10) + pad ("NED@10ms", 11)
              + pad ("NED@40ms", 11) + pad ("NED 0.5 at", 13) + pad ("NED 0.9 at", 13)
              + pad ("recurrence", 13) + "modal prom.");

        double plateNed90[3] {}, bestOther[3] { 1.0e9, 1.0e9, 1.0e9 };
        double plateAt10 = 0.0;

        int row = 0;

        for (int p : { 0, 4, 8 })
        {
            for (int m = 0; m < modeCount; ++m)
            {
                const auto u  = userAt (kGrid[p], 0.0);
                const auto ir = impulseResponse (modes[static_cast<size_t> (m)], u, 1.2);
                const auto f  = analyse (ir);

                if (m == plate)
                {
                    plateNed90[row] = f.tNed90;

                    if (p == 4)
                        plateAt10 = f.nedAt10;
                }
                else
                {
                    bestOther[row] = std::min (bestOther[row], f.tNed90);
                }

                info ("    " + pad (m == 0 ? kGrid[p].label : "", 20)
                      + pad (modes[static_cast<size_t> (m)].name, 10)
                      + pad (fx (f.nedAt10, 2), 11) + pad (fx (f.nedAt40, 2), 11)
                      + pad (fx (f.tNed50, 0) + " ms", 13) + pad (fx (f.tNed90, 0) + " ms", 13)
                      + pad (fx (f.recurrence, 3), 13) + fx (modalProminenceDb (ir), 2) + " dB");
            }

            ++row;
        }

        info ("");
        info ("    A diffuse field reads 1.0. The 20 ms analysis window puts a");
        info ("    floor under how early ANY figure here can be reached.");

        bool fastest = true;

        for (int i = 0; i < 3; ++i)
            fastest = fastest && plateNed90[i] <= bestOther[i];

        check (fastest, "Plate reaches echo density 0.9 no later than every other Mode "
                        "at all three settings");

        check (plateAt10 >= 0.7, "Plate's echo density is already at 0.7 in the window "
                                 "starting 10 ms in, at the defaults");

        // What the early layer adds to the onset, since Plate's ER is a burst.
        {
            User withEr;
            withEr.earlyLate = 0.5;

            const auto f0 = analyse (impulseResponse (modes[plate], User {}, 1.2));
            const auto f1 = analyse (impulseResponse (modes[plate], withEr, 1.2));

            info ("");
            info ("    Plate's early layer is a burst of density, not reflections.");
            info ("    Echo density in the window from 10 ms: " + fx (f0.nedAt10, 2)
                  + " at Early/Late 0, " + fx (f1.nedAt10, 2) + " at Early/Late 50.");
            info ("    Early/Late therefore still does something audible in Plate,");
            info ("    and it does the thing the control's name says.");
        }
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.10 - Ambient: what the long delay set costs in memory");
    // =======================================================================
    {
        double longestAny = 0.0;

        for (const auto& m : modes)
            longestAny = std::max (longestAny, m.delaysMs[7]);

        const double hallCapacity    = modes[hall].delaysMs[7] * fdn::maxSizeScale;
        const double ambientCapacity = modes[ambient].delaysMs[7] * fdn::maxSizeScale;
        const double maxCapacity     = longestAny * fdn::maxSizeScale;

        info ("  Per line, the tank reserves the longest delay at the largest Size,");
        info ("  rounded up to a power of two. Eight lines per bank, two banks -");
        info ("  the second exists for every Size change already, so a Mode");
        info ("  switch needs NO memory beyond it.");
        info ("");
        info ("    " + pad ("rate", 12) + pad ("Hall today", 16) + pad ("Ambient alone", 16)
              + pad ("max over Modes", 17) + "increase");

        double increaseAt48 = 0.0;

        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto kib = [&] (double capacityMs)
            {
                return static_cast<double> (lineBufferSamples (capacityMs, rate))
                       * 8.0 * 2.0 * sizeof (double) / 1024.0;
            };

            const double increase = 100.0 * (kib (maxCapacity) / kib (hallCapacity) - 1.0);

            if (rate == 48000.0)
                increaseAt48 = increase;

            info ("    " + pad (fx (rate / 1000.0, 1) + " kHz", 12)
                  + pad (fx (kib (hallCapacity), 0) + " KiB", 16)
                  + pad (fx (kib (ambientCapacity), 0) + " KiB", 16)
                  + pad (fx (kib (maxCapacity), 0) + " KiB", 17)
                  + "+" + fx (increase, 0) + " %");
        }

        info ("");
        info ("    longest line, Hall     " + fx (modes[hall].delaysMs[7], 2) + " ms  ->  "
              + fx (hallCapacity, 1) + " ms reserved");
        info ("    longest line, Ambient  " + fx (modes[ambient].delaysMs[7], 2) + " ms  ->  "
              + fx (ambientCapacity, 1) + " ms reserved");

        // The longest line that would still fit in Hall's rounded buffer.
        const double freeMs = (static_cast<double> (lineBufferSamples (hallCapacity, 48000.0)) - 7.0)
                              * 1000.0 / 48000.0 / fdn::maxSizeScale;

        info ("");
        info ("    At 48 kHz Hall's buffers already round up to hold a longest");
        info ("    line of " + fx (freeMs, 1) + " ms. An Ambient set whose longest line stayed");
        info ("    under that would cost nothing; this candidate's does not.");
        info ("");
        info ("    SHARED MAXIMUM versus REALLOCATION. Reallocating on a Mode");
        info ("    change means allocating on a parameter change, which the audio");
        info ("    thread may not do and which the engine has never done. One");
        info ("    allocation at prepare, sized for the largest Mode, is the only");
        info ("    option consistent with the existing real-time contract.");

        check (increaseAt48 <= 100.0, "allocating for the largest Mode at most doubles "
                                      "tank memory at 48 kHz");
    }

    // =======================================================================
    beginSuite ("PHASE 12.2.11 - Mode switching: crossfade against the naive step");
    // =======================================================================
    {
        /*
            RESEARCH, NOT AN IMPLEMENTATION. Two complete rigs stand in for
            the two banks. The transition is the one MorphingStereoFdn already
            performs for Size - prime the silent incoming tank on the live
            input, then equal-power crossfade the OUTPUTS - applied to a whole
            configuration instead of to one number.
        */
        const auto length  = static_cast<size_t> (kRate * 4.0);
        const auto change  = static_cast<size_t> (kRate * 2.0);
        const auto prime   = static_cast<size_t> (kRate * 0.5);
        const auto fade    = static_cast<size_t> (kRate * 0.12);

        const auto noise = whiteNoise (length, 4242u);

        auto level = [] (const Stereo& x, size_t from, size_t to)
        {
            double sum = 0.0;

            for (size_t n = from; n < to; ++n)
                sum += x.l[n] * x.l[n] + x.r[n] * x.r[n];

            return dB (sum / static_cast<double> (2 * (to - from)));
        };

        auto largestStep = [] (const Stereo& x, size_t from, size_t to)
        {
            double worst = 0.0;

            for (size_t n = from + 1; n < to; ++n)
                worst = std::max ({ worst, std::abs (x.l[n] - x.l[n - 1]),
                                    std::abs (x.r[n] - x.r[n - 1]) });

            return worst;
        };

        struct Pair { int from, to; };

        info ("    " + pad ("transition", 22) + pad ("method", 26) + pad ("worst dip", 12)
              + pad ("worst rise", 12) + "largest step / steady");

        double worstCrossfadeExcursion = 0.0, worstCrossfadeStep = 0.0;
        double worstNaiveStep = 0.0, worstUnprimedDip = 0.0;

        for (const auto& pair : { Pair { hall, room }, Pair { room, ambient }, Pair { plate, hall } })
        {
            const auto& from = modes[static_cast<size_t> (pair.from)];
            const auto& to   = modes[static_cast<size_t> (pair.to)];

            const std::string label = from.name + " -> " + to.name;

            // Steady references.
            const auto steadyFrom = std::make_unique<Rig> (from, User {})->render (noise, length);
            const auto steadyTo   = std::make_unique<Rig> (to,   User {})->render (noise, length);

            const double levelFrom = level (steadyFrom, change, length);
            const double levelTo   = level (steadyTo,   change, length);
            const double low  = std::min (levelFrom, levelTo);
            const double high = std::max (levelFrom, levelTo);

            const double steadyStep = std::max (largestStep (steadyFrom, change, length),
                                                largestStep (steadyTo,   change, length));

            auto excursion = [&] (const Stereo& y, double& dip, double& rise)
            {
                dip = rise = 0.0;

                const size_t window = msToSamples (20.0);

                for (size_t n = change; n + window < length; n += window / 2)
                {
                    const double l = level (y, n, n + window);

                    dip  = std::max (dip,  low - l);
                    rise = std::max (rise, l - high);
                }
            };

            for (int method = 0; method < 3; ++method)
            {
                Stereo y;
                y.l.assign (length, 0.0);
                y.r.assign (length, 0.0);

                ScopedFtz ftz;

                if (method < 2)
                {
                    // 0: primed crossfade. 1: crossfade straight into an
                    // empty tank, to show what priming is for.
                    auto outgoing = std::make_unique<Rig> (from, User {});
                    auto incoming = std::make_unique<Rig> (to,   User {});

                    const size_t fadeStart = change + (method == 0 ? prime : 0);

                    for (size_t n = 0; n < length; ++n)
                    {
                        double ol = noise[n], orr = noise[n];
                        outgoing->processSample (ol, orr);

                        double il = 0.0, ir = 0.0;

                        if (n >= change)
                        {
                            il = ir = noise[n];
                            incoming->processSample (il, ir);
                        }

                        double g = 0.0;

                        if (n >= fadeStart)
                            g = std::min (1.0, static_cast<double> (n - fadeStart)
                                               / static_cast<double> (fade));

                        const double a = std::cos (g * 1.57079632679489661923);
                        const double b = std::sin (g * 1.57079632679489661923);

                        y.l[n] = a * ol + b * il;
                        y.r[n] = a * orr + b * ir;
                    }
                }
                else
                {
                    // 2: the thing that must not be done - the new delay set
                    // written into the ringing loop.
                    auto rig = std::make_unique<Rig> (from, User {});

                    // Only meaningful where the incoming set fits the
                    // outgoing capacity; otherwise the core refuses it,
                    // which is the correct behaviour and is reported.
                    bool applied = true;

                    for (size_t n = 0; n < length; ++n)
                    {
                        if (n == change)
                            applied = succeeded (rig->getCore().setDelaysMilliseconds (
                                to.delaysMs.data(), mapping::sizeScaleFor (0.60)));

                        double l = noise[n], r = noise[n];
                        rig->processSample (l, r);

                        y.l[n] = l;
                        y.r[n] = r;
                    }

                    if (! applied)
                    {
                        info ("    " + pad (label, 22) + pad ("naive in-place step", 26)
                              + "REFUSED by the core: the incoming set exceeds capacity");
                        continue;
                    }
                }

                double dip = 0.0, rise = 0.0;
                excursion (y, dip, rise);

                const double step = largestStep (y, change, length) / std::max (1.0e-12, steadyStep);

                if (method == 0)
                {
                    worstCrossfadeExcursion = std::max ({ worstCrossfadeExcursion, dip, rise });
                    worstCrossfadeStep = std::max (worstCrossfadeStep, step);
                }
                else if (method == 1)
                {
                    worstUnprimedDip = std::max (worstUnprimedDip, dip);
                }
                else
                {
                    worstNaiveStep = std::max (worstNaiveStep, step);
                }

                info ("    " + pad (method == 0 ? label : "", 22)
                      + pad (method == 0 ? "primed output crossfade"
                           : method == 1 ? "crossfade, not primed"
                                         : "naive in-place step", 26)
                      + pad (fx (dip, 2) + " dB", 12) + pad (fx (rise, 2) + " dB", 12)
                      + fx (step, 2) + "x");
            }
        }

        info ("");
        info ("    Dip and rise are measured in 20 ms windows against the band");
        info ("    between the two steady levels, on sustained noise - the");
        info ("    hardest case for a level change to hide in. 'Largest step' is");
        info ("    the biggest sample-to-sample jump relative to the same");
        info ("    statistic in steady state; a click is a large one.");

        check (worstCrossfadeExcursion <= 3.0,
               "the primed output crossfade stays within 3 dB of the band between the "
               "two steady levels - no burst and no drop");

        check (worstCrossfadeStep <= 1.5,
               "and produces no sample step larger than 1.5x the steady-state maximum - "
               "no click");

        check (worstUnprimedDip > worstCrossfadeExcursion,
               "priming is doing real work: fading into an empty tank dips further");

        info ("");
        info ("    worst excursion, primed crossfade   " + fx (worstCrossfadeExcursion, 2) + " dB");
        info ("    worst dip, not primed               " + fx (worstUnprimedDip, 2) + " dB");
        info ("    largest step, naive in-place        " + fx (worstNaiveStep, 2) + "x steady");
    }
}

} // namespace avtest
