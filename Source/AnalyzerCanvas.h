#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "LookAndFeel.h"
#include "HostTheme.h"
#include "AIAnalysis.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AnalyzerMode  — the 4 selectable telemetry views
// ─────────────────────────────────────────────────────────────────────────────
enum class AnalyzerMode
{
    Spectrum = 0,   // FFT frequency curve + AI overlays + crosshair
    Stereo,         // Goniometer Lissajous scope + phase correlation
    Dynamics,       // LUFS meters + true peak + crest factor
    AICoPilot       // Spectrum + AI-diagnosed overlay zones
};

// ─────────────────────────────────────────────────────────────────────────────
//  AnalyzerCanvas  — DMG-style multi-mode telemetry display
//  Centerpiece of the JuicePipe UI. Tabbed modes replace the pipe visualizer.
// ─────────────────────────────────────────────────────────────────────────────
class AnalyzerCanvas : public juce::Component,
                       private juce::Timer
{
public:
    AnalyzerCanvas();
    ~AnalyzerCanvas() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Feed live audio data
    void updateTelemetry (const juce::String& json);

    // Set AI analysis results for overlay display
    void setAIAnalysis (const AIAnalysis& analysis);

    // Mode switching
    void setMode (AnalyzerMode mode);
    AnalyzerMode getMode() const { return currentMode; }

    // Mouse interaction for crosshair
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // ── Drawing sub-functions ──────────────────────────────────────────────
    void drawSpectrumMode (juce::Graphics& g);
    void drawStereoMode (juce::Graphics& g);
    void drawDynamicsMode (juce::Graphics& g);
    void drawAICoPilotMode (juce::Graphics& g);
    void drawModeSelector (juce::Graphics& g);
    void drawGrid (juce::Graphics& g, int numHoriz, int numVert);
    void drawCrosshair (juce::Graphics& g);

    // ── Data ────────────────────────────────────────────────────────────────
    AnalyzerMode currentMode { AnalyzerMode::Spectrum };

    // FFT bins (smoothed)
    static constexpr int kNumBins = 256;
    float fftBins[kNumBins]   { 0 };
    float fftSmooth[kNumBins] { 0 };

    // Telemetry values
    float lufs       { -60 }; float stereo    { 0.5f };
    float crest      { 0 };   float peak      { -60 };
    float phaseCorr  { 1 };   float subCorr   { 1 };
    float bassE      { 0 };   float midE      { 0 };
    float highE      { 0 };   float subE      { 0 };

    // Goniometer points (circular buffer)
    static constexpr int kGonioSize = 1024;
    float gonioBufferL[kGonioSize] { 0 };
    float gonioBufferR[kGonioSize] { 0 };
    int   gonioWrite { 0 };
    int   gonioCount { 0 };

    // Mouse crosshair
    juce::Point<float> mousePos    { -1, -1 };
    bool               mouseInView { false };

    // AI overlay
    AIAnalysis currentAnalysis;

    // Colors
    juce::Colour accent { 0xffa855f7 };

    // Layout
    float plotLeft { 0 }, plotRight { 0 }, plotTop { 0 }, plotBottom { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerCanvas)
};
