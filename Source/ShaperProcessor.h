#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  ShaperProcessor — the reference-matching EQ ("the shaper").
//
//  Applies a linear-phase match-EQ FIR so the track's spectrum is reshaped
//  toward a target:
//    · AUTO   → target = the uploaded reference's spectrum
//    · MANUAL → target = the hand-drawn trace curve
//  Both reduce to "gain = target − live", so one engine serves both modes.
//  In mid/side mode the mid and side signals each get their own filter.
//
//  The match is level-neutral: the octave-weighted mean of the difference is
//  removed before the filter is built, so the shaper changes tone, never
//  loudness (a reference mastered 10 dB hotter does not turn into +10 dB).
//
//  Latency is a constant kLatency whether shaping or not. Disabled = a pure
//  kLatency delay, so toggling SHAPE never changes the host's delay
//  compensation. Filter updates and enable/disable crossfade over
//  kFadeSamples, so neither clicks.
//
//  Convolution is uniformly partitioned: the first kPartition taps run as a
//  direct FIR (so no latency is added on top of the filter's own), the rest
//  as FFT partitions evaluated once per kPartition samples — about a tenth of
//  the cost of a direct 2048-tap FIR, and independent of the host block size.
//
//  Filters are designed OFF the audio thread and handed over through a fixed
//  pool of slots under a SpinLock the audio thread only try-locks.
// ─────────────────────────────────────────────────────────────────────────────
class ShaperProcessor
{
public:
    static constexpr int kTapCount    = 2048;              // runtime FIR length
    static constexpr int kDesignOrder = 12;                // 4096-pt design FFT
    static constexpr int kDesignSize  = 1 << kDesignOrder;
    static constexpr int kNumBins     = kDesignSize / 2;   // 2048 design bins (≈ 11.7 Hz @ 48 k)
    static constexpr int kLatency     = kTapCount / 2;     // 1024 samples
    static constexpr int kFadeSamples = 2048;              // ≈ 43 ms @ 48 kHz

    static constexpr int kPartition   = 128;               // direct head / FFT partition length
    static constexpr int kPartitions  = kTapCount / kPartition;
    static constexpr int kFftOrder    = 8;                 // 2 · kPartition

    static constexpr float kMaxCorrectionDb = 24.0f;       // per-bin clamp
    static constexpr float kLiveFloor       = 1.0e-4f;     // −80 dB: nothing to shape below

    ShaperProcessor();
    ~ShaperProcessor() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setEnabled (bool on)   { enabled.store (on); }
    bool isEnabled() const      { return enabled.load(); }

    // Publish freshly designed kTapCount impulse responses (message thread).
    // setFilter: one filter for L and R. setMidSideFilters: mid and side get
    // their own. nullptr taps = identity (a pure kLatency delay).
    void setFilter (const float* taps, int count);
    void setMidSideFilters (const float* midTaps, const float* sideTaps, int count);

    static constexpr int getLatencySamples() { return kLatency; }

    // In-place. R == nullptr for a mono stream (treated as all-mid).
    void process (float* L, float* R, int numSamples);

    // ── FIR design (any thread; pure functions) ─────────────────────────────
    // Spectra are linear magnitudes on the analyzer grid: `numBins` bins
    // spanning 0 … Nyquist (bin i at i·sampleRate / (2·numBins)).
    //
    // buildCorrection: the smoothed match curve in dB (× amount) on that grid.
    // A target bin ≤ 0 means "no target here": it takes the correction of the
    // nearest bins that have one (as does a bin where the live signal is below
    // kLiveFloor). The octave-weighted mean is removed so the match is
    // level-neutral; pass `forcedMeanDb` to remove a given offset instead (the
    // side filter uses the mid's, so relative side level — width — is matched
    // without re-levelling). Returns false (and a flat curve) when nothing is
    // comparable; `outMeanDb` receives the offset that was removed.
    static bool buildCorrection (const float* targetBins, const float* liveBins, int numBins,
                                 double sampleRate, float amount, std::vector<float>& outCurveDb,
                                 const float* forcedMeanDb = nullptr, float* outMeanDb = nullptr);

