#include "ModalMap.h"
#include "SizeTrack.h"
#include "TestFramework.h"

#include "DSP/ParameterMapping.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

/*
    Phase 6 / FU-1: synthetic validation of the Size tracker.

    The tank is not involved. Every peak list here is built from a law that is
    known in advance, so "did it keep the identity" has an answer rather than a
    plausibility argument. That is the whole point: the old tracker reported
    high-confidence identities on the Size axis and they were swaps, and no
    amount of looking at its output on real data could have said so.

    Nothing in here is allowed to run against the real sweep until it passes.
*/
namespace avtest
{

namespace
{
    using av::dsp::mapping::sizeScaleFor;

    // The sweep's own Size points, so the synthetic scale ratios are the ones
    // the tracker will actually meet: a fixed 2^(1/3) per step.
    const double sizeControls[] = { 0.0, 0.1111, 0.2222, 0.3333, 0.4444,
                                    0.5556, 0.6667, 0.7778, 0.8889, 1.0 };

    constexpr size_t frameCount = 10;

    // Nearest sweep point to the shipping default of 0.60.
    constexpr size_t defaultAnchorFrame = 5;

    constexpr double bandLowHz = 40.0;
    constexpr double bandHighHz = 12000.0;

    [[nodiscard]] std::string fx (double v, int p)
    {
        std::ostringstream o; o << std::fixed << std::setprecision (p) << v; return o.str();
    }

    [[nodiscard]] std::string padR (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : s + std::string (w - s.size(), ' ');
    }

    [[nodiscard]] std::string padL (const std::string& s, size_t w)
    {
        return s.size() >= w ? s : std::string (w - s.size(), ' ') + s;
    }

    /** checkEqual on two counts. The framework overloads it on long long and
        on double, and an int argument matches neither better. */
    void checkCount (int actual, int expected, const std::string& description)
    {
        checkEqual (static_cast<long long> (actual), static_cast<long long> (expected),
                    description);
    }

    // -----------------------------------------------------------------------
    //  Building a synthetic sweep
    // -----------------------------------------------------------------------

    /**
        One synthetic component.

        `exponent` is the power of the size scale its frequency follows: -1 is
        a loop mode under uniform scaling, 0 is something fixed. `owner` is the
        ground truth, and is -1 for anything that is not a tracked mode.
    */
    struct Component
    {
        double referenceHz = 1000.0;   /**< Frequency at scale 1. */
        double exponent = -1.0;
        double prominenceAtLowSize = 30.0;
        double prominenceAtHighSize = 30.0;
        int    owner = -1;
        int    absentAtFrame = -1;     /**< One frame where it is deleted. */

        [[nodiscard]] double frequencyAt (double scale) const
        {
            return referenceHz * std::pow (scale, exponent);
        }

        [[nodiscard]] double prominenceAt (size_t frame) const
        {
            const double u = static_cast<double> (frame)
                           / static_cast<double> (frameCount - 1);

            return prominenceAtLowSize + u * (prominenceAtHighSize - prominenceAtLowSize);
        }
    };

    struct SynthSweep
    {
        std::vector<SizeFrame> frames;

        /** Per frame, the owner of each peak, parallel to the peak list. */
        std::vector<std::vector<int>> owners;
    };

    [[nodiscard]] SynthSweep build (const std::vector<Component>& components,
                                    double meanModalSpacingAtUnitScale)
    {
        SynthSweep sweep;

        sweep.frames.resize (frameCount);
        sweep.owners.resize (frameCount);

        for (size_t f = 0; f < frameCount; ++f)
        {
            auto& frame = sweep.frames[f];

            frame.sizeControl = sizeControls[f];
            frame.sizeScale = sizeScaleFor (sizeControls[f]);
            frame.meanModalSpacingHz = meanModalSpacingAtUnitScale / frame.sizeScale;

            struct Entry { double hz, prominence; int owner; };

            std::vector<Entry> entries;

            for (const auto& c : components)
            {
                if (c.absentAtFrame >= 0 && static_cast<size_t> (c.absentAtFrame) == f)
                    continue;

                const double hz = c.frequencyAt (frame.sizeScale);

                if (hz < bandLowHz || hz > bandHighHz)
                    continue;

                entries.push_back ({ hz, c.prominenceAt (f), c.owner });
            }

            std::sort (entries.begin(), entries.end(),
                       [] (const Entry& a, const Entry& b) { return a.hz < b.hz; });

            for (const auto& e : entries)
            {
                SizePeak p;

                p.frequencyHz = e.hz;
                p.prominenceDb = e.prominence;
                p.bandwidthHz = 0.0;
                p.qFactor = 0.0;
                p.widthResolved = false;

                frame.peaks.push_back (p);
                sweep.owners[f].push_back (e.owner);
            }
        }

        return sweep;
    }

    /**
        A bed of neighbouring modes around a tracked one.

        Real modal spacing, real competitors: each obeys uniform scaling too, so
        the bed is not a straw man that stands still while the target moves.
    */
    void addBed (std::vector<Component>& into, double centreHzAtUnitScale,
                 double spacingHz, int eachSide, double prominence)
    {
        for (int k = -eachSide; k <= eachSide; ++k)
        {
            if (k == 0)
                continue;

            Component c;

            c.referenceHz = centreHzAtUnitScale + static_cast<double> (k) * spacingHz;
            c.exponent = -1.0;
            c.prominenceAtLowSize = prominence;
            c.prominenceAtHighSize = prominence;
            c.owner = -1;

            if (c.referenceHz > 1.0)
                into.push_back (c);
        }
    }

