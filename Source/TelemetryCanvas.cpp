#include "TelemetryCanvas.h"
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════
//  ColorSwatch
// ═══════════════════════════════════════════════════════════════════════════

void ColorSwatch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (swatch);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ColorSwatch::mouseDown (const juce::MouseEvent&)
{
    auto* cs = new juce::ColourSelector (juce::ColourSelector::showColourAtTop
                                         | juce::ColourSelector::showSliders
                                         | juce::ColourSelector::editableColour);
    cs->setName ("Focus colour");
    cs->setCurrentColour (swatch);
    cs->setSize (320, 400);
    cs->addChangeListener (this);

    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (cs),
                                            getScreenBounds(), nullptr);
}

void ColorSwatch::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
    {
        swatch = cs->getCurrentColour();
        if (onColourPicked) onColourPicked (swatch);
        repaint();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  TelemetryCanvas
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
    juce::FontOptions uiFont (float size, bool bold = false)
    {
        return juce::FontOptions ("Helvetica Neue", size, bold ? juce::Font::bold : juce::Font::plain);
    }

    float binToDb (float v) { return v > 1.0e-5f ? 20.0f * std::log10 (v) : -120.0f; }
}

TelemetryCanvas::TelemetryCanvas()
{
    setWantsKeyboardFocus (true);   // Delete removes the selected band
    startTimerHz (kFrameHz);
}

TelemetryCanvas::~TelemetryCanvas()
{
    stopTimer();
}

void TelemetryCanvas::resized()
{
    plotRight  = (float) getWidth() - 36.0f;    // room for the correction scale
    plotBottom = (float) getHeight() - 44.0f;   // room for freq labels + group meter
}

void TelemetryCanvas::setUserBins (const float* bins, int n, double sr)
{
    if (! juce::exactlyEqual (sr, sampleRate))
    {
        sampleRate = sr;
        for (int i = 0; i < kNumBands; ++i)   // band shapes depend on the rate
            bandSectionCount[(size_t) i] = ParametricEq::design (bands[(size_t) i], sampleRate, bandSections[(size_t) i].data());
        curvesDirty = true;
    }
    hasUser = false;
    const int count = juce::jmin (n, kNumBins);
    for (int i = 0; i < count; ++i)
    {
        const float db = binToDb (bins[i]);
        targetUser[i] = dbToUnit (db);
        if (db > -90.0f) hasUser = true;
    }
}

void TelemetryCanvas::setReference (const float* bins, int n)
{
    hasRef = false;
    if (bins == nullptr || n <= 0)
    {
        std::fill (std::begin (targetRef), std::end (targetRef), 0.0f);
        std::fill (std::begin (smoothRef), std::end (smoothRef), 0.0f);
        repaint();
        return;
    }

    const int count = juce::jmin (n, kNumBins);
    for (int i = 0; i < count; ++i)
    {
        const float db = binToDb (bins[i]);
        targetRef[i] = dbToUnit (db);
        if (db > -90.0f) hasRef = true;
    }
    repaint();
}

void TelemetryCanvas::setInputBins (const float* bins, int n)
{
    hasIn = false;
    if (bins == nullptr || n <= 0) return;
    const int count = juce::jmin (n, kNumBins);
    for (int i = 0; i < count; ++i)
    {
        const float db = binToDb (bins[i]);
        targetIn[i] = dbToUnit (db);
        if (db > -90.0f) hasIn = true;
    }
}

void TelemetryCanvas::setCorrection (const std::vector<float>& db, const std::vector<float>& sideDb, bool applied)
{
    if (db != correction || sideDb != correctionSide) { correction = db; correctionSide = sideDb; curvesDirty = true; }
    correctionApplied = applied;
}

void TelemetryCanvas::timerCallback()
{
    // Same visual time constants at any frame rate (tuned at 60 Hz).
    static const float k    = 1.0f - std::pow (1.0f - 0.12f, 60.0f / (float) kFrameHz);
    static const float hold = std::pow (0.996f, 60.0f / (float) kFrameHz);
    for (int i = 0; i < kNumBins; ++i)
    {
        smoothUser[i] += (targetUser[i] - smoothUser[i]) * k;
        smoothRef [i] += (targetRef [i] - smoothRef [i]) * k;
        smoothIn  [i] += (targetIn  [i] - smoothIn  [i]) * k;
        peakHold[i] = juce::jmax (peakHold[i] * hold, smoothUser[i]);
    }

    for (int i = 0; i < kNumBins; ++i)
    {
        prefixUser[i + 1] = prefixUser[i] + smoothUser[i];
        prefixRef [i + 1] = prefixRef [i] + smoothRef [i];
        prefixPeak[i + 1] = prefixPeak[i] + peakHold[i];
        prefixIn  [i + 1] = prefixIn  [i] + smoothIn[i];
    }
    repaint();
}

// ── Coordinate mapping ──────────────────────────────────────────────────────

void TelemetryCanvas::updateAxisRange()
{
    if (focus == FocusModel::Group::Master) { axisMin = 20.0f; axisMax = 20000.0f; return; }

    const auto r = FocusModel::bandRange (focus);
    const float pad = r.getLength() * 0.08f;
    axisMin = juce::jmax (20.0f, r.getStart() - pad);
    axisMax = juce::jmin (20000.0f, r.getEnd() + pad);
}

float TelemetryCanvas::xForFreq (float hz) const
{
    const float lo = std::log10 (axisMin);
    const float hi = std::log10 (axisMax);
    const float t  = (std::log10 (juce::jlimit (axisMin, axisMax, hz)) - lo) / (hi - lo);
    return plotLeft + (plotRight - plotLeft) * t;
}

float TelemetryCanvas::freqForX (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - plotLeft) / (plotRight - plotLeft));
    return axisMin * std::pow (axisMax / axisMin, t);
}

float TelemetryCanvas::yForValue (float v) const
{
    return plotBottom - (plotBottom - plotTop) * juce::jlimit (0.0f, 1.0f, v);
}

float TelemetryCanvas::dbForY (float y) const
{
    return unitToDb ((plotBottom - y) / (plotBottom - plotTop));
}

