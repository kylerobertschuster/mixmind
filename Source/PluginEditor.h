#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ChatComponent.h"
#include "ContextPanel.h"
#include "AnalyzerCanvas.h"
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
    AnalyzerCanvas analyzer;
    ChatComponent  chatComponent;
    StrawPanel     strawPanel;

    // Header
    juce::Label      titleLabel;
    juce::TextButton licenseButton { "LICENSE" };

    // Chat state
    std::vector<ChatMessage> history;
    MessageBubble*           thinkingBubble { nullptr };
    bool                     waitingForReply { false };

    void handleUserMessage (const juce::String& text);
    void showLicenseDialog();
    void updateLicenseDisplay();
    void timerCallback() override;

    int dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
