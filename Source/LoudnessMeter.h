#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  LoudnessMeter — ITU-R BS.1770-4 / EBU R128 loudness + true peak.
//
//    · K-weighting per channel (shelf + high-pass), channel energies summed
//      with unity weights (L, R), as the standard specifies. A mono
//      programme (R == nullptr) is measured as a single channel.
//    · Momentary (400 ms) and short-term (3 s) windows, updated every 100 ms.
//    · Integrated: 400 ms gating blocks with 75 % overlap, absolute gate
//      −70 LUFS, relative gate −10 LU. Gating is done on block energies (not
//      on averaged LUFS values), using a fixed-size histogram so the audio
//      thread never allocates and cost does not grow with programme length.
//    · True peak per channel via 4× windowed-sinc polyphase oversampling.
//    · Unweighted RMS + peak over the last 3 s (for crest factor).
//
//  process() runs on the audio thread; getters are GUI-safe atomic reads.
// ─────────────────────────────────────────────────────────────────────────────
class LoudnessMeter
{
public:
    static constexpr float kSilenceDb = -120.0f;   // "no reading yet"

    LoudnessMeter() = default;
    ~LoudnessMeter() = default;

    void prepare (double sampleRate, int blockSize);
    void reset();                                   // not thread-safe: call when stopped
    void requestReset() { resetRequested.store (true); }   // any thread; applied on next process()

    // Feed one block. Pass R == nullptr for a single-channel (mono) programme.
    void process (const float* L, const float* R, int numSamples);

    float getIntegratedLufs()  const { return integrated.get(); }      // gated (I)
    float getMomentaryLufs()   const { return momentary.get(); }       // 400 ms (M)
    float getShortTermLufs()   const { return shortTerm.get(); }       // 3 s (S)
    float getTruePeakDb()      const { return truePeakMaxDb.get(); }   // max since reset (dBTP)
    float getRecentPeakDb()    const { return recentPeakDb.get(); }    // true peak, last 3 s
    float getRmsDb()           const { return rmsDb.get(); }           // unweighted, last 3 s

    // Crest factor of the last 3 s (peak − RMS, dB). 0 when silent.
    float getCrestFactorDb() const
    {
        const float pk = getRecentPeakDb(), rms = getRmsDb();
        if (pk <= kSilenceDb || rms <= kSilenceDb) return 0.0f;
        return juce::jmax (0.0f, pk - rms);
    }

    // BS.1770: loudness of a mean-square channel-summed energy.
    static float toLufs (double energy, double count);

    // The 100 ms steps, as they close (audio thread only). AudioAnalyzer cuts
    // its blocks at step ends so each measurement frame is exactly one step.
    struct Step
    {
        double kEnergy { 0 };    // Σ K-weighted squares, summed over channels
        double energy  { 0 };    // Σ unweighted squares, averaged over channels
        float  truePeak { 0 };   // max true peak (linear)
        int    samples { 0 };
    };
    int  samplesToStepEnd() const noexcept     { return juce::jmax (1, subLength - subCount); }
    juce::uint64 getStepCount() const noexcept { return stepCount; }   // survives reset()
    const Step& getLastStep() const noexcept   { return lastStep; }
    bool takeResetRequest() noexcept           { return resetRequested.exchange (false); }

    // K-weighting biquad coefficients for a sample rate (exposed for tests).
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };
    static void designKWeighting (double sampleRate, Biquad& shelf, Biquad& highPass);

private:
    static constexpr int kMaxChannels   = 2;
    static constexpr int kSubPerGate    = 4;     // 4 × 100 ms = 400 ms
    static constexpr int kSubPerShort   = 30;    // 30 × 100 ms = 3 s
    static constexpr int kUp            = 4;     // true-peak oversampling
    static constexpr int kPhaseTaps     = 12;

    // Gating histogram: −70 … +10 LUFS in 0.01 LU steps.
    static constexpr float kHistMinLufs = -70.0f;
    static constexpr float kHistStep    = 0.01f;
    static constexpr int   kHistBins    = 8000;

    void closeSubBlock();
    void updateIntegrated();

    double sr { 48000.0 };
    int    subLength { 4800 };   // samples per 100 ms step

    Biquad shelf[kMaxChannels], highPass[kMaxChannels];

    // Current 100 ms sub-block.
    double subEnergy { 0 };      // K-weighted, summed over channels
    double subRms    { 0 };      // unweighted, averaged over channels
    float  subPeak   { 0 };
    int    subCount  { 0 };

    // Ring of recent sub-blocks (newest at subHead - 1).
    std::array<double, kSubPerShort> ringEnergy {};
    std::array<double, kSubPerShort> ringRms {};
    std::array<float,  kSubPerShort> ringPeak {};
    int subHead { 0 }, subFilled { 0 };
    juce::uint64 stepCount { 0 };
    Step lastStep;

    // Integrated gating state (allocated in prepare, never on the audio thread).
    std::vector<double> histEnergy;
    std::vector<int>    histCount;
    double gatedEnergy { 0 };    // blocks above the absolute gate
    long   gatedBlocks { 0 };

    // True peak (4× polyphase, one history per channel).
    std::array<std::array<float, kPhaseTaps>, kUp> phases {};
    std::array<std::array<float, kPhaseTaps>, kMaxChannels> tpHist {};
    float truePeakMax { 0 };

    std::atomic<bool>   resetRequested { false };
    juce::Atomic<float> integrated    { kSilenceDb };
    juce::Atomic<float> momentary     { kSilenceDb };
    juce::Atomic<float> shortTerm     { kSilenceDb };
    juce::Atomic<float> truePeakMaxDb { kSilenceDb };
    juce::Atomic<float> recentPeakDb  { kSilenceDb };
    juce::Atomic<float> rmsDb         { kSilenceDb };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeter)
};
