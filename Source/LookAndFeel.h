#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostTheme.h"

namespace JP
{
    // Dark pro-audio chassis with powder pink accents
    const juce::Colour bg          { 0xff0b0b0e };
    const juce::Colour surface     { 0xff141418 };
    const juce::Colour surfaceRaised { 0xff1a1a20 };
    const juce::Colour border      { 0x1ff2c4ce };
    const juce::Colour borderSoft  { 0x0ff2c4ce };
    const juce::Colour text        { 0xffffffff };
    const juce::Colour textMuted   { 0xffa0a0aa };
    const juce::Colour textDim     { 0xff555560 };
    inline juce::Colour accent()   { return juce::Colour (0xfff2c4ce); }
    inline juce::Colour accentBg() { return juce::Colour (0x18f2c4ce); }
    const juce::Colour error       { 0xffff4444 };
    const juce::Colour success     { 0xfff2c4ce };
    const juce::Colour warning     { 0xffffaa00 };
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
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool);
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool);
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&);
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool, bool, bool, bool, bool,
                            const juce::String&, const juce::String&, const juce::Image*, const juce::Colour*);
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&);
    juce::Font getTextButtonFont (juce::TextButton&, int);
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
