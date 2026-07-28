#include "ReflexEditor.h"

ReflexEditor::ReflexEditor (ReflexProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    JP::setTheme(juce::Colour(0xff69db7c));
    setSize (700, 400);
    setResizable (true, true);
    setResizeLimits (500, 300, 1200, 700);

    titleLabel.setText ("JuicePipe - Reflex", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.5f));
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    auto makeSlider = [&](juce::Slider& s, juce::Label& l, const juce::String& name, float min, float max, float def, float step)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
        s.setRange (min, max, step);
        s.setValue (def);
        s.setColour (juce::Slider::rotarySliderFillColourId, JP::accent);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, JP::border);
        s.setColour (juce::Slider::thumbColourId, JP::accent);
        s.setColour (juce::Slider::textBoxTextColourId, JP::text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setFont (juce::FontOptions ("Helvetica Neue", 9.0f, juce::Font::plain));
        l.setColour (juce::Label::textColourId, JP::textMuted);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    makeSlider (freqSlider,  freqLabel,  "FREQ",    100, 2000, 440, 1);
    makeSlider (resSlider,   resLabel,   "RES",     0.5f, 20, 5, 0.1f);
    makeSlider (mixSlider,   mixLabel,   "MIX",     0, 1, 0.5f, 0.01f);
    makeSlider (delaySlider, delayLabel, "DELAY",   0.01f, 1, 0.25f, 0.01f);
    makeSlider (fbSlider,    fbLabel,    "FDBK",    0, 0.9f, 0.3f, 0.01f);
    makeSlider (reverbSlider,reverbLabel,"VERB",    0, 1, 0.2f, 0.01f);
    makeSlider (seqSlider,   seqLabel,   "SEQ RATE",0.05f, 1, 0.25f, 0.01f);

    freqSlider.onValueChange = [this] { proc.getResFreq() = (float)freqSlider.getValue(); };
    resSlider.onValueChange  = [this] { proc.getResonance() = (float)resSlider.getValue(); };
    mixSlider.onValueChange  = [this] { proc.getMix() = (float)mixSlider.getValue(); };
    delaySlider.onValueChange= [this] { proc.getDelayTime() = (float)delaySlider.getValue(); };
    fbSlider.onValueChange   = [this] { proc.getFeedback() = (float)fbSlider.getValue(); };
    reverbSlider.onValueChange=[this]{ proc.getReverbAmt() = (float)reverbSlider.getValue(); };
    seqSlider.onValueChange  = [this] { proc.getSeqRate() = (float)seqSlider.getValue(); };

    if (!proc.licenseManager.isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

void ReflexEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, (float)getWidth(), (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, (float)getWidth(), (float)JP::headerH, 1.0f);
}

void ReflexEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (260));
    licenseButton.setBounds (header.withLeft (getWidth() - 130).withWidth (110).withHeight (26).withY (7));

    auto area = getLocalBounds().withTrimmedTop (JP::headerH + 8).reduced (12);
    int w = (area.getWidth() - 40) / 4;
    int h = area.getHeight();

    auto place = [&](int col, juce::Slider& s, juce::Label& l) {
        int x = 12 + col * (w + 12);
        s.setBounds (x, area.getY() + 16, w, w);
        l.setBounds (x, area.getY() + w + 16, w, 16);
    };

    place (0, freqSlider, freqLabel);
    place (1, resSlider, resLabel);
    place (2, mixSlider, mixLabel);
    place (3, delaySlider, delayLabel);

    place (1, fbSlider, fbLabel);
    place (2, reverbSlider, reverbLabel);
    place (3, seqSlider, seqLabel);
}

void ReflexEditor::showLicenseDialog()
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

void ReflexEditor::updateLicenseDisplay()
{
    if (proc.licenseManager.isLicensed()) licenseButton.setButtonText ("LICENSED");
    else licenseButton.setButtonText (juce::String(proc.licenseManager.getFreePromptsRemaining())+" FREE");
}
