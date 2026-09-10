#include "PluginEditor.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf); JP::setAccent(juce::Colour(0xffff8a80));
    setSize (1050, 680);
    setResizable (true, true);
    setResizeLimits (900, 400, 1600, 1000);

    // Header — crisp, opaque text, no alpha
    titleLabel.setText ("JuicePipe  —  MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text);
    addAndMakeVisible (titleLabel);

    // Focus dropdown — Master (band-coloured rainbow) + colour-coded sound groups
    for (auto g : FocusModel::selectableGroups())
        focusBox.addItem (FocusModel::groupName (g), (int) g + 1);
    focusBox.setSelectedId ((int) FocusModel::Group::Master + 1, juce::dontSendNotification);
    focusBox.onChange = [this] { applyFocusSelection(); };
    addAndMakeVisible (focusBox);

    // Reference loader
    loadRefButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    loadRefButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    loadRefButton.onClick = [this] { loadReference(); };
    addAndMakeVisible (loadRefButton);

    // Reference layer — screenshot ghost composited behind the live FFT.
    layerButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    layerButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    layerButton.setTooltip ("Load a screenshot of a target curve. Drag to move, scroll to zoom,\nAlt+drag to stretch, double-click the plot to fit. Right-click to clear.");
    layerButton.onClick = [this]
    {
        if (!telemetry.hasRefImage())
            loadImageLayer();
        else
        {
            const bool v = !telemetry.getRefImageVisible();
            telemetry.setRefImageVisible (v);
            layerButton.setButtonText (v ? "LAYER ✓" : "LAYER");
            layerButton.setColour (juce::TextButton::textColourOffId, v ? JP::accent : JP::textMuted);
        }
    };
    addAndMakeVisible (layerButton);

    // Trace — click points on the plot to draw a target curve.
    traceButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    traceButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    traceButton.setTooltip ("Click points on the plot to draw a target curve.\nRight-click on the plot removes the last point. Right-click here clears all.");
    traceButton.onClick = [this]
    {
        const bool on = !telemetry.getTraceMode();
        telemetry.setTraceMode (on);
        traceButton.setButtonText (on ? "TRACE ✓" : "TRACE");
        traceButton.setColour (juce::TextButton::textColourOffId, on ? JP::accent : JP::textMuted);
    };
    addAndMakeVisible (traceButton);

    // Layer opacity
    opacitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    opacitySlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    opacitySlider.setRange (0.05, 1.0, 0.01);
    opacitySlider.setValue (0.5);
    opacitySlider.setEnabled (false);
    opacitySlider.setTooltip ("Reference layer opacity");
    opacitySlider.onValueChange = [this] { telemetry.setRefImageOpacity ((float) opacitySlider.getValue()); };
    addAndMakeVisible (opacitySlider);

    // ── Shaper controls ───────────────────────────────────────────────────
    shapeButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    shapeButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    shapeButton.setClickingTogglesState (true);
    shapeButton.setTooltip ("Apply the match EQ (AUTO = reference, MANUAL = trace).");
    shapeButton.onClick = [this]
    {
        const bool on = shapeButton.getToggleState();
        audioProcessor.parameters.getParameter ("shapeEnable")->setValueNotifyingHost (on ? 1.0f : 0.0f);
        shapeButton.setButtonText (on ? "SHAPE ✓" : "SHAPE");
        shapeButton.setColour (juce::TextButton::textColourOffId, on ? JP::accent : JP::textMuted);
    };
    addAndMakeVisible (shapeButton);

    modeButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    modeButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    modeButton.setTooltip ("AUTO = match the loaded reference. MANUAL = match the drawn trace.");
    modeButton.onClick = [this]
    {
        manualMode = !manualMode;
        audioProcessor.parameters.getParameter ("shapeMode")->setValueNotifyingHost (manualMode ? 1.0f : 0.0f);
        modeButton.setButtonText (manualMode ? "MANUAL" : "AUTO");
        modeButton.setColour (juce::TextButton::textColourOffId, manualMode ? JP::accent : JP::textMuted);
    };
    addAndMakeVisible (modeButton);

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 22);
    amountSlider.setRange (0.0, 100.0, 1.0);
    amountSlider.setValue (75.0);
    amountSlider.setTooltip ("Match amount (0 = bypass the curve, 100 = full match).");
    amountSlider.onValueChange = [this]
    {
        audioProcessor.parameters.getParameter ("shapeAmount")->setValueNotifyingHost ((float) amountSlider.getValue() * 0.01f);
    };
    addAndMakeVisible (amountSlider);

    // Colour swatch — remaps the current focus group's colour
    colorSwatch.setSwatchColour (FocusModel::colorFor (FocusModel::Group::Master).saturated);
    colorSwatch.onColourPicked = [this] (juce::Colour c)
    {
        auto g = telemetry.getFocusGroup();
        if (g == FocusModel::Group::Master) return;
        FocusModel::setColor (g, c);
        telemetry.repaint();
    };
    addAndMakeVisible (colorSwatch);

    // Spectrum
    addAndMakeVisible (telemetry);

    applyFocusSelection();
    startTimerHz (60);
}

MixMindEditor::~MixMindEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth();

    // Header
    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    // Signal-present dot (pulses while audio is flowing).
    float dy = JP::headerH * 0.5f, dx = w - 30.0f;
    bool sig = audioProcessor.audioAnalyzer.getLufs() > -90.0f;
    float la = sig ? 0.3f + 0.7f * std::abs(std::sin((float)dotPhase * 0.04f)) : 0.2f;
    g.setColour (sig ? juce::Colours::cyan.withAlpha(la) : JP::textDim);
    g.fillEllipse (dx, dy - 3, 6, 6);
}

void MixMindEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (JP::headerH);
    titleLabel.setBounds  (header.withLeft (14).withWidth (176));
    loadRefButton.setBounds (header.withLeft (198).withWidth (80).withHeight (26).withY (7));
    layerButton.setBounds  (header.withLeft (284).withWidth (60).withHeight (26).withY (7));
    traceButton.setBounds  (header.withLeft (350).withWidth (64).withHeight (26).withY (7));
    opacitySlider.setBounds (header.withLeft (420).withWidth (76).withHeight (26).withY (7));
    focusBox.setBounds      (header.withLeft (502).withWidth (100).withHeight (26).withY (7));
    colorSwatch.setBounds   (header.withLeft (610).withWidth (24).withHeight (24).withY (8));
    shapeButton.setBounds   (header.withLeft (644).withWidth (68).withHeight (26).withY (7));
    modeButton.setBounds    (header.withLeft (718).withWidth (76).withHeight (26).withY (7));
    amountSlider.setBounds  (header.withLeft (800).withWidth (110).withHeight (26).withY (7));

    telemetry.setBounds (b);
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    const auto& aa = audioProcessor.audioAnalyzer;
    telemetry.setUserBins (aa.getFFTBins(), AudioAnalyzer::numBins, aa.getSampleRate());
    telemetry.setUserScalars (aa.getLufs(), aa.getStereoWidth(), aa.getPhaseCorr(), aa.getCrestFactor());
    updateShaper();
}

// ── Shaper ────────────────────────────────────────────────────────────────
void MixMindEditor::updateShaper()
{
    auto& apvts = audioProcessor.parameters;
    const bool shapeOn = apvts.getRawParameterValue ("shapeEnable")->load() > 0.5f;

    if (shapeOn && !wasShapeOn)
        firThrottle = 14;   // design immediately on enable
    wasShapeOn = shapeOn;

    if (!shapeOn) return;

    if (++firThrottle < 15) return;   // ~4 Hz redesign
    firThrottle = 0;

    const auto& aa = audioProcessor.audioAnalyzer;
    const int n = AudioAnalyzer::numBins;
    const float amount    = apvts.getRawParameterValue ("shapeAmount")->load();
    const bool  autoMode  = apvts.getRawParameterValue ("shapeMode")->load() < 0.5f;

    std::vector<float> target ((size_t) n);

    if (autoMode)
    {
        if (!referenceAnalyzer.hasReference()) return;
        std::copy (referenceAnalyzer.getBins(), referenceAnalyzer.getBins() + n, target.begin());
    }
    else
    {
        if (!telemetry.hasTrace()) return;
        ShaperProcessor::buildTargetFromCurve (telemetry.getTraceCurve(), n, aa.getSampleRate(), target);
    }

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), aa.getFFTBins(), n, amount, taps);
    audioProcessor.shaper.setFilter (taps.data(), (int) taps.size());
}

// ── Reference + Focus ──────────────────────────────────────────────────────

void MixMindEditor::loadReference()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load reference track", juce::File(), "*.wav;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<MixMindEditor> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, chooser] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            const auto results = fc.getResults();
            if (results.isEmpty()) return;

            const auto file = results.getReference (0);
            juce::String error;
            if (safeThis->referenceAnalyzer.loadFile (file, safeThis->audioProcessor.audioAnalyzer.getSampleRate(), error))
            {
                safeThis->telemetry.setReference (safeThis->referenceAnalyzer.getBins(), ReferenceAnalyzer::numBins);
                safeThis->telemetry.setRefScalars (safeThis->referenceAnalyzer.getLufs(),
                                                  safeThis->referenceAnalyzer.getStereoWidth(),
                                                  safeThis->referenceAnalyzer.getPhaseCorr(),
                                                  safeThis->referenceAnalyzer.getCrestFactor());
                safeThis->loadRefButton.setButtonText (safeThis->referenceAnalyzer.getFileName());
                safeThis->loadRefButton.setColour (juce::TextButton::textColourOffId, JP::text);
                safeThis->firThrottle = 14;   // refresh the match filter with the new reference
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                        "Load reference", error);
            }
        });
}

void MixMindEditor::applyFocusSelection()
{
    const auto g = static_cast<FocusModel::Group> (focusBox.getSelectedId() - 1);
    telemetry.setFocusGroup (g);

    const bool isMaster = (g == FocusModel::Group::Master);
    colorSwatch.setVisible (!isMaster);
    if (!isMaster)
        colorSwatch.setSwatchColour (FocusModel::colorFor (g).saturated);
}

void MixMindEditor::loadImageLayer()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load reference layer image", juce::File(),
        "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp");
    juce::Component::SafePointer<MixMindEditor> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, chooser] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            const auto results = fc.getResults();
            if (results.isEmpty()) return;

            const juce::Image img = juce::ImageFileFormat::loadFrom (results.getReference (0));
            if (img.isNull())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                        "Load layer", "Couldn't load that image.");
                return;
            }

            safeThis->telemetry.setRefImage (img);
            safeThis->opacitySlider.setEnabled (true);
            safeThis->layerButton.setButtonText ("LAYER ✓");
            safeThis->layerButton.setColour (juce::TextButton::textColourOffId, JP::accent);
        });
}

void MixMindEditor::mouseDown (const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown()) return;

    if (e.eventComponent == &layerButton)
    {
        telemetry.clearRefImage();
        opacitySlider.setEnabled (false);
        layerButton.setButtonText ("LAYER");
        layerButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    }
    else if (e.eventComponent == &traceButton)
    {
        telemetry.clearTrace();
    }
}
