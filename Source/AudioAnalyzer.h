#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include "LoudnessMeter.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AudioAnalyzer — the live read side, shared by MixMind / Scope / Meter / EQT.
//
//    · Spectrum of the mid signal (L+R)/2 — the same downmix the reference
//      analyzer uses, so live and reference curves are directly comparable.
//      2048-pt Hann (normalised to unit mean), 50 % overlap, magnitude × 2/N,
//      so a sine of amplitude A peaks at ≈ A (0 dBFS sine → 1.0).
//    · Two averages of that spectrum:
//        display   — ~200 ms, for the analyzer curve;
//        long-term — ~3 s, gated on signal presence; this is what the match
//                    EQ compares against the reference, so the correction
//                    does not chase individual hits.
//    · Honest BS.1770 metering via LoudnessMeter.
//
//  Written on the audio thread, read on the GUI thread. Spectrum arrays are
//  plain floats: a torn read only affects one displayed frame, and the shaper
//  smooths 1/3-octave anyway. Scalars are atomics.
// ─────────────────────────────────────────────────────────────────────────────
class AudioAnalyzer
{
public:
    AudioAnalyzer();
    ~AudioAnalyzer() = default;

    void prepare (double sampleRate, int samplesPerBlock);
    void process (const juce::AudioBuffer<float>& buffer);

    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numBins  = fftSize / 2;

    const float* getFFTBins() const      { return fftAvg; }     // display average
    const float* getLongTermBins() const { return longTerm; }   // match-EQ average
    bool hasLongTermSpectrum() const     { return longTermFrames.load() > 0; }

    // Band levels in dB RMS (< 250 Hz, 250 Hz – 2 kHz, > 2 kHz).
    float getBassLevelDb() const { return bassDb.get(); }
    float getMidLevelDb()  const { return midDb.get(); }
    float getHighLevelDb() const { return highDb.get(); }

    // BS.1770 metering.
    float getLufs()           const { return loudnessMeter.getIntegratedLufs(); }
    float getMomentaryLufs()  const { return loudnessMeter.getMomentaryLufs(); }
    float getShortTermLufs()  const { return loudnessMeter.getShortTermLufs(); }
    float getTruePeakDb()     const { return loudnessMeter.getTruePeakDb(); }
    float getRmsDb()          const { return loudnessMeter.getRmsDb(); }
    float getCrestFactor()    const { return loudnessMeter.getCrestFactorDb(); }
    void  resetLoudness()           { loudnessMeter.requestReset(); }

    // Stereo image, averaged over ~300 ms.
    float getStereoWidth() const { return stereoWidth.get(); }
    float getPhaseCorr()   const { return phaseCorrelation.get(); }

    // Goniometer feed: copies the newest `n` stereo samples (oldest first).
    static constexpr int scopeSize = 2048;
    void copyScopeSamples (float* l, float* r, int n) const;

    double getSampleRate() const { return sampleRate; }

private:
    void performFFT();

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    float windowPower { 1.0f };   // Σ w², for band-power normalisation

    float fifo[fftSize] { 0 };
    int   fifoIdx { 0 };
    int   hopCount { 0 };
    bool  fifoFull { false };
    float fftData[2 * fftSize] { 0 };

    float fftAvg[numBins] { 0 };
    float longTerm[numBins] { 0 };
    std::atomic<int> longTermFrames { 0 };

    float bassPow { 0 }, midPow { 0 }, highPow { 0 };
    juce::Atomic<float> bassDb { LoudnessMeter::kSilenceDb };
    juce::Atomic<float> midDb  { LoudnessMeter::kSilenceDb };
    juce::Atomic<float> highDb { LoudnessMeter::kSilenceDb };

    double sLR { 0 }, sLL { 0 }, sRR { 0 }, sMid { 0 }, sSide { 0 };
    juce::Atomic<float> stereoWidth { 0.0f };
    juce::Atomic<float> phaseCorrelation { 1.0f };

    float scopeL[scopeSize] { 0 };
    float scopeR[scopeSize] { 0 };
    std::atomic<int> scopeWrite { 0 };

    LoudnessMeter loudnessMeter;
    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