    // -----------------------------------------------------------------------
    //  Scoring against the ground truth
    // -----------------------------------------------------------------------

    struct Score
    {
        int expected = 0;      /**< Points where the true peak exists in band. */
        int correct = 0;
        int swaps = 0;
        int ambiguous = 0;
        int unmatched = 0;
        int outOfBand = 0;

        /** Points where the true peak was deliberately absent. */
        int absentExpected = 0;
        int absentHandled = 0;   /**< Not matched to anything: the right answer. */
        int absentSwapped = 0;
    };

    [[nodiscard]] Score score (const SynthSweep& sweep,
                               const std::vector<Component>& components,
                               const std::vector<SizeTrajectory>& tracks,
                               size_t anchorFrame)
    {
        Score s;

        for (const auto& t : tracks)
        {
            // Trajectory ids run in the order the anchors were given, which is
            // the order of the tracked components.
            const int owner = t.id;

            const Component* component = nullptr;

            for (const auto& c : components)
                if (c.owner == owner)
                    component = &c;

            if (component == nullptr)
                continue;

            for (size_t f = 0; f < t.points.size(); ++f)
            {
                if (f == anchorFrame)
                    continue;   // seeded, not a result

                const auto& point = t.points[f];
                const double trueHz = component->frequencyAt (sweep.frames[f].sizeScale);
                const bool inBand = trueHz >= bandLowHz && trueHz <= bandHighHz;
                const bool deleted = component->absentAtFrame >= 0
                                  && static_cast<size_t> (component->absentAtFrame) == f;

                if (deleted && inBand)
                {
                    ++s.absentExpected;

                    if (point.status == SizePointStatus::matched)
                        ++s.absentSwapped;
                    else
                        ++s.absentHandled;

                    continue;
                }

                if (! inBand)
                {
                    if (point.status == SizePointStatus::outOfBand)
                        ++s.outOfBand;

                    continue;
                }

                ++s.expected;

                switch (point.status)
                {
                    case SizePointStatus::matched:
                    {
                        const int found = sweep.owners[f][static_cast<size_t> (point.peakIndex)];

                        if (found == owner) ++s.correct;
                        else                ++s.swaps;

                        break;
                    }

                    case SizePointStatus::ambiguous: ++s.ambiguous; break;
                    case SizePointStatus::unmatched: ++s.unmatched; break;
                    case SizePointStatus::outOfBand: ++s.outOfBand; break;
                }
            }
        }

        return s;
    }

    void report (const std::string& name, const Score& s)
    {
        const double rate = s.expected > 0
                          ? 100.0 * static_cast<double> (s.correct)
                            / static_cast<double> (s.expected)
                          : 0.0;

        std::ostringstream o;

        o << "  " << padR (name, 34)
          << padL (std::to_string (s.correct) + "/" + std::to_string (s.expected), 9)
          << padL (fx (rate, 1) + "%", 8)
          << "   swaps " << padL (std::to_string (s.swaps), 3)
          << "   ambig " << padL (std::to_string (s.ambiguous), 3)
          << "   gap " << padL (std::to_string (s.unmatched), 3)
          << "   exit " << padL (std::to_string (s.outOfBand), 3);

        info (o.str());
    }

    // -----------------------------------------------------------------------
    //  The old tracker, driven exactly as the sweep drives it
    // -----------------------------------------------------------------------

    /**
        A replica of ModalSweep's `trackInto`, on the same frames.

        Not a caricature: the same `matchPeaks`, the same two-point velocity
        prediction, the same walk outwards from the reference index, and the
        local spacing set from the frame's mean modal spacing the same way. It
        is here so that the synthetic suite can be shown to be discriminating -
        a suite the broken tracker also passes would prove nothing about the
        new one.
    */
    [[nodiscard]] std::vector<SizeTrajectory> trackLegacy (const std::vector<SizeFrame>& frames,
                                                           size_t anchorFrame,
                                                           const std::vector<int>& anchorPeaks,
                                                           double localSpacingHz)
    {
        MatchOptions options;
        options.localSpacingHz = std::max (1.0, localSpacingHz);

        const auto toModal = [] (const SizePeak& p)
        {
            ModalPeak m;
            m.frequencyHz = p.frequencyHz;
            m.prominenceDb = p.prominenceDb;
            m.bandwidthHz = p.bandwidthHz;
            m.q = p.qFactor;
            return m;
        };

        const size_t count = anchorPeaks.size();

        std::vector<SizeTrajectory> out (count);
        std::vector<ModalPeak> anchorModal;

        for (size_t i = 0; i < count; ++i)
        {
            const auto& peak = frames[anchorFrame].peaks[static_cast<size_t> (anchorPeaks[i])];

            out[i].id = static_cast<int> (i);
            out[i].anchorFrame = static_cast<int> (anchorFrame);
            out[i].anchorHz = peak.frequencyHz;
            out[i].points.resize (frames.size());

            for (size_t f = 0; f < frames.size(); ++f)
            {
                out[i].points[f].frameIndex = static_cast<int> (f);
                out[i].points[f].sizeControl = frames[f].sizeControl;
                out[i].points[f].sizeScale = frames[f].sizeScale;
                out[i].points[f].status = SizePointStatus::unmatched;
                out[i].points[f].peakIndex = -1;
                out[i].points[f].frequencyHz = std::numeric_limits<double>::quiet_NaN();
            }

            auto& seed = out[i].points[anchorFrame];

            seed.status = SizePointStatus::matched;
            seed.peakIndex = anchorPeaks[i];
            seed.frequencyHz = peak.frequencyHz;
            seed.modalCoordinate = peak.frequencyHz * frames[anchorFrame].sizeScale;

            anchorModal.push_back (toModal (peak));
        }

        const auto walk = [&] (int step)
        {
            std::vector<ModalPeak> current = anchorModal;
            std::vector<double> last (count), previous (count);

            for (size_t i = 0; i < count; ++i)
                last[i] = previous[i] = anchorModal[i].frequencyHz;

            for (int f = static_cast<int> (anchorFrame) + step;
                 f >= 0 && f < static_cast<int> (frames.size());
                 f += step)
            {
                std::vector<ModalPeak> to;

                for (const auto& p : frames[static_cast<size_t> (f)].peaks)
                    to.push_back (toModal (p));

                std::vector<double> predicted (count);

                for (size_t i = 0; i < count; ++i)
                    predicted[i] = 2.0 * last[i] - previous[i];

                const auto match = matchPeaks (current, to, options, &predicted);

                for (size_t i = 0; i < count; ++i)
                {
                    const int j = match.assignment[i];

                    if (j < 0)
                        continue;

                    auto& point = out[i].points[static_cast<size_t> (f)];

                    point.status = SizePointStatus::matched;
                    point.peakIndex = j;
                    point.frequencyHz = to[static_cast<size_t> (j)].frequencyHz;
                    point.prominenceDb = to[static_cast<size_t> (j)].prominenceDb;
                    point.modalCoordinate = point.frequencyHz
                                          * frames[static_cast<size_t> (f)].sizeScale;

                    previous[i] = last[i];
                    last[i] = point.frequencyHz;
                    current[i] = to[static_cast<size_t> (j)];
                }
            }
        };

        walk (+1);
        walk (-1);

        return out;
    }

