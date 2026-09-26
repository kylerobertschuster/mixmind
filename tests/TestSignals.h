#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

// Deterministic test signals + small helpers shared by the test files.
namespace TestSignals
{
    inline float dbToGain (double db) { return (float) std::pow (10.0, db / 20.0); }

    inline std::vector<float> sine (double fs, double hz, double amplitude, double seconds, double phase = 0.0)
    {
        std::vector<float> v ((size_t) std::llround (fs * seconds));
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (amplitude * std::sin (w * (double) i + phase));
        return v;
    }

    inline std::vector<float> whiteNoise (double fs, double seconds, double rmsDb, juce::int64 seed)
    {
        juce::Random rng (seed);
        std::vector<float> v ((size_t) std::llround (fs * seconds));
        const float scale = dbToGain (rmsDb) * std::sqrt (3.0f);   // uniform ±a has RMS a/√3
        for (auto& x : v) x = (rng.nextFloat() * 2.0f - 1.0f) * scale;
        return v;
    }

    // Pink-ish noise (Paul Kellet's economy filter) — a music-like tilt.
    inline std::vector<float> pinkNoise (double fs, double seconds, double peakScale, juce::int64 seed)
    {
        juce::Random rng (seed);
        std::vector<float> v ((size_t) std::llround (fs * seconds));
        double b0 = 0, b1 = 0, b2 = 0;
        for (auto& x : v)
        {
            const double w = rng.nextFloat() * 2.0 - 1.0;
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            x = (float) ((b0 + b1 + b2 + w * 0.1848) * peakScale);
        }
        return v;
    }

    inline std::vector<float> concat (std::initializer_list<std::vector<float>> parts)
    {
        std::vector<float> out;
        for (const auto& p : parts) out.insert (out.end(), p.begin(), p.end());
        return out;
    }

    inline double rmsDb (const float* x, size_t n)
    {
        double s = 0.0;
        for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (juce::jmax (1.0e-30, s / (double) juce::jmax ((size_t) 1, n)));
    }

    // Writes a 24-bit WAV. `right` may be empty for a mono file.
    inline bool writeWav (const juce::File& file, const std::vector<float>& left,
                          const std::vector<float>& right, double fs)
    {
        file.deleteFile();
        const int channels = right.empty() ? 1 : 2;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr) return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), fs, (unsigned int) channels, 24, {}, 0));
        if (writer == nullptr) return false;
        stream.release();   // the writer owns it now

        const float* chans[2] = { left.data(), channels == 2 ? right.data() : nullptr };
        return writer->writeFromFloatArrays (chans, channels, (int) left.size());
    }

    // A temp file that deletes itself.
    struct TempWav
    {
        explicit TempWav (const juce::String& name)
            : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("mixmind-tests").getChildFile (name + ".wav"))
        {
            file.getParentDirectory().createDirectory();
        }
        ~TempWav() { file.deleteFile(); }
        juce::File file;
    };
}
