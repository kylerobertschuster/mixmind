#pragma once
#include <juce_core/juce_core.h>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  ChannelMode — which part of a stereo signal the shaper analyzes and
//  processes (FabFilter Pro-Q-style channel selector).
//
//    · Stereo → analyze the mono downmix, apply the same EQ to L and R
//    · Left   → analyze + EQ the left channel only (R passes through)
//    · Right  → analyze + EQ the right channel only (L passes through)
//    · Mid    → analyze + EQ the mid component, leaving the side untouched
//    · Side   → analyze + EQ the side component, leaving the mid untouched
//
//  The analysis side uses the same signal mix so the displayed spectrum and
//  the match target always describe the channel the EQ is applied to.
//
//  The ordering of the enum is a persisted file format: the selected index is
//  saved in project state as an APVTS choice parameter. Append new modes, never
//  reorder or renumber the existing ones.
// ─────────────────────────────────────────────────────────────────────────────
enum class ChannelMode { Stereo = 0, Left, Right, Mid, Side };

// Every mode, in enum order. Sized from Side so adding an enumerator without
// listing it here fails to compile.
inline const std::array<ChannelMode, (size_t) ChannelMode::Side + 1>& allChannelModes()
{
    static const std::array<ChannelMode, (size_t) ChannelMode::Side + 1> modes
    {
        ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid, ChannelMode::Side
    };
    return modes;
}

inline juce::String channelModeName (ChannelMode m)
{
    switch (m)
    {
        case ChannelMode::Left:   return "LEFT";
        case ChannelMode::Right:  return "RIGHT";
        case ChannelMode::Mid:    return "MID";
        case ChannelMode::Side:   return "SIDE";
        case ChannelMode::Stereo:
        default:                  return "STEREO";
    }
}

// Choice strings for the APVTS parameter and the editor's dropdown — one source
// of truth, so the saved index can never mean different things in each.
inline juce::StringArray channelModeChoices()
{
    juce::StringArray a;
    for (auto m : allChannelModes())
        a.add (channelModeName (m));
    return a;
}

// Mono analysis signal for a mode, given a stereo sample pair.
inline float channelAnalysisSample (ChannelMode m, float l, float r)
{
    switch (m)
    {
        case ChannelMode::Left:   return l;
        case ChannelMode::Right:  return r;
        case ChannelMode::Mid:    return (l + r) * 0.5f;
        case ChannelMode::Side:   return (l - r) * 0.5f;
        case ChannelMode::Stereo:
        default:                  return (l + r) * 0.5f;   // mono downmix
    }
}
