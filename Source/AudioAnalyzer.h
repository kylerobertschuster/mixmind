#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>

class AudioAnalyzer
{
public:
    AudioAnalyzer();
    ~AudioAnalyzer() = default;

    void prepare (double sampleRate, int samplesPerBlock);
    void process (const juce::AudioBuffer<float>& buffer);

    // Real FFT bin data (2048-point magnitude spectrum)
    static constexpr int fftSize = 2048;
    static constexpr int numBins = fftSize / 2;
    const float* getFFTBins() const { return fftOutput; }

    // Smoothed values for display
    float getBassEnergy()  const { return bassEnergy; }
    float getMidEnergy()   const { return midEnergy; }
    float getHighEnergy()  const { return highEnergy; }
    float getLufs()        const { return currentLufs; }
    float getStereoWidth() const { return stereoWidth; }
    float getPhaseCorr()   const { return phaseCorrelation; }

private:
    void pushNextSample (float l, float r);
    void performFFT();

    static constexpr int fftOrder = 11;

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };

    float fifoL[fftSize] { 0 };
    float fifoR[fftSize] { 0 };
    float fftDataL[2 * fftSize] { 0 };
    float fftDataR[2 * fftSize] { 0 };
    int   fifoIdx { 0 };
    bool  fftReady { false };

    // Smoothed output bins (exposed to UI)
    float fftOutput[numBins] { 0 };
    float fftSmooth[numBins] { 0 };

    float bassEnergy    { 0 };
    float midEnergy     { 0 };
    float highEnergy    { 0 };
    float currentLufs   { -60 };
    float stereoWidth   { 0.5f };
    float phaseCorrelation { 1 };

    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
