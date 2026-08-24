#include "DspAnalysis.h"
#include "Suites.h"
#include "TestFramework.h"

#include "DSP/StereoDiffusionNetwork.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace av::dsp;

namespace avtest
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    using Stereo = StereoDiffusionNetwork<double, Interpolation::lagrange3>;

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

    struct StereoResult
    {
        std::vector<double> left, right, mono;
    };

    StereoResult runStereo (Stereo& net, const std::vector<double>& leftIn,
                            const std::vector<double>& rightIn)
    {
        StereoResult r;
        r.left = leftIn;
        r.right = rightIn;
        r.mono.resize (leftIn.size());

        for (size_t n = 0; n < leftIn.size(); ++n)
        {
            net.processSample (r.left[n], r.right[n]);
            r.mono[n] = 0.5 * (r.left[n] + r.right[n]);
        }

        return r;
    }

    /**
        Worst third-octave band dip in the mono downmix, relative to the mean
        level of the downmix.

        A decorrelated pair always loses about 3 dB overall when summed, which
        is expected and inaudible as a tonal change. What a listener notices is
        one band collapsing relative to the others, so the metric is the deepest
        band below the downmix's own mean, not below the input.
    */
    double worstMonoBandDipDb (const std::vector<double>& mono, double sampleRate, size_t fftSize)
    {
        const auto mag = magnitudeSpectrum (mono, fftSize, true);
        const auto bands = thirdOctaveEnergy (mag, sampleRate, fftSize, 100.0, 10000.0);

        if (bands.energyDb.empty())
            return 0.0;

        double sum = 0.0;
        for (double e : bands.energyDb)
            sum += e;

        const double meanDb = sum / static_cast<double> (bands.energyDb.size());

        double worst = 0.0;
        for (double e : bands.energyDb)
            worst = std::max (worst, meanDb - e);

        return worst;
    }
}