    // A correction curve (dB, analyzer grid) → kTapCount linear-phase taps
    // centred on kLatency.
    static void designFromCurve (const std::vector<float>& curveDb, std::vector<float>& outTaps);

    // buildCorrection + designFromCurve (identity when nothing is comparable).
    static void buildMatchFilter (const float* targetBins, const float* liveBins,
                                  int numBins, double sampleRate, float amount,
                                  std::vector<float>& outTaps,
                                  std::vector<float>* outCurveDb = nullptr);

    // Hand-drawn trace → target magnitudes on the analyzer grid. Points are
    // (Hz, dB) sorted by frequency; dB is on the analyzer's scale (0 dB =
    // full-scale sine). Interpolated linearly in log-frequency and converted
    // dB → linear explicitly. Outside the traced span the target is 0.
    struct TracePoint { float hz; float db; };
    static void buildTargetFromCurve (const std::vector<TracePoint>& curve,
                                      int numBins, double sampleRate,
                                      std::vector<float>& outTarget);

    // Magnitude response (dB) of a tap set at `hz` (tests / diagnostics).
    static float responseDb (const std::vector<float>& taps, double hz, double sampleRate);

private:
    static constexpr int kBinsPerPartition = kPartition + 1;   // spectra stored as interleaved re, im
    static constexpr int kTailParts        = kPartitions - 1;
    static constexpr int kSlots            = 5;    // active, previous, designed, pending + one to write
    static constexpr int kNone             = -2;   // no slot
    static constexpr int kIdentity         = -1;   // pure delay

    // A designed filter pair, pre-transformed for the partitioned engine.
    // Channel 0 = L/R (linked) or mid; channel 1 = side (mid/side only).
    struct Slot
    {
        bool midSide { false };
        std::array<std::vector<float>, 2>   head;    // first kPartition taps, reversed
        std::array<std::vector<float>, 2> parts;     // kTailParts × kBinsPerPartition spectra
    };

    static void buildSlotChannel (Slot&, const float* taps, int channel, int count);
    void publish (const Slot* s);   // nullptr = identity
    int  freeSlot() const;
    void computeTail (int slot, float* out);   // out: kPartition L then kPartition R
    void render (int slot, const float* tail, float delayedL, float delayedR, float& yL, float& yR) const noexcept;
    void blockComplete();
    void beginTransition (int target);

    // GUI → audio hand-over.
    juce::SpinLock lock;
    std::array<Slot, kSlots> slots;
    std::atomic<int> pendingSlot  { kNone };
    std::atomic<int> designedSlot { kIdentity };
    std::atomic<int> activeSlot   { kIdentity };
    std::atomic<int> previousSlot { kNone };
    std::atomic<int> pendingSerial { 0 };
    std::atomic<bool> enabled { false };
    int appliedSerial { -1 };
    int fadePos { kFadeSamples };      // == kFadeSamples → not fading

    // Audio-thread state (allocated in prepare).
    juce::dsp::FFT fft { kFftOrder };
    std::array<std::vector<float>, 4> headLines;     // L, R, M, S — mirrored, 2·kPartition
    static constexpr int kDelaySize = 2 * kLatency;  // power of two > kLatency
    std::array<std::vector<float>, 2> delayLines;    // L, R identity delay
    std::array<std::vector<float>, 2> blocks;        // L, R: previous + current partition
    std::array<std::vector<float>, 2> fdl;           // L, R: last kTailParts window spectra
    std::vector<float> tailActive, tailPrevious;     // 2 · kPartition
    std::vector<float> fftBuffer;                    // 4 · kPartition
    std::vector<float> accum;                        // one spectrum
    int pos { 0 }, delayIdx { 0 }, fdlNewest { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperProcessor)
};
