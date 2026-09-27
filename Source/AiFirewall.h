#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "ShaperProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AiPayload — one validated AI proposal, ready to apply. Plain data of a fixed
//  size (no heap, no strings), so it can travel through a lock-free FIFO.
//  Every value in it has passed AiFirewall.
// ─────────────────────────────────────────────────────────────────────────────
struct AiPayload
{
    static constexpr int kMaxChanges     = 64;
    static constexpr int kMaxTracePoints = 64;

    struct Change
    {
        int   param;   // index into AudioProcessor::getParameters()
        float value;   // normalised 0..1, finite, a legal value for that parameter
    };

    std::array<Change, kMaxChanges> changes {};
    int numChanges { 0 };

    bool setsTrace { false };   // true + 0 points = clear the trace
    std::array<ShaperProcessor::TracePoint, kMaxTracePoints> trace {};
    int numTracePoints { 0 };

    int numClamped { 0 };       // values the firewall pulled into range
};

// ─────────────────────────────────────────────────────────────────────────────
//  AiFirewall — the only way from model output to anything the DSP uses.
//
//  Accepts a JSON object:
//      { "parameters": { "<paramID>": <value>, ... },
//        "trace":      [ [hz, dB], ... ] }        (either key may be absent)
//  Values are in the parameter's own units (Hz, dB, Q, 0..1 amount). Choices
//  take their name ("High Shelf", case / spaces / '_' / '-' ignored) or index;
//  switches take true / false / 0 / 1. The trace is (Hz, dB) on the analyzer
//  scale, exactly as the hand-drawn trace is stored.
//
//  Strict and all-or-nothing: malformed JSON, an unknown key or parameter, a
//  wrong type, any NaN / inf, an unknown choice or a bad trace rejects the whole
//  payload. Only continuous quantities are clamped (to the parameter's own
//  range; the trace to 20 Hz–20 kHz and −100..0 dB). The AI never supplies filter
//  coefficients — there is no field for them; it proposes band settings and
//  the analog-matched designer computes the coefficients.
//
//  Parameter metadata is copied at construction, so validate() never touches
//  the processor and is safe on the worker thread.
// ─────────────────────────────────────────────────────────────────────────────
class AiFirewall
{
public:
    static constexpr int   kMaxTextBytes = 64 * 1024;
    static constexpr float kTraceMinHz = 20.0f, kTraceMaxHz = 20000.0f;
    static constexpr float kTraceMinDb = -100.0f, kTraceMaxDb = 0.0f;
    static constexpr float kTraceMinSpacing = 1.01f;   // adjacent points ≥ 1 % apart, as on the canvas

    explicit AiFirewall (const juce::Array<juce::AudioProcessorParameter*>& params);

    // On failure `out` is left empty and the result says why (for the user / the model).
    juce::Result validate (const juce::String& json, AiPayload& out) const;
    juce::Result validate (const juce::var& payload, AiPayload& out) const;

private:
    enum class Kind { continuous, choice, toggle };
    struct ParamInfo
    {
        juce::String id;
        int index;
        Kind kind;
        juce::NormalisableRange<float> range;
        juce::StringArray choiceKeys;   // normalised choice names
    };

    juce::Result readParameter (const ParamInfo&, const juce::var&, AiPayload::Change&, bool& clamped) const;
    static juce::Result readTrace (const juce::var&, AiPayload&);
    const ParamInfo* find (const juce::String& id) const;
    static juce::String choiceKey (const juce::String&);

    std::vector<ParamInfo> params;
};
