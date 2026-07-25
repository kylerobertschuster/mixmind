#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include "PluginProcessor.h"
#include "ChatComponent.h"
#include "ContextPanel.h"
#include "PresetManager.h"
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindEditor  — pipe-device layout
//  Straw sidebar (left) + Bowl/chat area (right)
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
    MixMindLAF        laf;

    static constexpr int kSidebarW = 240;
    static constexpr int kEditorW  = 1024;
    static constexpr int kEditorH  = 680;

    // Header
    juce::Label      titleLabel;
    juce::Label      statusLabel;
    juce::TextButton licenseButton { "LICENSE" };

    // Panels
    StrawPanel    strawPanel;
    ChatComponent chatComponent;

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

    juce::Colour statusColour { MM::text2 };
    int          dotPhase     { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
