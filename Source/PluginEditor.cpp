#include "PluginEditor.h"
#include "LicenseManager.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);
    setSize (kEditorW, kEditorH);
    setResizable (true, true);
    setResizeLimits (800, 500, 1600, 1000);

    // ── Header ───────────────────────────────────────────────────────────────
    titleLabel.setText ("MIXMIND  //  JuicePipe", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Courier New", 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, MM::accent);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions ("Courier New", 11.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, MM::text2);
    statusLabel.setJustificationType (juce::Justification::right);
    addAndMakeVisible (statusLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    // ── Straw panel (left sidebar) ───────────────────────────────────────────
    strawPanel.onQuickPrompt = [this]
    {
        handleUserMessage (strawPanel.quickPromptText);
    };
    addAndMakeVisible (strawPanel);

    // ── Chat / bowl area ─────────────────────────────────────────────────────
    chatComponent.onSendMessage = [this] (const juce::String& text)
    {
        handleUserMessage (text);
    };
    addAndMakeVisible (chatComponent);

    startTimerHz (30);

    if (!audioProcessor.getLicenseManager().hasShownWelcome())
        juce::Timer::callAfterDelay (300, [this] { showLicenseDialog(); });
}

MixMindEditor::~MixMindEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ── Layout ────────────────────────────────────────────────────────────────────
void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (MM::bg);

    auto w = (float)getWidth();
    auto h = (float)getHeight();

    // Header bar
    auto header = juce::Rectangle<float> (0, 0, w, (float)MM::headerH);
    g.setColour (MM::surface);
    g.fillRect (header);
    g.setColour (MM::border);
    g.drawLine (header.getBottomLeft().x, header.getBottomLeft().y,
                header.getBottomRight().x, header.getBottomRight().y, 1.0f);

    // Glass highlight
    g.setGradientFill (juce::ColourGradient (
        MM::glass, 0.0f, 0.0f,
        juce::Colours::transparentBlack, 0.0f, (float)MM::headerH, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, w, (float)MM::headerH));

    // Straw tube edge
    auto sidebarX = (float)kSidebarW;
    g.setColour (MM::glassBorder);
    g.drawLine (sidebarX, (float)MM::headerH, sidebarX, h, 1.0f);

    g.setGradientFill (juce::ColourGradient (
        MM::glass.withAlpha (0.08f), sidebarX - 4, 0,
        juce::Colours::transparentBlack, sidebarX, 0, false));
    g.fillRect (sidebarX - 4, (float)MM::headerH, 4.0f, h - MM::headerH);

    // Status dots
    auto rightX = w - 24.0f;
    auto centerY = MM::headerH * 0.5f;

    float dotAlpha = waitingForReply
        ? 0.4f + 0.6f * std::abs (std::sin ((float)dotPhase * 0.1f)) : 0.5f;
    g.setColour (waitingForReply ? MM::accent.withAlpha (dotAlpha) : MM::text3);
    g.fillEllipse (rightX - 120, centerY - 4, 8, 8);

    bool isListening = !audioProcessor.audioAnalyzer.getAnalysisAsJson().contains ("-100");
    float listenAlpha = isListening
        ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.05f)) : 0.2f;
    g.setColour (isListening ? juce::Colours::cyan.withAlpha (listenAlpha) : MM::text3);
    g.fillEllipse (rightX - 140, centerY - 4, 8, 8);
}

void MixMindEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (MM::headerH);
    titleLabel.setBounds (header.withLeft (16).withWidth (300));
    licenseButton.setBounds (header.withLeft (getWidth() - 150).withWidth (100).withHeight (28).withY (7));
    statusLabel.setBounds (header.withLeft (getWidth() - 260).withWidth (100));

    auto sidebar = bounds.removeFromLeft (kSidebarW);
    strawPanel.setBounds (sidebar);
    chatComponent.setBounds (bounds);
}

