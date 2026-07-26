#pragma once
#include <juce_core/juce_core.h>

// ─────────────────────────────────────────────────────────────────────────────
//  TelemetryData  — multi-domain analyzer output
//  FFT spectral bands + goniometer phase + metering + EQ state.
//  Serialized as JSON and sent to the AI backend for cross-domain analysis.
// ─────────────────────────────────────────────────────────────────────────────
struct TelemetryData
{
    // ── Spectral (FFT) ────────────────────────────────────────────────────
    float bassEnergy   { 0.0f };    // 20-250 Hz integrated energy (dB)
    float midEnergy    { 0.0f };    // 250-2000 Hz
    float highEnergy   { 0.0f };    // 2000+ Hz
    float subBassEnergy{ 0.0f };    // 20-60 Hz

    // ── Goniometer / Phase ────────────────────────────────────────────────
    float phaseCorrelation { 1.0f };  // -1.0 to 1.0
    float stereoWidth      { 0.5f };  // 0.0 (mono) to 1.0 (full width)

    // ── Metering / Dynamics ────────────────────────────────────────────────
    float integratedLufs { -60.0f };
    float shortTermLufs  { -60.0f };
    float truePeakDb     { -60.0f };  // dBTP
    float crestFactor    { 0.0f };    // difference between peak and RMS (dB)
    float rmsLevel       { -60.0f };

    // ── Sub-bass phase check ──────────────────────────────────────────────
    float subBassCorrelation { 1.0f }; // phase correlation in 20-60Hz band

    // ── EQ State ──────────────────────────────────────────────────────────
    juce::String eqStateJson { "{}" };

    // ── Serialize to JSON for the AI backend ───────────────────────────────
    juce::String toJson() const
    {
        juce::String json;
        json << "{"
             << "\"bass_energy\":"           << bassEnergy           << ","
             << "\"mid_energy\":"            << midEnergy            << ","
             << "\"high_energy\":"           << highEnergy           << ","
             << "\"sub_bass_energy\":"       << subBassEnergy        << ","
             << "\"phase_correlation\":"     << phaseCorrelation     << ","
             << "\"stereo_width\":"          << stereoWidth          << ","
             << "\"integrated_lufs\":"       << integratedLufs       << ","
             << "\"short_term_lufs\":"       << shortTermLufs        << ","
             << "\"true_peak_db\":"          << truePeakDb           << ","
             << "\"crest_factor\":"          << crestFactor          << ","
             << "\"rms_level\":"             << rmsLevel             << ","
             << "\"sub_bass_correlation\":"  << subBassCorrelation   << ","
             << "\"eq_state\":"              << eqStateJson
             << "}";
        return json;
    }
};
