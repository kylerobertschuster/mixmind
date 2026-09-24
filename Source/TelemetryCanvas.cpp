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
    plotRight  = (float) getWidth() - 12.0f;
    plotBottom = (float) getHeight() - 44.0f;   // room for freq labels + group meter
}

void TelemetryCanvas::setUserBins (const float* bins, int n, double sr)
{
    sampleRate = sr;
    hasUser = false;
    const int count = juce::jmin (n, kNumBins);
    for (int i = 0; i < count; ++i)
    {
        const float db = bins[i] > 0.0001f ? 20.0f * std::log10 (bins[i]) : -120.0f;
        targetUser[i] = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
        if (db > -90.0f) hasUser = true;
    }
}

void TelemetryCanvas::setReference (const float* bins, int n)
{
    if (bins == nullptr) { hasRef = false; return; }

    hasRef = false;
    const int count = juce::jmin (n, kNumBins);
    for (int i = 0; i < count; ++i)
    {
        const float db = bins[i] > 0.0001f ? 20.0f * std::log10 (bins[i]) : -120.0f;
        targetRef[i] = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
        if (db > -90.0f) hasRef = true;
    }
}

void TelemetryCanvas::setUserScalars (float lufs, float width, float phase, float crest)
{
    userLufs = lufs; userWidth = width; userPhase = phase; userCrest = crest;
}

void TelemetryCanvas::setRefScalars (float lufs, float width, float phase, float crest)
{
    refLufs = lufs; refWidth = width; refPhase = phase; refCrest = crest;
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
    repaint();
}

// ── Coordinate mapping ──────────────────────────────────────────────────────

float TelemetryCanvas::xForFreq (float hz) const
{
    const float lo = std::log10 (axisMin);
    const float hi = std::log10 (axisMax);
    const float t  = (std::log10 (juce::jlimit (axisMin, axisMax, hz)) - lo) / (hi - lo);
    return plotLeft + (plotRight - plotLeft) * t;
}

float TelemetryCanvas::yForValue (float v) const
{
    return plotBottom - (plotBottom - plotTop) * juce::jlimit (0.0f, 1.0f, v);
}

juce::Path TelemetryCanvas::buildCurve (const float* bins) const
{
    juce::Path p;
    bool started = false;
    for (int i = 0; i < kNumBins; ++i)
    {
        const float hz = (float) i * (float) sampleRate / (float) AudioAnalyzer::fftSize;
        if (hz < axisMin || hz > axisMax) continue;
        const float x = xForFreq (hz);
        const float y = yForValue (bins[i]);
        if (!started) { p.startNewSubPath (x, y); started = true; }
        else          p.lineTo (x, y);
    }
    return p;
}

juce::Path TelemetryCanvas::buildBandCurve (const float* bins, float loHz, float hiHz) const
{
    juce::Path p;
    bool started = false;
    for (int i = 0; i < kNumBins; ++i)
    {
        const float hz = (float) i * (float) sampleRate / (float) AudioAnalyzer::fftSize;
        if (hz < loHz || hz > hiHz) continue;
        const float x = xForFreq (hz);
        const float y = yForValue (bins[i]);
        if (!started) { p.startNewSubPath (x, y); started = true; }
        else          p.lineTo (x, y);
    }
    return p;
}

// ── Reference layer (image ghost) + trace ──────────────────────────────────

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

