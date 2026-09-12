#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FocusModel.h"
#include "LookAndFeel.h"
#include "AudioAnalyzer.h"
#include "MatchState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ColorSwatch — a clickable swatch that opens a ColourSelector popup.
//  Used to remap a focus group's colour (reference pastel is auto-derived).
// ─────────────────────────────────────────────────────────────────────────────
class ColorSwatch : public juce::Component,
                    private juce::ChangeListener
{
public:
    std::function<void(juce::Colour)> onColourPicked;

    ColorSwatch() = default;
    ~ColorSwatch() override = default;

    void setSwatchColour (juce::Colour c) { swatch = c; repaint(); }
    juce::Colour getSwatchColour() const { return swatch; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Colour swatch { juce::Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorSwatch)
};

// ─────────────────────────────────────────────────────────────────────────────
//  TelemetryCanvas — dual-signal spectrum.
//    · Master focus  → full range, the user spectrum is coloured per band by
//                      each focus group's hue (a "rainbow" where a bass peak
//                      is blue, a vocal peak is violet, etc.).
//    · Group focus   → zooms to the group's band, reference = pastel hue,
//                      user = saturated hue (same colour family).
// ─────────────────────────────────────────────────────────────────────────────
class TelemetryCanvas : public juce::Component,
                        private juce::Timer
{
public:
    TelemetryCanvas();
    ~TelemetryCanvas() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setUserBins (const float* bins, int n, double sampleRate);
    void setReference (const float* bins, int n);   // nullptr clears
    bool hasReference() const { return hasRef; }

    void setUserScalars (float lufs, float width, float phase, float crest);
    void setRefScalars  (float lufs, float width, float phase, float crest);

    void setFocusGroup (FocusModel::Group g) { focus = g; repaint(); }
    FocusModel::Group getFocusGroup() const { return focus; }

    // ── Reference layer — an image ghost (e.g. a screenshot of a target
    //    spectrum curve) composited behind the live FFT. ──────────────────
    void setRefImage (const juce::Image& img);
    void setRefImageOpacity (float v) { imageOpacity = juce::jlimit (0.05f, 1.0f, v); repaint(); }
    void setRefImageVisible (bool v)  { imageVisible = v; repaint(); }
    bool hasRefImage() const          { return imageLoaded; }
    bool getRefImageVisible() const   { return imageVisible; }
    void clearRefImage();

    // ── Trace — a hand-drawn target curve the user clicks onto the plot. ─
    void setTraceMode (bool on);
    bool getTraceMode() const { return traceMode; }
    bool hasTrace() const     { return traceCurve.size() >= 2; }
    void clearTrace()         { traceCurve.clear(); repaint(); }
    int  getTracePointCount() const { return traceCurve.size(); }

    // The trace in normalised (frequencyHz, value01) space, sorted. This — not
    // the pixels — is the stored form: the pixels depend on the window size and
    // on axisMin/axisMax, which move with the selected focus band.
    MixMindState::TraceCurve getTraceCurve() const { return traceCurve; }

    // Restores a curve recalled from a session.
    void setTraceCurve (const MixMindState::TraceCurve& curve);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int kNumBins = AudioAnalyzer::numBins;

private:
    void timerCallback() override;

    void drawGrid (juce::Graphics& g);
    void drawMaster (juce::Graphics& g);
    void drawFocused (juce::Graphics& g);

    juce::Path buildCurve (const float* bins) const;
    juce::Path buildBandCurve (const float* bins, float loHz, float hiHz) const;
    float xForFreq (float hz) const;
    float yForValue (float v) const;

    void drawLegend (juce::Graphics& g);
    void drawMasterLegend (juce::Graphics& g);
    void drawReadout (juce::Graphics& g);
    void drawGroupMeter (juce::Graphics& g);
    void drawHint (juce::Graphics& g, const juce::String& text);
    void drawRefImage (juce::Graphics& g);
    void drawTrace (juce::Graphics& g);
    float groupEnergy (FocusModel::Group g, const float* bins) const;

    juce::Path smoothTrace() const;
    void fitImageToPlot();
    juce::Rectangle<float> plotRect() const { return { plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop }; }

    // Smoothed display values (0..1) for user + reference.
    float smoothUser[kNumBins] { 0.0f };
    float smoothRef [kNumBins] { 0.0f };
    float targetUser[kNumBins] { 0.0f };
    float targetRef [kNumBins] { 0.0f };
    bool  hasUser { false };
    bool  hasRef  { false };

    // Scalar telemetry (LUFS / stereo width / phase / crest) for the readout.
    float userLufs  { -60.0f }, userWidth { 0.5f }, userPhase { 1.0f }, userCrest { 0.0f };
    float refLufs   { -60.0f }, refWidth  { 0.5f }, refPhase  { 1.0f }, refCrest  { 0.0f };

    // Peak-hold envelope (slow release) drawn as a faint line above the curve.
    float peakHold[kNumBins] { 0.0f };

    // Reference image layer (screenshot ghost).
    juce::Image refImage;
    bool imageLoaded { false };
    bool imageVisible { true };
    float imageOpacity { 0.5f };
    juce::Rectangle<float> imageBounds;
    bool mouseHover { false };
    juce::Point<float> lastMouse;

    // Hand-drawn target curve, held in normalised (frequencyHz, value01) space
    // and kept sorted. Pixels are derived at draw time, so a window resize or a
    // focus-band change cannot silently change what the curve means.
    bool traceMode { false };
    MixMindState::TraceCurve traceCurve;

    double sampleRate { 44100.0 };
    FocusModel::Group focus { FocusModel::Group::Master };

    float axisMin { 20.0f }, axisMax { 20000.0f };

    float plotLeft { 44.0f }, plotRight { 0.0f }, plotTop { 48.0f }, plotBottom { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelemetryCanvas)
};
