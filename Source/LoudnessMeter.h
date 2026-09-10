#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <deque>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  LoudnessMeter — honest BS.1770-style loudness + true-peak.
//
//    · K-weighted (two-biquad) momentary / short-term / gated-integrated LUFS
//    · 4× oversampled true peak (windowed-sinc polyphase)
//    · unweighted RMS (for crest factor)
//
//  All measurement is done on the audio thread; getters are GUI-safe reads of
//  atomics. This replaces the old "RMS − 3 dB" approximation.
// ─────────────────────────────────────────────────────────────────────────────
class LoudnessMeter
{
public:
    LoudnessMeter() = default;
    ~LoudnessMeter() = default;

    void prepare (double sampleRate, int blockSize);
    void reset();

    // Feed one block (mono input by passing R == L). Audio thread.
    void process (const float* L, const float* R, int numSamples);

    float getIntegratedLufs() const { return integrated.get(); }  // gated (I)
    float getMomentaryLufs()  const { return momentary.get(); }   // 400 ms (M)
    float getShortTermLufs()  const { return shortTerm.get(); }   // 3 s (S)
    float getTruePeakDb()     const { return truePeakDb.get(); }  // 4× oversampled
    float getRmsDb()          const { return rmsDb.get(); }       // unweighted (crest)

private:
    struct BlockEnergy { float energy = 0.0f; int count = 0; };

    void pushWindow (std::deque<BlockEnergy>& w, int& samples, float& sum,
                     float energy, int count, int targetSamples);
    static float toLufs (float energy, int count);

    double sr { 48000.0 };
    int    blockSize { 512 };

    // K-weighting (applied to the mono sum).
    juce::dsp::IIR::Filter<float> k1, k2;

    // Current-block accumulation.
    float blockEnergy { 0.0f };     // K-weighted sum of squares
    float blockRmsEnergy { 0.0f };  // unweighted
    int   blockCount { 0 };

    // Sliding windows (400 ms, 3 s).
    std::deque<BlockEnergy> w400, w3000;
    int   s400 { 0 }, s3000 { 0 };
    float sum400 { 0.0f }, sum3000 { 0.0f };

    // Integrated: 400 ms gating-block history.
    float gateEnergy { 0.0f };
    int   gateCount  { 0 };
    int   gateTarget { 0 };
    std::vector<float> gateBlocks;

    // True peak (4× polyphase).
    static constexpr int kUp        = 4;
    static constexpr int kPhaseTaps = 12;
    std::array<std::array<float, kPhaseTaps>, kUp> phases {};
    std::array<float, kPhaseTaps> hist[kUp] {};
    float truePeak { 0.0f };

    // Atomically-published readouts.
    juce::Atomic<float> integrated { -120.0f };
    juce::Atomic<float> momentary  { -120.0f };
    juce::Atomic<float> shortTerm  { -120.0f };
    juce::Atomic<float> truePeakDb { -120.0f };
    juce::Atomic<float> rmsDb      { -120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
