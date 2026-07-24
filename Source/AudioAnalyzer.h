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
    void pushNextSampleIntoFifo (float sample);
    void performFFT();

    static constexpr int fftOrder = 11; // 2048 points
    static constexpr int fftSize  = 1 << fftOrder;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    float fifo [fftSize];
    float fftData [2 * fftSize];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Running analysis
    float currentLufs  = -100.0f;
    float currentPeak  = -100.0f;
    float stereoWidth  = 0.0f;     // NEW: 0 = mono, 1 = full stereo, >1 = wide/out-of-phase

    float bassEnergy = 0.0f;  // 20-250 Hz
    float midEnergy  = 0.0f;  // 250-2000 Hz
    float highEnergy = 0.0f;  // 2000+ Hz

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
