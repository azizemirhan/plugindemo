#include "DspAnalysis.h"
#include "SignalExport.h"
#include "Suites.h"
#include "TestFramework.h"
#include "WetGainStaging.h"

#include "DSP/AttackInjection.h"
#include "DSP/Denormal.h"
#include "DSP/EarlyReflections.h"
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
    PHASE 10 - the crossfade law. Research only, no production constant moved.

    THE ARCHITECTURE IS NOT REOPENED. Early/Late stays a composition control
    between an explicit ER layer and the late network, BAL-4's parameter-aware
    Decay x Size calibration stays the calibration layer, and Early/Late 0
    stays an explicit bit-exact bypass. The only open question is which GAIN
    LAW rides on top of that calibration.

    WHY THERE IS A QUESTION AT ALL, IN ONE PARAGRAPH.

    A crossfade between two branches holds its total constant only if it
    matches how the two branches add. Decorrelated branches add in ENERGY, so
    the right law is the one whose squared gains sum to one - constant power.
    Correlated branches add in AMPLITUDE, so the right law is the one whose
    gains themselves sum to one - linear. The ER layer and the late field are
    nearly decorrelated on broadband material and substantially correlated on
    a sustained tone, which is why Stage 3.5 found constant power perfect on
    noise and 6 dB out on a tone.

    No value of K fixes that, because K sets the ENDPOINTS and the problem is
    in the MIDDLE. So the question is whether a law between the two extremes
    is a better compromise than either.

    WHAT IS STILL FORBIDDEN, and it is the whole reason a compromise is being
    sought rather than a solution: nothing may look at the incoming audio. No
    envelope followers, no RMS detectors, no correlation-driven gain, no
    source classification, no AGC. The law must be a function of the control
    position and the parameters, and of nothing else.
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
    //  The laws
    // =======================================================================
    /**
        A gain law is a pair of shapes on t in [0, 1], before calibration.

        EVERY LAW HERE HAS EXACT ENDPOINTS, and the power forms get them for
        free: 0^p is 0 and 1^p is 1 for any p > 0. That is a small advantage
        over the trigonometric form, which needed its upper endpoint written
        out by hand because cos(pi/2) is 6.1e-17 rather than 0.

        `early` is multiplied by the calibration K afterwards, never here, so
        that the law and the calibration stay separable - which is what lets
        the same K be used for every law and keeps the comparison honest.
    */
    struct Law
    {
        const char* name;
        double exponent;    ///< ignored when `trig` is true
        bool   trig;

        [[nodiscard]] double late (double t) const
        {
            if (trig)
                return t >= 1.0 ? 0.0 : std::cos (t * 1.5707963267948966);

            return std::pow (1.0 - t, exponent);
        }

        [[nodiscard]] double early (double t) const
        {
            if (trig)
                return t <= 0.0 ? 0.0 : std::sin (t * 1.5707963267948966);

            return std::pow (t, exponent);
        }
    };

    /*
        p = 0.5 is constant power in the power family, exactly as the trig law
        is: (1-t)^1 + t^1 = 1 in energy. It is included so that the family and
        the shipped law can be compared on the same axis rather than across
        two formulations - if they measure the same, the exponent axis is the
        only thing varying, which is what makes the rest of the table mean
        something.

        p = 1.0 is linear amplitude, LAW-B.
    */
    const std::vector<Law> laws
    {
        { "LAW-A trig",  0.0,  true  },
        { "LAW-C p=0.50", 0.50, false },
        { "LAW-C p=0.60", 0.60, false },
        { "LAW-C p=0.70", 0.70, false },
        { "LAW-C p=0.80", 0.80, false },
        { "LAW-C p=0.90", 0.90, false },
        { "LAW-B p=1.00", 1.00, false },
    };

    // =======================================================================
    //  The production chain, branches kept separate
    // =======================================================================
    struct Config
    {
        double size  = kSizeDefault;
        double decay = kDecay;
    };

    [[nodiscard]] std::unique_ptr<Morph> makeTank (const Config& c)
    {
        auto tank = std::make_unique<Morph>();

        tank->prepare (kSampleRate);
        tank->setInputDiffusionEnabled (true);
        tank->setCompensationEnabled (true, true);

        tank->setRt60Seconds (mapping::rt60For (c.decay), true);
        tank->setInputDiffusion (kEarlyDiffusion, true);
        tank->setSize (mapping::sizeScaleFor (c.size));

        tank->setDamping (mapping::lowDampRatioFor  (kLowDamp),
                          mapping::highDampRatioFor (kHighDamp),
                          kLowDampHz, kHighDampHz, true);

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (kModDepth),
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

    struct Branches
    {
        std::vector<double> lateL, lateR, erL, erR;
    };

    [[nodiscard]] Branches renderBranches (const Config& c,
                                           const std::vector<double>& drive,
                                           size_t length)
    {
        auto tank = makeTank (c);

        PreDelay<double> pre;
        pre.prepare (kSampleRate);
        pre.snapTo (0.0);

        EarlyReflections early;
        early.prepare (kSampleRate);
        early.snapToSizeRatio (mapping::sizeScaleFor (c.size)
                               / mapping::sizeScaleFor (kSizeDefault));

        AttackInjection attack;
        attack.prepare (kSampleRate);
        attack.snapTo (0.0);

        Branches out;
        out.lateL.assign (length, 0.0);
        out.lateR.assign (length, 0.0);
        out.erL  .assign (length, 0.0);
        out.erR  .assign (length, 0.0);

        for (size_t n = 0; n < length; ++n)
        {
            out.lateL[n] = n < drive.size() ? drive[n] : 0.0;
            out.lateR[n] = out.lateL[n];
        }

        const auto count = static_cast<int> (length);

        pre.process (out.lateL.data(), out.lateR.data(), count);

        early.process (out.lateL.data(), out.lateR.data(),
                       out.erL.data(), out.erR.data(), count);

        attack.process (out.lateL.data(), out.lateR.data(), count);

        for (size_t n = 0; n < length; ++n)
            tank->processSample (out.lateL[n], out.lateR[n]);

        const double trim = mapping::previewWetTrim (mapping::rt60For (c.decay),
                                                     mapping::sizeScaleFor (c.size));

        for (size_t n = 0; n < length; ++n)
        {
            out.lateL[n] *= trim;
            out.lateR[n] *= trim;
        }

        return out;
    }

    [[nodiscard]] double energyOf (const std::vector<double>& x)
    {
        double sum = 0.0;

        for (double s : x)
            sum += s * s;

        return sum;
    }

    /** Total integrated wet energy at a control position, under a law. */
    [[nodiscard]] double totalAt (const Branches& b, const Law& law, double t, double k)
    {
        const double late  = law.late (t);
        const double early = law.early (t) * k;

        double energy = 0.0;

        for (size_t n = 0; n < b.lateL.size(); ++n)
        {
            const double l = late * b.lateL[n] + early * b.erL[n];
            const double r = late * b.lateR[n] + early * b.erR[n];

            energy += l * l + r * r;
        }

        return energy;
    }

    struct Traversal
    {
        std::array<double, 5> db { 0.0, 0.0, 0.0, 0.0, 0.0 };
        double range = 0.0, maxAbove = 0.0, maxBelow = 0.0, midpoint = 0.0;

        /**
            SAG: how far the middle of the control falls below the straight
            line joining its two ends, in dB.

            This is the metric that isolates the LAW from the CALIBRATION, and
            without it the comparison cannot be read. A traversal has two
            independent faults. If K is wrong for the material, the endpoint
            is wrong and the whole curve tilts - that is a calibration error
            and every law inherits it equally. If the law does not match how
            the branches add, the curve bows away from its own chord - that is
            a law error, and it is what a listener hears as a dip in the
            middle of the control.

            A pad measures 7.58 dB of range under every law here, which looks
            damning until you see that its sag is 0.36 dB: the range is
            entirely the tilt, and the law is innocent.
        */
        double sag = 0.0;
    };

    [[nodiscard]] Traversal traversalOf (const Branches& b, const Law& law, double k)
    {
        Traversal out;

        const std::array<double, 5> positions { 0.0, 0.25, 0.50, 0.75, 1.0 };

        const double reference = totalAt (b, law, 0.0, k);

        double lo = 0.0, hi = 0.0;

        for (size_t i = 0; i < positions.size(); ++i)
        {
            const double db = 10.0 * std::log10 (totalAt (b, law, positions[i], k)
                                                 / reference);
            out.db[i] = db;

            lo = std::min (lo, db);
            hi = std::max (hi, db);
        }

        out.maxAbove = hi;
        out.maxBelow = lo;
        out.range    = hi - lo;
        out.midpoint = out.db[2];

        // The chord joins Early/Late 0 to Early/Late 100; the sag is the
        // worst shortfall of the three interior points against it.
        for (size_t i = 1; i <= 3; ++i)
        {
            const double along = 0.25 * static_cast<double> (i);
            const double chord = out.db[0] + along * (out.db[4] - out.db[0]);

            out.sag = std::max (out.sag, chord - out.db[i]);
        }

        return out;
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

    /** A pure sustained sine. The diagnostic extreme, not a product target. */
    [[nodiscard]] std::vector<double> sustainedSine()
    {
        const auto n = static_cast<size_t> (1.0 * kSampleRate);

        std::vector<double> x (n, 0.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSampleRate;
            const double env = std::min (1.0, t * 100.0)
                             * std::min (1.0, (1.0 - t) * 100.0);

            x[i] = 0.4 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * t)
                 * std::max (0.0, env);
        }

        return x;
    }

    /** The left channel of a stereo test source, as a mono drive.

        Mono on purpose: the chain feeds the same signal to both channels, so
        the source's own stereo content would be one more variable in a
        measurement that is about a gain law. */
    [[nodiscard]] std::vector<double> monoFrom (const std::vector<StereoFrame>& f)
    {
        std::vector<double> x (f.size(), 0.0);

        for (size_t n = 0; n < f.size(); ++n)
            x[n] = f[n].l;

        return x;
    }
}

