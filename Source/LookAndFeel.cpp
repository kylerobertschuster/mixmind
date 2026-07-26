#include "LookAndFeel.h"

JuicePipeLAF::JuicePipeLAF()
{
    setColour (juce::ResizableWindow::backgroundColourId, JP::bg);
    setColour (juce::TextEditor::backgroundColourId, JP::surface);
    setColour (juce::TextEditor::textColourId, JP::text);
    setColour (juce::TextEditor::outlineColourId, JP::border);
    setColour (juce::TextEditor::focusedOutlineColourId, JP::accent());
    setColour (juce::ComboBox::backgroundColourId, JP::surface);
    setColour (juce::ComboBox::textColourId, JP::text);
    setColour (juce::ComboBox::outlineColourId, JP::border);
    setColour (juce::ComboBox::arrowColourId, JP::textMuted);
    setColour (juce::PopupMenu::backgroundColourId, JP::surfaceRaised);
    setColour (juce::PopupMenu::textColourId, JP::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, JP::accentBg());
    setColour (juce::PopupMenu::highlightedTextColourId, JP::accent());
    setColour (juce::ScrollBar::thumbColourId, JP::border);
    setColour (juce::ScrollBar::trackColourId, JP::bg);
    setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    setColour (juce::TextButton::textColourOffId, JP::text);
    setColour (juce::TextButton::textColourOnId, JP::accent());
    setColour (juce::CaretComponent::caretColourId, JP::accent());
}

juce::Typeface::Ptr JuicePipeLAF::getTypefaceForFont (const juce::Font&)
{
    // Use system SF Pro on macOS, fallback to sans-serif
    return juce::Typeface::createSystemTypefaceFor (
        juce::FontOptions ("SF Pro Display", 13.0f, juce::Font::plain));
}

void JuicePipeLAF::drawButtonBackground (juce::Graphics& g, juce::Button& btn,
                                          const juce::Colour&, bool isOver, bool isDown)
{
    auto bounds = btn.getLocalBounds().toFloat().reduced (0.5f);
    auto corner = 4.0f;

    juce::Colour fill = JP::surface;
    if (isDown)       fill = JP::accentBg();
    else if (isOver)  fill = JP::surfaceRaised;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (isOver ? JP::accent().withAlpha (0.3f) : JP::border);
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void JuicePipeLAF::drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                                    bool isOver, bool)
{
    g.setColour (isOver ? JP::accent() : btn.findColour (juce::TextButton::textColourOffId));
    g.setFont (getTextButtonFont (btn, btn.getHeight()));
    g.drawText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, false);
}

void JuicePipeLAF::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                  int, int, int buttonW, int buttonH, juce::ComboBox&)
{
    auto corner = 4.0f;
    auto bounds = juce::Rectangle<float> (0, 0, (float)w, (float)h);

    g.setColour (JP::surface);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (JP::border);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    // Minimal arrow
    auto arrowX = (float)w - (float)buttonW / 2.0f;
    auto arrowY = (float)h / 2.0f;
    juce::Path p;
    p.addTriangle (arrowX - 3.5f, arrowY - 1.5f, arrowX + 3.5f, arrowY - 1.5f, arrowX, arrowY + 2.5f);
    g.setColour (JP::textMuted);
    g.fillPath (p);
}

void JuicePipeLAF::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                       bool, bool, bool isHighlighted, bool, bool,
                                       const juce::String& text, const juce::String&,
                                       const juce::Image*, const juce::Colour*)
{
    if (isHighlighted)
    {
        g.setColour (JP::accentBg());
        g.fillRect (area);
        g.setColour (JP::accent());
    }
    else
    {
        g.setColour (JP::text);
    }
    g.setFont (juce::FontOptions ("SF Pro Display", 12.0f, juce::Font::plain));
    g.drawText (text, area.reduced (10, 0), juce::Justification::left, false);
}

void JuicePipeLAF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (JP::border);
    g.drawRoundedRectangle (0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, 4.0f, 1.0f);
}

juce::Font JuicePipeLAF::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions ("SF Pro Display",
                      juce::jmin (11.0f, h * 0.45f), juce::Font::plain));
}
