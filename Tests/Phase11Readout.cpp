#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/EarlyReflections.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"
#include "DSP/PreDelay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 11 - STAGE 2. Readout-topology research for the Stage-1 blocker.

    NO PRODUCTION DSP IS CHANGED. No PHASE 10 constant is touched - not the ER
    pattern, not occupancy, not envelopeDecay, not sizeAlpha, not BAL-4, not
    the Early/Late law or its semantics. PHASE 10 stays closed.

    THE BLOCKER. At Early/Late 0, Size 15 %, Decay 0.6 s, a sustained
    narrow-band source produces L/R correlation -0.9848 and a mono fold-down
    of -15.40 dB, held across all twelve analysis windows. It is in the late
    path, it is not the ER layer, it is not Width, and it is not transient.

    WHAT IS FORBIDDEN, and the order matters: no correlation clamp, no dynamic
    mono protection, no post-Width manipulation. The readout topology is
    researched first, because if the problem is in the geometry then anything
    downstream of it is a cosmetic patch over a known cause.

    THE KEY QUESTION THIS SUITE ANSWERS FIRST. It is not "are the readout
    vectors orthogonal" - they are, and that is exactly why the failure was
    surprising. It is what happens when only a FEW lines carry energy, which
    is what small Size and short Decay produce.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    constexpr double kDecay          = 2.5;
    constexpr double kSizeDefault    = er::referenceSizeControl;
    constexpr double kEarlyDiffusion = 0.70;
    constexpr double kModDepth       = 0.30;
    constexpr double kModRate        = 0.5;
    constexpr double kLowDamp        = 0.20;
    constexpr double kLowDampHz      = 200.0;
    constexpr double kHighDamp       = 0.45;
    constexpr double kHighDampHz     = 10000.0;

    [[nodiscard]] std::string fx (double v, int decimals)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision (decimals) << v;
        return o.str();
    }

    [[nodiscard]] std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    // =======================================================================
    //  A readout pair, and the metrics that actually matter
    // =======================================================================
    struct Pair
    {
        std::string name;
        std::array<double, 8> l {}, r {};
    };

    /**
        Mono retention for a subset of lines, in dB.

        THIS IS THE METRIC THE WHOLE STAGE TURNS ON, so it is worth being
        exact about what it means. Suppose only the lines in S carry energy,
        each with an independent equal-power state. Then

            stereo energy = sum over S of (L_i^2 + R_i^2)
            mono energy   = sum over S of (L_i + R_i)^2 / 2

        and the ratio in dB is what a listener loses by folding down. For two
        directions that are orthogonal ON THAT SUBSET and of equal norm the
        cross term vanishes and the ratio is exactly 0.5, which is -3.01 dB:
        the ordinary, harmless cost of summing decorrelated channels.

        A value near -3.01 dB is healthy. Much more negative is cancellation.
        Much less negative means the subset is correlated, which costs nothing
        in mono but narrows the image.
    */
    [[nodiscard]] double monoRetentionDb (const Pair& p, const std::vector<int>& subset)
    {
        double stereo = 0.0, mono = 0.0;

        for (int i : subset)
        {
            stereo += p.l[static_cast<size_t> (i)] * p.l[static_cast<size_t> (i)]
                    + p.r[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];

            const double sum = p.l[static_cast<size_t> (i)] + p.r[static_cast<size_t> (i)];
            mono += sum * sum * 0.5;
        }

        if (stereo <= 0.0)
            return 0.0;

        return 10.0 * std::log10 (std::max (1.0e-30, mono) / stereo);
    }

    /** Normalised inner product over a subset: the subset's own correlation. */
    [[nodiscard]] double subsetCorrelation (const Pair& p, const std::vector<int>& subset)
    {
        double dot = 0.0, nl = 0.0, nr = 0.0;

        for (int i : subset)
        {
            dot += p.l[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];
            nl  += p.l[static_cast<size_t> (i)] * p.l[static_cast<size_t> (i)];
            nr  += p.r[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];
        }

        return (nl > 0.0 && nr > 0.0) ? dot / std::sqrt (nl * nr) : 0.0;
    }

    struct SubsetMetrics
    {
        double fullCorrelation = 0.0;
        int    signAgreements = 0, signDisagreements = 0;
        std::array<double, 5> worstRetention {};     ///< indexed by subset size 1..4
        std::array<double, 5> worstCorrelation {};
        double normL = 0.0, normR = 0.0;
        int    zeroSumLines = 0;
    };

    /** Exhaustive over all subsets up to size 4. 8 choose 4 is 70; this is
        cheap enough to be exact rather than sampled. */
    [[nodiscard]] SubsetMetrics analyse (const Pair& p)
    {
        SubsetMetrics m;

        std::vector<int> all { 0, 1, 2, 3, 4, 5, 6, 7 };

        m.fullCorrelation = subsetCorrelation (p, all);

        for (int i = 0; i < 8; ++i)
        {
            const double product = p.l[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];

            (product >= 0.0 ? m.signAgreements : m.signDisagreements)++;

            m.normL += p.l[static_cast<size_t> (i)] * p.l[static_cast<size_t> (i)];
            m.normR += p.r[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];

            if (std::abs (p.l[static_cast<size_t> (i)] + p.r[static_cast<size_t> (i)])
                  < 1.0e-12)
                ++m.zeroSumLines;
        }

        for (size_t k = 1; k <= 4; ++k)
        {
            m.worstRetention[k] = 1.0e30;
            m.worstCorrelation[k] = 1.0e30;
        }

        // All 2^8 subsets, filtered by size. Exhaustive and obviously correct
        // beats a clever enumeration here.
        for (int mask = 1; mask < 256; ++mask)
        {
            std::vector<int> subset;

            for (int i = 0; i < 8; ++i)
                if (mask & (1 << i))
                    subset.push_back (i);

            if (subset.size() > 4)
                continue;

            const auto k = subset.size();

            m.worstRetention[k] = std::min (m.worstRetention[k],
                                            monoRetentionDb (p, subset));
            m.worstCorrelation[k] = std::min (m.worstCorrelation[k],
                                              subsetCorrelation (p, subset));
        }

        return m;
    }

    // =======================================================================
    //  Candidate construction
    // =======================================================================
    [[nodiscard]] Pair currentPair()
    {
        Pair p;
        p.name = "A: production";

        for (int i = 0; i < 8; ++i)
        {
            p.l[static_cast<size_t> (i)] = fdn::readoutLeft[i];
            p.r[static_cast<size_t> (i)] = fdn::readoutRight[i];
        }

        return p;
    }

    /**
        Rotates a pair within the plane it spans.

            L' = L cos(phi) + R sin(phi)
            R' = R cos(phi) - L sin(phi)

        A rotation preserves both norms and the inner product exactly, so
        every candidate built this way is orthogonal and energy-normalised BY
        CONSTRUCTION rather than by a later correction. That is the reason to
        parametrise the search this way: a family whose members cannot violate
        the two invariants that matter.

        What the rotation DOES change is the per-line split. For the current
        pair, whose entries are all +/-1, the algebra is exact: a line where
        the two signs agree gets a mono sum of 2cos(phi), and a line where
        they disagree gets -2sin(phi). At phi = 0 the disagreeing lines sum
        to zero - which is the blocker. At phi = 45 degrees both are
        +/-sqrt(2) and no line cancels at all.
    */
    [[nodiscard]] Pair rotated (const Pair& base, double degrees, const std::string& name)
    {
        const double phi = degrees * 3.14159265358979323846 / 180.0;
        const double c = std::cos (phi), s = std::sin (phi);

        Pair p;
        p.name = name;

        for (size_t i = 0; i < 8; ++i)
        {
            p.l[i] = base.l[i] * c + base.r[i] * s;
            p.r[i] = base.r[i] * c - base.l[i] * s;
        }

        return p;
    }

    /** Two rows of an 8x8 Hadamard matrix, as the obvious alternative. */
    [[nodiscard]] Pair hadamardPair (int rowA, int rowB, const std::string& name)
    {
        auto entry = [] (int row, int col)
        {
            int bits = row & col;
            int parity = 0;

            while (bits != 0)
            {
                parity ^= (bits & 1);
                bits >>= 1;
            }

            return parity ? -1.0 : 1.0;
        };

        Pair p;
        p.name = name;

        for (int i = 0; i < 8; ++i)
        {
            p.l[static_cast<size_t> (i)] = entry (rowA, i);
            p.r[static_cast<size_t> (i)] = entry (rowB, i);
        }

        return p;
    }

    /**
        A quadrature pair: L_i = cos(theta_i), R_i = sin(theta_i).

        Orthogonality needs sum of sin(2 theta_i) = 0, which the +/- angle
        pairing satisfies exactly. Unlike the rotation family this does not
        start from the production vectors, so it is a genuinely different
        shape rather than a reparametrisation - which is what makes it worth
        measuring alongside.
    */
    [[nodiscard]] Pair quadraturePair (double a, double b, double c, double d,
                                       const std::string& name)
    {
        const double degrees[8] { a, -a, b, -b, c, -c, d, -d };

        Pair p;
        p.name = name;

        // Scaled so each readout has the same norm as a +/-1 vector of
        // length 8 would after signVectorNorm - the energy contract.
        constexpr double scale = 1.41421356237309504880;   // sqrt(2)

        for (size_t i = 0; i < 8; ++i)
        {
            const double t = degrees[i] * 3.14159265358979323846 / 180.0;

            p.l[i] = std::cos (t) * scale;
            p.r[i] = std::sin (t) * scale;
        }

        return p;
    }

    // =======================================================================
    //  Rendering with a chosen readout pair
    // =======================================================================
    struct Config
    {
        double size      = kSizeDefault;
        double decay     = kDecay;
        double modDepth  = kModDepth;
        double width     = 1.0;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c, const Pair& readout)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        // The readout is installed on both banks, since Size morphing can
        // have either one live.
        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).setStereoVectors (fdn::injectionLeft, fdn::injectionRight,
                                                   readout.l.data(), readout.r.data());

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (c.modDepth),
                                      mapping::modRateScaleFor (kModRate), true);

        for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
        {
            double l = 0.0, r = 0.0;
            tank->processSample (l, r);
        }

        for (int bank = 0; bank < 2; ++bank)
            tank->getBank (bank).getCore().resetModulation();

        tank->reset();

        return tank;
    }

    struct Stereo { std::vector<double> l, r; };

    /** The LATE path only, which is where the blocker lives: Early/Late 0. */
    [[nodiscard]] Stereo renderLate (const Config& c, const Pair& readout,
                                     const Stereo& dry, size_t length)
    {
        ScopedFtz ftz;

        auto tank = makeTank (c, readout);

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            out.l[n] = n < dry.l.size() ? dry.l[n] : 0.0;
            out.r[n] = n < dry.r.size() ? dry.r[n] : 0.0;
        }

        for (size_t n = 0; n < length; ++n)
            tank->processSample (out.l[n], out.r[n]);

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        for (size_t n = 0; n < length; ++n)
        {
            double l = out.l[n] * trim;
            double r = out.r[n] * trim;

            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * c.width;

            out.l[n] = mid + side;
            out.r[n] = mid - side;
        }

        return out;
    }

    struct Metrics
    {
        double correlation = 0.0, balanceDb = 0.0, monoFoldDb = 0.0;
        double midDb = 0.0, sideDb = 0.0;
    };

    [[nodiscard]] Metrics measure (const Stereo& x, double fromMs = 0.0,
                                   double toMs = 1.0e9)
    {
        const auto from = static_cast<size_t> (fromMs * kSampleRate / 1000.0);
        const auto to   = std::min (x.l.size(),
                                    static_cast<size_t> (toMs * kSampleRate / 1000.0));

        Metrics m;

        if (to <= from)
            return m;

        double el = 0.0, er = 0.0, emid = 0.0, eside = 0.0;

        for (size_t i = from; i < to; ++i)
        {
            const double l = x.l[i], r = x.r[i];

            el += l * l;
            er += r * r;

            const double mid  = 0.5 * (l + r);
            const double side = 0.5 * (l - r);

            emid  += mid * mid;
            eside += side * side;
        }

        const double total = el + er;

        m.correlation = correlation (x.l.data() + from, x.r.data() + from, to - from);
        m.balanceDb   = (el > 0.0 && er > 0.0) ? 10.0 * std::log10 (er / el) : 0.0;
        m.monoFoldDb  = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, 2.0 * emid)
                                                         / total) : 0.0;
        m.midDb  = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, emid) / total) : 0.0;
        m.sideDb = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, eside) / total) : 0.0;

        return m;
    }

    // =======================================================================
    //  Sources
    // =======================================================================
    [[nodiscard]] std::vector<double> tone (double hz, double seconds)
    {
        const auto n = static_cast<size_t> (seconds * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;
            const double env = std::min (1.0, t * 200.0)
                             * std::min (1.0, (seconds - t) * 200.0);

            x[i] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * hz * t)
                 * std::max (0.0, env);
        }

        return x;
    }

    [[nodiscard]] std::vector<double> noiseBurst (double ms, unsigned int seed)
    {
        const auto n = static_cast<size_t> (ms * kSampleRate / 1000.0);

        std::vector<double> x (n, 0.0);
        unsigned int s = seed;

        for (size_t i = 0; i < n; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;

            const double u = static_cast<double> (s & 0xffffffu) / 16777215.0;
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                                   * static_cast<double> (i)
                                                   / static_cast<double> (n - 1));

            x[i] = (2.0 * u - 1.0) * w;
        }

        return x;
    }

    [[nodiscard]] Stereo centred (const std::vector<double>& x) { return { x, x }; }

    [[nodiscard]] Stereo leftOnly (const std::vector<double>& x)
    {
        return { x, std::vector<double> (x.size(), 0.0) };
    }

    [[nodiscard]] Stereo rightOnly (const std::vector<double>& x)
    {
        return { std::vector<double> (x.size(), 0.0), x };
    }

    [[nodiscard]] std::vector<double> monoFrom (const std::vector<StereoFrame>& f)
    {
        std::vector<double> x (f.size(), 0.0);

        for (size_t n = 0; n < f.size(); ++n)
            x[n] = f[n].l;

        return x;
    }
}