void TelemetryCanvas::setTraceMode (bool on)
{
    traceMode = on;
    setMouseCursor (on ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void TelemetryCanvas::setTraceCurve (const MixMindState::TraceCurve& curve)
{
    traceCurve = curve;
    dragIndex = -1;   // a restored curve replaces whatever was being dragged

    std::sort (traceCurve.begin(), traceCurve.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    repaint();
}
// ── Trace point ↔ pixel mapping ─────────────────────────────────────────────
// Control points are stored as (freqHz, value01), so they keep their musical
// meaning when the plot re-zooms to a focus band; pixels are recomputed each
// paint and only used for hit-testing + drawing.

juce::Point<float> TelemetryCanvas::pointToPixel (float freq, float value) const
{
    return { xForFreq (freq), yForValue (value) };
}

std::pair<float, float> TelemetryCanvas::pixelToPoint (juce::Point<float> p) const
{
    return { freqForX (p.x), valueForY (p.y) };
}

float TelemetryCanvas::freqForX (float x) const
{
    const float span = plotRight - plotLeft;
    if (span <= 0.0f) return axisMin;
    const float t = juce::jlimit (0.0f, 1.0f, (x - plotLeft) / span);
    return axisMin * std::pow (axisMax / axisMin, t);
}

float TelemetryCanvas::valueForY (float y) const
{
    const float span = plotBottom - plotTop;
    if (span <= 0.0f) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (plotBottom - y) / span);
}

int TelemetryCanvas::hitTestPoint (juce::Point<float> p) const
{
    constexpr float radius = 9.0f;
    int best = -1;
    float bestDist = radius;

    for (int i = 0; i < traceCurve.size(); ++i)
    {
        const auto px = pointToPixel (traceCurve.getReference (i).first,
                                      traceCurve.getReference (i).second);
        const float d = px.getDistanceFrom (p);
        if (d <= bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void TelemetryCanvas::sortTracePoints()
{
    std::sort (traceCurve.begin(), traceCurve.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });
}

// The spline itself lives in MixMindState so the shaper can densify a recalled
// curve with no editor open: what the plot draws and what the FIR matches are
// then the same samples, by construction.
juce::Array<std::pair<float, float>> TelemetryCanvas::sampleSpline() const
{
    return MixMindState::densifyTrace (traceCurve);
}

void TelemetryCanvas::fitImageToPlot()
{
    imageBounds = plotRect();
}

juce::Path TelemetryCanvas::smoothTrace() const
{
    juce::Path p;
    // Same samples the shaper sees, drawn as a polyline (they're dense enough
    // that the curve reads as smooth).
    const auto pts = sampleSpline();

    if (pts.isEmpty())
    {
        if (traceCurve.size() == 1)
        {
            const auto& pt = traceCurve.getReference (0);
            p.startNewSubPath (pointToPixel (pt.first, pt.second));
        }
        return p;
    }

    for (int i = 0; i < pts.size(); ++i)
    {
        const auto& pt = pts.getReference (i);
        const auto px = pointToPixel (pt.first, pt.second);
        if (i == 0) p.startNewSubPath (px);
        else        p.lineTo (px);
    }
    return p;
}

void TelemetryCanvas::drawRefImage (juce::Graphics& g)
{
    if (!imageLoaded || !imageVisible) return;

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

    // Interaction hint.
    g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::plain));
    g.setColour (JP::textDim.withAlpha (0.45f));
    g.drawText ("drag move · scroll zoom · ⌥drag stretch · double-click fit",
                juce::Rectangle<float> (plotLeft + 4.0f, plotBottom - 16.0f, plotRight - plotLeft - 8.0f, 14.0f),
                juce::Justification::left, false);
}

void TelemetryCanvas::drawTrace (juce::Graphics& g)
{
    if (traceCurve.isEmpty()) return;

    const auto plot = plotRect();
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    for (const auto& pt : traceCurve)
    {
        // Only the handles actually inside the current zoom are drawn; the
        // curve itself clamps to the edges (see pointToPixel/xForFreq).
        const auto px = pointToPixel (pt.first, pt.second);
        if (plot.contains (px))
            g.fillEllipse (px.x - 3.0f, px.y - 3.0f, 6.0f, 6.0f);
    }

    if (traceCurve.size() >= 2)
    {
        juce::Path dashed;
        const float dash[2] = { 6.0f, 4.0f };
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createDashedStroke (dashed, smoothTrace(), dash, 2);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillPath (dashed);
    }
}

// ── Mouse interaction (image layer / trace) ────────────────────────────────

void TelemetryCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (traceMode)
    {
        if (e.mods.isRightButtonDown())
        {
            // Right-click removes a handle: the one under the cursor, or the
            // last stored point when the click misses them all.
            const int hit = hitTestPoint (e.position);
            if (hit >= 0)                                     traceCurve.remove (hit);
            else if (! traceCurve.isEmpty())                  traceCurve.removeLast();
        }
        else if (plotRect().contains (e.position))
        {
            dragIndex = hitTestPoint (e.position);   // grab an existing handle…
            if (dragIndex < 0)                       // …otherwise drop a new one
            {
                traceCurve.add (pixelToPoint (e.position));
                dragIndex = traceCurve.size() - 1;
            }
        }
        repaint();
        return;
    }

    if (imageLoaded && imageVisible && imageBounds.contains (e.position))
        lastMouse = e.position;
}

void TelemetryCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (traceMode)
    {
        if (dragIndex >= 0 && dragIndex < traceCurve.size())
        {
            traceCurve.setUnchecked (dragIndex, pixelToPoint (e.position));
            repaint();
        }
        return;
    }

    if (!imageLoaded || !imageVisible) return;

    const float dx = e.position.x - lastMouse.x;
    const float dy = e.position.y - lastMouse.y;

    if (e.mods.isAltDown())
    {
        imageBounds.setSize (juce::jmax (8.0f, imageBounds.getWidth()  + dx),
                             juce::jmax (8.0f, imageBounds.getHeight() + dy));
    }
    else
    {
        imageBounds.translate (dx, dy);
    }
    lastMouse = e.position;
    repaint();
}

