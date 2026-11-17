#include "Phase12Structural.h"
#include "Suites.h"
#include "TestFramework.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

/*
    PHASE 12 - STAGE 5. The fast half: what protects the heavy results.

    RESEARCH ONLY. Source/ IS NOT CHANGED.

    Stage 5 adds options to the research rig (a custom matrix, custom
    injection and readout vectors, a stretched dispersion chain, per-line
    decay, a decay calibration). Stages 3 and 4 measured their candidates on
    that same rig, and their impostor searches take most of an hour to repeat.

    This suite is why they do not have to be repeated after a rig edit: it
    renders the structures those stages scored and compares each response,
    bit for bit, with a fingerprint taken BEFORE the Stage 5 options were
    added. If a fingerprint moves, the heavy suites' recorded numbers no
    longer describe the rig and must be re-run.

    AV_S5_FINGERPRINT=1 prints the fingerprints instead of checking them.
*/

using namespace av::dsp;

namespace avtest
{

using namespace stage3;
using namespace structural;

namespace
{
    [[nodiscard]] std::uint64_t fingerprintOf (const Stereo& x)
    {
        std::uint64_t h = 1469598103934665603ull;

        for (const auto* channel : { &x.l, &x.r })
            for (double v : *channel)
            {
                std::uint64_t bits;
                std::memcpy (&bits, &v, sizeof bits);

                for (int b = 0; b < 8; ++b)
                {
                    h ^= (bits >> (8 * b)) & 0xffu;
                    h *= 1099511628211ull;
                }
            }

        return h;
    }

    struct Case { std::string name; Structure structure; User user; bool leftOnly; std::uint64_t expected; };

    [[nodiscard]] std::vector<Case> buildCases()
    {
        std::vector<Case> cases;

        auto add = [&] (const char* name, Structure s, User u, bool leftOnly, std::uint64_t expected)
        {
            s.name = name;
            cases.push_back ({ name, s, u, leftOnly, expected });
        };

        const User medium = userAt (kPoints[0]);
        const User small  = userAt (kPoints[1]);
        const User large  = userAt (kPoints[2]);

        User busy = medium;
        busy.earlyLate = 0.5;
        busy.attack = 0.4;
        busy.width = 1.3;
        busy.earlyDiff = 0.35;

        add ("Hall, medium", Structure {}, medium, false, 0xd0605de5e984f5f1ull);
        add ("Hall, small",  Structure {}, small,  false, 0x8658a6e5f2a0ffebull);
        add ("Hall, large",  Structure {}, large,  false, 0xa3049e55d3224648ull);
        add ("Hall, early layer + attack + width", Structure {}, busy, false, 0x85965e054b90bb3full);
        add ("Hall, left only", Structure {}, medium, true, 0x52b6085149aed93full);

        {
            Structure s;
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = 0.30;
            s.injection = InjectionKind::split;
            s.readout = ReadoutKind::split;
            add ("Room B", s, medium, false, 0xdeded4fb06f0d3a6ull);
            add ("Room B, left only", s, small, true, 0xefb83c06a9f6d196ull);
        }
        {
            Structure s;
            s.preDispersion = 240;
            add ("Plate A", s, medium, false, 0x41b034c7262859cbull);
        }
        {
            Structure s;
            s.preDispersion = 120;
            s.preDispersionCoeff = -0.85;
            s.diffuserStages = 8;
            add ("Plate B", s, large, false, 0x4648c5c33c086657ull);
        }
        {
            Structure s;
            s.diffuserStages = 8;
            s.loopAllpasses = 2;
            s.readout = ReadoutKind::multiTap;
            add ("Chamber (Stage 3)", s, medium, false, 0x06d9316e7e78bafbull);
        }
        {
            Structure s;
            s.matrix = MatrixKind::coupledBlocks;
            s.coupling = 0.15;
            s.injection = InjectionKind::blockA;
            s.readout = ReadoutKind::blockB;
            s.genericVectors = true;
            add ("Ambient D", s, medium, false, 0x3175bdefb1af1c4bull);

            s.genericVectors = false;
            s.blockDecayRatio = 0.35;
            add ("Ambient C", s, small, false, 0x2c6574bbc0514a43ull);
        }
        {
            Structure s;
            s.matrix = MatrixKind::ring;
            add ("ring", s, large, false, 0x984ce6df1cf17129ull);
        }
        {
            Structure s;
            s.matrix = MatrixKind::householder;
            add ("householder", s, medium, false, 0x06fd240b1fee05eeull);
        }
        {
            Structure s;
            s.diffuser = DiffuserKind::velvet;
            add ("velvet", s, medium, false, 0x4baa2f3fcb8c6585ull);
        }
        {
            Structure s;
            s.injection = InjectionKind::staggered;
            s.staggerMs = 30.0;
            add ("staggered", s, medium, false, 0xc205fe73cb188368ull);
        }
        {
            Structure s;
            s.loopDispersion = 12;
            s.erFeedsTank = true;
            add ("loop dispersion + early feeds tank", s, busy, false, 0x293aec3a3ab853f5ull);
        }

        return cases;
    }
}

void runPhase12Stage5FastTests()
{
    // =======================================================================
    beginSuite ("PHASE 12.5.0 - the research rig still renders Stage 3 and 4's structures bit for bit");
    // =======================================================================

    info ("  Seventeen responses, half a second each, hashed. The expected values");
    info ("  were taken before Stage 5 added anything to the rig.");
    info ("");

    auto cases = buildCases();

    std::vector<std::uint64_t> got (cases.size());

    runParallel (cases.size(), [&] (size_t i)
    {
        got[i] = fingerprintOf (impulseResponse (cases[i].structure, cases[i].user, 0.5,
                                                 true, ! cases[i].leftOnly));
    });

    const bool print = std::getenv ("AV_S5_FINGERPRINT") != nullptr;

    int moved = 0;

    for (size_t i = 0; i < cases.size(); ++i)
    {
        char text[32];
        std::snprintf (text, sizeof text, "0x%016llxull", static_cast<unsigned long long> (got[i]));

        if (print)
            info ("    " + pad (cases[i].name, 40) + text);
        else if (got[i] != cases[i].expected)
        {
            ++moved;
            info ("    MOVED: " + pad (cases[i].name, 40) + text);
        }
    }

    if (! print)
        checkEqual (static_cast<long long> (moved), 0LL,
                    "every Stage 3 / Stage 4 structure renders exactly as it did before Stage 5");
}

} // namespace avtest
