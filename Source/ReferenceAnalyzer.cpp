#include "ReferenceAnalyzer.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
    // Builds the same averaged Hann-windowed magnitude spectrum as
    // AudioAnalyzer (linear 0..1, 2048-pt FFT), resampling to `liveRate` if
    // the file's rate differs so bins line up with the live analyzer.
    void analyzeSpectrum (const std::vector<float>& src, double srcRate,
                          double liveRate, float* outBins)
    {
        const int numBins = ReferenceAnalyzer::numBins;
        juce::zeromem (outBins, sizeof (float) * (size_t) numBins);
        if (src.empty()) return;

        const std::vector<float>* mono = &src;
        std::vector<float> resampled;
        if (std::abs (srcRate - liveRate) > 1.0)
        {
            const double ratio = srcRate / liveRate;
            const size_t outN  = (size_t)(src.size() / ratio);
            if (outN == 0) return;
            resampled.resize (outN);
            for (size_t i = 0; i < outN; ++i)
            {
                const double s = i * ratio;
                const size_t s0 = (size_t) s;
                const size_t s1 = juce::jmin (s0 + 1, src.size() - 1);
                const float frac = (float)(s - s0);
                resampled[i] = src[s0] + (src[s1] - src[s0]) * frac;
            }
            mono = &resampled;
        }

        juce::dsp::FFT fft (11);   // 2048-point, matches AudioAnalyzer::fftSize
        juce::dsp::WindowingFunction<float> window (AudioAnalyzer::fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> accum ((size_t) numBins, 0.0f);
        float fftBuf[2 * AudioAnalyzer::fftSize];   // interleaved real/imag
        int frames = 0;

        for (size_t start = 0; start + AudioAnalyzer::fftSize <= mono->size();
             start += AudioAnalyzer::fftSize / 2)
        {
            for (int i = 0; i < AudioAnalyzer::fftSize; ++i)
                fftBuf[i] = (*mono)[start + i];
            for (int i = AudioAnalyzer::fftSize; i < 2 * AudioAnalyzer::fftSize; ++i)
                fftBuf[i] = 0.0f;

            window.multiplyWithWindowingTable (fftBuf, AudioAnalyzer::fftSize);
            fft.performFrequencyOnlyForwardTransform (fftBuf);

            for (int i = 0; i < numBins; ++i)
                accum[(size_t) i] += fftBuf[i];

            ++frames;
        }

        if (frames > 0)
            for (int i = 0; i < numBins; ++i)
                outBins[i] = (accum[(size_t) i] / frames) * (2.0f / (float) AudioAnalyzer::fftSize);
    }
}

ReferenceAnalyzer::ReferenceAnalyzer()
{
    // WAV / AIFF / FLAC / Ogg Vorbis. (MP3 needs a separate decoder flag — later.)
    formatManager.registerBasicFormats();
}

void ReferenceAnalyzer::clear()
{
    loaded = false;
    juce::zeromem (midBins,   sizeof (midBins));
    juce::zeromem (sideBins,  sizeof (sideBins));
    juce::zeromem (leftBins,  sizeof (leftBins));
    juce::zeromem (rightBins, sizeof (rightBins));
    lufs = -60.0f;
    stereoWidth = 0.5f;
    phaseCorr = 1.0f;
    peakDb = -180.0f;
    rmsDb = -180.0f;
    fileName = {};
}

void ReferenceAnalyzer::writeToTree (juce::ValueTree& tree) const
{
    tree.setProperty ("refLoaded", loaded, nullptr);

    if (! loaded)
        return;

    tree.setProperty ("refName",   fileName,       nullptr);
    tree.setProperty ("refLufs",   (double) lufs,  nullptr);
    tree.setProperty ("refWidth",  (double) stereoWidth, nullptr);
    tree.setProperty ("refPhase",  (double) phaseCorr,   nullptr);
    tree.setProperty ("refPeakDb", (double) peakDb,      nullptr);
    tree.setProperty ("refRmsDb",  (double) rmsDb,       nullptr);

    // All four spectra are stored: the channel mode is a parameter that can be
    // switched after the session is recalled, and without the file there is no
    // way to re-analyse the channels that were not selected at save time.
    const juce::MemoryBlock midBlob   (midBins,   sizeof (midBins));
    const juce::MemoryBlock sideBlob  (sideBins,  sizeof (sideBins));
    const juce::MemoryBlock leftBlob  (leftBins,  sizeof (leftBins));
    const juce::MemoryBlock rightBlob (rightBins, sizeof (rightBins));

    tree.setProperty ("refBins",      juce::var (midBlob),   nullptr);
    tree.setProperty ("refBinsSide",  juce::var (sideBlob),  nullptr);
    tree.setProperty ("refBinsLeft",  juce::var (leftBlob),  nullptr);
    tree.setProperty ("refBinsRight", juce::var (rightBlob), nullptr);
}

