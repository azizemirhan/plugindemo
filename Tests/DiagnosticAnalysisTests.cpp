#include "DiagnosticAnalyzer.h"
#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/Denormal.h"
#include "DSP/ModulationArchitecture.h"
#include "DSP/MorphingStereoFdn.h"
#include "DSP/ParameterMapping.h"

#include "Diagnostics/DiagnosticPaths.h"
#include "Diagnostics/DiagnosticWav.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace av::dsp;
using namespace av::diag;

namespace avtest
{

namespace
{
    constexpr double referenceRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    std::string fixed (double v, int decimals)
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (decimals);
        s << v;
        return s.str();
    }

    std::string pad (const std::string& s, size_t width)
    {
        return s.size() >= width ? s : s + std::string (width - s.size(), ' ');
    }

    struct TemporaryDirectory
    {
        std::string path;

        explicit TemporaryDirectory (const std::string& name)
        {
            const auto base = std::filesystem::temp_directory_path() / "auroraverb-tests";
            path = (base / name).string();

            std::error_code error;
            std::filesystem::remove_all (path, error);
            std::filesystem::create_directories (path, error);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all (path, error);
        }
    };

    /** Renders the shipped preview chain and writes it as a capture would. */
    bool renderCapture (const std::string& path, const std::vector<double>& source,
                        double decaySeconds, double tailSeconds)
    {
        ScopedFtz ftz;

        auto tank = std::make_unique<MorphingStereoFdn<fdn::order>>();
        tank->prepare (referenceRate);
        tank->setInputDiffusionEnabled (true);
        tank->setAllpassDepth (0.0, true);
        tank->setModulationDepth (0.0, true);
        tank->setCompensationEnabled (true, true);
        tank->setRt60Seconds (mapping::rt60For (decaySeconds), true);
        tank->setSize (mapping::sizeScaleFor (0.60));

        architecture::applyWithDepth (*tank, architecture::defaultCandidate,
                                      mapping::modDepthFor (0.30), 1.0, true);

        tank->reset();

        const double trim = mapping::previewWetTrim (mapping::rt60For (decaySeconds),
                                                     mapping::sizeScaleFor (0.60));

        const auto total = source.size() + static_cast<size_t> (tailSeconds * referenceRate);

        std::vector<float> interleaved (total * 2);

        for (size_t n = 0; n < total; ++n)
        {
            double l = n < source.size() ? source[n] : 0.0;
            double r = l;

            tank->processSample (l, r);

            interleaved[n * 2] = static_cast<float> (l * trim);
            interleaved[n * 2 + 1] = static_cast<float> (r * trim);
        }

        WavWriter writer;

        if (! writer.open (path, referenceRate, 2))
            return false;

        if (! writer.writeFrames (interleaved.data(), total))
            return false;

        return writer.close();
    }
}

