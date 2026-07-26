#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ─────────────────────────────────────────────────────────────────────────────
//  HostTheme  — detects DAW and returns matching fonts, colors, and sizing
//  Makes JuicePipe feel native in Ableton, Logic, Reaper, etc.
// ─────────────────────────────────────────────────────────────────────────────
namespace HostTheme
{
    enum class Host
    {
        AbletonLive,
        LogicPro,
        ProTools,
        Reaper,
        FLStudio,
        StudioOne,
        Bitwig,
        Cubase,
        GarageBand,
        Unknown
    };

    // Detect the current DAW host
    Host detect();

    // ── Typography ──────────────────────────────────────────────────────────
    struct FontSet
    {
        juce::String ui;       // labels, buttons, dropdowns
        juce::String mono;     // numerical readouts, meters
        juce::String heading;  // title bar
    };

    FontSet getFonts();

    // ── Color Palette ───────────────────────────────────────────────────────
    struct ColorSet
    {
        juce::Colour bg;
        juce::Colour surface;
        juce::Colour border;
        juce::Colour text;
        juce::Colour muted;
        juce::Colour accent;
    };

    ColorSet getColors();
    juce::Colour accentForHost (Host host);
}
