#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LicenseManager.h"

class ThreeFXProcessor : public juce::AudioProcessor
{
public:
    ThreeFXProcessor();
    ~ThreeFXProcessor() override = default;
    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JuicePipe 3FX"; }
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

    float phaseRate{0.5f}, phaseDepth{0.5f}, flangeRate{0.3f}, flangeDepth{0.4f}, delayTime{0.3f}, delayFb{0.4f}, mix{0.5f};
    LicenseManager licenseManager;

private:
    double sr{44100};
    float phaseLFO{0}, flangeLFO{0};
    juce::AudioBuffer<float> delayBuf;
    int delayPos{0}, delaySize{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeFXProcessor)
};
