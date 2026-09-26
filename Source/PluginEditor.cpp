#include "PluginEditor.h"

namespace
{
    constexpr int kControlH = 26;

    void styleHeaderButton (juce::Button& b)
    {
        b.setColour (juce::TextButton::buttonColourId,  JP::surfaceRaised);
        b.setColour (juce::TextButton::textColourOffId, JP::textMuted);
        b.setColour (juce::TextButton::textColourOnId,  JP::accent);
    }

    void styleSlider (juce::Slider& s, juce::Colour c)
    {
        s.setColour (juce::Slider::thumbColourId, c);
        s.setColour (juce::Slider::trackColourId, c.withAlpha (0.45f));
        s.setColour (juce::Slider::backgroundColourId, JP::surfaceRaised);
    }

    bool isImageFile (const juce::File& f)
    {
        return f.hasFileExtension ("png;jpg;jpeg;gif;bmp;webp");
    }
}

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);
    JP::setAccent (juce::Colour (0xffff8a80));

    // Header — crisp, opaque text, no alpha
    titleLabel.setText ("MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 14.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text);
    titleLabel.setTooltip (juce::String::fromUTF8 ("JuicePipe MixMind — reference matching + BS.1770 metering"));
    addAndMakeVisible (titleLabel);

    // Reference track
    styleHeaderButton (loadRefButton);
    loadRefButton.setTooltip ("Load a reference track (or drop an audio file on the plugin).\nRight-click for more.");
    loadRefButton.onClick = [this]
    {
        if (audioProcessor.getReference() != nullptr) showReferenceMenu();
        else                                         chooseReference();
    };
    loadRefButton.onContextClick = [this] { showReferenceMenu(); };
    addAndMakeVisible (loadRefButton);

    // Reference layer — screenshot ghost composited behind the live FFT.
    styleHeaderButton (layerButton);
    layerButton.setTooltip ("Load a screenshot of a target curve. Drag to move, scroll to zoom,\n"
                            "Alt+drag to stretch, double-click the plot to fit. Right-click for more.");
    layerButton.onClick = [this]
    {
        if (telemetry.hasRefImage()) setLayerVisible (! telemetry.getRefImageVisible());
        else                         chooseImageLayer();
    };
    layerButton.onContextClick = [this] { showLayerMenu(); };
    addAndMakeVisible (layerButton);

    opacitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    opacitySlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    opacitySlider.setRange (0.05, 1.0, 0.01);
    opacitySlider.setValue (0.5);
    opacitySlider.setEnabled (false);
    opacitySlider.setTooltip ("Layer opacity");
    opacitySlider.onValueChange = [this] { telemetry.setRefImageOpacity ((float) opacitySlider.getValue()); };
    styleSlider (opacitySlider, JP::textMuted);
    addAndMakeVisible (opacitySlider);

    // Trace — draw a target curve for MANUAL mode.
    styleHeaderButton (traceButton);
    traceButton.setClickingTogglesState (true);
    traceButton.setTooltip ("Edit the target curve used in MANUAL mode: click to add points, drag to move,\n"
                            "right-click a point to delete it. Right-click this button to clear the trace.");
    traceButton.onClick = [this] { telemetry.setTraceMode (traceButton.getToggleState()); };
    traceButton.onContextClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Clear trace", ! audioProcessor.getTrace().empty(), false, [this] { audioProcessor.setTrace ({}); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&traceButton));
    };
    addAndMakeVisible (traceButton);

    // Focus dropdown — Master (band-coloured rainbow) + colour-coded sound groups
    for (auto g : FocusModel::selectableGroups())
        focusBox.addItem (FocusModel::groupName (g), (int) g + 1);
    focusBox.setSelectedId ((int) FocusModel::Group::Master + 1, juce::dontSendNotification);
    focusBox.setTooltip ("Focus: zoom the analyzer to a frequency region");
    focusBox.onChange = [this] { applyFocusSelection(); };
    addAndMakeVisible (focusBox);

    colorSwatch.setSwatchColour (FocusModel::colorFor (FocusModel::Group::Master).saturated);
    colorSwatch.onColourPicked = [this] (juce::Colour c)
    {
        auto g = telemetry.getFocusGroup();
        if (g == FocusModel::Group::Master) return;
        FocusModel::setColor (g, c);
        telemetry.repaint();
    };
    addAndMakeVisible (colorSwatch);

    // ── Shaper controls ───────────────────────────────────────────────────
    styleHeaderButton (shapeButton);
    shapeButton.setClickingTogglesState (true);
    shapeButton.setTooltip ("Apply the match EQ (AUTO = reference, MANUAL = trace).\n"
                            "The curve is previewed on the plot before you switch it on.");
    shapeButton.onStateChange = [this] { shapeButton.setButtonText (shapeButton.getToggleState() ? "SHAPE ON" : "SHAPE"); };
    addAndMakeVisible (shapeButton);
    shapeAttachment = std::make_unique<APVTS::ButtonAttachment> (audioProcessor.parameters, "shapeEnable", shapeButton);

    styleHeaderButton (modeButton);
    modeButton.setColour (juce::TextButton::textColourOffId, JP::text);
    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("AUTO = match the loaded reference. MANUAL = match the drawn trace.");
    modeButton.onStateChange = [this] { modeButton.setButtonText (modeButton.getToggleState() ? "MANUAL" : "AUTO"); };
    addAndMakeVisible (modeButton);
    modeAttachment = std::make_unique<APVTS::ButtonAttachment> (audioProcessor.parameters, "shapeMode", modeButton);

    styleHeaderButton (stereoButton);
    stereoButton.setColour (juce::TextButton::textColourOffId, JP::text);
    stereoButton.setClickingTogglesState (true);
    stereoButton.setTooltip ("LINK = one match curve for both channels.\n"
                             "M/S = match the mid and the side separately (AUTO only): the width per band follows the reference.");
    stereoButton.onStateChange = [this] { stereoButton.setButtonText (stereoButton.getToggleState() ? "M/S" : "LINK"); };
    addAndMakeVisible (stereoButton);
    stereoAttachment = std::make_unique<APVTS::ButtonAttachment> (audioProcessor.parameters, "matchStereo", stereoButton);

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
    amountSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    amountSlider.setColour (juce::Slider::textBoxTextColourId, JP::text);
    amountSlider.setTooltip ("Match amount (0 % = flat, 100 % = full match).");
    styleSlider (amountSlider, FocusModel::matchColour());
    addAndMakeVisible (amountSlider);
    amountAttachment = std::make_unique<APVTS::SliderAttachment> (audioProcessor.parameters, "shapeAmount", amountSlider);

    // Spectrum
    telemetry.onTraceEdited = [this] (std::vector<TelemetryCanvas::TracePoint> pts)
    {
        audioProcessor.setTrace (std::move (pts));
        seenTraceVersion = audioProcessor.getTraceVersion();   // canvas already shows it
    };
    telemetry.onReadoutClicked = [this] { audioProcessor.outputAnalyzer.resetLoudness(); };
    telemetry.onBandGestureStart = [this] (int b) { bandGesture (b, true); };
    telemetry.onBandGestureEnd   = [this] (int b) { bandGesture (b, false); };
    telemetry.onBandChanged      = [this] (int b, const ParametricEq::Band& band) { writeBand (b, band); };
    addAndMakeVisible (telemetry);

    // Sync button labels with the restored parameter state.
    shapeButton.onStateChange();
    modeButton.onStateChange();
    stereoButton.onStateChange();

    applyFocusSelection();

    openGLContext.setContinuousRepainting (false);
    openGLContext.attachTo (*this);

    setResizable (true, true);
    setResizeLimits (960, 420, 1800, 1100);
    setSize (1080, 680);

    timerCallback();
    startTimerHz (30);
}