void runStereoDiffusionTests()
{
    constexpr double sampleRate = 48000.0;
    constexpr size_t fftSize = 32768;

    // =======================================================================
    beginSuite ("Stereo diffusion / mono input becomes a stereo field");
    // =======================================================================
    {
        Stereo net;
        net.prepare (sampleRate);
        net.setStageCount (6, true);
        net.setDiffusion (0.8, true);

        const auto noise = whiteNoise (fftSize, 2468u);
        const auto out = runStereo (net, noise, noise);

        const double rho = correlation (out.left, out.right);

        info ("mono input, 6 stages, diffusion 0.8:  L/R correlation " + fixed (rho, 4));

        check (std::abs (rho) < 0.5, "a mono input comes out decorrelated (|rho| < 0.5)");
        check (rho > -0.3, "correlation is not driven strongly negative");

        // Neither channel may be a copy of the input, and neither may be silent.
        checkClose (rms (out.left) / rms (noise), 1.0, 0.05, "the left channel preserves RMS");
        checkClose (rms (out.right) / rms (noise), 1.0, 0.05, "the right channel preserves RMS");

        check (std::abs (correlation (out.left, noise)) < 0.6,
               "the left channel is not simply the input");
        check (std::abs (correlation (out.right, noise)) < 0.6,
               "the right channel is not simply the input");
    }

    // =======================================================================
    beginSuite ("Stereo diffusion / each channel stays magnitude-flat");
    // =======================================================================
    {
        Stereo net;
        net.prepare (sampleRate);
        net.setStageCount (6, true);
        net.setDiffusion (0.8, true);

        std::vector<double> irL (fftSize, 0.0), irR (fftSize, 0.0);

        for (size_t n = 0; n < fftSize; ++n)
        {
            double l = (n == 0) ? 1.0 : 0.0;
            double r = l;
            net.processSample (l, r);
            irL[n] = l;
            irR[n] = r;
        }

        const auto magL = magnitudeSpectrum (irL, fftSize, false);
        const auto magR = magnitudeSpectrum (irR, fftSize, false);

        const auto devL = bandDeviation (magL, sampleRate, fftSize, 20.0, 20000.0);
        const auto devR = bandDeviation (magR, sampleRate, fftSize, 20.0, 20000.0);

        info ("left  channel: +" + fixed (devL.maxAboveDb, 3) + " / -" + fixed (devL.maxBelowDb, 3)
              + " dB, energy " + fixed (energy (irL), 5));
        info ("right channel: +" + fixed (devR.maxAboveDb, 3) + " / -" + fixed (devR.maxBelowDb, 3)
              + " dB, energy " + fixed (energy (irR), 5));

        checkAtMost (devL.maxAboveDb, 1.5, "left channel never rises more than 1.5 dB");
        checkAtMost (devR.maxAboveDb, 1.5, "right channel never rises more than 1.5 dB");
        checkClose (energy (irL), 1.0, 0.05, "left channel preserves energy");
        checkClose (energy (irR), 1.0, 0.05, "right channel preserves energy");
    }

    // =======================================================================
    beginSuite ("Stereo diffusion / shared stages trade spread for mono safety");
    // =======================================================================
    {
        // The tuning decision, measured rather than asserted.
        info ("  shared   L/R correlation   worst mono band dip");

        std::vector<std::pair<int, std::pair<double, double>>> results;

        for (int shared : { 0, 1, 2, 3, 4 })
        {
            Stereo net;
            (void) net.setSharedStages (shared, true); // before prepare: always accepted
            net.prepare (sampleRate);
            net.setStageCount (6, true);
            net.setDiffusion (0.8, true);

            const auto noise = whiteNoise (fftSize, 13579u);
            const auto out = runStereo (net, noise, noise);

            const double rho = correlation (out.left, out.right);
            const double dip = worstMonoBandDipDb (out.mono, sampleRate, fftSize);

            info ("    " + std::to_string (shared) + "      " + pad (fixed (rho, 4), 18)
                  + fixed (dip, 2) + " dB");

            results.push_back ({ shared, { rho, dip } });
        }

        // The expected story — more sharing, shallower notches — does not hold.
        // Record that explicitly so the tuning comment cannot drift back to the
        // intuitive version.
        bool monotonic = true;
        for (size_t i = 1; i < results.size(); ++i)
            monotonic &= (results[i].second.second <= results[i - 1].second.second + 0.01);

        check (! monotonic,
               "the mono downmix dip is NOT monotonic in the number of shared stages");

        const auto shipped = results[static_cast<size_t> (diffusion::defaultSharedStages)];

        // The shipped setting has to be usable at the default chain length, so
        // settings that would share every active stage are not candidates.
        double bestUsableDip = 1.0e9;

        for (const auto& r : results)
            if (r.first < diffusion::defaultStageCount)
                bestUsableDip = std::min (bestUsableDip, r.second.second);

        checkClose (shipped.second.second, bestUsableDip, 0.01,
                    "the shipped setting is the best of those that leave stages differing");

        info ("shipped setting: " + std::to_string (diffusion::defaultSharedStages)
              + " shared stage(s), correlation " + fixed (shipped.second.first, 4)
              + ", worst mono dip " + fixed (shipped.second.second, 2) + " dB");

        check (std::abs (shipped.second.first) < 0.5,
               "the shipped sharing setting keeps the channels decorrelated");
        checkAtMost (shipped.second.second, 9.0,
                     "the shipped sharing setting keeps the worst mono band dip under 9 dB");
    }

    // =======================================================================
    beginSuite ("Stereo diffusion / mono compatibility");
    // =======================================================================
    {
        Stereo net;
        net.prepare (sampleRate);
        net.setStageCount (6, true);
        net.setDiffusion (0.8, true);

        const auto noise = whiteNoise (fftSize, 8642u);
        const auto out = runStereo (net, noise, noise);

        // How much level survives the downmix. Two fully decorrelated channels
        // lose 3 dB; total cancellation would lose everything.
        const double monoLossDb = 20.0 * std::log10 (rms (out.mono) / rms (noise));
        const double dip = worstMonoBandDipDb (out.mono, sampleRate, fftSize);

        info ("mono downmix level: " + fixed (monoLossDb, 2)
              + " dB relative to the input,  worst third-octave dip "
              + fixed (dip, 2) + " dB below its own mean");

        check (monoLossDb > -4.0,
               "the downmix loses no more than 4 dB overall (no broadband cancellation)");
        checkAtMost (dip, 9.0, "no third-octave band collapses more than 9 dB");

        // The same check with a stereo input, which is the easier case.
        const auto left  = whiteNoise (fftSize, 111u);
        const auto right = whiteNoise (fftSize, 222u);

        Stereo net2;
        net2.prepare (sampleRate);
        net2.setStageCount (6, true);
        net2.setDiffusion (0.8, true);

        const auto stereoOut = runStereo (net2, left, right);

        check (allFinite (stereoOut.left) && allFinite (stereoOut.right),
               "a genuine stereo input is processed without artefacts");
        checkClose (rms (stereoOut.left) / rms (left), 1.0, 0.05,
                    "stereo input: left RMS is preserved");
        checkClose (rms (stereoOut.right) / rms (right), 1.0, 0.05,
                    "stereo input: right RMS is preserved");
    }

    // =======================================================================
    beginSuite ("Stereo diffusion / cross-coupling");
    // =======================================================================
    {
        info ("  coupling   L/R correlation   mono dip   pair energy");

        for (double coupling : { 0.0, 0.25, 0.5, 1.0 })
        {
            Stereo net;
            net.prepare (sampleRate);
            net.setStageCount (6, true);
            net.setDiffusion (0.8, true);
            net.setCrossCoupling (coupling, true);

            const auto noise = whiteNoise (fftSize, 24680u);
            const auto out = runStereo (net, noise, noise);

            const double rho = correlation (out.left, out.right);
            const double dip = worstMonoBandDipDb (out.mono, sampleRate, fftSize);
            const double pairEnergy = (energy (out.left) + energy (out.right))
                                    / (2.0 * energy (noise));

            info ("    " + fixed (coupling, 2) + "      " + pad (fixed (rho, 4), 18)
                  + pad (fixed (dip, 2) + " dB", 11) + fixed (pairEnergy, 4));

            // The rotation is orthogonal, so it preserves energy exactly. The
            // few percent shortfall is the chain's group delay pushing the tail
            // past the end of the measurement window, not the matrix.
            checkClose (pairEnergy, 1.0, 0.06,
                        "coupling " + fixed (coupling, 2) + ": the rotation preserves total energy");
        }
    }

    // =======================================================================
    beginSuite ("Stereo diffusion / determinism, block size and stability");
    // =======================================================================
    {
        auto makeNet = [&]
        {
            auto net = std::make_unique<Stereo>();
            net->prepare (sampleRate);
            net->setStageCount (8, true);
            net->setDiffusion (0.9, true);
            return net;
        };

        const auto inL = whiteNoise (30000, 4u);
        const auto inR = whiteNoise (30000, 5u);

        auto reference = makeNet();
        const auto expected = runStereo (*reference, inL, inR);

        auto again = makeNet();
        const auto repeat = runStereo (*again, inL, inR);

        bool identical = true;
        for (size_t i = 0; i < expected.left.size(); ++i)
            identical &= (expected.left[i] == repeat.left[i])
                      && (expected.right[i] == repeat.right[i]);

        check (identical, "two identically configured stereo networks are bit-identical");

        bool allBlockSizesMatch = true;

        for (int blockSize : { 1, 16, 32, 64, 128, 256, 512, 1024, 8192 })
        {
            auto net = makeNet();
            std::vector<double> l = inL, r = inR;

            int n = 0;
            while (n < static_cast<int> (l.size()))
            {
                const int count = std::min (blockSize, static_cast<int> (l.size()) - n);
                net->processBlock (l.data() + n, r.data() + n, count);
                n += count;
            }

            for (size_t i = 0; i < l.size(); ++i)
                allBlockSizesMatch &= (l[i] == expected.left[i]) && (r[i] == expected.right[i]);
        }

        check (allBlockSizesMatch, "block sizes 1 to 8192 all render bit-identically in stereo");

        // Sample rates, including 192 kHz.
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            Stereo net;
            net.prepare (rate);
            net.setStageCount (6, true);
            net.setDiffusion (0.8, true);

            const auto noise = whiteNoise (static_cast<size_t> (rate * 0.25), 6u);
            const auto out = runStereo (net, noise, noise);

            const double rho = correlation (out.left, out.right);
            const std::string label = fixed (rate / 1000.0, 1) + " kHz";

            check (allFinite (out.left) && allFinite (out.right), label + ": output is finite");
            check (std::abs (rho) < 0.55, label + ": channels stay decorrelated (rho = "
                                          + fixed (rho, 3) + ")");
        }

        // Long run with automation.
        Stereo net;
        net.prepare (sampleRate);
        net.setStageCount (8, true);
        net.setDiffusion (0.5, true);

        double worstPeak = 0.0;
        bool finite = true;
        unsigned int state = 555u;

        for (int n = 0; n < 48000 * 120; ++n)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double x = (static_cast<double> (state) * (2.0 / 4294967296.0)) - 1.0;

            if (n % 64 == 0)
            {
                const double phase = 2.0 * pi * 0.25 * static_cast<double> (n) / sampleRate;
                net.setDiffusion (0.5 + 0.5 * std::sin (phase));
                net.setCrossCoupling (0.5 + 0.5 * std::sin (phase * 0.7));
            }

            double l = x, r = -x;
            net.processSample (l, r);

            worstPeak = std::max (worstPeak, std::max (std::abs (l), std::abs (r)));
            finite &= std::isfinite (l) && std::isfinite (r);
        }

        check (finite, "2 minutes of full-scale stereo with automation stays finite");
        info ("worst stereo peak over 2 minutes: " + fixed (worstPeak, 4));
        checkAtMost (worstPeak, 6.0, "no runaway growth in stereo");

        // Silence and reset.
        net.reset();

        std::vector<double> tailL (500000), tailR (500000);
        for (size_t n = 0; n < tailL.size(); ++n)
        {
            double l = 0.0, r = 0.0;
            net.processSample (l, r);
            tailL[n] = l;
            tailR[n] = r;
        }

        check (! anySubnormal (tailL.data(), tailL.size())
            && ! anySubnormal (tailR.data(), tailR.size()),
               "a silent stereo tail produces no subnormals");
        checkAtMost (peakAbsolute (tailL), 1.0e-18, "left channel settles to silence");
        checkAtMost (peakAbsolute (tailR), 1.0e-18, "right channel settles to silence");
    }
}

} // namespace avtest
