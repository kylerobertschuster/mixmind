#include "AnalyzerCanvas.h"
#include <cmath>

AnalyzerCanvas::AnalyzerCanvas() { startTimerHz (60); }

void AnalyzerCanvas::timerCallback()
{
    const float k = 0.12f;
    for (int i = 0; i < kNumBins; ++i)
    {
        smoothBins[i] += (targetBins[i] - smoothBins[i]) * k;
        if (smoothBins[i] > peakHold[i]) peakHold[i] = smoothBins[i];
        else peakHold[i] = peakHold[i] * 0.998f;
    }
    repaint();
}

void AnalyzerCanvas::updateBins (const float* bins, int numBins)
{
    hasSignal = false;
    int step = juce::jmax (1, numBins / kNumBins);
    for (int i = 0; i < kNumBins; ++i)
    {
        int srcIdx = i * step;
        if (srcIdx < numBins)
        {
            float val = bins[srcIdx];
            // Convert raw magnitude to dB
            float db = val > 0.0001f ? 20.0f * std::log10 (val) : -120.0f;
            targetBins[i] = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
            if (db > -90.0f) hasSignal = true;
        }
    }
}

void AnalyzerCanvas::resized()
{
    plotRight  = (float)getWidth() - 12.0f;
    plotBottom = (float)getHeight() - 28.0f;
}

void AnalyzerCanvas::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    drawGrid (g);
    drawSpectrum (g);

    if (!hasSignal)
    {
        g.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::plain));
        g.setColour (JP::textDim.withAlpha (0.4f));
        g.drawText ("No signal — play audio through this track",
                    juce::Rectangle<float> (plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop),
                    juce::Justification::centred, false);
    }
}

void AnalyzerCanvas::drawGrid (juce::Graphics& g)
{
    g.setColour (JP::border.withAlpha (0.3f));
    float w = plotRight - plotLeft;
    float h = plotBottom - plotTop;

    for (int i = 0; i <= 6; ++i)
    {
        float y = plotTop + h * (float)i / 6.0f;
        g.drawLine (plotLeft, y, plotRight, y, 0.5f);
    }

    // Freq labels
    float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    g.setFont (juce::FontOptions ("Helvetica Neue", 7.0f, juce::Font::plain));
    g.setColour (JP::textDim);
    for (float hz : freqs)
    {
        float t = std::log10 (hz / 20.0f) / std::log10 (1000.0f);
        float x = plotLeft + w * t;
        juce::String label = hz >= 1000 ? juce::String (hz/1000,0) + "k" : juce::String ((int)hz);
        g.drawText (label, juce::Rectangle<float> (x-15, plotBottom+2, 30, 14),
                    juce::Justification::centred, false);
    }
}

void AnalyzerCanvas::drawSpectrum (juce::Graphics& g)
{
    if (!hasSignal) return;
    float w = plotRight - plotLeft;
    float h = plotBottom - plotTop;

    // Rainbow fill under curve
    juce::Path fillPath;
    fillPath.startNewSubPath (plotLeft, plotBottom);
    for (int i = 0; i < kNumBins; ++i)
    {
        float x = plotLeft + w * (float)i / (float)(kNumBins - 1);
        float y = plotBottom - h * smoothBins[i];
        fillPath.lineTo (x, y);
    }
    fillPath.lineTo (plotRight, plotBottom);
    fillPath.closeSubPath();

    g.setGradientFill (juce::ColourGradient (
        JP::pastelColor(0).withAlpha (0.12f), 0, plotTop,
        JP::pastelColor(0).withAlpha (0.02f), 0, plotBottom, false));
    g.fillPath (fillPath);

    // Rainbow spectrum — 8 colored segments
    int segSize = kNumBins / 8;
    for (int seg = 0; seg < 8; ++seg)
    {
        int start = seg * segSize;
        int end   = (seg == 7) ? kNumBins - 1 : (seg + 1) * segSize;
        auto col = JP::pastelColor(seg);

        juce::Path segPath;
        segPath.startNewSubPath (plotLeft + w * (float)start / (float)(kNumBins - 1),
                                  plotBottom - h * smoothBins[start]);
        for (int i = start + 1; i <= end; ++i)
        {
            float x = plotLeft + w * (float)i / (float)(kNumBins - 1);
            float y = plotBottom - h * smoothBins[i];
            segPath.lineTo (x, y);
        }
        g.setColour (col.withAlpha (0.75f));
        g.strokePath (segPath, juce::PathStrokeType (1.2f));
    }

    // Peak hold
    juce::Path peakPath;
    peakPath.startNewSubPath (plotLeft, plotBottom - h * peakHold[0]);
    for (int i = 1; i < kNumBins; ++i)
    {
        float x = plotLeft + w * (float)i / (float)(kNumBins - 1);
        float y = plotBottom - h * peakHold[i];
        peakPath.lineTo (x, y);
    }
    g.setColour (JP::pastelColor(0).withAlpha (0.10f));
    g.strokePath (peakPath, juce::PathStrokeType (0.8f));
}
