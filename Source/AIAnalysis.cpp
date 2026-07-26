#include "AIAnalysis.h"

AIAnalysis AIAnalysis::fromJson (const juce::String& rawText)
{
    AIAnalysis result;

    // Strip markdown code fences if present (e.g., ```json ... ``` )
    juce::String jsonText = rawText.trim();
    if (jsonText.startsWith ("```"))
    {
        int start  = jsonText.indexOf ("\n") + 1;
        int end    = jsonText.lastIndexOf ("```");
        if (end > start)
            jsonText = jsonText.substring (start, end).trim();
        else
            jsonText = jsonText.substring (start).trim();
    }

    auto obj = juce::JSON::parse (jsonText);

    if (obj.isVoid()) return result;

    result.valid          = true;
    result.summary        = obj.getProperty ("summary", "").toString();
    result.statusSeverity = obj.getProperty ("status_severity", "info").toString();
    result.phaseWarning   = obj.getProperty ("phase_warning", "").toString();
    result.routingAdvice  = obj.getProperty ("routing_advice", "").toString();

    // EQ suggestions
    if (auto* eqArr = obj.getProperty ("eq_suggestions", juce::var()).getArray())
        for (auto& eq : *eqArr)
            result.eqSuggestions.push_back (EQSuggestion::fromJson (eq));

    // Visual overlay targets
    if (auto* visArr = obj.getProperty ("visual_overlay_targets", juce::var()).getArray())
        for (auto& v : *visArr)
            result.overlayTargets.push_back (VisualOverlayTarget::fromJson (v));

    return result;
}

juce::String AIAnalysis::toDisplayText() const
{
    if (!valid) return {};

    juce::String text;

    // Severity badge
    juce::String badge;
    if (statusSeverity == "critical") badge = "⚡ CRITICAL";
    else if (statusSeverity == "warning") badge = "⚠ WARNING";
    else badge = "ℹ INFO";

    text << badge << "  " << summary << "\n\n";

    // EQ suggestions
    if (!eqSuggestions.empty())
    {
        text << "─── EQ SUGGESTIONS ───\n";
        for (auto& eq : eqSuggestions)
        {
            text << "  • " << juce::String (eq.freqHz, 0) << " Hz  "
                 << (eq.gainDb >= 0 ? "+" : "") << juce::String (eq.gainDb, 1) << " dB  "
                 << "Q: " << juce::String (eq.q, 1) << "  "
                 << "[" << eq.filterType << "]  "
                 << eq.channel << "\n"
                 << "    " << eq.reason << "\n\n";
        }
    }

    // Visual overlays
    if (!overlayTargets.empty())
    {
        text << "─── HIGHLIGHTED ZONES ───\n";
        for (auto& v : overlayTargets)
            text << "  • " << juce::String (v.freqStartHz, 0) << "–"
                 << juce::String (v.freqEndHz, 0) << " Hz: "
                 << v.label << "\n";
        text << "\n";
    }

    // Phase
    if (phaseWarning.isNotEmpty())
        text << "─── PHASE ───\n" << phaseWarning << "\n\n";

    // Routing
    if (routingAdvice.isNotEmpty())
        text << "─── ROUTING ───\n" << routingAdvice << "\n\n";

    return text;
}
