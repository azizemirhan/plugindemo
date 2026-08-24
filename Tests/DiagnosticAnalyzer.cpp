#include "DiagnosticAnalyzer.h"

#include "DspAnalysis.h"
#include "FdnAnalysis.h"
#include "ModulationAnalysis.h"

#include "Diagnostics/DiagnosticPaths.h"
#include "Diagnostics/DiagnosticReport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace avtest
{

namespace
{
    uint32_t readU32 (const unsigned char* p) noexcept
    {
        return static_cast<uint32_t> (p[0])
             | (static_cast<uint32_t> (p[1]) << 8)
             | (static_cast<uint32_t> (p[2]) << 16)
             | (static_cast<uint32_t> (p[3]) << 24);
    }

    uint16_t readU16 (const unsigned char* p) noexcept
    {
        return static_cast<uint16_t> (static_cast<uint16_t> (p[0])
                                      | (static_cast<uint16_t> (p[1]) << 8));
    }

    double toDb (double linear) noexcept
    {
        return linear > 0.0 ? 20.0 * std::log10 (linear) : -200.0;
    }

    std::string fixedText (double value, int decimals)
    {
        char buffer[64] {};
        std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, value);
        return buffer;
    }

    std::string pad (const std::string& text, size_t width)
    {
        return text.size() >= width ? text : text + std::string (width - text.size(), ' ');
    }

    /**
        True peak by 4x zero-stuffed FFT interpolation.

        Sample peak misses inter-sample peaks, which is precisely what a
        converter will reconstruct and clip on. Far too heavy for a callback,
        which is why it only exists here.
    */
    /**
        True peak of one power-of-two block, by 4x spectral interpolation.

        `margin` samples at each end of the result are ignored. The transform is
        circular, so the block's first and last samples are neighbours in the
        reconstruction; wherever they differ there is a step, and the
        band-limited interpolation rings around it. Discarding the edges and
        letting the caller overlap the blocks removes that, which zero-padding
        the whole signal into one transform cannot: the abrupt end of the signal
        is exactly such a step, and it overshot the real peak of a unit sine by
        2.4 %.
    */
    double truePeakOfBlock (const double* block, size_t n, size_t margin)
    {
        std::vector<std::complex<double>> spectrum (n, { 0.0, 0.0 });

        for (size_t i = 0; i < n; ++i)
            spectrum[i] = { block[i], 0.0 };

        fft (spectrum);

        // Zero-pad the spectrum to four times the length, which is exact
        // band-limited interpolation rather than a filter approximation.
        std::vector<std::complex<double>> upsampled (n * 4, { 0.0, 0.0 });

        const size_t half = n / 2;

        upsampled[0] = spectrum[0];

        for (size_t k = 1; k < half; ++k)
        {
            upsampled[k] = spectrum[k];
            upsampled[upsampled.size() - k] = spectrum[n - k];
        }

        // The original Nyquist bin is not Nyquist any more once the rate has
        // gone up four times, so it has to be split between the positive and
        // negative sides. Copying it once, as the first version did, left the
        // spectrum non-Hermitian: the inverse transform then came out complex
        // and taking its real part discarded energy, which is why the measured
        // true peak could sit *below* the sample peak — an impossibility that
        // the invariant check caught.
        upsampled[half] = spectrum[half] * 0.5;
        upsampled[upsampled.size() - half] = std::conj (spectrum[half]) * 0.5;

        ifft (upsampled);

        double peak = 0.0;

        const size_t first = margin * 4;
        const size_t last = upsampled.size() > margin * 4 ? upsampled.size() - margin * 4 : 0;

        for (size_t i = first; i < last; ++i)
            peak = std::max (peak, std::abs (upsampled[i].real()) * 4.0);

        return peak;
    }

    /**
        True peak of a whole signal, by overlapping blocks.

        Reported together with the sample peak, not instead of it: the sample
        instants are part of the reconstructed waveform, so the true peak is by
        definition at least the sample peak. That is the definition rather than a
        floor bolted on — and it also removes a rounding sensitivity, because
        when the peak falls exactly on a sample, as an impulse response's usually
        does, a round trip through two large transforms returns it a few parts in
        10^15 low.
    */
    double truePeakOf (const std::vector<double>& signal)
    {
        const double samplePeak = peakAbsolute (signal);

        if (signal.size() < 64)
            return samplePeak;

        constexpr size_t blockSize = 8192;
        constexpr size_t margin = 512;

        double peak = 0.0;

        if (signal.size() <= blockSize)
        {
            // Short enough for one transform. Padded to a power of two, and the
            // padding is inside the discarded margin.
            size_t n = 64;
            while (n < signal.size() + 2 * margin)
                n <<= 1;

            std::vector<double> padded (n, 0.0);
            std::copy (signal.begin(), signal.end(), padded.begin() + static_cast<ptrdiff_t> (margin));

            peak = truePeakOfBlock (padded.data(), n, margin / 2);
        }
        else
        {
            // Overlapping blocks, each fully filled from the signal, with only
            // the interior of each kept. The hop is smaller than the retained
            // interior so every sample is covered by at least one block.
            const size_t hop = blockSize - 2 * margin;

            for (size_t start = 0; start + blockSize <= signal.size(); start += hop)
                peak = std::max (peak, truePeakOfBlock (signal.data() + start, blockSize, margin));

            // The tail, aligned to the end so it is also a full block.
            if (signal.size() >= blockSize)
                peak = std::max (peak, truePeakOfBlock (signal.data() + signal.size() - blockSize,
                                                        blockSize, margin));
        }

        return std::max (peak, samplePeak);
    }
}

