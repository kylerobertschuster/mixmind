#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  MatchState — the parts of a session that are not APVTS parameters.
//
//  A reference spectrum and a hand-drawn target curve are neither automatable
//  nor scalar, so AudioProcessorValueTreeState has nowhere to put them. They
//  ride along as children of the state tree instead.
//
//  The trace is stored as (frequencyHz, value01), NOT as the canvas's pixel
//  positions. Pixels would be wrong: the plot rect depends on the window size,
//  and axisMin/axisMax move with the selected focus band, so a resized window
//  or a different focus would silently re-interpret a saved curve.
//
//  Header-only on purpose — these are small, pure conversions with no state,
//  and keeping them here avoids adding a translation unit to every target.
// ─────────────────────────────────────────────────────────────────────────────
namespace MixMindState
{
    inline const juce::Identifier matchTree { "MixMindMatch" };
    inline const juce::Identifier traceTree { "TraceCurve" };
    inline const juce::Identifier pointElem { "POINT" };

    using TraceCurve = juce::Array<std::pair<float, float>>;

    inline juce::ValueTree traceToTree (const TraceCurve& curve)
    {
        juce::ValueTree tree (traceTree);

        for (const auto& pt : curve)
        {
            juce::ValueTree node (pointElem);
            node.setProperty ("hz",    (double) pt.first,  nullptr);
            node.setProperty ("value", (double) pt.second, nullptr);
            tree.appendChild (node, nullptr);
        }

        return tree;
    }

    inline TraceCurve treeToTrace (const juce::ValueTree& tree)
    {
        TraceCurve out;

        if (! tree.isValid() || ! tree.hasType (traceTree))
            return out;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto node = tree.getChild (i);

            if (! node.hasType (pointElem))
                continue;

            const double hz = node.getProperty ("hz", 0.0);
            const double v  = node.getProperty ("value", 0.0);

            // A non-finite point carries nothing recoverable, and jlimit() would
            // pass it straight through (every comparison against NaN is false),
            // so it is dropped rather than clamped. That matters: one NaN makes
            // buildMatchFilter produce a NaN impulse response, and a NaN FIR
            // silences the track.
            if (! std::isfinite (hz) || ! std::isfinite (v))
                continue;

            // Finite but out of range — a hand-edited or foreign session — is
            // clamped back into the domain the canvas can draw.
            out.add ({ juce::jlimit (1.0f, 96000.0f, (float) hz),
                       juce::jlimit (0.0f, 1.0f,     (float) v) });
        }

        // The FIR designer walks the curve as a sorted segment list, and so
        // does the preview. Sorting here keeps the two in agreement even if a
        // session was written by an older build that preserved click order.
        std::sort (out.begin(), out.end(),
                   [] (const auto& a, const auto& b) { return a.first < b.first; });

        return out;
    }
}
