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

    juce::String getAnalysisAsJson() const;

private:
    void pushNextSampleIntoFifo (float sampleL, float sampleR);
    void performFFT();

    static constexpr int fftOrder = 11; // 2048 points
    static constexpr int fftSize  = 1 << fftOrder;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    // Stereo FIFO for goniometer / phase correlation
    float fifoL [fftSize];
    float fifoR [fftSize];
    float fftDataL [2 * fftSize];
    float fftDataR [2 * fftSize];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Running analysis
    float integratedLufs   { -60.0f };
    float shortTermLufs    { -60.0f };
    float truePeakDb       { -60.0f };
    float crestFactor      { 0.0f };
    float rmsLevel         { -60.0f };

    float stereoWidth      { 0.5f };
    float phaseCorrelation { 1.0f };
    float subBassCorrelation { 1.0f };

    float bassEnergy    { 0.0f };
    float subBassEnergy { 0.0f };
    float midEnergy     { 0.0f };
    float highEnergy    { 0.0f };

    double sampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
