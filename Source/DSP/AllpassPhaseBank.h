#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "AllpassModulationTuning.h"
#include "Lfo.h"
#include "SmoothedParameter.h"
#include "TimeVaryingAllpass.h"

namespace av::dsp
{

/**
    One short time-varying allpass chain per FDN delay line.

    Sits inside the feedback loop, between the delay read and the decay gain:

        integer delay read -> allpass phase section -> [damping slot] -> gain

    The FDN's own delay lengths are untouched. What moves is the group delay of
    a section short enough that it cannot act as a diffuser, which shifts the
    tank's modes without resampling its delay lines.

    BYPASS IS REAL
    --------------
    At Depth 0 the sections are not merely unmodulated — an unmodulated section
    is still a delay, and leaving it in the loop would change the tank. The bank
    crossfades each line between its input and its section output, so Depth 0
    settles to a bit-identical passthrough and the tank reverts exactly to what
    PHASE 4 measured. The crossfade is a dry/wet blend of an allpass, which is a
    comb filter in the middle, and that is why it is used only for engaging and
    disengaging rather than as the depth control itself.

    LANES
    -----
    Each line has its own LFO: its own rate factor, phase offset, seed and depth
    scale, so no two lines swing together. Rate changes never reset a phase.

    Real-time safe after prepare(). Everything is fixed size; the mode is an
    enum rather than a virtual call, so there is no allocation and no indirect
    dispatch on the audio thread.
*/
template <int N = allpassMod::maxLines>
class AllpassPhaseBank
{
public:
    using Mode = TimeVaryingAllpass::Mode;

    static constexpr int maxSections = allpassMod::maxSectionsPerLine;

    // -----------------------------------------------------------------------
    //  Setup — allocates. Never call from the audio thread.
    // -----------------------------------------------------------------------

    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (int line = 0; line < N; ++line)
        {
            for (int section = 0; section < maxSections; ++section)
            {
                auto& s = sections[index (line, section)];

                s.prepare (currentSampleRate, allpassMod::maxSectionDelayMs);
                s.setMode (mode);
                s.setDelayMilliseconds (section == 0
                                            ? allpassMod::sectionDelaysMs[line]
                                            : allpassMod::secondSectionDelaysMs[line]);
                s.setCoefficient (coefficientCentreFor (mode));
            }

            auto& lane = lanes[static_cast<size_t> (line)];

            lane.prepare (currentSampleRate);
            lane.setShape (shape);
            lane.setSeed (allpassMod::laneSeeds[line]);
            lane.setPhaseOffset (allpassMod::lanePhaseOffsets[line]);
            lane.reset();

            engagement[static_cast<size_t> (line)].prepare (currentSampleRate, 0.04);
            engagement[static_cast<size_t> (line)].snapTo (0.0);

            staticEngagement[static_cast<size_t> (line)].prepare (currentSampleRate, 0.04);
            staticEngagement[static_cast<size_t> (line)].snapTo (0.0);
        }

        swingSmoother.prepare (currentSampleRate, 0.02);
        swingSmoother.snapTo (allpassMod::swingForDepth (depth));

        applyRates();
        applyEngagement (true);
        reset();
    }

    /** Clears the section state and restarts the lanes. */
    void reset() noexcept
    {
        for (auto& s : sections)
            s.reset();

        for (auto& lane : lanes)
            lane.reset();

        for (auto& e : engagement)
            e.snapToTarget();

        for (auto& e : staticEngagement)
            e.snapToTarget();

        swingSmoother.snapToTarget();
    }

    // -----------------------------------------------------------------------
    //  Parameters — real-time safe.
    // -----------------------------------------------------------------------

    void setMode (Mode m) noexcept
    {
        if (m == mode)
            return;

        mode = m;

        for (auto& s : sections)
        {
            s.setMode (m);
            s.setCoefficient (coefficientCentreFor (m));
        }
    }

