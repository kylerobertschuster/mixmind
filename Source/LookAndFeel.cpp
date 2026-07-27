#include "LookAndFeel.h"

JuicePipeLAF::JuicePipeLAF()
{
    setColour (juce::ResizableWindow::backgroundColourId, JP::bg);
    setColour (juce::TextEditor::backgroundColourId, JP::surface);
    setColour (juce::TextEditor::textColourId, JP::text);
    setColour (juce::TextEditor::outlineColourId, JP::border);
    setColour (juce::TextEditor::focusedOutlineColourId, JP::text);
    setColour (juce::ComboBox::backgroundColourId, JP::surface);
    setColour (juce::ComboBox::textColourId, JP::text);
    setColour (juce::ComboBox::outlineColourId, JP::border);
    setColour (juce::ComboBox::arrowColourId, JP::textMuted);
    setColour (juce::PopupMenu::backgroundColourId, JP::surfaceRaised);
    setColour (juce::PopupMenu::textColourId, JP::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, JP::accentBg());
    setColour (juce::PopupMenu::highlightedTextColourId, JP::text);
    setColour (juce::ScrollBar::thumbColourId, JP::border);
    setColour (juce::ScrollBar::trackColourId, JP::bg);
    setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    setColour (juce::TextButton::textColourOffId, JP::text);
    setColour (juce::TextButton::textColourOnId, JP::text);
}

juce::Typeface::Ptr JuicePipeLAF::getTypefaceForFont (const juce::Font&)
{
    return juce::Typeface::createSystemTypefaceFor (
        juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::plain));
}

void JuicePipeLAF::drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&, bool o, bool d)
{
    auto r = btn.getLocalBounds().toFloat().reduced (0.5f);
    juce::Colour f = JP::surfaceRaised;
    if (d) f = JP::accentBg(); else if (o) f = JP::surfaceRaised.brighter (0.03f);
    g.setColour (f); g.fillRoundedRectangle (r, 4.0f);
    // Pill border — pink on hover
    if (o)
    {
        g.setColour (JP::accent().withAlpha (0.4f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    else
    {
        g.setColour (JP::border);
        g.drawRoundedRectangle (r, 4.0f, 0.5f);
    }
}

void JuicePipeLAF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool o, bool)
{
    g.setColour (JP::text);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setFont (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void JuicePipeLAF::drawComboBox (juce::Graphics& g, int w, int h, bool, int,int,int bw,int bh, juce::ComboBox&)
{
    auto b = juce::Rectangle<float> (0,0,(float)w,(float)h);
    g.setColour (JP::surface); g.fillRoundedRectangle (b, 3.0f);
    g.setColour (JP::border); g.drawRoundedRectangle (b, 3.0f, 0.5f);
    juce::Path p; p.addTriangle (w-bw/2.0f-5, h/2.0f-2, w-bw/2.0f+3, h/2.0f-2, w-bw/2.0f-1, h/2.0f+2);
    g.setColour (JP::textMuted); g.fillPath (p);
}

void JuicePipeLAF::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& a, bool, bool, bool hl, bool, bool,
                                       const juce::String& t, const juce::String&, const juce::Image*, const juce::Colour*)
{
    if (hl) { g.setColour (JP::accentBg()); g.fillRect (a); }
    g.setColour (JP::text);
    g.setFont (juce::FontOptions ("Helvetica Neue", 11.0f, juce::Font::bold));
    g.drawText (t, a.reduced (10,0), juce::Justification::left, false);
}

void JuicePipeLAF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (JP::border); g.drawRoundedRectangle (0.5f,0.5f,(float)w-1,(float)h-1, 3.0f, 1.0f);
}

juce::Font JuicePipeLAF::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", juce::jmin (10.0f, h*0.42f), juce::Font::bold));
}
