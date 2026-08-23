#include "PluginEditor.h"
#include "LicenseManager.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf); JP::setAccent(juce::Colour(0xffff8a80));
    setSize (1050, 680);
    setResizable (true, true);
    setResizeLimits (700, 400, 1600, 1000);

    // Header — crisp, opaque text, no alpha
    titleLabel.setText ("JuicePipe  —  MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 13.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text);
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    // Quick prompts — styled button opens popup
    promptButton.setButtonText ("PROMPTS");
    promptButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    promptButton.setColour (juce::TextButton::textColourOffId, JP::textMuted);
    promptButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (2, "Is my low end balanced?");
        menu.addItem (3, "How's the stereo width?");
        menu.addItem (4, "Check my dynamics and crest factor");
        menu.addItem (5, "What's eating my headroom?");
        menu.addItem (6, "Give me a master chain for this");
        menu.addItem (7, "Diagnose my overall mix balance");
        menu.addItem (8, "Are my vocals sitting right?");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&promptButton),
            [this] (int result)
            {
                switch (result) {
                    case 2: chatComponent.setInputText ("Is my low end balanced?"); break;
                    case 3: chatComponent.setInputText ("How's the stereo width?"); break;
                    case 4: chatComponent.setInputText ("Check my dynamics and crest factor"); break;
                    case 5: chatComponent.setInputText ("What's eating my headroom?"); break;
                    case 6: chatComponent.setInputText ("Give me a master chain for this"); break;
                    case 7: chatComponent.setInputText ("Diagnose my overall mix balance"); break;
                    case 8: chatComponent.setInputText ("Are my vocals sitting right?"); break;
                }
            });
    };
    addAndMakeVisible (promptButton);

    // Focus dropdown — Master (cow print) + colour-coded sound groups
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

    // Presets sidebar
    strawPanel.onQuickPrompt = [this] { handleUserMessage (strawPanel.quickPromptText); };
    addAndMakeVisible (strawPanel);

    // Chat
    chatComponent.onSendMessage = [this] (const juce::String& t) { handleUserMessage (t); };
    addAndMakeVisible (chatComponent);

    // Sync initial focus state (hides colour swatch for the default Master view).
    applyFocusSelection();

    startTimerHz (60);

    if (!audioProcessor.getLicenseManager().isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

MixMindEditor::~MixMindEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth(), h = (float)getHeight();

    // Header
    g.setColour (JP::surface);
    g.fillRect (juce::Rectangle<float> (0, 0, w, (float)JP::headerH));
    g.setColour (JP::border);
    g.drawLine (0, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    // Sidebar divider
    float sx = w - (float)JP::sidebarW;
    g.setColour (JP::border);
    g.drawLine (sx, (float)JP::headerH, sx, h, 0.5f);

    // Status dots
    float dy = JP::headerH * 0.5f, dx = w - 50.0f;
    float a = waitingForReply ? 0.3f + 0.7f * std::abs(std::sin((float)dotPhase * 0.08f)) : 0.4f;
    g.setColour (waitingForReply ? JP::accent.withAlpha(a) : JP::textDim);
    g.fillEllipse (dx, dy - 3, 6, 6);

    bool sig = audioProcessor.audioAnalyzer.getLufs() > -90.0f;
    float la = sig ? 0.3f + 0.7f * std::abs(std::sin((float)dotPhase * 0.04f)) : 0.2f;
    g.setColour (sig ? juce::Colours::cyan.withAlpha(la) : JP::textDim);
    g.fillEllipse (dx + 16, dy - 3, 6, 6);
}

void MixMindEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (176));
    loadRefButton.setBounds (header.withLeft (198).withWidth (82).withHeight (26).withY (7));
    focusBox.setBounds (header.withLeft (288).withWidth (104).withHeight (26).withY (7));
    colorSwatch.setBounds (header.withLeft (400).withWidth (24).withHeight (24).withY (8));
    promptButton.setBounds (header.withLeft (432).withWidth (80).withHeight (26).withY (7));
    licenseButton.setBounds (header.withLeft (getWidth() - 160).withWidth (130).withHeight (26).withY (7));

    auto sidebar = b.removeFromRight (JP::sidebarW);
    strawPanel.setBounds (sidebar);

    auto chatArea = b.removeFromBottom ((int)(b.getHeight() * 0.42f));
    telemetry.setBounds (b);
    chatComponent.setBounds (chatArea);
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    const auto& aa = audioProcessor.audioAnalyzer;
    telemetry.setUserBins (aa.getFFTBins(), AudioAnalyzer::numBins, aa.getSampleRate());
    telemetry.setUserScalars (aa.getLufs(), aa.getStereoWidth(), aa.getPhaseCorr(), aa.getCrestFactor());
    if (waitingForReply) repaint();
}

