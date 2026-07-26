#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include "ApiClient.h"
#include "AudioAnalyzer.h"
#include "LicenseManager.h"
#include "AIAnalysis.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindProcessor  — now with DSP EQ that applies AI suggestions
// ─────────────────────────────────────────────────────────────────────────────
class MixMindProcessor : public juce::AudioProcessor
{
public:
    MixMindProcessor();
    ~MixMindProcessor() override = default;

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

    // ── Public API ────────────────────────────────────────────────────────
    ApiClient& getApiClient() { return apiClient; }
    LicenseManager& getLicenseManager() { return licenseManager; }

    // Auto-EQ: apply AI-suggested EQ changes to the audio
    void applyEQ (const std::vector<EQSuggestion>& suggestions);
    void clearEQ();
    bool isEQActive() const { return eqActive; }

    // Transport & DSP
    double currentBpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool isPlaying = false;
    AudioAnalyzer audioAnalyzer;

private:
    ApiClient apiClient;
    LicenseManager licenseManager;

    // DSP chain for auto-EQ
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter, Filter, Filter, Filter, Filter> eqChain;
    bool eqActive { false };
    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
