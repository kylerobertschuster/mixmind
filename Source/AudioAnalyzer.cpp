#include "AudioAnalyzer.h"
#include <cmath>

AudioAnalyzer::AudioAnalyzer() = default;

void AudioAnalyzer::prepare (double sr, int)
{
    sampleRate = sr;
    juce::zeromem (fifoL, sizeof (fifoL));
    juce::zeromem (fifoR, sizeof (fifoR));
    juce::zeromem (fftDataL, sizeof (fftDataL));
    juce::zeromem (fftDataR, sizeof (fftDataR));
    juce::zeromem (fftOutput, sizeof (fftOutput));
    fifoIdx = 0;
    fftReady = false;
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) return;

    auto* L = buffer.getReadPointer (0);
    auto* R = buffer.getNumChannels() >= 2 ? buffer.getReadPointer (1) : L;
    int n = buffer.getNumSamples();

    // RMS / LUFS
    currentLufs = juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, 0, n)) - 3.0f;

    // Peak (fast attack, ~1.5s release) for crest factor / true-peak readouts.
    const float blockPeak = buffer.getMagnitude (0, 0, n);
    const float release   = std::exp (-(float) n / ((float) sampleRate * 1.5f));
    currentPeak = juce::jmax (blockPeak, currentPeak.get() * release);

    // Phase correlation
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

    // Feed FIFO for FFT
    for (int i = 0; i < n; ++i)
        pushNextSample (L[i], R[i]);
}

void AudioAnalyzer::pushNextSample (float l, float r)
{
    if (fifoIdx == fftSize)
    {
        if (!fftReady)
        {
            juce::zeromem (fftDataL, sizeof (fftDataL));
            juce::zeromem (fftDataR, sizeof (fftDataR));
            memcpy (fftDataL, fifoL, sizeof (fifoL));
            memcpy (fftDataR, fifoR, sizeof (fifoR));
            fftReady = true;
            performFFT();
        }
        fifoIdx = 0;
    }
    fifoL[fifoIdx] = l;
    fifoR[fifoIdx] = r;
    fifoIdx++;
}

void AudioAnalyzer::performFFT()
{
    window.multiplyWithWindowingTable (fftDataL, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftDataL);

    float b = 0, m = 0, h = 0;
    const float k = 0.15f;

    for (int i = 0; i < numBins; ++i)
    {
        float hz = (float)i * (float)sampleRate / (float)fftSize;
        float mag = fftDataL[i];
        fftOutput[i] = mag * (2.0f / (float)fftSize);  // normalize to 0-1

        // Smooth for display

        if (hz < 250)       b += mag;
        else if (hz < 2000) m += mag;
        else                h += mag;
    }

    bassEnergy  = bassEnergy.get()  + (b - bassEnergy.get())  * k;
    midEnergy   = midEnergy.get()   + (m - midEnergy.get())   * k;
    highEnergy  = highEnergy.get()  + (h - highEnergy.get())  * k;
    fftReady = false;
}
