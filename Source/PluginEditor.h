#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include "PluginProcessor.h"
#include "ChatComponent.h"
#include "ContextPanel.h"
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindEditor  — 800 × 560 plugin window
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

    // ── Layout ──────────────────────────────────────────────────────────────
    static constexpr int kHeaderH    = 48;
    static constexpr int kSidebarW   = 240;
    static constexpr int kEditorW    = 860;
    static constexpr int kEditorH    = 580;

    // ── Header widgets ───────────────────────────────────────────────────────
    juce::Label   titleLabel;
    juce::Label   statusLabel;

    // ── Panels ───────────────────────────────────────────────────────────────
    ContextPanel  contextPanel;
    ChatComponent chatComponent;

    // ── Conversation history ─────────────────────────────────────────────────
    std::vector<ChatMessage> history;
    MessageBubble*           thinkingBubble { nullptr };
    bool                     waitingForReply { false };

    // ── Helpers ──────────────────────────────────────────────────────────────
    void handleUserMessage (const juce::String& text);
    void setStatus (const juce::String& text, bool live = false);
    void timerCallback() override; // pulses the status dot

    juce::Colour statusColour { MM::text2 };
    int          dotPhase     { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
