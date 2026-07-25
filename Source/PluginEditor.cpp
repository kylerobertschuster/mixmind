#include "PluginEditor.h"
#include "LicenseManager.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);
    setSize (kEditorW, kEditorH);
    setResizable (true, true);
    setResizeLimits (700, 460, 1400, 900);

    // ── Header ───────────────────────────────────────────────────────────────
    titleLabel.setText ("MIXMIND  //  AI MIXING ASSISTANT", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions ("Courier New", 13.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, MM::accent);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("READY", juce::dontSendNotification);
    statusLabel.setFont (juce::FontOptions ("Courier New", 11.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, MM::text2);
    statusLabel.setJustificationType (juce::Justification::right);
    addAndMakeVisible (statusLabel);

    // License / settings button in the header
    settingsButton.setColour (juce::TextButton::buttonColourId, MM::surface);
    settingsButton.setColour (juce::TextButton::textColourOffId, MM::accent);
    settingsButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (settingsButton);

    // ── Context panel ─────────────────────────────────────────────────────────
    contextPanel.onQuickPrompt = [this]
    {
        handleUserMessage (contextPanel.quickPromptText);
    };
    addAndMakeVisible (contextPanel);

    // ── Chat ──────────────────────────────────────────────────────────────────
    chatComponent.onSendMessage = [this] (const juce::String& text)
    {
        handleUserMessage (text);
    };
    addAndMakeVisible (chatComponent);

    startTimerHz (30);

    // Show welcome dialog on first launch
    if (!audioProcessor.getLicenseManager().hasShownWelcome())
    {
        juce::Timer::callAfterDelay (200, [this] { showLicenseDialog(); });
    }
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

    // Header bar
    auto header = getLocalBounds().removeFromTop (kHeaderH).toFloat();
    g.setColour (MM::surface);
    g.fillRect (header);
    g.setColour (MM::border);
    g.drawLine (juce::Line<float> (header.getBottomLeft(), header.getBottomRight()), 1.0f);

    // Sidebar divider
    g.setColour (MM::border);
    g.drawLine ((float)kSidebarW, (float)kHeaderH,
                (float)kSidebarW, (float)getHeight(), 1.0f);

    // Status Dots
    auto rightX = (float)getWidth() - 24.0f;
    auto centerY = (float)kHeaderH * 0.5f;

    // 1. Thinking / API Status Dot
    float dotAlpha = waitingForReply
                     ? 0.4f + 0.6f * std::abs (std::sin ((float)dotPhase * 0.1f))
                     : 0.5f;
    g.setColour (waitingForReply ? MM::accent.withAlpha (dotAlpha) : MM::text3);
    g.fillEllipse (rightX - 80, centerY - 4, 8, 8);

    // 2. Audio Listening Dot (Pulses when signal is > -60dB)
    bool isListening = audioProcessor.audioAnalyzer.getAnalysisAsJson().contains ("-100") == false;
    float listenAlpha = isListening
                        ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.05f))
                        : 0.2f;
    g.setColour (isListening ? juce::Colours::cyan.withAlpha (listenAlpha) : MM::text3);
    g.fillEllipse (rightX - 100, centerY - 4, 8, 8);
}

void MixMindEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderH);

    titleLabel.setBounds (header.withLeft (16).withWidth (380));
    settingsButton.setBounds (header.withLeft (getWidth() - 168).withWidth (90).withHeight (28).withY (10));
    statusLabel.setBounds (header.withLeft (getWidth() - 260).withWidth (90));

    auto sidebar = bounds.removeFromLeft (kSidebarW);
    contextPanel.setBounds (sidebar);
    chatComponent.setBounds (bounds);
}

