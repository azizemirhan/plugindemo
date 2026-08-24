#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace av::diag
{

/**
    A minimal 32-bit float WAV writer, shared by the diagnostic capture and the
    developer signal export.

    One copy, not two. The two callers want the same thing and a second
    implementation would be a second place for the header arithmetic to be
    wrong — and only one of them would be covered by the tests that check it.

    Float rather than integer PCM: a reverb tail spans a very wide dynamic
    range, the point of these files is to hear and measure what the DSP did, and
    float means nothing has to be normalised, dithered or clipped on the way
    out. The file holds exactly the samples that were produced, including any
    that went over full scale — which is the whole reason for capturing them.

    Never used from the audio thread. Opening and writing a file is exactly what
    the ring buffer exists to keep off it.
*/
class WavWriter
{
public:
    /** Opens the file and writes a placeholder header. */
    [[nodiscard]] bool open (const std::string& path, double sampleRate, int channels)
    {
        stream.open (path, std::ios::binary | std::ios::trunc);

        if (! stream.is_open())
            return false;

        rate = sampleRate;
        channelCount = channels;
        framesWritten = 0;

        writeHeader (0);
        return stream.good();
    }

    /** Appends interleaved float frames. */
    bool writeFrames (const float* interleaved, size_t frames)
    {
        if (! stream.is_open() || frames == 0)
            return stream.is_open();

        const size_t samples = frames * static_cast<size_t> (channelCount);

        for (size_t i = 0; i < samples; ++i)
        {
            uint32_t bits;
            const float value = interleaved[i];
            std::memcpy (&bits, &value, sizeof (bits));
            writeU32 (bits);
        }

        framesWritten += frames;
        return stream.good();
    }

    /** Rewrites the header with the real sizes and closes. */
    bool close()
    {
        if (! stream.is_open())
            return true;

        stream.seekp (0, std::ios::beg);
        writeHeader (framesWritten);

        const bool ok = stream.good();
        stream.close();
        return ok;
    }

    ~WavWriter() { close(); }

    [[nodiscard]] size_t getFramesWritten() const noexcept { return framesWritten; }

private:
    void writeU16 (uint16_t value)
    {
        const unsigned char bytes[2]
        {
            static_cast<unsigned char> (value & 0xff),
            static_cast<unsigned char> ((value >> 8) & 0xff),
        };

        stream.write (reinterpret_cast<const char*> (bytes), 2);
    }

    void writeU32 (uint32_t value)
    {
        const unsigned char bytes[4]
        {
            static_cast<unsigned char> (value & 0xff),
            static_cast<unsigned char> ((value >> 8) & 0xff),
            static_cast<unsigned char> ((value >> 16) & 0xff),
            static_cast<unsigned char> ((value >> 24) & 0xff),
        };

        stream.write (reinterpret_cast<const char*> (bytes), 4);
    }

    void writeHeader (size_t frames)
    {
        const uint16_t bitsPerSample = 32;
        const auto blockAlign = static_cast<uint16_t> (channelCount * bitsPerSample / 8);
        const auto dataBytes = static_cast<uint32_t> (frames * blockAlign);

        stream.write ("RIFF", 4);
        writeU32 (36 + dataBytes);
        stream.write ("WAVE", 4);

        stream.write ("fmt ", 4);
        writeU32 (16);
        writeU16 (3);                                  // IEEE float
        writeU16 (static_cast<uint16_t> (channelCount));
        writeU32 (static_cast<uint32_t> (rate));
        writeU32 (static_cast<uint32_t> (rate) * blockAlign);
        writeU16 (blockAlign);
        writeU16 (bitsPerSample);

        stream.write ("data", 4);
        writeU32 (dataBytes);
    }

    std::ofstream stream;
    double rate = 48000.0;
    int channelCount = 2;
    size_t framesWritten = 0;
};

} // namespace av::diag
