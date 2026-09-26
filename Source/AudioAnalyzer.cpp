#include "AudioAnalyzer.h"
#include <cmath>

AudioAnalyzer::AudioAnalyzer()
{
    float ones[fftSize];
    std::fill (ones, ones + fftSize, 1.0f);
    window.multiplyWithWindowingTable (ones, (size_t) fftSize);
    windowPower = 0.0f;
    for (float w : ones) windowPower += w * w;
}

void AudioAnalyzer::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    juce::zeromem (fifo, sizeof (fifo));
    juce::zeromem (sideFifo, sizeof (sideFifo));
    juce::zeromem (fftData, sizeof (fftData));
    juce::zeromem (fftAvg, sizeof (fftAvg));
    juce::zeromem (longTerm, sizeof (longTerm));
    juce::zeromem (longTermSide, sizeof (longTermSide));
    juce::zeromem (scopeL, sizeof (scopeL));
    juce::zeromem (scopeR, sizeof (scopeR));
    fifoIdx = hopCount = 0;
    fifoFull = false;
    longTermFrames.store (0);
    scopeWrite.store (0);
    bassPow = midPow = highPow = 0.0f;
    sLR = sLL = sRR = sMid = sSide = 0.0;
    loudnessMeter.prepare (sr, blockSize);
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || n == 0) return;

    const bool stereo = buffer.getNumChannels() >= 2;
    const float* L = buffer.getReadPointer (0);
    const float* R = stereo ? buffer.getReadPointer (1) : L;

    loudnessMeter.process (L, stereo ? R : nullptr, n);

    // Stereo image: leaky sums (~300 ms) so the readout is stable.
    double lr = 0, ll = 0, rr = 0, mm = 0, ss = 0;
    for (int i = 0; i < n; ++i)
    {
        const double l = L[i], r = R[i];
        lr += l * r; ll += l * l; rr += r * r;
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        mm += m * m; ss += s * s;
    }
    const double decay = std::exp (-(double) n / (0.3 * sampleRate));
    sLR = sLR * decay + lr; sLL = sLL * decay + ll; sRR = sRR * decay + rr;
    sMid = sMid * decay + mm; sSide = sSide * decay + ss;

    const double denom = std::sqrt (sLL * sRR);
    phaseCorrelation = denom > 1e-12 ? juce::jlimit (-1.0f, 1.0f, (float) (sLR / denom)) : 1.0f;
    stereoWidth      = sMid > 1e-12 ? (float) std::sqrt (sSide / sMid) : (sSide > 1e-12 ? 1.0f : 0.0f);

    // Goniometer ring.
    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        scopeL[w] = L[i];
        scopeR[w] = R[i];
        w = (w + 1) & (scopeSize - 1);
    }
    scopeWrite.store (w, std::memory_order_release);

    // Spectrum of the mid signal, 50 % overlap.
    for (int i = 0; i < n; ++i)
    {
        fifo[fifoIdx]     = 0.5f * (L[i] + R[i]);
        sideFifo[fifoIdx] = 0.5f * (L[i] - R[i]);
        if (++fifoIdx == fftSize) { fifoIdx = 0; fifoFull = true; }

        if (++hopCount >= fftSize / 2)
        {
            hopCount = 0;
            if (fifoFull) performFFT();
        }
    }
}

void AudioAnalyzer::copyScopeSamples (float* l, float* r, int count) const
{
    count = juce::jlimit (0, scopeSize, count);
    const int end = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
    {
        const int idx = (end - count + i + scopeSize) & (scopeSize - 1);
        l[i] = scopeL[idx];
        r[i] = scopeR[idx];
    }
}

void AudioAnalyzer::performFFT()
{
    // Unroll the circular FIFO (oldest sample first).
    double meanSquare = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const float v = fifo[(fifoIdx + i) & (fftSize - 1)];
        fftData[i] = v;
        meanSquare += (double) v * v;
    }
    meanSquare /= fftSize;
    juce::zeromem (fftData + fftSize, sizeof (float) * fftSize);

    window.multiplyWithWindowingTable (fftData, (size_t) fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData);

    constexpr float displayAvg = 0.1f;   // ≈ 200 ms at 48 kHz with a 1024 hop
    constexpr float bandSmooth = 0.15f;

    // Long-term average: cumulative until it has ~3 s of history, then
    // exponential. Frames below −70 dBFS are skipped so pauses and transport
    // stops do not drag the match target toward silence.
    const bool  gateOpen = meanSquare > 1.0e-7;
    const int   frames   = gateOpen ? juce::jmin (longTermFrames.load() + 1, 1 << 20) : 0;
    const float tauAlpha = (float) (1.0 - std::exp (-(fftSize / 2) / (3.0 * sampleRate)));
    const float ltAlpha  = gateOpen ? juce::jmax (tauAlpha, 1.0f / (float) frames) : 0.0f;

    const float norm    = 2.0f / (float) fftSize;
    const float powNorm = 2.0f / ((float) fftSize * windowPower);
    const float binHz   = (float) sampleRate / (float) fftSize;

    float b = 0, m = 0, h = 0;
    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[i];
        const float v   = mag * norm;

        fftAvg[i] += (v - fftAvg[i]) * displayAvg;
        if (gateOpen) longTerm[i] += (v - longTerm[i]) * ltAlpha;

        const float hz = (float) i * binHz;
        const float p  = mag * mag;
        if (hz < 250.0f)       b += p;
        else if (hz < 2000.0f) m += p;
        else                   h += p;
    }

    if (gateOpen)
    {
        // Side long-term, gated by the mid so a quiet-but-real side still counts.
        for (int i = 0; i < fftSize; ++i)
            fftData[i] = sideFifo[(fifoIdx + i) & (fftSize - 1)];
        juce::zeromem (fftData + fftSize, sizeof (float) * fftSize);
        window.multiplyWithWindowingTable (fftData, (size_t) fftSize);
        forwardFFT.performFrequencyOnlyForwardTransform (fftData);
        for (int i = 0; i < numBins; ++i)
            longTermSide[i] += (fftData[i] * norm - longTermSide[i]) * ltAlpha;

        longTermFrames.store (frames);
    }

    // Band power → mean-square (Parseval with window-power correction).
    bassPow += (b * powNorm - bassPow) * bandSmooth;
    midPow  += (m * powNorm - midPow)  * bandSmooth;
    highPow += (h * powNorm - highPow) * bandSmooth;

    const auto toDb = [] (float pw)
    {
        return pw > 1.0e-12f ? juce::jmax (LoudnessMeter::kSilenceDb, 10.0f * std::log10 (pw)) : LoudnessMeter::kSilenceDb;
    };
    bassDb = toDb (bassPow);
    midDb  = toDb (midPow);
    highDb = toDb (highPow);
}
