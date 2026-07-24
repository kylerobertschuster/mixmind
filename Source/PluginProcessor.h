#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "ApiClient.h"
#include "AudioAnalyzer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindProcessor
//  MixMind is a utility/tool plugin — it produces no audio output.
//  All it needs to do is host the editor and own the ApiClient.
// ─────────────────────────────────────────────────────────────────────────────
class MixMindProcessor : public juce::AudioProcessor
{
public:
    MixMindProcessor();
    ~MixMindProcessor() override = default;

    // ── AudioProcessor interface ───────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MixMind"; }

    bool   acceptsMidi()               const override { return false; }
    bool   producesMidi()              const override { return false; }
    bool   isMidiEffect()              const override { return false; }
    double getTailLengthSeconds()      const override { return 0.0; }

    int  getNumPrograms()              override { return 1; }
    int  getCurrentProgram()           override { return 0; }
    void setCurrentProgram (int)       override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation  (juce::MemoryBlock& dest) override;
    void setStateInformation  (const void* data, int size) override;

    // ── Public API for the editor ──────────────────────────────────────────
    ApiClient& getApiClient() { return apiClient; }

    // Persist context across sessions via plugin state
    juce::String savedGenre;
    juce::String savedDaw;

    // Transport & DSP
    double currentBpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool isPlaying = false;

    AudioAnalyzer audioAnalyzer;

private:
    ApiClient apiClient;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
