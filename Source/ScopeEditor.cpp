#include "ScopeEditor.h"

ScopeEditor::ScopeEditor (ScopeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    JP::setAccentHex (0xffa78bfa);
    setSize (600, 500);
    setResizable (true, true);
    setResizeLimits (400, 300, 1200, 900);

    titleLabel.setText ("JuicePipe - Scope", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.5f));
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    startTimerHz (60);

    if (!proc.licenseManager.isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

void ScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth(), h = (float)getHeight();

    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    drawGoniometer (g);
    drawReadouts (g);
}

void ScopeEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (300));
    licenseButton.setBounds (header.withLeft (getWidth() - 140).withWidth (110).withHeight (26).withY (7));
}

void ScopeEditor::timerCallback()
{
    ++dotPhase;
    float l = proc.audioAnalyzer.getBassEnergy();
    float r = proc.audioAnalyzer.getMidEnergy();
    gonioL[gonioIdx] = l / 60.0f;
    gonioR[gonioIdx] = r / 60.0f;
    gonioIdx = (gonioIdx + 1) % gonioSize;
    if (gonioCount < gonioSize) gonioCount++;
    repaint();
}

void ScopeEditor::drawGoniometer (juce::Graphics& g)
{
    float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
    float r = juce::jmin (getWidth(), getHeight()) * 0.35f;

    // Scope background
    g.setColour (juce::Colour (0x06f2c4ce));
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (JP::border);
    g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 0.5f);
    g.setColour (JP::border.withAlpha (0.2f));
    g.drawLine (cx - r, cy, cx + r, cy, 0.5f);
    g.drawLine (cx, cy - r, cx, cy + r, 0.5f);

    // Lissajous dots
    if (gonioCount > 1)
    {
        juce::Path path;
        for (int i = 0; i < gonioCount; ++i)
        {
            float x = cx + gonioL[i] * r * 0.8f;
            float y = cy - gonioR[i] * r * 0.8f;
            x = juce::jlimit (cx - r, cx + r, x);
            y = juce::jlimit (cy - r, cy + r, y);
            if (i == 0) path.startNewSubPath (x, y);
            else path.lineTo (x, y);
        }
        g.setColour (JP::accent().withAlpha (0.5f));
        g.strokePath (path, juce::PathStrokeType (0.8f));
    }
}

void ScopeEditor::drawReadouts (juce::Graphics& g)
{
    float x = 16, y = (float)getHeight() - 100;
    auto pink = JP::accent();

    auto drawBar = [&](const juce::String& label, float val, float min, float max, const juce::String& unit)
    {
        g.setFont (juce::FontOptions ("Helvetica Neue", 10.0f, juce::Font::bold));
        g.setColour (pink.withAlpha (0.5f));
        g.drawText (label, juce::Rectangle<float> (x, y, 60, 16), juce::Justification::left, false);

        float norm = juce::jlimit (0.0f, 1.0f, (val - min) / (max - min));
        g.setColour (pink.withAlpha (0.08f));
        g.fillRoundedRectangle (x + 64, y + 3, 160, 10, 3);
        g.setColour (pink.withAlpha (0.4f));
        g.fillRoundedRectangle (x + 64, y + 3, 160 * norm, 10, 3);

        g.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));
        g.setColour (pink.withAlpha (0.7f));
        juce::String text = juce::String (val, 2) + " " + unit;
        g.drawText (text, juce::Rectangle<float> (x + 230, y, 80, 16), juce::Justification::left, false);
        y += 22;
    };

    drawBar ("PHASE", proc.audioAnalyzer.getPhaseCorr(), -1, 1, "corr");
    drawBar ("WIDTH", proc.audioAnalyzer.getStereoWidth(), 0, 1, "M/S");
    drawBar ("LUFS",  proc.audioAnalyzer.getLufs(), -60, 0, "dB");
}

void ScopeEditor::showLicenseDialog()
{
    auto& lm = proc.licenseManager;
    juce::String msg = lm.isLicensed() ? "Licensed. Enter new key:" :
        juce::String (lm.getFreePromptsRemaining()) + " free prompts. Paste key:";

    auto* w = new juce::AlertWindow ("License", msg, juce::AlertWindow::QuestionIcon, this);
    w->addTextEditor ("key", lm.getLicenseKey(), "MM-XXXXXXXXXXXXXXXX");
    w->addButton ("Activate", 1); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w](int r){
        if (r == 1) {
            auto k = w->getTextEditorContents ("key").trim();
            if (k.isNotEmpty()) { proc.licenseManager.setLicenseKey (k); updateLicenseDisplay(); }
        }
        delete w;
    }));
    lm.markWelcomeShown();
}

void ScopeEditor::updateLicenseDisplay()
{
    if (proc.licenseManager.isLicensed())
        licenseButton.setButtonText ("LICENSED");
    else
        licenseButton.setButtonText (juce::String (proc.licenseManager.getFreePromptsRemaining()) + " FREE");
}
