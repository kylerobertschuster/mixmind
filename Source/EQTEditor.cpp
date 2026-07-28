#include "EQTEditor.h"
#include <cmath>

EQTEditor::EQTEditor (EQTProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    JP::setTheme(juce::Colour(0xff22d3ee));
    setSize (800, 500);
    setResizable (true, true);
    setResizeLimits (500, 300, 1400, 900);

    titleLabel.setText ("JuicePipe - EQT", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.5f));
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    bypassButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    bypassButton.setColour (juce::TextButton::textColourOffId, JP::text);
    bypassButton.onClick = [this] {
        if (proc.isActive()) { proc.clearBands(); bypassButton.setButtonText ("BYPASSED"); }
        else { applyBands(); bypassButton.setButtonText ("BYPASS"); }
    };
    addAndMakeVisible (bypassButton);

    clearButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    clearButton.setColour (juce::TextButton::textColourOffId, JP::text);
    clearButton.onClick = [this] { bands.clear(); proc.clearBands(); bypassButton.setButtonText ("BYPASS"); repaint(); };
    addAndMakeVisible (clearButton);

    startTimerHz (60);
    if (!proc.licenseManager.isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

void EQTEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth(), h = (float)getHeight();

    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    drawSpectrum (g);
    drawEQCurve (g);
    drawEQBands (g);

    if (!hasSignal)
    {
        g.setFont (juce::FontOptions ("Helvetica Neue", 14.0f, juce::Font::plain));
        g.setColour (JP::textDim.withAlpha (0.4f));
        g.drawText ("No signal — play audio through this track",
                    juce::Rectangle<float> (plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop),
                    juce::Justification::centred, false);
    }
}

void EQTEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (260));
    clearButton.setBounds (header.withLeft (getWidth() - 290).withWidth (60).withHeight (26).withY (7));
    bypassButton.setBounds (header.withLeft (getWidth() - 220).withWidth (80).withHeight (26).withY (7));
    licenseButton.setBounds (header.withLeft (getWidth() - 130).withWidth (110).withHeight (26).withY (7));

    plotRight  = (float)getWidth() - 12.0f;
    plotBottom = (float)getHeight() - 22.0f;
}

void EQTEditor::timerCallback()
{
    ++dotPhase;
    const float* bins = proc.audioAnalyzer.getFFTBins();
    int nBins = AudioAnalyzer::numBins;
    int step = juce::jmax (1, nBins / kNumBins);
    hasSignal = false;
    for (int i = 0; i < kNumBins; ++i)
    {
        int si = i * step;
        if (si < nBins)
        {
            float db = bins[si] > 0.0001f ? 20.0f * std::log10(bins[si]) : -120.0f;
            float target = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
            smoothBins[i] += (target - smoothBins[i]) * 0.12f;
            if (smoothBins[i] > peakHold[i]) peakHold[i] = smoothBins[i];
            else peakHold[i] *= 0.998f;
            if (db > -90.0f) hasSignal = true;
        }
    }
    repaint();
}

