#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include "AudioAnalyzer.h"

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
    const float* getBins() const { return refBins; }

    float getLufs()         const { return lufs; }
    float getStereoWidth()  const { return stereoWidth; }
    float getPhaseCorr()    const { return phaseCorr; }
    float getPeakDb()       const { return peakDb; }
    float getCrestFactor()  const
    {
        if (peakDb <= -180.0f || rmsDb <= -180.0f) return 0.0f;
        return juce::jmax (0.0f, peakDb - rmsDb);
    }

private:
    juce::AudioFormatManager formatManager;

    bool loaded { false };
    float refBins[AudioAnalyzer::numBins] { 0.0f };
    float lufs        { -60.0f };   // honest BS.1770 integrated (fallback: short-term)
    float stereoWidth { 0.5f };
    float phaseCorr   { 1.0f };
    float peakDb      { -180.0f };  // 4x-oversampled true peak
    float rmsDb       { -180.0f };
    juce::String fileName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceAnalyzer)
};
