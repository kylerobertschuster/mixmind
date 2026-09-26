#include "ReferenceAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

ReferenceAnalyzer::ReferenceAnalyzer()
{
    // WAV / AIFF / FLAC / Ogg Vorbis, MP3 (JUCE_USE_MP3AUDIOFORMAT), and the
    // CoreAudio formats (AAC / M4A / ALAC) on macOS.
    formatManager.registerBasicFormats();
}

bool ReferenceAnalyzer::canRead (const juce::File& file) const
{
    return formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

int ReferenceAnalyzer::fftSizeForRate (double sampleRate)
{
    // Keep bins ≈ 21.5–23.4 Hz wide whatever the file rate (2048 @ 44.1/48 k,
    // 4096 @ 88.2/96 k, 8192 @ 176.4/192 k).
    const int octaves = (int) std::lround (std::log2 (juce::jmax (1000.0, sampleRate) / 48000.0));
    return AudioAnalyzer::fftSize << juce::jlimit (0, 3, octaves);
}

bool ReferenceAnalyzer::analyse (const juce::File& file, Result& out, juce::String& error,
                                 const std::function<bool()>& shouldAbort)
{
    juce::ScopedNoDenormals noDenormals;
    out = {};

    if (! file.existsAsFile())
    {
        error = "File not found:\n" + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Couldn't decode \"" + file.getFileName() + "\".\nSupported: " + getWildcard();
        return false;
    }

    const double fs = reader->sampleRate;
    if (fs <= 0.0 || reader->numChannels == 0)
    {
        error = "\"" + file.getFileName() + "\" has no audio.";
        return false;
    }

    const bool stereo = reader->numChannels >= 2;
    const juce::int64 total = juce::jmin (reader->lengthInSamples, (juce::int64) (fs * kMaxSeconds));
    if (total <= 0)
    {
        error = "\"" + file.getFileName() + "\" is empty.";
        return false;
    }

    // ── Spectrum state (native rate) ─────────────────────────────────────
    const int N     = fftSizeForRate (fs);
    const int order = juce::roundToInt (std::log2 ((double) N));
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) N, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float>  ring ((size_t) N, 0.0f);
    std::vector<float>  frame ((size_t) (2 * N), 0.0f);
    std::vector<double> accum ((size_t) (N / 2), 0.0);
    int ringIdx = 0, hop = 0, frames = 0;
    bool ringFull = false;

    // ── Loudness, image, RMS ─────────────────────────────────────────────
    LoudnessMeter meter;
    constexpr int chunk = 65536;
    meter.prepare (fs, chunk);

    double sLL = 0, sRR = 0, sLR = 0, sMid = 0, sSide = 0, sRms = 0;

    juce::AudioBuffer<float> buf (stereo ? 2 : 1, chunk);

    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        if (shouldAbort && shouldAbort())
        {
            error = "Cancelled.";
            return false;
        }

        const int n = (int) juce::jmin ((juce::int64) chunk, total - pos);
        buf.clear();
        if (! reader->read (&buf, 0, n, pos, true, true))
        {
            error = "Read error in \"" + file.getFileName() + "\".";
            return false;
        }

        const float* L = buf.getReadPointer (0);
        const float* R = stereo ? buf.getReadPointer (1) : L;

        // A mono reference is measured as it would play on a stereo bus
        // (dual-mono), so its loudness compares directly with the live mix.
        meter.process (L, R, n);

        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = R[i];
            sLL += l * l; sRR += r * r; sLR += l * r;
            const double m = 0.5 * (l + r), s = 0.5 * (l - r);
            sMid += m * m; sSide += s * s;
            sRms += 0.5 * (l * l + r * r);

            ring[(size_t) ringIdx] = (float) m;
            if (++ringIdx == N) { ringIdx = 0; ringFull = true; }

            if (++hop < N / 2 || ! ringFull) continue;
            hop = 0;

            double meanSquare = 0.0;
            for (int k = 0; k < N; ++k)
            {
                const float v = ring[(size_t) ((ringIdx + k) % N)];
                frame[(size_t) k] = v;
                meanSquare += (double) v * v;
            }
            if (meanSquare / N <= 1.0e-7)   // same −70 dBFS gate as the live long-term
                continue;

            std::fill (frame.begin() + N, frame.end(), 0.0f);
            window.multiplyWithWindowingTable (frame.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (frame.data());
            for (size_t k = 0; k < accum.size(); ++k)
                accum[k] += frame[k];
            ++frames;
        }
    }

    if (frames == 0)
    {
        error = "\"" + file.getFileName() + "\" is silent or too short to analyse.";
        return false;
    }

    out.name            = file.getFileNameWithoutExtension();
    out.path            = file.getFullPathName();
    out.sampleRate      = fs;
    out.fftSize         = N;
    out.durationSeconds = (double) total / fs;

    out.spectrum.resize ((size_t) (N / 2));
    const double norm = 2.0 / ((double) N * frames);
    for (size_t k = 0; k < out.spectrum.size(); ++k)
        out.spectrum[k] = (float) (accum[k] * norm);

    out.lufs       = meter.getIntegratedLufs();   // kSilenceDb if under one 400 ms gating block
    out.truePeakDb = meter.getTruePeakDb();

    const double meanSq = sRms / (double) total;
    out.rmsDb = meanSq > 0.0 ? juce::jmax (LoudnessMeter::kSilenceDb, (float) (10.0 * std::log10 (meanSq)))
                             : LoudnessMeter::kSilenceDb;

    const double denom = std::sqrt (sLL * sRR);
    out.phaseCorr   = denom > 1e-12 ? juce::jlimit (-1.0f, 1.0f, (float) (sLR / denom)) : 1.0f;
    out.stereoWidth = sMid > 1e-12 ? (float) std::sqrt (sSide / sMid) : 0.0f;
    return true;
}

void ReferenceAnalyzer::mapToGrid (const Result& ref, double liveSampleRate, float* outBins, int numBins)
{
    std::fill (outBins, outBins + numBins, 0.0f);
    if (! ref.isValid() || liveSampleRate <= 0.0) return;

    const double liveBinHz = liveSampleRate / (double) AudioAnalyzer::fftSize;
    const double refBinHz  = ref.sampleRate / (double) ref.fftSize;
    const float  gain      = (float) std::sqrt (liveBinHz / refBinHz);
    const int    last      = (int) ref.spectrum.size() - 1;

    for (int i = 0; i < numBins; ++i)
    {
        const double p = (double) i * liveBinHz / refBinHz;
        if (p > (double) last) break;   // above the reference's Nyquist: no data

        const int    k0 = (int) p;
        const int    k1 = juce::jmin (k0 + 1, last);
        const float  t  = (float) (p - k0);
        const float  a  = ref.spectrum[(size_t) k0];
        const float  b  = ref.spectrum[(size_t) k1];
        outBins[i] = (a + (b - a) * t) * gain;
    }
}
