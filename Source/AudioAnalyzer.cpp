#include "AudioAnalyzer.h"
#include <cmath>

namespace
{
    // [first, end) analyzer bins of each octave band: bins whose centre lies in
    // [fc/√2, fc·√2). DC is never included. A band can be empty at high rates.
    std::array<std::pair<int, int>, MeasurementFrame::kBands> bandRanges (double sampleRate)
    {
        std::array<std::pair<int, int>, MeasurementFrame::kBands> r {};
        const double binHz = sampleRate / (double) AudioAnalyzer::fftSize;
        for (int b = 0; b < MeasurementFrame::kBands; ++b)
        {
            const double fc = MeasurementFrame::bandCentreHz (b);
            const int first = juce::jlimit (1, AudioAnalyzer::numBins, (int) std::ceil (fc / std::sqrt (2.0) / binHz));
            const int end   = juce::jlimit (first, AudioAnalyzer::numBins, (int) std::ceil (fc * std::sqrt (2.0) / binHz));
            r[(size_t) b] = { first, end };
        }
        return r;
    }
}

AudioAnalyzer::AudioAnalyzer()
{
    float ones[fftSize];
    std::fill (ones, ones + fftSize, 1.0f);
    window.multiplyWithWindowingTable (ones, (size_t) fftSize);
    windowPower = 0.0f;
    for (float w : ones) windowPower += w * w;
    bandBins = bandRanges (sampleRate);
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
    longTermFrames = 0;
    scopeWrite.store (0);
    bassPow = midPow = highPow = 0.0f;
    sLR = sLL = sRR = sMid = sSide = 0.0;
    loudnessMeter.prepare (sr, blockSize);

    // The host does not call prepare() and process() at once, so this thread
    // is the only writer here; readers pick the reset up like any update.
    publishSpectra();

    bandBins = bandRanges (sr);
    frame = {};
    frameIndex = 0;
    ++epoch;   // the history starts a new run; stale frames still queued carry the old epoch
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || n == 0) return;

    const bool stereo = buffer.getNumChannels() >= 2;
    const float* L = buffer.getReadPointer (0);
    const float* R = stereo ? buffer.getReadPointer (1) : L;

    // A loudness reset restarts the meter's steps; the partial frame goes with it.
    if (loudnessMeter.takeResetRequest())
    {
        loudnessMeter.reset();
        frame = {};
    }

    // Cut the block at the meter's 100 ms step ends, so every frame covers
    // exactly one step whatever the host block size.
    for (int pos = 0; pos < n;)
    {
        const int len = juce::jmin (n - pos, loudnessMeter.samplesToStepEnd());
        const auto steps = loudnessMeter.getStepCount();
        loudnessMeter.process (L + pos, stereo ? R + pos : nullptr, len);
        analyse (L + pos, R + pos, len);
        if (loudnessMeter.getStepCount() != steps)
            closeFrame();
        pos += len;
    }
}

