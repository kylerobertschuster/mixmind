#include "AudioAnalyzer.h"
#include <cmath>

AudioAnalyzer::AudioAnalyzer()
    : forwardFFT (fftOrder),
      window (fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    juce::zeromem (fifoL, sizeof (fifoL));
    juce::zeromem (fifoR, sizeof (fifoR));
    juce::zeromem (fftDataL, sizeof (fftDataL));
    juce::zeromem (fftDataR, sizeof (fftDataR));
}

void AudioAnalyzer::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    juce::zeromem (fifoL, sizeof (fifoL));
    juce::zeromem (fifoR, sizeof (fifoR));
    juce::zeromem (fftDataL, sizeof (fftDataL));
    juce::zeromem (fftDataR, sizeof (fftDataR));
    fifoIndex = 0;
    nextFFTBlockReady = false;
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    auto* L = buffer.getReadPointer (0);
    auto* R = buffer.getNumChannels() >= 2 ? buffer.getReadPointer (1) : L;
    int numSamples = buffer.getNumSamples();

    // ── RMS & LUFS ──────────────────────────────────────────────────────
    float rms = buffer.getRMSLevel (0, 0, numSamples);
    rmsLevel = juce::Decibels::gainToDecibels (rms);
    integratedLufs = rmsLevel - 3.0f;
    shortTermLufs = integratedLufs; // simplified: same for now

    // ── True Peak ────────────────────────────────────────────────────────
    truePeakDb = juce::Decibels::gainToDecibels (
        buffer.getMagnitude (0, 0, numSamples));
    crestFactor = truePeakDb - rmsLevel;

    // ── Phase Correlation (goniometer) ───────────────────────────────────
    // Correlation = Σ(L·R) / √(ΣL²·ΣR²)
    if (buffer.getNumChannels() >= 2)
    {
        double sumLR = 0.0, sumL2 = 0.0, sumR2 = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            sumLR += (double) L[i] * R[i];
            sumL2 += (double) L[i] * L[i];
            sumR2 += (double) R[i] * R[i];
        }
        double denom = std::sqrt (sumL2 * sumR2);
        phaseCorrelation = (denom > 1e-12) ? (float)(sumLR / denom) : 1.0f;
        phaseCorrelation = juce::jlimit (-1.0f, 1.0f, phaseCorrelation);
    }
    else
    {
        phaseCorrelation = 1.0f;
    }

    // ── Mid/Side stereo width ────────────────────────────────────────────
    if (buffer.getNumChannels() >= 2)
    {
        float midPwr = 0.0f, sidePwr = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float m = (L[i] + R[i]) * 0.5f;
            float s = (L[i] - R[i]) * 0.5f;
            midPwr  += m * m;
            sidePwr += s * s;
        }
        stereoWidth = (midPwr > 0.0f) ? std::sqrt (sidePwr / midPwr) : 0.0f;
    }
    else
    {
        stereoWidth = 0.0f;
    }

    // ── Feed stereo samples into FFT FIFO ────────────────────────────────
    for (int i = 0; i < numSamples; ++i)
        pushNextSampleIntoFifo (L[i], R[i]);
}

void AudioAnalyzer::pushNextSampleIntoFifo (float sampleL, float sampleR)
{
    if (fifoIndex == fftSize)
    {
        if (!nextFFTBlockReady)
        {
            juce::zeromem (fftDataL, sizeof (fftDataL));
            juce::zeromem (fftDataR, sizeof (fftDataR));
            memcpy (fftDataL, fifoL, sizeof (fifoL));
            memcpy (fftDataR, fifoR, sizeof (fifoR));
            nextFFTBlockReady = true;
            performFFT();
        }
        fifoIndex = 0;
    }
    fifoL[fifoIndex] = sampleL;
    fifoR[fifoIndex] = sampleR;
    fifoIndex++;
}

void AudioAnalyzer::performFFT()
{
    // Windowing
    window.multiplyWithWindowingTable (fftDataL, fftSize);
    window.multiplyWithWindowingTable (fftDataR, fftSize);

    // FFT
    forwardFFT.performFrequencyOnlyForwardTransform (fftDataL);
    forwardFFT.performFrequencyOnlyForwardTransform (fftDataR);

    float b = 0.0f, sb = 0.0f, m = 0.0f, h = 0.0f;
    double corrSubNum = 0.0, corrSubDenL = 0.0, corrSubDenR = 0.0;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        float hz    = ((float)i * (float)sampleRate) / (float)fftSize;
        float levelL = fftDataL[i];
        float levelR = fftDataR[i];
        float level  = (levelL + levelR) * 0.5f;

        if (hz < 60.0f)
        {
            sb += level;
            // Sub-bass phase correlation (cross-power between L and R in this band)
            corrSubNum   += (double) fftDataL[i] * fftDataR[i];
            corrSubDenL  += (double) fftDataL[i] * fftDataL[i];
            corrSubDenR  += (double) fftDataR[i] * fftDataR[i];
        }
        else if (hz < 250.0f)
            b += level;
        else if (hz < 2000.0f)
            m += level;
        else
            h += level;
    }

    // EWMA smoothing
    const float k = 0.10f;
    subBassEnergy = (subBassEnergy * (1.0f - k)) + (sb * k);
    bassEnergy    = (bassEnergy    * (1.0f - k)) + (b  * k);
    midEnergy     = (midEnergy     * (1.0f - k)) + (m  * k);
    highEnergy    = (highEnergy    * (1.0f - k)) + (h  * k);

    // Sub-bass correlation (20-60Hz band)
    double denom = std::sqrt (corrSubDenL * corrSubDenR);
    if (denom > 1e-12)
        subBassCorrelation = juce::jlimit (-1.0f, 1.0f, (float)(corrSubNum / denom));
    else
        subBassCorrelation = 1.0f;

    nextFFTBlockReady = false;
}

juce::String AudioAnalyzer::getAnalysisAsJson() const
{
    // ── Full multi-domain telemetry JSON ────────────────────────────────
    return "{"
        "\"bass_energy\":"            + juce::String (bassEnergy, 1)          + ","
        "\"mid_energy\":"             + juce::String (midEnergy, 1)           + ","
        "\"high_energy\":"            + juce::String (highEnergy, 1)          + ","
        "\"sub_bass_energy\":"        + juce::String (subBassEnergy, 1)       + ","
        "\"phase_correlation\":"      + juce::String (phaseCorrelation, 3)    + ","
        "\"stereo_width\":"           + juce::String (stereoWidth, 3)         + ","
        "\"integrated_lufs\":"        + juce::String (integratedLufs, 1)      + ","
        "\"short_term_lufs\":"        + juce::String (shortTermLufs, 1)       + ","
        "\"true_peak_db\":"           + juce::String (truePeakDb, 1)          + ","
        "\"crest_factor\":"           + juce::String (crestFactor, 1)         + ","
        "\"rms_level\":"              + juce::String (rmsLevel, 1)            + ","
        "\"sub_bass_correlation\":"   + juce::String (subBassCorrelation, 3)  +
        "}";
}
