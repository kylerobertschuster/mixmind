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
    startTimerHz (60);
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
    sampleRate = sr;
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

void TelemetryCanvas::setCorrection (const std::vector<float>& db, bool applied)
{
    correction = db;
    correctionApplied = applied;
}

void TelemetryCanvas::timerCallback()
{
    const float k = 0.12f;
    for (int i = 0; i < kNumBins; ++i)
    {
        smoothUser[i] += (targetUser[i] - smoothUser[i]) * k;
        smoothRef [i] += (targetRef [i] - smoothRef [i]) * k;
        peakHold[i] = juce::jmax (peakHold[i] * 0.996f, smoothUser[i]);
    }

    for (int i = 0; i < kNumBins; ++i)
    {
        prefixUser[i + 1] = prefixUser[i] + smoothUser[i];
        prefixRef [i + 1] = prefixRef [i] + smoothRef [i];
        prefixPeak[i + 1] = prefixPeak[i] + peakHold[i];
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
        g.drawText (juce::String::fromUTF8 ("drag move · scroll zoom · alt-drag stretch · double-click fit"),
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

    if (imageLoaded && imageVisible && imageBounds.contains (e.position))
        lastMouse = e.position;
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

    if (! imageLoaded || ! imageVisible) return;

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
}

void TelemetryCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (traceMode || ! imageLoaded || ! imageVisible) return;

    const float factor = (wheel.deltaY > 0.0f) ? 1.1f : 1.0f / 1.1f;
    const float cx = e.position.x, cy = e.position.y;
    const float w = imageBounds.getWidth(), h = imageBounds.getHeight();
    imageBounds.setBounds (cx - (cx - imageBounds.getX()) * factor,
                           cy - (cy - imageBounds.getY()) * factor,
                           w * factor, h * factor);
    repaint();
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

    if (imageLoaded) { fitImageToPlot(); repaint(); }
}

void TelemetryCanvas::mouseMove (const juce::MouseEvent& e)
{
    const bool over = imageLoaded && imageVisible && imageBounds.contains (e.position);
    if (over != mouseHover) { mouseHover = over; repaint(); }

    if (! traceMode)
        setMouseCursor (youReadoutRect().contains (e.position) ? juce::MouseCursor::PointingHandCursor
                                                               : juce::MouseCursor::NormalCursor);
}

void TelemetryCanvas::mouseExit (const juce::MouseEvent&)
{
    if (mouseHover) { mouseHover = false; repaint(); }
}

// ── Paint ───────────────────────────────────────────────────────────────────

void TelemetryCanvas::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    updateAxisRange();

    drawRefImage (g);   // screenshot ghost behind everything
    drawGrid (g);

    if (focus == FocusModel::Group::Master) drawMaster (g);
    else                                    drawFocused (g);

    drawCorrection (g);  // match-EQ curve
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

        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.18f), 0.0f, plotTop,
                                                 col.withAlpha (0.03f), 0.0f, plotBottom, false));
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

    if (hasUser)
    {
        const juce::Path curve = buildCurve (userSeries());
        juce::Path fill = curve;
        fill.lineTo (plotRight, plotBottom);
        fill.lineTo (plotLeft, plotBottom);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (col.saturated.withAlpha (0.16f), 0.0f, plotTop,
                                                 col.saturated.withAlpha (0.02f), 0.0f, plotBottom, false));
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
    if (correction.empty()) return;

    const float mid   = 0.5f * (plotTop + plotBottom);
    const float scale = 0.5f * (plotBottom - plotTop) / kCorrectionRangeDb;
    const auto  col   = correctionApplied ? FocusModel::matchColour() : JP::text.withAlpha (0.45f);

    // 0 dB line + scale on the right edge.
    g.setColour (col.withAlpha (0.25f));
    const float dash[2] = { 3.0f, 5.0f };
    g.drawDashedLine (juce::Line<float> (plotLeft, mid, plotRight, mid), dash, 2, 1.0f);

    g.setFont (uiFont (8.0f));
    for (float db : { kCorrectionRangeDb * 2.0f / 3.0f, 0.0f, -kCorrectionRangeDb * 2.0f / 3.0f })
    {
        const float y = mid - db * scale;
        g.setColour (col.withAlpha (0.6f));
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                    juce::Rectangle<float> (plotRight + 4.0f, y - 6.0f, 30.0f, 12.0f),
                    juce::Justification::left, false);
    }

    juce::Path p;
    bool started = false;
    const float hzPerBin = binHz();
    for (size_t i = 1; i < correction.size(); ++i)
    {
        const float hz = (float) i * hzPerBin;
        if (hz < axisMin || hz > axisMax) continue;
        const float x = xForFreq (hz);
        const float y = mid - juce::jlimit (-kCorrectionRangeDb, kCorrectionRangeDb, correction[i]) * scale;
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else           p.lineTo (x, y);
    }
    if (! started) return;

    juce::Path fill = p;
    fill.lineTo (p.getCurrentPosition().x, mid);
    fill.lineTo (p.getBounds().getX(), mid);
    fill.closeSubPath();
    g.setColour (col.withAlpha (correctionApplied ? 0.10f : 0.05f));
    g.fillPath (fill);

    g.setColour (col.withAlpha (correctionApplied ? 0.95f : 0.7f));
    g.strokePath (p, juce::PathStrokeType (correctionApplied ? 2.0f : 1.5f));
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
