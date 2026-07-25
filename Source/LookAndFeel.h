#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ─────────────────────────────────────────────────────────────────────────────
//  JuicePipe MixMind — Pipe-Device UI Theme
//  Dark industrial palette with lime-green accents from the JuicePipe logo.
// ─────────────────────────────────────────────────────────────────────────────
namespace MM
{
    // Core palette
    const juce::Colour bg        { 0xff0c0c0c };  // near-black
    const juce::Colour surface   { 0xff151515 };  // dark panel
    const juce::Colour surfaceRaised { 0xff1c1c1c };
    const juce::Colour border    { 0xff252525 };  // subtle line
    const juce::Colour text      { 0xffd4d4d4 };
    const juce::Colour text2     { 0xff888888 };  // muted
    const juce::Colour text3     { 0xff555555 };  // dim

    // Accent — JuicePipe lime green
    const juce::Colour accent    { 0xff76ff03 };  // #76FF03
    const juce::Colour accentDim { 0xff558b2f };
    const juce::Colour accentBg  { 0x2076ff03 };  // 12% opacity

    // Status
    const juce::Colour error     { 0xffff4444 };
    const juce::Colour success   { 0xff76ff03 };
    const juce::Colour warning   { 0xffffaa00 };

    // Glass / pipe
    const juce::Colour glass     { 0x10ffffff };  // glass highlight
    const juce::Colour glassBorder { 0x20ffffff };

    // Layout
    constexpr int headerH  = 42;
    constexpr int sidebarW = 240;
    constexpr int editorW  = 1024;
    constexpr int editorH  = 680;
    constexpr int inputH   = 52;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MixMindLAF — custom LookAndFeel with pipe-device aesthetic
// ─────────────────────────────────────────────────────────────────────────────
class MixMindLAF : public juce::LookAndFeel_V4
{
public:
    MixMindLAF();
    ~MixMindLAF() override = default;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& bg, bool isOver, bool isDown);
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isOver, bool isDown);
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&);
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcut,
                            const juce::Image* icon, const juce::Colour* textColour);
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&);
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight);
};