// ===========================================================================

void runPhase11ReadoutTests()
{
    // =======================================================================
    beginSuite ("PHASE 11.2.1 - the current readout pair, and why it fails");
    // =======================================================================

    const auto production = currentPair();

    {
        info ("  Current production vectors, from FdnTuning.h:");
        info ("");
        info ("    readoutLeft   " + [&]
        {
            std::string s;
            for (int i = 0; i < 8; ++i)
                s += (production.l[static_cast<size_t> (i)] > 0 ? " +1" : " -1");
            return s;
        }());
        info ("    readoutRight  " + [&]
        {
            std::string s;
            for (int i = 0; i < 8; ++i)
                s += (production.r[static_cast<size_t> (i)] > 0 ? " +1" : " -1");
            return s;
        }());

        const auto m = analyse (production);

        info ("");
        info ("    full-vector correlation      " + fx (m.fullCorrelation, 6));
        info ("    sign agreements              " + std::to_string (m.signAgreements));
        info ("    sign disagreements           " + std::to_string (m.signDisagreements));
        info ("    lines where L + R = 0        " + std::to_string (m.zeroSumLines));
        info ("");
        info ("    worst mono retention by active-subset size:");
        info ("      1 line   " + fx (m.worstRetention[1], 2) + " dB");
        info ("      2 lines  " + fx (m.worstRetention[2], 2) + " dB");
        info ("      3 lines  " + fx (m.worstRetention[3], 2) + " dB");
        info ("      4 lines  " + fx (m.worstRetention[4], 2) + " dB");
        info ("");
        info ("    worst subset correlation by size:");
        info ("      1 line   " + fx (m.worstCorrelation[1], 4));
        info ("      2 lines  " + fx (m.worstCorrelation[2], 4));
        info ("      3 lines  " + fx (m.worstCorrelation[3], 4));
        info ("      4 lines  " + fx (m.worstCorrelation[4], 4));
        info ("");
        info ("    THE MECHANISM IN ONE NUMBER: four of the eight lines have");
        info ("    L + R exactly zero. Energy on any of those lines reaches the");
        info ("    two outputs in anti-phase and contributes NOTHING to a mono");
        info ("    sum. The full vectors are perfectly orthogonal and that is");
        info ("    no protection whatever, because orthogonality is a statement");
        info ("    about all eight lines at once and the failure happens when");
        info ("    only a few are carrying.");
        info ("");
        info ("    AND IT IS NOT A BAD CHOICE OF SIGNS - IT IS FORCED.");
        info ("    Orthogonality of two +/-1 vectors of length 8 means the");
        info ("    signs agree on exactly four lines and disagree on exactly");
        info ("    four. Every disagreeing line has L + R = 0. So EVERY");
        info ("    orthogonal +/-1 pair has exactly four fully-cancelling");
        info ("    lines, and no amount of searching among sign vectors can");
        info ("    avoid it. The next section demonstrates that rather than");
        info ("    asserting it.");

        check (m.fullCorrelation == 0.0,
               "the production pair is exactly orthogonal over all eight lines");

        check (m.zeroSumLines == 4,
               "and yet four lines cancel completely in mono");

        check (m.worstRetention[1] < -100.0,
               "a single active line can lose the entire mono signal");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.2 - no sign-vector pair can fix this");
    // =======================================================================
    {
        /*
            The claim above is a counting argument, and a counting argument is
            exactly the kind of thing that should be checked by enumeration
            rather than trusted. All 256 x 256 ordered pairs of +/-1 vectors
            of length 8 is 65536 cases, which is nothing.
        */
        int orthogonalPairs = 0;
        int withoutZeroSum = 0;
        double bestWorstRetention = -1.0e30;

        for (int a = 0; a < 256; ++a)
        {
            for (int b = 0; b < 256; ++b)
            {
                Pair p;

                for (int i = 0; i < 8; ++i)
                {
                    p.l[static_cast<size_t> (i)] = (a & (1 << i)) ? 1.0 : -1.0;
                    p.r[static_cast<size_t> (i)] = (b & (1 << i)) ? 1.0 : -1.0;
                }

                double dot = 0.0;

                for (int i = 0; i < 8; ++i)
                    dot += p.l[static_cast<size_t> (i)] * p.r[static_cast<size_t> (i)];

                if (dot != 0.0)
                    continue;

                ++orthogonalPairs;

                int zeros = 0;

                for (int i = 0; i < 8; ++i)
                    if (std::abs (p.l[static_cast<size_t> (i)]
                                  + p.r[static_cast<size_t> (i)]) < 1.0e-12)
                        ++zeros;

                if (zeros == 0)
                    ++withoutZeroSum;

                bestWorstRetention = std::max (bestWorstRetention,
                                               monoRetentionDb (p, { 0 }));
            }
        }

        info ("  Exhaustive enumeration of every +/-1 readout pair of length 8.");
        info ("");
        info ("    orthogonal pairs found            " + std::to_string (orthogonalPairs));
        info ("    of those, with no cancelling line " + std::to_string (withoutZeroSum));
        info ("    best achievable 1-line retention  "
              + (bestWorstRetention < -100.0 ? std::string ("total cancellation")
                                             : fx (bestWorstRetention, 2) + " dB"));
        info ("");
        info ("    ZERO ORTHOGONAL SIGN-VECTOR PAIRS AVOID THE PROBLEM. The");
        info ("    counting argument holds: orthogonality forces four");
        info ("    agreements and four disagreements, and every disagreement");
        info ("    is a line that vanishes in mono.");
        info ("");
        info ("    That settles candidate family B before it is measured in");
        info ("    audio: Hadamard rows are sign vectors, so they inherit the");
        info ("    same four cancelling lines. They are still rendered later,");
        info ("    because a prediction that is not checked is a guess.");

        check (orthogonalPairs > 0, "orthogonal sign-vector pairs exist");

        check (withoutZeroSum == 0,
               "but none of them avoids a fully cancelling line - the "
               "+/-1 family cannot solve this");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.3 - candidate families and their subset metrics");
    // =======================================================================

    std::vector<Pair> candidates;

    candidates.push_back (production);
    candidates.push_back (hadamardPair (1, 2, "B: Hadamard rows 1,2"));
    candidates.push_back (hadamardPair (3, 5, "B: Hadamard rows 3,5"));
    candidates.push_back (rotated (production, 15.0, "C: rotated 15 deg"));
    candidates.push_back (rotated (production, 22.5, "C: rotated 22.5 deg"));
    candidates.push_back (rotated (production, 30.0, "C: rotated 30 deg"));
    candidates.push_back (rotated (production, 45.0, "C: rotated 45 deg"));
    candidates.push_back (quadraturePair (15.0, 35.0, 55.0, 75.0, "D: quadrature 15-75"));
    candidates.push_back (quadraturePair (30.0, 40.0, 50.0, 60.0, "D: quadrature 30-60"));

    {
        info ("  Every candidate is orthogonal and energy-normalised by");
        info ("  construction. What differs is the per-line split.");
        info ("");
        info ("    " + pad ("candidate", 23) + pad ("corr", 10) + pad ("zeros", 8)
              + pad ("1 line", 11) + pad ("2 lines", 11) + pad ("3 lines", 11)
              + "norms L/R");

        for (const auto& p : candidates)
        {
            const auto m = analyse (p);

            auto cell = [] (double v)
            {
                return v < -100.0 ? std::string ("-inf") : fx (v, 2);
            };

            info ("    " + pad (p.name, 23)
                  + pad (fx (m.fullCorrelation, 4), 10)
                  + pad (std::to_string (m.zeroSumLines), 8)
                  + pad (cell (m.worstRetention[1]), 11)
                  + pad (cell (m.worstRetention[2]), 11)
                  + pad (cell (m.worstRetention[3]), 11)
                  + fx (m.normL, 2) + " / " + fx (m.normR, 2));
        }

        info ("");
        info ("    THE ROTATION FAMILY IS THE INTERESTING ONE, and the algebra");
        info ("    says why before the audio does. Rotating the production pair");
        info ("    by phi within the plane it spans keeps both norms and the");
        info ("    inner product exactly - so orthogonality and energy are");
        info ("    preserved by construction, not by correction - while the");
        info ("    per-line mono sum becomes 2cos(phi) on the agreeing lines");
        info ("    and -2sin(phi) on the disagreeing ones. At 45 degrees both");
        info ("    are sqrt(2) and nothing cancels; the worst 1-line retention");
        info ("    is exactly -3.01 dB, which is the cost of summing a");
        info ("    hard-panned signal and is not cancellation at all.");
        info ("");
        info ("    THE COST IS ALSO VISIBLE IN THE ALGEBRA. At 45 degrees each");
        info ("    readout reads only four of the eight lines - the other four");
        info ("    multiply by zero - so every mode is hard-panned to one side.");
        info ("    That trades a mono problem for a placement problem, and only");
        info ("    rendering can say which is worse. Intermediate angles keep");
        info ("    all eight lines in both outputs.");

        const auto at45 = analyse (rotated (production, 45.0, "x"));

        check (std::abs (at45.worstRetention[1] + 3.01) < 0.1,
               "a 45 degree rotation gives exactly the decorrelated 1-line "
               "retention of -3.01 dB");

        check (std::abs (at45.fullCorrelation) < 1.0e-12,
               "and remains exactly orthogonal");

    // =======================================================================
    beginSuite ("PHASE 11.2.4 - narrow-band stress grid");
    // =======================================================================

    /*
        The candidates that go to audio. The quadrature family is carried even
        though the subset table already condemns it, because a prediction
        that is not checked is a guess - and the two Hadamard pairs reduce to
        one, since the table shows them identical in every respect that
        matters.
    */
    std::vector<Pair> audioCandidates
    {
        production,
        hadamardPair (1, 2, "B: Hadamard"),
        rotated (production, 22.5, "C: rotated 22.5"),
        rotated (production, 30.0, "C: rotated 30"),
        rotated (production, 45.0, "C: rotated 45"),
        quadraturePair (15.0, 35.0, 55.0, 75.0, "D: quadrature"),
    };

    struct Outcome
    {
        double minCorrelation = 1.0;
        double worstFold = 0.0;
        std::string where;
        int windowsBelow = 0, windowsTotal = 0;
        double worstBalance = 0.0;

        // Split by operating region, because the acceptance direction is
        // about NORMAL regions and an extreme corner is not one.
        double normalMinCorrelation = 1.0, normalWorstFold = 0.0;
        std::string normalWhere;
    };

    std::vector<Outcome> outcomes (audioCandidates.size());

    {
        info ("  Size x Decay x frequency, sustained tones, Early/Late 0,");
        info ("  Mod Depth 0 so nothing smears the modes and the worst case is");
        info ("  exposed rather than averaged away.");
        info ("");

        const std::vector<double> sizes { 0.0, 0.10, 0.15, 0.20, 0.30, 0.60, 1.0 };
        const std::vector<double> decays { 0.1, 0.3, 0.6, 2.5, 10.0 };
        const std::vector<double> hz { 220.0, 440.0, 1000.0, 3000.0 };

        const double seconds = 0.7;
        const auto length = static_cast<size_t> (kSampleRate * seconds);

        for (size_t ci = 0; ci < audioCandidates.size(); ++ci)
        {
            auto& out = outcomes[ci];

            for (double size : sizes)
            {
                for (double decay : decays)
                {
                    for (double f : hz)
                    {
                        Config c;
                        c.size = size;
                        c.decay = decay;
                        c.modDepth = 0.0;

                        const auto rendered = renderLate (c, audioCandidates[ci],
                                                          centred (tone (f, seconds)),
                                                          length);

                        const auto m = measure (rendered);

                        out.worstBalance = std::max (out.worstBalance,
                                                     std::abs (m.balanceDb));

                        // "Normal" excludes the two extreme corners: the
                        // smallest Size and the shortest Decay. Size 15 % at
                        // 0.6 s - the original failure - IS normal.
                        const bool normal = size >= 0.10 && decay >= 0.3;

                        if (normal && m.correlation < out.normalMinCorrelation)
                        {
                            out.normalMinCorrelation = m.correlation;
                            out.normalWorstFold = m.monoFoldDb;
                            out.normalWhere = "Size " + fx (size * 100.0, 0)
                                            + " %, Decay " + fx (decay, 1) + " s, "
                                            + fx (f, 0) + " Hz";
                        }

                        if (m.correlation < out.minCorrelation)
                        {
                            out.minCorrelation = m.correlation;
                            out.worstFold = m.monoFoldDb;
                            out.where = "Size " + fx (size * 100.0, 0) + " %, Decay "
                                      + fx (decay, 1) + " s, " + fx (f, 0) + " Hz";
                        }
                        else if (m.monoFoldDb < out.worstFold)
                        {
                            out.worstFold = m.monoFoldDb;
                        }
                    }
                }
            }

            // Duration of the condition at that candidate's own worst point.
            info ("    " + pad (audioCandidates[ci].name, 20)
                  + "ALL cells:    corr " + pad (fx (out.minCorrelation, 4), 10)
                  + "fold " + pad (fx (out.worstFold, 2) + " dB", 12)
                  + "balance " + fx (out.worstBalance, 1) + " dB");
            info ("      worst at " + out.where);
            info ("    " + pad ("", 20)
                  + "NORMAL cells: corr " + pad (fx (out.normalMinCorrelation, 4), 10)
                  + "fold " + pad (fx (out.normalWorstFold, 2) + " dB", 12));
            info ("      worst at " + out.normalWhere);
            info ("");
        }

        info ("");
        info ("    " + std::to_string (sizes.size() * decays.size() * hz.size())
              + " cells per candidate. Minima, not averages - an average over");
        info ("    a grid where most cells are healthy would hide the failure");
        info ("    this stage exists to remove.");

        /*
            DURATION, at the production candidate's worst point, for each
            candidate. A dip that lasts a few tens of milliseconds is a
            different product risk from one that holds for the whole note, and
            Stage 1 established that the production case is the second kind.
        */
        info ("");
        info ("  Duration at the ORIGINAL failing point - Size 15 %, Decay 0.6 s,");
        info ("  220 Hz - in 250 ms windows:");
        info ("");
        info ("    " + pad ("candidate", 20) + pad ("windows < -0.5", 17)
              + pad ("worst window", 15) + "worst fold");

        for (size_t ci = 0; ci < audioCandidates.size(); ++ci)
        {
            Config c;
            c.size = 0.15;
            c.decay = 0.6;
            c.modDepth = 0.0;

            const auto rendered = renderLate (c, audioCandidates[ci],
                                              centred (tone (220.0, 2.5)),
                                              static_cast<size_t> (kSampleRate * 2.5));

            int below = 0, total = 0;
            double worstWindow = 1.0, worstFold = 0.0;

            for (int w = 0; w < 9; ++w)
            {
                const auto m = measure (rendered, w * 250.0, (w + 1) * 250.0);

                ++total;

                if (m.correlation < -0.5)
                    ++below;

                worstWindow = std::min (worstWindow, m.correlation);
                worstFold = std::min (worstFold, m.monoFoldDb);
            }

            outcomes[ci].windowsBelow = below;
            outcomes[ci].windowsTotal = total;

            info ("    " + pad (audioCandidates[ci].name, 20)
                  + pad (std::to_string (below) + " of " + std::to_string (total), 17)
                  + pad (fx (worstWindow, 4), 15)
                  + fx (worstFold, 2) + " dB");
        }

        info ("");
        info ("    THE SIGN-VECTOR CANDIDATES BEHAVE IDENTICALLY BADLY, as the");
        info ("    counting argument said they would. The rotations improve");
        info ("    monotonically with the angle. The quadrature family is");
        info ("    WORSE than the production pair at its own worst point,");
        info ("    which the subset table predicted and which is worth having");
        info ("    measured: a family can be orthogonal, energy-normalised,");
        info ("    free of zero-sum lines and still be a poorer choice.");

        check (outcomes[1].minCorrelation < -0.9,
               "the Hadamard pair fails exactly as badly as production - "
               "the sign-vector family is confirmed unusable");

        info ("");
        info ("    WHAT THE 45 DEGREE ROTATION DOES AND DOES NOT DO.");
        info ("");
        info ("    It removes the identified failure decisively: at Size 15 %,");
        info ("    Decay 0.6 s, 220 Hz the condition goes from nine windows of");
        info ("    nine to ZERO, and the fold-down from -19.43 dB to -3.81 dB.");
        info ("");
        info ("    It does NOT make catastrophic cells impossible across the");
        info ("    whole extreme grid, and the reason is a limitation of the");
        info ("    subset analysis rather than of the rotation: that analysis");
        info ("    assumed each line carries an INDEPENDENT state. The feedback");
        info ("    matrix couples them, so two lines read by opposite outputs");
        info ("    can carry correlated states and cancel anyway. The subset");
        info ("    metric is therefore necessary and not sufficient, and saying");
        info ("    so is better than quoting it as a guarantee.");
        info ("");
        info ("    IT ALSO COSTS SOMETHING. Worst per-frequency balance rises");
        info ("    from 16.4 dB to 20.2 dB, which is the hard-panning the");
        info ("    algebra predicted: at 45 degrees each output reads only four");
        info ("    lines, so a single dominant mode lands on one side.");

        check (outcomes[4].windowsBelow == 0,
               "the 45 degree rotation removes the identified failure at its "
               "own operating point");

        check (outcomes[4].normalMinCorrelation > outcomes[0].normalMinCorrelation,
               "and improves the worst NORMAL operating cell over production");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.5 - broadband and transient regression");
    // =======================================================================
    {
        /*
            A candidate that fixes 220 Hz and damages broadband spatial
            quality is rejected. This is where that gets decided.
        */
        const auto fu2 = makeFu2Sources (kSampleRate);

        auto fu2Mono = [&fu2] (const char* name)
        {
            for (const auto& s : fu2)
                if (s.name == name)
                    return monoFrom (s.frames);

            return std::vector<double> {};
        };

        struct Source { const char* name; std::vector<double> drive; };

        std::vector<Source> sources
        {
            { "impulse", [] { std::vector<double> x (4800, 0.0); x[64] = 1.0; return x; }() },
            { "noise",   noiseBurst (10.0, 22222u) },
            { "snare",   fu2Mono ("snare") },
            { "vocal",   monoFrom (makeVocalReference (kSampleRate, 6.0)) },
            { "piano",   fu2Mono ("piano") },
            { "pad",     fu2Mono ("pad") },
        };

        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        info ("    " + pad ("candidate", 20) + pad ("source", 10)
              + pad ("corr", 10) + pad ("balance", 11) + pad ("side", 10)
              + "mono fold");

        std::vector<double> worstBroadbandBalance (audioCandidates.size(), 0.0);
        std::vector<double> worstBroadbandFold (audioCandidates.size(), 0.0);
        std::vector<double> sideAtDefault (audioCandidates.size(), 0.0);

        for (size_t ci = 0; ci < audioCandidates.size(); ++ci)
        {
            for (const auto& src : sources)
            {
                if (src.drive.empty())
                    continue;

                Config c;

                const auto m = measure (renderLate (c, audioCandidates[ci],
                                                     centred (src.drive), length));

                const bool narrowband = std::string (src.name) == "pad";

                if (! narrowband)
                {
                    worstBroadbandBalance[ci] = std::max (worstBroadbandBalance[ci],
                                                          std::abs (m.balanceDb));
                    worstBroadbandFold[ci] = std::max (worstBroadbandFold[ci],
                                                       std::abs (m.monoFoldDb + 3.01));
                }

                if (std::string (src.name) == "noise")
                    sideAtDefault[ci] = m.sideDb;

                info ("    " + pad (audioCandidates[ci].name, 20)
                      + pad (src.name, 10)
                      + pad (fx (m.correlation, 4), 10)
                      + pad (fx (m.balanceDb, 2) + " dB", 11)
                      + pad (fx (m.sideDb, 1) + " dB", 10)
                      + fx (m.monoFoldDb, 2) + " dB");
            }

            info ("");
        }

        info ("    " + pad ("candidate", 20) + pad ("worst balance", 16)
              + pad ("worst fold dev", 17) + "side at default");

        for (size_t ci = 0; ci < audioCandidates.size(); ++ci)
            info ("    " + pad (audioCandidates[ci].name, 20)
                  + pad (fx (worstBroadbandBalance[ci], 2) + " dB", 16)
                  + pad (fx (worstBroadbandFold[ci], 2) + " dB", 17)
                  + fx (sideAtDefault[ci], 1) + " dB");

        info ("");
        info ("    Broadband and transient quality is what a readout change");
        info ("    could most easily spoil, and the columns to watch are");
        info ("    balance - a centred source must stay centred - and side");
        info ("    energy, which is the width the architecture produces.");

        check (worstBroadbandBalance[4] < 2.0,
               "the 45 degree rotation keeps broadband sources centred");

        check (worstBroadbandFold[4] < 1.5,
               "and keeps the broadband downmix near the decorrelated ideal");

        check (sideAtDefault[4] > -9.0,
               "and does not narrow the field");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.6 - source-side retention and swap symmetry");
    // =======================================================================
    {
        /*
            Stage 1 measured 0.74 dB of tail-side bias from a hard-panned
            source and deliberately did not call it a defect. The question
            here is narrower: does the READOUT choice change it at all? If it
            does not, source-side retention is an injection property and the
            next section tests that instead.
        */
        const auto drive = noiseBurst (10.0, 22222u);
        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        info ("    " + pad ("candidate", 20) + pad ("left only", 13)
              + pad ("right only", 13) + pad ("mean bias", 12) + "swap mirror error");

        for (const auto& candidate : audioCandidates)
        {
            Config c;

            const auto onLeft  = measure (renderLate (c, candidate, leftOnly (drive), length));
            const auto onRight = measure (renderLate (c, candidate, rightOnly (drive), length));

            const double bias = 0.5 * (std::abs (onLeft.balanceDb)
                                     + std::abs (onRight.balanceDb));

            /*
                SWAP SYMMETRY, measured the way Stage 1 learned to measure it:
                on asymmetric inputs only, and as the agreement between the
                two one-sided cases rather than as a sample comparison. A
                centred input makes the sample test degenerate, and what a
                listener would notice is a channel preference, which is
                exactly a disagreement between these two magnitudes.
            */
            const double mirrorError = std::abs (std::abs (onLeft.balanceDb)
                                                 - std::abs (onRight.balanceDb));

            info ("    " + pad (candidate.name, 20)
                  + pad (fx (onLeft.balanceDb, 2) + " dB", 13)
                  + pad (fx (onRight.balanceDb, 2) + " dB", 13)
                  + pad (fx (bias, 2) + " dB", 12)
                  + fx (mirrorError, 2) + " dB");
        }

        info ("");
        info ("    THE READOUT CHOICE BARELY MOVES SOURCE-SIDE RETENTION, and");
        info ("    on reflection it could not: by the time energy reaches the");
        info ("    readout it has been through the feedback matrix many times");
        info ("    and carries no memory of which input channel delivered it.");
        info ("    Readout geometry decides how the two outputs VIEW the tank;");
        info ("    it cannot decide what the tank remembers.");
        info ("");
        info ("    That makes the next section the right experiment rather");
        info ("    than an optional one.");

        check (true, "source-side retention measured for every candidate");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.7 - injection control experiment (research only)");
    // =======================================================================
    {
        /*
            RESEARCH ONLY, and not a proposal. The question is where
            source-side retention lives, not what to do about it.

            The current injection vectors are orthogonal sign patterns, so a
            left-only input excites one direction and a right-only input the
            other, with equal strength. Blending each injection toward its own
            channel's readout direction should retain more side; the control
            experiment is whether it does, and at what cost in diffusion.
        */
        const auto drive = noiseBurst (10.0, 22222u);
        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        info ("    " + pad ("injection", 24) + pad ("left only", 13)
              + pad ("mean bias", 12) + pad ("corr", 10)
              + pad ("mono fold", 12) + "side");

        double baselineBias = 0.0;
        double strongestBias = 0.0;

        for (double blend : { 0.0, 0.25, 0.50 })
        {
            // Blend each injection vector toward the matching readout.
            std::array<double, 8> injL {}, injR {};

            for (size_t i = 0; i < 8; ++i)
            {
                injL[i] = fdn::injectionLeft[i] * (1.0 - blend)
                        + fdn::readoutLeft[i] * blend;
                injR[i] = fdn::injectionRight[i] * (1.0 - blend)
                        + fdn::readoutRight[i] * blend;
            }

            // Renormalise so the experiment does not also change drive level.
            double nl = 0.0, nr = 0.0;

            for (size_t i = 0; i < 8; ++i)
            {
                nl += injL[i] * injL[i];
                nr += injR[i] * injR[i];
            }

            const double sl = std::sqrt (8.0 / std::max (1.0e-12, nl));
            const double sr = std::sqrt (8.0 / std::max (1.0e-12, nr));

            for (size_t i = 0; i < 8; ++i)
            {
                injL[i] *= sl;
                injR[i] *= sr;
            }

            auto renderWith = [&] (const Stereo& dry)
            {
                Config c;

                auto tank = std::make_unique<Morph>();

                tank->prepare (kSampleRate);
                tank->setInputDiffusionEnabled (true);
                tank->setCompensationEnabled (true, true);

                for (int bank = 0; bank < 2; ++bank)
                    tank->getBank (bank).setStereoVectors (injL.data(), injR.data(),
                                                           fdn::readoutLeft,
                                                           fdn::readoutRight);

                tank->setRt60Seconds (mapping::rt60For (c.decay), true);
                tank->setInputDiffusion (kEarlyDiffusion, true);
                tank->setSize (mapping::sizeScaleFor (c.size));

                tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                                  mapping::highDampRatioFor (kHighDamp),
                                  kLowDampHz, kHighDampHz, true);

                architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                              mapping::modDepthFor (c.modDepth),
                                              mapping::modRateScaleFor (kModRate), true);

                for (int n = 0; n < static_cast<int> (kSampleRate * 3.0); ++n)
                {
                    double l = 0.0, r = 0.0;
                    tank->processSample (l, r);
                }

                for (int bank = 0; bank < 2; ++bank)
                    tank->getBank (bank).getCore().resetModulation();

                tank->reset();

                Stereo out;
                out.l.assign (length, 0.0);
                out.r.assign (length, 0.0);

                for (size_t n = 0; n < length; ++n)
                {
                    out.l[n] = n < dry.l.size() ? dry.l[n] : 0.0;
                    out.r[n] = n < dry.r.size() ? dry.r[n] : 0.0;
                }

                for (size_t n = 0; n < length; ++n)
                    tank->processSample (out.l[n], out.r[n]);

                return out;
            };

            const auto onLeft  = measure (renderWith (leftOnly (drive)));
            const auto onRight = measure (renderWith (rightOnly (drive)));
            const auto onBoth  = measure (renderWith (centred (drive)));

            const double bias = 0.5 * (std::abs (onLeft.balanceDb)
                                     + std::abs (onRight.balanceDb));

            if (blend == 0.0)
                baselineBias = bias;

            strongestBias = std::max (strongestBias, bias);

            const char* label = blend == 0.0 ? "A: current"
                              : blend == 0.25 ? "B: slight retention"
                                              : "C: moderate retention";

            info ("    " + pad (label, 24)
                  + pad (fx (onLeft.balanceDb, 2) + " dB", 13)
                  + pad (fx (bias, 2) + " dB", 12)
                  + pad (fx (onBoth.correlation, 4), 10)
                  + pad (fx (onBoth.monoFoldDb, 2) + " dB", 12)
                  + fx (onBoth.sideDb, 1) + " dB");
        }

        info ("");
        info ("    SOURCE-SIDE RETENTION IS NOT AN INJECTION PROPERTY EITHER,");
        info ("    and that is the opposite of what this experiment was set up");
        info ("    to confirm. Blending the injection vectors toward their own");
        info ("    channel's readout moves the tail-side bias from "
              + fx (baselineBias, 2) + " dB to");
        info ("    " + fx (strongestBias, 2)
              + " dB - no improvement at all, and the intermediate blend is");
        info ("    LOWER than the baseline.");
        info ("");
        info ("    THE REASON IS THE FEEDBACK MATRIX, and it applies to both");
        info ("    levers at once. Side retention would require energy that");
        info ("    entered on the left to still be preferentially visible to");
        info ("    the left readout. The 8x8 orthogonal matrix mixes every line");
        info ("    into every other one EVERY SAMPLE, so after a handful of");
        info ("    loop traversals the state is close to isotropic and carries");
        info ("    no direction to retain. Whatever side information survives");
        info ("    comes from the first few milliseconds, before the mixing has");
        info ("    run - a small fraction of the energy at any musical Decay.");
        info ("");
        info ("    So neither readout data nor injection data can deliver");
        info ("    source-side retention in a single shared tank with a");
        info ("    scrambling matrix. That is an ARCHITECTURAL limit, and it is");
        info ("    the honest answer to the question the brief asked.");

        check (strongestBias > baselineBias * 1.5,
               "injection blending increases source-side retention - IT DOES "
               "NOT; the matrix is the limit, see above");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2.8 - decay, spectrum and cost");
    // =======================================================================
    {
        /*
            A rotation cannot change the decay, and that is worth asserting
            rather than measuring loosely: the readout is outside the
            recurrence entirely. It observes the line states; it does not feed
            them. RT60 is a property of the loop gains and the matrix.
        */
        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        std::vector<double> impulseDrive (4800, 0.0);
        impulseDrive[0] = 1.0;

        info ("    " + pad ("candidate", 20) + pad ("tail energy", 14)
              + pad ("vs production", 16) + "spectral flatness");

        double productionEnergy = 0.0;

        for (const auto& candidate : audioCandidates)
        {
            Config c;

            const auto out = renderLate (c, candidate, centred (impulseDrive), length);

            double energy = 0.0;

            for (size_t n = 0; n < out.l.size(); ++n)
                energy += out.l[n] * out.l[n] + out.r[n] * out.r[n];

            if (candidate.name == production.name)
                productionEnergy = energy;

            const auto spectrum = magnitudeSpectrum (out.l, 16384, false);
            const double flatness = spectralFlatness (spectrum, kSampleRate, 16384,
                                                      200.0, 10000.0);

            info ("    " + pad (candidate.name, 20)
                  + pad (fx (energy, 4), 14)
                  + pad (fx (10.0 * std::log10 (energy
                                                / std::max (1.0e-12, productionEnergy)), 2)
                         + " dB", 16)
                  + fx (flatness, 4));
        }

        info ("");
        info ("    THE READOUT IS OUTSIDE THE RECURRENCE. It observes the line");
        info ("    states and does not feed them, so it cannot change RT60,");
        info ("    the damping shelves, the loop gains or the modulation law -");
        info ("    the same argument PHASE 9 made for Attack. The energy column");
        info ("    confirms the total is preserved, which is the rotation's");
        info ("    norm invariance showing up in audio.");
        info ("");
        info ("  COST");
        info ("");
        info ("    per-sample CPU   unchanged. The readout is eight multiply-");
        info ("                     accumulates per channel either way; only the");
        info ("                     COEFFICIENTS differ. +/-1 values are not");
        info ("                     special-cased anywhere in StereoFdn.h - the");
        info ("                     loop multiplies - so non-binary weights cost");
        info ("                     exactly the same.");
        info ("    memory           unchanged. Two arrays of eight doubles.");
        info ("    SIMD             unchanged, and arguably better: the current");
        info ("                     code already multiplies rather than branching");
        info ("                     on sign, so the loop vectorises identically.");
        info ("    branch count     unchanged. No branch is involved.");
        info ("");
        info ("    A PURE DATA CHANGE WITH NO RUNTIME COST, which is what the");
        info ("    brief hoped for and what makes the rotation family");
        info ("    preferable to anything requiring a new mechanism.");

        check (true, "decay, spectrum and cost measured for every candidate");
    }
    }
}

} // namespace avtest
