#pragma once
#include <juce_core/juce_core.h>

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
// ─────────────────────────────────────────────────────────────────────────────
enum class ChannelMode { Stereo = 0, Left, Right, Mid, Side };

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
