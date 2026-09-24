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

    // Resamples the control points onto a dense log-frequency grid, interpolating
    // linearly in log-frequency between them and landing exactly on the control
    // points. Two callers read the trace this way — the plot, to draw it, and the
    // FIR designer, to map it onto the analysis bins — so the drawn curve and the
    // matched curve cannot drift apart without one of them changing here.
    //
    // Any positive step count draws an identical polyline (the function is
    // piecewise linear in log-frequency and the x axis is log-frequency), so the
    // density exists for consumers that resample in linear frequency, not for the
    // picture.
    inline constexpr int kTraceSamplesPerOctave = 24;   // ≈1.4 % of an octave

    inline TraceCurve densifyTrace (const TraceCurve& curve)
    {
        if (curve.size() < 2)
            return curve;   // nothing to interpolate; callers handle 0 and 1 points

        const float stepLf = std::log10 (2.0f) / (float) kTraceSamplesPerOctave;

        TraceCurve out;

        for (int i = 0; i < curve.size() - 1; ++i)
        {
            const auto& a = curve.getReference (i);
            const auto& b = curve.getReference (i + 1);
            const float lo = std::log10 (juce::jmax (1.0f, a.first));
            const float hi = std::log10 (juce::jmax (1.0f, b.first));

            // A zero-width segment (two points at the same frequency) keeps one
            // sample rather than dividing by zero.
            const float span = hi - lo;
            const int steps = (span > 0.0f && std::isfinite (span))
                                ? juce::jlimit (1, 128, (int) std::ceil (span / stepLf))
                                : 1;

            for (int j = 0; j < steps; ++j)
            {
                const float t = (float) j / (float) steps;

                // The first sample is the control point itself, not a round trip
                // through log10/pow, so a handle sits on the drawn line exactly.
                const float hz = (j == 0) ? a.first  : std::pow (10.0f, lo + span * t);
                const float v  = (j == 0) ? a.second : a.second + (b.second - a.second) * t;

                // One non-finite sample would poison the whole path in JUCE.
                if (std::isfinite (hz) && std::isfinite (v))
                    out.add ({ hz, v });
            }
        }

        out.add (curve.getReference (curve.size() - 1));
        return out;
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
