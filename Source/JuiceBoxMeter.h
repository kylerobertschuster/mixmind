#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  JuiceBoxMeter  — floating/collapsible telemetry panel styled as a juice box
//  Shows LUFS, Peak, Crest, Phase, Stereo Width in a pink rounded container.
// ─────────────────────────────────────────────────────────────────────────────
class JuiceBoxMeter : public juce::Component,
                      private juce::Timer
{
public:
    JuiceBoxMeter();
    ~JuiceBoxMeter() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateFromJson (const juce::String& json);
    void setCollapsed (bool c) { collapsed = c; repaint(); resized(); }
    bool isCollapsed() const { return collapsed; }

private:
    void timerCallback() override;
    void drawMeterBar (juce::Graphics& g, float x, float y, float w, float h,
                       float value, float minVal, float maxVal,
                       const juce::String& label, const juce::String& unit,
                       bool warn = false);

    // Animated values
    float aLufs    { -60 };  float lufs    { -60 };
    float aPeak    { -60 };  float peak    { -60 };
    float aCrest   { 0 };    float crest   { 0 };
    float aPhase   { 1 };    float phase   { 1 };
    float aStereo  { 0.5f }; float stereo  { 0.5f };
    float aBass    { 0 };    float bassE   { 0 };
    float aMid     { 0 };    float midE    { 0 };
    float aHigh    { 0 };    float highE   { 0 };

    bool collapsed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceBoxMeter)
};
