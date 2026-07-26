#include "ChatComponent.h"

// ═════════════════════════════════════════════════════════════════════════════
//  MessageBubble
// ═════════════════════════════════════════════════════════════════════════════
MessageBubble::MessageBubble (Role r, const juce::String& text, bool isThinking)
    : role (r), content (text), thinking (isThinking)
{
    setInterceptsMouseClicks (false, false);
}

void MessageBubble::setContent (const juce::String& text)
{
    content  = text;
    thinking = false;
    repaint();
}

int MessageBubble::getPreferredHeight (int width) const
{
    const int bubbleW = juce::jmin (width - 40, 520);
    const int textW   = bubbleW - 28;

    // meta line
    int h = 26; // meta + gap

    if (thinking)
    {
        h += 42;
    }
    else
    {
        juce::AttributedString as;
        as.append (content, bodyFont);
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::topLeft);

        juce::TextLayout layout;
        layout.createLayout (as, (float)textW);
        h += (int)layout.getHeight() + 24; // padding
    }

    return h + 8; // bottom gap
}

void MessageBubble::paint (juce::Graphics& g)
{
    auto bounds  = getLocalBounds();
    const int w  = bounds.getWidth();
    const int bubbleMaxW = juce::jmin (w - 40, 520);

    bool isUser = (role == Role::User);

    // ── Meta label ──────────────────────────────────────────────────────────
    g.setFont (metaFont);
    g.setColour (isUser ? JP::accent().withAlpha (0.7f) : JP::textMuted);
    g.drawText (isUser ? "YOU" : "MIXMIND",
                bounds.removeFromTop (20).reduced (4, 0),
                isUser ? juce::Justification::right : juce::Justification::left);

    // ── Bubble rect ─────────────────────────────────────────────────────────
    juce::Rectangle<int> bubble;
    if (isUser)
        bubble = { w - bubbleMaxW, 0, bubbleMaxW, bounds.getHeight() };
    else
        bubble = { 0, 0, bubbleMaxW, bounds.getHeight() };

    // Background + border
    if (isUser)
    {
        g.setColour (JP::accent().withAlpha(0.4f));
        g.fillRect (bubble);
        g.setColour (JP::accent().withAlpha (0.2f));
        g.drawRect (bubble.toFloat(), 1.0f);
    }
    else
    {
        g.setColour (JP::surface);
        g.fillRect (bubble);
        g.setColour (JP::border);
        g.drawRect (bubble.toFloat(), 1.0f);
    }

    // ── Content ─────────────────────────────────────────────────────────────
    auto textArea = bubble.reduced (14, 10);

    if (thinking)
    {
        // Animated dots (paint-only approximation — actual animation needs a Timer)
        g.setColour (JP::textMuted);
        g.setFont (bodyFont);
        g.drawText ("Analyzing your session…", textArea, juce::Justification::centredLeft);

        // Three dots
        for (int i = 0; i < 3; ++i)
        {
            float alpha = 0.3f + 0.7f * (float)((juce::Time::getMillisecondCounter() / 400 + i) % 3 == 0);
            g.setColour (JP::accent().withAlpha (alpha));
            g.fillEllipse ((float)(textArea.getRight() - 60 + i * 14), (float)textArea.getCentreY() - 3, 7, 7);
        }
    }
    else
    {
        // Render content — split into tip blocks (lines starting with digit or →)
        auto lines = juce::StringArray::fromLines (content);
        int  yPos  = textArea.getY();
        int  xL    = textArea.getX();
        int  tw    = textArea.getWidth();

        for (auto& rawLine : lines)
        {
            auto line = rawLine.trim();
            if (line.isEmpty()) { yPos += 8; continue; }

            // Detect tip lines: "1." "2." "→" "•"
            bool isTip = (line.length() > 2 &&
                          (juce::CharacterFunctions::isDigit (line[0]) || line.startsWith ("->") || line.startsWith ("→") || line.startsWith ("•")));

            if (isTip)
            {
                // Green left border tip card
                juce::Rectangle<int> tipRect (xL, yPos, tw, 0);

                // measure height
                juce::AttributedString as;
                as.append (line, bodyFont);
                as.setWordWrap (juce::AttributedString::byWord);
                juce::TextLayout tl;
                tl.createLayout (as, (float)(tw - 12));
                int tipH = (int)tl.getHeight() + 16;

                tipRect.setHeight (tipH);

                g.setColour (JP::accent().withAlpha(0.4f));
                g.fillRect (tipRect);
                g.setColour (JP::accent().withAlpha(0.6f));
                g.fillRect (tipRect.withWidth (2));

                g.setColour (JP::text);
                g.setFont (bodyFont);
                g.drawFittedText (line, tipRect.reduced (10, 6), juce::Justification::topLeft, 20, 0.9f);

                yPos += tipH + 6;
            }
            else
            {
                // Normal paragraph text
                juce::AttributedString as;
                as.append (line, bodyFont);
                as.setWordWrap (juce::AttributedString::byWord);
                as.setColour (isUser ? JP::text : JP::text);

                juce::TextLayout tl;
                tl.createLayout (as, (float)tw);
                int lineH = (int)tl.getHeight() + 4;

                g.setColour (JP::text);
                g.setFont (bodyFont);
                g.drawFittedText (line, { xL, yPos, tw, lineH + 8 },
                                  juce::Justification::topLeft, 20, 0.9f);
                yPos += lineH + 4;
            }
        }
    }
}