double truePeakOfSignal (const std::vector<double>& signal)
{
    // The same truePeakOf the capture analysis uses. Exposed rather than
    // duplicated: two implementations of a headroom measurement that disagree
    // would be worse than not measuring it twice.
    return truePeakOf (signal);
}

const char* describe (Measurability m) noexcept
{
    switch (m)
    {
        case Measurability::measured:           return "measured";
        case Measurability::sourceNotImpulsive: return "not measurable from this source";
        case Measurability::tooShort:           return "capture too short";
        case Measurability::silent:             return "capture is silent";
        case Measurability::fileMissing:        return "file missing";
    }

    return "unknown";
}

bool readWav (const std::string& path, std::vector<double>& left,
              std::vector<double>& right, double& sampleRate, std::string& error)
{
    std::ifstream stream (path, std::ios::binary);

    if (! stream.is_open())
    {
        error = "cannot open " + path;
        return false;
    }

    std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (stream)),
                                      std::istreambuf_iterator<char>());

    if (bytes.size() < 44)
    {
        error = "file too short to be a WAV";
        return false;
    }

    if (std::memcmp (bytes.data(), "RIFF", 4) != 0
        || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    size_t position = 12;

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t dataOffset = 0, dataBytes = 0;

    while (position + 8 <= bytes.size())
    {
        const char* id = reinterpret_cast<const char*> (bytes.data() + position);
        const uint32_t size = readU32 (bytes.data() + position + 4);

        if (std::memcmp (id, "fmt ", 4) == 0 && position + 8 + 16 <= bytes.size())
        {
            format   = readU16 (bytes.data() + position + 8);
            channels = readU16 (bytes.data() + position + 10);
            rate     = readU32 (bytes.data() + position + 12);
            bits     = readU16 (bytes.data() + position + 22);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataOffset = position + 8;
            dataBytes = std::min (static_cast<size_t> (size), bytes.size() - dataOffset);
        }

        position += 8 + size + (size & 1u);
    }

    if (format != 3 || bits != 32)
    {
        error = "expected 32-bit float WAV";
        return false;
    }

    if (channels < 1 || channels > 2 || dataOffset == 0)
    {
        error = "unsupported channel count or missing data chunk";
        return false;
    }

    sampleRate = static_cast<double> (rate);

    const size_t frames = dataBytes / (4u * channels);

    left.assign (frames, 0.0);
    right.assign (frames, 0.0);

    for (size_t frame = 0; frame < frames; ++frame)
    {
        float value = 0.0f;

        std::memcpy (&value, bytes.data() + dataOffset + frame * 4u * channels, 4);
        left[frame] = static_cast<double> (value);

        if (channels == 2)
        {
            std::memcpy (&value, bytes.data() + dataOffset + frame * 8u + 4u, 4);
            right[frame] = static_cast<double> (value);
        }
        else
        {
            right[frame] = left[frame];
        }
    }

    return true;
}

