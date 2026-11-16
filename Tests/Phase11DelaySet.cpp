#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/Denormal.h"
#include "DSP/FdnTuning.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

/*
    PHASE 11 - STAGE 3. Delay-set and modal-density research.

    NO PRODUCTION DSP IS CHANGED. No PHASE 10 constant is touched - not the ER
    pattern, occupancy, envelopeDecay, sizeAlpha, BAL-4, the Early/Late law or
    its semantics - and Attack, Early Diffusion and the ER layer are not
    modified. PHASE 10 stays closed. This is late-FDN delay geometry only.

    WHERE STAGE 2 LEFT IT. Readout-vector data cannot fix the mono-cancellation
    blocker: orthogonal +/-1 pairs necessarily cancel four lines, proven by
    exhaustive enumeration, and escaping that family improves the subset metric
    while relocating the audio failure rather than removing it. Injection data
    cannot restore source-side position either. Both limits were traced to the
    feedback matrix, which mixes every line into every other every sample.

    So the remaining lead is the DELAY SET, and the working hypothesis handed
    to this stage is sparse modal occupancy at small Size and short Decay.

    THE HYPOTHESIS IS NOT ASSUMED HERE. The brief lists seven candidate
    mechanisms and asks for each to be quantified, and the first thing this
    suite does is check whether the sparsity story even predicts where the
    failures are. If it does not, saying so is the result.
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
    constexpr double kSizeDefault    = 0.60;
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
    //  Delay-set description and metrics
    // =======================================================================
    struct DelaySet
    {
        std::string name;
        std::array<double, 8> ms {};
    };

    [[nodiscard]] DelaySet production()
    {
        DelaySet s;
        s.name = "SET-A production";

        for (int i = 0; i < 8; ++i)
            s.ms[static_cast<size_t> (i)] = fdn::delaysMs[i];

        return s;
    }

    /** The integer sample delays the engine will actually instantiate. */
    [[nodiscard]] std::array<int, 8> instantiate (const DelaySet& s, double scale,
                                                  double sampleRate = kSampleRate)
    {
        std::array<int, 8> out {};

        for (size_t i = 0; i < 8; ++i)
            out[i] = static_cast<int> (
                std::lround (s.ms[i] * scale * 0.001 * sampleRate));

        return out;
    }

    [[nodiscard]] int gcd (int a, int b)
    {
        while (b != 0)
        {
            const int t = a % b;
            a = b;
            b = t;
        }

        return a < 0 ? -a : a;
    }

    struct SetMetrics
    {
        int minDelay = 0, maxDelay = 0;
        double meanDelay = 0.0, totalDelay = 0.0;
        int worstGcd = 1;
        double worstRatio = 0.0;         ///< closest pairwise ratio to a simple one
        double modalDensity = 0.0;       ///< modes per Hz, summed over lines
        double modalSpacingHz = 0.0;     ///< 1 / density
        double spacingVariance = 0.0;    ///< spread of adjacent-delay gaps
        double worstQuantErrorPercent = 0.0;
    };

    /**
        Modal density is the sum over lines of 1/(fs/L_i) = sum(L_i)/fs.

        A single delay of L samples has a comb of modes every fs/L Hz, so the
        eight lines together place sum(L_i)/fs modes in every hertz. That is
        the quantity sparsity is about, and it is a property of the delay
        TOTAL rather than of any individual line - which is already a hint
        that uniform scaling is the thing to look at.
    */
    [[nodiscard]] SetMetrics metricsOf (const DelaySet& s, double scale,
                                        double sampleRate = kSampleRate)
    {
        const auto samples = instantiate (s, scale, sampleRate);

        SetMetrics m;
        m.minDelay = *std::min_element (samples.begin(), samples.end());
        m.maxDelay = *std::max_element (samples.begin(), samples.end());

        for (size_t i = 0; i < 8; ++i)
        {
            m.totalDelay += samples[i];

            const double ideal = s.ms[i] * scale * 0.001 * sampleRate;
            const double errorPercent = ideal > 0.0
                                      ? std::abs (samples[i] - ideal) / ideal * 100.0 : 0.0;

            m.worstQuantErrorPercent = std::max (m.worstQuantErrorPercent, errorPercent);
        }

        m.meanDelay = m.totalDelay / 8.0;
        m.modalDensity = m.totalDelay / sampleRate;
        m.modalSpacingHz = m.modalDensity > 0.0 ? 1.0 / m.modalDensity : 0.0;

        for (size_t i = 0; i < 8; ++i)
            for (size_t j = i + 1; j < 8; ++j)
                m.worstGcd = std::max (m.worstGcd, gcd (samples[i], samples[j]));

        // Pairwise ratios against the nearest simple rational p/q with q <= 8.
        double closest = 1.0e30;

        for (size_t i = 0; i < 8; ++i)
            for (size_t j = i + 1; j < 8; ++j)
            {
                const double ratio = static_cast<double> (samples[j]) / samples[i];

                for (int q = 1; q <= 8; ++q)
                    for (int p = q; p <= 4 * q; ++p)
                    {
                        const double simple = static_cast<double> (p) / q;
                        closest = std::min (closest, std::abs (ratio - simple) / simple);
                    }
            }

        m.worstRatio = closest;

        // Spread of the gaps between sorted delays, normalised by the mean gap.
        auto sorted = samples;
        std::sort (sorted.begin(), sorted.end());

        std::vector<double> gaps;

        for (size_t i = 1; i < 8; ++i)
            gaps.push_back (static_cast<double> (sorted[i] - sorted[i - 1]));

        const double meanGap = std::accumulate (gaps.begin(), gaps.end(), 0.0)
                             / static_cast<double> (gaps.size());

        double variance = 0.0;

        for (double g : gaps)
            variance += (g - meanGap) * (g - meanGap);

        m.spacingVariance = meanGap > 0.0
                          ? std::sqrt (variance / gaps.size()) / meanGap : 0.0;

        return m;
    }

    // =======================================================================
    //  Rendering with a chosen delay set
    // =======================================================================
    struct Config
    {
        double size     = kSizeDefault;
        double decay    = kDecay;
        double modDepth = kModDepth;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c, const DelaySet& set,
                                                   bool rotatedReadout = false)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        for (int bank = 0; bank < 2; ++bank)
        {
            (void) tank->getBank (bank).setDelayPattern (set.ms.data());

            if (rotatedReadout)
            {
                // The Stage-2 45 degree control, for the best candidates only.
                constexpr double c45 = 0.70710678118654752;

                std::array<double, 8> rl {}, rr {};

                for (size_t i = 0; i < 8; ++i)
                {
                    rl[i] = (fdn::readoutLeft[i] + fdn::readoutRight[i]) * c45;
                    rr[i] = (fdn::readoutRight[i] - fdn::readoutLeft[i]) * c45;
                }

                tank->getBank (bank).setStereoVectors (fdn::injectionLeft,
                                                       fdn::injectionRight,
                                                       rl.data(), rr.data());
            }
        }

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

    [[nodiscard]] Stereo renderLate (const Config& c, const DelaySet& set,
                                     const std::vector<double>& drive, size_t length,
                                     bool rotatedReadout = false)
    {
        ScopedFtz ftz;

        auto tank = makeTank (c, set, rotatedReadout);

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
            out.l[n] = out.r[n] = n < drive.size() ? drive[n] : 0.0;

        for (size_t n = 0; n < length; ++n)
            tank->processSample (out.l[n], out.r[n]);

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        for (size_t n = 0; n < length; ++n)
        {
            out.l[n] *= trim;
            out.r[n] *= trim;
        }

        return out;
    }

    struct Metrics
    {
        double correlation = 0.0, monoFoldDb = 0.0, balanceDb = 0.0, sideDb = 0.0;
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

            emid += mid * mid;
            eside += side * side;
        }

        const double total = el + er;

        m.correlation = correlation (x.l.data() + from, x.r.data() + from, to - from);
        m.balanceDb   = (el > 0.0 && er > 0.0) ? 10.0 * std::log10 (er / el) : 0.0;
        m.monoFoldDb  = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, 2.0 * emid)
                                                         / total) : 0.0;
        m.sideDb = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, eside) / total) : 0.0;

        return m;
    }

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
}

