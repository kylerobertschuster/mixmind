#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include "LoudnessMeter.h"

class AudioAnalyzer
{
public:
    AudioAnalyzer();
    ~AudioAnalyzer() = default;

    void prepare (double sampleRate, int samplesPerBlock);
    void process (const juce::AudioBuffer<float>& buffer);

    // FFT (2048-point, Hann-windowed, time-averaged) — linear magnitude 0..1.
    static constexpr int fftSize = 2048;
    static constexpr int numBins = fftSize / 2;
    const float* getFFTBins() const { return fftAvg; }

    // Smoothed energy bands (0..1).
    float getBassEnergy()  const { return bassEnergy.get(); }
    float getMidEnergy()   const { return midEnergy.get(); }
    float getHighEnergy()  const { return highEnergy.get(); }

    // Honest BS.1770 metering.
    float getLufs()          const { return loudnessMeter.getIntegratedLufs(); }
    float getMomentaryLufs() const { return loudnessMeter.getMomentaryLufs(); }
    float getShortTermLufs() const { return loudnessMeter.getShortTermLufs(); }
    float getTruePeakDb()    const { return loudnessMeter.getTruePeakDb(); }
    float getRmsDb()         const { return loudnessMeter.getRmsDb(); }

    float getPeakDb() const { return loudnessMeter.getTruePeakDb(); }
    float getCrestFactor() const
    {
        const float pk  = getPeakDb();
        const float rms = getRmsDb();
        if (pk <= -180.0f || rms <= -180.0f) return 0.0f;
        return juce::jmax (0.0f, pk - rms);
    }

    float getStereoWidth() const { return stereoWidth.get(); }
    float getPhaseCorr()   const { return phaseCorrelation.get(); }

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

    // Written on the audio thread, read on the GUI thread. Plain floats — torn
    // reads are harmless for display, and the shaper smooths 1/3-octave anyway.
    float fftOutput[numBins] { 0 };
    float fftAvg[numBins] { 0 };

    juce::Atomic<float> bassEnergy  { 0 };
    juce::Atomic<float> midEnergy   { 0 };
    juce::Atomic<float> highEnergy  { 0 };
    juce::Atomic<float> stereoWidth { 0.5f };
    juce::Atomic<float> phaseCorrelation { 1 };

    LoudnessMeter loudnessMeter;
    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
