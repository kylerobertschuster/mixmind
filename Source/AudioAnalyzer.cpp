#include "AudioAnalyzer.h"
#include <cmath>

AudioAnalyzer::AudioAnalyzer() = default;

void AudioAnalyzer::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    juce::zeromem (fifoL, sizeof (fifoL));
    juce::zeromem (fftDataL, sizeof (fftDataL));
    juce::zeromem (fftOutput, sizeof (fftOutput));
    juce::zeromem (fftAvg, sizeof (fftAvg));
    fifoIdx = 0;
    fftReady = false;
    loudnessMeter.prepare (sr, blockSize);
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) return;

    auto* L = buffer.getReadPointer (0);
    auto* R = buffer.getNumChannels() >= 2 ? buffer.getReadPointer (1) : L;
    int n = buffer.getNumSamples();

    // Honest loudness + true-peak (BS.1770 K-weighted, gated).
    loudnessMeter.process (L, R, n);

    // Phase correlation + stereo width (instantaneous per block).
    if (buffer.getNumChannels() >= 2)
    {
        double sumLR = 0, sumL2 = 0, sumR2 = 0;
        for (int i = 0; i < n; ++i)
        {
            sumLR += (double)L[i] * R[i];
            sumL2 += (double)L[i] * L[i];
            sumR2 += (double)R[i] * R[i];
        }
        double denom = std::sqrt (sumL2 * sumR2);
        phaseCorrelation = (denom > 1e-12) ? juce::jlimit (-1.0f, 1.0f, (float)(sumLR / denom)) : 1.0f;

        float midPwr = 0, sidePwr = 0;
        for (int i = 0; i < n; ++i)
        {
            float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f;
            midPwr += m * m; sidePwr += s * s;
        }
        stereoWidth = (midPwr > 0) ? std::sqrt (sidePwr / midPwr) : 0;
    }
    else { phaseCorrelation = 1; stereoWidth = 0; }

    for (int i = 0; i < n; ++i)
        pushNextSample (channelAnalysisSample ((ChannelMode) channelMode.load(), L[i], R[i]));
}

void AudioAnalyzer::pushNextSample (float a)
{
    if (fifoIdx == fftSize)
    {
        if (!fftReady)
        {
            juce::zeromem (fftDataL, sizeof (fftDataL));
            memcpy (fftDataL, fifoL, sizeof (fifoL));
            fftReady = true;
            performFFT();
        }
        fifoIdx = 0;
    }
    fifoL[fifoIdx] = a;
    fifoIdx++;
}

void AudioAnalyzer::performFFT()
{
    window.multiplyWithWindowingTable (fftDataL, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftDataL);

    const float k   = 0.15f;   // energy-band smoothing
    const float avg = 0.2f;    // spectral averaging time constant

    float b = 0, m = 0, h = 0;
    for (int i = 0; i < numBins; ++i)
    {
        float hz  = (float) i * (float) sampleRate / (float) fftSize;
        float mag = fftDataL[i];
        fftOutput[i] = mag * (2.0f / (float) fftSize);   // normalise to 0..1

        // Time-average the spectrum (SPAN-style) so the curve is stable.
        fftAvg[i] += (fftOutput[i] - fftAvg[i]) * avg;

        if (hz < 250)       b += mag;
        else if (hz < 2000) m += mag;
        else                h += mag;
    }

    bassEnergy  = bassEnergy.get()  + (b - bassEnergy.get())  * k;
    midEnergy   = midEnergy.get()   + (m - midEnergy.get())   * k;
    highEnergy  = highEnergy.get()  + (h - highEnergy.get())  * k;
    fftReady = false;
}