bool looksImpulsive (const std::vector<double>& signal, double sampleRate)
{
    if (signal.size() < static_cast<size_t> (sampleRate * 0.2))
        return false;

    const double peak = peakAbsolute (signal);
    const double level = rms (signal);

    if (peak <= 0.0 || level <= 0.0)
        return false;

    // An impulse response has a very high crest factor and almost all of its
    // energy near the start. Music and sustained tones have neither.
    const double crest = peak / level;

    double totalEnergy = 0.0, earlyEnergy = 0.0;

    const auto earlyLength = static_cast<size_t> (0.05 * static_cast<double> (signal.size()));

    for (size_t i = 0; i < signal.size(); ++i)
    {
        const double power = signal[i] * signal[i];
        totalEnergy += power;

        if (i < earlyLength)
            earlyEnergy += power;
    }

    if (totalEnergy <= 0.0)
        return false;

    const double earlyFraction = earlyEnergy / totalEnergy;

    return crest > 8.0 && earlyFraction > 0.35;
}

DiagnosticAnalysis analyseCapture (const std::string& path, bool impulsive)
{
    DiagnosticAnalysis a;

    std::vector<double> left, right;

    if (! readWav (path, left, right, a.sampleRate, a.error))
    {
        a.decayMeasurability = Measurability::fileMissing;
        a.movementMeasurability = Measurability::fileMissing;
        return a;
    }

    a.valid = true;
    a.channels = 2;
    a.frames = left.size();
    a.durationSeconds = a.sampleRate > 0.0 ? static_cast<double> (a.frames) / a.sampleRate : 0.0;

    if (a.frames == 0)
    {
        a.decayMeasurability = Measurability::tooShort;
        a.movementMeasurability = Measurability::tooShort;
        return a;
    }

    a.peakLeft = peakAbsolute (left);
    a.peakRight = peakAbsolute (right);
    a.rmsLeft = rms (left);
    a.rmsRight = rms (right);
    a.dcLeft = mean (left);
    a.dcRight = mean (right);

    for (size_t i = 0; i < a.frames; ++i)
    {
        if (std::abs (left[i]) > 1.0)  ++a.clippedSamples;
        if (std::abs (right[i]) > 1.0) ++a.clippedSamples;
    }

    a.truePeak = std::max (truePeakOf (left), truePeakOf (right));

    const double meanRms = 0.5 * (a.rmsLeft + a.rmsRight);

    if (meanRms <= 1.0e-9)
    {
        a.decayMeasurability = Measurability::silent;
        a.movementMeasurability = Measurability::silent;
        return a;
    }

    a.correlation = correlation (left, right);

    std::vector<double> mono (a.frames);

    for (size_t i = 0; i < a.frames; ++i)
        mono[i] = 0.5 * (left[i] + right[i]);

    const double monoRms = rms (mono);

    if (monoRms > 0.0)
        a.monoDownmixDb = 20.0 * std::log10 (monoRms / meanRms);

    // Third octaves, measured exactly the way every DSP phase measured them.
    {
        size_t fftSize = 1;
        while (fftSize < mono.size() && fftSize < 1u << 20)
            fftSize <<= 1;

        const auto magnitude = magnitudeSpectrum (mono.data(),
                                                  std::min (mono.size(), fftSize),
                                                  fftSize, true);
        const auto bands = thirdOctaveEnergy (magnitude, a.sampleRate, fftSize, 100.0, 10000.0);

        double sum = 0.0;

        for (double e : bands.energyDb)
            sum += e;

        if (! bands.energyDb.empty())
        {
            const double band = sum / static_cast<double> (bands.energyDb.size());

            for (double e : bands.energyDb)
                a.worstThirdOctaveDb = std::max (a.worstThirdOctaveDb, band - e);
        }
    }

    // Silence tail: how long the end of the capture stays below audibility.
    {
        const double threshold = 1.0e-5;
        size_t silent = 0;

        for (size_t i = a.frames; i-- > 0;)
        {
            if (std::abs (mono[i]) > threshold)
                break;

            ++silent;
        }

        a.silenceTailSeconds = a.sampleRate > 0.0
                             ? static_cast<double> (silent) / a.sampleRate : 0.0;
        a.tailDecaysToSilence = silent > 0;
    }

    // --- Decay. Only if the source justifies it. ---
    const bool suitable = impulsive || looksImpulsive (mono, a.sampleRate);

    if (! suitable)
    {
        a.decayMeasurability = Measurability::sourceNotImpulsive;
    }
    else if (a.durationSeconds < 0.5)
    {
        a.decayMeasurability = Measurability::tooShort;
    }
    else
    {
        const auto broadband = measureDecay (mono, a.sampleRate);

        if (! broadband.valid)
        {
            a.decayMeasurability = Measurability::tooShort;
        }
        else
        {
            a.rt60Broadband = broadband.rt60FromT30;
            a.t20 = broadband.rt60FromT20;
            a.t30 = broadband.rt60FromT30;
            a.decayLinearity = broadband.linearity;
            a.earlyDecayTime = broadband.earlyDecayTime;

            const auto low  = measureBandDecay (mono, a.sampleRate, 100.0, 500.0);
            const auto mid  = measureBandDecay (mono, a.sampleRate, 500.0, 2000.0);
            const auto high = measureBandDecay (mono, a.sampleRate, 2000.0, 8000.0);

            a.rt60Low  = low.rt60FromT30;
            a.rt60Mid  = mid.rt60FromT30;
            a.rt60High = high.rt60FromT30;

            // Kept, not discarded. A band RT60 whose fit is poor is a different
            // finding from one whose fit is good, and the two used to be
            // reported identically.
            a.decayLinearityLow  = low.linearity;
            a.decayLinearityMid  = mid.linearity;
            a.decayLinearityHigh = high.linearity;
        }
    }

    // --- Movement. Needs a few seconds of tail to say anything. ---
    if (a.durationSeconds < 3.0)
    {
        a.movementMeasurability = Measurability::tooShort;
    }
    else
    {
        const auto start = static_cast<size_t> (0.5 * a.sampleRate);
        const auto length = std::min (a.frames - start, static_cast<size_t> (4.0 * a.sampleRate));

        if (length < static_cast<size_t> (2.0 * a.sampleRate))
        {
            a.movementMeasurability = Measurability::tooShort;
        }
        else
        {
            const auto flat = flattenDecay (mono.data() + start, length);

            a.latePeriodicity = maxEnvelopePeriodicity (flat.data(), flat.size(),
                                                        static_cast<size_t> (0.001 * a.sampleRate),
                                                        static_cast<size_t> (0.120 * a.sampleRate));

            const std::vector<double> window (mono.begin() + static_cast<ptrdiff_t> (start),
                                              mono.begin() + static_cast<ptrdiff_t> (start + length));

            const auto spectrogram = spectrogramOf (window, a.sampleRate, 32768, 16384);

            if (spectrogram.frames.size() >= 2)
            {
                a.modalPersistence = modalPeakPersistence (spectrogram, 200.0, 4000.0, 40, 4.0);

                // The canonical calculation, same spectrogram and same
                // parameters - not a second implementation of the idea.
                a.modalMovementHz = modalPeakMovementHz (spectrogram, 200.0, 4000.0, 40, 4.0);
                a.spectralFlux = spectralFlux (spectrogram, 200.0, 4000.0);
                a.peakLifetimeSeconds = modalPeakLifetime (spectrogram, 200.0, 4000.0, 40, 4.0)
                                        .meanSeconds;
            }
            else
            {
                a.movementMeasurability = Measurability::tooShort;
            }
        }
    }

    return a;
}

