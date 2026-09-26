#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "FocusModel.h"
#include "LookAndFeel.h"
#include "AudioAnalyzer.h"
#include "ParametricEq.h"
#include "ShaperProcessor.h"

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
//  Plus the parametric EQ nodes and curve, the hand-drawn trace, the
//  match-EQ correction curve(s), a screenshot layer and the REF / YOU
//  metering readout.
//
//  Vertical scale: analyzer dB (0 dB = full-scale sine) from kFloorDb to
//  kCeilDb, linear in dB. dbToUnit()/unitToDb() are the only mapping between
//  plot height and level; the trace is stored in dB, never in pixels.
// ─────────────────────────────────────────────────────────────────────────────
class TelemetryCanvas : public juce::Component,
                        private juce::Timer
{
public:
    using TracePoint = ShaperProcessor::TracePoint;
    using Band = ParametricEq::Band;
    static constexpr int kNumBands = ParametricEq::kNumBands;

    struct Readout
    {
        float lufs     { LoudnessMeter::kSilenceDb };
        float truePeak { LoudnessMeter::kSilenceDb };
        float width    { 0.0f };
        float phase    { 1.0f };
        float crest    { 0.0f };
    };

    static constexpr float kFloorDb = -100.0f;
    static constexpr float kCeilDb  = 0.0f;
    static float dbToUnit (float db) { return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kCeilDb - kFloorDb)); }
    static float unitToDb (float v)  { return kFloorDb + juce::jlimit (0.0f, 1.0f, v) * (kCeilDb - kFloorDb); }

    // Correction / EQ scale (± this many dB over the plot height) — the band
    // gain range and the match clamp.
    static constexpr float kCorrectionRangeDb = 24.0f;

    TelemetryCanvas();
    ~TelemetryCanvas() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setUserBins (const float* bins, int n, double sampleRate);
    void setReference (const float* bins, int n);   // nullptr / 0 clears
    void setInputBins (const float* bins, int n);   // pre-processing spectrum, drawn faintly; nullptr hides
    bool hasReference() const { return hasRef; }

    void setUserReadout (const Readout& r) { userReadout = r; }
    void setRefReadout  (const Readout* r) { hasRefReadout = (r != nullptr); if (r != nullptr) refReadout = *r; }

    // Match-EQ correction (dB per analyzer bin): mid (or linked) and side
    // (empty unless matching mid/side). `applied` = SHAPE is on.
    void setCorrection (const std::vector<float>& db, const std::vector<float>& sideDb, bool applied);

    // ── Parametric EQ. Double-click empty space to add a band; drag a node
    //    (shift = fine), scroll over it for Q, right-click for type / slope /
    //    placement, double-click or alt-click to delete. ──────────────────
    void setBands (const std::array<Band, kNumBands>& newBands);
    std::function<void (int)> onBandGestureStart, onBandGestureEnd;
    std::function<void (int, const Band&)> onBandChanged;

    // Transient status line under the legend (e.g. "Analyzing reference…").
    void setStatus (const juce::String& s) { if (s != status) { status = s; repaint(); } }

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

    // ── Trace — a hand-drawn target curve. Click to add a point, drag a point
    //    to move it, right-click (or double-click) a point to delete it. ──
    void setTraceMode (bool on);
    bool getTraceMode() const { return traceMode; }
    void setTrace (const std::vector<TracePoint>& points) { trace = points; repaint(); }
    bool hasTrace() const     { return trace.size() >= 2; }
    std::function<void (std::vector<TracePoint>)> onTraceEdited;

    // Clicking the YOU readout resets integrated loudness.
    std::function<void()> onReadoutClicked;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kNumBins = AudioAnalyzer::numBins;