// ── License dialog ────────────────────────────────────────────────────────────
void MixMindEditor::showLicenseDialog()
{
    auto& lm = audioProcessor.getLicenseManager();

    juce::String message;

    if (lm.isLicensed())
    {
        message = "✓ Licensed — unlimited prompts\n\n"
                  "Your key: " + lm.getLicenseKey() + "\n\n"
                  "Enter a new key to switch:";
    }
    else if (lm.getFreePromptsRemaining() > 0)
    {
        message = "Welcome to MixMind!\n\n"
                  "You have " + juce::String (lm.getFreePromptsRemaining())
                  + " free prompts remaining.\n\n"
                  "Enter a license key for unlimited access:";
    }
    else
    {
        message = "You've used all 10 free prompts.\n\n"
                  "Enter a license key to continue:";
    }

    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::QuestionIcon,
        "MixMind License",
        message,
        "OK",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create (
            [this, &lm = audioProcessor.getLicenseManager()] (int result)
            {
                if (result == 0) return; // Cancel
                
                // Prompt the user for a license key via another dialog
                juce::AlertWindow w ("Enter License Key",
                                     "Paste your license key below:",
                                     juce::AlertWindow::QuestionIcon);
                w.addTextEditor ("key", lm.getLicenseKey(), "License key (MM-...)");
                w.addButton ("Activate", 1);
                w.addButton ("Cancel", 0);

                w.enterModalState (true,
                    juce::ModalCallbackFunction::create (
                        [this] (int keyResult)
                        {
                            if (keyResult == 1)
                            {
                                if (auto* aw = dynamic_cast<juce::AlertWindow*> (
                                        juce::AlertWindow::getCurrentlyModalComponent (false)))
                                {
                                    auto key = aw->getTextEditorContents ("key").trim();
                                    audioProcessor.getLicenseManager().setLicenseKey (key);
                                    audioProcessor.getApiClient().setLicenseKey (key);
                                    updateLicenseDisplay();
                                }
                            }
                        }
                    ),
                    false
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
        settingsButton.setButtonText ("LICENSED ✓");
        settingsButton.setColour (juce::TextButton::textColourOffId,
                                   juce::Colours::limegreen);
    }
    else
    {
        auto remaining = lm.getFreePromptsRemaining();
        settingsButton.setButtonText (juce::String (remaining) + " FREE");
        settingsButton.setColour (juce::TextButton::textColourOffId,
                                   remaining <= 3 ? juce::Colours::orange : MM::accent);
    }
}

// ── Message handling ──────────────────────────────────────────────────────────
void MixMindEditor::handleUserMessage (const juce::String& text)
{
    if (waitingForReply) return;
    if (text.trim().isEmpty()) return;

    auto& lm = audioProcessor.getLicenseManager();

    // Gate: check license / free prompts
    if (!lm.canPrompt())
    {
        showLicenseDialog();
        return;
    }

    auto ctx = contextPanel.getContext();

    // Save context to processor state
    audioProcessor.savedGenre  = ctx.genre;
    audioProcessor.savedDaw    = ctx.daw;

    // Add user bubble
    chatComponent.addUserMessage (text);
    history.push_back ({ ChatMessage::Role::User, text });

    // Show thinking state
    thinkingBubble  = chatComponent.addAIThinking();
    waitingForReply = true;
    chatComponent.setInputEnabled (false);
    setStatus ("THINKING", true);
    repaint();

    // Fire API request
    auto systemPrompt = contextPanel.buildSystemPrompt();

    // Inject Audio Analysis & Transport data
    systemPrompt += "\n\n=== OBJECTIVE AUDIO DATA ===\n";
    systemPrompt += "BPM: " + juce::String (audioProcessor.currentBpm, 1) + "\n";
    systemPrompt += "Time Signature: " + juce::String (audioProcessor.timeSigNumerator) + "/" + juce::String (audioProcessor.timeSigDenominator) + "\n";
    systemPrompt += "Is Playing: " + juce::String (audioProcessor.isPlaying ? "Yes" : "No") + "\n";
    systemPrompt += "Spectral Analysis Snapshot: " + audioProcessor.audioAnalyzer.getAnalysisAsJson() + "\n";
    systemPrompt += "Use this live tracking data to accurately answer any questions the user has about their mix. Cite the specific dB values if helpful.\n";

    audioProcessor.getApiClient().send (systemPrompt, history,
        [this] (ApiClient::Result result)
        {
            // Back on the message thread
            waitingForReply = false;
            chatComponent.setInputEnabled (true);

            if (result.success)
            {
                chatComponent.finalizeAI (thinkingBubble, result.text);
                history.push_back ({ ChatMessage::Role::Assistant, result.text });
                setStatus ("READY");

                // Count this prompt toward the free limit (only if not licensed)
                audioProcessor.getLicenseManager().recordPrompt();
                updateLicenseDisplay();
            }
            else
            {
                chatComponent.finalizeAI (thinkingBubble,
                    "Error: " + result.errorMessage);
                setStatus ("ERROR");
            }

            thinkingBubble = nullptr;
            repaint();
        });
}

void MixMindEditor::setStatus (const juce::String& text, bool /*live*/)
{
    statusLabel.setText (text, juce::dontSendNotification);
    repaint();
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;
    if (waitingForReply) repaint();
}