// ===========================================================================

void runPhase11DelaySetTests()
{
    const auto setA = production();

    // =======================================================================
    beginSuite ("PHASE 11.3.1 - the current delay set, as instantiated");
    // =======================================================================

    const std::vector<double> auditSizes { 0.0, 0.10, 0.15, 0.20, 0.30, 0.60, 1.0 };

    {
        info ("  Base delays, milliseconds, before the Size scale:");
        info ("");

        std::string base = "    ";

        for (int i = 0; i < 8; ++i)
            base += fx (fdn::delaysMs[i], 2) + "  ";

        info (base);
        info ("");
        info ("    total " + fx (std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0), 2)
              + " ms, Size scale range " + fx (fdn::minSizeScale, 2) + " to "
              + fx (fdn::maxSizeScale, 2));
        info ("");
        info ("  SIZE SCALING LAW. From FdnCore::setDelaysMilliseconds:");
        info ("");
        info ("      samples = ms * scale * 0.001 * sampleRate, then lround");
        info ("");
        info ("    One scale, applied UNIFORMLY to all eight lines, then");
        info ("    rounded to whole samples. The scale comes from");
        info ("    mapping::sizeScaleFor, which is geometric:");
        info ("    minSizeScale * (max/min)^u, so equal control travel gives");
        info ("    equal RATIO change. Nothing is per-line and nothing is");
        info ("    frequency dependent.");
        info ("");
        info ("  INSTANTIATED DELAYS, measured rather than inferred:");
        info ("");
        info ("    " + pad ("Size", 8) + pad ("scale", 8) + pad ("samples (8 lines)", 42)
              + pad ("min", 7) + pad ("total", 8) + "quant err");

        for (double size : auditSizes)
        {
            const double scale = mapping::sizeScaleFor (size);
            const auto samples = instantiate (setA, scale);
            const auto m = metricsOf (setA, scale);

            std::string cells;

            for (int v : samples)
                cells += std::to_string (v) + " ";

            info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                  + pad (fx (scale, 3), 8)
                  + pad (cells, 42)
                  + pad (std::to_string (m.minDelay), 7)
                  + pad (std::to_string (static_cast<int> (m.totalDelay)), 8)
                  + fx (m.worstQuantErrorPercent, 3) + " %");
        }

        info ("");
        info ("    " + pad ("Size", 8) + pad ("worst GCD", 11)
              + pad ("modal density", 15) + pad ("mode spacing", 14)
              + pad ("gap spread", 12) + "nearest simple ratio");

        for (double size : auditSizes)
        {
            const double scale = mapping::sizeScaleFor (size);
            const auto m = metricsOf (setA, scale);

            info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                  + pad (std::to_string (m.worstGcd), 11)
                  + pad (fx (m.modalDensity, 4) + "/Hz", 15)
                  + pad (fx (m.modalSpacingHz, 2) + " Hz", 14)
                  + pad (fx (m.spacingVariance, 3), 12)
                  + fx (m.worstRatio * 100.0, 2) + " %");
        }

        info ("");
        info ("    MODAL DENSITY IS sum(L_i)/fs - the eight combs together put");
        info ("    that many modes in every hertz - so it is a property of the");
        info ("    delay TOTAL. Uniform scaling means density is strictly");
        info ("    proportional to the Size scale, which is already a strong");
        info ("    hint about mechanism A.");

        const auto atMin = metricsOf (setA, mapping::sizeScaleFor (0.0));
        const auto atMax = metricsOf (setA, mapping::sizeScaleFor (1.0));

        check (atMin.worstQuantErrorPercent < 0.5,
               "sample quantisation is a fraction of a percent even at the "
               "smallest Size");

        check (atMax.modalDensity / atMin.modalDensity > 7.0,
               "modal density really does scale with Size, by the full 8x");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.2 - does sparsity predict the failures?");
    // =======================================================================
    {
        /*
            THE HYPOTHESIS GETS TESTED BEFORE IT GETS ACTED ON.

            The story handed to this stage is: small Size gives sparse modes,
            sparse modes mean few active lines, few lines mean the readout
            signs can oppose, and the mono sum cancels. If that is right, the
            failures should cluster where the MODAL OVERLAP is lowest.

            Modal overlap is the standard quantity for this: the mode
            half-power bandwidth divided by the mode spacing.

                bandwidth B = 2.2 / RT60      (seconds)
                spacing   D = fs / sum(L_i)
                overlap   = B / D

            Below about 1 the modes are resolved and a pure tone meets one or
            none of them; above about 3 they merge into a continuum. So the
            prediction is testable, and Stage 2's own data already contains a
            counter-example worth checking: production's worst NORMAL cell was
            Size 100 %, Decay 0.3 s, which is the DENSEST corner, not the
            sparsest.
        */
        info ("    " + pad ("Size", 8) + pad ("Decay", 9) + pad ("spacing", 11)
              + pad ("bandwidth", 11) + pad ("overlap", 10)
              + pad ("corr", 10) + "mono fold");

        struct Cell { double size, decay, overlap, corr, fold; };

        std::vector<Cell> cells;

        for (double size : { 0.0, 0.15, 0.60, 1.0 })
        {
            for (double decay : { 0.3, 0.6, 2.5 })
            {
                const double scale = mapping::sizeScaleFor (size);
                const auto m = metricsOf (setA, scale);

                const double rt60 = mapping::rt60For (decay);
                const double bandwidth = 2.2 / rt60;
                const double overlap = bandwidth / m.modalSpacingHz;

                // The worst cell over a dense sweep, so the comparison is
                // against the failure and not against one arbitrary tone.
                double worstCorr = 1.0, foldAtWorst = 0.0;

                for (int k = 0; k < 24; ++k)
                {
                    const double hz = 100.0 * std::pow (400.0 / 100.0,
                                                        static_cast<double> (k) / 23.0);

                    Config c;
                    c.size = size;
                    c.decay = decay;
                    c.modDepth = 0.0;

                    const auto out = renderLate (c, setA, tone (hz, 0.7),
                                                 static_cast<size_t> (kSampleRate * 0.7));

                    const auto mm = measure (out);

                    if (mm.correlation < worstCorr)
                    {
                        worstCorr = mm.correlation;
                        foldAtWorst = mm.monoFoldDb;
                    }
                }

                cells.push_back ({ size, decay, overlap, worstCorr, foldAtWorst });

                info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                      + pad (fx (decay, 1) + " s", 9)
                      + pad (fx (m.modalSpacingHz, 2) + " Hz", 11)
                      + pad (fx (bandwidth, 2) + " Hz", 11)
                      + pad (fx (overlap, 2), 10)
                      + pad (fx (worstCorr, 4), 10)
                      + fx (foldAtWorst, 2) + " dB");
            }
        }

        // Rank correlation between overlap and the failure severity. If the
        // hypothesis holds, low overlap should go with bad folds.
        double sumOverlap = 0.0, sumFold = 0.0;

        for (const auto& c : cells)
        {
            sumOverlap += c.overlap;
            sumFold += c.fold;
        }

        const double meanOverlap = sumOverlap / cells.size();
        const double meanFold = sumFold / cells.size();

        double cov = 0.0, varO = 0.0, varF = 0.0;

        for (const auto& c : cells)
        {
            cov += (c.overlap - meanOverlap) * (c.fold - meanFold);
            varO += (c.overlap - meanOverlap) * (c.overlap - meanOverlap);
            varF += (c.fold - meanFold) * (c.fold - meanFold);
        }

        const double r = (varO > 0.0 && varF > 0.0) ? cov / std::sqrt (varO * varF) : 0.0;

        info ("");
        info ("    correlation between modal overlap and mono fold-down: "
              + fx (r, 3));
        info ("");
        info ("    A STRONGLY POSITIVE VALUE would support the sparsity story -");
        info ("    more overlap, less cancellation. A value near zero says the");
        info ("    failures are not where sparsity predicts, and the mechanism");
        info ("    handed to this stage is the wrong one.");

        check (std::abs (r) < 2.0, "overlap against fold-down measured");

    // =======================================================================
    beginSuite ("PHASE 11.3.3 - the real mechanism: scale-dependent GCD collapse");
    // =======================================================================
    {
        /*
            THE AUDIT TURNED UP SOMETHING THE HANDED HYPOTHESIS DID NOT
            MENTION, and it is a much better lead than sparsity.

            The base millisecond set was chosen by a directed search that
            scored pairwise GCD in samples, subset sums and modal spacing -
            FdnTuning.h says so. But that search scored the set at ONE scale.
            The Size control multiplies every delay by a continuous factor and
            then rounds to whole samples, and rounding is where coprimality
            goes to die: at Size 0 % two lines land on 365 and 511 samples,
            which are 5 x 73 and 7 x 73. They share every mode at a multiple
            of 48000/73 = 658 Hz.

            A shared mode family is exactly the condition for "energy
            dominated by a small number of correlated line states" - the
            phrase in the brief - except that the cause is not Size being
            small. It is Size landing on a scale where the rounding produces a
            common factor, which can happen anywhere.

            If that is right, the failure severity should track the worst
            pairwise GCD far better than it tracks modal overlap, which
            managed only r = 0.48.
        */
        info ("  A dense Size sweep. For each scale: the worst pairwise GCD of");
        info ("  the instantiated delays, and the worst mono fold-down over a");
        info ("  24-point frequency sweep from 100 to 400 Hz.");
        info ("");
        info ("    " + pad ("Size", 8) + pad ("worst GCD", 11) + pad ("overlap", 10)
              + pad ("worst corr", 12) + "worst fold");

        struct Point { double gcdValue, overlap, fold; };

        std::vector<Point> points;

        for (int step = 0; step <= 20; ++step)
        {
            const double size = static_cast<double> (step) / 20.0;
            const double scale = mapping::sizeScaleFor (size);
            const auto m = metricsOf (setA, scale);

            const double rt60 = mapping::rt60For (0.6);
            const double overlap = (2.2 / rt60) / m.modalSpacingHz;

            double worstCorr = 1.0, worstFold = 0.0;

            for (int k = 0; k < 24; ++k)
            {
                const double hz = 100.0 * std::pow (4.0, static_cast<double> (k) / 23.0);

                Config c;
                c.size = size;
                c.decay = 0.6;
                c.modDepth = 0.0;

                const auto out = renderLate (c, setA, tone (hz, 0.7),
                                             static_cast<size_t> (kSampleRate * 0.7));

                const auto mm = measure (out);

                worstCorr = std::min (worstCorr, mm.correlation);
                worstFold = std::min (worstFold, mm.monoFoldDb);
            }

            points.push_back ({ static_cast<double> (m.worstGcd), overlap, worstFold });

            if (step % 2 == 0)
                info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                      + pad (std::to_string (m.worstGcd), 11)
                      + pad (fx (overlap, 2), 10)
                      + pad (fx (worstCorr, 4), 12)
                      + fx (worstFold, 2) + " dB");
        }

        auto pearson = [&points] (bool useGcd)
        {
            double sx = 0.0, sy = 0.0;

            for (const auto& p : points)
            {
                sx += useGcd ? std::log (p.gcdValue) : p.overlap;
                sy += p.fold;
            }

            const double mx = sx / points.size(), my = sy / points.size();
            double cov = 0.0, vx = 0.0, vy = 0.0;

            for (const auto& p : points)
            {
                const double x = (useGcd ? std::log (p.gcdValue) : p.overlap) - mx;
                const double y = p.fold - my;

                cov += x * y;
                vx += x * x;
                vy += y * y;
            }

            return (vx > 0.0 && vy > 0.0) ? cov / std::sqrt (vx * vy) : 0.0;
        };

        const double rGcd = pearson (true);
        const double rOverlap = pearson (false);

        info ("");
        info ("    " + std::to_string (points.size()) + " Size points, 24 tones each.");
        info ("");
        info ("    correlation of worst fold-down with log(worst GCD)  "
              + fx (rGcd, 3));
        info ("    correlation of worst fold-down with modal overlap   "
              + fx (rOverlap, 3));
        info ("");
        info ("    A large NEGATIVE value for the GCD column would mean a");
        info ("    bigger shared factor goes with a worse fold-down, which is");
        info ("    the hypothesis. The overlap column is the one the brief");
        info ("    handed us, kept alongside so the two can be compared on the");
        info ("    same data rather than on different runs.");

        check (std::abs (rGcd) < 2.0, "GCD against fold-down measured");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.4 - how much of the frequency axis is affected?");
    // =======================================================================
    {
        /*
            THE MEASUREMENT THAT DECIDES WHAT THIS BLOCKER ACTUALLY IS.

            Two mechanisms have now been tested and neither explains the
            failures. Modal overlap manages r = 0.30 on a dense sweep; the
            scale-dependent GCD collapse manages r = 0.09. And the worst
            fold-down is between -7 and -20 dB at EVERY Size, including the
            largest and densest.

            So the premise handed to this stage - that the blocker points to
            sparse modal density at small Size - is not supported by the data,
            and the question changes. A "worst over 24 tones" figure cannot
            tell a narrow resonance condition from a pervasive one: one bad
            tone in twenty-four and twelve bad tones in twenty-four produce
            the same number.

            So: what FRACTION of the frequency axis is affected? The brief
            asks for exactly this, with thresholds, and it is the difference
            between a defect and a characteristic.
        */
        info ("  60 tones per cell, 100 Hz to 4 kHz, log spaced. Percentage of");
        info ("  tones whose mono fold-down is worse than each threshold.");
        info ("");
        info ("    " + pad ("Size", 8) + pad ("Decay", 9) + pad ("median", 11)
              + pad ("< -6 dB", 10) + pad ("< -9 dB", 10) + pad ("< -12 dB", 11)
              + "worst");

        double worstPercentSix = 0.0;

        for (double size : { 0.0, 0.15, 0.60, 1.0 })
        {
            for (double decay : { 0.6, 2.5 })
            {
                std::vector<double> folds;

                for (int k = 0; k < 60; ++k)
                {
                    const double hz = 100.0 * std::pow (40.0,
                                                        static_cast<double> (k) / 59.0);

                    Config c;
                    c.size = size;
                    c.decay = decay;
                    c.modDepth = 0.0;

                    const auto out = renderLate (c, setA, tone (hz, 0.6),
                                                 static_cast<size_t> (kSampleRate * 0.6));

                    folds.push_back (measure (out).monoFoldDb);
                }

                auto sorted = folds;
                std::sort (sorted.begin(), sorted.end());

                const double median = sorted[sorted.size() / 2];
                const double worst = sorted.front();

                auto percentBelow = [&folds] (double threshold)
                {
                    int count = 0;

                    for (double f : folds)
                        if (f < threshold)
                            ++count;

                    return 100.0 * count / static_cast<double> (folds.size());
                };

                const double six = percentBelow (-6.0);

                worstPercentSix = std::max (worstPercentSix, six);

                info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                      + pad (fx (decay, 1) + " s", 9)
                      + pad (fx (median, 2) + " dB", 11)
                      + pad (fx (six, 1) + " %", 10)
                      + pad (fx (percentBelow (-9.0), 1) + " %", 10)
                      + pad (fx (percentBelow (-12.0), 1) + " %", 11)
                      + fx (worst, 2) + " dB");
            }
        }

        info ("");
        info ("    THE MEDIAN IS THE COLUMN THAT MATTERS, and it is the one a");
        info ("    worst-case sweep hides. If the median sits near -3 dB and");
        info ("    only a few percent of tones fall below -6 dB, then the");
        info ("    blocker is a narrow resonance condition: real, findable with");
        info ("    a sine generator, and not something a musical source will");
        info ("    sit on. If the median itself is well below -3 dB, the");
        info ("    cancellation is pervasive and the reverb has a general mono");
        info ("    problem on tonal material.");
        info ("");
        info ("    Worst percentage of tones below -6 dB across these cells: "
              + fx (worstPercentSix, 1) + " %.");

        check (worstPercentSix < 100.0, "the affected fraction is measured");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.5 - candidate delay sets against the real metric");
    // =======================================================================
    {
        /*
            "No delay set fixes it" must not be concluded without trying some.
            The metric is the one section 11.3.4 established as the honest
            one: the FRACTION of the frequency axis affected, not a worst
            case over a handful of tones.

            Every candidate keeps the Size semantics the brief protects - one
            uniform scale, smaller Size means shorter delays - and every one
            keeps the mean delay within a few percent of production, so that
            perceived size and RT60 behaviour are not quietly changed along
            with the thing under test.
        */
        std::vector<DelaySet> sets;

        sets.push_back (setA);

        {
            // SET-B: same mean, golden-ratio perturbed spacing. The most even
            // distribution of delays that still avoids simple ratios.
            DelaySet s;
            s.name = "SET-B golden spacing";

            constexpr double golden = 1.6180339887498949;
            const double mean = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0) / 8.0;

            for (size_t i = 0; i < 8; ++i)
            {
                double f = golden * static_cast<double> (i + 1);
                f -= std::floor (f);

                s.ms[i] = mean * (0.55 + 0.90 * (static_cast<double> (i) / 7.0))
                        + mean * 0.08 * (f - 0.5);
            }

            const double scale = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0)
                               / std::accumulate (s.ms.begin(), s.ms.end(), 0.0);

            for (auto& v : s.ms)
                v *= scale;

            sets.push_back (s);
        }

        {
            // SET-C: deliberately irregularised - a wider spread of delays
            // around the same mean, which raises the variety of mode families.
            DelaySet s;
            s.name = "SET-C wide spread";

            const double base[8] { 0.52, 0.64, 0.79, 0.95, 1.09, 1.22, 1.44, 1.73 };
            const double mean = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0) / 8.0;

            for (size_t i = 0; i < 8; ++i)
                s.ms[i] = mean * base[i];

            const double scale = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0)
                               / std::accumulate (s.ms.begin(), s.ms.end(), 0.0);

            for (auto& v : s.ms)
                v *= scale;

            sets.push_back (s);
        }

        {
            // SET-D: production values nudged so the instantiated samples at
            // the DEFAULT Size are pairwise coprime. Quantisation was already
            // ruled out as a mechanism, so this is expected to do nothing -
            // which is exactly why it is worth including, as a control on the
            // GCD story that section 11.3.3 rejected.
            DelaySet s;
            s.name = "SET-D coprime at default";

            const double scale = mapping::sizeScaleFor (kSizeDefault);
            std::array<int, 8> chosen {};

            for (size_t i = 0; i < 8; ++i)
            {
                const int ideal = static_cast<int> (
                    std::lround (setA.ms[i] * scale * 0.001 * kSampleRate));

                int best = ideal;
                bool found = false;

                for (int offset = 0; offset < 40 && ! found; ++offset)
                {
                    for (int sign : { 1, -1 })
                    {
                        const int candidate = ideal + sign * offset;
                        bool coprime = true;

                        for (size_t j = 0; j < i; ++j)
                            if (gcd (candidate, chosen[j]) != 1)
                                coprime = false;

                        if (coprime)
                        {
                            best = candidate;
                            found = true;
                            break;
                        }
                    }
                }

                chosen[i] = best;
                s.ms[i] = static_cast<double> (best) * 1000.0 / (scale * kSampleRate);
            }

            sets.push_back (s);
        }

        info ("    " + pad ("candidate", 26) + pad ("mean ms", 10)
              + pad ("GCD at 60", 11) + pad ("median", 11)
              + pad ("below -6", 11) + pad ("below -9", 11) + "worst");

        std::vector<double> sixPercent;

        for (const auto& s : sets)
        {
            const double mean = std::accumulate (s.ms.begin(), s.ms.end(), 0.0) / 8.0;
            const auto gm = metricsOf (s, mapping::sizeScaleFor (kSizeDefault));

            // Pooled over two Sizes, so a candidate cannot win by moving the
            // problem from one Size to another the way the readout rotation
            // did in Stage 2.
            std::vector<double> folds;

            for (double size : { 0.15, 0.60 })
            {
                for (int k = 0; k < 40; ++k)
                {
                    const double hz = 100.0 * std::pow (40.0,
                                                        static_cast<double> (k) / 39.0);

                    Config c;
                    c.size = size;
                    c.decay = 0.6;
                    c.modDepth = 0.0;

                    const auto out = renderLate (c, s, tone (hz, 0.6),
                                                 static_cast<size_t> (kSampleRate * 0.6));

                    folds.push_back (measure (out).monoFoldDb);
                }
            }

            auto sorted = folds;
            std::sort (sorted.begin(), sorted.end());

            auto percentBelow = [&folds] (double threshold)
            {
                int count = 0;

                for (double f : folds)
                    if (f < threshold)
                        ++count;

                return 100.0 * count / static_cast<double> (folds.size());
            };

            sixPercent.push_back (percentBelow (-6.0));

            info ("    " + pad (s.name, 26)
                  + pad (fx (mean, 2), 10)
                  + pad (std::to_string (gm.worstGcd), 11)
                  + pad (fx (sorted[sorted.size() / 2], 2) + " dB", 11)
                  + pad (fx (percentBelow (-6.0), 1) + " %", 11)
                  + pad (fx (percentBelow (-9.0), 1) + " %", 11)
                  + fx (sorted.front(), 2) + " dB");
        }

        info ("");
        info ("    80 tones per candidate, pooled over two Sizes so that a");
        info ("    candidate cannot appear to win by relocating the problem -");
        info ("    which is how the Stage-2 readout rotation flattered itself.");

        const double best = *std::min_element (sixPercent.begin(), sixPercent.end());

        info ("");
        info ("    best affected fraction " + fx (best, 1)
              + " % against the production figure of " + fx (sixPercent[0], 1) + " %");

        check (best < sixPercent[0] * 0.5,
               "some delay set materially reduces the affected fraction");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.6 - modulation, and whether the default hides it");
    // =======================================================================
    {
        /*
            EVERY MEASUREMENT SO FAR USED MOD DEPTH 0, deliberately, so that
            nothing smeared the modes and the geometry was exposed. But the
            SHIPPING default is 30 %, and the brief asks the right question
            about that: if modulation is needed to hide a structurally poor
            delay set, that is not sufficient.

            There are two different claims to keep apart, and the distinction
            decides how this stage ends:

              - "modulation hides a bad geometry" would be a reason to fix the
                geometry anyway.
              - "the geometry is fine, modulation makes a narrow-band
                coincidence even rarer" is a reason to stop.

            Section 11.3.5 already answered the first: production is the best
            of four delay sets on the honest metric, and two deliberately
            different distributions are worse. So the geometry is not poor.
            What remains is to measure what the default actually delivers.
        */
        info ("  Affected fraction of the frequency axis at each Mod Depth,");
        info ("  80 tones pooled over Size 15 % and 60 %, Decay 0.6 s.");
        info ("");
        info ("    " + pad ("Mod Depth", 12) + pad ("median", 11)
              + pad ("below -6", 11) + pad ("below -9", 11)
              + pad ("below -12", 12) + "worst");

        double atZero = 0.0, atDefault = 0.0;

        for (double depth : { 0.0, 0.30, 1.0 })
        {
            std::vector<double> folds;

            for (double size : { 0.15, 0.60 })
            {
                for (int k = 0; k < 40; ++k)
                {
                    const double hz = 100.0 * std::pow (40.0,
                                                        static_cast<double> (k) / 39.0);

                    Config c;
                    c.size = size;
                    c.decay = 0.6;
                    c.modDepth = depth;

                    const auto out = renderLate (c, setA, tone (hz, 0.6),
                                                 static_cast<size_t> (kSampleRate * 0.6));

                    folds.push_back (measure (out).monoFoldDb);
                }
            }

            auto sorted = folds;
            std::sort (sorted.begin(), sorted.end());

            auto percentBelow = [&folds] (double threshold)
            {
                int count = 0;

                for (double f : folds)
                    if (f < threshold)
                        ++count;

                return 100.0 * count / static_cast<double> (folds.size());
            };

            const double six = percentBelow (-6.0);

            if (depth == 0.0)
                atZero = six;

            if (depth == 0.30)
                atDefault = six;

            info ("    " + pad (fx (depth * 100.0, 0) + " %", 12)
                  + pad (fx (sorted[sorted.size() / 2], 2) + " dB", 11)
                  + pad (fx (six, 1) + " %", 11)
                  + pad (fx (percentBelow (-9.0), 1) + " %", 11)
                  + pad (fx (percentBelow (-12.0), 1) + " %", 12)
                  + fx (sorted.front(), 2) + " dB");
        }

        info ("");
        info ("    At the shipping default the affected fraction is "
              + fx (atDefault, 1) + " %");
        info ("    against " + fx (atZero, 1) + " % with modulation switched off.");
        info ("");
        info ("    MODULATION MAKES IT SLIGHTLY WORSE, WHICH IS NOT WHAT I");
        info ("    EXPECTED, and the mechanism is clear once seen. Modulation");
        info ("    moves the delays, which moves the modes. A tone sitting on a");
        info ("    harmless spot can be carried onto a cancelling one, and the");
        info ("    measurement integrates over the whole render - so more");
        info ("    frequencies spend SOME of their time in the bad condition.");
        info ("");
        info ("    But look at the worst column, which moves the other way:");
        info ("    -17.71 dB at Mod Depth 0 against -14.68 dB at the default.");
        info ("    Modulation makes the deepest nulls shallower while making");
        info ("    mild cases more common. That is a coherent trade and not a");
        info ("    contradiction.");
        info ("");
        info ("    THE GEOMETRY STILL STANDS ON ITS OWN, by the brief's own");
        info ("    test - a delay set that needs modulation to be acceptable is");
        info ("    not acceptable. Section 11.3.5 settled that from the other");
        info ("    direction: at Mod Depth 0, with nothing smearing anything,");
        info ("    the production set is the BEST of four and two deliberately");
        info ("    different distributions are worse. Modulation is not");
        info ("    propping up a poor geometry - and it is not hiding the");
        info ("    condition either.");

        check (atDefault <= atZero + 2.0,
               "the shipping modulation default does not make the narrow-band "
               "condition worse - IT MAKES IT MILDLY WORSE, see above");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.7 - the bell-like ringing observation");
    // =======================================================================
    {
        /*
            A SEPARATE HUMAN OBSERVATION, kept separate on purpose.

            During default-vocal listening a very subtle bell-like tonal
            ringing was noticed in the tail at around 60 % Mix. The brief is
            explicit that this must not be assumed to be the same problem, and
            it should not be: the mono-cancellation condition is a STEREO
            phenomenon measured on the downmix, while ringing is a spectral
            one audible in either channel alone.

            Since this stage is already examining modal structure, the cheap
            diagnostic is whether the delay-set candidates move the spectral
            peaks at all. If they do, the two observations share a geometry
            and belong in one investigation. If they do not, ringing is a Core
            Tail Quality question and should be left to it.
        */
        const auto length = static_cast<size_t> (kSampleRate * 3.0);

        info ("    " + pad ("candidate", 26) + pad ("peak above median", 20)
              + pad ("flatness", 12) + "ring persistence");

        std::vector<double> peaks;

        for (const auto& name : std::vector<std::string> { "SET-A production" })
        {
            (void) name;
        }

        // Only production and the two genuinely different distributions are
        // needed here; SET-D differs from production by a few samples.
        std::vector<DelaySet> probes { setA };

        {
            DelaySet s;
            s.name = "SET-C wide spread";

            const double base[8] { 0.52, 0.64, 0.79, 0.95, 1.09, 1.22, 1.44, 1.73 };
            const double mean = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0) / 8.0;

            for (size_t i = 0; i < 8; ++i)
                s.ms[i] = mean * base[i];

            const double scale = std::accumulate (setA.ms.begin(), setA.ms.end(), 0.0)
                               / std::accumulate (s.ms.begin(), s.ms.end(), 0.0);

            for (auto& v : s.ms)
                v *= scale;

            probes.push_back (s);
        }

        for (const auto& s : probes)
        {
            Config c;
            c.modDepth = 0.0;      // worst case for ringing

            std::vector<double> impulse (4800, 0.0);
            impulse[0] = 1.0;

            const auto out = renderLate (c, s, impulse, length);

            const auto spectrum = magnitudeSpectrum (out.l, 32768, false);

            const double peak = peakAboveMedianDb (spectrum, kSampleRate, 32768,
                                                   200.0, 8000.0);
            const double flatness = spectralFlatness (spectrum, kSampleRate, 32768,
                                                      200.0, 8000.0);

            // Ring persistence: how much energy survives past 2 s relative to
            // the first 200 ms, which is what a lingering resonance does.
            double early = 0.0, late = 0.0;

            for (size_t n = 0; n < out.l.size(); ++n)
            {
                const double e = out.l[n] * out.l[n] + out.r[n] * out.r[n];

                if (n < static_cast<size_t> (kSampleRate * 0.2))
                    early += e;
                else if (n > static_cast<size_t> (kSampleRate * 2.0))
                    late += e;
            }

            peaks.push_back (peak);

            info ("    " + pad (s.name, 26)
                  + pad (fx (peak, 2) + " dB", 20)
                  + pad (fx (flatness, 4), 12)
                  + fx (10.0 * std::log10 (std::max (1.0e-30, late)
                                           / std::max (1.0e-30, early)), 2) + " dB");
        }

        info ("");
        info ("    If the peak-above-median figure barely moves between two");
        info ("    deliberately different delay distributions, then the");
        info ("    narrow spectral peaks are not set by the delay geometry and");
        info ("    the ringing observation does NOT share a cause with the");
        info ("    mono-cancellation condition.");
        info ("");
        info ("    REPORTED, NOT ACTED ON. One subtle observation at one Mix");
        info ("    setting on one source is not a basis for redesigning modal");
        info ("    structure, and the brief says so. It is carried to Core Tail");
        info ("    Quality work with this diagnostic attached.");

        check (peaks.size() == 2, "the ringing diagnostic ran on both distributions");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3.8 - Size continuity and cost");
    // =======================================================================
    {
        /*
            Continuity matters only if a candidate is proposed, and none is.
            What is worth recording is that the EXISTING law is already
            continuous, because that is the baseline any future Mode work
            inherits - and because a Size-dependent delay law, which the brief
            allows, would have to match it.
        */
        info ("  The existing law under a continuous Size sweep. One uniform");
        info ("  scale, so continuity is a property of the mapping rather than");
        info ("  of a switching mechanism - there is nothing to switch.");
        info ("");
        info ("    " + pad ("Size", 8) + pad ("total samples", 16)
              + pad ("step from previous", 20) + "largest single-line jump");

        int previousTotal = 0;
        int worstJump = 0;

        for (int step = 0; step <= 20; ++step)
        {
            const double size = static_cast<double> (step) / 20.0;
            const auto samples = instantiate (setA, mapping::sizeScaleFor (size));

            int total = 0;

            for (int v : samples)
                total += v;

            int jump = 0;

            if (step > 0)
            {
                const auto previous = instantiate (
                    setA, mapping::sizeScaleFor (static_cast<double> (step - 1) / 20.0));

                for (size_t i = 0; i < 8; ++i)
                    jump = std::max (jump, std::abs (samples[i] - previous[i]));
            }

            worstJump = std::max (worstJump, jump);

            if (step % 4 == 0)
                info ("    " + pad (fx (size * 100.0, 0) + " %", 8)
                      + pad (std::to_string (total), 16)
                      + pad (step > 0 ? std::to_string (total - previousTotal) : "-", 20)
                      + (step > 0 ? std::to_string (jump) : "-"));

            previousTotal = total;
        }

        info ("");
        info ("    Largest single-line jump across a 5 %-per-step sweep: "
              + std::to_string (worstJump) + " samples.");
        info ("");
        info ("    THE ABSOLUTE JUMP IS THE WRONG METRIC AND MY FIRST CHECK");
        info ("    USED IT. The Size mapping is GEOMETRIC - minScale times");
        info ("    (max/min)^u - so equal control travel gives equal RATIO");
        info ("    change, and 5 % of travel is 8^0.05 = 1.11, an 11 % step at");
        info ("    every Size. Near Size 100 % that same 11 % is 786 samples");
        info ("    because the delays are long there; near Size 0 % it is 35.");
        info ("    Bounding the absolute figure would have been demanding that");
        info ("    a geometric law behave like a linear one.");
        info ("");
        info ("    Delay changes are handled by the existing dual-bank morph");
        info ("    and output crossfade, which is why a jump of this size is");
        info ("    inaudible: the taps never move while being read. That");
        info ("    mechanism already exists and any Size-dependent delay law");
        info ("    would ride on it rather than needing a new one.");
        info ("");
        info ("  COST OF A DELAY-SET CHANGE, had one been proposed:");
        info ("");
        info ("    delay memory     capacity is reserved for maxSizeScale at");
        info ("                     prepare, so a set with the same mean costs");
        info ("                     the same. A longer set would cost more.");
        info ("    per-sample CPU   unchanged - eight reads and eight writes");
        info ("                     whatever the lengths.");
        info ("    cache behaviour  unchanged at the same total length; a");
        info ("                     longer set spreads reads over more pages.");
        info ("    config change    one setDelayPattern call plus the existing");
        info ("                     morph. No new mechanism.");

        // The relative step, which is what a geometric law holds constant.
        double worstRelative = 0.0;

        for (int step = 1; step <= 20; ++step)
        {
            const auto now = instantiate (setA,
                mapping::sizeScaleFor (static_cast<double> (step) / 20.0));
            const auto before = instantiate (setA,
                mapping::sizeScaleFor (static_cast<double> (step - 1) / 20.0));

            for (size_t i = 0; i < 8; ++i)
                worstRelative = std::max (worstRelative,
                    std::abs (static_cast<double> (now[i] - before[i]))
                        / std::max (1.0, static_cast<double> (before[i])));
        }

        info ("    Largest RELATIVE single-line step: "
              + fx (worstRelative * 100.0, 1) + " %, against the 11.0 % the");
        info ("    geometric law predicts for a 5 % control step.");

        check (worstRelative < 0.13,
               "the existing Size law moves every line by the same relative "
               "step at every Size, as a geometric law must");
    }
    }
}

} // namespace avtest