void runDiagnosticAnalysisTests()
{
    // =======================================================================
    beginSuite ("Diagnostics / offline analysis of a real capture");
    // =======================================================================
    {
        TemporaryDirectory temporary ("analysis");

        // An impulse through the shipped chain, captured exactly the way a
        // session would capture it, then analysed the way the session analyser
        // would analyse it.
        std::vector<double> impulseSource (16, 0.0);
        impulseSource[0] = 1.0;

        const std::string path = paths::join (temporary.path, "impulse.wav");

        check (renderCapture (path, impulseSource, 2.0, 5.0),
               "the preview chain renders and captures an impulse response");

        const auto analysis = analyseCapture (path, true);

        check (analysis.valid, "the capture analyses");

        info ("");
        info ("  " + pad ("duration", 26) + fixed (analysis.durationSeconds, 3) + " s");
        info ("  " + pad ("peak L / R", 26) + fixed (analysis.peakLeft, 4) + " / "
              + fixed (analysis.peakRight, 4));
        info ("  " + pad ("true peak", 26) + fixed (analysis.truePeak, 4));
        info ("  " + pad ("RMS L / R", 26) + fixed (analysis.rmsLeft, 6) + " / "
              + fixed (analysis.rmsRight, 6));
        info ("  " + pad ("DC L / R", 26) + fixed (analysis.dcLeft, 8) + " / "
              + fixed (analysis.dcRight, 8));
        info ("  " + pad ("clipped samples", 26) + std::to_string (analysis.clippedSamples));
        info ("  " + pad ("correlation", 26) + fixed (analysis.correlation, 4));
        info ("  " + pad ("mono downmix", 26) + fixed (analysis.monoDownmixDb, 2) + " dB");
        info ("  " + pad ("worst 3rd-octave dip", 26) + fixed (analysis.worstThirdOctaveDb, 2) + " dB");
        info ("  " + pad ("RT60 broadband", 26) + fixed (analysis.rt60Broadband, 3) + " s");
        info ("  " + pad ("RT60 low / mid / high", 26) + fixed (analysis.rt60Low, 3) + " / "
              + fixed (analysis.rt60Mid, 3) + " / " + fixed (analysis.rt60High, 3) + " s");
        info ("  " + pad ("T20 / T30", 26) + fixed (analysis.t20, 3) + " / "
              + fixed (analysis.t30, 3) + " s");
        info ("  " + pad ("decay linearity", 26) + fixed (analysis.decayLinearity, 4));
        info ("  " + pad ("modal persistence", 26) + fixed (analysis.modalPersistence, 4));
        info ("  " + pad ("late-tail periodicity", 26) + fixed (analysis.latePeriodicity, 2));
        info ("  " + pad ("peak lifetime", 26) + fixed (analysis.peakLifetimeSeconds, 3) + " s");
        info ("  " + pad ("spectral flux", 26) + fixed (analysis.spectralFlux, 4));
        info ("  " + pad ("silence tail", 26) + fixed (analysis.silenceTailSeconds, 3) + " s");

        // The measurements have to agree with what the offline DSP suites
        // measured on the same engine, which is the point of reusing them.
        check (analysis.decayMeasurability == Measurability::measured,
               "an impulse capture is recognised as measurable");

        checkAtMost (100.0 * std::abs (analysis.rt60Broadband - 2.0) / 2.0, 6.0,
                     "RT60 measured through a capture agrees with the requested decay");

        check (analysis.decayLinearity > 0.98, "the decay is a straight line in dB");

        check (analysis.movementMeasurability == Measurability::measured,
               "the movement metrics are measurable from this capture");

        checkAtMost (analysis.latePeriodicity, 30.0,
                     "late-tail periodicity through a capture matches the PHASE 5B target");

        checkAtMost (analysis.modalPersistence, 0.75,
                     "and so does modal persistence");

        checkAtMost (analysis.worstThirdOctaveDb, 4.0,
                     "mono cancellation stays inside the 4 dB the DSP phases were held to");

        check (analysis.truePeak >= std::max (analysis.peakLeft, analysis.peakRight),
               "true peak is at or above sample peak, as inter-sample peaks require");

        checkAtMost (analysis.truePeak, 1.0,
                     "a full-scale impulse does not clip, measured with inter-sample peaks");

        // The invariant above is satisfied by construction, so on its own it
        // would not prove the interpolation works. This does: a sine whose
        // period does not divide the sample interval has its real maxima
        // between samples, and a correct 4x reconstruction has to find them.
        {
            const auto length = static_cast<size_t> (referenceRate * 0.5);

            std::vector<double> offGrid (length);

            // Close to Nyquist/3, deliberately not a whole number of samples per
            // cycle, and phase-shifted so no sample lands on a crest.
            constexpr double toneHz = 7999.37;

            for (size_t n = 0; n < length; ++n)
                offGrid[n] = std::sin (2.0 * pi * toneHz * static_cast<double> (n)
                                       / referenceRate + 0.37);

            const std::string offGridPath = paths::join (temporary.path, "off-grid.wav");

            std::vector<float> interleaved (length * 2);

            for (size_t n = 0; n < length; ++n)
            {
                interleaved[n * 2] = static_cast<float> (offGrid[n]);
                interleaved[n * 2 + 1] = static_cast<float> (offGrid[n]);
            }

            WavWriter writer;
            check (writer.open (offGridPath, referenceRate, 2), "an off-grid tone is written");
            check (writer.writeFrames (interleaved.data(), length), "and its frames stored");
            check (writer.close(), "and the file closed");

            const auto offGridAnalysis = analyseCapture (offGridPath, false);

            info ("");
            info ("off-grid tone: sample peak " + fixed (offGridAnalysis.peakLeft, 6)
                  + ", true peak " + fixed (offGridAnalysis.truePeak, 6));

            check (offGridAnalysis.truePeak > offGridAnalysis.peakLeft,
                   "true peak finds a maximum between samples that sample peak misses");

            // The real maximum of a unit sine is 1, whatever the sampling.
            checkClose (offGridAnalysis.truePeak, 1.0, 0.01,
                        "and recovers the tone's real amplitude");
        }
    }

    // =======================================================================
    beginSuite ("Diagnostics / refusing to measure what cannot be measured");
    // =======================================================================
    {
        TemporaryDirectory temporary ("not-impulsive");

        // A sustained pad. Measuring RT60 from this would produce a confident
        // number that means nothing, which is worse than reporting nothing.
        const auto length = static_cast<size_t> (referenceRate * 6.0);

        std::vector<double> pad_ (length);

        for (size_t n = 0; n < length; ++n)
        {
            const double t = static_cast<double> (n) / referenceRate;

            pad_[n] = 0.2 * (std::sin (2.0 * pi * 110.0 * t)
                             + std::sin (2.0 * pi * 164.81 * t)
                             + std::sin (2.0 * pi * 220.0 * t)) / 3.0;
        }

        const std::string path = paths::join (temporary.path, "pad.wav");

        check (renderCapture (path, pad_, 2.0, 3.0), "a sustained pad renders and captures");

        const auto analysis = analyseCapture (path, false);

        check (analysis.valid, "the capture is readable");

        info ("decay measurability:    " + std::string (describe (analysis.decayMeasurability)));
        info ("movement measurability: " + std::string (describe (analysis.movementMeasurability)));

        check (analysis.decayMeasurability == Measurability::sourceNotImpulsive,
               "a sustained source is refused for RT60 rather than measured anyway");

        checkEqual (analysis.rt60Broadband, 0.0,
                    "and no decay figure is reported for it");

        // The level and stereo measurements are still valid, because they do
        // not depend on the source being impulsive.
        check (analysis.rmsLeft > 0.0, "level measurements still work on any source");
        check (analysis.truePeak > 0.0, "as does true peak");

        // The classifier itself.
        std::vector<double> impulse (static_cast<size_t> (referenceRate), 0.0);
        impulse[0] = 1.0;

        for (size_t n = 1; n < impulse.size(); ++n)
            impulse[n] = 0.3 * std::exp (-static_cast<double> (n) / 4000.0)
                         * std::sin (0.05 * static_cast<double> (n));

        check (looksImpulsive (impulse, referenceRate),
               "an impulse response is recognised as impulsive");

        check (! looksImpulsive (pad_, referenceRate),
               "a sustained pad is not");

        std::vector<double> noise = whiteNoise (static_cast<size_t> (referenceRate), 77u);

        check (! looksImpulsive (noise, referenceRate),
               "and neither is steady noise");
    }

    // =======================================================================
    beginSuite ("Diagnostics / session analyser end to end");
    // =======================================================================
    {
        TemporaryDirectory temporary ("session-analysis");

        // A session folder with a real capture in it.
        std::vector<double> impulseSource (16, 0.0);
        impulseSource[0] = 1.0;

        check (renderCapture (paths::join (temporary.path, "output.wav"),
                              impulseSource, 2.0, 5.0),
               "a session capture is written");

        const int code = analyseSessionFolder (temporary.path);

        info ("analyser exit code: " + std::to_string (code));

        check (code == 0 || code == 1,
               "the analyser reports PASS or PASS WITH WARNINGS for a healthy capture");

        check (std::filesystem::exists (paths::join (temporary.path, "analysis.json")),
               "analysis.json is written");
        check (std::filesystem::exists (paths::join (temporary.path, "analysis.txt")),
               "analysis.txt is written");

        // A folder with no capture must be reported as invalid rather than
        // analysed into a confident empty result.
        TemporaryDirectory empty ("session-analysis-empty");

        const int missingCode = analyseSessionFolder (empty.path);

        info ("exit code for a session with no capture: " + std::to_string (missingCode));

        checkEqual (static_cast<long long> (missingCode), 3LL,
                    "a session with no audio reports the missing-session code");

        checkEqual (static_cast<long long> (analyseSessionFolder ("Z:/no/such/folder")), 3LL,
                    "as does a folder that does not exist");
    }
}

} // namespace avtest
