#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ChatComponent.h"
#include "ContextPanel.h"
#include "PipeVisualizer.h"
#include "PresetManager.h"
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindEditor  — surgical telemetry layout
//  Pipe visualizer (left) + Chat (center) + Presets panel (right)
// ─────────────────────────────────────────────────────────────────────────────
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

    // Header
    juce::Label      titleLabel;
    juce::Label      statusLabel;
    juce::TextButton licenseButton { "LICENSE" };

    // Panels
    PipeVisualizer pipeVis;         // left — the glass telemetry channel
    ChatComponent  chatComponent;   // center — the bowl
    StrawPanel     strawPanel;      // right — presets & controls

    // Conversation
    std::vector<ChatMessage> history;
    MessageBubble*           thinkingBubble { nullptr };
    bool                     waitingForReply { false };

    // Helpers
    void handleUserMessage (const juce::String& text);
    void setStatus (const juce::String& text);
    void timerCallback() override;
    void showLicenseDialog();
    void updateLicenseDisplay();

    int  dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