void TelemetryCanvas::mouseUp (const juce::MouseEvent&)
{
    if (! traceMode || dragIndex < 0) return;

    // Re-sort once the drag ends, not during it — indices must stay stable
    // while a handle is being moved past its neighbours.
    sortTracePoints();
    dragIndex = -1;
    repaint();
}

void TelemetryCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (traceMode || !imageLoaded || !imageVisible) return;

    const float factor = (wheel.deltaY > 0.0f) ? 1.1f : 1.0f / 1.1f;
    const float cx = e.position.x, cy = e.position.y;
    const float w = imageBounds.getWidth(), h = imageBounds.getHeight();
    imageBounds.setBounds (cx - (cx - imageBounds.getX()) * factor,
                           cy - (cy - imageBounds.getY()) * factor,
                           w * factor, h * factor);
    repaint();
}

void TelemetryCanvas::mouseDoubleClick (const juce::MouseEvent&)
{
    if (!traceMode && imageLoaded) { fitImageToPlot(); repaint(); }
}

void TelemetryCanvas::mouseMove (const juce::MouseEvent& e)
{
    const bool over = imageLoaded && imageVisible && imageBounds.contains (e.position);
    if (over != mouseHover) { mouseHover = over; repaint(); }
}

void TelemetryCanvas::mouseExit (const juce::MouseEvent&)
{
    if (mouseHover) { mouseHover = false; repaint(); }
}

// ── Paint ───────────────────────────────────────────────────────────────────

void TelemetryCanvas::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);

    if (focus == FocusModel::Group::Master) { axisMin = 20.0f; axisMax = 20000.0f; }
    else
    {
        const auto r = FocusModel::bandRange (focus);
        const float pad = r.getLength() * 0.08f;
        axisMin = juce::jmax (20.0f, r.getStart() - pad);
        axisMax = juce::jmin (20000.0f, r.getEnd() + pad);
    }

    drawRefImage (g);   // screenshot ghost behind everything
    drawGrid (g);

    if (focus == FocusModel::Group::Master) drawMaster (g);
    else                                    drawFocused (g);

    drawTrace (g);       // hand-drawn target curve on top
    drawReadout (g);
    drawGroupMeter (g);
}

void TelemetryCanvas::drawGrid (juce::Graphics& g)
{
    g.setColour (JP::border.withAlpha (0.3f));
    for (int i = 0; i <= 6; ++i)
    {
        const float y = plotTop + (plotBottom - plotTop) * (float) i / 6.0f;
        g.drawLine (plotLeft, y, plotRight, y, 0.5f);
    }

    static const float ticks[] = { 20,30,40,50,60,80,100,150,200,300,400,500,700,
                                   1000,1500,2000,3000,4000,5000,7000,10000,15000,20000 };

    g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::plain));
    g.setColour (JP::textDim);
    for (float hz : ticks)
    {
        if (hz < axisMin || hz > axisMax) continue;
        const float x = xForFreq (hz);
        g.drawLine (x, plotBottom, x, plotBottom + 3.0f, 1.0f);

        juce::String lbl;
        if (hz >= 1000.0f)
        {
            const float k = hz / 1000.0f;
            lbl = (k == (int) k) ? juce::String ((int) k) + "k" : juce::String (k, 1) + "k";
        }
        else lbl = juce::String ((int) hz);

        g.drawText (lbl, juce::Rectangle<float> (x - 16.0f, plotBottom + 4.0f, 32.0f, 14.0f),
                    juce::Justification::centred, false);
    }
}

