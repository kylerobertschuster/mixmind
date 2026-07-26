#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  PipeVisualizer  — DMG-inspired precision telemetry channel
//  A glass-encased vertical meter with exact numerical readouts.
//  Simple View: spectral bands + LUFS.  Advanced: adds crest, phase, true peak.
// ─────────────────────────────────────────────────────────────────────────────
class PipeVisualizer : public juce::Component,
                       private juce::Timer
{
public:
    PipeVisualizer();
    ~PipeVisualizer() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateFromJson (const juce::String& json);

    // Flavor accent control
    void setAccent (juce::Colour c) { accent = c; repaint(); }

    // DMG-style view modes
    void setAdvancedMode (bool advanced) { advancedMode = advanced; repaint(); }
    bool isAdvancedMode() const { return advancedMode; }

private:
    void timerCallback() override;
    void drawPipeChannel (juce::Graphics& g);
    void drawReadouts (juce::Graphics& g);

    // Parsed values
    float bass     { 0.0f }; float high     { 0.0f };
    float mid      { 0.0f }; float lufs     { -60.0f };
    float stereo   { 0.5f }; float crest    { 0.0f };
    float peak     { -60.0f };

    // Animated (smoothed)
    float aBass    { 0.0f }; float aHigh    { 0.0f };
    float aMid     { 0.0f }; float aLufs    { -60.0f };
    float aStereo  { 0.5f }; float aCrest   { 0.0f };
    float aPeak    { -60.0f };

    juce::Colour accent    { JP::accentMixMind };
    bool         advancedMode { false };

    float pipeX { 0 }, pipeW { 0 }, pipeY { 0 }, pipeH { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PipeVisualizer)
};
