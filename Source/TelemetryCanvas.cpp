#include "TelemetryCanvas.h"
#include <cmath>

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

    drawGrid (g);

    if (focus == FocusModel::Group::Master) drawMaster (g);
    else                                    drawFocused (g);

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
