#pragma once
#include <vector>
#include "MeasurementHistory.h"
#include "ReferenceAnalyzer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Mix Doctor — the diagnostic engine (ADR-001, ADR-002, ADR-007).
//
//  Turns measurements (MeasurementHistory::Stats of the mix, and the loaded
//  reference) into findings. It never measures, and it is deterministic: the
//  same statistics give the same report. Every rule has an explicit threshold
//  (below, and tested) and compares with the user's reference or a published
//  standard — never with an unmeasured "industry norm". v1.0 works from the
//  mix bus, so a finding may list potential causes but never names the
//  tracks involved (ADR-008).
// ─────────────────────────────────────────────────────────────────────────────
namespace MixDoctor
{
    enum class Severity   { healthy, low, medium, high };
    enum class Confidence { low, medium, high };
    juce::String toString (Severity);
    juce::String toString (Confidence);

    // A number a finding rests on (the AI explanation may only quote these, #11).
    struct Evidence
    {
        juce::String metric;   // e.g. "band_level_vs_reference"
        juce::String range;    // e.g. "22–177 Hz" ("" = whole mix)
        float value { 0.0f };
        juce::String unit;     // "dB", "dBTP", "LU", "LUFS"
    };

    struct Finding
    {
        juce::String id;                   // stable, e.g. "tone_bass"
        juce::String title;                // one line, producer language
        juce::String observation;          // what was measured
        juce::String impact;               // why it matters to the listener
        juce::StringArray potentialCauses; // inferred findings only (ADR-007)
        juce::String action;               // what to try first
        Severity   severity   { Severity::healthy };
        Confidence confidence { Confidence::low };
        std::vector<Evidence> evidence;
    };

    // What the mix is compared with, on the live analyzer grid.
    struct ReferenceProfile
    {
        juce::String name;
        std::array<bool,  MeasurementFrame::kBands> bandHasData {};
        std::array<float, MeasurementFrame::kBands> bandDb {};
        float lufs { LoudnessMeter::kSilenceDb };
        float truePeakDb { LoudnessMeter::kSilenceDb };
        float crestDb { 0.0f };
    };
    // `liveGridBins`: the reference's mid spectrum mapped onto the live grid
    // (ReferenceAnalyzer::mapToGrid, numBins bins at liveSampleRate).
    ReferenceProfile profileFromReference (const ReferenceAnalyzer::Result&, const float* liveGridBins,
                                           double liveSampleRate);

    struct Report
    {
        bool   ready { false };              // enough signal to diagnose
        juce::String notReadyReason;
        double observedSeconds { 0.0 };      // seconds with signal
        juce::String referenceName;          // empty = no reference
        std::vector<Finding> findings;       // most severe first; healthy last
        juce::String toMarkdown() const;
    };

    // Observation needed: nothing below kMinSeconds of signal; confidence
    // grows with what was heard (ADR-007).
    constexpr double kMinSeconds = 3.0, kMediumSeconds = 8.0, kHighSeconds = 20.0;

    Report diagnose (const MeasurementHistory::Stats& mix, const ReferenceProfile* reference);
}
