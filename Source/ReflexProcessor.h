#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LicenseManager.h"

class ReflexProcessor : public juce::AudioProcessor
{
public:
    ReflexProcessor();
    ~ReflexProcessor() override = default;

    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JuicePipe Reflex"; }
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

    // Parameters
    float& getResFreq()     { return resFreq; }
    float& getResonance()   { return resonance; }
    float& getMix()         { return mix; }
    float& getDelayTime()   { return delayTime; }
    float& getFeedback()    { return feedback; }
    float& getReverbAmt()   { return reverbAmt; }
    float& getSeqRate()     { return seqRate; }
    int   getSeqStep()      { return seqStep; }
    void  setSeqStep (int s) { seqStep = s % 8; }

    // Note sequence (8 semitone offsets from base frequency)
    static constexpr int kSeqLen = 8;
    float sequence[kSeqLen] = { 0, 2, -3, 5, -2, 4, -5, 3 };

    LicenseManager licenseManager;

private:
    double sampleRate { 44100 };
    float resFreq    { 440.0f };
    float resonance  { 5.0f };
    float mix        { 0.5f };
    float delayTime  { 0.25f };
    float feedback   { 0.3f };
    float reverbAmt  { 0.2f };
    float seqRate    { 0.25f }; // quarters per step
    int   seqStep    { 0 };

    // Resonator filters (parallel bandpass)
    juce::dsp::IIR::Filter<float> resonatorL, resonatorR;

    // Simple delay line buffers
    juce::AudioBuffer<float> delayBuffer;
    int delayWritePos { 0 };
    int delaySize { 0 };

    // Simple reverb
    struct CombFilter { float buffer[2400] {0}; int idx{0}; float fb{0.5f}; };
    CombFilter combL[4], combR[4];
    float lpL {0}, lpR {0};

    // Transient detection
    float envelope { 0 };
    int samplesSinceTrigger { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReflexProcessor)
};
