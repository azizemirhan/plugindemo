#include "ModalMap.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /**
        Magnitude of a Hann-windowed DFT evaluated exactly at one frequency.

        Not a bin of an FFT: the frequency is arbitrary, so no interpolation and
        no bin misalignment. As a filter this is a bandpass whose main lobe is
        about 2/window Hz wide, and - the point of using it - it contributes no
        decay of its own. The previous estimator band-limited to about 1 Hz and
        then measured the filter's own ring; this cannot do that, because the
        window slides and each frame is an independent measurement of the
        amplitude at that instant.
    */
    [[nodiscard]] double windowedMagnitudeAt (const double* x, size_t length,
                                              double centreHz, double sampleRate)
    {
        const double w = 2.0 * pi * centreHz / sampleRate;

        double re = 0.0, im = 0.0, norm = 0.0;

        for (size_t n = 0; n < length; ++n)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (n)
                                                      / static_cast<double> (length - 1));
            const double s = x[n] * hann;
            const double phase = w * static_cast<double> (n);

            re += s * std::cos (phase);
            im -= s * std::sin (phase);
            norm += hann;
        }

        return norm > 0.0 ? 2.0 * std::sqrt (re * re + im * im) / norm : 0.0;
    }

    struct LineFit { double slope = 0.0, intercept = 0.0, rSquared = 0.0, residualRms = 0.0; int n = 0; };

    [[nodiscard]] LineFit fitLine (const std::vector<double>& t, const std::vector<double>& y)
    {
        LineFit f;

        f.n = static_cast<int> (t.size());

        if (f.n < 3)
            return f;

        const double n = static_cast<double> (f.n);
        const double mt = std::accumulate (t.begin(), t.end(), 0.0) / n;
        const double my = std::accumulate (y.begin(), y.end(), 0.0) / n;

        double stt = 0.0, sty = 0.0, syy = 0.0;

        for (int i = 0; i < f.n; ++i)
        {
            const double dt = t[static_cast<size_t> (i)] - mt;
            const double dy = y[static_cast<size_t> (i)] - my;

            stt += dt * dt; sty += dt * dy; syy += dy * dy;
        }

        if (stt <= 0.0)
            return f;

        f.slope = sty / stt;
        f.intercept = my - f.slope * mt;

        double residual = 0.0;

        for (int i = 0; i < f.n; ++i)
        {
            const double e = y[static_cast<size_t> (i)]
                           - (f.intercept + f.slope * t[static_cast<size_t> (i)]);
            residual += e * e;
        }

        f.rSquared = syy > 0.0 ? 1.0 - residual / syy : 0.0;
        f.residualRms = std::sqrt (residual / n);

        return f;
    }
}

const char* describe (DecayQuality q) noexcept
{
    switch (q)
    {
        case DecayQuality::measured:          return "measured";
        case DecayQuality::unresolved:        return "unresolved";
        case DecayQuality::contaminated:      return "contaminated";
        case DecayQuality::insufficientData:  return "insufficient data";
    }

    return "unknown";
}

ModalDecayEstimate estimateModalDecay (const std::vector<double>& signal,
                                       double sampleRate, double centreHz,
                                       const DecayOptions& options)
{
    ModalDecayEstimate e;

    const auto window = static_cast<size_t> (sampleRate * options.windowSeconds);
    const auto hop = std::max<size_t> (1, static_cast<size_t> (sampleRate * options.hopSeconds));

    e.envelopeBandwidthHz = options.windowSeconds > 0.0 ? 2.0 / options.windowSeconds : 0.0;

    if (window < 8 || signal.size() < window + hop)
    {
        e.reason = "signal shorter than one analysis window";
        return e;
    }

    // Envelope of the mode: one amplitude per hop.
    std::vector<double> timeSeconds, levelDb;

    for (size_t start = 0; start + window <= signal.size(); start += hop)
    {
        const double m = windowedMagnitudeAt (signal.data() + start, window, centreHz, sampleRate);

        // The window's centre is when this amplitude applies.
        timeSeconds.push_back ((static_cast<double> (start) + 0.5 * static_cast<double> (window))
                               / sampleRate);
        levelDb.push_back (m > 0.0 ? 20.0 * std::log10 (m) : -400.0);
    }

    if (levelDb.size() < static_cast<size_t> (options.minFrames))
    {
        e.reason = "too few frames for a fit";
        return e;
    }

    const double peak = *std::max_element (levelDb.begin(), levelDb.end());

    // T20 and T30, the same convention the broadband figure uses: fit from
    // 5 dB below the peak so the onset is excluded, down to 25 or 35 dB below.
    const auto fitBetween = [&] (double upperDrop, double lowerDrop) -> LineFit
    {
        std::vector<double> t, y;

        bool started = false;

        for (size_t i = 0; i < levelDb.size(); ++i)
        {
            const double drop = peak - levelDb[i];

            if (drop < upperDrop)
                continue;

            started = true;

            if (drop > lowerDrop)
                break;

            t.push_back (timeSeconds[i]);
            y.push_back (levelDb[i]);
        }

        return started ? fitLine (t, y) : LineFit {};
    };

    const auto t20 = fitBetween (5.0, 25.0);
    const auto t30 = fitBetween (5.0, 35.0);

    e.framesUsed = t30.n > 0 ? t30.n : t20.n;

    const LineFit& primary = t30.n >= options.minFrames ? t30 : t20;

    if (primary.n < options.minFrames || primary.slope >= 0.0)
    {
        e.reason = t30.n < options.minFrames && t20.n < options.minFrames
                 ? "the mode never decayed far enough within the signal"
                 : "no decaying trend";
        return e;
    }

    e.rSquared = primary.rSquared;
    e.residualRms = primary.residualRms;

    if (t20.n >= 3 && t20.slope < 0.0) e.rt60FromT20 = -60.0 / t20.slope;
    if (t30.n >= 3 && t30.slope < 0.0) e.rt60FromT30 = -60.0 / t30.slope;

    // Two modes inside the envelope's bandwidth beat against each other, and a
    // beating envelope is not a straight line in dB however long it is fitted.
    // That is the signal this estimator uses to refuse: the residual about the
    // line rises far above what a single decaying mode leaves behind.
    if (primary.rSquared < options.minRSquared || primary.residualRms > options.maxResidualDb)
    {
        e.quality = DecayQuality::unresolved;
        e.reason = "envelope is not a single exponential - R^2 "
                 + std::to_string (primary.rSquared) + ", residual "
                 + std::to_string (primary.residualRms) + " dB";
        return e;
    }

    // Contamination guard. This runs after the residual test, not instead of
    // it, because the two catch different things: the residual catches an
    // envelope that visibly beats, and this catches the case where it does
    // not - two unresolved modes whose combination happens to look straight.
    for (const auto& n : options.neighbours)
    {
        const double distance = std::abs (n.frequencyHz - centreHz);

        if (distance < e.envelopeBandwidthHz && n.relativeLevelDb > options.contaminationLevelDb)
        {
            e.quality = DecayQuality::contaminated;
            e.reason = "a neighbour at " + std::to_string (distance)
                     + " Hz sits inside the " + std::to_string (e.envelopeBandwidthHz)
                     + " Hz analysis bandwidth at " + std::to_string (n.relativeLevelDb)
                     + " dB relative - the fit cannot be attributed to one mode";
            return e;
        }
    }

    e.quality = DecayQuality::measured;
    e.reason = "none";

    return e;
}

} // namespace avtest