std::string buildAnalysisText (const DiagnosticAnalysis& analysis, const std::string& sourceName)
{
    std::ostringstream out;

    const auto line = [&out] (const char* label, const std::string& value)
    {
        out << pad (label, 28) << value << "\n";
    };

    out << "AuroraVerb Offline Analysis\n";
    out << "===========================\n\n";

    line ("Source:", sourceName);

    if (! analysis.valid)
    {
        out << "\nCould not analyse: " << analysis.error << "\n";
        return out.str();
    }

    line ("Sample rate:", fixedText (analysis.sampleRate, 1) + " Hz");
    line ("Duration:", fixedText (analysis.durationSeconds, 3) + " s");
    line ("Frames:", std::to_string (analysis.frames));

    out << "\nLevels\n------\n";
    line ("Peak L / R:", fixedText (toDb (analysis.peakLeft), 2) + " / "
                         + fixedText (toDb (analysis.peakRight), 2) + " dBFS");
    line ("True peak:", fixedText (toDb (analysis.truePeak), 2) + " dBTP");
    line ("RMS L / R:", fixedText (toDb (analysis.rmsLeft), 2) + " / "
                        + fixedText (toDb (analysis.rmsRight), 2) + " dBFS");
    line ("DC L / R:", fixedText (analysis.dcLeft, 8) + " / " + fixedText (analysis.dcRight, 8));
    line ("Clipped samples:", std::to_string (analysis.clippedSamples));

    out << "\nStereo\n------\n";
    line ("Correlation:", fixedText (analysis.correlation, 4));
    line ("Mono downmix:", fixedText (analysis.monoDownmixDb, 2) + " dB");
    line ("Worst 3rd-octave dip:", fixedText (analysis.worstThirdOctaveDb, 2) + " dB");

    out << "\nDecay\n-----\n";

    if (analysis.decayMeasurability != Measurability::measured)
    {
        line ("RT60:", describe (analysis.decayMeasurability));
        out << "\n  No decay figure is reported for this source. Measuring RT60 from material\n";
        out << "  that is not impulsive produces a confident number that means nothing, which\n";
        out << "  is worse than reporting nothing.\n";
    }
    else
    {
        line ("RT60 broadband:", fixedText (analysis.rt60Broadband, 3) + " s");
        line ("RT60 low / mid / high:", fixedText (analysis.rt60Low, 3) + " / "
                                        + fixedText (analysis.rt60Mid, 3) + " / "
                                        + fixedText (analysis.rt60High, 3) + " s");
        line ("T20 / T30:", fixedText (analysis.t20, 3) + " / " + fixedText (analysis.t30, 3) + " s");
        line ("Early decay time:", fixedText (analysis.earlyDecayTime, 3) + " s");
        line ("Decay linearity R^2:", fixedText (analysis.decayLinearity, 4));
        line ("  by band, low / mid / high:", fixedText (analysis.decayLinearityLow, 4) + " / "
                                              + fixedText (analysis.decayLinearityMid, 4) + " / "
                                              + fixedText (analysis.decayLinearityHigh, 4));
    }

    out << "\nMovement\n--------\n";

    if (analysis.movementMeasurability != Measurability::measured)
    {
        line ("Modal metrics:", describe (analysis.movementMeasurability));
    }
    else
    {
        line ("Modal persistence:", fixedText (analysis.modalPersistence, 4));
        line ("Modal movement:", fixedText (analysis.modalMovementHz, 3) + " Hz");
        out << "  Persistence counts a peak as surviving if the next frame has one within\n";
        out << "  +/- 4 Hz, so a mode being moved a few Hz still counts as persistent.\n";
        out << "  Movement is how far the survivors actually went; read the two together.\n";
        line ("Late-tail periodicity:", fixedText (analysis.latePeriodicity, 2));
        line ("Peak lifetime:", fixedText (analysis.peakLifetimeSeconds, 3) + " s");
        line ("Spectral flux:", fixedText (analysis.spectralFlux, 4));
    }

    out << "\nTail\n----\n";
    line ("Silence at end:", fixedText (analysis.silenceTailSeconds, 3) + " s");
    line ("Decays to silence:", analysis.tailDecaysToSilence ? "yes" : "no");

    return out.str();
}

