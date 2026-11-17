#include "Phase12Stage6.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DspAnalysis.h"

#include <cmath>
#include <cstring>

/*
    PHASE 12 - STAGE 6. The fast half: what the heavy results rest on.

    RESEARCH ONLY. Source/ IS NOT CHANGED.

    That the rig's Stage 6 options are inert at their defaults is PHASE
    12.5.0's job (its fingerprints were taken before Stage 5 and still have to
    match). This suite checks the new pieces themselves:

      - the Room and Ambient builders, with nothing changed, ARE Stage 5's
        R4c and A1b;
      - the pipelined chain is the plain chain;
      - every open-tank scattering matrix is orthogonal, and the open tank
        built as Hall is Hall;
      - the multirate filter bank, with its chains out, is a delay.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace
{
    [[nodiscard]] bool sameTopology (const Structure& a, const Structure& b)
    {
        return a.delaysMs == b.delaysMs && a.customMatrix == b.customMatrix
            && a.customInL == b.customInL && a.customInR == b.customInR
            && a.customOutL == b.customOutL && a.customOutR == b.customOutR
            && a.lineDecayRatio == b.lineDecayRatio && a.matrix == b.matrix
            && a.injection == b.injection && a.readout == b.readout;
    }
}

void runPhase12Stage6FastTests()
{
    using namespace stage6;

    beginSuite ("PHASE 12.6.0 - Stage 6's new pieces are what they claim to be");

    // --- builders ---------------------------------------------------------------
    {
        bool room = true, ambient = true;

        for (const auto& p : kPoints)
        {
            const User u = userAt (p);

            Structure a = stage5::standingRoom().best.make (u);
            Structure b = roomRaw (RoomSpec {}) (u);

            if (! sameTopology (a, b))
                room = false;

            a = stage5::standingAmbient().best.make (u);
            b = ambientRaw (AmbientSpec {}) (u);

            if (! sameTopology (a, b))
                ambient = false;
        }

        check (room,    "the Room builder with nothing changed is Stage 5's R4c, entry for entry");
        check (ambient, "the Ambient builder with nothing changed is Stage 5's A1b, entry for entry");
    }

    // --- the pipelined chain --------------------------------------------------------
    {
        const auto noise = whiteNoise (4096, 0xA11CEu);
        const std::vector<double> coefficients (240, -0.7);

        Dispersion plainL, plainR;
        plainL.prepare (240, -0.7);
        plainR.prepare (240, -0.7);

        std::vector<double> referenceL (noise.size()), referenceR (noise.size());

        for (size_t n = 0; n < noise.size(); ++n)
        {
            referenceL[n] = plainL.process (noise[n]);
            referenceR[n] = plainR.process (-0.5 * noise[n]);
        }

        bool exact = true;
        double worst = 0.0;

        for (int vector = 0; vector < 3; ++vector)
            for (int split : { 1, 4, 8, 16 })
            {
                PipelinedChain chain;

                if (! chain.prepare (coefficients, split, vector == 1, vector == 2))
                {
                    exact = false;
                    continue;
                }

                const auto lag = static_cast<size_t> (split - 1);

                for (size_t n = 0; n < noise.size(); ++n)
                {
                    double l = noise[n], r = -0.5 * noise[n];
                    chain.process (l, r);

                    if (n >= lag)
                    {
                        const double dl = std::abs (l - referenceL[n - lag]), dr = std::abs (r - referenceR[n - lag]);

                        worst = std::max (worst, std::max (dl, dr));

                        if (split == 1 && vector == 0 && (dl != 0.0 || dr != 0.0))
                            exact = false;
                    }
                }
            }

        check (exact, "one scalar sub-chain is the plain chain, bit for bit");
        check (worst < 1.0e-9, "every pipelined form is the plain chain delayed by (sub-chains - 1) samples");

        PipelinedChain odd;
        check (! odd.prepare (std::vector<double> (10, -0.7), 4, false), "a chain that does not divide evenly is refused");
    }

    // --- the open tank --------------------------------------------------------------
    {
        bool orthogonal = openHallSpec()->orthogonalityError() < 1.0e-12;

        for (const auto& s : chamberSpecs())
            if (s.second->orthogonalityError() > 1.0e-12)
            {
                orthogonal = false;
                info ("    not orthogonal: " + s.first);
            }

        check (orthogonal, "every open-tank scattering matrix is orthogonal");

        Structure open;
        open.open = openHallSpec();

        const User u;
        const auto a = impulseResponse (Structure {}, u, 0.5);
        const auto b = impulseResponse (open, u, 0.5);

        double difference = 0.0, energy = 0.0;

        for (size_t n = 0; n < a.l.size(); ++n)
        {
            difference += (a.l[n] - b.l[n]) * (a.l[n] - b.l[n]) + (a.r[n] - b.r[n]) * (a.r[n] - b.r[n]);
            energy += a.l[n] * a.l[n] + a.r[n] * a.r[n];
        }

        const double db = 10.0 * std::log10 (std::max (1.0e-30, difference) / energy);

        info ("    the open tank built as Hall, against Hall, first half second: " + fx (db, 0) + " dB");

        check (db < -60.0, "the open tank built as Hall is Hall to 60 dB, sample by sample");
    }

    // --- multirate -------------------------------------------------------------------
    {
        const auto noise = whiteNoise (8192, 0xF00Du);

        for (int bands : { 2, 3 })
        {
            Multirate bank;
            bank.prepare (*multirateDesign (bands));
            bank.setBypass (true);

            std::vector<double> y (noise.size());

            for (size_t n = 0; n < noise.size(); ++n)
                y[n] = bank.process (noise[n]);

            double best = 1.0e30, energy = 0.0;

            for (size_t n = 2000; n < 6000; ++n)
                energy += noise[n] * noise[n];

            for (size_t lag = 0; lag < 400; ++lag)
            {
                double e = 0.0;

                for (size_t n = 2000; n < 6000; ++n)
                    e += (y[n + lag] - noise[n]) * (y[n + lag] - noise[n]);

                best = std::min (best, e);
            }

            check (10.0 * std::log10 (std::max (1.0e-30, best) / energy) < -200.0,
                   "the " + std::to_string (bands) + "-band filter bank with its chains out is a pure delay");
        }
    }
}

} // namespace avtest