void AudioAnalyzer::analyse (const float* L, const float* R, int n)
{
    // Stereo image: leaky sums (~300 ms) so the readout is stable; plain sums
    // for the frame.
    double lr = 0, ll = 0, rr = 0, mm = 0, ss = 0;
    for (int i = 0; i < n; ++i)
    {
        const double l = L[i], r = R[i];
        lr += l * r; ll += l * l; rr += r * r;
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        mm += m * m; ss += s * s;
    }
    frame.ll += ll; frame.rr += rr; frame.lr += lr;

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
    const int   frames   = gateOpen ? juce::jmin (longTermFrames + 1, 1 << 20) : 0;
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
        midMag[i] = v;

        fftAvg[i] += (v - fftAvg[i]) * displayAvg;
        if (gateOpen) longTerm[i] += (v - longTerm[i]) * ltAlpha;

        const float hz = (float) i * binHz;
        const float p  = mag * mag;
        if (hz < 250.0f)       b += p;
        else if (hz < 2000.0f) m += p;
        else                   h += p;
    }

    // Side spectrum: for the frame's mono-compatibility bands, and the
    // long-term side (gated by the mid, so a quiet-but-real side still counts).
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = sideFifo[(fifoIdx + i) & (fftSize - 1)];
    juce::zeromem (fftData + fftSize, sizeof (float) * fftSize);
    window.multiplyWithWindowingTable (fftData, (size_t) fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData);
    for (int i = 0; i < numBins; ++i)
        fftData[i] *= norm;

    if (gateOpen)
    {
        for (int i = 0; i < numBins; ++i)
            longTermSide[i] += (fftData[i] - longTermSide[i]) * ltAlpha;
        longTermFrames = frames;
    }

    for (int band = 0; band < MeasurementFrame::kBands; ++band)
    {
        const auto [first, end] = bandBins[(size_t) band];
        if (end <= first) continue;
        double mag = 0.0, midP = 0.0, sideP = 0.0;
        for (int i = first; i < end; ++i)
        {
            mag   += midMag[i];
            midP  += (double) midMag[i] * midMag[i];
            sideP += (double) fftData[i] * fftData[i];
        }
        const double count = end - first;
        frame.bandMag[(size_t) band]     += (float) (mag / count);
        frame.bandMidPow[(size_t) band]  += (float) (midP / count);
        frame.bandSidePow[(size_t) band] += (float) (sideP / count);
    }
    ++frame.fftFrames;

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

    publishSpectra();
}

void AudioAnalyzer::publishSpectra()
{
    auto& s = spectra.back();
    std::copy (fftAvg, fftAvg + numBins, s.display);
    std::copy (longTerm, longTerm + numBins, s.longTerm);
    std::copy (longTermSide, longTermSide + numBins, s.longTermSide);
    s.longTermFrames = longTermFrames;
    spectra.publish();
}

void AudioAnalyzer::closeFrame()
{
    const auto& step = loudnessMeter.getLastStep();
    frame.epoch      = epoch;
    frame.index      = frameIndex++;
    frame.sampleRate = sampleRate;
    frame.samples    = step.samples;
    frame.kEnergy    = step.kEnergy;
    frame.energy     = step.energy;
    frame.truePeak   = step.truePeak;
    if (frame.fftFrames > 1)
    {
        const float k = 1.0f / (float) frame.fftFrames;
        for (int b = 0; b < MeasurementFrame::kBands; ++b)
        {
            frame.bandMag[(size_t) b]     *= k;
            frame.bandMidPow[(size_t) b]  *= k;
            frame.bandSidePow[(size_t) b] *= k;
        }
    }

    // Never wait: with nobody reading, the frame is dropped (its index is
    // then missing from the history).
    {
        const auto scope = frameFifo.write (1);
        if (scope.blockSize1 > 0)
            frameSlots[(size_t) scope.startIndex1] = frame;
    }
    frame = {};
}

bool AudioAnalyzer::popFrame (MeasurementFrame& out)
{
    const auto scope = frameFifo.read (1);
    if (scope.blockSize1 == 0) return false;
    out = frameSlots[(size_t) scope.startIndex1];
    return true;
}

int AudioAnalyzer::bandBinCount (int band, double sr)
{
    const auto r = bandRanges (sr)[(size_t) juce::jlimit (0, MeasurementFrame::kBands - 1, band)];
    return r.second - r.first;
}

void AudioAnalyzer::bandMagnitudes (const float* bins, double sr, float* outBands)
{
    const auto ranges = bandRanges (sr);
    for (int b = 0; b < MeasurementFrame::kBands; ++b)
    {
        const auto [first, end] = ranges[(size_t) b];
        double sum = 0.0;
        for (int i = first; i < end; ++i) sum += bins[i];
        outBands[b] = end > first ? (float) (sum / (end - first)) : 0.0f;
    }
}