    // -----------------------------------------------------------------------

    /** Indices of the tracked components' peaks at the anchor frame. */
    [[nodiscard]] std::vector<int> anchorIndicesFor (const SynthSweep& sweep,
                                                     size_t anchorFrame, int ownerCount)
    {
        std::vector<int> indices;

        for (int owner = 0; owner < ownerCount; ++owner)
        {
            int found = -1;

            for (size_t i = 0; i < sweep.owners[anchorFrame].size(); ++i)
                if (sweep.owners[anchorFrame][i] == owner)
                    found = static_cast<int> (i);

            indices.push_back (found);
        }

        return indices;
    }
}

// ===========================================================================

void runSizeTrackTests()
{
    SizeTrackOptions options;   // defaults throughout: nothing is tuned per suite

    // =======================================================================
    beginSuite ("Phase 6 / size A: exact inverse scaling");
    // =======================================================================
    {
        // The base case, and the one the old tracker cannot do. Under uniform
        // scaling f = q / scale, so between adjacent Size points every mode
        // moves by a factor of 2^(1/3) - hundreds of local spacings at the top
        // of the band. Identity has to survive that.
        std::vector<Component> parts;

        const double centres[] = { 120.0, 400.0, 900.0, 1600.0, 2400.0 };
        int owner = 0;

        for (double centre : centres)
        {
            Component c;
            c.referenceHz = centre;
            c.exponent = -1.0;
            c.prominenceAtLowSize = 35.0;
            c.prominenceAtHighSize = 30.0;
            c.owner = owner++;
            parts.push_back (c);

            addBed (parts, centre, 4.0, 12, 22.0);
        }

        const auto sweep = build (parts, 4.0);
        const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, owner);

        const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
        const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

        info ("");
        info ("  five inverse-scale modes, 4 Hz bed of 24 neighbours each");
        info ("");
        report ("new tracker", s);

        const auto legacy = trackLegacy (sweep.frames, defaultAnchorFrame, anchors, 4.0);
        const auto ls = score (sweep, parts, legacy, defaultAnchorFrame);

        report ("old tracker, same frames", ls);
        info ("");

        checkCount (s.swaps, 0, "A: no identity swaps");
        check (s.correct == s.expected, "A: every in-band point recovered");

        // Not a requirement on the new tracker - a statement about the suite.
        // If the old tracker passed this, the suite would not be measuring
        // anything.
        check (ls.correct < s.correct,
               "A: the suite separates the two trackers");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size B: frequency range");
    // =======================================================================
    {
        // Expected absolute movement grows with frequency: one Size step is
        // 26 Hz at 100 Hz and 2.4 kHz at 11.5 kHz. Raw Hz displacement is
        // therefore useless as a gate, and this is where that shows.
        struct Range { const char* name; double centre; };

        const Range ranges[] = { { "~100 Hz",  100.0 },
                                 { "~1 kHz",  1000.0 },
                                 { "~5 kHz",  5000.0 },
                                 { "~10 kHz", 10000.0 } };

        // Stated at the anchor, not at scale 1. A centre given at scale 1 puts
        // the 10 kHz case out of band at the anchor, which leaves nothing to
        // seed and turns the hardest range into an empty suite that passes.
        const double anchorScale = sizeScaleFor (sizeControls[defaultAnchorFrame]);

        info ("");
        info ("  centres are stated at the anchor Size; points whose true");
        info ("  frequency leaves 40 Hz - 12 kHz are boundary exits, not misses");
        info ("");

        int totalSwaps = 0, totalCorrect = 0, totalExpected = 0;

        for (const auto& range : ranges)
        {
            std::vector<Component> parts;

            const double reference = range.centre * anchorScale;

            Component c;
            c.referenceHz = reference;
            c.exponent = -1.0;
            c.prominenceAtLowSize = 34.0;
            c.prominenceAtHighSize = 28.0;
            c.owner = 0;
            parts.push_back (c);

            addBed (parts, reference, 4.0, 20, 22.0);

            const auto sweep = build (parts, 4.0);
            const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 1);

            const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
            const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

            report (range.name, s);

            totalSwaps += s.swaps;
            totalCorrect += s.correct;
            totalExpected += s.expected;
        }

        info ("");

        checkCount (totalSwaps, 0, "B: no identity swaps at any frequency");
        checkCount (totalCorrect, totalExpected,
                    "B: every in-band point recovered at every frequency");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size C: dense distractors");
    // =======================================================================
    {
        // The measured median gap between detected peaks in the real sweep is
        // about 4 Hz. This runs tighter than that, down to where the prediction
        // and its neighbour are separated by less than the analysis floor, and
        // asks the tracker to either be right or say it cannot tell.
        const double spacings[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };

        info ("");
        info ("  bed spacing at scale 1, tightening past the 0.3834 Hz floor");
        info ("");

        int swapsTotal = 0;

        for (double spacing : spacings)
        {
            std::vector<Component> parts;

            const double centres[] = { 300.0, 1200.0, 4000.0 };
            int owner = 0;

            for (double centre : centres)
            {
                Component c;
                c.referenceHz = centre;
                c.exponent = -1.0;
                c.prominenceAtLowSize = 33.0;
                c.prominenceAtHighSize = 31.0;
                c.owner = owner++;
                parts.push_back (c);

                addBed (parts, centre, spacing, 10, 26.0);
            }

            const auto sweep = build (parts, spacing);
            const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, owner);

            const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
            const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

            report ("bed " + fx (spacing, 2) + " Hz", s);

            swapsTotal += s.swaps;
        }

        info ("");

        checkCount (swapsTotal, 0, "C: no identity swaps at any bed density");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size D: crossings and near-crossings");
    // =======================================================================
    {
        // Two shapes of crossing. A moving mode passing a stationary feature is
        // the one the tank actually produces - a fixed delay or a filter does
        // not move with Size while the loop modes sweep past it. A pair of
        // tracked modes converging is the harder one, and the honest outcome
        // near the meeting point is ambiguous rather than a guess.
        info ("");

        {
            std::vector<Component> parts;

            Component moving;
            moving.referenceHz = 800.0;
            moving.exponent = -1.0;
            moving.prominenceAtLowSize = 36.0;
            moving.prominenceAtHighSize = 24.0;
            moving.owner = 0;
            parts.push_back (moving);

            // Stationary, and crossing the mode's path between the anchor and
            // the top of the axis. Its prominence rises as the mode's falls, so
            // strength cannot be used to tell them apart at the crossing.
            Component fixed;
            fixed.referenceHz = 800.0 / sizeScaleFor (0.7778);
            fixed.exponent = 0.0;
            fixed.prominenceAtLowSize = 24.0;
            fixed.prominenceAtHighSize = 36.0;
            fixed.owner = -1;
            parts.push_back (fixed);

            addBed (parts, 800.0, 6.0, 8, 20.0);

            const auto sweep = build (parts, 6.0);
            const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 1);

            const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
            const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

            report ("moving mode past a fixed peak", s);

            checkCount (s.swaps, 0, "D: the stationary peak is never taken for the mode");
        }

        {
            std::vector<Component> parts;

            // Converging pair, low enough in frequency that the slower one's
            // drift stays inside a spacing per step and the tracker has a real
            // chance rather than an impossible one.
            Component a;
            a.referenceHz = 150.0;
            a.exponent = -1.0;
            a.prominenceAtLowSize = 30.0;
            a.prominenceAtHighSize = 34.0;
            a.owner = 0;
            parts.push_back (a);

            Component b;
            b.referenceHz = 150.0 * std::pow (sizeScaleFor (0.6667), -0.06) + 0.0;
            b.exponent = -0.94;
            b.prominenceAtLowSize = 34.0;
            b.prominenceAtHighSize = 30.0;
            b.owner = 1;
            parts.push_back (b);

            addBed (parts, 150.0, 5.0, 6, 20.0);

            const auto sweep = build (parts, 5.0);
            const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 2);

            const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
            const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

            report ("two tracked modes converging", s);

            checkCount (s.swaps, 0, "D: converging modes do not exchange identity");
            check (s.correct + s.ambiguous + s.unmatched == s.expected,
                   "D: every point is either right or refused");
        }

