#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ChatComponent.h"
#include "ContextPanel.h"
#include "TelemetryCanvas.h"
#include "ReferenceAnalyzer.h"
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

    // Layout
    TelemetryCanvas   telemetry;
    ChatComponent     chatComponent;
    StrawPanel        strawPanel;
    ReferenceAnalyzer referenceAnalyzer;

    // Header
    juce::Label      titleLabel;
    juce::TextButton licenseButton { "LICENSE" };
    juce::TextButton aiButton { "AI" };
    juce::TextButton loadRefButton { "LOAD REF" };
    juce::ComboBox   focusBox;
    ColorSwatch      colorSwatch;

    // Chat state
    std::vector<ChatMessage> history;
    MessageBubble*           thinkingBubble { nullptr };
    bool                     waitingForReply { false };
    bool                     chatVisible { false };   // AI is demoted: collapsed by default

    void handleUserMessage (const juce::String& text);
    void setChatVisible (bool visible);
    void showLicenseDialog();
    void updateLicenseDisplay();
    void loadReference();
    void applyFocusSelection();
    void timerCallback() override;

    int dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
