#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <atomic>
#include "ChannelMode.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ShaperProcessor — the reference-matching EQ ("the shaper").
//
//  Applies a linear-phase match-EQ FIR to the signal so the track's spectrum is
//  reshaped toward a target:
//    · AUTO   → target = the uploaded reference's spectrum
//    · MANUAL → target = the hand-drawn trace curve
//  Both reduce to "gain = target − live", so one engine serves both modes.
//
//  The FIR is designed OFF the audio thread (by the editor) and published
//  atomically. When bypassed the processor is fully transparent (0 latency).
// ─────────────────────────────────────────────────────────────────────────────
class ShaperProcessor
{
public:
    static constexpr int kTapCount    = 1024;              // runtime FIR length
    static constexpr int kDesignOrder = 11;                // 2048-pt design FFT
    static constexpr int kDesignSize  = 1 << kDesignOrder;
    static constexpr int kNumBins     = kDesignSize / 2;   // 1024 spectral bins
    static constexpr int kLatency     = kTapCount / 2;     // 512 samples

    ShaperProcessor() = default;
    ~ShaperProcessor() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setEnabled (bool on)                 { enabled.store (on); }
    bool isEnabled() const                    { return enabled.load(); }

    // Which part of the stereo signal the FIR is applied to.
    void setChannelMode (ChannelMode m)       { mode.store ((int) m); }
    ChannelMode getChannelMode() const        { return (ChannelMode) mode.load(); }

    // Publish a freshly-designed impulse response (GUI thread). A null/empty
    // set is allowed and is treated as transparent.
    void setFilter (const float* taps, int count);

    // True latency of the currently-active engine (0 when bypassed). While
    // enabled the signal is delayed by kLatency even before a filter arrives, so
    // this never disagrees with what process() actually does.
    int getLatencySamples() const             { return enabled.load() ? kLatency : 0; }

    // Processes one stereo block in-place. When bypassed, in == out (unchanged).
    // The channel mode selects whether the FIR hits both channels (Stereo),
    // one side (Left/Right), or the mid/side component.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    // ── FIR design (GUI-thread safe) ────────────────────────────────────────
    // targetBins / liveBins: linear-magnitude spectra (0..1) over 0..Nyquist,
    // each `numBins` long. amount: 0..1 (0 = flat, 1 = full match). Produces
    // kTapCount taps, DC-normalised so the shaper only reshapes, never re-levels.
    static void buildMatchFilter (const float* targetBins, const float* liveBins,
                                  int numBins, float amount,
                                  std::vector<float>& outTaps);

    // Builds a target magnitude spectrum from a hand-drawn curve (freqHz, value01).
    // value01 is the trace's Y in the plot's dB-mapped scale (0..1 ↔ −100..0 dBFS
    // magnitude); it is converted to linear magnitude here so buildMatchFilter()
    // can subtract it (in dB) from the live spectrum.
    static void buildTargetFromCurve (const juce::Array<std::pair<float, float>>& curve,
                                      int numBins, double sampleRate,
                                      std::vector<float>& outTarget);

private:
    // Double-buffered coefficients: audio thread reads [activeIdx], GUI writes the
    // other buffer under the lock, then flips activeIdx.
    std::vector<float> taps[2];
    std::atomic<int>   activeIdx  { 0 };
    std::atomic<int>   activeTaps { 0 };
    std::atomic<bool>  enabled    { false };
    std::atomic<int>   mode       { (int) ChannelMode::Stereo };
    juce::SpinLock     lock;

    // Delay lines (power-of-two length ≥ 2·kTapCount for cheap masking).
    static constexpr int kDelayLen = 2048;
    static constexpr int kDelayMask = kDelayLen - 1;
    std::vector<float> delayL, delayR;

    // The matched component comes out of the FIR kLatency samples late. Any
    // component left dry (the unselected side, or the untouched half of a
    // mid/side split) has to be delayed by the same amount, or it would arrive
    // ahead of the filtered part and skew the stereo image. These hold the raw
    // input, so the dry read is independent of what the FIR delay lines hold.
    std::vector<float> dryL, dryR;
    int writeIdx { 0 };

    // Audio-thread snapshot of the active taps (taken once per block).
    std::vector<float> curTaps;

    // Single-sample FIR convolution over one delay line (read at writeIdx,
    // walking backwards).
    static float applyFir (const float* h, int M, const std::vector<float>& delay, int writeIdx);

    double sampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperProcessor)
};