MixMindEditor::~MixMindEditor()
{
    openGLContext.detach();
    stopTimer();
    shapeAttachment.reset();
    modeAttachment.reset();
    stereoAttachment.reset();
    amountAttachment.reset();
    setLookAndFeel (nullptr);
}

void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    const auto w = (float) getWidth();

    // Header
    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float) JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float) JP::headerH, w, (float) JP::headerH, 1.0f);

    // Signal-present dot (pulses while audio is flowing).
    const float dy = JP::headerH * 0.5f, dx = w - 20.0f;
    const bool sig = audioProcessor.audioAnalyzer.getMomentaryLufs() > -70.0f;
    const float la = sig ? 0.3f + 0.7f * std::abs (std::sin ((float) dotPhase * 0.08f)) : 0.2f;
    g.setColour (sig ? juce::Colours::cyan.withAlpha (la) : JP::textDim);
    g.fillEllipse (dx, dy - 3, 6, 6);
}

void MixMindEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    auto r = telemetry.getBounds().toFloat().reduced (8.0f);
    g.setColour (JP::accent.withAlpha (0.08f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (JP::accent.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    g.setFont (juce::FontOptions ("Helvetica Neue", 15.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("Drop audio to load a reference  ·  drop an image to load a layer"), r, juce::Justification::centred);
}

void MixMindEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (JP::headerH).reduced (12, (JP::headerH - kControlH) / 2);

    const auto take = [&header] (int w, int gap = 6)
    {
        auto r = header.removeFromLeft (w);
        header.removeFromLeft (gap);
        return r;
    };
    const auto takeRight = [&header] (int w, int gap = 6)
    {
        auto r = header.removeFromRight (w);
        header.removeFromRight (gap);
        return r;
    };

    // Fits the 960 px minimum width.
    titleLabel.setBounds    (take (70, 8));
    loadRefButton.setBounds (take (130));
    layerButton.setBounds   (take (60, 4));
    opacitySlider.setBounds (take (48, 10));
    traceButton.setBounds   (take (62, 10));
    focusBox.setBounds      (take (90));
    colorSwatch.setBounds   (take (kControlH - 2).withSizeKeepingCentre (kControlH - 2, kControlH - 2));

    takeRight (14, 10);   // signal dot
    amountSlider.setBounds (takeRight (130, 8));
    stereoButton.setBounds (takeRight (52));
    modeButton.setBounds   (takeRight (72));
    shapeButton.setBounds  (takeRight (84));

    telemetry.setBounds (b);
}

// ── Periodic sync with the processor ───────────────────────────────────────

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    // YOU = what comes out (after the match and the bands); the input is
    // drawn faintly underneath whenever the plugin is changing the sound.
    auto& aa = audioProcessor.outputAnalyzer;
    const bool shapeOn = audioProcessor.parameters.getRawParameterValue ("shapeEnable")->load() > 0.5f;

    telemetry.setUserBins (aa.getFFTBins(), AudioAnalyzer::numBins, audioProcessor.getCurrentSampleRate());

    std::array<ParametricEq::Band, ParametricEq::kNumBands> bands;
    bool eqOn = false;
    for (int b = 0; b < ParametricEq::kNumBands; ++b)
    {
        bands[(size_t) b] = audioProcessor.readBand (b);
        eqOn |= bands[(size_t) b].on;
    }
    telemetry.setBands (bands);
    if (shapeOn || eqOn) telemetry.setInputBins (audioProcessor.audioAnalyzer.getFFTBins(), AudioAnalyzer::numBins);
    else                 telemetry.setInputBins (nullptr, 0);

    TelemetryCanvas::Readout you;
    you.lufs     = aa.getLufs();
    you.truePeak = aa.getTruePeakDb();
    you.width    = aa.getStereoWidth();
    you.phase    = aa.getPhaseCorr();
    you.crest    = aa.getCrestFactor();
    telemetry.setUserReadout (you);

    if (audioProcessor.getReferenceVersion() != seenRefVersion)
        refreshReference();

    if (audioProcessor.getTraceVersion() != seenTraceVersion)
    {
        seenTraceVersion = audioProcessor.getTraceVersion();
        telemetry.setTrace (audioProcessor.getTrace());
    }

    telemetry.setCorrection (audioProcessor.getCorrectionDb(), audioProcessor.getCorrectionSideDb(), shapeOn);

    repaint (getLocalBounds().removeFromTop (JP::headerH).removeFromRight (40));
}

void MixMindEditor::refreshReference()
{
    const int version = audioProcessor.getReferenceVersion();
    const bool firstSync = seenRefVersion < 0;
    seenRefVersion = version;

    const auto status = audioProcessor.getReferenceStatus();
    const auto ref    = audioProcessor.getReference();

    if (ref != nullptr)
    {
        const auto& bins = audioProcessor.getReferenceBins();
        telemetry.setReference (bins.data(), (int) bins.size());

        TelemetryCanvas::Readout r;
        r.lufs     = ref->lufs;
        r.truePeak = ref->truePeakDb;
        r.width    = ref->stereoWidth;
        r.phase    = ref->phaseCorr;
        r.crest    = ref->getCrestFactor();
        telemetry.setRefReadout (&r);

        loadRefButton.setButtonText (ref->name);
        loadRefButton.setColour (juce::TextButton::textColourOffId, JP::text);
        loadRefButton.setTooltip (ref->path + "\n" + juce::String (ref->durationSeconds / 60.0, 1) + juce::String::fromUTF8 (" min · ")
                                  + juce::String (ref->sampleRate / 1000.0, 1) + " kHz\nRight-click for more.");
    }
    else
    {
        telemetry.setReference (nullptr, 0);
        telemetry.setRefReadout (nullptr);
        loadRefButton.setButtonText ("LOAD REF");
        loadRefButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    }

    if (status == MixMindProcessor::RefStatus::loading)
    {
        telemetry.setStatus ("Analyzing " + audioProcessor.getReferenceMessage() + juce::String::fromUTF8 ("…"));
        loadRefButton.setButtonText (juce::String::fromUTF8 ("ANALYZING…"));
    }
    else
    {
        telemetry.setStatus ({});
    }

    if (status == MixMindProcessor::RefStatus::failed && ! firstSync)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load reference",
                                                audioProcessor.getReferenceMessage(), {}, this);
}

