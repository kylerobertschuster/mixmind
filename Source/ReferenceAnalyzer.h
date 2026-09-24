#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include "AudioAnalyzer.h"
#include "ChannelMode.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ReferenceAnalyzer — loads a reference track and produces the same
//  normalized FFT magnitude spectrum + scalar telemetry as the live analyzer,
//  so the reference and user curves overlay on a shared frequency axis.
// ─────────────────────────────────────────────────────────────────────────────
class ReferenceAnalyzer
{
public:
    ReferenceAnalyzer();
    ~ReferenceAnalyzer() = default;

    // Load + analyze. `liveSampleRate` resamples the file so bins line up
    // with the live AudioAnalyzer. Returns false with `errorMessage` on fail.
    bool loadFile (const juce::File& file, double liveSampleRate, juce::String& errorMessage);

    void clear();

    bool hasReference() const { return loaded; }
    juce::String getFileName() const { return fileName; }

    static constexpr int numBins = AudioAnalyzer::numBins;

    // Spectrum of the currently-selected channel (linear magnitude 0..1).
    // Stereo and Mid share the mid (mono-downmix) spectrum.
    const float* getBins() const
    {
        switch (channelMode)
        {
            case ChannelMode::Left:  return leftBins;
            case ChannelMode::Right: return rightBins;
            case ChannelMode::Side:  return sideBins;
            case ChannelMode::Mid:
            case ChannelMode::Stereo:
            default:                 return midBins;
        }
    }

    void setChannelMode (ChannelMode m) { channelMode = m; }
    ChannelMode getChannelMode() const { return channelMode; }

    float getLufs()         const { return lufs; }
    float getStereoWidth()  const { return stereoWidth; }
    float getPhaseCorr()    const { return phaseCorr; }
    float getPeakDb()       const { return peakDb; }
    float getCrestFactor()  const
    {
        if (peakDb <= -180.0f || rmsDb <= -180.0f) return 0.0f;
        return juce::jmax (0.0f, peakDb - rmsDb);
    }

    // ── Session recall ──────────────────────────────────────────────────────
    // A reference is not an automatable parameter, so it rides along as a
    // child of the plugin state tree. The 1024 bins go in as one base64 blob —
    // as individual XML elements they would add ~40 KB to every saved session.
    //
    // The analysed bins are stored rather than the file path on purpose: the
    // session then recalls identically even if the reference file has since
    // been moved, re-encoded, or deleted.
    void writeToTree (juce::ValueTree& tree) const;
    bool readFromTree (const juce::ValueTree& tree);

private:
    juce::AudioFormatManager formatManager;

    bool loaded { false };
    float midBins[AudioAnalyzer::numBins]   { 0.0f };
    float sideBins[AudioAnalyzer::numBins]  { 0.0f };
    float leftBins[AudioAnalyzer::numBins]  { 0.0f };
    float rightBins[AudioAnalyzer::numBins] { 0.0f };
    ChannelMode channelMode { ChannelMode::Stereo };
    float lufs        { -60.0f };   // honest BS.1770 integrated (fallback: short-term)
    float stereoWidth { 0.5f };
    float phaseCorr   { 1.0f };
    float peakDb      { -180.0f };  // 4x-oversampled true peak
    float rmsDb       { -180.0f };
    juce::String fileName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceAnalyzer)
};
