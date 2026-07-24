#include "AudioAnalyzer.h"

AudioAnalyzer::AudioAnalyzer()
    : forwardFFT (fftOrder),
      window (fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    juce::zeromem(fifo, sizeof(fifo));
    juce::zeromem(fftData, sizeof(fftData));
}

void AudioAnalyzer::prepare (double newSampleRate, int /*samplesPerBlock*/)
{
    sampleRate = newSampleRate;
    juce::zeromem(fifo, sizeof(fifo));
    juce::zeromem(fftData, sizeof(fftData));
    fifoIndex = 0;
    nextFFTBlockReady = false;
}

void AudioAnalyzer::process (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    // 1. Peak & RMS (approximating LUFS for now via generic RMS)
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    currentLufs = juce::Decibels::gainToDecibels(rms) - 3.0f; // rough approximation
    currentPeak = juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 0, buffer.getNumSamples()));

    // 2. Stereo width: mid/side energy ratio
    //    0 = mono, 1 = full stereo, >1 = wide or out-of-phase
    if (buffer.getNumChannels() >= 2)
    {
        auto* L = buffer.getReadPointer(0);
        auto* R = buffer.getReadPointer(1);
        float midPower  = 0.0f;
        float sidePower = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float m = (L[i] + R[i]) * 0.5f;
            float s = (L[i] - R[i]) * 0.5f;
            midPower  += m * m;
            sidePower += s * s;
        }

        stereoWidth = (midPower > 0.0f) ? std::sqrt(sidePower / midPower) : 0.0f;
    }

    // 3. Feed mono-sum into FFT FIFO
    auto* channelDataL = buffer.getReadPointer(0);
    auto* channelDataR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : channelDataL;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float monoSample = (channelDataL[i] + channelDataR[i]) * 0.5f;
        pushNextSampleIntoFifo(monoSample);
    }
}

void AudioAnalyzer::pushNextSampleIntoFifo (float sample)
{
    if (fifoIndex == fftSize)
    {
        if (! nextFFTBlockReady)
        {
            juce::zeromem (fftData, sizeof (fftData));
            memcpy (fftData, fifo, sizeof (fifo));
            nextFFTBlockReady = true;
            performFFT();
        }
        fifoIndex = 0;
    }

    fifo[fifoIndex++] = sample;
}

void AudioAnalyzer::performFFT()
{
    window.multiplyWithWindowingTable (fftData, fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData);

    float b = 0.0f;
    float m = 0.0f;
    float h = 0.0f;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        float hz    = ((float)i * (float)sampleRate) / (float)fftSize;
        float level = fftData[i];

        if (hz < 250.0f)        b += level;
        else if (hz < 2000.0f)  m += level;
        else                    h += level;
    }

    // EWMA smoothing
    bassEnergy = (bassEnergy * 0.9f) + (b * 0.1f);
    midEnergy  = (midEnergy  * 0.9f) + (m * 0.1f);
    highEnergy = (highEnergy * 0.9f) + (h * 0.1f);

    nextFFTBlockReady = false;
}

juce::String AudioAnalyzer::getAnalysisAsJson() const
{
    // Derived metrics
    float crestFactor    = currentPeak - currentLufs;                            // dynamic range indicator
    float bassToMidRatio = (midEnergy > 0.0f) ? bassEnergy / midEnergy : 0.0f;  // mud detector
    bool  isClipping     = currentPeak >= -0.1f;

    return "{"
        "\"lufs\": "             + juce::String(currentLufs, 1)       + ","
        "\"peak_db\": "          + juce::String(currentPeak, 1)       + ","
        "\"crest_factor_db\": "  + juce::String(crestFactor, 1)       + ","
        "\"bass_energy\": "      + juce::String(bassEnergy, 1)        + ","
        "\"mid_energy\": "       + juce::String(midEnergy, 1)         + ","
        "\"high_energy\": "      + juce::String(highEnergy, 1)        + ","
        "\"bass_to_mid_ratio\": "+ juce::String(bassToMidRatio, 2)    + ","
        "\"stereo_width\": "     + juce::String(stereoWidth, 2)       + ","
        "\"is_clipping\": "      + (isClipping ? "true" : "false")    +
        "}";
}