float TelemetryCanvas::sampleSeries (const Series& s, float hz) const
{
    static const float halfWindow = std::pow (2.0f, 1.0f / 24.0f) - 1.0f;   // ±1/24 octave

    const float b    = juce::jlimit (0.0f, (float) (kNumBins - 1), hz / binHz());
    const float half = b * halfWindow;

    if (half >= 1.0f)
    {
        const int lo = juce::jmax (0, (int) std::lround (b - half));
        const int hi = juce::jmin (kNumBins - 1, (int) std::lround (b + half));
        return (float) ((s.prefix[hi + 1] - s.prefix[lo]) / (double) (hi - lo + 1));
    }

    const int   i1 = (int) b;
    const float t  = b - (float) i1;
    const float y0 = s.bins[juce::jmax (0, i1 - 1)];
    const float y1 = s.bins[i1];
    const float y2 = s.bins[juce::jmin (kNumBins - 1, i1 + 1)];
    const float y3 = s.bins[juce::jmin (kNumBins - 1, i1 + 2)];
    const float v  = y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3
                                                     + t * (3.0f * (y1 - y2) + y3 - y0)));
    return juce::jlimit (0.0f, 1.0f, v);
}

juce::Path TelemetryCanvas::buildCurve (const Series& s) const
{
    return buildBandCurve (s, axisMin, axisMax);
}

juce::Path TelemetryCanvas::buildBandCurve (const Series& s, float loHz, float hiHz) const
{
    juce::Path p;
    loHz = juce::jmax (loHz, axisMin, binHz());
    hiHz = juce::jmin (hiHz, axisMax);
    if (hiHz <= loHz) return p;

    const float x0 = xForFreq (loHz), x1 = xForFreq (hiHz);
    p.startNewSubPath (x0, yForValue (sampleSeries (s, loHz)));
    for (float x = x0 + 2.0f; x < x1; x += 2.0f)
        p.lineTo (x, yForValue (sampleSeries (s, freqForX (x))));
    p.lineTo (x1, yForValue (sampleSeries (s, hiHz)));
    return p;
}

// ── Reference layer (image ghost) ──────────────────────────────────────────

void TelemetryCanvas::setRefImage (const juce::Image& img)
{
    if (img.isNull()) { clearRefImage(); return; }
    refImage = img;
    imageLoaded = true;
    imageVisible = true;
    fitImageToPlot();
    repaint();
}

void TelemetryCanvas::clearRefImage()
{
    refImage = juce::Image();
    imageLoaded = false;
    imageVisible = true;
    repaint();
}

void TelemetryCanvas::fitImageToPlot()
{
    imageBounds = plotRect();
}

void TelemetryCanvas::drawRefImage (juce::Graphics& g)
{
    if (! imageLoaded || ! imageVisible) return;

    g.saveState();
    g.reduceClipRegion (plotRect().toNearestInt());
    g.setOpacity (imageOpacity);
    g.drawImage (refImage, imageBounds, juce::RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);

    // Dashed bounds so the layer is visible and alignable.
    const float dash[2] = { 4.0f, 4.0f };
    g.setColour (juce::Colours::white.withAlpha (mouseHover ? 0.55f : 0.22f));
    const auto& r = imageBounds;
    g.drawDashedLine (juce::Line<float> (r.getX(), r.getY(), r.getRight(), r.getY()), dash, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (r.getX(), r.getBottom(), r.getRight(), r.getBottom()), dash, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (r.getX(), r.getY(), r.getX(), r.getBottom()), dash, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (r.getRight(), r.getY(), r.getRight(), r.getBottom()), dash, 2, 1.0f);
    g.restoreState();

    if (! traceMode)
    {
        g.setFont (uiFont (8.0f));
        g.setColour (JP::textDim.withAlpha (0.45f));
        g.drawText (juce::String::fromUTF8 ("shift-drag move · shift-scroll zoom · shift-alt-drag stretch · shift-double-click fit"),
                    juce::Rectangle<float> (plotLeft + 4.0f, plotBottom - 16.0f, plotRight - plotLeft - 8.0f, 14.0f),
                    juce::Justification::left, false);
    }
}

// ── Trace ───────────────────────────────────────────────────────────────────

void TelemetryCanvas::setTraceMode (bool on)
{
    traceMode = on;
    dragPoint = -1;
    setMouseCursor (on ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

juce::Point<float> TelemetryCanvas::tracePointPosition (const TracePoint& p) const
{
    return { xForFreq (p.hz), yForDb (p.db) };
}

int TelemetryCanvas::tracePointAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 9.0f;   // px
    for (size_t i = 0; i < trace.size(); ++i)
    {
        if (trace[i].hz < axisMin || trace[i].hz > axisMax) continue;
        const float d = tracePointPosition (trace[i]).getDistanceFrom (pos);
        if (d < bestDist) { bestDist = d; best = (int) i; }
    }
    return best;
}

void TelemetryCanvas::commitTrace()
{
    repaint();
    if (onTraceEdited) onTraceEdited (trace);
}

void TelemetryCanvas::drawTrace (juce::Graphics& g)
{
    if (trace.empty()) return;

    g.saveState();
    g.reduceClipRegion (plotRect().expanded (4.0f).toNearestInt());

    // The shaper interpolates linearly in log-frequency and dB, which is a
    // straight line on this plot — so straight segments are the honest view.
    if (trace.size() >= 2)
    {
        juce::Path p;
        p.startNewSubPath (tracePointPosition (trace.front()));
        for (size_t i = 1; i < trace.size(); ++i)
            p.lineTo (tracePointPosition (trace[i]));

        juce::Path dashed;
        const float dash[2] = { 6.0f, 4.0f };
        juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded)
            .createDashedStroke (dashed, p, dash, 2);
        g.setColour (juce::Colours::white.withAlpha (traceMode ? 0.9f : 0.6f));
        g.fillPath (dashed);
    }

    for (size_t i = 0; i < trace.size(); ++i)
    {
        const auto c = tracePointPosition (trace[i]);
        const bool hot = traceMode && (int) i == dragPoint;
        g.setColour (juce::Colours::white.withAlpha (traceMode ? 0.95f : 0.6f));
        g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        if (hot)
        {
            g.setColour (JP::accent);
            g.drawEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 1.5f);
        }
    }
    g.restoreState();

    if (traceMode)
    {
        g.setFont (uiFont (8.0f));
        g.setColour (JP::textDim.withAlpha (0.6f));
        g.drawText (juce::String::fromUTF8 ("click add · drag move · right-click / double-click delete"),
                    juce::Rectangle<float> (plotLeft + 4.0f, plotBottom - 16.0f, plotRight - plotLeft - 8.0f, 14.0f),
                    juce::Justification::left, false);
    }
}