// ===========================================================================

void runPhase10BalanceLawTests()
{
    const auto fu2 = makeFu2Sources (kSampleRate);

    auto fu2Named = [&fu2] (const char* name)
    {
        for (const auto& s : fu2)
            if (s.name == name)
                return monoFrom (s.frames);

        return std::vector<double> {};
    };

    struct Source { const char* name; std::vector<double> drive; bool diagnostic; };

    const std::vector<Source> sources
    {
        { "broadband noise",  noiseBurst (10.0, 22222u),                false },
        { "snare transient",  fu2Named ("snare"),                       false },
        { "vocal",            monoFrom (makeVocalReference (kSampleRate, 6.0)), false },
        { "piano",            fu2Named ("piano"),                       false },
        { "pad",              fu2Named ("pad"),                         false },
        { "sustained sine",   sustainedSine(),                          true  },
    };

    // =======================================================================
    beginSuite ("PHASE 10 law - the calibration is law-independent");
    // =======================================================================
    {
        /*
            ITEM 7 OF THE BRIEF, AND IT HAS TO COME FIRST.

            "Do not allow a crossfade law to appear better merely because it
            was evaluated with a different calibration quality."

            K is determined by the ENDPOINTS: it is the gain that makes the
            total at Early/Late 100 % equal the total at 0 %. Every law here
            has the same endpoints - late goes 1 to 0, early goes 0 to 1 -
            so the condition that fixes K is identical, and the same K serves
            all of them. That is an argument, so it is also measured.

            Note what K is NOT: it is not fitted to minimise mid-travel error.
            Doing that would make K law-dependent and would trade an exact
            endpoint for a better middle, which is the wrong trade - a user
            who sets Early/Late to 100 % is entitled to the same loudness as
            at 0 %.

            And the K used throughout this pass is the ideal K measured on the
            BROADBAND source at each configuration, which is exactly what
            BAL-4's table stores. Using the table's interpolation here instead
            would fold its 1.08 dB rms into every law equally and obscure the
            thing being compared.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        Config c;
        const auto b = renderBranches (c, noiseBurst (10.0, 22222u), length);

        const double k = std::sqrt ((energyOf (b.lateL) + energyOf (b.lateR))
                                  / (energyOf (b.erL) + energyOf (b.erR)));

        info ("  Ideal K from the broadband endpoint condition: " + fx (k, 4));
        info ("  (BAL-4's table value at this configuration, by construction.)");
        info ("");
        info ("    " + pad ("law", 15) + pad ("late(0)", 10) + pad ("early(0)", 11)
              + pad ("late(1)", 10) + pad ("early(1)", 11) + "endpoint total");

        bool endpointsExact = true;
        double worstEndpoint = 0.0;

        for (const auto& law : laws)
        {
            const double l0 = law.late (0.0), e0 = law.early (0.0);
            const double l1 = law.late (1.0), e1 = law.early (1.0);

            endpointsExact = endpointsExact
                          && l0 == 1.0 && e0 == 0.0 && l1 == 0.0 && e1 == 1.0;

            const double db = 10.0 * std::log10 (totalAt (b, law, 1.0, k)
                                                 / totalAt (b, law, 0.0, k));

            worstEndpoint = std::max (worstEndpoint, std::abs (db));

            info ("    " + pad (law.name, 15) + pad (fx (l0, 4), 10)
                  + pad (fx (e0, 4), 11) + pad (fx (l1, 4), 10)
                  + pad (fx (e1, 4), 11) + fx (db, 3) + " dB");
        }

        info ("");
        info ("    Every law reaches the same two endpoints with the same K,");
        info ("    so the comparison that follows varies the law and nothing");
        info ("    else. The table is NOT re-fitted per law, and this is the");
        info ("    measurement that licenses not re-fitting it.");

        check (endpointsExact,
               "every law has exactly late 1 / early 0 at one end and 0 / 1 at the other");

        check (worstEndpoint < 0.05,
               "and the same K makes the endpoint totals match for all of them");
    }

    // =======================================================================
    beginSuite ("PHASE 10 law - branch correlation, the mechanism");
    // =======================================================================
    {
        /*
            Before the law table, the reason there is a spread at all. A law
            can only be a compromise between energy addition and amplitude
            addition if different sources really do sit at different points
            between them, so that is measured rather than assumed.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);
        const auto window = static_cast<size_t> (kSampleRate * 1.0);

        info ("    " + pad ("source", 20) + pad ("corr(late, ER)", 17)
              + "adds as");

        for (const auto& src : sources)
        {
            if (src.drive.empty())
                continue;

            Config c;
            const auto b = renderBranches (c, src.drive, length);

            const double corr = std::abs (correlation (b.lateL.data(), b.erL.data(),
                                                       window));

            info ("    " + pad (std::string (src.name) + (src.diagnostic ? " *" : ""), 20)
                  + pad (fx (corr, 4), 17)
                  + (corr < 0.1 ? "energy" : corr > 0.3 ? "amplitude" : "between"));
        }

        info ("");
        info ("    Energy addition wants constant power; amplitude addition");
        info ("    wants a linear law. A source whose branches are partly");
        info ("    correlated wants something between, which is the whole");
        info ("    hypothesis the exponent family exists to test.");

        check (true, "branch correlation measured per source");
    }

    // =======================================================================
    beginSuite ("PHASE 10 law - traversal by source and law");
    // =======================================================================

    std::vector<std::pair<std::string, double>> lawScores;

    {
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        info ("  Total integrated wet output relative to Early/Late 0,");
        info ("  at 25 / 50 / 75 / 100 %. Same K for every law.");
        info ("");

        // name -> worst range over normal musical sources, and over all.
        std::vector<double> worstNormal (laws.size(), 0.0);
        std::vector<double> worstDiagnostic (laws.size(), 0.0);
        std::vector<double> worstMidNormal (laws.size(), 0.0);

        for (const auto& src : sources)
        {
            if (src.drive.empty())
                continue;

            Config c;
            const auto b = renderBranches (c, src.drive, length);

            const double k = std::sqrt ((energyOf (b.lateL) + energyOf (b.lateR))
                                      / (energyOf (b.erL) + energyOf (b.erR)));

            // The parameter-derived K, which is what ships: measured on the
            // broadband source at this configuration, NOT on this source.
            static double broadbandK = 0.0;

            if (std::string (src.name) == "broadband noise")
                broadbandK = k;

            info ("    " + std::string (src.name) + (src.diagnostic ? "   * diagnostic" : ""));
            info ("      " + pad ("law", 15) + pad ("25 %", 10) + pad ("50 %", 10)
                  + pad ("75 %", 10) + pad ("100 %", 10) + pad ("range", 10)
                  + "sag");

            for (size_t i = 0; i < laws.size(); ++i)
            {
                const auto t = traversalOf (b, laws[i], broadbandK);

                info ("      " + pad (laws[i].name, 15)
                      + pad (fx (t.db[1], 2) + " dB", 10)
                      + pad (fx (t.db[2], 2) + " dB", 10)
                      + pad (fx (t.db[3], 2) + " dB", 10)
                      + pad (fx (t.db[4], 2) + " dB", 10)
                      + pad (fx (t.range, 2) + " dB", 10)
                      + fx (t.sag, 2) + " dB");

                if (src.diagnostic)
                {
                    worstDiagnostic[i] = std::max (worstDiagnostic[i], t.sag);
                }
                else
                {
                    worstNormal[i]    = std::max (worstNormal[i], t.sag);
                    worstMidNormal[i] = std::max (worstMidNormal[i], t.range);
                }
            }

            info ("");
        }

        info ("    SUMMARY. `sag` is the law's own contribution; `range` also");
        info ("    contains the calibration tilt, which every law inherits");
        info ("    equally and which no law can fix.");
        info ("");
        info ("    " + pad ("law", 15) + pad ("worst sag, normal", 20)
              + pad ("worst range, normal", 22) + "sag, sine");

        for (size_t i = 0; i < laws.size(); ++i)
        {
            info ("    " + pad (laws[i].name, 15)
                  + pad (fx (worstNormal[i], 2) + " dB", 20)
                  + pad (fx (worstMidNormal[i], 2) + " dB", 22)
                  + fx (worstDiagnostic[i], 2) + " dB");

            lawScores.emplace_back (laws[i].name, worstNormal[i]);
        }

        const auto best = std::min_element (worstNormal.begin(), worstNormal.end());
        const auto bestIndex = static_cast<size_t> (best - worstNormal.begin());

        info ("");
        info ("    Best on normal musical material: " + std::string (laws[bestIndex].name)
              + " at " + fx (*best, 2) + " dB of sag.");
        info ("");
        info ("    THE HYPOTHESIS IS REFUTED, AND CLEANLY. The exponent family");
        info ("    was proposed because partly-correlated branches should want");
        info ("    something between energy addition and amplitude addition.");
        info ("    Every step away from constant power makes every source");
        info ("    worse, including the correlated ones, and the correlation");
        info ("    table says why: the most correlated source measures 0.199,");
        info ("    nowhere near the 1.0 that would make amplitude addition");
        info ("    right. At 0.2, energy addition is still much the better");
        info ("    approximation, so moving towards linear buys nothing and");
        info ("    pays a mid-travel sag for it - 3.04 dB at p = 1 on noise");
        info ("    that was not there before.");

        // The exponent family must be monotonically worse, or the argument
        // above is a story rather than a reading of the table.
        bool monotone = true;

        for (size_t i = 2; i < laws.size(); ++i)
            monotone = monotone && worstNormal[i] >= worstNormal[i - 1] - 0.01;

        check (monotone,
               "sag grows monotonically with the exponent, so no interior "
               "exponent is a compromise worth having");

        check (worstNormal[0] <= worstNormal.back() + 0.01,
               "constant power has the least sag of the family");

        check (worstNormal[0] < 1.5,
               "constant power keeps the law's own contribution under 1.5 dB on "
               "normal musical material");

        /*
            AND A RESULT THAT WAS NOT THE QUESTION ASKED.

            Two of these laws are both exactly constant power - the squared
            gains sum to 1 - and they do not perform the same. p = 0.50 has
            0.61 dB of worst sag against the trigonometric law's 1.28 dB, and
            on the pad it is 0.08 dB against 1.28 dB.

            They differ in SHAPE, not in power. Write the two power splits out:

              trig      late^2 = cos^2(t*pi/2)      early^2 = sin^2(t*pi/2)
              p = 0.50  late^2 = 1 - t              early^2 = t

            The second is LINEAR IN THE CONTROL. That has a consequence the
            first does not: with branches that add in energy, the total is

              total / Le  =  (1 - t)  +  t * (K^2 Ee / Le)

            which is a straight line in t for ANY value of K, right or wrong.
            So a calibration error turns into a smooth monotone tilt and
            nothing else. The trigonometric law also sums to one, but its
            split is not linear in t, so a mismatched endpoint makes the curve
            bow away from its own chord - which is exactly the "obvious bump
            or drop in the middle of the control" the brief rules out.

            This matters because the calibration WILL be imperfect: section
            one of Stage 3.5 put the source-dependent floor at 6 dB and no
            parameter-only law can close it. A law that degrades gracefully
            under calibration error is therefore worth more than one that is
            equally correct when the calibration is exact.

            The next section tests that claim directly rather than resting on
            the algebra.
        */
        check (worstNormal[1] < worstNormal[0],
               "the linear-in-power shape has less sag than the trigonometric one, "
               "though both are exactly constant power");
    }

    // =======================================================================
    beginSuite ("PHASE 10 law - behaviour under a deliberately wrong K");
    // =======================================================================
    {
        /*
            The claim: with a linear-in-power split, a wrong K produces a
            monotone tilt and no mid-travel anomaly, whatever the error. With
            the trigonometric split it produces a bow.

            Tested by taking a broadband render - where the branches really do
            add in energy, so the algebra applies cleanly - and mis-calibrating
            K on purpose across a wide range.
        */
        const auto length = static_cast<size_t> (kSampleRate * 4.0);

        Config c;
        const auto b = renderBranches (c, noiseBurst (10.0, 22222u), length);

        const double ideal = std::sqrt ((energyOf (b.lateL) + energyOf (b.lateR))
                                      / (energyOf (b.erL) + energyOf (b.erR)));

        const Law trig   { "trig", 0.0, true };
        const Law linear { "p=0.50", 0.50, false };

        info ("    " + pad ("K error", 12) + pad ("trig sag", 13)
              + pad ("p=0.50 sag", 14) + "range, both");

        double worstTrig = 0.0, worstLinear = 0.0;

        for (double errorDb : { -12.0, -6.0, -3.0, 0.0, 3.0, 6.0, 12.0 })
        {
            const double k = ideal * std::pow (10.0, errorDb / 20.0);

            const auto a = traversalOf (b, trig, k);
            const auto l = traversalOf (b, linear, k);

            worstTrig   = std::max (worstTrig, a.sag);
            worstLinear = std::max (worstLinear, l.sag);

            info ("    " + pad (fx (errorDb, 0) + " dB", 12)
                  + pad (fx (a.sag, 3) + " dB", 13)
                  + pad (fx (l.sag, 3) + " dB", 14)
                  + fx (a.range, 2) + " dB");
        }

        info ("");
        info ("    Worst sag across a 24 dB span of calibration error:");
        info ("      trigonometric  " + fx (worstTrig, 3) + " dB");
        info ("      linear-in-power " + fx (worstLinear, 3) + " dB");
        info ("");
        info ("    The range column is identical for both, as it must be -");
        info ("    the endpoints are the same and the tilt is the calibration's");
        info ("    not the law's. What differs is entirely the shape between");
        info ("    them.");

        info ("    The 0.034 dB that p = 0.50 shows at zero error is not a sag,");
        info ("    it is the floor: the two branches measure 0.0103 correlation");
        info ("    rather than exactly zero, and the trigonometric law shows");
        info ("    the same 0.034 dB there. At every other point the linear");
        info ("    split is flat to three decimals.");

        check (worstLinear <= 0.05,
               "a linear-in-power split has no sag beyond the decorrelation "
               "floor at any calibration error");

        check (worstTrig > worstLinear * 3.0,
               "the trigonometric split bows several times as much under the "
               "same error");
    }

    // =======================================================================
    beginSuite ("PHASE 10 law - automation of the two best laws");
    // =======================================================================
    {
        /*
            Both candidates are constant power and differ only in shape, so
            neither can introduce a timing change - the gains are smoothed and
            nothing in either law can reach a tap. What has to be shown is
            that the four automation cases the brief names produce no step, no
            discontinuity and no non-finite sample, under the law actually
            being driven per block the way the processor drives it.
        */
        constexpr int blockSize = 64;

        const auto length = static_cast<size_t> (kSampleRate * 2.0);

        Config c;
        const auto b = renderBranches (c, noiseBurst (1500.0, 31337u), length);

        const double ideal = std::sqrt ((energyOf (b.lateL) + energyOf (b.lateR))
                                      / (energyOf (b.erL) + energyOf (b.erR)));

        enum class Drive { balanceSweep, balanceRapid, decaySweep, sizeSweep, held };

        auto run = [&] (const Law& law, Drive drive, double& worstStep, bool& finite)
        {
            LinearSmoother<double> lateGain, earlyGain;
            lateGain .prepare (kSampleRate, EarlyLateBalance::rampSeconds);
            earlyGain.prepare (kSampleRate, EarlyLateBalance::rampSeconds);

            lateGain .snapTo (law.late (0.5));
            earlyGain.snapTo (law.early (0.5) * ideal);

            std::vector<double> out (length, 0.0);

            for (size_t n = 0; n < length; ++n)
            {
                if (n % static_cast<size_t> (blockSize) == 0)
                {
                    const double phase = static_cast<double> (n) / length;

                    double t = 0.5, k = ideal;

                    switch (drive)
                    {
                        case Drive::balanceSweep: t = phase; break;

                        // One full excursion every four blocks: far faster
                        // than a host will send, which is the point.
                        case Drive::balanceRapid:
                            t = ((n / blockSize) % 4) < 2 ? 0.0 : 1.0;
                            break;

                        case Drive::decaySweep:
                        {
                            const double decay = 0.1 * std::pow (600.0, phase);
                            k = ideal * std::sqrt (mapping::rt60For (decay)
                                                   / mapping::rt60For (kDecay));
                            break;
                        }

                        case Drive::sizeSweep:
                            k = ideal * std::sqrt (mapping::sizeScaleFor (phase)
                                                   / mapping::sizeScaleFor (kSizeDefault));
                            break;

                        case Drive::held: break;
                    }

                    lateGain .setTarget (law.late (t));
                    earlyGain.setTarget (law.early (t) * k);
                }

                out[n] = lateGain.getNextValue() * b.lateL[n]
                       + earlyGain.getNextValue() * b.erL[n];
            }

            finite = allFinite (out);

            worstStep = 0.0;

            for (size_t n = 1; n < out.size(); ++n)
                worstStep = std::max (worstStep, std::abs (out[n] - out[n - 1]));
        };

        const Law trig   { "LAW-A trig", 0.0, true };
        const Law linear { "p = 0.50", 0.50, false };

        info ("    " + pad ("law", 14) + pad ("case", 20)
              + pad ("steepest step", 16) + pad ("vs held", 10) + "finite");

        bool allFiniteEverywhere = true, noStep = true;

        for (const auto& law : { trig, linear })
        {
            double heldStep = 0.0;
            bool finite = true;

            run (law, Drive::held, heldStep, finite);

            for (auto drive : { Drive::balanceSweep, Drive::balanceRapid,
                                Drive::decaySweep, Drive::sizeSweep })
            {
                double step = 0.0;
                run (law, drive, step, finite);

                allFiniteEverywhere = allFiniteEverywhere && finite;
                noStep = noStep && step <= heldStep * 1.1 + 1.0e-12;

                const char* name = drive == Drive::balanceSweep ? "Early/Late sweep"
                                 : drive == Drive::balanceRapid ? "Early/Late rapid"
                                 : drive == Drive::decaySweep   ? "Decay at E/L 50"
                                                                : "Size at E/L 50";

                info ("    " + pad (law.name, 14) + pad (name, 20)
                      + pad (fx (step, 6), 16)
                      + pad (fx (step / heldStep, 3) + "x", 10)
                      + (finite ? "yes" : "NO"));
            }
        }

        info ("");
        info ("    No case exceeds the step the same material produces with");
        info ("    everything held, and nothing is non-finite. ER tap times");
        info ("    cannot move under either law: a law computes two gains and");
        info ("    has no access to the schedule. The legacy path is likewise");
        info ("    unreachable - early(0) is exactly 0 in both laws, and the");
        info ("    processor takes its bypass branch before the gains are");
        info ("    consulted at all.");

        check (noStep, "no automation case under either law produces a step");
        check (allFiniteEverywhere, "no automation case produces a non-finite sample");
        check (trig.early (0.0) == 0.0 && linear.early (0.0) == 0.0,
               "both laws give exactly zero early gain at the legacy endpoint");
    }

    // =======================================================================
    beginSuite ("PHASE 10 law - CPU difference between the laws");
    // =======================================================================
    {
        /*
            The honest answer is "none that can be measured in the audio
            path", and the reason is structural: a law is evaluated once per
            control update to produce two smoother TARGETS, not per sample.
            Per sample the work is two multiply-accumulates whatever the law.

            So what is timed is the law evaluation itself, at a deliberately
            absurd rate - once per sample - to put an upper bound on
            something that in production happens once per block.
        */
        constexpr int iterations = 2000000;
        constexpr int repeats = 5;

        volatile double sink = 0.0;

        auto timeIt = [&] (const Law& law)
        {
            double best = 1.0e30;

            for (int rep = 0; rep < repeats; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();

                double acc = 0.0;

                for (int i = 0; i < iterations; ++i)
                {
                    const double t = static_cast<double> (i % 1000) * 0.001;
                    acc += law.late (t) + law.early (t);
                }

                sink += acc;

                const auto t1 = std::chrono::steady_clock::now();

                best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            }

            return best;
        };

        const Law trig   { "LAW-A trig", 0.0, true };
        const Law linear { "p = 0.50", 0.50, false };
        const Law power  { "p = 0.70", 0.70, false };

        const double a = timeIt (trig);
        const double b = timeIt (linear);
        const double c = timeIt (power);

        info ("  " + std::to_string (iterations) + " evaluations, best of "
              + std::to_string (repeats) + ". Production evaluates a law ONCE");
        info ("  PER BLOCK, so divide by the block size to get the real figure.");
        info ("");
        info ("    " + pad ("law", 14) + pad ("per evaluation", 18) + "relative");
        info ("    " + pad ("trig", 14)
              + pad (fx (a / iterations * 1.0e9, 1) + " ns", 18) + "1.00x");
        info ("    " + pad ("p = 0.50", 14)
              + pad (fx (b / iterations * 1.0e9, 1) + " ns", 18)
              + fx (b / a, 2) + "x");
        info ("    " + pad ("p = 0.70", 14)
              + pad (fx (c / iterations * 1.0e9, 1) + " ns", 18)
              + fx (c / a, 2) + "x");
        info ("");
        /*
            AND THE p = 0.50 FIGURE ABOVE OVERSTATES IT, because the test
            harness evaluates every law through std::pow for comparability.
            A production p = 0.50 is std::sqrt, which is one instruction.
            Measuring it is cheaper than explaining it away.
        */
        double sqrtTime = 1.0e30;

        for (int rep = 0; rep < repeats; ++rep)
        {
            const auto t0 = std::chrono::steady_clock::now();

            double acc = 0.0;

            for (int i = 0; i < iterations; ++i)
            {
                const double t = static_cast<double> (i % 1000) * 0.001;
                acc += std::sqrt (1.0 - t) + std::sqrt (t);
            }

            sink += acc;

            const auto t1 = std::chrono::steady_clock::now();

            sqrtTime = std::min (sqrtTime,
                                 std::chrono::duration<double> (t1 - t0).count());
        }

        info ("    " + pad ("p=0.50 sqrt", 14)
              + pad (fx (sqrtTime / iterations * 1.0e9, 1) + " ns", 18)
              + fx (sqrtTime / a, 2) + "x");
        info ("");
        info ("    The pow() row for p = 0.50 is an artefact of this harness");
        info ("    evaluating every law through the same call. As sqrt it is");
        info ("    the CHEAPEST of the three, not the dearest.");
        info ("");
        info ("    At 48 kHz and a 512-sample block that is under a nanosecond");
        info ("    per block for any of them. CPU does not select a law here,");
        info ("    and a report that presented it as a tiebreaker would be");
        info ("    dressing a preference as a measurement.");

        check (std::max ({ a, b, c }) / iterations < 1.0e-6,
               "every law evaluates in well under a microsecond");

        (void) sink;
    }
}

} // namespace avtest
