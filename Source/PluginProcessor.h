#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ApiClient.h"
#include "AudioAnalyzer.h"
#include "LicenseManager.h"

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
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    ApiClient& getApiClient() { return apiClient; }
    LicenseManager& getLicenseManager() { return licenseManager; }
    AudioAnalyzer audioAnalyzer;

private:
    ApiClient apiClient;
    LicenseManager licenseManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
