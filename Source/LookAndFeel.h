#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostTheme.h"

namespace JP
{
    const juce::Colour bg          { 0xff080809 };
    const juce::Colour surface     { 0xff0e0e10 };
    const juce::Colour surfaceRaised { 0xff141416 };
    const juce::Colour border      { 0x1af0c4d4 };
    const juce::Colour borderSoft  { 0x0ff0c4d4 };
    const juce::Colour text        { 0xfff0c4d4 };
    const juce::Colour textMuted   { 0x73f0c4d4 };
    const juce::Colour textDim     { 0x2ef0c4d4 };
    inline juce::Colour accent()   { return juce::Colour (0xfff0c4d4); }
    inline juce::Colour accentBg() { return juce::Colour (0x12f0c4d4); }
    const juce::Colour error       { 0xffff8fa3 };
    const juce::Colour success     { 0xfff0c4d4 };
    const juce::Colour warning     { 0xffffc080 };
    const juce::Colour glass       { 0x08f0c4d4 };
    const juce::Colour glassHighlight { 0x12f0c4d4 };
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
