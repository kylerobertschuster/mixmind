#include "LookAndFeel.h"

MixMindLAF::MixMindLAF()
{
    setColour (juce::ResizableWindow::backgroundColourId, MM::bg);
    setColour (juce::TextEditor::backgroundColourId, MM::surface);
    setColour (juce::TextEditor::textColourId, MM::text);
    setColour (juce::TextEditor::outlineColourId, MM::border);
    setColour (juce::TextEditor::focusedOutlineColourId, MM::accent);
    setColour (juce::ComboBox::backgroundColourId, MM::surface);
    setColour (juce::ComboBox::textColourId, MM::text);
    setColour (juce::ComboBox::outlineColourId, MM::border);
    setColour (juce::ComboBox::arrowColourId, MM::text2);
    setColour (juce::PopupMenu::backgroundColourId, MM::surfaceRaised);
    setColour (juce::PopupMenu::textColourId, MM::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, MM::accentBg);
    setColour (juce::PopupMenu::highlightedTextColourId, MM::accent);
    setColour (juce::ScrollBar::thumbColourId, MM::border);
    setColour (juce::ScrollBar::trackColourId, MM::bg);
    setColour (juce::TextButton::buttonColourId, MM::surfaceRaised);
    setColour (juce::TextButton::textColourOffId, MM::text);
    setColour (juce::TextButton::textColourOnId, MM::accent);

    setDefaultSansSerifTypefaceName ("Courier New");
}

void MixMindLAF::drawButtonBackground (juce::Graphics& g, juce::Button& btn,
                                        const juce::Colour&, bool isOver, bool isDown)
{
    auto bounds = btn.getLocalBounds().toFloat().reduced (0.5f);
    auto corner = 6.0f;

    juce::Colour fill = MM::surfaceRaised;
    if (isDown)       fill = MM::accentBg;
    else if (isOver)  fill = MM::surfaceRaised.brighter (0.05f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (isOver ? MM::accent.withAlpha (0.4f) : MM::border);
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void MixMindLAF::drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                                  bool isOver, bool)
{
    g.setColour (isOver ? MM::accent : btn.findColour (juce::TextButton::textColourOffId));
    g.setFont (getTextButtonFont (btn, btn.getHeight()));
    g.drawText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, false);
}

void MixMindLAF::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                int, int, int buttonW, int buttonH, juce::ComboBox& box)
{
    auto corner = 6.0f;
    auto bounds = juce::Rectangle<float> (0, 0, (float)w, (float)h);

    g.setColour (MM::surface);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (MM::border);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    // Arrow
    auto arrowX = (float)w - (float)buttonW / 2.0f;
    auto arrowY = (float)h / 2.0f;
    juce::Path p;
    p.addTriangle (arrowX - 4, arrowY - 2, arrowX + 4, arrowY - 2, arrowX, arrowY + 3);
    g.setColour (MM::text2);
    g.fillPath (p);
}

void MixMindLAF::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     bool, bool, bool isHighlighted, bool, bool,
                                     const juce::String& text, const juce::String&,
                                     const juce::Image*, const juce::Colour*)
{
    if (isHighlighted)
    {
        g.setColour (MM::accentBg);
        g.fillRect (area);
        g.setColour (MM::accent);
    }
    else
    {
        g.setColour (MM::text);
    }
    g.setFont (13.0f);
    g.drawText (text, area.reduced (12, 0), juce::Justification::left, false);
}

void MixMindLAF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (MM::border);
    g.drawRoundedRectangle (0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, 6.0f, 1.0f);
}

juce::Font MixMindLAF::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font ("Courier New", juce::jmin (12.0f, h * 0.5f), juce::Font::plain);
}
