#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace JP
{
    // ── Dark Mode Pastel Chassis (60%) ────────────────────────────────────
    const juce::Colour bg          { 0xff1e1e1e };
    const juce::Colour surface     { 0xff252528 };
    const juce::Colour surfaceRaised { 0xff2c2c30 };
    const juce::Colour border      { 0x18ffffff };

    // ── Text — clean white on dark (30%) ──────────────────────────────────
    const juce::Colour text        { 0xffeeeeee };
    const juce::Colour textMuted   { 0x80eeeeee };
    const juce::Colour textDim     { 0x40eeeeee };

    // ── Pastel Rainbow Accents (10% — knobs, curves, buttons) ────────────
    // Desaturated, high luminance — no eye strain on dark backgrounds
    inline juce::Colour accent { 0xffff8a80 };  // default: soft coral
    const juce::Colour accentBg    { 0x18ff8a80 };
    const juce::Colour error       { 0xffff5252 };
    const juce::Colour warning     { 0xffffcc80 };

    // Full pastel rainbow palette
    const juce::Colour pastel[7] = {
        juce::Colour(0xffff8a80),  // Soft Coral Red
        juce::Colour(0xffffcc80),  // Warm Apricot Orange
        juce::Colour(0xffffff8d),  // Pale Lemon Yellow
        juce::Colour(0xffb9f6ca),  // Mint Green
        juce::Colour(0xff80d8ff),  // Sky Blue
        juce::Colour(0xffb388ff),  // Soft Lavender
        juce::Colour(0xffea80fc),  // Light Magenta
    };
    inline juce::Colour pastelColor(int i) { return pastel[i % 7]; }

    inline void setAccent (juce::Colour c) { accent = c; }

    constexpr int headerH  = 40;
    constexpr int sidebarW = 220;
    constexpr int editorW  = 1050;
    constexpr int editorH  = 680;
}

class JuicePipeLAF : public juce::LookAndFeel_V4
{
public:
    JuicePipeLAF();
    ~JuicePipeLAF() override = default;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};
