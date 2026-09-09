#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

/**
    Offline decay analysis for the PHASE 7 damping gates.

    WHY THIS FILE EXISTS
    --------------------
    The aggregate T20/T30 gate was previously fed by a band filter built from
    two one-pole highpasses and two one-pole lowpasses. That shape is gentle and
    leaky: at 12 kHz its skirt reached down into a region whose RT60 was three
    times longer, so the "band" summed decays that had no business being in it
    and the estimator reported a slope change that the tank did not have.

    Measured on one rendered tail, changing only the band filter:

        3 kHz     one-pole cascade  3.55 %      constant-Q  1.40 %
        6 kHz     one-pole cascade  6.57 %      constant-Q  0.86 %
        12 kHz    one-pole cascade  5.79 %      constant-Q  0.79 %

    The 8.50 % that drove two topology studies was a property of that filter.
    It is kept in the record as OLD ANALYSER / INVALID FOR GATING rather than
    deleted; this file is what replaces it.

    THE BAND, DEFINED
    -----------------
    Two identical RBJ constant-peak-gain bandpass biquads, applied forwards over
    the buffer and then again backwards. The magnitude is therefore |H_bp|^4 and
    the phase is exactly zero, so no group delay can smear the decay it is
    measuring.

    Q is not taken from the usual fractional-octave formula, because that
    formula describes ONE section and this analyser uses four. It is solved for
    numerically instead, so that the ACHIEVED -3 dB edges of the composite
    response are the requested number of octaves apart. The achieved edges are
    reported, not assumed.

    VALIDITY
    --------
    A measurement is marked invalid only for a concrete, stated reason — the fit
    interval was not reachable, the band's own ring time is not negligible
    against the decay, or the render did not hold enough dynamic range. Modal
    overlap is NOT an exclusion rule: a one-octave aggregate measurement can be
    perfectly well posed in a region where individual modes are not separable,
    and throwing away the hard cases is how a gate stops being a gate.
*/
namespace avtest::decay
{

inline constexpr double pi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
//  Band filter
// ---------------------------------------------------------------------------

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    void filter (std::vector<double>& x) const noexcept
    {
        double z1 = 0.0, z2 = 0.0;

        for (auto& s : x)
        {
            const double in = s;
            const double out = b0 * in + z1;

            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;

            s = out;
        }
    }

    /** |H(f)| of one section. */
    [[nodiscard]] double magnitude (double freqHz, double sampleRate) const noexcept
    {
        const double w = 2.0 * pi * freqHz / sampleRate;
        const double c1 = std::cos (w), s1 = std::sin (w);
        const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

        const double nr = b0 + b1 * c1 + b2 * c2;
        const double ni =    - b1 * s1 - b2 * s2;
        const double dr = 1.0 + a1 * c1 + a2 * c2;
        const double di =     - a1 * s1 - a2 * s2;

        const double dm = dr * dr + di * di;

        if (dm <= 0.0)
            return 0.0;

        return std::sqrt ((nr * nr + ni * ni) / dm);
    }
};

[[nodiscard]] inline Biquad makeBandpass (double centreHz, double q, double sampleRate) noexcept
{
    const double w0 = 2.0 * pi * centreHz / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    Biquad result;
    result.b0 =  alpha / a0;
    result.b1 =  0.0;
    result.b2 = -alpha / a0;
    result.a1 = (-2.0 * std::cos (w0)) / a0;
    result.a2 = (1.0 - alpha) / a0;

    return result;
}

/** Sections applied forwards then backwards; four in magnitude terms. */
inline constexpr int sectionsPerPass = 2;
inline constexpr int magnitudeOrder  = 2 * sectionsPerPass;

/** Composite |H|, i.e. one section's magnitude raised to the applied count. */
[[nodiscard]] inline double compositeMagnitude (const Biquad& section,
                                                double freqHz, double sampleRate) noexcept
{
    return std::pow (section.magnitude (freqHz, sampleRate), magnitudeOrder);
}

