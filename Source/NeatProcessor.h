#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LicenseManager.h"

class NeatProcessor : public juce::AudioProcessor
{
public:
    NeatProcessor();
    ~NeatProcessor() override = default;
    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JuicePipe Neat Neat Neat"; }
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

    float repeats[3]{4,4,4};
    float decay[3]{0.6f,0.5f,0.4f};
    float pitch[3]{0,3,-2};
    float mix{0.5f};
    bool active{true};
    LicenseManager licenseManager;

private:
    double sr{44100};
    juce::AudioBuffer<float> delayBuf[3];
    int delayPos[3]{0}, delaySize[3]{0};
    float pitchPhase[3]{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeatProcessor)
};
