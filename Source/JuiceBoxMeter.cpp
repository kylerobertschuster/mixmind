#include "JuiceBoxMeter.h"

JuiceBoxMeter::JuiceBoxMeter() { startTimerHz (30); }

void JuiceBoxMeter::timerCallback()
{
    const float k = 0.10f;
    aLufs   += (lufs   - aLufs)   * k;
    aPeak   += (peak   - aPeak)   * k;
    aCrest  += (crest  - aCrest)  * k;
    aPhase  += (phase  - aPhase)  * k;
    aStereo += (stereo - aStereo) * k;
    aBass   += (bassE  - aBass)   * k;
    aMid    += (midE   - aMid)    * k;
    aHigh   += (highE  - aHigh)   * k;
    repaint();
}

void JuiceBoxMeter::updateFromJson (const juce::String& json)
{
    auto v = [&](const juce::String& key, float def = 0) -> float {
        int p = json.indexOf ("\"" + key + "\":");
        if (p < 0) return def;
        p += key.length() + 3;
        int e = json.indexOf (p, ",");
        if (e < 0) e = json.indexOf (p, "}");
        return json.substring (p, e < 0 ? json.length() : e).getFloatValue();
    };
    lufs   = v ("integrated_lufs", -60);
    peak   = v ("true_peak_db", -60);
    crest  = v ("crest_factor");
    phase  = v ("phase_correlation", 1);
    stereo = v ("stereo_width", 0.5f);
    bassE  = v ("bass_energy");
    midE   = v ("mid_energy");
    highE  = v ("high_energy");
}

void JuiceBoxMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto pink = JP::accent();
    auto black = JP::text;

    // Juice box body — rounded rectangle
    g.setColour (pink.withAlpha (0.15f));
    g.fillRoundedRectangle (b.reduced (2), 10.0f);
    g.setColour (pink.withAlpha (0.25f));
    g.drawRoundedRectangle (b.reduced (2), 10.0f, 1.0f);

    // Top flap
    auto flap = juce::Rectangle<float> (b.getX() + 4, b.getY() + 2, b.getWidth() - 8, 14.0f);
    g.setColour (pink.withAlpha (0.20f));
    g.fillRoundedRectangle (flap, 6.0f);

    // Small straw at top right
    auto strawX = b.getRight() - 22;
    auto strawY = b.getY() - 4;
    g.setColour (pink.withAlpha (0.12f));
    g.fillRoundedRectangle (strawX, strawY, 8, 16, 4.0f);
    g.setColour (pink.withAlpha (0.08f));
    g.drawRoundedRectangle (strawX, strawY, 8, 16, 4.0f, 0.5f);

    if (collapsed)
    {
        g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::bold));
        g.setColour (pink.withAlpha (0.5f));
        g.drawText ("METERS", b, juce::Justification::centred, false);
        return;
    }

    // Title
    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::bold));
    g.setColour (pink.withAlpha (0.6f));
    g.drawText ("TELEMETRY", juce::Rectangle<float> (8, 6, b.getWidth() - 40, 14),
                juce::Justification::left, false);

    // Meter bars
    float mx = 8, mw = b.getWidth() - 16;
    float my = 28;
    float mh = 12;
    float gap = 4;

    drawMeterBar (g, mx, my, mw, mh, aLufs, -60, 0, "LUFS", "dB", aLufs > -10);
    my += mh + gap;
    drawMeterBar (g, mx, my, mw, mh, aPeak, -12, 0, "PEAK", "dBTP", aPeak > -1);
    my += mh + gap;
    drawMeterBar (g, mx, my, mw, mh, aCrest, 0, 20, "CREST", "dB", aCrest < 6);
    my += mh + gap;
    drawMeterBar (g, mx, my, mw, mh, (aPhase + 1) / 2, 0, 1, "PHASE", "corr", aPhase < 0.3f);
    my += mh + gap;
    drawMeterBar (g, mx, my, mw, mh, aStereo, 0, 1, "WIDTH", "M/S", false);
    my += mh + 4;

    // Spectral bars
    g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::bold));
    g.setColour (pink.withAlpha (0.5f));
    g.drawText ("SPECTRUM", juce::Rectangle<float> (mx, my, mw, 12), juce::Justification::left, false);
    my += 14;

    float barH = 6;
    drawMeterBar (g, mx, my, mw, barH, (aBass + 60) / 60, 0, 1, "BASS", "", false);
    my += barH + 3;
    drawMeterBar (g, mx, my, mw, barH, (aMid + 60) / 60, 0, 1, "MID", "", false);
    my += barH + 3;
    drawMeterBar (g, mx, my, mw, barH, (aHigh + 60) / 60, 0, 1, "HIGH", "", false);
}

void JuiceBoxMeter::drawMeterBar (juce::Graphics& g, float x, float y, float w, float h,
                                   float value, float minVal, float maxVal,
                                   const juce::String& label, const juce::String& unit,
                                   bool warn)
{
    auto pink = JP::accent();
    float norm = juce::jlimit (0.0f, 1.0f, (value - minVal) / (maxVal - minVal));
    float fillW = w * norm;

    // Track
    g.setColour (pink.withAlpha (0.08f));
    g.fillRoundedRectangle (x, y, w, h, 3.0f);

    // Fill
    g.setColour (warn ? JP::warning.withAlpha (0.7f) : pink.withAlpha (0.4f));
    g.fillRoundedRectangle (x, y, fillW, h, 3.0f);

    // Label
    g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::bold));
    g.setColour (pink.withAlpha (0.5f));
    g.drawText (label, juce::Rectangle<float> (x + 4, y, 44, h), juce::Justification::left, false);

    // Value
    juce::String valText = juce::String (value, 1) + (unit.isNotEmpty() ? " " + unit : "");
    g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::plain));
    g.setColour (warn ? JP::warning : pink.withAlpha (0.7f));
    g.drawText (valText, juce::Rectangle<float> (x + w - 70, y, 66, h), juce::Justification::right, false);
}

void JuiceBoxMeter::resized()
{
    if (collapsed)
        setSize (140, 100);
    else
        setSize (200, 280);
}