    [[nodiscard]] Mode getMode() const noexcept { return mode; }

    /** 0..1. Exactly zero disengages the bank entirely. */
    void setDepth (double zeroToOne, bool immediate = false) noexcept
    {
        depth = std::clamp (zeroToOne, 0.0, 1.0);

        const double target = allpassMod::swingForDepth (depth);

        if (immediate) swingSmoother.snapTo (target);
        else           swingSmoother.setTarget (target);

        applyEngagement (immediate);
    }

    void setRateHz (double hz) noexcept
    {
        baseRate = std::clamp (hz, allpassMod::minRateHz, allpassMod::maxRateHz);
        applyRates();
    }

    void setShape (LfoShape s) noexcept
    {
        shape = s;

        for (auto& lane : lanes)
            lane.setShape (s);
    }

    /** How many of the N lines carry a section. */
    void setActiveLines (int count) noexcept
    {
        activeLines = std::clamp (count, 0, N);
        applyEngagement (false);
    }

    /** One or two cascaded sections per active line. */
    void setSectionsPerLine (int count) noexcept
    {
        sectionsPerLine = std::clamp (count, 1, maxSections);
    }

    // -----------------------------------------------------------------------
    //  PHASE 9 STAGE 2 RESEARCH. Not a product control.
    // -----------------------------------------------------------------------
    /**
        Static phase diffusion, independent of Mod Depth.

        PHASE 9 Stage 1 found that the one mechanism which measurably reduces
        late-field periodicity is adding in-loop allpass sections - and that
        it is useless as a product control, because this bank disengages
        entirely at Mod Depth 0. A Late Diffusion knob built on it would stop
        working when a different knob was turned down.

        This is the research path for that problem. It claims the SPARE
        section - index 1, already allocated and prepared, unused while
        sectionsPerLine is 1, which is what production ships - and drives it
        with a fixed coefficient and its own engagement weight. Modulation
        keeps section 0; diffusion gets section 1. Neither depends on the
        other, and the semantics do not change with Mod Depth: section 1 is
        always what diffusion controls.

        `weight` 0 is an exact bypass. The section is skipped, no smoother
        reaches the signal, and the output is bit-identical to what this class
        produced before this path existed - which is the backward-compatibility
        gate, and it is tested rather than asserted.

        Requires sectionsPerLine == 1, since otherwise modulation and
        diffusion would both claim section 1. Production ships 1.
    */
    void setStaticDiffusion (double weight, double coefficient,
                             bool immediate = false) noexcept
    {
        staticWeight = std::clamp (weight, 0.0, 1.0);
        staticCoefficient = std::clamp (coefficient, 0.0,
                                        TimeVaryingAllpass::maxCoefficient);

        for (int line = 0; line < N; ++line)
        {
            sections[index (line, 1)].setCoefficient (staticCoefficient);

            const double target = (staticWeight > 0.0 && line < activeLines)
                                  ? staticWeight : 0.0;

            if (immediate) staticEngagement[static_cast<size_t> (line)].snapTo (target);
            else           staticEngagement[static_cast<size_t> (line)].setTarget (target);
        }
    }

    [[nodiscard]] double getStaticDiffusion() const noexcept { return staticWeight; }
    [[nodiscard]] double getStaticCoefficient() const noexcept { return staticCoefficient; }

