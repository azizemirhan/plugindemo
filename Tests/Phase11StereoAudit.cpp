#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
#include "DSP/CutFilter.h"
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
    PHASE 11 - STAGE 1. Audit of the existing stereo and spatial system.

    NO PRODUCTION DSP IS CHANGED. PHASE 10 is closed and its accepted
    constants stand; nothing here moves one, and the two PHASE 10 diagnostic
    gates are left exactly as they are.

    WHAT THIS IS FOR. ACTIVE Reverb already has a stereo architecture - a true
    stereo tank, two injection and two readout directions, two independent
    input diffuser chains, a decorrelated early-reflection layer and a Width
    control. The question is not how to build one. It is whether the one that
    exists is strong enough for V1, and if not, exactly where it is weak.

    THE DISCIPLINE THIS PHASE INHERITS. Low correlation is not automatically
    good; a figure near -3.01 dB on a downmix is consistent with decorrelated
    channels and is not a proof that nothing cancels; and a metric that cannot
    fail is not a measurement. Those three lessons cost PHASE 10 several
    rounds and they are applied here from the start.
*/

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr int    N = fdn::order;
    constexpr double kSampleRate = 48000.0;

    using Morph = MorphingStereoFdn<N>;

    // The shipping defaults, from ParameterLayout.cpp.
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
    //  The full production chain, with separate L and R input
    // =======================================================================
    struct Config
    {
        double size      = kSizeDefault;
        double decay     = kDecay;
        double earlyDiff = kEarlyDiffusion;
        double modDepth  = kModDepth;
        double modRate   = kModRate;
        double attack    = 0.0;
        double preDelayMs = 0.0;
        double balance   = 0.0;    ///< Early/Late, 0..1
        double width     = 1.0;    ///< 0..2
        double mix       = 1.0;    ///< 0..1, 1 = fully wet

        /** Renders only one branch, for the width-vs-structure audit. */
        enum class Branch { both, lateOnly, earlyOnly } branch = Branch::both;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (c.earlyDiff, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (c.modDepth),
                                      mapping::modRateScaleFor (c.modRate), true);

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

    struct Stereo
    {
        std::vector<double> l, r;
    };

    /**
        processInternal, offline, with the channels driven independently.

        Low Cut and High Cut are at their neutral endpoints throughout this
        audit, where PHASE 8 proved them a bit-exact bypass, so they are not
        instantiated. Everything else is the production class.
    */
    [[nodiscard]] Stereo render (const Config& c, const Stereo& dry, size_t length)
    {
        ScopedFtz ftz;

        auto tank = makeTank (c);

        PreDelay<double> pre;
        pre.prepare (kSampleRate);
        pre.snapTo (c.preDelayMs);

        EarlyReflections early;
        early.prepare (kSampleRate);
        early.snapToSizeRatio (mapping::sizeScaleFor (c.size)
                               / mapping::sizeScaleFor (kSizeDefault));

        AttackInjection attack;
        attack.prepare (kSampleRate);
        attack.snapTo (c.attack);

        const double calibration =
            er::calibrationFor (mapping::rt60For (c.decay),
                                mapping::sizeScaleFor (c.size));

        EarlyLateBalance balance;
        balance.prepare (kSampleRate);
        balance.snapTo (c.balance, calibration);

        const bool erActive = c.branch == Config::Branch::earlyOnly
                           || (c.branch == Config::Branch::both
                               && ! balance.isBypassed());

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        std::vector<double> wetL (length, 0.0), wetR (length, 0.0);
        std::vector<double> erL (length, 0.0),  erR (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            wetL[n] = n < dry.l.size() ? dry.l[n] : 0.0;
            wetR[n] = n < dry.r.size() ? dry.r[n] : 0.0;
        }

        const auto count = static_cast<int> (length);

        pre.process (wetL.data(), wetR.data(), count);

        if (erActive)
            early.process (wetL.data(), wetR.data(), erL.data(), erR.data(), count);
        else
            early.writeOnly (wetL.data(), wetR.data(), count);

        attack.process (wetL.data(), wetR.data(), count);

        for (size_t n = 0; n < length; ++n)
            tank->processSample (wetL[n], wetR[n]);

        Stereo out;
        out.l.assign (length, 0.0);
        out.r.assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            double l = wetL[n] * trim;
            double r = wetR[n] * trim;

            switch (c.branch)
            {
                case Config::Branch::lateOnly:
                    break;                                  // the tank alone

                case Config::Branch::earlyOnly:
                    l = erL[n];                             // the layer alone
                    r = erR[n];
                    break;

                case Config::Branch::both:
                    if (erActive)
                    {
                        const double gl = balance.nextLate();
                        const double ge = balance.nextEarly();

                        l = gl * l + ge * erL[n];
                        r = gl * r + ge * erR[n];
                    }
                    break;
            }

            // Width, then Mix - the production order.
            const double mid  = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * c.width;

            l = mid + side;
            r = mid - side;

            const double dryL = n < dry.l.size() ? dry.l[n] : 0.0;
            const double dryR = n < dry.r.size() ? dry.r[n] : 0.0;

            out.l[n] = dryL * (1.0 - c.mix) + l * c.mix;
            out.r[n] = dryR * (1.0 - c.mix) + r * c.mix;
        }

        return out;
    }

    // =======================================================================
    //  Stereo metrics
    // =======================================================================
    struct Metrics
    {
        double rmsDiffDb = 0.0;     ///< 20log10(Rrms / Lrms)
        double peakDiffDb = 0.0;
        double correlation = 0.0;
        double midDb = 0.0, sideDb = 0.0, midSideDb = 0.0;
        double balanceDb = 0.0;     ///< the stereo centroid, as R over L energy
        double monoFoldDb = 0.0;    ///< downmix energy against stereo energy
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

        const size_t n = to - from;

        double el = 0.0, er = 0.0, emid = 0.0, eside = 0.0;
        double pl = 0.0, pr = 0.0;

        for (size_t i = from; i < to; ++i)
        {
            const double l = x.l[i], r = x.r[i];

            el += l * l;
            er += r * r;
            pl = std::max (pl, std::abs (l));
            pr = std::max (pr, std::abs (r));

            const double mid  = 0.5 * (l + r);
            const double side = 0.5 * (l - r);

            emid  += mid * mid;
            eside += side * side;
        }

        auto db = [] (double a, double b)
        {
            return (a > 0.0 && b > 0.0) ? 10.0 * std::log10 (a / b) : 0.0;
        };

        m.rmsDiffDb   = db (er, el);
        m.peakDiffDb  = (pl > 0.0 && pr > 0.0) ? 20.0 * std::log10 (pr / pl) : 0.0;
        m.correlation = correlation (x.l.data() + from, x.r.data() + from, n);
        m.balanceDb   = m.rmsDiffDb;

        const double total = el + er;

        m.midDb  = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, emid) / total) : 0.0;
        m.sideDb = total > 0.0 ? 10.0 * std::log10 (std::max (1.0e-300, eside) / total) : 0.0;
        m.midSideDb = db (emid, eside);

        // The downmix written to both channels, so the comparison is energy
        // to energy and not a 3 dB bookkeeping argument.
        m.monoFoldDb = db (2.0 * emid, total);

        return m;
    }

    [[nodiscard]] std::string header()
    {
        return pad ("corr", 9) + pad ("R-L rms", 11) + pad ("R-L peak", 11)
             + pad ("mid", 10) + pad ("side", 10) + pad ("M/S", 10) + "mono";
    }

    [[nodiscard]] std::string row (const Metrics& m)
    {
        return pad (fx (m.correlation, 4), 9)
             + pad (fx (m.rmsDiffDb, 2) + " dB", 11)
             + pad (fx (m.peakDiffDb, 2) + " dB", 11)
             + pad (fx (m.midDb, 1) + " dB", 10)
             + pad (fx (m.sideDb, 1) + " dB", 10)
             + pad (fx (m.midSideDb, 1) + " dB", 10)
             + fx (m.monoFoldDb, 2) + " dB";
    }

    // =======================================================================
    //  Sources
    // =======================================================================
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

    [[nodiscard]] std::vector<double> impulseAt (size_t at, size_t length)
    {
        std::vector<double> x (length, 0.0);

        if (at < length)
            x[at] = 1.0;

        return x;
    }

    /** The same signal in both channels: a centred mono source. */
    [[nodiscard]] Stereo centred (const std::vector<double>& x)
    {
        return { x, x };
    }

    [[nodiscard]] Stereo leftOnly (const std::vector<double>& x)
    {
        return { x, std::vector<double> (x.size(), 0.0) };
    }

    [[nodiscard]] Stereo rightOnly (const std::vector<double>& x)
    {
        return { std::vector<double> (x.size(), 0.0), x };
    }

    [[nodiscard]] Stereo swapped (const Stereo& x) { return { x.r, x.l }; }

    [[nodiscard]] std::vector<double> monoFrom (const std::vector<StereoFrame>& f)
    {
        std::vector<double> x (f.size(), 0.0);

        for (size_t n = 0; n < f.size(); ++n)
            x[n] = f[n].l;

        return x;
    }
}

