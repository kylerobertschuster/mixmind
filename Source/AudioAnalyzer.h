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
    float getBassEnergy()  const { return bassEnergy.get(); }
    float getMidEnergy()   const { return midEnergy.get(); }
    float getHighEnergy()  const { return highEnergy.get(); }
    float getLufs()        const { return currentLufs.get(); }
    float getStereoWidth() const { return stereoWidth.get(); }
    float getPhaseCorr()   const { return phaseCorrelation.get(); }
    float getPeakDb()      const { return juce::Decibels::gainToDecibels (currentPeak.get()); }
    float getCrestFactor() const
    {
        const float pk  = getPeakDb();
        const float rms = currentLufs.get() + 3.0f;  // undo the LUFS offset
        if (pk <= -180.0f || rms <= -180.0f) return 0.0f;
        return juce::jmax (0.0f, pk - rms);
    }
    double getSampleRate() const { return sampleRate; }

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

    // Written on audio thread, read on GUI thread — plain float, torn reads are harmless for display
    float fftOutput[numBins] { 0 };
    juce::Atomic<float> bassEnergy  { 0 };
    juce::Atomic<float> midEnergy   { 0 };
    juce::Atomic<float> highEnergy  { 0 };
    juce::Atomic<float> currentLufs { -60 };
    juce::Atomic<float> stereoWidth { 0.5f };
    juce::Atomic<float> phaseCorrelation { 1 };
    juce::Atomic<float> currentPeak { 0 };

    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
