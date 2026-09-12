#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <deque>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  LoudnessMeter — honest BS.1770-4 loudness + true peak.
//
//    · K-weighted (two-biquad) momentary / short-term / gated-integrated LUFS
//    · 4× oversampled true peak (windowed-sinc polyphase), per channel
//    · unweighted RMS of the loudest channel (for crest factor)
//
//  Both channels are K-weighted separately and their mean squares are summed
//  with G = 1.0, exactly as the standard specifies — never the mono average,
//  which reads 3 dB low for correlated stereo and 6 dB low for a hard-panned
//  source. `process(L, nullptr, n)` measures a single channel.
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

    // Feed one block. Pass R == nullptr for a mono source (one channel).
    // Audio thread.
    void process (const float* L, const float* R, int numSamples);

    float getIntegratedLufs() const { return integrated.get(); }  // gated (I)
    float getMomentaryLufs()  const { return momentary.get(); }   // 400 ms (M)
    float getShortTermLufs()  const { return shortTerm.get(); }   // 3 s (S)
    float getTruePeakDb()     const { return truePeakDb.get(); }  // 4× oversampled, loudest channel
    float getRmsDb()          const { return rmsDb.get(); }       // unweighted, loudest channel

private:
    static constexpr int kChannels = 2;

    struct BlockEnergy { float energy = 0.0f; int count = 0; };

    void pushWindow (std::deque<BlockEnergy>& w, int& samples, float& sum,
                     float energy, int count, int targetSamples);
    void pushGatingSegment();
    static float toLufs (float energy, int count);

    double sr { 48000.0 };
    int    blockSize { 512 };

    // K-weighting (per channel, BS.1770 coefficients).
    std::array<juce::dsp::IIR::Filter<float>, kChannels> k1, k2;

    // Current-block accumulation.
    float blockEnergy { 0.0f };                       // K-weighted, channels summed
    std::array<float, kChannels> blockRmsEnergy { };  // unweighted, per channel
    int   blockCount { 0 };

    // Sliding windows (400 ms, 3 s).
    std::deque<BlockEnergy> w400, w3000;
    int   s400 { 0 }, s3000 { 0 };
    float sum400 { 0.0f }, sum3000 { 0.0f };

    // Integrated: 400 ms gating blocks built from 100 ms segments (75 % overlap).
    static constexpr int kSegmentsPerBlock = 4;
    float segEnergy { 0.0f };
    int   segCount  { 0 };
    int   segTarget { 0 };
    std::deque<BlockEnergy> gateSegments;
    std::vector<BlockEnergy> gateBlocks;

    // True peak (4× polyphase), per channel.
    static constexpr int kUp        = 4;
    static constexpr int kPhaseTaps = 12;
    std::array<std::array<float, kPhaseTaps>, kUp> phases {};
    std::array<std::array<float, kPhaseTaps>, kUp> hist[kChannels] {};
    float truePeak { 0.0f };

    // Atomically-published readouts.
    juce::Atomic<float> integrated { -120.0f };
    juce::Atomic<float> momentary  { -120.0f };
    juce::Atomic<float> shortTerm  { -120.0f };
    juce::Atomic<float> truePeakDb { -120.0f };
    juce::Atomic<float> rmsDb      { -120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