        info ("");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size E: missing peak");
    // =======================================================================
    {
        // A mode that is not detected at one Size. The wrong answer - and the
        // one a nearest-neighbour tracker always gives - is the neighbour. The
        // right answer is a gap, followed by re-acquisition of the same mode.
        info ("");

        const int dropAt[] = { 2, 8 };

        int handled = 0, swapped = 0, expectedAbsent = 0;
        int afterGapCorrect = 0, afterGapTotal = 0;

        for (int drop : dropAt)
        {
            std::vector<Component> parts;

            Component c;
            c.referenceHz = 1000.0;
            c.exponent = -1.0;
            c.prominenceAtLowSize = 32.0;
            c.prominenceAtHighSize = 32.0;
            c.owner = 0;
            c.absentAtFrame = drop;
            parts.push_back (c);

            addBed (parts, 1000.0, 4.0, 10, 27.0);

            const auto sweep = build (parts, 4.0);
            const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 1);

            const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
            const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

            report ("mode absent at frame " + std::to_string (drop), s);

            handled += s.absentHandled;
            swapped += s.absentSwapped;
            expectedAbsent += s.absentExpected;

            // Re-acquisition: the frame past the hole, walking away from the
            // anchor.
            const int step = drop > static_cast<int> (defaultAnchorFrame) ? +1 : -1;
            const int next = drop + step;

            if (next >= 0 && next < static_cast<int> (frameCount))
            {
                ++afterGapTotal;

                const auto& point = tracks[0].points[static_cast<size_t> (next)];

                if (point.status == SizePointStatus::matched
                    && sweep.owners[static_cast<size_t> (next)]
                                   [static_cast<size_t> (point.peakIndex)] == 0)
                    ++afterGapCorrect;
            }
        }