void MessageBubble::resized() {}


// ═════════════════════════════════════════════════════════════════════════════
//  ChatList
// ═════════════════════════════════════════════════════════════════════════════
ChatList::ChatList()
{
    setInterceptsMouseClicks (false, false);
}

MessageBubble* ChatList::addUserMessage (const juce::String& text)
{
    auto* b = new MessageBubble (MessageBubble::Role::User, text);
    bubbles.emplace_back (b);
    addAndMakeVisible (b);
    layout();
    return b;
}

MessageBubble* ChatList::addAIThinking()
{
    auto* b = new MessageBubble (MessageBubble::Role::AI, "", true);
    bubbles.emplace_back (b);
    addAndMakeVisible (b);
    layout();
    return b;
}

void ChatList::finalizeAI (MessageBubble* bubble, const juce::String& text)
{
    bubble->setContent (text);
    layout();
}

void ChatList::clear()
{
    for (auto& b : bubbles) removeChildComponent (b.get());
    bubbles.clear();
    layout();
}

void ChatList::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
}

void ChatList::resized() { layout(); }

void ChatList::layout()
{
    const int w = getWidth();
    if (w <= 0) return;

    int y = kPadding;
    for (auto& b : bubbles)
    {
        int h = b->getPreferredHeight (w);
        b->setBounds (kPadding, y, w - kPadding * 2, h);
        y += h + kBubbleGap;
    }
    setSize (w, juce::jmax (y + kPadding, 100));
}

int ChatList::getPreferredHeight() const
{
    return getHeight();
}


// ═════════════════════════════════════════════════════════════════════════════
//  ChatComponent
// ═════════════════════════════════════════════════════════════════════════════
ChatComponent::ChatComponent()
{
    // Viewport
    viewport.setViewedComponent (&chatList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, JP::border);
    addAndMakeVisible (viewport);

    // Input
    inputBox.setMultiLine (true, true);
    inputBox.setReturnKeyStartsNewLine (false);
    inputBox.setScrollbarsShown (false);
    inputBox.setFont (juce::Font (juce::FontOptions ("SF Pro Display", 13.0f, juce::Font::plain)));
    inputBox.setTextToShowWhenEmpty ("Ask about your mix…",
                                     JP::textDim);
    inputBox.addListener (this);
    addAndMakeVisible (inputBox);

    addAndMakeVisible (sendButton);
    sendButton.onClick = [this] { submit(); };
}

void ChatComponent::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);

    // Input bar background
    auto inputArea = getLocalBounds().removeFromBottom (60);
    g.setColour (JP::surface);
    g.fillRect (inputArea);
    g.setColour (JP::border);
    g.drawLine (0, (float)inputArea.getY(), (float)getWidth(), (float)inputArea.getY(), 1.0f);
}

void ChatComponent::resized()
{
    auto bounds = getLocalBounds();

    // Input bar at bottom
    auto inputBar = bounds.removeFromBottom (60).reduced (12, 8);
    auto sendArea = inputBar.removeFromRight (52);
    inputBox.setBounds (inputBar.reduced (0, 2));
    sendButton.setBounds (sendArea.withWidth (44).withX (sendArea.getX() + 4));

    // Viewport fills rest
    viewport.setBounds (bounds);
    chatList.setSize (bounds.getWidth(), juce::jmax (chatList.getHeight(), bounds.getHeight()));
}

MessageBubble* ChatComponent::addUserMessage (const juce::String& text)
{
    auto* b = chatList.addUserMessage (text);
    scrollToBottom();
    return b;
}

MessageBubble* ChatComponent::addAIThinking()
{
    auto* b = chatList.addAIThinking();
    scrollToBottom();

    // Repaint timer for the pulsing dots
    juce::Timer::callAfterDelay (200, [this, b]
    {
        if (b != nullptr) b->repaint();
    });
    return b;
}

void ChatComponent::finalizeAI (MessageBubble* bubble, const juce::String& text)
{
    chatList.finalizeAI (bubble, text);
    scrollToBottom();
}

void ChatComponent::clear()
{
    chatList.clear();
}

void ChatComponent::setInputEnabled (bool enabled)
{
    inputBox.setEnabled (enabled);
    sendButton.setEnabled (enabled);
    sendButton.setAlpha (enabled ? 1.0f : 0.4f);
}

void ChatComponent::scrollToBottom()
{
    juce::Timer::callAfterDelay (40, [this]
    {
        viewport.setViewPosition (0, chatList.getHeight());
    });
}

void ChatComponent::textEditorReturnKeyPressed (juce::TextEditor&)
{
    submit();
}

void ChatComponent::submit()
{
    auto text = inputBox.getText().trim();
    if (text.isEmpty()) return;
    inputBox.clear();
    if (onSendMessage) onSendMessage (text);
}