// ── Reference ──────────────────────────────────────────────────────────────

void MixMindEditor::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference track", juce::File(),
                                                   audioProcessor.getReferenceWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MixMindEditor> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            const auto file = fc.getResult();
            if (file.existsAsFile())
                safeThis->audioProcessor.loadReference (file);
        });
}

void MixMindEditor::showReferenceMenu()
{
    const auto ref = audioProcessor.getReference();
    juce::PopupMenu m;
    m.addItem (ref != nullptr ? juce::String::fromUTF8 ("Replace reference…") : juce::String::fromUTF8 ("Load reference…"), [this] { chooseReference(); });
    if (ref != nullptr)
    {
        const juce::File file (ref->path);
        m.addItem ("Re-analyze", file.existsAsFile(), false, [this, file] { audioProcessor.loadReference (file); });
        m.addItem ("Show in folder", file.existsAsFile(), false, [file] { file.revealToUser(); });
        m.addSeparator();
        m.addItem ("Clear reference", [this] { audioProcessor.clearReference(); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadRefButton));
}

// ── Image layer ────────────────────────────────────────────────────────────

void MixMindEditor::chooseImageLayer()
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference layer image", juce::File(),
                                                   "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MixMindEditor> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis != nullptr && fc.getResult().existsAsFile())
                safeThis->loadImageLayer (fc.getResult());
        });
}

