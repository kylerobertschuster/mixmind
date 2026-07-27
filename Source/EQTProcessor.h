#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AudioAnalyzer.h"
#include "LicenseManager.h"

class EQTProcessor : public juce::AudioProcessor
{
public:
    EQTProcessor();
    ~EQTProcessor() override = default;

    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JuicePipe EQT"; }
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

    // EQ control
    struct EQBand { float freq = 1000, gain = 0, q = 1.0f; int type = 0; bool active = false; };
    void setBands (const std::vector<EQBand>& bands);
    void clearBands();
    bool isActive() const { return eqActive; }

    AudioAnalyzer audioAnalyzer;
    LicenseManager licenseManager;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::ProcessorChain<Filter,Filter,Filter,Filter,Filter,Filter,Filter,Filter> chain;
    bool eqActive { false };
    double sampleRate { 44100 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQTProcessor)
};
