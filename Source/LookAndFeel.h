#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostTheme.h"

// ─────────────────────────────────────────────────────────────────────────────
//  JuicePipe — Surgical Telemetry Palette
//  FabFilter / Oeksound aesthetic: dark graphite, amethyst glow, 1px lines.
// ─────────────────────────────────────────────────────────────────────────────
namespace JP
{
    // ── Core Surfaces ──────────────────────────────────────────────────────
    const juce::Colour bg          { 0xff121316 };  // dark slate graphite
    const juce::Colour surface     { 0xff1a1c20 };  // raised panel
    const juce::Colour surfaceRaised { 0xff22252a };
    const juce::Colour border      { 0xff2a2d33 };  // razor-thin line
    const juce::Colour borderGlow  { 0xff3a3d44 };

    // ── Text ───────────────────────────────────────────────────────────────
    const juce::Colour text        { 0xffe0e0e0 };
    const juce::Colour textMuted   { 0xff808590 };
    const juce::Colour textDim     { 0xff505560 };

    // ── Flavor Accents (each plugin flavor shifts this) ────────────────────
    const juce::Colour accentMixMind { 0xffa855f7 };  // electric amethyst
    const juce::Colour accentCrisp   { 0xff22d3ee };  // acid cyan
    const juce::Colour accentPunch   { 0xfff59e0b };  // ember gold

    // Default accent (MixMind)
    inline juce::Colour accent()    { return accentMixMind; }
    inline juce::Colour accentBg()  { return accentMixMind.withAlpha (0.12f); }

    // ── Status ──────────────────────────────────────────────────────────────
    const juce::Colour error       { 0xffef4444 };
    const juce::Colour success     { 0xff22c55e };
    const juce::Colour warning     { 0xfff59e0b };

    // ── Glass / Telemetry ──────────────────────────────────────────────────
    const juce::Colour glass       { 0x10ffffff };
    const juce::Colour glassEdge   { 0x18ffffff };
    const juce::Colour glassHighlight { 0x25ffffff };

    // ── Layout ─────────────────────────────────────────────────────────────
    constexpr int headerH  = 40;
    constexpr int pipeW    = 48;   // width of the pipe visualizer
    constexpr int sidebarW = 220;  // presets panel
    constexpr int editorW  = 1050;
    constexpr int editorH  = 680;
}

// ─────────────────────────────────────────────────────────────────────────────
//  JuicePipeLAF — surgical look and feel
// ─────────────────────────────────────────────────────────────────────────────
class JuicePipeLAF : public juce::LookAndFeel_V4
{
public:
    JuicePipeLAF();
    ~JuicePipeLAF() override = default;

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
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