void MixMindEditor::loadImageLayer (const juce::File& file)
{
    const juce::Image img = juce::ImageFileFormat::loadFrom (file);
    if (img.isNull())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load layer",
                                                "Couldn't load that image.", {}, this);
        return;
    }
    telemetry.setRefImage (img);
    opacitySlider.setEnabled (true);
    setLayerVisible (true);
}

void MixMindEditor::setLayerVisible (bool v)
{
    telemetry.setRefImageVisible (v);
    layerButton.setToggleState (v && telemetry.hasRefImage(), juce::dontSendNotification);
}

void MixMindEditor::showLayerMenu()
{
    const bool has = telemetry.hasRefImage();
    juce::PopupMenu m;
    m.addItem (has ? juce::String::fromUTF8 ("Replace image…") : juce::String::fromUTF8 ("Load image…"), [this] { chooseImageLayer(); });
    if (has)
    {
        m.addItem ("Show layer", true, telemetry.getRefImageVisible(),
                   [this] { setLayerVisible (! telemetry.getRefImageVisible()); });
        m.addSeparator();
        m.addItem ("Clear layer", [this]
        {
            telemetry.clearRefImage();
            opacitySlider.setEnabled (false);
            layerButton.setToggleState (false, juce::dontSendNotification);
        });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&layerButton));
}

// ── Drag and drop ──────────────────────────────────────────────────────────

bool MixMindEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (audioProcessor.canLoadAsReference (juce::File (f)) || isImageFile (juce::File (f)))
            return true;
    return false;
}

void MixMindEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    for (const auto& path : files)
    {
        const juce::File f (path);
        if (audioProcessor.canLoadAsReference (f)) { audioProcessor.loadReference (f); return; }
        if (isImageFile (f))                       { loadImageLayer (f); return; }
    }
}

// ── Parametric bands ───────────────────────────────────────────────────────

void MixMindEditor::bandGesture (int band, bool starting)
{
    const auto& bp = audioProcessor.getBandParams (band);
    for (auto* p : std::initializer_list<juce::RangedAudioParameter*> { bp.on, bp.type, bp.freq, bp.gain, bp.q,
                                                                         bp.slope, bp.placement })
    {
        if (starting) p->beginChangeGesture();
        else          p->endChangeGesture();
    }
}

void MixMindEditor::writeBand (int band, const ParametricEq::Band& b)
{
    const auto& bp = audioProcessor.getBandParams (band);
    const auto set = [] (juce::RangedAudioParameter* p, float plainValue)
    {
        const float v = p->convertTo0to1 (plainValue);
        if (! juce::exactlyEqual (v, p->getValue())) p->setValueNotifyingHost (v);
    };
    set (bp.on,        b.on ? 1.0f : 0.0f);
    set (bp.type,      (float) (int) b.type);
    set (bp.freq,      b.freq);
    set (bp.gain,      b.gainDb);
    set (bp.q,         b.q);
    set (bp.slope,     (float) ParametricEq::indexFromSlope (b.slope));
    set (bp.placement, (float) (int) b.placement);
}

// ── Focus ──────────────────────────────────────────────────────────────────

void MixMindEditor::applyFocusSelection()
{
    const auto g = static_cast<FocusModel::Group> (focusBox.getSelectedId() - 1);
    telemetry.setFocusGroup (g);

    const bool isMaster = (g == FocusModel::Group::Master);
    colorSwatch.setVisible (! isMaster);
    if (! isMaster)
        colorSwatch.setSwatchColour (FocusModel::colorFor (g).saturated);
}