    // -----------------------------------------------------------------------
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Advances every lane one sample. Call once per sample, before process(). */
    void advance() noexcept
    {
        const double swing = swingSmoother.getNextValue();

        // The static diffusion path (PHASE 9 research) keeps the bank alive
        // even at Mod Depth 0, which is the whole point of it. With that path
        // at zero this reduces to exactly the previous condition.
        bypassed = swing <= 0.0 && allEngagementSettledOff()
                                && allStaticSettledOff();

        if (bypassed)
        {
            // Phases keep moving so re-engaging does not jump, but nothing is
            // evaluated and no coefficient is written.
            for (int line = 0; line < N; ++line)
                lanes[static_cast<size_t> (line)].skipSample();

            return;
        }

        const double centre = coefficientCentreFor (mode);

        for (int line = 0; line < N; ++line)
        {
            const double value = lanes[static_cast<size_t> (line)].getNextValue();
            const double lineSwing = swing * allpassMod::laneDepthScales[line];

            currentCoefficient[static_cast<size_t> (line)] = centre + lineSwing * value;

            for (int section = 0; section < sectionsPerLine; ++section)
            {
                // The second section of a cascade moves against the first. Two
                // sections driven by the same signal would simply double one
                // movement, which is the same thing as one deeper section;
                // opposing them makes the pair produce a group-delay shape the
                // single section cannot.
                const double shifted = section == 0 ? value : -value;

                sections[index (line, section)].setCoefficient (centre + lineSwing * shifted);
            }
        }
    }

    /** Processes one sample of one line. */
    [[nodiscard]] double process (double x, int line) noexcept
    {
        if (bypassed)
            return x;

        // Both smoothers advance on every sample the bank is awake for,
        // whichever path ends up contributing, so they cannot drift apart.
        const double w  = engagement[static_cast<size_t> (line)].getNextValue();
        const double sw = staticEngagement[static_cast<size_t> (line)].getNextValue();

        double y = x;

        if (w > 0.0)
        {
            double wet = x;

            for (int section = 0; section < sectionsPerLine; ++section)
                wet = sections[index (line, section)].processSample (wet);

            y = x + w * (wet - x);
        }

        // PHASE 9 research: the static diffusion section, after the
        // modulation cascade and with its own weight. At sw = 0 this is
        // skipped entirely and `y` is bit-identical to what this function
        // returned before the path existed.
        if (sw > 0.0)
        {
            const double diffused = sections[index (line, 1)].processSample (y);

            y = y + sw * (diffused - y);
        }

        return y;
    }

    // -----------------------------------------------------------------------
    //  Introspection
    // -----------------------------------------------------------------------

    [[nodiscard]] bool   isBypassed()       const noexcept { return bypassed; }
    [[nodiscard]] double getDepth()         const noexcept { return depth; }
    [[nodiscard]] double getRateHz()        const noexcept { return baseRate; }
    [[nodiscard]] LfoShape getShape()       const noexcept { return shape; }
    [[nodiscard]] int    getActiveLines()   const noexcept { return activeLines; }
    [[nodiscard]] int    getSectionsPerLine() const noexcept { return sectionsPerLine; }
    [[nodiscard]] double getCurrentSwing()  const noexcept { return swingSmoother.getCurrentValue(); }

    [[nodiscard]] double getCoefficient (int line) const noexcept
    {
        return currentCoefficient[static_cast<size_t> (line)];
    }

    [[nodiscard]] const TimeVaryingAllpass& getSection (int line, int section) const noexcept
    {
        return sections[index (line, section)];
    }

    [[nodiscard]] double getLaneRateHz (int line) const noexcept
    {
        return baseRate * allpassMod::laneRateFactors[line];
    }

    /** Base loop delay of a section, in samples. */
    [[nodiscard]] int getSectionDelaySamples (int line, int section) const noexcept
    {
        return sections[index (line, section)].getDelaySamples();
    }