/** The two frequencies where the composite response is 3 dB down. */
struct BandEdges
{
    double lowHz = 0.0, highHz = 0.0;

    [[nodiscard]] double octaves() const noexcept
    {
        return (lowHz > 0.0 && highHz > lowHz) ? std::log2 (highHz / lowHz) : 0.0;
    }
};

[[nodiscard]] inline BandEdges measureEdges (const Biquad& section, double centreHz,
                                             double sampleRate) noexcept
{
    const double target = 1.0 / std::sqrt (2.0);   // -3 dB

    const auto search = [&] (double from, double to)
    {
        double lo = from, hi = to;

        for (int i = 0; i < 80; ++i)
        {
            const double mid = std::sqrt (lo * hi);

            if (compositeMagnitude (section, mid, sampleRate) > target)
                hi = mid;
            else
                lo = mid;
        }

        return std::sqrt (lo * hi);
    };

    BandEdges edges;
    edges.lowHz  = search (centreHz / 64.0, centreHz);
    edges.highHz = search (std::min (centreHz * 64.0, 0.499 * sampleRate), centreHz);

    return edges;
}

/**
    Q such that the COMPOSITE response spans the requested octaves at -3 dB.

    Solved rather than taken from the single-section formula, which would make
    the achieved band roughly half the width asked for.
*/
[[nodiscard]] inline double solveQ (double centreHz, double octaves, double sampleRate) noexcept
{
    double lo = 0.05, hi = 400.0;

    for (int i = 0; i < 100; ++i)
    {
        const double mid = std::sqrt (lo * hi);
        const auto edges = measureEdges (makeBandpass (centreHz, mid, sampleRate), centreHz,
                                         sampleRate);

        if (edges.octaves() > octaves)
            lo = mid;       // too wide, raise Q
        else
            hi = mid;
    }

    return std::sqrt (lo * hi);
}

struct Band
{
    Biquad section;
    BandEdges edges;
    double centreHz = 0.0;
    double q = 0.0;

    /** Bandwidth in Hz between the -3 dB edges. */
    [[nodiscard]] double bandwidthHz() const noexcept { return edges.highHz - edges.lowHz; }

    /** How long the band itself rings, which bounds what it can measure. */
    [[nodiscard]] double ringSeconds() const noexcept
    {
        return 1.0 / (pi * std::max (bandwidthHz(), 1.0e-9));
    }

    void apply (std::vector<double>& x) const
    {
        for (int i = 0; i < sectionsPerPass; ++i)
            section.filter (x);

        std::reverse (x.begin(), x.end());

        for (int i = 0; i < sectionsPerPass; ++i)
            section.filter (x);

        std::reverse (x.begin(), x.end());
    }
};

[[nodiscard]] inline Band makeBand (double centreHz, double octaves, double sampleRate)
{
    Band band;
    band.centreHz = centreHz;
    band.q = solveQ (centreHz, octaves, sampleRate);
    band.section = makeBandpass (centreHz, band.q, sampleRate);
    band.edges = measureEdges (band.section, centreHz, sampleRate);

    return band;
}

// ---------------------------------------------------------------------------
//  Decay estimate
// ---------------------------------------------------------------------------

struct Result
{
    double edt = 0.0, t20 = 0.0, t30 = 0.0, fitted = 0.0, r2 = 0.0;
    double dynamicRangeDb = 0.0;
    double ringFraction = 0.0;      ///< band ring time over T30
    bool valid = false;
    std::string reason;

    [[nodiscard]] double mismatch() const noexcept
    {
        const double mean = 0.5 * (t20 + t30);
        return mean > 0.0 ? std::abs (t20 - t30) / mean : 0.0;
    }
};