// ── Mouse interaction (trace / image layer / readout) ──────────────────────

void TelemetryCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (traceMode)
    {
        const int hit = tracePointAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0)               trace.erase (trace.begin() + hit);
            else if (! trace.empty())   trace.pop_back();
            dragPoint = -1;
            commitTrace();
            return;
        }

        if (hit >= 0) { dragPoint = hit; repaint(); return; }

        if (plotRect().contains (e.position))
        {
            const TracePoint p { freqForX (e.position.x), dbForY (e.position.y) };
            const auto it = std::upper_bound (trace.begin(), trace.end(), p,
                                              [] (const TracePoint& a, const TracePoint& b) { return a.hz < b.hz; });
            dragPoint = (int) std::distance (trace.begin(), trace.insert (it, p));
            commitTrace();
        }
        return;
    }

    if (youReadoutRect().contains (e.position))
    {
        if (onReadoutClicked) onReadoutClicked();
        return;
    }

    // Shift = the screenshot layer; otherwise the EQ.
    if (e.mods.isShiftDown() && imageLoaded && imageVisible)
    {
        lastMouse = e.position;
        return;
    }

    grabKeyboardFocus();
    const int hit = bandAt (e.position);
    if (hit < 0) { selectedBand = -1; repaint(); return; }

    selectedBand = hit;
    if (e.mods.isPopupMenu()) { showBandMenu (hit); return; }
    if (e.mods.isAltDown())
    {
        auto b = bands[(size_t) hit];
        b.on = false;
        editBand (hit, b);
        selectedBand = -1;
        return;
    }

    dragBand = hit;
    dragOrigin = bands[(size_t) hit];
    dragStartPos = e.position;
    if (onBandGestureStart) onBandGestureStart (hit);
    repaint();
}

void TelemetryCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (traceMode)
    {
        if (dragPoint < 0 || dragPoint >= (int) trace.size()) return;

        // Keep the frequency order stable while dragging (the processor
        // stores the trace sorted, and the index must keep meaning this point).
        const auto i = (size_t) dragPoint;
        float hz = freqForX (e.position.x);
        if (i > 0)                hz = juce::jmax (hz, trace[i - 1].hz * 1.01f);
        if (i + 1 < trace.size()) hz = juce::jmin (hz, trace[i + 1].hz / 1.01f);
        trace[i] = { hz, dbForY (e.position.y) };
        commitTrace();
        return;
    }

    if (dragBand >= 0)
    {
        // Shift = fine adjustment (a tenth of the mouse movement).
        const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const juce::Point<float> origin (xForFreq (dragOrigin.freq), yForGain (dragOrigin.gainDb));
        const auto target = origin + (e.position - dragStartPos) * scale;

        auto b = bands[(size_t) dragBand];
        b.freq = juce::jlimit (ParametricEq::kMinHz, ParametricEq::kMaxHz, freqForX (target.x));
        if (ParametricEq::hasGain (b.type))
            b.gainDb = juce::jlimit (-ParametricEq::kMaxGainDb, ParametricEq::kMaxGainDb, gainForY (target.y));
        bands[(size_t) dragBand] = b;
        bandSectionCount[(size_t) dragBand] = ParametricEq::design (b, sampleRate, bandSections[(size_t) dragBand].data());
        curvesDirty = true;
        if (onBandChanged) onBandChanged (dragBand, b);
        repaint();
        return;
    }

    if (! e.mods.isShiftDown() || ! imageLoaded || ! imageVisible) return;

    const float dx = e.position.x - lastMouse.x;
    const float dy = e.position.y - lastMouse.y;

    if (e.mods.isAltDown())
        imageBounds.setSize (juce::jmax (8.0f, imageBounds.getWidth()  + dx),
                             juce::jmax (8.0f, imageBounds.getHeight() + dy));
    else
        imageBounds.translate (dx, dy);

    lastMouse = e.position;
    repaint();
}

void TelemetryCanvas::mouseUp (const juce::MouseEvent&)
{
    if (dragPoint >= 0) { dragPoint = -1; repaint(); }
    if (dragBand >= 0)
    {
        if (onBandGestureEnd) onBandGestureEnd (dragBand);
        dragBand = -1;
        repaint();
    }
}

void TelemetryCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (traceMode) return;

    const float factor = (wheel.deltaY > 0.0f) ? 1.1f : 1.0f / 1.1f;

    if (e.mods.isShiftDown() && imageLoaded && imageVisible)
    {
        const float cx = e.position.x, cy = e.position.y;
        const float w = imageBounds.getWidth(), h = imageBounds.getHeight();
        imageBounds.setBounds (cx - (cx - imageBounds.getX()) * factor,
                               cy - (cy - imageBounds.getY()) * factor,
                               w * factor, h * factor);
        repaint();
        return;
    }

    const int hit = bandAt (e.position);
    if (hit < 0) return;
    auto b = bands[(size_t) hit];
    b.q = juce::jlimit (ParametricEq::kMinQ, ParametricEq::kMaxQ, b.q * factor);
    selectedBand = hit;
    editBand (hit, b);
}

void TelemetryCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (traceMode)
    {
        const int hit = tracePointAt (e.position);
        if (hit >= 0)
        {
            trace.erase (trace.begin() + hit);
            dragPoint = -1;
            commitTrace();
        }
        return;
    }

    if (e.mods.isShiftDown())
    {
        if (imageLoaded) { fitImageToPlot(); repaint(); }
        return;
    }

    // Double-click a node: delete it. Empty space: add a bell there.
    const int hit = bandAt (e.position);
    if (hit >= 0)
    {
        auto b = bands[(size_t) hit];
        b.on = false;
        editBand (hit, b);
        selectedBand = -1;
        return;
    }

    if (! plotRect().contains (e.position)) return;
    for (int i = 0; i < kNumBands; ++i)
    {
        if (bands[(size_t) i].on) continue;
        Band b;
        b.on     = true;
        b.type   = ParametricEq::Type::bell;
        b.freq   = juce::jlimit (ParametricEq::kMinHz, ParametricEq::kMaxHz, freqForX (e.position.x));
        b.gainDb = juce::jlimit (-ParametricEq::kMaxGainDb, ParametricEq::kMaxGainDb, gainForY (e.position.y));
        b.q      = 1.0f;
        selectedBand = i;
        editBand (i, b);
        return;
    }
}

void TelemetryCanvas::mouseMove (const juce::MouseEvent& e)
{
    const bool over = imageLoaded && imageVisible && imageBounds.contains (e.position);
    if (over != mouseHover) { mouseHover = over; repaint(); }

    const int hover = traceMode ? -1 : bandAt (e.position);
    if (hover != hoverBand) { hoverBand = hover; repaint(); }

    if (! traceMode)
        setMouseCursor (youReadoutRect().contains (e.position) ? juce::MouseCursor::PointingHandCursor
                      : hover >= 0                               ? juce::MouseCursor::DraggingHandCursor
                                                                 : juce::MouseCursor::NormalCursor);
}

void TelemetryCanvas::mouseExit (const juce::MouseEvent&)
{
    if (mouseHover) { mouseHover = false; repaint(); }
    if (hoverBand >= 0) { hoverBand = -1; repaint(); }
}

bool TelemetryCanvas::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && juce::isPositiveAndBelow (selectedBand, kNumBands) && bands[(size_t) selectedBand].on)
    {
        auto b = bands[(size_t) selectedBand];
        b.on = false;
        editBand (selectedBand, b);
        selectedBand = -1;
        return true;
    }
    return false;
}

// ── Parametric EQ ───────────────────────────────────────────────────────────

juce::Colour TelemetryCanvas::bandColour (int band)
{
    static const juce::Colour colours[kNumBands] = {
        juce::Colour (0xffff8a80), juce::Colour (0xffffcc80), juce::Colour (0xffffff8d), juce::Colour (0xffb9f6ca),
        juce::Colour (0xff80d8ff), juce::Colour (0xffb388ff), juce::Colour (0xffea80fc), juce::Colour (0xffe0e0e0) };
    return colours[(size_t) juce::jlimit (0, kNumBands - 1, band)];
}

void TelemetryCanvas::setBands (const std::array<Band, kNumBands>& newBands)
{
    for (int i = 0; i < kNumBands; ++i)
    {
        if (i == dragBand) continue;   // the drag owns this band until mouse-up
        const auto& a = bands[(size_t) i];
        const auto& b = newBands[(size_t) i];
        const bool same = a.on == b.on && a.type == b.type && a.slope == b.slope && a.placement == b.placement
                       && juce::exactlyEqual (a.freq, b.freq) && juce::exactlyEqual (a.gainDb, b.gainDb)
                       && juce::exactlyEqual (a.q, b.q);
        if (same && bandSectionCount[(size_t) i] > 0) continue;
        bands[(size_t) i] = b;
        bandSectionCount[(size_t) i] = ParametricEq::design (b, sampleRate, bandSections[(size_t) i].data());
        curvesDirty = true;
        repaint();
    }
}

void TelemetryCanvas::editBand (int band, const Band& b)
{
    bands[(size_t) band] = b;
    bandSectionCount[(size_t) band] = ParametricEq::design (b, sampleRate, bandSections[(size_t) band].data());
    curvesDirty = true;
    if (onBandGestureStart) onBandGestureStart (band);
    if (onBandChanged)      onBandChanged (band, b);
    if (onBandGestureEnd)   onBandGestureEnd (band);
    repaint();
}

float TelemetryCanvas::yForGain (float db) const
{
    const float mid = 0.5f * (plotTop + plotBottom);
    return mid - juce::jlimit (-kCorrectionRangeDb, kCorrectionRangeDb, db) * 0.5f * (plotBottom - plotTop) / kCorrectionRangeDb;
}

float TelemetryCanvas::gainForY (float y) const
{
    const float mid = 0.5f * (plotTop + plotBottom);
    return (mid - y) / (0.5f * (plotBottom - plotTop)) * kCorrectionRangeDb;
}

juce::Point<float> TelemetryCanvas::nodePosition (int band) const
{
    const auto& b = bands[(size_t) band];
    return { xForFreq (b.freq), yForGain (ParametricEq::hasGain (b.type) ? b.gainDb : 0.0f) };
}

int TelemetryCanvas::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 10.0f;   // px
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        if (! b.on || b.freq < axisMin || b.freq > axisMax) continue;
        const float d = nodePosition (i).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

double TelemetryCanvas::bandResponseDb (int band, double hz) const
{
    return ParametricEq::magnitudeDb (bandSections[(size_t) band].data(), bandSectionCount[(size_t) band], hz, sampleRate);
}

juce::String TelemetryCanvas::bandDescription (int band) const
{
    const auto& b = bands[(size_t) band];
    juce::String s = juce::String (band + 1) + "  " + ParametricEq::typeNames()[(int) b.type] + "  ";
    s << (b.freq >= 1000.0f ? juce::String (b.freq / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (b.freq)) + " Hz");
    if (ParametricEq::hasGain (b.type)) s << juce::String::formatted ("  %+.1f dB", b.gainDb);
    s << "  Q " << juce::String (b.q, 2);
    if (b.type == ParametricEq::Type::lowCut || b.type == ParametricEq::Type::highCut) s << "  " << b.slope << " dB/oct";
    if (b.placement != ParametricEq::Placement::stereo) s << "  " << ParametricEq::placementNames()[(int) b.placement];
    return s;
}

