#include "MeterEditor.h"

MeterEditor::MeterEditor (MeterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (500, 600);
    setResizable (true, true);
    setResizeLimits (350, 400, 1000, 900);

    titleLabel.setText ("JuicePipe - Meter", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.5f));
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    startTimerHz (30);

    if (!proc.licenseManager.isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

void MeterEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth(), h = (float)getHeight();

    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    float pad = 24, barY = JP::headerH + 32;
    float barW = w - pad * 2;
    float gap = 16;

    drawMeterBar (g, pad, barY, barW, 36, aLufs,  proc.audioAnalyzer.getLufs(),        -60, 0,   "INTEGRATED LUFS", "LUFS",   JP::accent());
    barY += 52;
    drawMeterBar (g, pad, barY, barW, 28, aPeak,  proc.audioAnalyzer.getLufs() + 3,     -12, 0,   "TRUE PEAK",       "dBTP",   aPeak > -1 ? JP::warning : JP::accent(), true);
    barY += 44;
    drawMeterBar (g, pad, barY, barW, 28, aCrest, proc.audioAnalyzer.getLufs() * 0.2f,   0, 20,  "CREST FACTOR",    "dB",     aCrest < 6 ? JP::warning : JP::accent());
    barY += 44;
    drawMeterBar (g, pad, barY, barW, 28, aPhase, proc.audioAnalyzer.getPhaseCorr(),     -1,  1,  "PHASE CORR",      "corr",   aPhase < 0.3f ? JP::warning : JP::accent());
    barY += 44;
    drawMeterBar (g, pad, barY, barW, 28, aStereo,proc.audioAnalyzer.getStereoWidth(),    0,  1,  "STEREO WIDTH",    "M/S",    JP::accent());
    barY += 52;

    // Spectral bars
    g.setFont (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::bold));
    g.setColour (JP::accent().withAlpha (0.4f));
    g.drawText ("SPECTRUM", juce::Rectangle<float> (pad, barY, barW, 16), juce::Justification::left, false);
    barY += 22;
    drawMeterBar (g, pad, barY, barW, 16, aBass,  proc.audioAnalyzer.getBassEnergy(),    -60, 0, "BASS", "", JP::accent());
    barY += 28;
    drawMeterBar (g, pad, barY, barW, 16, aMid,   proc.audioAnalyzer.getMidEnergy(),     -60, 0, "MID",  "", JP::accent());
    barY += 28;
    drawMeterBar (g, pad, barY, barW, 16, aHigh,  proc.audioAnalyzer.getHighEnergy(),    -60, 0, "HIGH", "", JP::accent());
}

void MeterEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (300));
    licenseButton.setBounds (header.withLeft (getWidth() - 140).withWidth (110).withHeight (26).withY (7));
}

void MeterEditor::timerCallback()
{
    ++dotPhase;
    const float k = 0.10f;
    aLufs   += (proc.audioAnalyzer.getLufs()        - aLufs)   * k;
    aPeak   += (proc.audioAnalyzer.getLufs() + 3.0f - aPeak)   * k;
    aCrest  += ((proc.audioAnalyzer.getLufs() * 0.2f) - aCrest)* k;
    aPhase  += (proc.audioAnalyzer.getPhaseCorr()    - aPhase)  * k;
    aStereo += (proc.audioAnalyzer.getStereoWidth()  - aStereo) * k;
    aBass   += (proc.audioAnalyzer.getBassEnergy()   - aBass)   * k;
    aMid    += (proc.audioAnalyzer.getMidEnergy()    - aMid)    * k;
    aHigh   += (proc.audioAnalyzer.getHighEnergy()   - aHigh)   * k;
    repaint();
}

void MeterEditor::drawMeterBar (juce::Graphics& g, float x, float y, float w, float h,
                                 float& animVal, float target, float min, float max,
                                 const juce::String& label, const juce::String& unit,
                                 juce::Colour col, bool showPeak)
{
    float norm = juce::jlimit (0.0f, 1.0f, (animVal - min) / (max - min));

    g.setColour (col.withAlpha (0.06f));
    g.fillRoundedRectangle (x, y, w, h, 4);

    g.setColour (col.withAlpha (0.4f));
    g.fillRoundedRectangle (x, y, w * norm, h, 4);

    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::bold));
    g.setColour (col.withAlpha (0.5f));
    g.drawText (label, juce::Rectangle<float> (x + 8, y, 160, h), juce::Justification::left, false);

    g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));
    juce::String valText = juce::String (animVal, 1) + (unit.isNotEmpty() ? " " + unit : "");
    g.setColour (col.withAlpha (0.7f));
    g.drawText (valText, juce::Rectangle<float> (x + w - 100, y, 92, h), juce::Justification::right, false);
}

void MeterEditor::showLicenseDialog()
{
    auto& lm = proc.licenseManager;
    juce::String msg = lm.isLicensed() ? "Licensed. Enter new key:" :
        juce::String (lm.getFreePromptsRemaining()) + " free prompts. Paste key:";
    auto* w = new juce::AlertWindow ("License", msg, juce::AlertWindow::QuestionIcon, this);
    w->addTextEditor ("key", lm.getLicenseKey(), "MM-XXXXXXXXXXXXXXXX");
    w->addButton ("Activate", 1); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w](int r){
        if (r == 1) { auto k = w->getTextEditorContents ("key").trim();
            if (k.isNotEmpty()) { proc.licenseManager.setLicenseKey (k); updateLicenseDisplay(); } }
        delete w;
    }));
    lm.markWelcomeShown();
}

void MeterEditor::updateLicenseDisplay()
{
    if (proc.licenseManager.isLicensed()) licenseButton.setButtonText ("LICENSED");
    else licenseButton.setButtonText (juce::String (proc.licenseManager.getFreePromptsRemaining()) + " FREE");
}
