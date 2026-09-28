#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include "LoudnessMeter.h"
#include "SnapshotBuffer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MeasurementFrame — one 100 ms step of analysed audio (a LoudnessMeter step,
//  so frames tile the audio exactly whatever the host block size). Plain data
//  of a fixed size: it travels from the audio thread through a lock-free FIFO.
//  MeasurementHistory turns runs of frames into statistics.
// ─────────────────────────────────────────────────────────────────────────────
struct MeasurementFrame
{
    // Octave bands, centres 31.25 Hz · 2^k (≈ 31.5 Hz … 16 kHz).
    static constexpr int kBands = 10;
    static double bandCentreHz (int band) { return 31.25 * std::pow (2.0, band); }

    juce::uint32 epoch { 0 };    // bumps on every prepare(): a new run of frames
    juce::uint32 index { 0 };    // step number within the epoch; a gap = frames lost
    double sampleRate  { 0.0 };
    int    samples     { 0 };    // samples in the step (≈ 100 ms)

    double kEnergy { 0.0 };      // Σ K-weighted squares, summed over channels (BS.1770)
    double energy  { 0.0 };      // Σ unweighted squares, averaged over channels
    float  truePeak { 0.0f };    // max true peak in the step (linear, 4× oversampled)
    double ll { 0.0 }, rr { 0.0 }, lr { 0.0 };   // Σ L², Σ R², Σ L·R (stereo image)

    // Spectrum, averaged over the analyzer FFT frames that completed in this
    // step (fftFrames; 0 = no spectral data yet). Per octave band on the
    // analyzer's own bin grid, so compare only with a reference mapped onto
    // the same grid (ReferenceAnalyzer::mapToGrid, AudioAnalyzer::bandMagnitudes).
    int fftFrames { 0 };
    std::array<float, kBands> bandMag {};       // mid (L+R)/2: mean bin magnitude, analyzer scale
    std::array<float, kBands> bandMidPow {};    // mid: mean bin power
    std::array<float, kBands> bandSidePow {};   // side (L−R)/2: mean bin power
};

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
//                    does not chase individual hits. Kept for the side
//                    signal (L−R)/2 too, for mid/side matching.
//    · Honest BS.1770 metering via LoudnessMeter.
//    · Measurement frames (100 ms) for MeasurementHistory / Mix Doctor.
//
//  Threads: process() on the audio thread. Spectra and frames are published
//  whole — spectra through a triple buffer, frames through a lock-free FIFO —
//  to ONE reader thread (the message thread). Scalars are atomics.
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

    // One consistent set of spectra (analyzer scale, numBins each).
    struct Spectra
    {
        float display[numBins] {};        // ~200 ms average
        float longTerm[numBins] {};       // ~3 s gated average, mid — the match source
        float longTermSide[numBins] {};   // same, side (L−R)/2
        int   longTermFrames { 0 };       // 0 = no long-term data yet
        bool  hasLongTerm() const { return longTermFrames > 0; }
    };

    // The newest complete spectra. Reader thread only (the message thread):
    // the reference stays valid and unchanged until the next call, so take it
    // once per callback and don't keep it.
    const Spectra& getSpectra() { return spectra.latest(); }

    // Measurement frames, oldest first. Reader thread only. Frames that did
    // not fit (nobody reading) are dropped — their index is missing.
    static constexpr int kFrameQueue = 256;   // ≈ 25 s of audio
    bool popFrame (MeasurementFrame& out);

    // Mean bin magnitude per octave band of an analyzer-grid spectrum
    // (numBins bins at sampleRate / fftSize); 0 for a band with no bins.
    static void bandMagnitudes (const float* bins, double sampleRate, float* outBands);
    static int  bandBinCount (int band, double sampleRate);   // 0 = band has no bins at this rate

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
    void analyse (const float* L, const float* R, int n);
    void performFFT();
    void closeFrame();
    void publishSpectra();

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    float windowPower { 1.0f };   // Σ w², for band-power normalisation

    float fifo[fftSize] { 0 };
    float sideFifo[fftSize] { 0 };
    int   fifoIdx { 0 };
    int   hopCount { 0 };
    bool  fifoFull { false };
    float fftData[2 * fftSize] { 0 };
    float midMag[numBins] { 0 };

    // Writer-side working copies; published whole through `spectra`.
    float fftAvg[numBins] { 0 };
    float longTerm[numBins] { 0 };
    float longTermSide[numBins] { 0 };
    int   longTermFrames { 0 };
    SnapshotBuffer<Spectra> spectra;

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

    // Measurement frames (audio thread), handed over through a SPSC FIFO.
    std::array<std::pair<int, int>, MeasurementFrame::kBands> bandBins {};   // [first, end) per band
    MeasurementFrame frame;
    juce::uint32 epoch { 0 }, frameIndex { 0 };
    juce::AbstractFifo frameFifo { kFrameQueue };
    std::array<MeasurementFrame, kFrameQueue> frameSlots {};

    LoudnessMeter loudnessMeter;
    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