void TelemetryCanvas::drawMaster (juce::Graphics& g)
{
    if (!hasUser) { drawHint (g, "No signal — play audio through this track"); return; }

    // User spectrum coloured per focus-group band: each region of the graph
    // takes its group's saturated hue (bass peak = blue, vocals = violet, …).
    for (auto grp : FocusModel::defaultGroups())
    {
        const auto range = FocusModel::bandRange (grp);
        const auto col   = FocusModel::colorFor (grp).saturated;

        const juce::Path curve = buildBandCurve (smoothUser, range.getStart(), range.getEnd());

        juce::Path fill = curve;
        fill.lineTo (xForFreq (range.getEnd()),   plotBottom);
        fill.lineTo (xForFreq (range.getStart()), plotBottom);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.18f), 0.0f, plotTop,
                                                 col.withAlpha (0.03f), 0.0f, plotBottom, false));
        g.fillPath (fill);

        g.setColour (col.withAlpha (0.95f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    // Peak-hold envelope (subtle) — recent maximum per bin.
    g.setColour (juce::Colours::white.withAlpha (0.20f));
    g.strokePath (buildCurve (peakHold), juce::PathStrokeType (1.0f));

    // Reference overlay (pastel pink) if loaded.
    if (hasRef)
    {
        const juce::Path rc = buildCurve (smoothRef);
        g.setColour (FocusModel::masterReferenceColour().withAlpha (0.9f));
        g.strokePath (rc, juce::PathStrokeType (2.0f));
    }

    drawMasterLegend (g);
}

void TelemetryCanvas::drawFocused (juce::Graphics& g)
{
    const auto col = FocusModel::colorFor (focus);

    if (!hasUser && !hasRef)
    {
        drawHint (g, "No signal — play audio through this track");
        return;
    }

    // Reference (pastel) sits behind, user (saturated) on top.
    if (hasRef)
    {
        const juce::Path rc = buildCurve (smoothRef);
        g.setColour (col.pastel.withAlpha (0.85f));
        g.strokePath (rc, juce::PathStrokeType (2.0f));
    }

    if (hasUser)
    {
        const juce::Path curve = buildCurve (smoothUser);
        juce::Path fill = curve;
        fill.lineTo (plotRight, plotBottom);
        fill.lineTo (plotLeft, plotBottom);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (col.saturated.withAlpha (0.16f), 0.0f, plotTop,
                                                 col.saturated.withAlpha (0.02f), 0.0f, plotBottom, false));
        g.fillPath (fill);
        g.setColour (col.saturated.withAlpha (0.95f));
        g.strokePath (curve, juce::PathStrokeType (2.2f));

        // Peak-hold envelope (subtle) — recent maximum per bin.
        g.setColour (col.saturated.withAlpha (0.26f));
        g.strokePath (buildCurve (peakHold), juce::PathStrokeType (1.0f));
    }

    if (!hasRef)
        drawHint (g, "No reference — click LOAD REF to compare");

    drawLegend (g);
}

// ── Legends + hints ─────────────────────────────────────────────────────────

void TelemetryCanvas::drawLegend (juce::Graphics& g)
{
    const auto col = FocusModel::colorFor (focus);
    float x = plotLeft;
    const float y = 6.0f;

    g.setColour (col.pastel);
    g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
    g.setColour (JP::textDim);
    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));
    g.drawText ("REF", juce::Rectangle<float> (x + 16.0f, y, 40.0f, 12.0f), juce::Justification::left, false);

    x += 58.0f;
    g.setColour (col.saturated);
    g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
    g.setColour (JP::textDim);
    g.drawText ("YOU", juce::Rectangle<float> (x + 16.0f, y, 40.0f, 12.0f), juce::Justification::left, false);

    if (hasTrace())
    {
        x += 58.0f;
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
        g.setColour (JP::textDim);
        g.drawText ("TRACE", juce::Rectangle<float> (x + 16.0f, y, 52.0f, 12.0f), juce::Justification::left, false);
    }

    g.setColour (JP::text);
    g.setFont (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::bold));
    g.drawText (FocusModel::groupName (focus),
                juce::Rectangle<float> (plotRight - 120.0f, y, 120.0f, 12.0f),
                juce::Justification::right, false);
}