void TelemetryCanvas::showBandMenu (int band)
{
    const auto b = bands[(size_t) band];
    juce::PopupMenu m, types, slopes, places;
    const auto set = [this, band] (std::function<void (Band&)> change)
    {
        return [safe = juce::Component::SafePointer<TelemetryCanvas> (this), band, change]
        {
            if (safe == nullptr) return;
            auto nb = safe->bands[(size_t) band];
            change (nb);
            safe->editBand (band, nb);
        };
    };

    for (int t = 0; t < ParametricEq::typeNames().size(); ++t)
        types.addItem (ParametricEq::typeNames()[t], true, (int) b.type == t,
                       set ([t] (Band& x) { x.type = (ParametricEq::Type) t; }));
    const bool isCut = b.type == ParametricEq::Type::lowCut || b.type == ParametricEq::Type::highCut;
    for (int i = 0; i < 3; ++i)
        slopes.addItem (ParametricEq::slopeNames()[i], isCut, ParametricEq::indexFromSlope (b.slope) == i,
                        set ([i] (Band& x) { x.slope = ParametricEq::slopeFromIndex (i); }));
    for (int p = 0; p < 3; ++p)
        places.addItem (ParametricEq::placementNames()[p], true, (int) b.placement == p,
                        set ([p] (Band& x) { x.placement = (ParametricEq::Placement) p; }));

    m.addSectionHeader (bandDescription (band));
    m.addSubMenu ("Type", types);
    m.addSubMenu ("Slope", slopes, isCut);
    m.addSubMenu ("Stereo placement", places);
    if (ParametricEq::hasGain (b.type))
        m.addItem ("Reset gain", set ([] (Band& x) { x.gainDb = 0.0f; }));
    m.addSeparator();
    m.addItem ("Delete band", set ([] (Band& x) { x.on = false; }));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
        juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (nodePosition (band)).toInt())));
}

void TelemetryCanvas::drawEq (juce::Graphics& g)
{
    const float mid = 0.5f * (plotTop + plotBottom);
    bool any = false;
    for (const auto& b : bands) any |= b.on;

    if (! any)
    {
        if (! traceMode)
        {
            g.setFont (uiFont (8.0f));
            g.setColour (JP::textDim.withAlpha (0.6f));
            g.drawText ("double-click to add an EQ band", juce::Rectangle<float> (plotRight - 200.0f, plotBottom - 16.0f, 196.0f, 14.0f),
                        juce::Justification::right, false);
        }
        return;
    }

    g.saveState();
    g.reduceClipRegion (plotRect().toNearestInt());

    // Only the band being looked at gets a filled shape; area fills are the
    // expensive part of a software-rendered frame.
    const float x0 = xForFreq (axisMin), x1 = xForFreq (axisMax);
    for (int i = 0; i < kNumBands; ++i)
    {
        if (! bands[(size_t) i].on) continue;
        if (i != hoverBand && i != selectedBand && i != dragBand) continue;
        const auto& p = bandPaths[(size_t) i];
        juce::Path fill = p;
        fill.lineTo (x1, mid);
        fill.lineTo (x0, mid);
        fill.closeSubPath();
        g.setColour (bandColour (i).withAlpha (0.22f));
        g.fillPath (fill);
        g.setColour (bandColour (i).withAlpha (0.6f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (totalPath, juce::PathStrokeType (2.0f));
    g.restoreState();

    // Nodes.
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        if (! b.on || b.freq < axisMin || b.freq > axisMax) continue;
        const auto c = nodePosition (i);
        const bool hot = (i == hoverBand || i == selectedBand || i == dragBand);
        const float r = hot ? 8.0f : 6.5f;
        g.setColour (bandColour (i));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (JP::bg.withAlpha (0.9f));
        g.setFont (uiFont (8.5f, true));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred, false);
        if (hot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawEllipse (c.x - r - 2.0f, c.y - r - 2.0f, 2 * r + 4.0f, 2 * r + 4.0f, 1.0f);
        }
    }

    // Readout for the band under the mouse / being edited.
    const int shown = dragBand >= 0 ? dragBand : (hoverBand >= 0 ? hoverBand : selectedBand);
    if (juce::isPositiveAndBelow (shown, kNumBands) && bands[(size_t) shown].on)
    {
        const auto c = nodePosition (shown);
        const auto text = bandDescription (shown);
        g.setFont (uiFont (9.5f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
        auto r = juce::Rectangle<float> (c.x - w * 0.5f, c.y - 34.0f, w, 18.0f);
        if (r.getY() < plotTop) r.setY (c.y + 16.0f);
        r = r.constrainedWithin (plotRect());
        g.setColour (JP::surfaceRaised.withAlpha (0.95f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (bandColour (shown));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (JP::text);
        g.drawText (text, r, juce::Justification::centred, false);
    }
}

// ── Paint ───────────────────────────────────────────────────────────────────

void TelemetryCanvas::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    updateAxisRange();

    drawRefImage (g);   // screenshot ghost behind everything
    if (curvesDirty || curvesKey != juce::Rectangle<float> (axisMin, axisMax, (float) getWidth(), (float) getHeight()))
        rebuildCurves();

    // The grid only changes with size / zoom: render it once, blit it after.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (gridCache.isNull() || gridCacheKey != juce::Rectangle<float> (axisMin, axisMax, (float) getWidth(), (float) getHeight())
        || ! juce::approximatelyEqual (gridCacheScale, scale))
    {
        gridCache = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale),
                                 juce::roundToInt ((float) getHeight() * scale), true);
        juce::Graphics gg (gridCache);
        gg.addTransform (juce::AffineTransform::scale (scale));
        drawGrid (gg);
        gridCacheKey   = { axisMin, axisMax, (float) getWidth(), (float) getHeight() };
        gridCacheScale = scale;
    }
    if (juce::approximatelyEqual (scale, 1.0f)) g.drawImageAt (gridCache, 0, 0);   // plain blit, no resampling
    else g.drawImageTransformed (gridCache, juce::AffineTransform::scale (1.0f / scale));

    if (focus == FocusModel::Group::Master) drawMaster (g);
    else                                    drawFocused (g);

    drawCorrection (g);  // match-EQ curve(s)
    drawEq (g);          // parametric bands
    drawTrace (g);       // hand-drawn target curve on top
    drawReadout (g);
    drawGroupMeter (g);
}

void TelemetryCanvas::drawGrid (juce::Graphics& g)
{
    g.setFont (uiFont (8.0f));

    // Level grid every 20 dB, labelled on the left.
    for (float db = kCeilDb; db >= kFloorDb; db -= 20.0f)
    {
        const float y = yForDb (db);
        g.setColour (JP::border.withAlpha (db >= kCeilDb || db <= kFloorDb ? 0.5f : 0.3f));
        g.drawLine (plotLeft, y, plotRight, y, 0.5f);
        g.setColour (JP::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, plotLeft - 6.0f, 12.0f),
                    juce::Justification::right, false);
    }

    static const float ticks[] = { 20,30,40,50,60,80,100,150,200,300,400,500,700,
                                   1000,1500,2000,3000,4000,5000,7000,10000,15000,20000 };

    for (float hz : ticks)
    {
        if (hz < axisMin || hz > axisMax) continue;
        const float x = xForFreq (hz);

        const bool decade = juce::approximatelyEqual (hz, 100.0f) || juce::approximatelyEqual (hz, 1000.0f)
                         || juce::approximatelyEqual (hz, 10000.0f);
        if (decade)
        {
            g.setColour (JP::border.withAlpha (0.25f));
            g.drawLine (x, plotTop, x, plotBottom, 0.5f);
        }

        g.setColour (JP::textDim);
        g.drawLine (x, plotBottom, x, plotBottom + 3.0f, 1.0f);

        juce::String lbl;
        if (hz >= 1000.0f)
        {
            const float k = hz / 1000.0f;
            lbl = juce::approximatelyEqual (k, std::round (k)) ? juce::String ((int) std::round (k)) + "k"
                                                               : juce::String (k, 1) + "k";
        }
        else lbl = juce::String ((int) hz);

        g.drawText (lbl, juce::Rectangle<float> (x - 16.0f, plotBottom + 4.0f, 32.0f, 14.0f),
                    juce::Justification::centred, false);
    }
}