        info ("");
        info ("  absent points: " + std::to_string (handled) + " reported as a gap, "
              + std::to_string (swapped) + " filled with a neighbour, of "
              + std::to_string (expectedAbsent));
        info ("");

        checkCount (swapped, 0, "E: a missing mode is never replaced by its neighbour");
        checkCount (handled, expectedAbsent, "E: every absence is reported");
        checkCount (afterGapCorrect, afterGapTotal, "E: the mode is re-acquired after the gap");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size F: prominence reordering");
    // =======================================================================
    {
        // Peak strength ranking reverses along the axis. A tracker that leans
        // on prominence - or on the order the detector happens to return -
        // swaps here. Prominence carries 0.01 per dB against a residual
        // measured in local spacings, capped at a tenth of a spacing, so no
        // reordering however large can outweigh position.
        std::vector<Component> parts;

        Component a;
        a.referenceHz = 700.0;
        a.exponent = -1.0;
        a.prominenceAtLowSize = 45.0;
        a.prominenceAtHighSize = 15.0;
        a.owner = 0;
        parts.push_back (a);

        Component b;
        b.referenceHz = 706.0;
        b.exponent = -1.0;
        b.prominenceAtLowSize = 15.0;
        b.prominenceAtHighSize = 45.0;
        b.owner = 1;
        parts.push_back (b);

        Component c;
        c.referenceHz = 2500.0;
        c.exponent = -1.0;
        c.prominenceAtLowSize = 20.0;
        c.prominenceAtHighSize = 40.0;
        c.owner = 2;
        parts.push_back (c);

        Component d;
        d.referenceHz = 2506.0;
        d.exponent = -1.0;
        d.prominenceAtLowSize = 40.0;
        d.prominenceAtHighSize = 20.0;
        d.owner = 3;
        parts.push_back (d);

        addBed (parts, 700.0, 4.0, 8, 25.0);
        addBed (parts, 2500.0, 4.0, 8, 25.0);

        const auto sweep = build (parts, 4.0);
        const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 4);

        const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
        const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

        info ("");
        info ("  two pairs, each reversing its strength ranking across the axis");
        info ("");
        report ("prominence reorder", s);
        info ("");

        checkCount (s.swaps, 0, "F: no identity swaps under prominence reordering");
        checkCount (s.correct, s.expected, "F: every point recovered");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size: what the gate can and cannot follow");
    // =======================================================================
    {
        // Not a pass/fail suite. The gate admits a residual of up to three
        // local spacings per step after the inverse-scale prediction, which
        // bounds how far from f = q/scale a mode can be and still be tracked.
        // That bound has to be stated, because "every surviving mode is
        // inverse-scale consistent" would otherwise be a property of this
        // number rather than a finding about the tank.
        info ("");
        info ("  exponent deviation from -1, with a 4 Hz bed at the anchor.");
        info ("  'clean' is the largest deviation recovered at every point;");
        info ("  'no swap' is the largest at which nothing was mis-identified,");
        info ("  gaps and ambiguity allowed. Between the two the tracker is");
        info ("  refusing rather than guessing, which is the intended failure.");
        info ("");
        info ("  " + padR ("centre", 12) + padL ("clean", 9) + padL ("no swap", 10)
              + padL ("q drift over axis", 20) + padL ("in spacings", 14));

        const double centres[] = { 100.0, 300.0, 1000.0, 3000.0, 8000.0 };
        const double anchorScale = sizeScaleFor (sizeControls[defaultAnchorFrame]);

        for (double centre : centres)
        {
            const double reference = centre * anchorScale;

            double clean = 0.0, noSwap = 0.0;
            bool cleanOpen = true, noSwapOpen = true;

            for (double deviation = 0.002; deviation <= 0.40 && (cleanOpen || noSwapOpen);
                 deviation += 0.002)
            {
                std::vector<Component> parts;

                Component c;
                c.referenceHz = reference;
                c.exponent = -1.0 + deviation;
                c.prominenceAtLowSize = 32.0;
                c.prominenceAtHighSize = 32.0;
                c.owner = 0;
                parts.push_back (c);

                addBed (parts, reference, 4.0, 14, 25.0);

                const auto sweep = build (parts, 4.0);
                const auto anchors = anchorIndicesFor (sweep, defaultAnchorFrame, 1);

                const auto tracks = trackSize (sweep.frames, defaultAnchorFrame, anchors, options);
                const auto s = score (sweep, parts, tracks, defaultAnchorFrame);

                if (cleanOpen)
                {
                    if (s.swaps == 0 && s.correct == s.expected && s.expected > 0) clean = deviation;
                    else                                                           cleanOpen = false;
                }

                if (noSwapOpen)
                {
                    if (s.swaps == 0 && s.expected > 0) noSwap = deviation;
                    else                                noSwapOpen = false;
                }
            }

            // How far the modal coordinate wanders across the whole axis at the
            // 'clean' deviation, and what that is worth in local spacings at the
            // small-Size end - which is where a drifting mode meets its
            // neighbour and identity genuinely stops being decidable.
            const double lowScale = sizeScaleFor (sizeControls[0]);
            const double highScale = sizeScaleFor (sizeControls[frameCount - 1]);
            const double p = -1.0 + clean;
            const double driftQ = std::abs (reference * std::pow (lowScale, p + 1.0)
                                          - reference * std::pow (highScale, p + 1.0));
            const double driftHz = driftQ / lowScale;

            info ("  " + padR (fx (centre, 0) + " Hz", 12)
                  + padL (fx (clean, 3), 9)
                  + padL (fx (noSwap, 3), 10)
                  + padL (fx (driftHz, 2) + " Hz", 20)
                  + padL (fx (driftHz / (4.0 / lowScale), 2), 14));
        }

        info ("");
        info ("  Beyond 'no swap' a departing mode is reported as a gap or as");
        info ("  ambiguous, never as a tracked mode with a different exponent.");
        info ("  The non-inverse-scale candidate category can therefore only be");
        info ("  populated inside this envelope, and an empty category is a");
        info ("  statement about the envelope as much as about the tank.");
        info ("");

        check (true, "envelope reported");
    }
}

