#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioAnalyzer.h"
#include "ShaperProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMind — reference-matching spectrum shaper + analyzer.
//    · AUTO   mode: reshapes the live spectrum toward the uploaded reference.
//    · MANUAL mode: reshapes toward the hand-drawn trace curve.
//  Analysis (honest BS.1770 metering + averaged FFT) runs continuously; the
//  shaper is the insert path (latency-compensated, bypass = transparent).
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
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    AudioAnalyzer   audioAnalyzer;
    ShaperProcessor shaper;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int reportedLatency { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