/**
    Schroeder backward integration, normalised in the LINEAR energy domain.

    Normalisation happens here, before anything is averaged, so that every
    realisation carries equal statistical weight: a seed that happens to put
    more energy into the analysis band must not dominate an ensemble simply by
    being louder. The returned curve starts at exactly 1.0.
*/
[[nodiscard]] inline std::vector<double> normalisedEdc (const std::vector<double>& band)
{
    const size_t n = band.size();

    std::vector<double> energy (n + 1, 0.0);

    for (size_t i = n; i-- > 0;)
        energy[i] = energy[i + 1] + band[i] * band[i];

    if (energy[0] <= 0.0)
        return {};

    const double reference = energy[0];

    energy.resize (n);

    for (auto& e : energy)
        e /= reference;

    return energy;
}

/** Everything derived from one energy decay curve, linear and normalised. */
[[nodiscard]] inline Result analyseEdc (const std::vector<double>& edc, const Band& filter,
                                        double sampleRate)
{
    Result result;

    const size_t n = edc.size();

    if (n < 64)
    {
        result.reason = "curve too short";
        return result;
    }

    double smallest = edc[0];

    for (size_t i = 0; i < n; ++i)
        smallest = std::min (smallest, std::max (edc[i], 1.0e-300));

    result.dynamicRangeDb = 10.0 * std::log10 (std::max (edc[0], 1.0e-300)
                                               / std::max (smallest, 1.0e-300));

    std::vector<double> curve (n, 0.0);

    for (size_t i = 0; i < n; ++i)
        curve[i] = 10.0 * std::log10 (std::max (edc[i] / std::max (edc[0], 1.0e-300), 1.0e-300));

    const auto timeAt = [&] (double dB) -> double
    {
        for (size_t i = 0; i < n; ++i)
            if (curve[i] <= dB)
                return static_cast<double> (i) / sampleRate;

        return -1.0;
    };

    const double t5 = timeAt (-5.0), t10 = timeAt (-10.0);
    const double t25 = timeAt (-25.0), t35 = timeAt (-35.0);

    if (t5 < 0.0 || t10 <= 0.0 || t25 <= t5 || t35 <= t5)
    {
        result.reason = "fit interval not reachable in this render";
        return result;
    }

    result.edt = t10 * 6.0;
    result.t20 = (t25 - t5) * 3.0;
    result.t30 = (t35 - t5) * 2.0;

    const size_t first = static_cast<size_t> (t5 * sampleRate);
    const size_t last  = static_cast<size_t> (t35 * sampleRate);

    if (last <= first + 64)
    {
        result.reason = "fit interval too few samples";
        return result;
    }

    const size_t stride = std::max<size_t> (1, (last - first) / 2000);

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0, count = 0.0;

    for (size_t i = first; i <= last; i += stride)
    {
        const double x = static_cast<double> (i) / sampleRate;
        const double y = curve[i];

        sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
        count += 1.0;
    }

    const double denominator = count * sxx - sx * sx;

    if (std::abs (denominator) < 1.0e-12)
    {
        result.reason = "degenerate fit";
        return result;
    }

    const double slope = (count * sxy - sx * sy) / denominator;

    if (slope >= 0.0)
    {
        result.reason = "decay curve does not fall";
        return result;
    }

    result.fitted = -60.0 / slope;

    const double numerator = count * sxy - sx * sy;

    result.r2 = (numerator * numerator) / (denominator * (count * syy - sy * sy));

    result.ringFraction = filter.ringSeconds() / std::max (result.t30, 1.0e-9);

    // Concrete, stated reasons only.
    if (result.ringFraction > 0.05)
    {
        result.reason = "band rings for " + std::to_string (result.ringFraction * 100.0)
                        + " % of T30";
        return result;
    }

    if (result.dynamicRangeDb < 45.0)
    {
        result.reason = "only " + std::to_string (result.dynamicRangeDb)
                        + " dB of decay available";
        return result;
    }

    result.valid = true;
    return result;
}

/** Convenience: band-limited buffer straight to a result. */
[[nodiscard]] inline Result analyse (const std::vector<double>& band, const Band& filter,
                                     double sampleRate)
{
    const auto edc = normalisedEdc (band);

    if (edc.empty())
    {
        Result result;
        result.reason = "no energy in band";
        return result;
    }

    return analyseEdc (edc, filter, sampleRate);
}

} // namespace avtest::decay
