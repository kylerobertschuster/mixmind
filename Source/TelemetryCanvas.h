#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "FocusModel.h"
#include "LookAndFeel.h"
#include "AudioAnalyzer.h"

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
//    · Master focus  → full range, user spectrum rendered as cow print,
//                      reference as a pastel-pink overlay.
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

    void setFocusGroup (FocusModel::Group g) { focus = g; repaint(); }
    FocusModel::Group getFocusGroup() const { return focus; }

    static constexpr int kNumBins = AudioAnalyzer::numBins;

private:
    void timerCallback() override;

    void drawGrid (juce::Graphics& g);
    void drawMaster (juce::Graphics& g);
    void drawFocused (juce::Graphics& g);

    juce::Path buildCurve (const float* bins) const;
    float xForFreq (float hz) const;
    float yForValue (float v) const;
    float curveYAt (float nx, const float* bins) const;

    void drawLegend (juce::Graphics& g);
    void drawMasterLegend (juce::Graphics& g);
    void drawHint (juce::Graphics& g, const juce::String& text);

    struct CowSpot { float nx, ny, r; int seed; };
    void generateSpots();
    juce::Path buildBlob (float cx, float cy, float r, int seed) const;

    // Smoothed display values (0..1) for user + reference.
    float smoothUser[kNumBins] { 0.0f };
    float smoothRef [kNumBins] { 0.0f };
    float targetUser[kNumBins] { 0.0f };
    float targetRef [kNumBins] { 0.0f };
    bool  hasUser { false };
    bool  hasRef  { false };

    double sampleRate { 44100.0 };
    FocusModel::Group focus { FocusModel::Group::Master };

    float axisMin { 20.0f }, axisMax { 20000.0f };
    std::vector<CowSpot> spots;

    float plotLeft { 44.0f }, plotRight { 0.0f }, plotTop { 30.0f }, plotBottom { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelemetryCanvas)
};