private:
    void timerCallback() override;

    void drawGrid (juce::Graphics& g);
    void drawMaster (juce::Graphics& g);
    void drawFocused (juce::Graphics& g);
    void drawCorrection (juce::Graphics& g);
    juce::Path correctionPath (const std::vector<float>& db) const;
    void rebuildCurves();   // match + EQ paths; only when their data or the geometry changes
    void drawEq (juce::Graphics& g);

    // EQ helpers
    float yForGain (float db) const;
    float gainForY (float y) const;
    juce::Point<float> nodePosition (int band) const;
    int  bandAt (juce::Point<float> p) const;          // -1 if none
    void editBand (int band, const Band& b);           // single-shot edit (own gesture)
    void showBandMenu (int band);
    double bandResponseDb (int band, double hz) const;
    static juce::Colour bandColour (int band);
    juce::String bandDescription (int band) const;

    // Display resolution: curves are drawn at 1/12-octave resolution, sampled
    // every couple of pixels — bins are averaged where they are dense (highs)
    // and interpolated (Catmull-Rom) where they are sparse (lows).
    struct Series { const float* bins; const double* prefix; };
    Series userSeries() const { return { smoothUser, prefixUser }; }
    Series refSeries() const  { return { smoothRef,  prefixRef }; }
    Series peakSeries() const { return { peakHold,   prefixPeak }; }
    Series inSeries() const   { return { smoothIn,   prefixIn }; }
    float sampleSeries (const Series& s, float hz) const;
    juce::Path buildCurve (const Series& s) const;
    juce::Path buildBandCurve (const Series& s, float loHz, float hiHz) const;
    float xForFreq (float hz) const;
    float freqForX (float x) const;
    float yForValue (float v) const;
    float yForDb (float db) const { return yForValue (dbToUnit (db)); }
    float dbForY (float y) const;
    float binHz() const { return (float) sampleRate / (float) AudioAnalyzer::fftSize; }

    void drawLegend (juce::Graphics& g);
    void drawMasterLegend (juce::Graphics& g);
    void drawReadout (juce::Graphics& g);
    void drawGroupMeter (juce::Graphics& g);
    void drawHint (juce::Graphics& g, const juce::String& text);
    void drawRefImage (juce::Graphics& g);
    void drawTrace (juce::Graphics& g);
    float groupEnergy (FocusModel::Group g, const float* bins) const;
    void updateAxisRange();

    int  tracePointAt (juce::Point<float> p) const;   // -1 if none
    juce::Point<float> tracePointPosition (const TracePoint& p) const;
    void commitTrace();

    void fitImageToPlot();
    juce::Rectangle<float> plotRect() const { return { plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop }; }
    juce::Rectangle<float> youReadoutRect() const { return { plotLeft, 33.0f, 420.0f, 12.0f }; }

    // Smoothed display values (0..1 on the dB scale) for user + reference.
    float smoothUser[kNumBins] { 0.0f };
    float smoothRef [kNumBins] { 0.0f };
    float targetUser[kNumBins] { 0.0f };
    float targetRef [kNumBins] { 0.0f };
    bool  hasUser { false };
    bool  hasRef  { false };

    // Peak-hold envelope (slow release) drawn as a faint line above the curve.
    float peakHold[kNumBins] { 0.0f };

    // Pre-processing spectrum (shown faintly while the plugin is changing the sound).
    float smoothIn[kNumBins] { 0.0f };
    float targetIn[kNumBins] { 0.0f };
    bool  hasIn { false };

    // Running sums for the display smoothing (index i = sum of bins [0, i)).
    double prefixUser[kNumBins + 1] {}, prefixRef[kNumBins + 1] {}, prefixPeak[kNumBins + 1] {}, prefixIn[kNumBins + 1] {};

    Readout userReadout, refReadout;
    bool hasRefReadout { false };

    std::vector<float> correction, correctionSide;
    bool correctionApplied { false };

    // Parametric EQ state (mirrors the processor's parameters).
    std::array<Band, kNumBands> bands;
    std::array<std::array<ParametricEq::Biquad, ParametricEq::kMaxSections>, kNumBands> bandSections;
    std::array<int, kNumBands> bandSectionCount {};
    int hoverBand { -1 }, dragBand { -1 }, selectedBand { -1 };
    juce::Point<float> dragStartPos;
    Band dragOrigin;
    juce::String status;

    // Reference image layer (screenshot ghost).
    juce::Image refImage;
    bool imageLoaded { false };
    bool imageVisible { true };
    float imageOpacity { 0.5f };
    juce::Rectangle<float> imageBounds;
    bool mouseHover { false };
    juce::Point<float> lastMouse;

    // Hand-drawn trace (Hz, dB).
    bool traceMode { false };
    std::vector<TracePoint> trace;
    int dragPoint { -1 };

    double sampleRate { 44100.0 };
    FocusModel::Group focus { FocusModel::Group::Master };

    float axisMin { 20.0f }, axisMax { 20000.0f };

    static constexpr int kFrameHz = 30;

    // Cached curve paths (see rebuildCurves).
    juce::Path corrStroke, corrFill, sideDashed, totalPath;
    std::array<juce::Path, kNumBands> bandPaths;
    juce::Rectangle<float> curvesKey;
    bool curvesDirty { true };
    juce::Image gridCache;
    juce::Rectangle<float> gridCacheKey;   // (axisMin, axisMax, width, height)
    float gridCacheScale { 0.0f };

    float plotLeft { 44.0f }, plotRight { 0.0f }, plotTop { 56.0f }, plotBottom { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelemetryCanvas)
};
