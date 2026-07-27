#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioAnalyzer.h"
#include "LicenseManager.h"

class MeterProcessor : public juce::AudioProcessor
{
public:
    MeterProcessor();
    ~MeterProcessor() override = default;

    void prepareToPlay (double sr, int bs) override { audioAnalyzer.prepare (sr, bs); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd; audioAnalyzer.process (buffer);
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JuicePipe Meter"; }
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

    AudioAnalyzer audioAnalyzer;
    LicenseManager licenseManager;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterProcessor)
};