void TelemetryCanvas::drawMaster (juce::Graphics& g)
{
    // Reference overlay (pastel pink) if loaded — drawn first so YOU sits on top.
    if (hasRef)
    {
        const juce::Path rc = buildCurve (refSeries());
        g.setColour (FocusModel::masterReferenceColour().withAlpha (0.9f));
        g.strokePath (rc, juce::PathStrokeType (2.0f));
    }

    // Input (before the match / EQ) — only shown while the sound is being changed.
    if (hasIn)
    {
        g.setColour (JP::text.withAlpha (0.28f));
        g.strokePath (buildCurve (inSeries()), juce::PathStrokeType (1.2f));
    }

    if (! hasUser)
    {
        drawHint (g, hasRef ? "Play your mix to compare against the reference"
                            : juce::String::fromUTF8 ("No signal — play audio through this track, or drop a reference file here"));
        drawMasterLegend (g);
        return;
    }

    // User spectrum coloured per focus-group band: each region of the graph
    // takes its group's saturated hue (bass peak = blue, vocals = violet, …).
    for (auto grp : FocusModel::defaultGroups())
    {
        const auto range = FocusModel::bandRange (grp);
        const auto col   = FocusModel::colorFor (grp).saturated;

        const juce::Path curve = buildBandCurve (userSeries(), range.getStart(), range.getEnd());
        if (curve.isEmpty()) continue;

        juce::Path fill = curve;
        fill.lineTo (curve.getCurrentPosition().x, plotBottom);
        fill.lineTo (curve.getBounds().getX(), plotBottom);
        fill.closeSubPath();

        g.setColour (col.withAlpha (0.09f));   // flat: gradient area fills dominate a software frame
        g.fillPath (fill);

        g.setColour (col.withAlpha (0.95f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    // Peak-hold envelope (subtle) — recent maximum per bin.
    g.setColour (juce::Colours::white.withAlpha (0.20f));
    g.strokePath (buildCurve (peakSeries()), juce::PathStrokeType (1.0f));

    drawMasterLegend (g);
}

void TelemetryCanvas::drawFocused (juce::Graphics& g)
{
    const auto col = FocusModel::colorFor (focus);

    if (! hasUser && ! hasRef)
    {
        drawHint (g, juce::String::fromUTF8 ("No signal — play audio through this track"));
        drawLegend (g);
        return;
    }

    // Reference (pastel) sits behind, user (saturated) on top.
    if (hasRef)
    {
        const juce::Path rc = buildCurve (refSeries());
        g.setColour (col.pastel.withAlpha (0.85f));
        g.strokePath (rc, juce::PathStrokeType (2.0f));
    }

    if (hasIn)
    {
        g.setColour (JP::text.withAlpha (0.28f));
        g.strokePath (buildCurve (inSeries()), juce::PathStrokeType (1.2f));
    }

    if (hasUser)
    {
        const juce::Path curve = buildCurve (userSeries());
        juce::Path fill = curve;
        fill.lineTo (plotRight, plotBottom);
        fill.lineTo (plotLeft, plotBottom);
        fill.closeSubPath();

        g.setColour (col.saturated.withAlpha (0.08f));
        g.fillPath (fill);
        g.setColour (col.saturated.withAlpha (0.95f));
        g.strokePath (curve, juce::PathStrokeType (2.2f));

        g.setColour (col.saturated.withAlpha (0.26f));
        g.strokePath (buildCurve (peakSeries()), juce::PathStrokeType (1.0f));
    }

    if (! hasRef)
        drawHint (g, juce::String::fromUTF8 ("No reference — click LOAD REF or drop an audio file here"));

    drawLegend (g);
}

void TelemetryCanvas::drawCorrection (juce::Graphics& g)
{
    bool eqOn = false;
    for (const auto& b : bands) eqOn |= b.on;
    if (correction.empty() && ! eqOn) return;

    const float mid   = 0.5f * (plotTop + plotBottom);
    const float scale = 0.5f * (plotBottom - plotTop) / kCorrectionRangeDb;

    // 0 dB line + the correction / EQ scale on the right edge.
    g.setColour (JP::text.withAlpha (0.15f));
    const float dash[2] = { 3.0f, 5.0f };
    g.drawDashedLine (juce::Line<float> (plotLeft, mid, plotRight, mid), dash, 2, 1.0f);

    g.setFont (uiFont (8.0f));
    g.setColour (JP::text.withAlpha (0.35f));
    for (float db : { 18.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f })
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                    juce::Rectangle<float> (plotRight + 4.0f, mid - db * scale - 6.0f, 30.0f, 12.0f),
                    juce::Justification::left, false);

    if (corrStroke.isEmpty()) return;
    const auto col = correctionApplied ? FocusModel::matchColour() : JP::text.withAlpha (0.45f);
    g.setColour (col.withAlpha (correctionApplied ? 0.10f : 0.05f));
    g.fillPath (corrFill);
    g.setColour (col.withAlpha (correctionApplied ? 0.95f : 0.7f));
    g.strokePath (corrStroke, juce::PathStrokeType (correctionApplied ? 2.0f : 1.5f));
    g.setColour (col.withAlpha (0.8f));
    g.fillPath (sideDashed);
}

juce::Path TelemetryCanvas::correctionPath (const std::vector<float>& db) const
{
    const float mid   = 0.5f * (plotTop + plotBottom);
    const float scale = 0.5f * (plotBottom - plotTop) / kCorrectionRangeDb;

    juce::Path p;
    bool started = false;
    const float hzPerBin = binHz();
    for (size_t i = 1; i < db.size(); ++i)
    {
        const float hz = (float) i * hzPerBin;
        if (hz < axisMin || hz > axisMax) continue;
        const float x = xForFreq (hz);
        const float y = mid - juce::jlimit (-kCorrectionRangeDb, kCorrectionRangeDb, db[i]) * scale;
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else           p.lineTo (x, y);
    }
    return p;
}

void TelemetryCanvas::rebuildCurves()
{
    const float mid = 0.5f * (plotTop + plotBottom);

    // Match curves (change a few times a second, not per frame).
    corrStroke = correctionPath (correction);
    corrFill.clear();
    if (! corrStroke.isEmpty())
    {
        corrFill = corrStroke;
        corrFill.lineTo (corrStroke.getCurrentPosition().x, mid);
        corrFill.lineTo (corrStroke.getBounds().getX(), mid);
        corrFill.closeSubPath();
    }
    sideDashed.clear();
    const auto side = correctionPath (correctionSide);
    if (! side.isEmpty())
    {
        const float pattern[2] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (sideDashed, side, pattern, 2);
    }

    // EQ curves: each band's shape and the sum, one point every 2 px.
    const float x0 = xForFreq (axisMin), x1 = xForFreq (axisMax);
    std::vector<float> xs;
    for (float x = x0; x < x1; x += 2.0f) xs.push_back (x);
    xs.push_back (x1);

    std::vector<double> total (xs.size(), 0.0);
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& p = bandPaths[(size_t) i];
        p.clear();
        if (! bands[(size_t) i].on) continue;
        for (size_t k = 0; k < xs.size(); ++k)
        {
            const double db = bandResponseDb (i, k + 1 == xs.size() ? axisMax : freqForX (xs[k]));
            total[k] += db;
            if (k == 0) p.startNewSubPath (xs[k], yForGain ((float) db));
            else        p.lineTo (xs[k], yForGain ((float) db));
        }
    }
    totalPath.clear();
    for (size_t k = 0; k < xs.size(); ++k)
    {
        if (k == 0) totalPath.startNewSubPath (xs[k], yForGain ((float) total[k]));
        else        totalPath.lineTo (xs[k], yForGain ((float) total[k]));
    }

    curvesKey   = { axisMin, axisMax, (float) getWidth(), (float) getHeight() };
    curvesDirty = false;
}

// ── Legends + hints ─────────────────────────────────────────────────────────

void TelemetryCanvas::drawLegend (juce::Graphics& g)
{
    const auto col = FocusModel::colorFor (focus);
    float x = plotLeft;
    const float y = 6.0f;

    g.setFont (uiFont (9.0f));
    const auto chip = [&] (juce::Colour c, const juce::String& text, float width)
    {
        g.setColour (c);
        g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
        g.setColour (JP::textDim);
        g.drawText (text, juce::Rectangle<float> (x + 16.0f, y, width, 12.0f), juce::Justification::left, false);
        x += 16.0f + width;
    };

    chip (col.pastel, "REF", 38.0f);
    chip (col.saturated, "YOU", 38.0f);
    if (hasIn)                chip (JP::text.withAlpha (0.35f), "IN", 30.0f);
    if (! correction.empty()) chip (correctionApplied ? FocusModel::matchColour() : JP::textMuted, "MATCH", 48.0f);
    if (hasTrace())           chip (juce::Colours::white, "TRACE", 48.0f);

    g.setColour (JP::text);
    g.setFont (uiFont (10.0f, true));
    g.drawText (FocusModel::groupName (focus),
                juce::Rectangle<float> (plotRight - 120.0f, y, 120.0f, 12.0f),
                juce::Justification::right, false);
}

void TelemetryCanvas::drawMasterLegend (juce::Graphics& g)
{
    const float y = 6.0f;
    g.setFont (uiFont (9.0f));

    // YOU = band-coloured spectrum (one chip per focus group).
    float x = plotLeft;
    for (auto grp : FocusModel::defaultGroups())
    {
        g.setColour (FocusModel::colorFor (grp).saturated);
        g.fillRoundedRectangle (x, y, 7.0f, 10.0f, 1.5f);
        x += 9.0f;
    }
    g.setColour (JP::textDim);
    g.drawText ("YOU", juce::Rectangle<float> (x + 2.0f, y, 34.0f, 12.0f), juce::Justification::left, false);
    x += 38.0f;

    const auto chip = [&] (juce::Colour c, const juce::String& text, float width)
    {
        g.setColour (c);
        g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
        g.setColour (JP::textDim);
        g.drawText (text, juce::Rectangle<float> (x + 16.0f, y, width, 12.0f), juce::Justification::left, false);
        x += 16.0f + width;
    };

    chip (FocusModel::masterReferenceColour(), "REF", 38.0f);
    if (hasIn)                chip (JP::text.withAlpha (0.35f), "IN", 30.0f);
    if (! correction.empty()) chip (correctionApplied ? FocusModel::matchColour() : JP::textMuted, "MATCH", 48.0f);
    if (hasTrace())           chip (juce::Colours::white, "TRACE", 48.0f);

    g.setColour (JP::text);
    g.setFont (uiFont (10.0f, true));
    g.drawText ("Master", juce::Rectangle<float> (plotRight - 120.0f, y, 120.0f, 12.0f),
                juce::Justification::right, false);
}

void TelemetryCanvas::drawHint (juce::Graphics& g, const juce::String& text)
{
    g.setFont (uiFont (13.0f));
    g.setColour (JP::textDim.withAlpha (0.5f));
    g.drawText (text, plotRect(), juce::Justification::centred, false);
}

// ── Scalar readout (LUFS / true peak / width / phase / crest) ──────────────

void TelemetryCanvas::drawReadout (juce::Graphics& g)
{
    const bool master = (focus == FocusModel::Group::Master);
    const juce::Colour refCol = master ? FocusModel::masterReferenceColour()
                                       : FocusModel::colorFor (focus).pastel;
    const juce::Colour youCol = master ? juce::Colour (0xffeeeeee)
                                       : FocusModel::colorFor (focus).saturated;

    const auto valid = [] (float v) { return v > LoudnessMeter::kSilenceDb + 1.0f; };
    const auto fmtDb = [&] (float v, const char* unit)
    {
        return valid (v) ? juce::String (v, 1) + " " + unit : juce::String::fromUTF8 ("— ") + unit;
    };

    const auto row = [&] (float y, const juce::String& who, juce::Colour whoCol, const Readout& r)
    {
        g.setFont (uiFont (9.0f, true));
        g.setColour (whoCol);
        g.drawText (who, juce::Rectangle<float> (plotLeft, y, 32.0f, 12.0f), juce::Justification::left, false);

        g.setFont (uiFont (9.0f));
        g.setColour (JP::textMuted);
        float x = plotLeft + 32.0f;
        const auto cell = [&] (const juce::String& s, float w)
        {
            g.drawText (s, juce::Rectangle<float> (x, y, w, 12.0f), juce::Justification::left, false);
            x += w;
        };
        cell (fmtDb (r.lufs, "LUFS"), 76.0f);
        cell (fmtDb (r.truePeak, "dBTP"), 72.0f);
        cell (juce::String (r.width, 2) + " WID", 60.0f);
        cell ((r.phase >= 0.0f ? "+" : "") + juce::String (r.phase, 2) + " PH", 58.0f);
        cell (juce::String (r.crest, 1) + " CR", 50.0f);
        return x;
    };

    if (hasRefReadout) row (20.0f, "REF", refCol, refReadout);
    const float endX = row (33.0f, "YOU", youCol, userReadout);

    // Loudness difference to the reference — the number you gain-match by.
    if (hasRefReadout && valid (refReadout.lufs) && valid (userReadout.lufs))
    {
        const float d = userReadout.lufs - refReadout.lufs;
        g.setFont (uiFont (9.0f, true));
        g.setColour (std::abs (d) < 1.0f ? JP::text : JP::warning);
        g.drawText (juce::String::formatted ("%+.1f LU vs REF", d),
                    juce::Rectangle<float> (endX + 8.0f, 33.0f, 110.0f, 12.0f), juce::Justification::left, false);
    }

    if (status.isNotEmpty())
    {
        g.setFont (uiFont (9.0f));
        g.setColour (JP::accent);
        g.drawText (status, juce::Rectangle<float> (plotRight - 260.0f, 20.0f, 260.0f, 12.0f),
                    juce::Justification::right, false);
    }
}

// ── Per-focus-group energy meter ───────────────────────────────────────────

float TelemetryCanvas::groupEnergy (FocusModel::Group g, const float* bins) const
{
    const auto range = FocusModel::bandRange (g);
    float sum = 0.0f;
    int count = 0;
    const float hzPerBin = binHz();
    for (int i = 0; i < kNumBins; ++i)
    {
        const float hz = (float) i * hzPerBin;
        if (hz >= range.getStart() && hz < range.getEnd())
        {
            sum += bins[i];
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, sum / (float) count);
}

void TelemetryCanvas::drawGroupMeter (juce::Graphics& g)
{
    const auto groups = FocusModel::defaultGroups();
    const int n = groups.size();
    const float y = (float) getHeight() - 16.0f;
    const float h = 10.0f;
    const float gap = 4.0f;
    const float segW = ((plotRight - plotLeft) - gap * (float) (n - 1)) / (float) n;
    float x = plotLeft;

    for (auto grp : groups)
    {
        const float e  = groupEnergy (grp, smoothUser);
        const float re = hasRef ? groupEnergy (grp, smoothRef) : -1.0f;
        const auto col = FocusModel::colorFor (grp).saturated;

        g.setColour (JP::surfaceRaised);
        g.fillRoundedRectangle (x, y, segW, h, 2.0f);

        if (e > 0.002f)
        {
            g.setColour (col);
            g.fillRoundedRectangle (x, y, segW * e, h, 2.0f);
        }

        if (re >= 0.0f)
        {
            g.setColour (FocusModel::colorFor (grp).pastel);
            const float mx = x + segW * re;
            g.drawLine (mx, y - 3.0f, mx, y + h + 3.0f, 1.5f);
        }

        if (grp == focus)
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRoundedRectangle (x - 1.0f, y - 1.0f, segW + 2.0f, h + 2.0f, 3.0f, 1.0f);
        }

        x += segW + gap;
    }
}
