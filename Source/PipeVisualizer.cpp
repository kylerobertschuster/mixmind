#include "PipeVisualizer.h"

PipeVisualizer::PipeVisualizer() { startTimerHz (60); }

void PipeVisualizer::timerCallback()
{
    const float k = 0.10f;
    aBass   += (bass   - aBass)   * k;
    aMid    += (mid    - aMid)    * k;
    aHigh   += (high   - aHigh)   * k;
    aLufs   += (lufs   - aLufs)   * k;
    aStereo += (stereo - aStereo) * k;
    aCrest  += (crest  - aCrest)  * k;
    aPeak   += (peak   - aPeak)   * k;
    repaint();
}

void PipeVisualizer::updateFromJson (const juce::String& json)
{
    auto v = [&](const juce::String& key, float def = 0.0f) -> float
    {
        int p = json.indexOf ("\"" + key + "\":");
        if (p < 0) return def;
        p += key.length() + 3;
        int e = json.indexOf (p, ",");
        if (e < 0) e = json.indexOf (p, "}");
        if (e < 0) e = json.length();
        return json.substring (p, e).getFloatValue();
    };
    bass   = v ("bass_energy");
    mid    = v ("mid_energy");
    high   = v ("high_energy");
    lufs   = v ("integrated_lufs", -60.0f);
    stereo = v ("stereo_width", 0.5f);
    crest  = v ("crest_factor");
    peak   = v ("true_peak", -60.0f);
}

void PipeVisualizer::paint (juce::Graphics& g)
{
    drawPipeChannel (g);
    drawReadouts (g);
}

void PipeVisualizer::drawPipeChannel (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    pipeW = juce::jmin (18.0f, b.getWidth() * 0.5f);
    pipeX = (b.getWidth() - pipeW) / 2.0f;
    pipeY = 10.0f;
    pipeH = b.getHeight() - 20.0f;

    auto r = juce::Rectangle<float> (pipeX, pipeY, pipeW, pipeH);
    float cr = pipeW * 0.45f;

    // Outer glass casing — dark, semi-transparent
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0x12ffffff), pipeX, pipeY,
        juce::Colour (0x04ffffff), pipeX + pipeW, pipeY, false));
    juce::Path casing;
    casing.addRoundedRectangle (r, cr);
    g.fillPath (casing);

    // Edge glow — amethyst tint
    g.setColour (accent.withAlpha (0.10f));
    g.drawRoundedRectangle (r, cr, 0.5f);

    // Left edge glass refraction highlight
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0x1affffff), pipeX + 1.5f, 0,
        juce::Colour (0x00ffffff), pipeX + pipeW * 0.35f, 0, false));
    g.fillRoundedRectangle (pipeX + 1.5f, pipeY + 2.0f,
                            pipeW * 0.22f, pipeH - 4.0f, cr * 0.8f);

    // ═══════════════════════════════════════════════════════════════
    // Spectral liquid bands (stacked: bass / mid / high)
    // ═══════════════════════════════════════════════════════════════
    float pad = 2.0f;
    float bx = pipeX + pad;
    float bw = pipeW - pad * 2;
    float bh = (pipeH - pad * 2) / 3.0f;

    auto band = [&](float y, float val, juce::Colour col)
    {
        float lvl = juce::jlimit (0.0f, 1.0f, (val + 60.0f) / 60.0f);
        float fh = bh * lvl;
        auto fill = juce::Rectangle<float> (bx, y + bh - fh, bw, fh);

        // Gradient glow fill
        g.setGradientFill (juce::ColourGradient (
            col.withAlpha (0.65f), bx, y + bh,
            col.withAlpha (0.20f), bx, y + bh - fh, false));
        g.fillRoundedRectangle (fill, 2.0f);

        // Bright top edge
        if (fh > 1.5f)
        {
            g.setColour (col.withAlpha (0.85f));
            g.drawLine (fill.getX(), fill.getY(),
                        fill.getRight(), fill.getY(), 1.0f);
        }
    };

    float by = pipeY + pad;
    band (by,               aBass,  juce::Colour (0xff7c3aed)); // deep violet
    band (by + bh,          aMid,   juce::Colour (0xffa78bfa)); // lighter violet
    band (by + bh * 2.0f,   aHigh,  accent);                   // amethyst

    // Band separator lines
    g.setColour (juce::Colour (0x0affffff));
    g.drawLine (bx, by + bh, bx + bw, by + bh, 0.5f);
    g.drawLine (bx, by + bh * 2.0f, bx + bw, by + bh * 2.0f, 0.5f);

    // Band labels inside
    g.setFont (juce::FontOptions ("SF Pro Display", 7.0f, juce::Font::bold));
    g.setColour (JP::textDim);
    g.drawText ("LOW",  juce::Rectangle<float> (bx, by, bw, 9.0f),
                juce::Justification::centred, false);
    g.drawText ("MID",  juce::Rectangle<float> (bx, by + bh, bw, 9.0f),
                juce::Justification::centred, false);
    g.drawText ("HI",   juce::Rectangle<float> (bx, by + bh * 2.0f, bw, 9.0f),
                juce::Justification::centred, false);
}

void PipeVisualizer::drawReadouts (juce::Graphics& g)
{
    auto b = getLocalBounds();
    float rightEdge = (float)b.getWidth();
    float x = rightEdge + 4.0f; // readouts to the right of the pipe
    float y = pipeY + 4.0f;

    g.setFont (juce::FontOptions ("SF Pro Display", 8.5f, juce::Font::bold));
    g.setColour (JP::textMuted);

    // LUFS is the key readout — large and prominent
    g.setFont (juce::FontOptions ("SF Mono", 10.0f, juce::Font::bold));
    juce::String lufsText = juce::String (aLufs, 1) + " LUFS";
    g.setColour (aLufs > -14.0f ? JP::warning :
                 aLufs > -20.0f ? JP::accent() : JP::textMuted);
    g.drawSingleLineText (lufsText, 4, (int)pipeY + 10);

    // Stereo width
    g.setFont (juce::FontOptions ("SF Mono", 7.5f, juce::Font::plain));
    g.setColour (JP::textDim);
    juce::String swText = juce::String ((int)(aStereo * 100)) + "% W";
    g.drawSingleLineText (swText, 4, (int)pipeY + 24);

    if (advancedMode)
    {
        g.setFont (juce::FontOptions ("SF Mono", 7.5f, juce::Font::plain));

        // True peak
        juce::String tpText = juce::String (aPeak, 1) + " dBTP";
        g.setColour (aPeak > -1.0f ? JP::error : JP::textDim);
        g.drawSingleLineText (tpText, 4, (int)pipeY + 36);

        // Crest factor
        juce::String cfText = juce::String (aCrest, 1) + " CF";
        g.setColour (JP::textDim);
        g.drawSingleLineText (cfText, 4, (int)pipeY + 48);

        // Bass energy exact
        juce::String beText = juce::String (aBass, 1) + " dB";
        g.drawSingleLineText (beText, 4, (int)pipeY + 60);

        // Mid energy exact
        juce::String meText = juce::String (aMid, 1) + " dB";
        g.drawSingleLineText (meText, 4, (int)pipeY + 72);

        // High energy exact
        juce::String heText = juce::String (aHigh, 1) + " dB";
        g.drawSingleLineText (heText, 4, (int)pipeY + 84);
    }
}

void PipeVisualizer::resized() {}