bool ReferenceAnalyzer::readFromTree (const juce::ValueTree& tree)
{
    clear();

    if (! tree.isValid() || ! (bool) tree.getProperty ("refLoaded", false))
        return false;

    const auto* bins = tree.getProperty ("refBins").getBinaryData();

    // A truncated or foreign session must fail closed to "no reference" rather
    // than half-populate the bins, which would quietly skew the match EQ.
    if (bins == nullptr || bins->getSize() != sizeof (midBins))
        return false;

    std::memcpy (midBins, bins->getData(), sizeof (midBins));

    // Side/Left/Right arrived with per-channel matching. A session written
    // before that carries only the downmix, so those modes fall back to it
    // rather than showing no reference curve at all.
    const auto copyChannel = [&tree] (const char* prop, float* dest, size_t bytes)
    {
        const auto* blob = tree.getProperty (prop).getBinaryData();

        if (blob != nullptr && blob->getSize() == bytes)
            std::memcpy (dest, blob->getData(), bytes);
    };

    std::memcpy (sideBins,  midBins, sizeof (midBins));
    std::memcpy (leftBins,  midBins, sizeof (midBins));
    std::memcpy (rightBins, midBins, sizeof (midBins));

    copyChannel ("refBinsSide",  sideBins,  sizeof (sideBins));
    copyChannel ("refBinsLeft",  leftBins,  sizeof (leftBins));
    copyChannel ("refBinsRight", rightBins, sizeof (rightBins));

    fileName    = tree.getProperty ("refName", "").toString();
    lufs        = (float) (double) tree.getProperty ("refLufs",   -60.0);
    stereoWidth = (float) (double) tree.getProperty ("refWidth",   0.5);
    phaseCorr   = (float) (double) tree.getProperty ("refPhase",   1.0);
    peakDb      = (float) (double) tree.getProperty ("refPeakDb", -180.0);
    rmsDb       = (float) (double) tree.getProperty ("refRmsDb",  -180.0);

    loaded = true;
    return true;
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

    // ── Per-channel analysis spectra (Stereo/Mid share the downmix) ──────
    // Built one at a time into `mono` so a long reference doesn't allocate
    // four full-length buffers at once.
    std::vector<float> mono ((size_t) maxSamples);

    auto fillMono = [&] (ChannelMode m)
    {
        for (int i = 0; i < maxSamples; ++i)
        {
            const float l = buffer.getSample (0, i);
            const float r = channels > 1 ? buffer.getSample (1, i) : l;
            mono[(size_t) i] = channelAnalysisSample (m, l, r);
        }
    };

    // ── Per-channel analysis spectra (Stereo/Mid share the downmix) ──────
    // Each is built into `mono` and analysed one at a time so a long reference
    // doesn't allocate four full-length buffers at once. analyzeSpectrum()
    // resamples to the live rate, so the bins line up with AudioAnalyzer.
    fillMono (ChannelMode::Mid);
    analyzeSpectrum (mono, reader->sampleRate, liveSampleRate, midBins);
    fillMono (ChannelMode::Side);
    analyzeSpectrum (mono, reader->sampleRate, liveSampleRate, sideBins);
    fillMono (ChannelMode::Left);
    analyzeSpectrum (mono, reader->sampleRate, liveSampleRate, leftBins);
    fillMono (ChannelMode::Right);
    analyzeSpectrum (mono, reader->sampleRate, liveSampleRate, rightBins);

    loaded = true;
    fileName = file.getFileNameWithoutExtension();
    return true;
}
