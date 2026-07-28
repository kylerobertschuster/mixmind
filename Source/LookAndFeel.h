#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostTheme.h"

namespace JP
{
    // Soft saturated theme — mutable per-plugin
    inline juce::Colour bg          { 0xfff2c4ce };
    inline juce::Colour surface     { 0xffe8b8ca };
    inline juce::Colour surfaceRaised { 0xffdcacc0 };
    inline juce::Colour border      { 0x18000000 };
    inline juce::Colour text        { 0xff1a1a1a };
    inline juce::Colour textMuted   { 0x801a1a1a };
    inline juce::Colour textDim     { 0x401a1a1a };
    inline juce::Colour accent      { 0xff1a1a1a };
    inline juce::Colour accentBg    { 0x101a1a1a };
    inline juce::Colour error       { 0xffcc3333 };
    inline juce::Colour warning     { 0xffcc6600 };
    inline juce::Colour glass       { 0x08000000 };

    const juce::Colour rainbow[8] = {
        juce::Colour(0xffff6b6b), juce::Colour(0xffffa94d), juce::Colour(0xffffd43b), juce::Colour(0xff69db7c),
        juce::Colour(0xff4dabf7), juce::Colour(0xff748ffc), juce::Colour(0xffda77f2), juce::Colour(0xfff783ac)
    };
    inline juce::Colour rainbowColor(int i) { return rainbow[i % 8]; }

    inline void setTheme (juce::Colour c) {
        bg = c;
        surface = c.darker (0.06f);
        surfaceRaised = c.darker (0.10f);
        accentBg = juce::Colour (0x10000000);
    }

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