std::string buildAnalysisJson (const DiagnosticAnalysis& analysis, const std::string& sourceName)
{
    using av::diag::jsonNumber;
    using av::diag::jsonString;

    std::string j = "{\n";

    j += "  \"schema_version\": " + std::to_string (av::diag::schemaVersion) + ",\n";
    j += "  \"source\": " + jsonString (sourceName) + ",\n";
    j += "  \"valid\": " + std::string (analysis.valid ? "true" : "false") + ",\n";
    j += "  \"error\": " + jsonString (analysis.error) + ",\n";

    j += "  \"file\": {\n";
    j += "    \"sample_rate\": " + jsonNumber (analysis.sampleRate, 1) + ",\n";
    j += "    \"channels\": " + std::to_string (analysis.channels) + ",\n";
    j += "    \"frames\": " + std::to_string (analysis.frames) + ",\n";
    j += "    \"duration_seconds\": " + jsonNumber (analysis.durationSeconds, 4) + "\n";
    j += "  },\n";

    j += "  \"levels\": {\n";
    j += "    \"peak_left\": " + jsonNumber (analysis.peakLeft, 6) + ",\n";
    j += "    \"peak_right\": " + jsonNumber (analysis.peakRight, 6) + ",\n";
    j += "    \"true_peak\": " + jsonNumber (analysis.truePeak, 6) + ",\n";
    j += "    \"rms_left\": " + jsonNumber (analysis.rmsLeft, 6) + ",\n";
    j += "    \"rms_right\": " + jsonNumber (analysis.rmsRight, 6) + ",\n";
    j += "    \"dc_left\": " + jsonNumber (analysis.dcLeft, 8) + ",\n";
    j += "    \"dc_right\": " + jsonNumber (analysis.dcRight, 8) + ",\n";
    j += "    \"clipped_samples\": " + std::to_string (analysis.clippedSamples) + "\n";
    j += "  },\n";

    j += "  \"stereo\": {\n";
    j += "    \"correlation\": " + jsonNumber (analysis.correlation, 6) + ",\n";
    j += "    \"mono_downmix_db\": " + jsonNumber (analysis.monoDownmixDb, 4) + ",\n";
    j += "    \"worst_third_octave_db\": " + jsonNumber (analysis.worstThirdOctaveDb, 4) + "\n";
    j += "  },\n";

    j += "  \"decay\": {\n";
    j += "    \"measurability\": " + jsonString (describe (analysis.decayMeasurability)) + ",\n";
    j += "    \"rt60_broadband\": " + jsonNumber (analysis.rt60Broadband, 4) + ",\n";
    j += "    \"rt60_low\": " + jsonNumber (analysis.rt60Low, 4) + ",\n";
    j += "    \"rt60_mid\": " + jsonNumber (analysis.rt60Mid, 4) + ",\n";
    j += "    \"rt60_high\": " + jsonNumber (analysis.rt60High, 4) + ",\n";
    j += "    \"t20\": " + jsonNumber (analysis.t20, 4) + ",\n";
    j += "    \"t30\": " + jsonNumber (analysis.t30, 4) + ",\n";
    j += "    \"linearity\": " + jsonNumber (analysis.decayLinearity, 5) + ",\n";
    j += "    \"linearity_low\": " + jsonNumber (analysis.decayLinearityLow, 5) + ",\n";
    j += "    \"linearity_mid\": " + jsonNumber (analysis.decayLinearityMid, 5) + ",\n";
    j += "    \"linearity_high\": " + jsonNumber (analysis.decayLinearityHigh, 5) + ",\n";
    j += "    \"early_decay_time\": " + jsonNumber (analysis.earlyDecayTime, 4) + "\n";
    j += "  },\n";

    j += "  \"movement\": {\n";
    j += "    \"measurability\": " + jsonString (describe (analysis.movementMeasurability)) + ",\n";
    j += "    \"modal_persistence\": " + jsonNumber (analysis.modalPersistence, 5) + ",\n";
    j += "    \"modal_persistence_tolerance_hz\": 4.0,\n";
    j += "    \"modal_movement_hz\": " + jsonNumber (analysis.modalMovementHz, 5) + ",\n";
    j += "    \"late_periodicity\": " + jsonNumber (analysis.latePeriodicity, 4) + ",\n";
    j += "    \"peak_lifetime_seconds\": " + jsonNumber (analysis.peakLifetimeSeconds, 4) + ",\n";
    j += "    \"spectral_flux\": " + jsonNumber (analysis.spectralFlux, 5) + "\n";
    j += "  },\n";

    j += "  \"tail\": {\n";
    j += "    \"silence_seconds\": " + jsonNumber (analysis.silenceTailSeconds, 4) + ",\n";
    j += "    \"decays_to_silence\": "
         + std::string (analysis.tailDecaysToSilence ? "true" : "false") + "\n";
    j += "  }\n";

    j += "}\n";

    return j;
}

