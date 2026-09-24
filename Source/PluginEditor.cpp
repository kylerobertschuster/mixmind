#include "PluginEditor.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf); JP::setAccent(juce::Colour(0xffff8a80));
    setSize (1050, 680);
    setResizable (true, true);
    setResizeLimits (980, 400, 1600, 1000);   // header row needs ~980 to stay uncramped

    // Recall whatever the session restored. The processor owns the match state
    // now, and it was loaded before this editor was ever constructed.
    if (audioProcessor.referenceAnalyzer.hasReference())
        adoptReferenceIntoUi();

    telemetry.setTraceCurve (audioProcessor.getTraceCurve());
    lastSyncedTrace = audioProcessor.getTraceCurve();

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

    // Channel mode — which part of the stereo signal the shaper matches. This
    // drives the FIR, the live FFT and the reference spectrum together, so all
    // three always describe the same signal.
    chanBox.setTooltip ("Which part of the stereo signal the shaper analyzes and matches.\nSIDE is the difference signal (L-R) — useful for taming a wide reverb tail.");
    for (auto m : allChannelModes())
        chanBox.addItem (channelModeName (m), (int) m + 1);
    chanBox.onChange = [this]
    {
        // The parameter (not the box) is the state: it is what the processor
        // reads and what gets saved with the project.
        const int idx = chanBox.getSelectedId() - 1;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.parameters.getParameter ("channelMode")))
            *choice = idx;

        applyChannelMode();
    };
    addAndMakeVisible (chanBox);

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
    const int h = 26, y = 7;

    // One flowing row. The amount slider absorbs the leftover width, and the
    // right margin leaves room for the signal dot.
    int x = 14;
    titleLabel.setBounds    (header.withLeft (x).withWidth (140));                                        x += 146;
    loadRefButton.setBounds (header.withLeft (x).withWidth (76).withHeight (h).withY (y));                x += 82;
    layerButton.setBounds   (header.withLeft (x).withWidth (56).withHeight (h).withY (y));                x += 62;
    traceButton.setBounds   (header.withLeft (x).withWidth (60).withHeight (h).withY (y));                x += 66;
    opacitySlider.setBounds (header.withLeft (x).withWidth (64).withHeight (h).withY (y));                x += 70;
    chanBox.setBounds       (header.withLeft (x).withWidth (84).withHeight (h).withY (y));                x += 90;
    focusBox.setBounds      (header.withLeft (x).withWidth (96).withHeight (h).withY (y));                x += 102;
    colorSwatch.setBounds   (header.withLeft (x).withWidth (24).withHeight (24).withY (8));               x += 30;
    shapeButton.setBounds   (header.withLeft (x).withWidth (64).withHeight (h).withY (y));                x += 70;
    modeButton.setBounds    (header.withLeft (x).withWidth (72).withHeight (h).withY (y));                x += 78;
    amountSlider.setBounds  (juce::Rectangle<int> (x, y, juce::jmax (90, getWidth() - 46 - x), h));

    telemetry.setBounds (b);
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;

    // Pick up channel-mode changes that didn't come from the dropdown (session
    // reload, preset load, the host's generic parameter view).
    const int chanIdx = currentChannelModeIndex();
    if (chanIdx != lastChannelIdx)
        applyChannelMode();

    const auto& aa = audioProcessor.audioAnalyzer;
    telemetry.setUserBins (aa.getFFTBins(), AudioAnalyzer::numBins, aa.getSampleRate());
    telemetry.setUserScalars (aa.getLufs(), aa.getStereoWidth(), aa.getPhaseCorr(), aa.getCrestFactor());
    syncTraceToProcessor();
}

// ── Trace → processor ─────────────────────────────────────────────────────
// The editor no longer designs the match filter. The processor owns the match
// state and its own redesign timer, so the match keeps working — and keeps
// being saved — with no window open. This only hands over what was drawn.
void MixMindEditor::syncTraceToProcessor()
{
    const auto drawn = telemetry.getTraceCurve();

    // Both sides are the same normalised representation, so an unedited curve
    // compares bit-identical and costs nothing across the ~30 Hz UI timer.
    if (drawn.size() == lastSyncedTrace.size())
    {
        bool same = true;
        for (int i = 0; i < drawn.size() && same; ++i)
            same = drawn[i].first  == lastSyncedTrace[i].first
                && drawn[i].second == lastSyncedTrace[i].second;

        if (same) return;
    }

    lastSyncedTrace = drawn;
    audioProcessor.setTraceCurve (drawn);
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
            if (safeThis->audioProcessor.referenceAnalyzer.loadFile (file, safeThis->audioProcessor.audioAnalyzer.getSampleRate(), error))
            {
                safeThis->adoptReferenceIntoUi();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                        "Load reference", error);
            }
        });
}

// Single place that pushes the (possibly recalled) reference into the UI, so a
// session load and a fresh file pick cannot drift apart.
void MixMindEditor::adoptReferenceIntoUi()
{
    auto& ref = audioProcessor.referenceAnalyzer;

    telemetry.setReference (ref.getBins(), AudioAnalyzer::numBins);
    telemetry.setRefScalars (ref.getLufs(), ref.getStereoWidth(), ref.getPhaseCorr(), ref.getCrestFactor());

    loadRefButton.setButtonText (ref.getFileName());
    loadRefButton.setColour (juce::TextButton::textColourOffId, JP::text);
}

// The channel mode lives in the parameter, so index and enum can't drift. Clamped
// because a host may hand us a value between steps.
int MixMindEditor::currentChannelModeIndex() const
{
    const float raw = audioProcessor.parameters.getRawParameterValue ("channelMode")->load();
    return juce::jlimit (0, (int) allChannelModes().size() - 1, juce::roundToInt (raw));
}

void MixMindEditor::applyChannelMode()
{
    const int idx = currentChannelModeIndex();
    lastChannelIdx = idx;
    chanBox.setSelectedId (idx + 1, juce::dontSendNotification);

    // The processor applies the mode to the live analyzer and the shaper itself
    // (it must work with no editor open). What's left here is the reference: it
    // pre-computed all four spectra at load, so this only selects which one
    // getBins() hands out. No redesign nudge is needed — MixMindProcessor owns
    // the match design and re-derives the target on its own timer (the editor's
    // old firThrottle hook went away with it).
    auto& ref = audioProcessor.referenceAnalyzer;

    if (ref.hasReference())
    {
        ref.setChannelMode (allChannelModes()[(size_t) idx]);
        telemetry.setReference (ref.getBins(), ReferenceAnalyzer::numBins);
    }
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
        audioProcessor.clearTrace();   // the processor owns it now, so clear both
        lastSyncedTrace.clear();
    }
}
