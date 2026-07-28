#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MessageBubble  — renders a single chat message
// ─────────────────────────────────────────────────────────────────────────────
class MessageBubble : public juce::Component
{
public:
    enum class Role { User, AI };

    MessageBubble (Role r, const juce::String& text, bool isThinking = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Call this to replace "thinking…" content with the real response
    void setContent (const juce::String& text);

    int getPreferredHeight (int width) const;

private:
    Role         role;
    juce::String content;
    bool         thinking;

    juce::Font   bodyFont  { juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::plain) };
    juce::Font   metaFont  { juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::plain) };
    juce::Font   boldFont  { juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::bold) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MessageBubble)
};

// ─────────────────────────────────────────────────────────────────────────────
//  ChatList  — the inner component that holds all bubbles (goes inside Viewport)
// ─────────────────────────────────────────────────────────────────────────────
class ChatList : public juce::Component
{
public:
    ChatList();

    MessageBubble* addUserMessage  (const juce::String& text);
    MessageBubble* addAIThinking   ();
    void           finalizeAI      (MessageBubble* bubble, const juce::String& text);
    void           clear           ();

    void resized() override;
    void paint (juce::Graphics& g) override;

    int getPreferredHeight() const;

private:
    std::vector<std::unique_ptr<MessageBubble>> bubbles;

    static constexpr int kPadding     = 16;
    static constexpr int kBubbleGap   = 12;

    void layout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatList)
};

// ─────────────────────────────────────────────────────────────────────────────
//  ChatComponent  — viewport wrapper + input bar
// ─────────────────────────────────────────────────────────────────────────────
class ChatComponent : public juce::Component,
                      private juce::TextEditor::Listener
{
public:
    // Called when user submits a message
    std::function<void (const juce::String&)> onSendMessage;

    ChatComponent();
    ~ChatComponent() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ── Public API ─────────────────────────────────────────────────────────
    MessageBubble* addUserMessage  (const juce::String& text);
    MessageBubble* addAIThinking   ();
    void           finalizeAI      (MessageBubble* bubble, const juce::String& text);
    void           clear           ();

    void setInputEnabled (bool enabled);
    void setInputText (const juce::String& text);
    void scrollToBottom  ();

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void submit();

    juce::Viewport     viewport;
    ChatList           chatList;

    juce::TextEditor   inputBox;
    juce::TextButton   sendButton { "SEND" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatComponent)
};
