#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
//  ShaperProcessor — the reference-matching EQ ("the shaper").
//
//  Applies a linear-phase match-EQ FIR so the track's spectrum is reshaped
//  toward a target:
//    · AUTO   → target = the uploaded reference's spectrum
//    · MANUAL → target = the hand-drawn trace curve
//  Both reduce to "gain = target − live", so one engine serves both modes.
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
//  The FIR is designed OFF the audio thread and handed over under a SpinLock
//  that the audio thread only try-locks.
// ─────────────────────────────────────────────────────────────────────────────
class ShaperProcessor
{
public:
    static constexpr int kTapCount    = 1024;              // runtime FIR length
    static constexpr int kDesignOrder = 11;                // 2048-pt design FFT
    static constexpr int kDesignSize  = 1 << kDesignOrder;
    static constexpr int kNumBins     = kDesignSize / 2;   // 1024 spectral bins
    static constexpr int kLatency     = kTapCount / 2;     // 512 samples
    static constexpr int kFadeSamples = 2048;              // ≈ 43 ms @ 48 kHz

    static constexpr float kMaxCorrectionDb = 24.0f;       // per-bin clamp
    static constexpr float kLiveFloor       = 1.0e-4f;     // −80 dB: nothing to shape below

    ShaperProcessor() = default;
    ~ShaperProcessor() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setEnabled (bool on)   { enabled.store (on); }
    bool isEnabled() const      { return enabled.load(); }

    // Publish a freshly designed kTapCount impulse response (GUI thread).
    // nullptr = identity (a pure kLatency delay).
    void setFilter (const float* taps, int count);

    static constexpr int getLatencySamples() { return kLatency; }

    // In-place. R == nullptr for a mono stream.
    void process (float* L, float* R, int numSamples);

    // ── FIR design (any thread; pure functions) ─────────────────────────────
    // targetBins / liveBins: linear magnitudes on the analyzer grid (bin i at
    // i·sampleRate/kDesignSize). A target bin ≤ 0 means "no target here": it
    // takes the correction of the nearest bins that have one (as does a bin
    // where the live signal is below kLiveFloor). amount: 0..1. Produces
    // kTapCount taps centred on kLatency. If outCurveDb is given it receives the per-bin correction in
    // dB (after smoothing and amount) — the curve the UI draws.
    static void buildMatchFilter (const float* targetBins, const float* liveBins,
                                  int numBins, double sampleRate, float amount,
                                  std::vector<float>& outTaps,
                                  std::vector<float>* outCurveDb = nullptr);

    // Hand-drawn trace → target magnitudes. Points are (Hz, dB) sorted by
    // frequency; dB is on the analyzer's scale (0 dB = full-scale sine).
    // Interpolated linearly in log-frequency and converted dB → linear
    // explicitly. Outside the traced span the target is 0 ("no target").
    struct TracePoint { float hz; float db; };
    static void buildTargetFromCurve (const std::vector<TracePoint>& curve,
                                      int numBins, double sampleRate,
                                      std::vector<float>& outTarget);

    // Magnitude response (dB) of a tap set at `hz` (tests / diagnostics).
    static float responseDb (const std::vector<float>& taps, double hz, double sampleRate);

private:
    struct TapSet
    {
        std::vector<float> reversed;   // h[kTapCount-1-j], for a contiguous dot product
        bool identity { true };
    };

    // GUI → audio hand-over.
    juce::SpinLock     lock;
    std::vector<float> pendingTaps;
    bool               pendingIdentity { true };
    std::atomic<int>   pendingSerial { 0 };
    std::atomic<bool>  enabled { false };

    // Audio-thread state.
    TapSet designed, active, previous;
    int  appliedSerial { 0 };
    bool appliedEnabled { false };
    int  fadePos { kFadeSamples };      // == kFadeSamples → not fading

    // Mirrored delay lines: each sample is stored at w and w + kTapCount, so
    // the newest kTapCount samples are always contiguous at [w+1, w+kTapCount].
    std::vector<float> lineL, lineR;
    int writeIdx { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperProcessor)
};
