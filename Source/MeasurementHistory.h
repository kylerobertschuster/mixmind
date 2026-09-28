#pragma once
#include <array>
#include <vector>
#include "AudioAnalyzer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MeasurementHistory — the analysis window Mix Doctor observes. Keeps the
//  most recent frames (60 s by default) from one AudioAnalyzer and computes
//  statistics over any stretch of them. Message thread only.
//
//  Everything is computed from whole 100 ms frames, exactly:
//    · integrated loudness per BS.1770-4 (400 ms blocks, 75 % overlap,
//      −70 LUFS absolute and −10 LU relative gates, on block energies);
//    · loudness range per EBU Tech 3342 (3 s blocks at 10 Hz, −70 LUFS
//      absolute and −20 LU relative gates, 10th–95th percentile);
//    · max true peak, and — over the frames that carry signal (mean square
//      above −70 dBFS, the analyzer's own gate) — RMS, crest, stereo
//      correlation and width, octave-band levels and mono fold-down. (Gated
//      on the frame's own unweighted level: the K-weighting filter rings on
//      for a moment after audio stops, which would let the first silent
//      frame through a loudness gate.)
//  Blocks never span a missing frame; frames from before the last prepare()
//  (another epoch or rate) start a new history.
// ─────────────────────────────────────────────────────────────────────────────
class MeasurementHistory
{
public:
    static constexpr int   kBands = MeasurementFrame::kBands;
    static constexpr float kSilenceDb = LoudnessMeter::kSilenceDb;
    static constexpr float  kGateLufs  = -70.0f;
    static constexpr double kFrameGateMeanSquare = 1.0e-7;   // −70 dBFS
    static constexpr float kMonoFloorDb = -60.0f;

    explicit MeasurementHistory (double secondsKept = 60.0);

    void drain (AudioAnalyzer&);   // pops every pending frame
    void add (const MeasurementFrame&);
    void clear();

    bool   empty() const { return count == 0; }
    int    generation() const { return generationCount; }   // bumps whenever the history restarts
    juce::uint32 latestIndex() const;   // index of the newest frame (0 if empty)
    double secondsAvailable() const;

    struct Stats
    {
        int    frames { 0 };            // frames present in the window
        int    missingFrames { 0 };     // frames the window lost (dropped before they arrived)
        double seconds { 0.0 };         // audio covered by the present frames
        double gatedSeconds { 0.0 };    // of which carried signal (above −70 dBFS)

        float integratedLufs  { kSilenceDb };   // BS.1770-4
        float loudnessRangeLu { 0.0f };         // EBU Tech 3342; 0 with too little signal
        float truePeakDb      { kSilenceDb };
        float rmsDb           { kSilenceDb };   // unweighted, gated frames
        float crestDb         { 0.0f };         // truePeakDb − rmsDb; 0 when silent
        float correlation     { 1.0f };         // gated frames
        float width           { 0.0f };         // side / mid, RMS

        // Octave bands (MeasurementFrame::bandCentreHz), gated frames with
        // spectral data. bandDb: 20·log10 of the mean bin magnitude on the
        // analyzer grid (kSilenceDb if the band has no data). monoLossDb:
        // what folding to mono costs the band, 10·log10(mid / (mid + side)),
        // floored at kMonoFloorDb (0 = nothing lost, −3 ≈ uncorrelated).
        std::array<bool,  kBands> bandHasData {};
        std::array<float, kBands> bandDb {};
        std::array<float, kBands> monoLossDb {};
    };

    // All frames with index ≥ firstIndex (e.g. latestIndex() + 1 when a Mix
    // Doctor run starts).
    Stats statsSince (juce::uint32 firstIndex) const;
    // The newest `seconds` of audio.
    Stats statsForLast (double seconds) const;

private:
    const MeasurementFrame& at (int i) const;   // i = 0 oldest … count−1 newest
    Stats compute (int from, int to) const;     // frames [from, to)

    std::vector<MeasurementFrame> ring;
    int head { 0 }, count { 0 };
    bool haveEpoch { false };
    int  generationCount { 0 };
    juce::uint32 epoch { 0 };
    double sampleRate { 0.0 };
};
