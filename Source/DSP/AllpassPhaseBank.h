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
    //  Process — real-time safe.
    // -----------------------------------------------------------------------

    /** Advances every lane one sample. Call once per sample, before process(). */
    void advance() noexcept
    {
        const double swing = swingSmoother.getNextValue();

        bypassed = swing <= 0.0 && allEngagementSettledOff();

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

        const double w = engagement[static_cast<size_t> (line)].getNextValue();

        if (w <= 0.0)
            return x;

        double wet = x;

        for (int section = 0; section < sectionsPerLine; ++section)
            wet = sections[index (line, section)].processSample (wet);

        return x + w * (wet - x);
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

        if (weight <= 0.0)
            return 0.0;

        double total = 0.0;

        for (int section = 0; section < sectionsPerLine; ++section)
            total += sections[index (line, section)].getMeanGroupDelay();

        return weight * total;
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

    std::array<TimeVaryingAllpass, static_cast<size_t> (N * maxSections)> sections;
    std::array<Lfo, static_cast<size_t> (N)> lanes;
    std::array<LinearSmoother<double>, static_cast<size_t> (N)> engagement;
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
};

} // namespace av::dsp