// ── License dialog ────────────────────────────────────────────────────────────
void MixMindEditor::showLicenseDialog()
{
    auto& lm = audioProcessor.getLicenseManager();

    juce::String msg;
    if (lm.isLicensed())
        msg = "Licensed — unlimited prompts\n\nKey: " + lm.getLicenseKey() + "\n\nEnter a new key to switch:";
    else if (lm.getFreePromptsRemaining() > 0)
        msg = "Welcome to MixMind by JuicePipe!\n\n" + juce::String (lm.getFreePromptsRemaining())
              + " free prompts remaining.\n\nEnter a license key for unlimited access:";
    else
        msg = "You've used all 10 free prompts.\n\nEnter a license key to continue:";

    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::QuestionIcon, "MixMind License", msg, "OK", "Cancel", this,
        juce::ModalCallbackFunction::create (
            [this] (int result)
            {
                if (result == 0) return;
                juce::AlertWindow w ("Enter License Key", "Paste your license key:",
                                     juce::AlertWindow::QuestionIcon);
                w.addTextEditor ("key", audioProcessor.getLicenseManager().getLicenseKey(),
                                 "License key (MM-...)");
                w.addButton ("Activate", 1);
                w.addButton ("Cancel", 0);
                w.enterModalState (true,
                    juce::ModalCallbackFunction::create ([this] (int kr)
                    {
                        if (kr == 1)
                            if (auto* aw = dynamic_cast<juce::AlertWindow*> (
                                    juce::AlertWindow::getCurrentlyModalComponent (false)))
                            {
                                auto key = aw->getTextEditorContents ("key").trim();
                                audioProcessor.getLicenseManager().setLicenseKey (key);
                                audioProcessor.getApiClient().setLicenseKey (key);
                                updateLicenseDisplay();
                            }
                    }), false
                );
            }
        )
    );
    lm.markWelcomeShown();
}

void MixMindEditor::updateLicenseDisplay()
{
    auto& lm = audioProcessor.getLicenseManager();
    if (lm.isLicensed())
    {
        licenseButton.setButtonText ("LICENSED");
        licenseButton.setColour (juce::TextButton::textColourOffId, MM::success);
    }
    else
    {
        auto r = lm.getFreePromptsRemaining();
        licenseButton.setButtonText (juce::String (r) + " FREE");
        licenseButton.setColour (juce::TextButton::textColourOffId, r <= 3 ? MM::warning : MM::accent);
    }
}

// ── Message handling ──────────────────────────────────────────────────────────
void MixMindEditor::handleUserMessage (const juce::String& text)
{
    if (waitingForReply) return;
    if (text.trim().isEmpty()) return;

    auto& lm = audioProcessor.getLicenseManager();
    if (!lm.canPrompt())
    {
        showLicenseDialog();
        return;
    }

    chatComponent.addUserMessage (text);
    history.push_back ({ ChatMessage::Role::User, text });

    thinkingBubble  = chatComponent.addAIThinking();
    waitingForReply = true;
    chatComponent.setInputEnabled (false);
    setStatus ("THINKING");
    repaint();

    auto systemPrompt = strawPanel.buildSystemPrompt();
    systemPrompt += "\n\n=== LIVE AUDIO DATA ===\n";
    systemPrompt += "BPM: " + juce::String (audioProcessor.currentBpm, 1) + "\n";
    systemPrompt += "Time: " + juce::String (audioProcessor.timeSigNumerator)
                    + "/" + juce::String (audioProcessor.timeSigDenominator) + "\n";
    systemPrompt += "Playing: " + juce::String (audioProcessor.isPlaying ? "Yes" : "No") + "\n";
    systemPrompt += "Spectrum: " + audioProcessor.audioAnalyzer.getAnalysisAsJson() + "\n";
    systemPrompt += "Answer based on this live data. Cite specific values.\n";

    audioProcessor.getApiClient().send (systemPrompt, history,
        [this] (ApiClient::Result result)
        {
            waitingForReply = false;
            chatComponent.setInputEnabled (true);

            if (result.success)
            {
                chatComponent.finalizeAI (thinkingBubble, result.text);
                history.push_back ({ ChatMessage::Role::Assistant, result.text });
                setStatus ("READY");
                audioProcessor.getLicenseManager().recordPrompt();
                updateLicenseDisplay();
            }
            else
            {
                chatComponent.finalizeAI (thinkingBubble, "Error: " + result.errorMessage);
                setStatus ("ERROR");
            }

            thinkingBubble = nullptr;
            repaint();
        });
}

void MixMindEditor::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
    repaint();
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    if (waitingForReply) repaint();
}