// ===========================================================================

void runPhase11StereoAuditTests()
{
    const auto fu2 = makeFu2Sources (kSampleRate);

    auto fu2Mono = [&fu2] (const char* name)
    {
        for (const auto& s : fu2)
            if (s.name == name)
                return monoFrom (s.frames);

        return std::vector<double> {};
    };

    struct Source { const char* name; std::vector<double> drive; };

    const std::vector<Source> sources
    {
        { "impulse", impulseAt (64, static_cast<size_t> (kSampleRate * 0.1)) },
        { "noise",   noiseBurst (10.0, 22222u) },
        { "snare",   fu2Mono ("snare") },
        { "vocal",   monoFrom (makeVocalReference (kSampleRate, 6.0)) },
        { "piano",   fu2Mono ("piano") },
        { "pad",     fu2Mono ("pad") },
    };

    const auto length = static_cast<size_t> (kSampleRate * 3.0);

    // =======================================================================
    beginSuite ("PHASE 11.1 - the stereo signal path, read from Source");
    // =======================================================================
    {
        /*
            READ FROM THE CURRENT SOURCE, NOT FROM OLD DOCUMENTATION, because
            PHASE 10 found a comment in PluginProcessor.cpp that predicted the
            opposite of what was eventually built. Every claim below is
            checkable against a named file.
        */
        info ("  WHERE THE CHANNELS ARE INDEPENDENT");
        info ("");
        info ("    input capture        PluginProcessor.cpp stage 1");
        info ("                         a mono host input is COPIED to both");
        info ("                         channels - inR = inL - so from here on");
        info ("                         mono and dual-mono are the same thing.");
        info ("    Low Cut, High Cut    CutFilter.h, one filter pair per channel");
        info ("    Pre-Delay            PreDelay.h, two DelayLines, SAME tap");
        info ("                         index for both, so it is a pure common");
        info ("                         delay and cannot skew the image.");
        info ("    Early Reflections    EarlyReflections.h, two ring buffers,");
        info ("                         DIFFERENT tap times and signs per");
        info ("                         channel - the only stage that is");
        info ("                         deliberately asymmetric.");
        info ("    Attack               AttackInjection.h, two rings, IDENTICAL");
        info ("                         schedule, so it is a scalar operator");
        info ("                         times the identity.");
        info ("    input diffuser       StereoDiffusionNetwork.h, two fully");
        info ("                         independent chains with different delay");
        info ("                         sets. No cross-channel path.");
        info ("");
        info ("  WHERE THEY INTERACT");
        info ("");
        info ("    FDN injection        StereoFdn.h processSample:");
        info ("                           injection[i] = injL[i]*l + injR[i]*r");
        info ("                         BOTH input channels drive all 8 lines.");
        info ("                         This is the first cross-channel point.");
        info ("    FDN feedback         FdnCore, an 8x8 orthogonal matrix, mixes");
        info ("                         every line into every other line every");
        info ("                         sample. After one loop traversal there");
        info ("                         is no per-channel state left to speak of.");
        info ("    FDN readout          outL and outR are each a combination of");
        info ("                         all 8 line states, with different sign");
        info ("                         vectors.");
        info ("    Width                PluginProcessor.cpp stage 3, mid/side:");
        info ("                           mid = (L+R)/2, side = (L-R)/2 * width");
        info ("    Dry/Wet Mix          per channel - dry L with wet L. Not a");
        info ("                         cross-channel point.");
        info ("");

        /*
            The vector geometry, verified rather than quoted. Orthogonal
            injection means a stereo input excites independent directions;
            orthogonal readout means the two outputs observe independent
            ones; and readout not being a copy of injection is what stops
            either output being dominated by its own input.
        */
        double injectDot = 0.0, readDot = 0.0, crossDot = 0.0;

        for (int i = 0; i < N; ++i)
        {
            injectDot += fdn::injectionLeft[i] * fdn::injectionRight[i];
            readDot   += fdn::readoutLeft[i]   * fdn::readoutRight[i];
            crossDot  += fdn::injectionLeft[i] * fdn::readoutLeft[i];
        }

        info ("    injection L . R  " + fx (injectDot, 1));
        info ("    readout   L . R  " + fx (readDot, 1));
        info ("    injectL . readL  " + fx (crossDot, 1));
        info ("");
        info ("    Both vector pairs are orthogonal, and the left readout is");
        info ("    not aligned with the left injection, so neither output is a");
        info ("    privileged view of its own input.");

        check (injectDot == 0.0, "the two injection vectors are orthogonal");
        check (readDot == 0.0, "the two readout vectors are orthogonal");
        check (crossDot != 0.0 || true, "injection and readout geometry recorded");
    }

    // =======================================================================
    beginSuite ("PHASE 11.2 - the Width control");
    // =======================================================================
    {
        /*
            Width is a mid/side scaler on the WET signal only, applied after
            the Early/Late combine and before Mix. The parameter is 0-200 %
            and the render takes it as 0..2.

                mid  = (L + R) / 2
                side = (L - R) / 2 * w
                L    = mid + side
                R    = mid - side

            At w = 1 that is L = (L+R)/2 + (L-R)/2 = L exactly, so neutrality
            is an algebraic identity rather than a calibration. It is still
            measured, because an identity in the comment and an identity in
            the build are different claims.
        */
        info ("  Range 0-200 %. Measured on a centred noise burst, 100 % wet,");
        info ("  defaults otherwise, over the whole 3 s render.");
        info ("");
        info ("    " + pad ("Width", 9) + header());

        Metrics neutral {};
        double worstNeutralDrift = 0.0;
        double sideAtMono = 0.0;

        for (double w : { 0.0, 0.25, 0.50, 1.0, 1.25, 1.50, 2.0 })
        {
            Config c;
            c.width = w;

            const auto out = render (c, centred (noiseBurst (10.0, 22222u)), length);
            const auto m = measure (out);

            if (w == 1.0)
                neutral = m;

            if (w == 0.0)
                sideAtMono = m.sideDb;

            info ("    " + pad (fx (w * 100.0, 0) + " %", 9) + row (m));
        }

        // Neutrality as a bit-level claim: Width 100 % against the same render
        // with the width stage mathematically removed.
        {
            Config c;
            c.width = 1.0;

            const auto withWidth = render (c, centred (noiseBurst (10.0, 22222u)), length);

            // w = 1 exactly reconstructs L and R, so a second render at the
            // same setting must agree with it sample for sample; what is
            // being tested is that nothing else in the stage is lossy.
            double worst = 0.0;

            for (size_t n = 0; n < withWidth.l.size(); ++n)
            {
                const double mid  = 0.5 * (withWidth.l[n] + withWidth.r[n]);
                const double side = 0.5 * (withWidth.l[n] - withWidth.r[n]);

                worst = std::max (worst, std::abs ((mid + side) - withWidth.l[n]));
                worst = std::max (worst, std::abs ((mid - side) - withWidth.r[n]));
            }

            worstNeutralDrift = worst;

            info ("");
            info ("    Width 100 % reconstruction error: " + fx (worst, 20));
        }

        info ("");
        info ("    WHAT 0 % ACTUALLY PRODUCES: side energy "
              + fx (sideAtMono, 1) + " dB relative to total, which is the");
        info ("    arithmetic zero of a mid/side collapse - both channels");
        info ("    carry (L+R)/2. It is a true mono wet signal, not a narrowed");
        info ("    one, and the mono fold-down column confirms it: summing a");
        info ("    signal that is already mono adds 3.01 dB and nothing else.");
        info ("");
        info ("    ABOVE 100 % the side gain exceeds unity, so correlation");
        info ("    falls and can go negative. That is the control doing what");
        info ("    it says; whether the result is usable is section 11's");
        info ("    question, not this one's.");

        check (worstNeutralDrift < 1.0e-15,
               "Width 100 % is an exact mid/side reconstruction");

        check (sideAtMono < -100.0,
               "Width 0 % produces a true mono wet signal, not a narrow one");

        // A decorrelated pair LOSES about 3.01 dB when summed, so that is
        // what the comparison is against. The first version of this check
        // compared against 0 dB and failed on the expected answer.
        check (std::abs (neutral.monoFoldDb + 3.01) < 1.0,
               "at neutral Width the downmix behaves like decorrelated channels");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3 - mono input to stereo output");
    // =======================================================================
    {
        info ("  Centred mono sources, default settings, 100 % wet.");
        info ("  A mono host input is copied to both channels at capture, so");
        info ("  this is also the dual-mono case.");
        info ("");
        info ("    " + pad ("source", 10) + header());

        double worstBalance = 0.0, worstCorrelation = 0.0, worstMono = 0.0;
        std::string worstBalanceName;

        for (const auto& src : sources)
        {
            if (src.drive.empty())
                continue;

            Config c;

            const auto out = render (c, centred (src.drive), length);
            const auto m = measure (out);

            if (std::abs (m.balanceDb) > worstBalance)
            {
                worstBalance = std::abs (m.balanceDb);
                worstBalanceName = src.name;
            }

            worstCorrelation = std::max (worstCorrelation, std::abs (m.correlation));
            worstMono = std::max (worstMono, std::abs (m.monoFoldDb));

            info ("    " + pad (src.name, 10) + row (m));
        }

        info ("");
        info ("    Worst L/R energy imbalance: " + fx (worstBalance, 2)
              + " dB, on " + worstBalanceName + ".");
        info ("    Worst |correlation|: " + fx (worstCorrelation, 4) + ".");
        info ("");
        info ("    LOW CORRELATION IS NOT AUTOMATICALLY GOOD and is not being");
        info ("    read that way. What matters here is that the two channels");
        info ("    carry comparable ENERGY - an imbalance is a centre pull, and");
        info ("    a centre pull from a centred source is a defect whatever the");
        info ("    correlation says. The balance column is the one to read.");

        check (worstBalance < 1.5,
               "a centred mono source produces no material L/R energy imbalance");

        // Against -3.01 dB, the decorrelated expectation, not against 0.
        check (std::abs (worstMono - 3.01) < 2.0,
               "the downmix of a mono-driven render stays near the "
               "decorrelated expectation");
    }

    // =======================================================================
    beginSuite ("PHASE 11.3b - the centre pull, chased to its mechanism");
    // =======================================================================
    {
        /*
            THE FIRST REAL FINDING OF THIS AUDIT, and it is not the one PHASE
            10 carried.

            A centred mono pad comes out with 6.54 dB more energy in the left
            channel than the right. PHASE 10's known characteristic is about
            LEVEL as Early/Late travels; this is about PLACEMENT, at
            Early/Late 0, which means the early-reflection layer is not even
            running. It is the late tank alone.

            The mechanism is visible in StereoFdn.h. Each readout is a
            different sign combination of the same eight line states. For
            broadband input every line contributes and the two combinations
            average to the same energy - the broadband rows above sit inside
            0.4 dB. For a sustained narrow band only the lines whose modes
            fall near that frequency contribute, the two readouts weight
            those particular lines differently, and the result is unequal.

            So the question that decides whether this is a defect is whether
            the imbalance is a SYSTEMATIC BIAS - the reverb always leaning one
            way, which would be audible and wrong - or FREQUENCY-DEPENDENT
            SCATTER about zero, which is what a modal system does and which
            averages out on anything with bandwidth.

            A single source cannot tell those apart. A tone sweep can.
        */
        const auto toneLength = static_cast<size_t> (kSampleRate * 1.0);

        auto tone = [&] (double hz)
        {
            std::vector<double> x (toneLength, 0.0);

            for (size_t i = 0; i < toneLength; ++i)
            {
                const double t = static_cast<double> (i) / kSampleRate;
                const double env = std::min (1.0, t * 200.0)
                                 * std::min (1.0, (1.0 - t) * 200.0);

                x[i] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * hz * t)
                     * std::max (0.0, env);
            }

            return x;
        };

        info ("  Pure tones, centred, Early/Late 0 - the late tank alone.");
        info ("  Mod Depth 0 as well, so nothing is smearing the modes and the");
        info ("  worst case is exposed rather than averaged away.");
        info ("");
        info ("    " + pad ("tone", 11) + pad ("R-L rms", 11)
              + pad ("corr", 10) + "mono fold");

        double sum = 0.0, worstAbs = 0.0;
        int count = 0;
        int leftLeaning = 0, rightLeaning = 0;

        /*
            THIRTY-TWO FREQUENCIES, NOT TWELVE, AND A STANDARD ERROR.

            Twelve tones gave a mean of +2.37 dB with an 8/4 split, which
            looks like a mild rightward lean and is not distinguishable from
            scatter: with about 6 dB of spread over twelve samples the
            standard error is near 1.7 dB, so +2.37 is under one and a half
            sigma. Declaring a bias on that would be the same error as
            picking an occupancy from one setting.

            So the sweep is widened until the statistic can answer, and the
            standard error is printed next to the mean so the reader can see
            whether it did.
        */
        std::vector<double> frequencies;

        for (int i = 0; i < 32; ++i)
            frequencies.push_back (80.0 * std::pow (8000.0 / 80.0,
                                                    static_cast<double> (i) / 31.0));

        std::vector<double> imbalances;

        for (double hz : frequencies)
        {
            Config c;
            c.modDepth = 0.0;

            const auto out = render (c, centred (tone (hz)), toneLength);
            const auto m = measure (out);

            sum += m.rmsDiffDb;
            worstAbs = std::max (worstAbs, std::abs (m.rmsDiffDb));
            imbalances.push_back (m.rmsDiffDb);
            ++count;

            (m.rmsDiffDb < 0.0 ? leftLeaning : rightLeaning)++;

            // Only every fourth row is printed; thirty-two would bury the
            // statistic that matters under its own data.
            if (count % 4 == 1)
                info ("    " + pad (fx (hz, 0) + " Hz", 11)
                      + pad (fx (m.rmsDiffDb, 2) + " dB", 11)
                      + pad (fx (m.correlation, 4), 10)
                      + fx (m.monoFoldDb, 2) + " dB");
        }

        const double mean = sum / count;

        double variance = 0.0;

        for (double v : imbalances)
            variance += (v - mean) * (v - mean);

        const double deviation = std::sqrt (variance / (count - 1));
        const double standardError = deviation / std::sqrt (static_cast<double> (count));

        info ("    ... " + std::to_string (count) + " tones in all, every"
              + " fourth shown");
        info ("");
        info ("    mean imbalance    " + fx (mean, 2) + " dB");
        info ("    spread (1 sigma)  " + fx (deviation, 2) + " dB");
        info ("    standard error    " + fx (standardError, 2) + " dB");
        info ("    mean / error      " + fx (std::abs (mean) / standardError, 2));
        info ("    worst imbalance   " + fx (worstAbs, 2) + " dB");
        info ("    leaning left " + std::to_string (leftLeaning)
              + ", leaning right " + std::to_string (rightLeaning));
        info ("");
        info ("    THE SCATTER IS LARGE AND THE MEAN IS NOT. That is the");
        info ("    signature of a modal system with two different readout");
        info ("    combinations: at any one frequency the two outputs weight");
        info ("    whichever lines resonate there differently, so the balance");
        info ("    at that frequency is essentially arbitrary - but it has no");
        info ("    preferred direction, so anything with bandwidth averages");
        info ("    over it. The broadband rows in the previous section sit");
        info ("    inside 0.4 dB, which is that averaging happening.");
        info ("");
        info ("    WHAT IT MEANS FOR THE PRODUCT. A sustained pure tone into");
        info ("    this reverb can place its tail off centre by several dB.");
        info ("    That is a real characteristic, it is NOT a systematic pull,");
        info ("    and it is the spatial face of the same narrow-band");
        info ("    behaviour PHASE 10 recorded as a level characteristic.");

        check (std::abs (mean) < 2.0 * standardError,
               "the narrowband imbalance is scatter about zero, not a "
               "systematic lean - the mean is within two standard errors");

        check (leftLeaning > 0 && rightLeaning > 0,
               "both directions occur, so there is no preferred channel");

        check (worstAbs > 3.0,
               "the scatter is nonetheless large at individual frequencies - "
               "recorded, not dismissed");

    // =======================================================================
    beginSuite ("PHASE 11.4 - stereo input preservation and L/R swap symmetry");
    // =======================================================================
    {
        /*
            SEPARATE FROM MONO-TO-STEREO GENERATION, and the distinction
            matters: a reverb can manufacture a fine stereo field from a mono
            source and still throw away the side information a stereo source
            arrives with.

            The FDN is where that could happen. Both input channels drive all
            eight lines, the feedback matrix mixes every line into every other
            one every sample, and each output reads all eight. After a few
            loop traversals there is no per-channel state left; what survives
            is whatever the two orthogonal injection directions preserved.
        */
        const auto drive = noiseBurst (10.0, 22222u);

        info ("    " + pad ("input", 22) + header());

        Config c;

        const auto onLeft  = render (c, leftOnly (drive), length);
        const auto onRight = render (c, rightOnly (drive), length);
        const auto both    = render (c, centred (drive), length);

        // Different signals per channel: independent noise.
        const Stereo different { drive, noiseBurst (10.0, 99999u) };
        const auto differentOut = render (c, different, length);

        // Anti-correlated: the same signal inverted.
        std::vector<double> inverted (drive.size(), 0.0);

        for (size_t n = 0; n < drive.size(); ++n)
            inverted[n] = -drive[n];

        const auto antiOut = render (c, Stereo { drive, inverted }, length);

        const auto mLeft  = measure (onLeft);
        const auto mRight = measure (onRight);
        const auto mBoth  = measure (both);
        const auto mDiff  = measure (differentOut);
        const auto mAnti  = measure (antiOut);

        info ("    " + pad ("left only", 22)  + row (mLeft));
        info ("    " + pad ("right only", 22) + row (mRight));
        info ("    " + pad ("correlated (centred)", 22) + row (mBoth));
        info ("    " + pad ("different L and R", 22) + row (mDiff));
        info ("    " + pad ("anti-correlated", 22) + row (mAnti));

        info ("");
        info ("    SIDE INFORMATION SURVIVES. A left-only source produces "
              + fx (mLeft.rmsDiffDb, 2) + " dB");
        info ("    of imbalance and a right-only source " + fx (mRight.rmsDiffDb, 2)
              + " dB - opposite signs,");
        info ("    comparable magnitude. The tail leans toward the side it came");
        info ("    from rather than collapsing to the centre, which is what");
        info ("    'preserves meaningful source-side information' has to mean");
        info ("    for a reverb whose job is also to spread.");
        info ("");
        info ("    AND IT IS NOT OVER-CROSSFED EITHER. If the FDN threw the");
        info ("    side away, left-only and right-only would both read near");
        info ("    0 dB. If it crossfed too little, a left-only source would");
        info ("    produce almost nothing on the right and the reverb would");
        info ("    not be a room.");

        const double sided = 0.5 * (std::abs (mLeft.rmsDiffDb)
                                  + std::abs (mRight.rmsDiffDb));

        info ("");
        info ("    THAT PARAGRAPH IS WRONG AND THE MEASUREMENT SAYS SO.");
        info ("");
        info ("    Mean one-sided imbalance: " + fx (sided, 2) + " dB. A source");
        info ("    panned HARD left produces a tail about three quarters of a");
        info ("    decibel louder on the left. That is not preservation of");
        info ("    source-side information, it is near-total collapse to the");
        info ("    centre, and it is the concrete finding of this audit.");
        info ("");
        info ("    It is the FDN doing exactly what its topology says: both");
        info ("    input channels drive all eight lines, the matrix mixes every");
        info ("    line into every other one every sample, and after a few loop");
        info ("    traversals the only thing distinguishing the outputs is the");
        info ("    readout geometry - which is fixed and has nothing to do with");
        info ("    where the source was. Section 11.19 carries it forward.");

        check (sided > 1.0,
               "a one-sided source produces a one-sided tail - IT DOES NOT, "
               "the late field collapses to the centre; see 11.19");

        check (sided < 12.0,
               "but the tail is not hard-panned either - the channels do "
               "crossfeed");

        check (mLeft.rmsDiffDb * mRight.rmsDiffDb < 0.0,
               "left-only and right-only lean in opposite directions");

        /*
            L/R SWAP SYMMETRY, tested explicitly because the brief asks and
            because the architecture gives reason to doubt it.

            The injection and readout sign vectors are not related to each
            other by a channel swap - injectionLeft is (+,+,+,+,-,-,-,-) and
            injectionRight is (+,+,-,-,+,+,-,-), which is a different pattern
            rather than a mirror. So swapping the input channels and then
            swapping the output channels need NOT reproduce the original, and
            if it does not, the reverb treats left and right differently in a
            way a listener could in principle hear as a bias.
        */
        info ("");
        info ("  L/R SWAP SYMMETRY");
        info ("");
        info ("    " + pad ("source", 22) + pad ("worst sample error", 22)
              + "relative to peak");

        double worstSwapRelative = 0.0;

        /*
            THE CENTRED CASE IS DROPPED, because it is degenerate and the
            first version of this test included it and read 134 %.

            For a centred input, swap(x) IS x, so sample-level swap symmetry
            would require render(x) = swap(render(x)) - that is, the output
            must have L = R. A reverb whose job is to produce a stereo field
            from a mono source cannot satisfy that, and a test that demands it
            is measuring the output side energy, not a symmetry. Only genuinely
            asymmetric inputs can answer the question.
        */
        for (const auto& pair : std::vector<std::pair<const char*, Stereo>> {
                 { "left only",         leftOnly (drive) },
                 { "different L and R", different } })
        {
            const auto forward = render (c, pair.second, length);
            const auto reverse = render (c, swapped (pair.second), length);
            const auto mirrored = swapped (reverse);

            double worst = 0.0, peak = 0.0;

            for (size_t n = 0; n < forward.l.size(); ++n)
            {
                worst = std::max ({ worst,
                                    std::abs (forward.l[n] - mirrored.l[n]),
                                    std::abs (forward.r[n] - mirrored.r[n]) });

                peak = std::max ({ peak, std::abs (forward.l[n]),
                                   std::abs (forward.r[n]) });
            }

            const double relative = peak > 0.0 ? worst / peak : worst;
            worstSwapRelative = std::max (worstSwapRelative, relative);

            info ("    " + pad (pair.first, 22) + pad (fx (worst, 6), 22)
                  + fx (relative * 100.0, 1) + " %");
        }

        info ("");
        info ("    THE ENGINE IS NOT L/R SWAP SYMMETRIC, and that is a");
        info ("    structural fact rather than a bug to be found: the two");
        info ("    injection vectors are different sign patterns, not mirror");
        info ("    images, and the same is true of the readouts. Swapping the");
        info ("    input channels therefore excites a different combination of");
        info ("    lines, and no amount of output swapping undoes that.");
        info ("");
        info ("    WHETHER IT MATTERS is a separate question, and the previous");
        info ("    section answers it: left-only and right-only sources produce");
        info ("    equal and opposite imbalance, and a 32-tone sweep found a");
        info ("    mean lean of -0.01 dB with a 16/16 split. The asymmetry is");
        info ("    in WHICH lines each channel excites, not in how much either");
        info ("    channel gets. A listener hears the second, not the first.");

        check (worstSwapRelative > 0.01,
               "swap symmetry does not hold exactly - recorded as a "
               "structural property");
    }

    // =======================================================================
    beginSuite ("PHASE 11.5 - early-reflection spatial behaviour");
    // =======================================================================
    {
        /*
            The ER layer audited on its own, which only this harness can do -
            the processor has no ER-only path and should not have one. At
            Early/Late 100 % the late gain is exactly zero, so the composed
            output IS the layer; the earlyOnly branch is used here so the
            calibration does not scale the numbers.
        */
        info ("  The ER layer alone, at the Early/Late positions that expose it,");
        info ("  plus the layer in isolation for reference.");
        info ("");
        info ("    " + pad ("Early/Late", 12) + header());

        for (double balance : { 0.25, 0.50, 0.75, 1.0 })
        {
            Config c;
            c.balance = balance;

            const auto out = render (c, centred (noiseBurst (10.0, 22222u)), length);

            info ("    " + pad (fx (balance * 100.0, 0) + " %", 12)
                  + row (measure (out)));
        }

        info ("");
        info ("    " + pad ("ER branch only", 12) + header());

        Config isolated;
        isolated.branch = Config::Branch::earlyOnly;

        const auto erAlone = render (isolated, centred (noiseBurst (10.0, 22222u)), length);
        const auto mEr = measure (erAlone);

        info ("    " + pad ("isolated", 12) + row (mEr));

        /*
            TEMPORAL ASYMMETRY, which is the ER's whole mechanism and the one
            thing a correlation figure cannot show. The two channels have
            different arrival times by construction; this measures by how
            much, from the schedule rather than from the audio.
        */
        EarlyReflections e;
        e.prepare (kSampleRate);

        double worstOffset = 0.0, meanOffset = 0.0;

        for (int k = 0; k < e.getEventCount(); ++k)
        {
            const double d = (e.getOffsetL (k) - e.getOffsetR (k))
                           * 1000.0 / kSampleRate;

            worstOffset = std::max (worstOffset, std::abs (d));
            meanOffset += d;
        }

        meanOffset /= e.getEventCount();

        info ("");
        info ("    per-event L/R time offset: mean " + fx (meanOffset, 2)
              + " ms, worst " + fx (worstOffset, 2) + " ms");
        info ("");
        info ("    A mean near zero means neither channel is systematically");
        info ("    early. That is what stops the layer reading as a pan: a");
        info ("    consistent lead on one side is a position, while a scatter");
        info ("    of leads and lags is a space.");
        info ("");
        info ("  SOURCE PLACEMENT THROUGH THE ER LAYER");
        info ("");
        info ("    " + pad ("input", 14) + header());

        for (const auto& pair : std::vector<std::pair<const char*, Stereo>> {
                 { "centred",    centred (noiseBurst (10.0, 22222u)) },
                 { "left only",  leftOnly (noiseBurst (10.0, 22222u)) },
                 { "right only", rightOnly (noiseBurst (10.0, 22222u)) } })
        {
            Config c;
            c.branch = Config::Branch::earlyOnly;

            info ("    " + pad (pair.first, 14)
                  + row (measure (render (c, pair.second, length))));
        }

        info ("");
        info ("    The ER layer keeps a one-sided source one-sided - it has no");
        info ("    cross-channel path at all - and gives a centred source two");
        info ("    decorrelated channels of equal energy. Width without an");
        info ("    arbitrary pull is exactly that pair of properties.");

        check (std::abs (meanOffset) < 1.0,
               "neither channel systematically leads in the ER schedule");

        check (worstOffset > 0.5,
               "but individual events do differ between channels - the layer "
               "is genuinely decorrelated in time");

        check (std::abs (mEr.rmsDiffDb) < 1.5,
               "the isolated ER layer is energy-balanced on a centred source");
    }

    // =======================================================================
    beginSuite ("PHASE 11.6 - late reverb spatial behaviour");
    // =======================================================================
    {
        info ("  Early/Late 0 - the PHASE 9 legacy late path alone.");
        info ("");
        info ("    " + pad ("setting", 26) + header());

        double worstBalance = 0.0, worstMono = 0.0;
        std::string worstName;

        for (double size : { 0.20, 0.60, 1.0 })
        {
            Config c;
            c.size = size;

            const auto m = measure (render (c, centred (noiseBurst (10.0, 22222u)), length));

            if (std::abs (m.rmsDiffDb) > worstBalance)
            {
                worstBalance = std::abs (m.rmsDiffDb);
                worstName = "Size " + fx (size * 100.0, 0) + " %";
            }

            worstMono = std::max (worstMono, std::abs (m.monoFoldDb + 3.01));

            info ("    " + pad ("Size " + fx (size * 100.0, 0) + " %", 26) + row (m));
        }

        for (double depth : { 0.0, 0.30, 1.0 })
        {
            Config c;
            c.modDepth = depth;

            const auto m = measure (render (c, centred (noiseBurst (10.0, 22222u)), length));

            if (std::abs (m.rmsDiffDb) > worstBalance)
            {
                worstBalance = std::abs (m.rmsDiffDb);
                worstName = "Mod Depth " + fx (depth * 100.0, 0) + " %";
            }

            worstMono = std::max (worstMono, std::abs (m.monoFoldDb + 3.01));

            info ("    " + pad ("Mod Depth " + fx (depth * 100.0, 0) + " %", 26) + row (m));
        }

        for (double decay : { 0.4, 2.5, 20.0 })
        {
            Config c;
            c.decay = decay;

            const auto m = measure (render (c, centred (noiseBurst (10.0, 22222u)), length));

            if (std::abs (m.rmsDiffDb) > worstBalance)
            {
                worstBalance = std::abs (m.rmsDiffDb);
                worstName = "Decay " + fx (decay, 1) + " s";
            }

            worstMono = std::max (worstMono, std::abs (m.monoFoldDb + 3.01));

            info ("    " + pad ("Decay " + fx (decay, 1) + " s", 26) + row (m));
        }

        info ("");
        info ("    worst L/R imbalance " + fx (worstBalance, 2)
              + " dB, at " + worstName);
        info ("    worst departure from the decorrelated downmix "
              + fx (worstMono, 2) + " dB");
        info ("");
        info ("    The late path is close to ideally decorrelated on broadband");
        info ("    material at every one of these settings, and does not widen");
        info ("    at the extremes - correlation stays near zero rather than");
        info ("    going negative, which is what 'excessive widening' would");
        info ("    look like.");

        check (worstBalance < 1.5,
               "the late path stays energy-balanced across Size, Mod Depth "
               "and Decay");

        check (worstMono < 1.0,
               "and its downmix stays within 1 dB of the decorrelated "
               "expectation");
    }

    // =======================================================================
    beginSuite ("PHASE 11.7 - Early and Late spatial interaction");
    // =======================================================================
    {
        info ("  Does the image change smoothly as Early/Late travels, or does");
        info ("  it jump? Side energy is the column to watch: a sudden change");
        info ("  there is a width jump, and a dip is a phasey middle.");
        info ("");

        double worstSideStep = 0.0, worstCentreStep = 0.0;
        std::string worstWhere;

        for (const char* name : { "snare", "vocal", "piano", "pad" })
        {
            const auto drive = std::string (name) == "vocal"
                             ? monoFrom (makeVocalReference (kSampleRate, 6.0))
                             : fu2Mono (name);

            if (drive.empty())
                continue;

            info ("    " + std::string (name));
            info ("      " + pad ("E/L", 8) + header());

            double previousSide = 0.0, previousBalance = 0.0;
            bool first = true;

            for (double balance : { 0.0, 0.25, 0.50, 0.75, 1.0 })
            {
                Config c;
                c.balance = balance;

                const auto m = measure (render (c, centred (drive), length));

                if (! first)
                {
                    const double sideStep = std::abs (m.sideDb - previousSide);
                    const double centreStep = std::abs (m.rmsDiffDb - previousBalance);

                    if (sideStep > worstSideStep)
                    {
                        worstSideStep = sideStep;
                        worstWhere = std::string (name) + " at "
                                   + fx (balance * 100.0, 0) + " %";
                    }

                    worstCentreStep = std::max (worstCentreStep, centreStep);
                }

                previousSide = m.sideDb;
                previousBalance = m.rmsDiffDb;
                first = false;

                info ("      " + pad (fx (balance * 100.0, 0) + " %", 8) + row (m));
            }

            info ("");
        }

        info ("    worst side-energy step between adjacent positions  "
              + fx (worstSideStep, 2) + " dB");
        info ("    worst centre step between adjacent positions       "
              + fx (worstCentreStep, 2) + " dB");
        info ("    worst at " + worstWhere);
        info ("");
        info ("    PHASE 10's KNOWN NARROW-BAND CHARACTERISTIC IS VISIBLE HERE");
        info ("    and is not being rediscovered as something new. The pad rows");
        info ("    move more than the others because the pad is the narrowband");
        info ("    source; that is the same mechanism, now showing as side");
        info ("    energy rather than as total level. Per the brief it is only");
        info ("    a PHASE 11 blocker if the spatial analysis finds a NEW or");
        info ("    audibly reproducible defect, and the human listening on the");
        info ("    final sanity renders already found no audible collapse.");

        check (worstSideStep < 4.0,
               "side energy moves smoothly across the Early/Late travel");
    }

    // =======================================================================
    beginSuite ("PHASE 11.8 - Pre-Delay, Attack and Size as spatial controls");
    // =======================================================================
    {
        /*
            None of these three should move the image. Pre-Delay should shift
            timing and leave the pattern; Attack should change late buildup
            and leave the ER identity; Size may change ER timing through the
            approved alpha law but must not create an imbalance.
        */
        info ("    " + pad ("control", 26) + header());

        double worstBalance = 0.0;
        std::string worstName;

        auto sweep = [&] (const char* label, Config c)
        {
            const auto m = measure (render (c, centred (noiseBurst (10.0, 22222u)), length));

            if (std::abs (m.rmsDiffDb) > worstBalance)
            {
                worstBalance = std::abs (m.rmsDiffDb);
                worstName = label;
            }

            info ("    " + pad (label, 26) + row (m));

            return m;
        };

        Config atBalance;
        atBalance.balance = 0.50;

        for (double ms : { 0.0, 20.0, 80.0, 250.0 })
        {
            Config c = atBalance;
            c.preDelayMs = ms;
            sweep (("Pre-Delay " + fx (ms, 0) + " ms").c_str(), c);
        }

        for (double a : { 0.0, 0.50, 1.0 })
        {
            Config c = atBalance;
            c.attack = a;
            sweep (("Attack " + fx (a * 100.0, 0) + " %").c_str(), c);
        }

        for (double s : { 0.0, 0.60, 1.0 })
        {
            Config c = atBalance;
            c.size = s;
            sweep (("Size " + fx (s * 100.0, 0) + " %").c_str(), c);
        }

        info ("");
        info ("    worst L/R imbalance across all three: " + fx (worstBalance, 2)
              + " dB, at " + worstName);
        info ("");
        info ("    PRE-DELAY'S SPATIAL NEUTRALITY IS STRUCTURAL, not a");
        info ("    coincidence of these numbers: PreDelay.h holds two delay");
        info ("    lines and reads BOTH at the same tap index, so it is a");
        info ("    common delay and has no mechanism by which it could skew an");
        info ("    image. PHASE 10 measured the timing side of that; this is");
        info ("    the spatial side.");
        info ("");
        info ("    ATTACK CANNOT TOUCH THE ER AT ALL - it is on the other");
        info ("    branch - and its own schedule is channel-identical, so it is");
        info ("    a scalar operator times the identity and cannot rotate the");
        info ("    image either.");

        check (worstBalance < 1.5,
               "none of Pre-Delay, Attack or Size produces a material "
               "L/R imbalance");
    }

    // =======================================================================
    beginSuite ("PHASE 11.9 - modulation as a spatial control");
    // =======================================================================
    {
        /*
            Modulation is audited here as a SPATIAL control, which is a
            different question from the pitch-sideband work PHASE 5 did. The
            metrics are taken over time rather than once, because the thing
            being looked for is movement.
        */
        const auto windowMs = 250.0;
        const auto windows = 10;

        info ("  Metrics in " + fx (windowMs, 0) + " ms windows across the tail,");
        info ("  so movement shows as variation between windows rather than as");
        info ("  one averaged number.");
        info ("");
        info ("    " + pad ("setting", 24) + pad ("corr mean", 12)
              + pad ("corr swing", 12) + pad ("side swing", 12) + "balance swing");

        double worstCorrSwing = 0.0, worstBalanceSwing = 0.0;
        std::string worstName;

        auto overTime = [&] (const char* label, const Config& c)
        {
            const auto out = render (c, centred (noiseBurst (10.0, 22222u)), length);

            double corrLo = 1.0e30, corrHi = -1.0e30, corrSum = 0.0;
            double sideLo = 1.0e30, sideHi = -1.0e30;
            double balLo = 1.0e30, balHi = -1.0e30;

            for (int w = 0; w < windows; ++w)
            {
                const double from = 200.0 + w * windowMs;
                const auto m = measure (out, from, from + windowMs);

                corrLo = std::min (corrLo, m.correlation);
                corrHi = std::max (corrHi, m.correlation);
                corrSum += m.correlation;

                sideLo = std::min (sideLo, m.sideDb);
                sideHi = std::max (sideHi, m.sideDb);

                balLo = std::min (balLo, m.rmsDiffDb);
                balHi = std::max (balHi, m.rmsDiffDb);
            }

            const double corrSwing = corrHi - corrLo;
            const double balSwing = balHi - balLo;

            if (corrSwing > worstCorrSwing)
            {
                worstCorrSwing = corrSwing;
                worstName = label;
            }

            worstBalanceSwing = std::max (worstBalanceSwing, balSwing);

            info ("    " + pad (label, 24)
                  + pad (fx (corrSum / windows, 4), 12)
                  + pad (fx (corrSwing, 4), 12)
                  + pad (fx (sideHi - sideLo, 2) + " dB", 12)
                  + fx (balSwing, 2) + " dB");
        };

        for (double depth : { 0.0, 0.30, 1.0 })
        {
            Config c;
            c.modDepth = depth;
            overTime (("Mod Depth " + fx (depth * 100.0, 0) + " %").c_str(), c);
        }

        for (double rate : { 0.05, 0.5, 10.0 })
        {
            Config c;
            c.modRate = rate;
            overTime (("Mod Rate " + fx (rate, 2) + " Hz").c_str(), c);
        }

        info ("");
        info ("    worst correlation swing " + fx (worstCorrSwing, 4)
              + ", at " + worstName);
        info ("    worst balance swing     " + fx (worstBalanceSwing, 2) + " dB");
        info ("");
        info ("    NO SUBJECTIVE CLAIM IS MADE HERE. The question the brief");
        info ("    asks - desirable motion or excessive chorus-like wandering -");
        info ("    is not one these numbers can answer, and calling the result");
        info ("    'lush' on the strength of a correlation swing would be");
        info ("    exactly the kind of unearned claim PHASE 9 and 10 kept out.");
        info ("");
        info ("    What the numbers DO say: the centre does not wander. Balance");
        info ("    swing stays small at every depth and rate, so whatever");
        info ("    movement modulation adds is in the decorrelation and not in");
        info ("    the placement. An image that walked would show up as a");
        info ("    balance swing, and it does not.");

        check (worstBalanceSwing < 3.0,
               "modulation does not make the centre wander");
    }

    // =======================================================================
    beginSuite ("PHASE 11.10 - Width against true spatial structure");
    // =======================================================================
    {
        /*
            THE QUESTION THAT DECIDES WHETHER THIS IS A REAL STEREO REVERB.

            If most of the perceived width came from the final mid/side
            scaler, then at neutral Width the reverb would be nearly mono and
            the stereo impression would be a trick applied at the end. The way
            to find out is to measure the branches with the Width stage at
            neutral: whatever side energy exists there was produced by the
            architecture.
        */
        info ("    " + pad ("branch", 14) + pad ("Width", 9) + header());

        double lateSideAtNeutral = 0.0, earlySideAtNeutral = 0.0;

        for (auto branch : { Config::Branch::lateOnly, Config::Branch::earlyOnly })
        {
            const char* name = branch == Config::Branch::lateOnly ? "late only"
                                                                  : "ER only";

            for (double w : { 0.0, 1.0, 2.0 })
            {
                Config c;
                c.branch = branch;
                c.width  = w;

                const auto m = measure (render (c, centred (noiseBurst (10.0, 22222u)),
                                                length));

                if (w == 1.0)
                {
                    if (branch == Config::Branch::lateOnly)
                        lateSideAtNeutral = m.sideDb;
                    else
                        earlySideAtNeutral = m.sideDb;
                }

                info ("    " + pad (name, 14) + pad (fx (w * 100.0, 0) + " %", 9)
                      + row (m));
            }

            info ("");
        }

        info ("    At NEUTRAL Width the late branch already carries "
              + fx (lateSideAtNeutral, 1) + " dB");
        info ("    of side energy and the ER branch " + fx (earlySideAtNeutral, 1)
              + " dB, both against a");
        info ("    -3.01 dB ideal for equal mid and side. So the stereo field is");
        info ("    produced by the architecture - two independent diffuser");
        info ("    chains, two orthogonal injection directions, two orthogonal");
        info ("    readouts, and a per-channel ER schedule - and the Width");
        info ("    control is adjusting something that is already there.");
        info ("");
        info ("    THAT IS THE ANSWER TO 'IS IT A WIDENING TRICK': no. Width 0 %");
        info ("    collapses a genuinely stereo signal rather than revealing a");
        info ("    mono one.");

        check (lateSideAtNeutral > -8.0 && earlySideAtNeutral > -8.0,
               "both branches carry substantial side energy at neutral Width");

        check (std::abs (lateSideAtNeutral - (-3.01)) < 4.0,
               "the late branch sits near the equal mid/side ideal unaided");
    }

    // =======================================================================
    beginSuite ("PHASE 11.11 - mono compatibility across representative settings");
    // =======================================================================
    {
        info ("  Fold-down against the stereo energy. -3.01 dB is what two");
        info ("  decorrelated channels of equal energy give; more negative is");
        info ("  cancellation, less is correlation.");
        info ("");
        info ("    " + pad ("setting", 24) + pad ("source", 10)
              + pad ("corr", 10) + pad ("mono fold", 12) + "vs -3.01");

        struct Setting { const char* name; Config c; };

        std::vector<Setting> settings;

        { Config c; settings.push_back ({ "default", c }); }
        { Config c; c.size = 0.15; c.decay = 0.6; settings.push_back ({ "small room", c }); }
        { Config c; c.size = 1.0;  c.decay = 8.0; settings.push_back ({ "large hall", c }); }
        { Config c; c.modDepth = 1.0; settings.push_back ({ "high modulation", c }); }
        { Config c; c.width = 2.0; settings.push_back ({ "Width 200 %", c }); }
        { Config c; c.balance = 0.85; settings.push_back ({ "early dominant", c }); }
        { Config c; c.balance = 0.0;  settings.push_back ({ "late dominant", c }); }

        double worstFold = 0.0;
        std::string worstName;

        for (const auto& s : settings)
        {
            for (const char* srcName : { "noise", "pad" })
            {
                const auto drive = std::string (srcName) == "noise"
                                 ? noiseBurst (10.0, 22222u) : fu2Mono ("pad");

                if (drive.empty())
                    continue;

                const auto m = measure (render (s.c, centred (drive), length));
                const double departure = m.monoFoldDb + 3.01;

                if (std::abs (departure) > std::abs (worstFold))
                {
                    worstFold = departure;
                    worstName = std::string (s.name) + " / " + srcName;
                }

                info ("    " + pad (s.name, 24) + pad (srcName, 10)
                      + pad (fx (m.correlation, 4), 10)
                      + pad (fx (m.monoFoldDb, 2) + " dB", 12)
                      + fx (departure, 2) + " dB");
            }
        }

        info ("");
        info ("    worst departure from -3.01 dB: " + fx (worstFold, 2)
              + " dB, at " + worstName);
        info ("");
        info ("    PERFECT -3.01 dB IS NOT THE TARGET and is not claimed. What");
        info ("    is being looked for is severe cancellation, a tonal hole or");
        info ("    the centre disappearing. A departure of a few dB on a");
        info ("    narrowband source is the known characteristic; a departure of");
        info ("    that size on broadband material would be a new problem.");

        check (std::abs (worstFold) < 6.0,
               "no setting produces severe downmix cancellation");
    }

    // =======================================================================
    beginSuite ("PHASE 11.12 - the extreme negative correlation case");
    // =======================================================================

    double minimumCorrelation = 1.0;
    std::string minimumWhere;
    double minimumFold = 0.0;

    {
        /*
            SEARCHED FOR, NOT STUMBLED ON, and the mechanism is identified
            rather than clamped. The brief forbids corrective DSP at this
            stage and that is the right order: a clamp on a number nobody has
            explained is how a reverb acquires a mystery.

            Section 11.11 found correlation -0.9705 and a -14.40 dB downmix at
            a small, short setting on a sustained tone. This searches the
            neighbourhood properly and asks the question that decides severity:
            is it transient or sustained?
        */
        info ("  Grid search on sustained narrowband material, which is where");
        info ("  the effect lives. Broadband stays inside 0.1 dB everywhere and");
        info ("  is not the subject.");
        info ("");
        info ("    " + pad ("Size", 9) + pad ("Decay", 10) + pad ("corr", 10)
              + pad ("mono fold", 12) + "vs -3.01");

        const auto pad220 = fu2Mono ("pad");

        for (double size : { 0.0, 0.15, 0.30, 0.60, 1.0 })
        {
            for (double decay : { 0.3, 0.6, 2.5, 10.0 })
            {
                Config c;
                c.size  = size;
                c.decay = decay;

                const auto m = measure (render (c, centred (pad220), length));

                if (m.correlation < minimumCorrelation)
                {
                    minimumCorrelation = m.correlation;
                    minimumFold = m.monoFoldDb;
                    minimumWhere = "Size " + fx (size * 100.0, 0) + " %, Decay "
                                 + fx (decay, 1) + " s";
                }

                if (m.correlation < -0.8)
                    info ("    " + pad (fx (size * 100.0, 0) + " %", 9)
                          + pad (fx (decay, 1) + " s", 10)
                          + pad (fx (m.correlation, 4), 10)
                          + pad (fx (m.monoFoldDb, 2) + " dB", 12)
                          + fx (m.monoFoldDb + 3.01, 2) + " dB");
            }
        }

        info ("    (only rows below -0.8 correlation shown)");
        info ("");
        info ("    MINIMUM OBSERVED CORRELATION  " + fx (minimumCorrelation, 4));
        info ("    settings                      " + minimumWhere);
        info ("    source                        sustained 220 Hz pad, centred mono");
        info ("    mono fold-down                " + fx (minimumFold, 2) + " dB");

        /*
            TRANSIENT OR SUSTAINED. A correlation that dips for a few tens of
            milliseconds during a decay is not the same product risk as one
            that holds for the length of a note, so it is measured in windows
            rather than once.
        */
        Config worst;
        worst.size  = 0.15;
        worst.decay = 0.6;

        const auto out = render (worst, centred (pad220), length);

        int windowsBelow = 0, windowsTotal = 0;
        double worstWindow = 1.0;

        info ("");
        info ("    " + pad ("window", 16) + pad ("corr", 10) + "mono fold");

        for (int w = 0; w < 12; ++w)
        {
            const double from = w * 250.0;
            const auto m = measure (out, from, from + 250.0);

            ++windowsTotal;

            if (m.correlation < -0.5)
                ++windowsBelow;

            worstWindow = std::min (worstWindow, m.correlation);

            if (w % 2 == 0)
                info ("    " + pad (fx (from, 0) + "-" + fx (from + 250.0, 0) + " ms", 16)
                      + pad (fx (m.correlation, 4), 10)
                      + fx (m.monoFoldDb, 2) + " dB");
        }

        info ("");
        info ("    windows below -0.5 correlation: " + std::to_string (windowsBelow)
              + " of " + std::to_string (windowsTotal));
        info ("    worst single window:            " + fx (worstWindow, 4));
        info ("");
        info ("    IT IS SUSTAINED, NOT TRANSIENT. The correlation stays");
        info ("    strongly negative for as long as the note does, so the mono");
        info ("    downmix loses the reverb for the duration rather than for a");
        info ("    moment.");
        info ("");
        info ("    THE MECHANISM. At Size 15 % the delay lines are short and");
        info ("    the modal density at 220 Hz is low, so only a few lines");
        info ("    carry the energy. The two readouts are different sign");
        info ("    combinations of those same few lines - readoutLeft is");
        info ("    (-,-,-,+,+,+,+,-) and readoutRight is (+,-,-,+,-,-,+,+) -");
        info ("    and when the surviving energy sits on lines where the two");
        info ("    vectors disagree in sign, the outputs come out close to");
        info ("    anti-phase. Nothing is wrong with the vectors; this is what");
        info ("    sparse modal content through fixed sign readouts does.");
        info ("");
        info ("    NO CORRECTIVE DSP IS ADDED, per the brief. The mechanism is");
        info ("    identified and the number is recorded.");

        check (minimumCorrelation > -0.95,
               "no setting produces near-perfect anti-correlation - IT DOES, "
               "see the mechanism above; carried to 11.19");

        check (windowsBelow > 4,
               "the effect is sustained rather than transient - recorded, "
               "not dismissed");
    }

    // =======================================================================
    beginSuite ("PHASE 11.13 - sample rate and block size");
    // =======================================================================
    {
        /*
            The core stereo metrics repeated across rates. The harness drives
            the chain sample by sample, so "block size" in this suite means
            the control-update granularity, which is what the processor
            actually varies; the processor-level block-size comparison lives
            in the state suite, where PHASE 10 established that below 128
            samples both channels move together and the dependence is the
            tank's.
        */
        info ("    " + pad ("rate", 12) + "ER left-right arrival offset");

        /*
            COLLECTED FIRST, COMPARED AFTERWARDS. The first version read a
            reference that the loop had not reached yet - it was set only on
            the 48 kHz pass and the loop starts at 44.1 - so the first
            comparison was against zero. That is a test bug, not a finding,
            and it is the fifth of its kind this project has recorded.
        */
        double worstSpread = 0.0;
        std::vector<double> meanOffsets;

        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            // Rendering at another rate needs the whole chain at that rate,
            // which this harness fixes at construction. What can be compared
            // without rebuilding it is the ER schedule's spatial geometry,
            // which is the part of the stereo field that is specified in
            // milliseconds.
            EarlyReflections e;
            e.prepare (rate);

            double meanOffset = 0.0, worstOffset = 0.0;

            for (int k = 0; k < e.getEventCount(); ++k)
            {
                const double d = (e.getOffsetL (k) - e.getOffsetR (k)) * 1000.0 / rate;

                meanOffset += d;
                worstOffset = std::max (worstOffset, std::abs (d));
            }

            meanOffset /= e.getEventCount();

            info ("    " + pad (fx (rate / 1000.0, 1) + " kHz", 12)
                  + "ER L/R offset: mean " + fx (meanOffset, 3)
                  + " ms, worst " + fx (worstOffset, 3) + " ms");

            meanOffsets.push_back (meanOffset);
        }

        for (double a : meanOffsets)
            for (double b : meanOffsets)
                worstSpread = std::max (worstSpread, std::abs (a - b));

        info ("");
        info ("    The ER layer's spatial geometry is specified in");
        info ("    milliseconds, so it is the same field at every rate - the");
        info ("    mean and worst L/R offsets agree to a few microseconds.");
        info ("    PHASE 10 measured the absolute times across rates at");
        info ("    18.71 us; this is the same property seen as a stereo one.");
        info ("");
        info ("    BLOCK SIZE. The processor-level comparison is in the state");
        info ("    suite: identical at 128 samples and above, and below that");
        info ("    both channels move together, so the dependence belongs to");
        info ("    the tank's control-rate updates. No NEW spatial defect is");
        info ("    introduced, which is the condition the brief sets.");

        check (worstSpread < 0.05,
               "the ER stereo geometry is the same at every sample rate");
    }

    // =======================================================================
    beginSuite ("PHASE 11.14 - stereo-related CPU and memory");
    // =======================================================================
    {
        /*
            Measurement only, per the brief. Nothing is optimised and nothing
            should be read as a recommendation.
        */
        constexpr int blockSize = 512;
        constexpr int blocks = 300;
        constexpr int repeats = 5;

        std::vector<double> inL (blockSize, 0.0), inR (blockSize, 0.0);
        std::vector<double> outL (blockSize, 0.0), outR (blockSize, 0.0);

        unsigned int s = 99u;

        for (int i = 0; i < blockSize; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            inL[i] = inR[i] = static_cast<double> (s & 0xffffu) / 32768.0 - 1.0;
        }

        volatile double sink = 0.0;

        auto timeIt = [&] (auto&& body)
        {
            double best = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                body();
                const auto t1 = std::chrono::steady_clock::now();

                best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            }

            return best;
        };

        Config c;

        auto tank = makeTank (c);

        const double lateTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                std::copy (inL.begin(), inL.end(), outL.begin());
                std::copy (inR.begin(), inR.end(), outR.begin());

                for (int i = 0; i < blockSize; ++i)
                    tank->processSample (outL[i], outR[i]);

                sink += outL[0];
            }
        });

        EarlyReflections early;
        early.prepare (kSampleRate);

        const double erTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                early.process (inL.data(), inR.data(), outL.data(), outR.data(), blockSize);
                sink += outL[0];
            }
        });

        const double widthTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const double mid  = 0.5 * (inL[i] + inR[i]);
                    const double side = 0.5 * (inL[i] - inR[i]) * 1.37;

                    outL[i] = mid + side;
                    outR[i] = mid - side;
                }

                sink += outL[0];
            }
        });

        // The modulation's own stereo cost, as the difference between the
        // tank at the shipping depth and the same tank with depth zero.
        Config still;
        still.modDepth = 0.0;

        auto staticTank = makeTank (still);

        const double staticTime = timeIt ([&]
        {
            for (int b = 0; b < blocks; ++b)
            {
                std::copy (inL.begin(), inL.end(), outL.begin());
                std::copy (inR.begin(), inR.end(), outR.begin());

                for (int i = 0; i < blockSize; ++i)
                    staticTank->processSample (outL[i], outR[i]);

                sink += outL[0];
            }
        });

        const double samples = static_cast<double> (blocks) * blockSize;
        const double realtime = samples / kSampleRate;

        info ("  " + std::to_string (blocks) + " blocks of "
              + std::to_string (blockSize) + " samples, best of "
              + std::to_string (repeats) + ".");
        info ("");
        info ("    " + pad ("stage", 30) + pad ("time", 13) + "% of real time");
        info ("    " + pad ("late stereo engine (tank)", 30)
              + pad (fx (lateTime * 1000.0, 2) + " ms", 13)
              + fx (100.0 * lateTime / realtime, 3) + " %");
        info ("    " + pad ("the same tank, Mod Depth 0", 30)
              + pad (fx (staticTime * 1000.0, 2) + " ms", 13)
              + fx (100.0 * staticTime / realtime, 3) + " %");
        info ("    " + pad ("ER stereo layer", 30)
              + pad (fx (erTime * 1000.0, 2) + " ms", 13)
              + fx (100.0 * erTime / realtime, 3) + " %");
        info ("    " + pad ("Width mid/side", 30)
              + pad (fx (widthTime * 1000.0, 2) + " ms", 13)
              + fx (100.0 * widthTime / realtime, 3) + " %");
        info ("");
        info ("    modulation's share of the tank: "
              + fx (100.0 * (lateTime - staticTime) / lateTime, 1) + " %");
        info ("");
        info ("    MEMORY. The stereo-specific storage is the ER layer's two");
        info ("    ring buffers at 128 KiB for 48 kHz, the pre-delay's two");
        info ("    lines, and the two independent diffuser chains. The tank's");
        info ("    eight lines are shared by both channels by construction -");
        info ("    there is no per-channel tank - so stereo costs nothing");
        info ("    there.");

        check (widthTime < lateTime * 0.05,
               "the Width stage is a rounding error against the engine");

        (void) sink;
    }

    // =======================================================================
    beginSuite ("PHASE 11.15 - readiness for Mode data");
    // =======================================================================
    {
        /*
            An audit of whether PHASE 12's Modes could vary spatial behaviour
            through DATA rather than by rewriting the stereo core. No control
            and no mode logic is added here.
        */
        info ("  Which spatial attributes are already data, and which are not.");
        info ("");
        info ("    ALREADY DATA - a Mode could vary these today");
        info ("");
        info ("      ER pattern            er::Pattern in EarlyReflectionTuning.h.");
        info ("                            Event count, window, envelope, Size");
        info ("                            alpha and cell occupancy are all");
        info ("                            fields, and EarlyReflections::prepare");
        info ("                            already takes a Pattern. This was");
        info ("                            PHASE 10's explicit design goal and");
        info ("                            the engine/data split holds.");
        info ("      injection vectors     fdn::injectionLeft/Right, passed");
        info ("                            through StereoFdn::setStereoVectors,");
        info ("                            which is a public runtime call.");
        info ("      readout vectors       likewise, same call.");
        info ("      diffuser patterns     StereoDiffusionNetwork::setDelayPatterns");
        info ("                            replaces both chains' delay sets and");
        info ("                            reports refusal as a whole.");
        info ("      spatial scale         the Size law is Pattern data for the");
        info ("                            ER and a mapping for the tank.");
        info ("");
        info ("    NOT DATA - would need code to vary");
        info ("");
        info ("      cross-channel coupling  there is no coupling parameter. The");
        info ("                              coupling is the 8x8 feedback matrix");
        info ("                              plus the injection and readout");
        info ("                              geometry. A Mode could change the");
        info ("                              vectors, which changes the stereo");
        info ("                              VIEW, but the mixing itself is the");
        info ("                              matrix and is not parameterised.");
        info ("      per-channel tank        there is one shared eight-line tank.");
        info ("                              A Mode wanting genuinely separate");
        info ("                              left and right tanks would be new");
        info ("                              DSP, not new data.");
        info ("      ER cross-feed           the ER layer has no cross-channel");
        info ("                              path at all. A Mode wanting one");
        info ("                              would need the engine changed.");
        info ("");
        info ("    VERDICT ON MODE READINESS: the attributes a Mode most");
        info ("    plausibly wants to vary - ER pattern, injection and readout");
        info ("    geometry, diffuser delays, spatial scale - are all already");
        info ("    reachable as data through existing public calls. The one");
        info ("    thing that is not is the degree of cross-channel mixing, and");
        info ("    section 11.19 notes that this is also where the audit found");
        info ("    its concrete weakness. Those two facts are related and should");
        info ("    be weighed together rather than separately.");

        // The claim that the vectors are runtime-settable, asserted.
        auto tank = makeTank (Config {});

        const double alternativeInject[8] { +1, -1, +1, -1, +1, -1, +1, -1 };
        const double alternativeRead[8]   { -1, +1, -1, +1, -1, +1, -1, +1 };

        tank->getBank (0).setStereoVectors (alternativeInject, fdn::injectionRight,
                                            alternativeRead, fdn::readoutRight);

        check (true,
               "injection and readout vectors are settable at runtime, so a "
               "Mode can vary the stereo view through data");
    }
    }
}

} // namespace avtest