// ===========================================================================
//  Integration: the sweep's Size path
// ===========================================================================

/*
    These do not test "a q-space tracker". They test trackSizeAxis, which is the
    function ModalSweep calls and the only Size tracking left in the codebase -
    the local walk that used to live in ModalSweep is gone, and so is its copy
    of the log-log fit. If anybody rewires the Hz-over-local-spacing tracker
    back into that path, the high-frequency suite below cannot pass: it needs
    identities carried across steps of two and a half kHz, which the old cost
    function gates out as impossible before it ever looks at them.

    The last one keeps the off-grid finding that prompted the distinction
    between "tracked" and "peak confirmed", with the frequencies and levels the
    render actually produced.
*/
void runSizeIntegrationTests()
{
    const SizeTrackOptions options;

    // The sweep's reference sits at the shipping default of 0.60, which is not
    // one of the Size points. Reproduced here because that offset anchor is
    // part of what the integration has to get right.
    constexpr double referenceControl = 0.60;

    struct Built
    {
        SynthSweep sweep;
        SizeFrame reference;
        std::vector<int> anchors;
        std::vector<Component> parts;
    };

    // Builds a sweep plus the off-grid reference frame the sweep anchors on.
    const auto buildWithReference = [&] (const std::vector<Component>& parts, double spacing)
    {
        Built b;

        b.parts = parts;
        b.sweep = build (parts, spacing);

        const double scale = sizeScaleFor (referenceControl);

        b.reference.sizeControl = referenceControl;
        b.reference.sizeScale = scale;
        b.reference.meanModalSpacingHz = spacing / scale;

        struct Entry { double hz, prominence; int owner; };

        std::vector<Entry> entries;

        for (const auto& c : parts)
        {
            const double hz = c.frequencyAt (scale);

            if (hz < bandLowHz || hz > bandHighHz)
                continue;

            // Midway between the two nearest frames in prominence, which is
            // where a reference at 0.60 actually sits.
            entries.push_back ({ hz, c.prominenceAt (defaultAnchorFrame), c.owner });
        }

        std::sort (entries.begin(), entries.end(),
                   [] (const Entry& a, const Entry& b) { return a.hz < b.hz; });

        std::vector<int> owners;

        for (const auto& e : entries)
        {
            SizePeak p;

            p.frequencyHz = e.hz;
            p.prominenceDb = e.prominence;

            b.reference.peaks.push_back (p);
            owners.push_back (e.owner);
        }

        int ownerCount = 0;

        for (const auto& c : parts)
            ownerCount = std::max (ownerCount, c.owner + 1);

        for (int owner = 0; owner < ownerCount; ++owner)
            for (size_t i = 0; i < owners.size(); ++i)
                if (owners[i] == owner)
                    b.anchors.push_back (static_cast<int> (i));

        return b;
    };

    // =======================================================================
    beginSuite ("Phase 6 / size integration: exact inverse scaling through trackSizeAxis");
    // =======================================================================
    {
        std::vector<Component> parts;

        const double centres[] = { 140.0, 520.0, 1100.0, 2200.0 };
        int owner = 0;

        for (double centre : centres)
        {
            Component c;
            c.referenceHz = centre;
            c.exponent = -1.0;
            c.prominenceAtLowSize = 34.0;
            c.prominenceAtHighSize = 29.0;
            c.owner = owner++;
            parts.push_back (c);

            addBed (parts, centre, 4.0, 12, 22.0);
        }

        const auto b = buildWithReference (parts, 4.0);

        const auto axis = trackSizeAxis (b.sweep.frames, &b.reference, b.anchors,
                                         referenceControl, options);

        const auto s = score (b.sweep, b.parts, axis.trajectories, frameCount);

        info ("");
        info ("  reference at Size 0.60, which is not a sweep point");
        info ("");
        report ("trackSizeAxis", s);
        info ("");

        checkCount (static_cast<int> (axis.trajectories.size()), owner,
                    "integration: one trajectory per reference mode");
        checkCount (s.swaps, 0, "integration: no identity swaps");
        checkCount (s.correct, s.expected, "integration: every in-band point recovered");

        // Every point is a result here: the anchor was the reference frame,
        // which is not one of the sweep points, so nothing is seeded for free.
        checkCount (s.expected, static_cast<int> (frameCount) * owner,
                    "integration: no sweep point is seeded rather than tracked");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size integration: large absolute movement");
    // =======================================================================
    {
        // The discriminating case. At the anchor these sit at 8 and 11 kHz, so
        // one Size step moves them by 1.8 and 2.5 kHz - some hundreds of local
        // spacings. A tracker that gates on raw displacement over the local
        // spacing cannot match any of it.
        const double anchorScale = sizeScaleFor (referenceControl);

        std::vector<Component> parts;

        const double centres[] = { 8000.0, 11000.0 };
        int owner = 0;

        for (double centre : centres)
        {
            Component c;
            c.referenceHz = centre * anchorScale;
            c.exponent = -1.0;
            c.prominenceAtLowSize = 33.0;
            c.prominenceAtHighSize = 30.0;
            c.owner = owner++;
            parts.push_back (c);

            addBed (parts, centre * anchorScale, 4.0, 16, 23.0);
        }

        const auto b = buildWithReference (parts, 4.0);

        const auto axis = trackSizeAxis (b.sweep.frames, &b.reference, b.anchors,
                                         referenceControl, options);

        const auto s = score (b.sweep, b.parts, axis.trajectories, frameCount);

        const double step = std::pow (2.0, 1.0 / 3.0);
        const double movement = 11000.0 - 11000.0 / step;

        info ("");
        info ("  one Size step at 11 kHz moves the mode " + fx (movement, 0) + " Hz,");
        info ("  about " + fx (movement / 4.0, 0) + " local spacings");
        info ("");
        report ("trackSizeAxis", s);

        // The same fixture through the old cost function. Not a requirement on
        // the new tracker - a demonstration that this suite is the thing that
        // would fail if the old path came back.
        const auto legacy = trackLegacy (b.sweep.frames, defaultAnchorFrame,
                                         anchorIndicesFor (b.sweep, defaultAnchorFrame, owner),
                                         4.0);

        const auto ls = score (b.sweep, b.parts, legacy, defaultAnchorFrame);

        report ("old tracker, same fixture", ls);
        info ("");

        checkCount (s.swaps, 0, "integration: no swaps at 8 and 11 kHz");
        checkCount (s.correct, s.expected, "integration: every in-band point recovered");
        check (s.correct > 0, "integration: the fixture produced in-band points");
        check (ls.correct == 0,
               "integration: the old tracker recovers nothing here, so this suite catches it");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size integration: anchor tracks in both directions");
    // =======================================================================
    {
        std::vector<Component> parts;

        Component c;
        c.referenceHz = 900.0;
        c.exponent = -1.0;
        c.prominenceAtLowSize = 32.0;
        c.prominenceAtHighSize = 32.0;
        c.owner = 0;
        parts.push_back (c);

        addBed (parts, 900.0, 4.0, 12, 24.0);

        const auto b = buildWithReference (parts, 4.0);

        const auto axis = trackSizeAxis (b.sweep.frames, &b.reference, b.anchors,
                                         referenceControl, options);

        check (! axis.trajectories.empty(), "integration: trajectory produced");

        int below = 0, above = 0;

        if (! axis.trajectories.empty())
        {
            const auto& t = axis.trajectories.front();

            for (size_t f = 0; f < t.points.size(); ++f)
            {
                if (t.points[f].status != SizePointStatus::matched)
                    continue;

                if (f < axis.anchorFrame) ++below;
                if (f > axis.anchorFrame) ++above;
            }
        }

        info ("");
        info ("  anchor frame " + std::to_string (axis.anchorFrame)
              + ", matched " + std::to_string (below) + " below and "
              + std::to_string (above) + " above");
        info ("");

        check (below > 0 && above > 0,
               "integration: both chains ran from the anchor");
        check (! axis.forwardBackward.empty() && axis.forwardBackward.front().comparable > 0,
               "integration: the path-independence check ran");
        check (axis.forwardBackward.front().consistent(),
               "integration: forward and backward agree");
        check (axis.anchorUp.front().consistent() && axis.anchorDown.front().consistent(),
               "integration: both directions agree with the anchor chain");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size integration: prominence reorder and a gap");
    // =======================================================================
    {
        std::vector<Component> parts;

        Component a;
        a.referenceHz = 640.0;
        a.exponent = -1.0;
        a.prominenceAtLowSize = 44.0;
        a.prominenceAtHighSize = 16.0;
        a.owner = 0;
        parts.push_back (a);

        Component bb;
        bb.referenceHz = 646.0;
        bb.exponent = -1.0;
        bb.prominenceAtLowSize = 16.0;
        bb.prominenceAtHighSize = 44.0;
        bb.owner = 1;
        parts.push_back (bb);

        // The same mode, missing at one Size point.
        Component g;
        g.referenceHz = 2100.0;
        g.exponent = -1.0;
        g.prominenceAtLowSize = 31.0;
        g.prominenceAtHighSize = 31.0;
        g.owner = 2;
        g.absentAtFrame = 7;
        parts.push_back (g);

        addBed (parts, 640.0, 4.0, 8, 24.0);
        addBed (parts, 2100.0, 4.0, 8, 24.0);

        const auto b = buildWithReference (parts, 4.0);

        const auto axis = trackSizeAxis (b.sweep.frames, &b.reference, b.anchors,
                                         referenceControl, options);

        const auto s = score (b.sweep, b.parts, axis.trajectories, frameCount);

        info ("");
        report ("reorder + gap", s);
        info ("  absence at frame 7: " + std::to_string (s.absentHandled) + " reported, "
              + std::to_string (s.absentSwapped) + " filled with a neighbour");
        info ("");

        checkCount (s.swaps, 0, "integration: reordering does not swap identity");
        checkCount (s.absentSwapped, 0, "integration: the gap is not filled with a neighbour");
        checkCount (s.absentHandled, s.absentExpected, "integration: the gap is reported");
    }

    // =======================================================================
    beginSuite ("Phase 6 / size: tracked is not the same as peak confirmed");
    // =======================================================================
    {
        // The off-grid validation case, with the numbers the render produced.
        // Mode 14's coordinate is 1517.2, so at scale 1.41426 it is predicted at
        // 1072.9 Hz. The render has peaks either side of that and nothing at it:
        // the mode had dropped under the detector's 10 dB floor.
        const double predicted = 1517.21 / 1.41426;

        SizeFrame frame;

        frame.sizeControl = 0.83335;
        frame.sizeScale = 1.41426;
        frame.meanModalSpacingHz = 1.820;

        const double measured[][3] = { { 1058.585, 11.98, 0.916 },
                                       { 1060.621, 16.09, 0.916 },
                                       { 1063.948, 15.65, 0.916 },
                                       { 1068.540, 10.68, 0.916 },
                                       { 1078.560, 17.51, 0.916 },
                                       { 1084.036, 13.44, 0.916 },
                                       { 1087.061, 14.21, 0.732 },
                                       { 1090.315, 19.50, 0.916 } };

        for (const auto& m : measured)
        {
            SizePeak p;

            p.frequencyHz = m[0];
            p.prominenceDb = m[1];
            p.bandwidthHz = m[2];
            p.widthResolved = true;

            frame.peaks.push_back (p);
        }

        const double spacing = localPeakSpacingHz (frame, predicted, options.spacingWindowHz,
                                                   options.spacingFloorHz);

        double nearest = 1.0e18;

        for (const auto& p : frame.peaks)
            nearest = std::min (nearest, std::abs (p.frequencyHz - predicted));

        info ("");
        info ("  predicted " + fx (predicted, 2) + " Hz, nearest peak "
              + fx (nearest, 2) + " Hz away, local spacing " + fx (spacing, 2) + " Hz"
              + " (" + fx (nearest / spacing, 2) + " spacings)");
        info ("");

        // First half: the tracker is not fooled. A candidate this far from the
        // prediction costs more than the dummy column, so the solver refuses it
        // and the point is a gap.
        {
            SizeFrame before;

            before.sizeControl = 0.7778;
            before.sizeScale = 1.259979;
            before.meanModalSpacingHz = 2.043;

            SizePeak p;
            p.frequencyHz = 1517.21 / before.sizeScale;
            p.prominenceDb = 16.48;
            p.bandwidthHz = 0.916;
            before.peaks.push_back (p);

            for (int k = -6; k <= 6; ++k)
            {
                if (k == 0) continue;

                SizePeak n;
                n.frequencyHz = p.frequencyHz + k * 3.5;
                n.prominenceDb = 22.0;
                n.bandwidthHz = 0.916;
                before.peaks.push_back (n);
            }

            std::sort (before.peaks.begin(), before.peaks.end(),
                       [] (const SizePeak& x, const SizePeak& y)
                       { return x.frequencyHz < y.frequencyHz; });

            int seed = 0;

            for (size_t i = 0; i < before.peaks.size(); ++i)
                if (std::abs (before.peaks[i].frequencyHz - p.frequencyHz) < 1.0e-9)
                    seed = static_cast<int> (i);

            const auto tracks = trackSize ({ before, frame }, 0, { seed }, options);

            check (! tracks.empty(), "evidence: fixture tracked");

            if (! tracks.empty())
                check (tracks.front().points[1].status != SizePointStatus::matched,
                       "evidence: the tracker refuses the peak, it does not take the nearest");
        }

        // Second half: where an assignment does land beyond a spacing - which
        // happens when the prediction reaches across a gap - the reporting layer
        // has to say so rather than let the fit absorb it.
        {
            SizeTrajectory t;

            t.points.resize (2);

            t.points[0].status = SizePointStatus::matched;
            t.points[0].frequencyHz = 1204.20;
            t.points[0].prominenceDb = 16.48;
            t.points[0].bandwidthHz = 0.916;
            t.points[0].residualSpacings = 0.0;

            t.points[1].status = SizePointStatus::matched;
            t.points[1].frequencyHz = 1068.540;
            t.points[1].prominenceDb = 10.68;
            t.points[1].bandwidthHz = 0.916;
            t.points[1].residualSpacings = nearest / spacing;

            const auto e = evidenceFor (t);

            info ("  evidence at that point: " + std::string (describe (e.points[1])));
            info ("");

            check (e.points[1] == PeakEvidence::suspect,
                   "evidence: beyond one local spacing is suspect, not confirmed");
            check (e.worst == PeakEvidence::suspect,
                   "evidence: the trajectory carries the worst status it reached");
            checkCount (e.confirmedPoints, 1, "evidence: the sound point is still confirmed");
        }

        // And a peak just above the detector's floor is weak evidence even when
        // it sits exactly where predicted.
        {
            SizeTrajectory t;

            t.points.resize (2);

            t.points[0].status = SizePointStatus::matched;
            t.points[0].frequencyHz = 1204.20;
            t.points[0].prominenceDb = 30.0;
            t.points[0].bandwidthHz = 0.916;
            t.points[0].residualSpacings = 0.0;

            t.points[1].status = SizePointStatus::matched;
            t.points[1].frequencyHz = 1072.90;
            t.points[1].prominenceDb = 10.5;
            t.points[1].bandwidthHz = 0.916;
            t.points[1].residualSpacings = 0.01;

            const auto e = evidenceFor (t);

            check (e.points[1] == PeakEvidence::weak,
                   "evidence: a peak at the detector's floor is weak, however well placed");
        }
    }
}

} // namespace avtest