int analyseSessionFolder (const std::string& folder)
{
    using av::diag::paths::join;

    const std::string outputPath = join (folder, "output.wav");

    std::ifstream probe (outputPath, std::ios::binary);

    if (! probe.is_open())
        return 3;

    probe.close();

    const auto analysis = analyseCapture (outputPath, false);

    if (! analysis.valid)
        return 3;

    {
        std::ofstream json (join (folder, "analysis.json"), std::ios::binary | std::ios::trunc);

        if (! json.is_open())
            return 3;

        json << buildAnalysisJson (analysis, "output.wav");
    }

    {
        std::ofstream text (join (folder, "analysis.txt"), std::ios::binary | std::ios::trunc);

        if (! text.is_open())
            return 3;

        text << buildAnalysisText (analysis, "output.wav");
    }

    // Technical rules only, matching the live layer's.
    if (analysis.clippedSamples > 0 || analysis.truePeak > 1.0)
        return 1;

    if (analysis.worstThirdOctaveDb > av::diag::thresholds::monoCancellationWarningDb)
        return 1;

    return 0;
}

int analyseCaptureToFolder (const std::string& wavPath, const std::string& outputFolder,
                            bool impulsive)
{
    using av::diag::paths::join;

    std::ifstream probe (wavPath, std::ios::binary);

    if (! probe.is_open())
        return 3;

    probe.close();

    // The same analyseCapture the folder entry point calls. The only
    // difference is that `impulsive` is passed in rather than assumed false,
    // so a capture that really is one impulse into silence can be measured for
    // decay instead of being refused.
    const auto analysis = analyseCapture (wavPath, impulsive);

    if (! analysis.valid)
        return 3;

    const std::string label = wavPath.substr (wavPath.find_last_of ("/\\") + 1);

    {
        std::ofstream json (join (outputFolder, "analysis.json"), std::ios::binary | std::ios::trunc);

        if (! json.is_open())
            return 3;

        json << buildAnalysisJson (analysis, label);
    }

    {
        std::ofstream text (join (outputFolder, "analysis.txt"), std::ios::binary | std::ios::trunc);

        if (! text.is_open())
            return 3;

        text << buildAnalysisText (analysis, label);
    }

    if (analysis.clippedSamples > 0 || analysis.truePeak > 1.0)
        return 1;

    if (analysis.worstThirdOctaveDb > av::diag::thresholds::monoCancellationWarningDb)
        return 1;

    return 0;
}

} // namespace avtest