// ── License ──────────────────────────────────────────────────────────────
void MixMindEditor::showLicenseDialog()
{
    auto& lm = audioProcessor.getLicenseManager();
    juce::String msg;
    if (lm.isLicensed()) msg = "Licensed. Enter a new key:";
    else if (lm.getFreePromptsRemaining() > 0)
        msg = juce::String(lm.getFreePromptsRemaining()) + " free prompts. Paste key:";
    else msg = "All free prompts used. Paste key:";

    auto* w = new juce::AlertWindow ("MixMind License", msg,
                                      juce::AlertWindow::QuestionIcon, this);
    w->addTextEditor ("key", lm.getLicenseKey(), "MM-XXXXXXXXXXXXXXXX");
    w->addButton ("Activate", 1);
    w->addButton ("Cancel", 0);
    w->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, w] (int r) {
            if (r == 1) {
                auto key = w->getTextEditorContents ("key").trim();
                if (key.isNotEmpty()) {
                    audioProcessor.getLicenseManager().setLicenseKey (key);
                    audioProcessor.getApiClient().setLicenseKey (key);
                    updateLicenseDisplay();
                }
            }
            delete w;
        }));
    lm.markWelcomeShown();
}

void MixMindEditor::updateLicenseDisplay()
{
    auto& lm = audioProcessor.getLicenseManager();
    if (lm.isLicensed()) {
        licenseButton.setButtonText ("LICENSED");
        licenseButton.setColour (juce::TextButton::textColourOffId, JP::accent);
    } else {
        auto r = lm.getFreePromptsRemaining();
        licenseButton.setButtonText (juce::String(r) + " FREE - ENTER KEY");
        licenseButton.setColour (juce::TextButton::textColourOffId, JP::warning);
    }
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

    // The colour swatch only applies to hue-based groups (not Master/cow-print).
    const bool isMaster = (g == FocusModel::Group::Master);
    colorSwatch.setVisible (!isMaster);
    if (!isMaster)
        colorSwatch.setSwatchColour (FocusModel::colorFor (g).saturated);
}

// ── Chat ─────────────────────────────────────────────────────────────────
void MixMindEditor::handleUserMessage (const juce::String& text)
{
    if (waitingForReply || text.trim().isEmpty()) return;

    auto& lm = audioProcessor.getLicenseManager();
    if (!lm.canPrompt()) { showLicenseDialog(); return; }

    chatComponent.addUserMessage (text);
    history.push_back ({ ChatMessage::Role::User, text });
    thinkingBubble = chatComponent.addAIThinking();
    waitingForReply = true;
    chatComponent.setInputEnabled (false);
    repaint();

    juce::String systemPrompt =
        "You are MixMind, an expert mixing engineer. "
        "You receive real-time FFT spectrum data from a DAW plugin. "
        "Give specific, actionable mixing advice. Cite frequencies and dB values. "
        "Be direct. No filler.\n\n"
        "=== LIVE TELEMETRY ===\n"
        "LUFS: " + juce::String (audioProcessor.audioAnalyzer.getLufs(), 1) + "\n"
        "Stereo width: " + juce::String (audioProcessor.audioAnalyzer.getStereoWidth(), 2) + "\n"
        "Phase correlation: " + juce::String (audioProcessor.audioAnalyzer.getPhaseCorr(), 2) + "\n";

    audioProcessor.getApiClient().send (systemPrompt, history,
        [this] (ApiClient::Result result) {
            waitingForReply = false;
            chatComponent.setInputEnabled (true);
            if (result.success) {
                chatComponent.finalizeAI (thinkingBubble, result.text);
                history.push_back ({ ChatMessage::Role::Assistant, result.text });
                audioProcessor.getLicenseManager().recordPrompt();
                updateLicenseDisplay();
            } else {
                chatComponent.finalizeAI (thinkingBubble, "Error: " + result.errorMessage);
            }
            thinkingBubble = nullptr;
            repaint();
        });
}
