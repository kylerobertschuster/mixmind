#include "PluginEditor.h"
#include "LicenseManager.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);
    setSize (1050, 680);
    setResizable (true, true);
    setResizeLimits (700, 400, 1600, 1000);

    // Header
    titleLabel.setText ("JuicePipe - MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.5f));
    addAndMakeVisible (titleLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    // Spectrum
    addAndMakeVisible (analyzer);

    // Presets sidebar
    strawPanel.onQuickPrompt = [this] { handleUserMessage (strawPanel.quickPromptText); };
    addAndMakeVisible (strawPanel);

    // Chat
    chatComponent.onSendMessage = [this] (const juce::String& t) { handleUserMessage (t); };
    addAndMakeVisible (chatComponent);

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
    titleLabel.setBounds (header.withLeft (14).withWidth (300));
    licenseButton.setBounds (header.withLeft (getWidth() - 160).withWidth (130).withHeight (26).withY (7));

    auto sidebar = b.removeFromRight (JP::sidebarW);
    strawPanel.setBounds (sidebar);

    auto chatArea = b.removeFromBottom ((int)(b.getHeight() * 0.42f));
    analyzer.setBounds (b);
    chatComponent.setBounds (chatArea);
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    analyzer.updateBins (audioProcessor.audioAnalyzer.getFFTBins(),
                         AudioAnalyzer::numBins);
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
