#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  AIAnalysis  — structured response from the AI Co-Pilot engine
//  Parsed from the JSON payload returned by the backend per the schema.
// ─────────────────────────────────────────────────────────────────────────────

struct EQSuggestion
{
    float       freqHz       { 0 };
    float       gainDb       { 0 };
    float       q            { 1.0f };
    juce::String filterType  { "Bell" };
    juce::String channel     { "Mid" };
    juce::String reason;

    static EQSuggestion fromJson (const juce::var& obj)
    {
        EQSuggestion s;
        s.freqHz     = (float) obj.getProperty ("freq_hz", 0);
        s.gainDb     = (float) obj.getProperty ("recommended_gain_db", 0);
        s.q          = (float) obj.getProperty ("recommended_q", 1.0);
        s.filterType = obj.getProperty ("filter_type", "Bell").toString();
        s.channel    = obj.getProperty ("channel", "Mid").toString();
        s.reason     = obj.getProperty ("reason", "").toString();
        return s;
    }
};

struct VisualOverlayTarget
{
    float       freqStartHz { 0 };
    float       freqEndHz   { 0 };
    juce::String label;
    juce::String colorHex    { "#a855f7" };

    static VisualOverlayTarget fromJson (const juce::var& obj)
    {
        VisualOverlayTarget t;
        t.freqStartHz = (float) obj.getProperty ("freq_start_hz", 0);
        t.freqEndHz   = (float) obj.getProperty ("freq_end_hz", 0);
        t.label       = obj.getProperty ("label", "").toString();
        t.colorHex    = obj.getProperty ("color_hex", "#a855f7").toString();
        return t;
    }
};

struct AIAnalysis
{
    bool                           valid         { false };
    juce::String                   summary;
    juce::String                   statusSeverity { "info" };
    std::vector<EQSuggestion>       eqSuggestions;
    std::vector<VisualOverlayTarget> overlayTargets;
    juce::String                   phaseWarning;
    juce::String                   routingAdvice;

    static AIAnalysis fromJson (const juce::String& jsonText);
    juce::String toDisplayText() const;
};
