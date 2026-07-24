#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

// ─────────────────────────────────────────────────────────────────────────────
//  MixMind colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace MM
{
    const juce::Colour bg        { 0xff0f1115 }; // Deep charcoal/blue
    const juce::Colour surface   { 0xff161920 }; // Surface elevation
    const juce::Colour surface2  { 0xff1c212a }; // Lighter surface
    const juce::Colour border    { 0xff282e3a }; // Subtle borders
    const juce::Colour border2   { 0xff384252 }; // Focused borders
    const juce::Colour accent    { 0xff00ffd0 }; // Vibrant 'Cyan' accent
    const juce::Colour accentDim { 0x1500ffd0 }; // 8% alpha accent
    const juce::Colour warn      { 0xffffab00 }; // Warm amber
    const juce::Colour text      { 0xffeceef2 }; // Clean off-white
    const juce::Colour text2     { 0xff8a94a6 }; // Graphite secondary
    const juce::Colour text3     { 0xff4a5568 }; // Dimmed text
    const juce::Colour shadow    { 0x40000000 }; // Subtle shadows
}

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindLookAndFeel
// ─────────────────────────────────────────────────────────────────────────────
class MixMindLAF : public juce::LookAndFeel_V4
{
public:
    MixMindLAF()
    {
        setColour (juce::ResizableWindow::backgroundColourId,    MM::bg);
        setColour (juce::DocumentWindow::backgroundColourId,     MM::bg);

        // TextEditor
        setColour (juce::TextEditor::backgroundColourId,         MM::surface2);
        setColour (juce::TextEditor::outlineColourId,            MM::border);
        setColour (juce::TextEditor::focusedOutlineColourId,     MM::accent);
        setColour (juce::TextEditor::textColourId,               MM::text);
        setColour (juce::TextEditor::highlightColourId,          MM::accentDim);
        setColour (juce::CaretComponent::caretColourId,          MM::accent);

        // Label
        setColour (juce::Label::textColourId,                    MM::text);

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,           MM::surface2);
        setColour (juce::ComboBox::outlineColourId,              MM::border);
        setColour (juce::ComboBox::textColourId,                 MM::text);
        setColour (juce::ComboBox::arrowColourId,                MM::text2);

        // PopupMenu
        setColour (juce::PopupMenu::backgroundColourId,          MM::surface);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, MM::accentDim);
        setColour (juce::PopupMenu::highlightedTextColourId,     MM::accent);

        // TextButton
        setColour (juce::TextButton::buttonColourId,             MM::surface2);
        setColour (juce::TextButton::textColourOffId,            MM::text2);
        setColour (juce::TextButton::textColourOnId,             MM::accent);
    }

    // ── Buttons ────────────────────────────────────────────────────────────
    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& btn,
                               const juce::Colour&,
                               bool isHighlighted,
                               bool isDown) override
    {
        auto bounds = btn.getLocalBounds().toFloat().reduced (0.5f);
        auto cornerSize = 4.0f;

        // Subtle shadow
        g.setColour (MM::shadow);
        g.fillRoundedRectangle (bounds.translated (0, 1.0f), cornerSize);

        auto baseCol = btn.getToggleState() ? MM::accentDim : MM::surface2;
        if (isDown)       baseCol = MM::accentDim.withAlpha (0.4f);
        else if (isHighlighted) baseCol = MM::surface2.brighter (0.1f);

        g.setColour (baseCol);
        g.fillRoundedRectangle (bounds, cornerSize);

        auto borderCol = (isHighlighted || btn.getToggleState()) ? MM::accent.withAlpha (0.6f) : MM::border;
        g.setColour (borderCol);
        g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                         bool isHighlighted, bool /*isDown*/) override
    {
        auto col = (isHighlighted || btn.getToggleState()) ? MM::accent : MM::text;
        g.setColour (col);
        g.setFont (juce::FontOptions ("Inter", 13.0f, juce::Font::plain)); // Assume Inter or clean Sans
        g.drawFittedText (btn.getButtonText(),
                          btn.getLocalBounds().reduced (8, 4),
                          juce::Justification::centredLeft, 1);
    }

    // ── ComboBox ───────────────────────────────────────────────────────────
    void drawComboBox (juce::Graphics& g, int w, int h, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float)w, (float)h).reduced (0.5f);
        auto cornerSize = 4.0f;

        g.setColour (MM::surface2);
        g.fillRoundedRectangle (bounds, cornerSize);
        
        g.setColour (box.hasKeyboardFocus (false) ? MM::accent : MM::border);
        g.drawRoundedRectangle (bounds, cornerSize, 1.0f);

        // Flat stylized arrow
        auto arrowBounds = bounds.removeFromRight (24.0f).reduced (8.0f);
        juce::Path arrow;
        arrow.addTriangle (arrowBounds.getX(), arrowBounds.getY() + 2,
                           arrowBounds.getRight(), arrowBounds.getY() + 2,
                           arrowBounds.getCentreX(), arrowBounds.getBottom() - 2);
        g.setColour (MM::text2);
        g.fillPath (arrow);
    }

    // ── ScrollBar ──────────────────────────────────────────────────────────
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& /*scrollbar*/,
                        int x, int y, int w, int h,
                        bool isVertical, int thumbPos, int thumbSize,
                        bool isHovered, bool /*isDown*/) override
    {
        g.setColour (MM::bg);
        g.fillRect (x, y, w, h);

        auto thumbBounds = isVertical ? juce::Rectangle<float> (x + 3.0f, (float)thumbPos, w - 6.0f, (float)thumbSize)
                                      : juce::Rectangle<float> ((float)thumbPos, y + 3.0f, (float)thumbSize, h - 6.0f);

        g.setColour (isHovered ? MM::accent.withAlpha (0.5f) : MM::text3.withAlpha (0.3f));
        g.fillRoundedRectangle (thumbBounds, thumbBounds.getWidth() * 0.5f);
    }
};
