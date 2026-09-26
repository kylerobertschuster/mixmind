#include "LookAndFeel.h"

JuicePipeLAF::JuicePipeLAF()
{
    setColour (juce::ResizableWindow::backgroundColourId, JP::bg);
    setColour (juce::TextEditor::backgroundColourId, JP::surface);
    setColour (juce::TextEditor::textColourId, JP::text);
    setColour (juce::TextEditor::outlineColourId, JP::border);
    setColour (juce::TextEditor::focusedOutlineColourId, JP::accent);
    setColour (juce::ComboBox::backgroundColourId, JP::surface);
    setColour (juce::ComboBox::textColourId, JP::text);
    setColour (juce::ComboBox::outlineColourId, JP::border);
    setColour (juce::ComboBox::arrowColourId, JP::textMuted);
    setColour (juce::PopupMenu::backgroundColourId, JP::surfaceRaised);
    setColour (juce::PopupMenu::textColourId, JP::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, JP::accentBg);
    setColour (juce::PopupMenu::highlightedTextColourId, JP::accent);
    setColour (juce::ScrollBar::thumbColourId, JP::border);
    setColour (juce::ScrollBar::trackColourId, JP::bg);
    setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    setColour (juce::TextButton::textColourOffId, JP::text);
    setColour (juce::TextButton::textColourOnId, JP::accent);

    // Default face for fonts that don't name one; explicit FontOptions (and
    // their bold/plain style) are left alone.
    setDefaultSansSerifTypefaceName ("Helvetica Neue");
}

void JuicePipeLAF::drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                                         bool highlighted, bool down)
{
    const auto r  = btn.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = btn.getToggleState();

    juce::Colour fill = JP::surface;
    if (down || on)       fill = JP::accentBg;
    else if (highlighted) fill = JP::surfaceRaised;
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);

    g.setColour (on ? JP::accent.withAlpha (0.55f) : highlighted ? JP::accent.withAlpha (0.4f) : JP::border);
    g.drawRoundedRectangle (r, 4.0f, on ? 1.0f : 0.5f);
}

void JuicePipeLAF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto col = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                      : juce::TextButton::textColourOffId);
    g.setColour (b.isEnabled() ? col : col.withMultipliedAlpha (0.4f));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
}

void JuicePipeLAF::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int bw, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (JP::surface); g.fillRoundedRectangle (b, 4.0f);
    g.setColour (JP::border);  g.drawRoundedRectangle (b, 4.0f, 0.5f);
    juce::Path p;
    p.addTriangle ((float) w - (float) bw / 2.0f - 5.0f, (float) h / 2.0f - 2.0f,
                   (float) w - (float) bw / 2.0f + 3.0f, (float) h / 2.0f - 2.0f,
                   (float) w - (float) bw / 2.0f - 1.0f, (float) h / 2.0f + 2.0f);
    g.setColour (JP::textMuted); g.fillPath (p);
}

void JuicePipeLAF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (JP::border);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
}

juce::Font JuicePipeLAF::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", juce::jmin (10.0f, (float) h * 0.42f), juce::Font::bold));
}

juce::Font JuicePipeLAF::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", juce::jmin (11.0f, (float) box.getHeight() * 0.45f), juce::Font::bold));
}