void TelemetryCanvas::drawMasterLegend (juce::Graphics& g)
{
    const float y = 6.0f;
    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));

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

    // REF = pink overlay.
    g.setColour (FocusModel::masterReferenceColour());
    g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
    g.setColour (JP::textDim);
    g.drawText ("REF", juce::Rectangle<float> (x + 16.0f, y, 40.0f, 12.0f), juce::Justification::left, false);

    if (hasTrace())
    {
        x += 58.0f;
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
        g.setColour (JP::textDim);
        g.drawText ("TRACE", juce::Rectangle<float> (x + 16.0f, y, 52.0f, 12.0f), juce::Justification::left, false);
    }

    g.setColour (JP::text);
    g.setFont (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::bold));
    g.drawText ("Master", juce::Rectangle<float> (plotRight - 120.0f, y, 120.0f, 12.0f),
                juce::Justification::right, false);
}

void TelemetryCanvas::drawHint (juce::Graphics& g, const juce::String& text)
{
    g.setFont (juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::plain));
    g.setColour (JP::textDim.withAlpha (0.5f));
    g.drawText (text,
                juce::Rectangle<float> (plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop),
                juce::Justification::centred, false);
}

// ── Scalar readout (LUFS / width / phase) ──────────────────────────────────

void TelemetryCanvas::drawReadout (juce::Graphics& g)
{
    const bool master = (focus == FocusModel::Group::Master);
    const juce::Colour refCol = master ? FocusModel::masterReferenceColour()
                                       : FocusModel::colorFor (focus).pastel;
    const juce::Colour youCol = master ? juce::Colour (0xffeeeeee)
                                       : FocusModel::colorFor (focus).saturated;

    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));

    const auto fmtLufs  = [] (float v) { return juce::String (v, 1) + " LU"; };
    const auto fmtWidth = [] (float v) { return juce::String (v, 2) + " WID"; };
    const auto fmtPhase = [] (float v)
    {
        juce::String s = (v >= 0.0f ? "+" : "") + juce::String (v, 2);
        return s + " PH";
    };
    const auto fmtCrest = [] (float v) { return juce::String (v, 1) + " CR"; };

    const auto row = [&] (float y, const juce::String& who, juce::Colour whoCol,
                          float l, float w, float p, float c)
    {
        g.setColour (whoCol);
        g.drawText (who, juce::Rectangle<float> (plotLeft, y, 34.0f, 12.0f),
                    juce::Justification::left, false);

        g.setColour (JP::textDim);
        float x = plotLeft + 34.0f;
        const float w1 = 66.0f, w2 = 64.0f, w3 = 64.0f, w4 = 54.0f;
        g.drawText (fmtLufs  (l), juce::Rectangle<float> (x, y, w1, 12.0f), juce::Justification::left, false);
        x += w1;
        g.drawText (fmtWidth (w), juce::Rectangle<float> (x, y, w2, 12.0f), juce::Justification::left, false);
        x += w2;
        g.drawText (fmtPhase (p), juce::Rectangle<float> (x, y, w3, 12.0f), juce::Justification::left, false);
        x += w3;
        g.drawText (fmtCrest (c), juce::Rectangle<float> (x, y, w4, 12.0f), juce::Justification::left, false);
    };

    if (hasRef) row (20.0f, "REF", refCol, refLufs, refWidth, refPhase, refCrest);
    row (33.0f, "YOU", youCol, userLufs, userWidth, userPhase, userCrest);
}

// ── Per-focus-group energy meter ───────────────────────────────────────────

float TelemetryCanvas::groupEnergy (FocusModel::Group g, const float* bins) const
{
    const auto range = FocusModel::bandRange (g);
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < kNumBins; ++i)
    {
        const float hz = (float) i * (float) sampleRate / (float) AudioAnalyzer::fftSize;
        if (hz >= range.getStart() && hz < range.getEnd())
        {
            sum += bins[i];
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    const float avg = sum / (float) count;
    return juce::jlimit (0.0f, 1.0f, avg * 2.5f);
}

void TelemetryCanvas::drawGroupMeter (juce::Graphics& g)
{
    const auto groups = FocusModel::defaultGroups();
    const int n = groups.size();
    const float y = (float) getHeight() - 16.0f;
    const float h = 10.0f;
    const float gap = 4.0f;
    const float segW = ((plotRight - plotLeft) - gap * (n - 1)) / (float) n;
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
