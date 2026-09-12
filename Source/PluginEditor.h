#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "TelemetryCanvas.h"
#include "FocusModel.h"
#include "LookAndFeel.h"

class MixMindEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit MixMindEditor (MixMindProcessor&);
    ~MixMindEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    MixMindProcessor& audioProcessor;
    JuicePipeLAF      laf;

    TelemetryCanvas   telemetry;

    // Header
    juce::Label      titleLabel;
    juce::TextButton loadRefButton { "LOAD REF" };
    juce::TextButton layerButton { "LAYER" };
    juce::TextButton traceButton { "TRACE" };
    juce::Slider     opacitySlider;

    // Shaper controls
    juce::TextButton shapeButton { "SHAPE" };
    juce::TextButton modeButton  { "AUTO" };
    juce::Slider     amountSlider;

    juce::ComboBox   focusBox;
    ColorSwatch      colorSwatch;

    void loadReference();
    void loadImageLayer();
    void applyFocusSelection();
    void adoptReferenceIntoUi();
    void syncTraceToProcessor();
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool manualMode { false };
    int  dotPhase { 0 };
    MixMindState::TraceCurve lastSyncedTrace;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
