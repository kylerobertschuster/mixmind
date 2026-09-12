#include "ReferenceAnalyzer.h"
#include <cmath>
#include <vector>

ReferenceAnalyzer::ReferenceAnalyzer()
{
    // WAV / AIFF / FLAC / Ogg Vorbis. (MP3 needs a separate decoder flag — later.)
    formatManager.registerBasicFormats();
}

void ReferenceAnalyzer::clear()
{
    loaded = false;
    juce::zeromem (refBins, sizeof (refBins));
    lufs = -60.0f;
    stereoWidth = 0.5f;
    phaseCorr = 1.0f;
    peakDb = -180.0f;
    rmsDb = -180.0f;
    fileName = {};
}

bool ReferenceAnalyzer::loadFile (const juce::File& file, double liveSampleRate, juce::String& errorMessage)
{
    if (!file.existsAsFile())
    {
        errorMessage = "File not found.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        errorMessage = "Unsupported format (use WAV, AIFF, FLAC, Ogg, or MP3).";
        return false;
    }

    const int  channels   = juce::jmin (2, (int) reader->numChannels);
    const auto totalSamps = reader->lengthInSamples;

    // Cap at 10 minutes so a huge reference can't stall the UI thread.
    const auto maxSamples = (int) juce::jmin<juce::int64> (totalSamps, (juce::int64) 48000 * 60 * 10);
    if (maxSamples == 0)
    {
        errorMessage = "Empty file.";
        return false;
    }

    juce::AudioBuffer<float> buffer (channels, maxSamples);
    reader->read (&buffer, 0, maxSamples, 0, true, true);

    // ── Scalar telemetry from the stereo source ──────────────────────────
    double sumL2 = 0, sumR2 = 0, sumLR = 0, sumMid = 0, sumSide = 0;
    for (int i = 0; i < maxSamples; ++i)
    {
        const float l = buffer.getSample (0, i);
        const float r = channels > 1 ? buffer.getSample (1, i) : l;
        sumL2 += (double) l * l;
        sumR2 += (double) r * r;
        sumLR += (double) l * r;
        const float m = (l + r) * 0.5f, s = (l - r) * 0.5f;
        sumMid += (double) m * m;
        sumSide += (double) s * s;
    }

    const double denom = std::sqrt (sumL2 * sumR2);
    phaseCorr   = (denom > 1e-12) ? juce::jlimit (-1.0f, 1.0f, (float)(sumLR / denom)) : 1.0f;
    stereoWidth = (sumMid > 0) ? (float) std::sqrt (sumSide / sumMid) : 0.0f;

    // Honest BS.1770 integrated loudness + true peak (same meter as the live
    // analyzer, so REF and YOU readouts are directly comparable).
    {
        LoudnessMeter lm;
        lm.prepare (reader->sampleRate, 512);
        const int block = 512;
        for (int i = 0; i < maxSamples; i += block)
        {
            const int n = juce::jmin (block, maxSamples - i);
            lm.process (buffer.getReadPointer (0) + i,
                        channels > 1 ? buffer.getReadPointer (1) + i : buffer.getReadPointer (0) + i,
                        n);
        }
        lufs = lm.getIntegratedLufs();
        if (lufs <= -100.0f) lufs = lm.getShortTermLufs();   // too short for gating
        if (lufs <= -100.0f) lufs = lm.getMomentaryLufs();
        peakDb = lm.getTruePeakDb();
        rmsDb  = lm.getRmsDb();
    }

    // ── Mono mix for the spectrum ─────────────────────────────────────────
    std::vector<float> mono ((size_t) maxSamples);
    for (int i = 0; i < maxSamples; ++i)
    {
        float v = buffer.getSample (0, i);
        if (channels > 1) v = (v + buffer.getSample (1, i)) * 0.5f;
        mono[(size_t) i] = v;
    }

    // ── Resample to the live sample rate (linear) so bins align ──────────
    if (std::abs (reader->sampleRate - liveSampleRate) > 1.0)
    {
        const double ratio = reader->sampleRate / liveSampleRate;
        const size_t outN  = (size_t)(mono.size() / ratio);
        std::vector<float> resampled (outN);
        for (size_t i = 0; i < outN; ++i)
        {
            const double src = i * ratio;
            const size_t s0  = (size_t) src;
            const size_t s1  = juce::jmin (s0 + 1, mono.size() - 1);
            const float frac = (float)(src - s0);
            resampled[i] = mono[s0] + (mono[s1] - mono[s0]) * frac;
        }
        mono = std::move (resampled);
    }

    // ── Averaged magnitude spectrum (same normalization as AudioAnalyzer) ─
    juce::dsp::FFT fft (11); // 2048-point, matches AudioAnalyzer::fftSize
    juce::dsp::WindowingFunction<float> window (AudioAnalyzer::fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> accum (numBins, 0.0f);
    // performFrequencyOnlyForwardTransform writes 2 * fftSize floats, so the
    // buffer must be that long. Allocating only fftSize overran the stack by
    // 8 KB on every reference load. AudioAnalyzer's own buffers (fftDataL/R)
    // are sized 2 * fftSize for the same reason.
    float fftBuf[2 * AudioAnalyzer::fftSize];
    int frames = 0;

    for (size_t start = 0; start + AudioAnalyzer::fftSize <= mono.size(); start += AudioAnalyzer::fftSize / 2)
    {
        for (int i = 0; i < AudioAnalyzer::fftSize; ++i)
            fftBuf[i] = mono[start + i];

        window.multiplyWithWindowingTable (fftBuf, AudioAnalyzer::fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuf);

        for (int i = 0; i < numBins; ++i)
            accum[i] += fftBuf[i];

        ++frames;
    }

    if (frames > 0)
        for (int i = 0; i < numBins; ++i)
            refBins[i] = (accum[i] / frames) * (2.0f / (float) AudioAnalyzer::fftSize);
    else
        juce::zeromem (refBins, sizeof (refBins));

    loaded = true;
    fileName = file.getFileNameWithoutExtension();
    return true;
}