void EQTEditor::drawSpectrum (juce::Graphics& g)
{
    if (!hasSignal) return;
    auto pink = JP::accent;
    float w = plotRight - plotLeft, h = plotBottom - plotTop;

    juce::Path curve;
    curve.startNewSubPath (plotLeft, plotBottom - h * smoothBins[0]);
    for (int i = 1; i < kNumBins; ++i)
    {
        float x = plotLeft + w * (float)i / (float)(kNumBins-1);
        curve.lineTo (x, plotBottom - h * smoothBins[i]);
    }

    juce::Path fill = curve;
    fill.lineTo (plotRight, plotBottom);
    fill.lineTo (plotLeft, plotBottom);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (pink.withAlpha(0.12f),0,plotTop,pink.withAlpha(0.02f),0,plotBottom,false));
    g.fillPath (fill);

    g.setColour (pink.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (2.5f));
    g.setColour (pink.withAlpha (0.85f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));

    // Peak hold
    juce::Path pk;
    pk.startNewSubPath (plotLeft, plotBottom - h * peakHold[0]);
    for (int i = 1; i < kNumBins; ++i)
        pk.lineTo (plotLeft + w*(float)i/(float)(kNumBins-1), plotBottom - h*peakHold[i]);
    g.setColour (pink.withAlpha (0.15f));
    g.strokePath (pk, juce::PathStrokeType (0.8f));

    // Grid + freq labels
    g.setFont (juce::FontOptions ("Helvetica Neue", 7.0f, juce::Font::plain));
    g.setColour (JP::border.withAlpha (0.3f));
    for (int i = 0; i <= 6; ++i)
        g.drawLine (plotLeft, plotTop + h*i/6, plotRight, plotTop + h*i/6, 0.5f);

    g.setColour (JP::textDim);
    float freqs[] = {20,50,100,200,500,1000,2000,5000,10000,20000};
    for (float fq : freqs)
    {
        float t = std::log10(fq/20.0f) / std::log10(1000.0f);
        float x = plotLeft + w * t;
        g.drawText (fq>=1000 ? juce::String(fq/1000,0)+"k" : juce::String((int)fq),
                    juce::Rectangle<float>(x-15,plotBottom+2,30,14), juce::Justification::centred,false);
    }
}

void EQTEditor::drawEQCurve (juce::Graphics& g)
{
    if (bands.empty()) return;
    float w = plotRight - plotLeft, h = plotBottom - plotTop;
    juce::Path curve;
    bool started = false;

    for (int i = 0; i <= 500; ++i)
    {
        float norm = (float)i / 500.0f;
        float freq = 20.0f * std::pow (1000.0f, norm);
        float x = plotLeft + w * norm;
        float totalGain = 0;
        for (auto& b : bands)
            if (b.active) {
                float oct = std::abs(std::log2(freq / b.freq));
                totalGain += b.gain * std::exp(-oct*oct / 0.5f);
            }
        float y = plotBottom - h * ((totalGain + 18.0f) / 36.0f);
        y = juce::jlimit (plotTop, plotBottom, y);
        if (!started) { curve.startNewSubPath (x, y); started = true; }
        else curve.lineTo (x, y);
    }
    g.setColour (JP::accent.withAlpha (0.3f));
    g.strokePath (curve, juce::PathStrokeType (1.5f));
}

void EQTEditor::drawEQBands (juce::Graphics& g)
{
    auto pink = JP::accent;
    float w = plotRight - plotLeft, h = plotBottom - plotTop;
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& b = bands[i];
        if (!b.active) continue;
        float t = std::log10(b.freq/20.0f) / std::log10(1000.0f);
        float x = plotLeft + w * juce::jlimit(0.0f,1.0f,t);
        float y = plotBottom - h * ((b.gain + 18.0f) / 36.0f);

        // Glow
        g.setColour (pink.withAlpha (0.3f));
        g.fillEllipse (x-12, y-12, 24, 24);
        g.setColour (pink.withAlpha (0.08f));
        g.fillEllipse (x-16, y-16, 32, 32);

        // Node
        g.setColour (pink);
        g.fillEllipse (x-5, y-5, 10, 10);
        g.setColour (JP::bg);
        g.drawEllipse (x-5, y-5, 10, 10, 1.0f);

        // Label
        g.setFont (juce::FontOptions ("Helvetica Neue", 8.0f, juce::Font::bold));
        juce::String lbl = b.freq >= 1000 ? juce::String(b.freq/1000,1)+"k" : juce::String((int)b.freq)+"Hz";
        g.setColour (pink.withAlpha (0.7f));
        g.drawText (lbl, juce::Rectangle<float>(x-22,y-20,44,12), juce::Justification::centred,false);
        juce::String gl = (b.gain>=0?"+":"") + juce::String(b.gain,1) + "dB";
        g.drawText (gl, juce::Rectangle<float>(x-22,y+8,44,12), juce::Justification::centred,false);
    }
}

void EQTEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() == 2)
    {
        for (size_t i = 0; i < bands.size(); ++i)
        {
            float t = std::log10(bands[i].freq/20.0f)/std::log10(1000.0f);
            float x = plotLeft + (plotRight-plotLeft)*t;
            float y = plotBottom - (plotBottom-plotTop)*((bands[i].gain+18)/36.0f);
            if (std::abs(e.x-x)<16 && std::abs(e.y-y)<16) { bands.erase(bands.begin()+i); applyBands(); repaint(); return; }
        }
        return;
    }

    float t = juce::jlimit(0.0f,1.0f,(e.x-plotLeft)/(plotRight-plotLeft));
    float fq = 20.0f * std::pow(1000.0f, t);
    float ga = (1.0f - (e.y-plotTop)/(plotBottom-plotTop)) * 36.0f - 18.0f;

    for (size_t i = 0; i < bands.size(); ++i)
    {
        float bt = std::log10(bands[i].freq/20.0f)/std::log10(1000.0f);
        float bx = plotLeft + (plotRight-plotLeft)*bt;
        float by = plotBottom - (plotBottom-plotTop)*((bands[i].gain+18)/36.0f);
        if (std::abs(e.x-bx)<12 && std::abs(e.y-by)<12) { draggingIdx = (int)i; return; }
    }

    bands.push_back ({ fq, ga, 1.0f, 0, true });
    applyBands();
    repaint();
}

void EQTEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingIdx < 0 || draggingIdx >= (int)bands.size()) return;
    float t = juce::jlimit(0.0f,1.0f,(e.x-plotLeft)/(plotRight-plotLeft));
    bands[draggingIdx].freq = 20.0f * std::pow(1000.0f, t);
    bands[draggingIdx].gain = juce::jlimit(-18.0f,18.0f,(1.0f-(e.y-plotTop)/(plotBottom-plotTop))*36.0f-18.0f);
    applyBands();
    repaint();
}

void EQTEditor::mouseUp (const juce::MouseEvent&) { draggingIdx = -1; }

void EQTEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        float t = std::log10(bands[i].freq/20.0f)/std::log10(1000.0f);
        float bx = plotLeft + (plotRight-plotLeft)*t;
        float by = plotBottom - (plotBottom-plotTop)*((bands[i].gain+18)/36.0f);
        if (std::abs(e.x-bx)<16 && std::abs(e.y-by)<16)
        {
            bands[i].q += w.deltaY * 0.5f;
            bands[i].q = juce::jlimit(0.1f,10.0f,bands[i].q);
            applyBands();
            repaint();
            return;
        }
    }
}

void EQTEditor::applyBands()
{
    proc.setBands (bands);
    bypassButton.setButtonText (proc.isActive() ? "BYPASS" : "BYPASSED");
}

void EQTEditor::showLicenseDialog()
{
    auto& lm = proc.licenseManager;
    juce::String msg = lm.isLicensed() ? "Licensed. Enter new key:" :
        juce::String(lm.getFreePromptsRemaining()) + " free prompts. Paste key:";
    auto* w = new juce::AlertWindow ("License", msg, juce::AlertWindow::QuestionIcon, this);
    w->addTextEditor ("key", lm.getLicenseKey(), "MM-XXXXXXXXXXXXXXXX");
    w->addButton ("Activate", 1); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this,w](int r){
        if (r==1) { auto k = w->getTextEditorContents("key").trim();
            if (k.isNotEmpty()) { proc.licenseManager.setLicenseKey(k); updateLicenseDisplay(); } }
        delete w;
    }));
    lm.markWelcomeShown();
}

void EQTEditor::updateLicenseDisplay()
{
    if (proc.licenseManager.isLicensed()) licenseButton.setButtonText ("LICENSED");
    else licenseButton.setButtonText (juce::String(proc.licenseManager.getFreePromptsRemaining())+" FREE");
}