    /**
        Extra loop delay this line's chain contributes, in samples.

        Used by FdnCore to correct the decay gain. Each engaged section adds its
        own mean group delay, which is its pole count and therefore independent
        of both the coefficient and the modulation depth (see
        TimeVaryingAllpass::getMeanGroupDelay). The engagement crossfade weight
        scales it, so a line that is fading in contributes proportionally and the
        correction moves continuously rather than stepping.

        Reported from the engagement *target* rather than its current value, so
        that the gain a line is heading towards is the one its final topology
        needs. The gain smoother and the engagement smoother then converge
        together instead of chasing each other.
    */
    [[nodiscard]] double getAddedLoopDelaySamples (int line) const noexcept
    {
        if (line < 0 || line >= N)
            return 0.0;

        const double weight = engagement[static_cast<size_t> (line)].getTargetValue();

        double added = 0.0;

        // No early return on weight <= 0 any more: at Mod Depth 0 the
        // modulation weight IS zero while the static research path may still
        // be engaged, and returning early there would hide its group delay
        // from the decay-gain correction - which is exactly the drift this
        // function exists to prevent.
        if (weight > 0.0)
        {
            double total = 0.0;

            for (int section = 0; section < sectionsPerLine; ++section)
                total += sections[index (line, section)].getMeanGroupDelay();

            added = weight * total;
        }

        // PHASE 9 research: the static section adds its own group delay, so
        // the decay-gain correction has to see it too, or raising diffusion
        // would lengthen the loop without the gain following and the RT60
        // would drift. Weighted by its own engagement target for the same
        // reason the modulation path is.
        const double staticTarget =
            staticEngagement[static_cast<size_t> (line)].getTargetValue();

        if (staticTarget > 0.0)
            added += staticTarget * sections[index (line, 1)].getMeanGroupDelay();

        return added;
    }

private:
    [[nodiscard]] static constexpr size_t index (int line, int section) noexcept
    {
        return static_cast<size_t> (line * maxSections + section);
    }

    [[nodiscard]] static double coefficientCentreFor (Mode m) noexcept
    {
        // Thiran's parameter is a fractional delay in samples, not a
        // coefficient, so it needs its own centre.
        return m == Mode::thiran ? 1.0 : allpassMod::coefficientCentre;
    }

    void applyRates() noexcept
    {
        for (int line = 0; line < N; ++line)
            lanes[static_cast<size_t> (line)].setRateHz (baseRate * allpassMod::laneRateFactors[line]);
    }

    void applyEngagement (bool immediate) noexcept
    {
        for (int line = 0; line < N; ++line)
        {
            const double target = (depth > 0.0 && line < activeLines) ? 1.0 : 0.0;

            if (immediate) engagement[static_cast<size_t> (line)].snapTo (target);
            else           engagement[static_cast<size_t> (line)].setTarget (target);
        }
    }

    [[nodiscard]] bool allEngagementSettledOff() const noexcept
    {
        for (const auto& e : engagement)
            if (e.getCurrentValue() > 0.0 || e.isSmoothing())
                return false;

        return true;
    }

    /** PHASE 9 research: the same settled-off test for the static path. */
    [[nodiscard]] bool allStaticSettledOff() const noexcept
    {
        for (const auto& e : staticEngagement)
            if (e.getCurrentValue() > 0.0 || e.isSmoothing())
                return false;

        return true;
    }

    std::array<TimeVaryingAllpass, static_cast<size_t> (N * maxSections)> sections;
    std::array<Lfo, static_cast<size_t> (N)> lanes;
    std::array<LinearSmoother<double>, static_cast<size_t> (N)> engagement;
    std::array<LinearSmoother<double>, static_cast<size_t> (N)> staticEngagement;
    std::array<double, static_cast<size_t> (N)> currentCoefficient {};

    LinearSmoother<double> swingSmoother;

    Mode     mode  = allpassMod::defaultMode;
    LfoShape shape = LfoShape::sine;

    double depth             = 0.0;
    double baseRate          = allpassMod::defaultRateHz;
    double currentSampleRate = 48000.0;

    int  activeLines     = allpassMod::defaultActiveLines;
    int  sectionsPerLine = allpassMod::defaultSectionsPerLine;
    bool bypassed        = true;

    // PHASE 9 STAGE 2 research. Zero reproduces PHASE 8 exactly.
    double staticWeight      = 0.0;
    double staticCoefficient = 0.0;
};

} // namespace av::dsp
