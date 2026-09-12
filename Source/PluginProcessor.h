#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioAnalyzer.h"
#include "ShaperProcessor.h"
#include "ReferenceAnalyzer.h"
#include "MatchState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMind — reference-matching spectrum shaper + analyzer.
//    · AUTO   mode: reshapes the live spectrum toward the uploaded reference.
//    · MANUAL mode: reshapes toward the hand-drawn trace curve.
//  Analysis (honest BS.1770 metering + averaged FFT) runs continuously; the
//  shaper is the insert path (latency-compensated, bypass = transparent).
//
//  The reference and the drawn curve are owned here rather than by the editor.
//  The editor is destroyed whenever the host closes the window — which is the
//  whole of an offline bounce — so anything it owned could neither be saved nor
//  keep working.
// ─────────────────────────────────────────────────────────────────────────────
class MixMindProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    MixMindProcessor();
    ~MixMindProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MixMind"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
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
    ReferenceAnalyzer referenceAnalyzer;

    // ── Session-recallable match state ──────────────────────────────────────
    // The drawn curve in normalised (frequencyHz, value01) space, sorted.
    MixMindState::TraceCurve getTraceCurve() const;
    void setTraceCurve (MixMindState::TraceCurve curve);
    bool hasTrace() const;
    void clearTrace();

private:
    void timerCallback() override;
    void designMatchFilter();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int reportedLatency { 0 };

    MixMindState::TraceCurve traceCurve;

    // setStateInformation() is not guaranteed to arrive on the message thread,
    // so the curve is guarded rather than assumed single-threaded.
    mutable juce::SpinLock traceLock;
    bool prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
