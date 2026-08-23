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
    generateSpots();
    startTimerHz (60);
}

TelemetryCanvas::~TelemetryCanvas()
{
    stopTimer();
}

void TelemetryCanvas::resized()
{
    plotRight  = (float) getWidth() - 12.0f;
    plotBottom = (float) getHeight() - 26.0f;
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

void TelemetryCanvas::timerCallback()
{
    const float k = 0.12f;
    for (int i = 0; i < kNumBins; ++i)
    {
        smoothUser[i] += (targetUser[i] - smoothUser[i]) * k;
        smoothRef [i] += (targetRef [i] - smoothRef [i]) * k;
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

float TelemetryCanvas::curveYAt (float nx, const float* bins) const
{
    const float hz = axisMin * std::pow (axisMax / axisMin, nx);
    int bin = (int) (hz / (float) sampleRate * (float) AudioAnalyzer::fftSize);
    bin = juce::jlimit (0, kNumBins - 1, bin);
    return yForValue (bins[bin]);
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

    const juce::Path curve = buildCurve (smoothUser);
    juce::Path fill = curve;
    fill.lineTo (plotRight, plotBottom);
    fill.lineTo (plotLeft, plotBottom);
    fill.closeSubPath();

    // White base (the "hide").
    g.setColour (juce::Colour (0xfff6f2ec));
    g.fillPath (fill);

    // Black blotches, clipped inside the spectrum area.
    g.saveState();
    g.reduceClipRegion (fill);
    for (const auto& s : spots)
    {
        const float cx = plotLeft + (plotRight - plotLeft) * s.nx;
        const float curveY = curveYAt (s.nx, smoothUser);
        const float cy = curveY + (plotBottom - curveY) * s.ny;
        const juce::Path blob = buildBlob (cx, cy, s.r, s.seed);
        g.setColour (juce::Colour (0xff17171a).withAlpha (0.92f));
        g.fillPath (blob);
    }
    g.restoreState();

    // Curve outline for legibility.
    g.setColour (juce::Colour (0xff17171a).withAlpha (0.85f));
    g.strokePath (curve, juce::PathStrokeType (1.6f));

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

    // Cow-print swatch (YOU).
    float x = plotLeft;
    g.setColour (juce::Colour (0xfff6f2ec));
    g.fillRoundedRectangle (x, y, 12.0f, 10.0f, 2.0f);
    g.setColour (juce::Colour (0xff17171a));
    g.fillEllipse (x + 3.0f,  y + 2.0f, 3.0f, 3.0f);
    g.fillEllipse (x + 7.0f,  y + 5.0f, 2.5f, 2.5f);
    g.fillEllipse (x + 6.0f,  y + 1.5f, 2.0f, 2.0f);
    g.setColour (JP::textDim);
    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));
    g.drawText ("YOU", juce::Rectangle<float> (x + 16.0f, y, 40.0f, 12.0f), juce::Justification::left, false);

    // Reference swatch (pink).
    x += 58.0f;
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

// ── Cow-print blotches ──────────────────────────────────────────────────────

void TelemetryCanvas::generateSpots()
{
    uint32_t s = 0x1234abcd;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) & 0xffff; };

    spots.clear();
    for (int i = 0; i < 22; ++i)
    {
        CowSpot spot;
        spot.nx   = rnd() / 65536.0f;
        spot.ny   = 0.15f + 0.8f * (rnd() / 65536.0f);
        spot.r    = 5.0f + 20.0f * (rnd() / 65536.0f);
        spot.seed = (int) rnd();
        spots.push_back (spot);
    }
}

juce::Path TelemetryCanvas::buildBlob (float cx, float cy, float r, int seed) const
{
    constexpr int N = 26;
    const float a0 = (float)(seed % 360) * (float) (juce::MathConstants<float>::pi / 180.0);
    const float a1 = (float)((seed >> 3) % 360) * (float) (juce::MathConstants<float>::pi / 180.0);

    juce::Path p;
    p.startNewSubPath (cx + r, cy);
    for (int i = 1; i <= N; ++i)
    {
        const float ang = juce::MathConstants<float>::twoPi * (float) i / (float) N;
        const float wob = 0.78f + 0.28f * std::sin (3.0f * ang + a0) * std::sin (2.0f * ang + a1);
        const float rr = r * wob;
        p.lineTo (cx + rr * std::cos (ang), cy + rr * std::sin (ang));
    }
    p.closeSubPath();
    return p;
}
