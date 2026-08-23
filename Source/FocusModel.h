#pragma once
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  FocusModel — the color-coded "focus" system.
//
//  A FocusGroup is a sound / frequency region. Each group has a two-color
//  identity: a PASTEL colour for the reference track and a SATURATED colour
//  for the user's live audio. The user can override any group's colour; the
//  pastel reference colour is always derived automatically from the saturated
//  one (lightened toward white).
//
//  The Master group is special: it renders the full-range spectrum as
//  "cow print" (white fill + black blotches) instead of a hue pair.
// ─────────────────────────────────────────────────────────────────────────────
namespace FocusModel
{
    enum class Group
    {
        Master = 0,   // full range — cow print
        Bass,         // 20–250 Hz      — blue
        Guitar,       // 250 Hz–2 kHz   — green
        Vocals,       // 2–8 kHz        — violet
        Drums,        // 8–20 kHz       — yellow
        Synth,        // mid-range      — silver (gated 5th)
        Count
    };

    constexpr int groupCount = (int) Group::Count;

    struct FocusColor
    {
        juce::Colour pastel;
        juce::Colour saturated;
    };

    inline FocusColor makePair (juce::Colour saturated)
    {
        // Pastel = reference identity: the same hue, lightened toward white.
        return { saturated.interpolatedWith (juce::Colours::white, 0.55f), saturated };
    }

    inline juce::String groupName (Group g)
    {
        switch (g)
        {
            case Group::Master: return "Master";
            case Group::Bass:   return "Bass";
            case Group::Guitar: return "Guitar";
            case Group::Vocals: return "Vocals";
            case Group::Drums:  return "Drums";
            case Group::Synth:  return "Synth";
            default:            return "Master";
        }
    }

    // Focus band in Hz — when a group is focused, the spectrum zooms to this
    // range (with a little padding). Master spans the full audible range.
    inline juce::Range<float> bandRange (Group g)
    {
        switch (g)
        {
            case Group::Bass:   return { 20.0f,   250.0f };
            case Group::Guitar: return { 250.0f,  2000.0f };
            case Group::Vocals: return { 2000.0f, 8000.0f };
            case Group::Drums:  return { 8000.0f, 20000.0f };
            case Group::Synth:  return { 250.0f,  8000.0f };
            default:            return { 20.0f,   20000.0f };
        }
    }

    // Dropdown order: Master first, then the 4 default groups, then Synth.
    inline juce::Array<Group> selectableGroups()
    {
        return { Group::Master, Group::Bass, Group::Guitar, Group::Vocals, Group::Drums, Group::Synth };
    }

    // The default 4-segment split (fast analysis). Synth is the gated 5th.
    inline juce::Array<Group> defaultGroups()
    {
        return { Group::Bass, Group::Guitar, Group::Vocals, Group::Drums };
    }

    // ── Default saturated colours (pastel derived automatically) ──────────
    inline std::array<FocusColor, groupCount> defaultPalette()
    {
        std::array<FocusColor, groupCount> p;
        p[(int) Group::Master] = makePair (juce::Colour (0xffff8a80));  // coral (unused — master is cow print)
        p[(int) Group::Bass]   = makePair (juce::Colour (0xff2979ff));  // blue
        p[(int) Group::Guitar] = makePair (juce::Colour (0xff00c853));  // green
        p[(int) Group::Vocals] = makePair (juce::Colour (0xff8b5cf6));  // violet
        p[(int) Group::Drums]  = makePair (juce::Colour (0xffffc400));  // yellow
        p[(int) Group::Synth]  = makePair (juce::Colour (0xff8a8f98));  // silver → pastel silver / gunmetal
        return p;
    }

    // Mutable, user-overridable palette (GUI-thread only).
    inline std::array<FocusColor, groupCount>& palette()
    {
        static std::array<FocusColor, groupCount> p = defaultPalette();
        return p;
    }

    inline FocusColor colorFor (Group g)   { return palette()[(int) g]; }
    inline void      setColor  (Group g, juce::Colour saturated) { palette()[(int) g] = makePair (saturated); }
    inline void      resetColor (Group g) { palette()[(int) g] = defaultPalette()[(int) g]; }

    // Reference overlay colour used in Master (cow print) mode.
    inline juce::Colour masterReferenceColour() { return juce::Colour (0xffff9db0); }
}
