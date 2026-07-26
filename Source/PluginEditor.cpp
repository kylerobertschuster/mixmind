#include "PluginEditor.h"
#include "LicenseManager.h"
#include "HostTheme.h"
#include "AIAnalysis.h"

MixMindEditor::MixMindEditor (MixMindProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);
    setSize (JP::editorW, JP::editorH);
    setResizable (true, true);
    setResizeLimits (900, 500, 1600, 1000);

    // ── Header ───────────────────────────────────────────────────────────────
    auto fonts = HostTheme::getFonts();
    auto hostAccent = HostTheme::getColors().accent;

    titleLabel.setText ("JuicePipe  \xe2\x80\x94  MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (fonts.heading, 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.6f));
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions (fonts.ui, 10.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, JP::textMuted);
    statusLabel.setJustificationType (juce::Justification::right);
    setStatus ("READY");
    addAndMakeVisible (statusLabel);

    licenseButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    updateLicenseDisplay();
    addAndMakeVisible (licenseButton);

    // ── Pipe visualizer — left ───────────────────────────────────────────────
    addAndMakeVisible (pipeVis);

    // ── Straw panel — right ──────────────────────────────────────────────────
    strawPanel.onQuickPrompt = [this]
    {
        handleUserMessage (strawPanel.quickPromptText);
    };
    addAndMakeVisible (strawPanel);

    // ── Chat — center ─────────────────────────────────────────────────────────
    chatComponent.onSendMessage = [this] (const juce::String& text)
    {
        handleUserMessage (text);
    };
    addAndMakeVisible (chatComponent);

    startTimerHz (60); // smooth pipe animation

    if (!audioProcessor.getLicenseManager().hasShownWelcome())
        juce::Timer::callAfterDelay (400, [this] { showLicenseDialog(); });
}

MixMindEditor::~MixMindEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ── Layout ────────────────────────────────────────────────────────────────────
void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);

    auto w = (float)getWidth();
    auto h = (float)getHeight();

    // ── Header bar ──────────────────────────────────────────────────────────
    auto header = juce::Rectangle<float> (0, 0, w, (float)JP::headerH);
    g.setColour (JP::surface);
    g.fillRect (header);
    g.setColour (JP::border);
    g.drawLine (header.getX(), header.getBottom(), header.getRight(), header.getBottom(), 1.0f);

    // Subtle glass highlight at top
    g.setGradientFill (juce::ColourGradient (
        JP::glassHighlight, 0.0f, 0.0f,
        juce::Colours::transparentBlack, 0.0f, (float)JP::headerH, false));
    g.fillRect (header);

    // ── Panel dividers — razor-thin vertical lines ──────────────────────────
    float pipeRight = (float)JP::pipeW;
    float sidebarX = w - (float)JP::sidebarW;

    g.setColour (JP::border);
    g.drawLine (pipeRight, (float)JP::headerH, pipeRight, h, 0.5f);
    g.drawLine (sidebarX, (float)JP::headerH, sidebarX, h, 0.5f);

    // ── Status dots (top right of header) ───────────────────────────────────
    float dotY = (float)JP::headerH * 0.5f;
    float dotX = w - 50.0f;

    // Thinking / API pulse
    float a = waitingForReply
        ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.08f)) : 0.4f;
    g.setColour (waitingForReply ? HostTheme::getColors().accent.withAlpha (a) : JP::textDim);
    g.fillEllipse (dotX, dotY - 3.0f, 6.0f, 6.0f);

    // Audio signal presence
    bool listening = !audioProcessor.audioAnalyzer.getAnalysisAsJson().contains ("-100");
    float la = listening ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.04f)) : 0.2f;
    g.setColour (listening ? juce::Colours::cyan.withAlpha (la) : JP::textDim);
    g.fillEllipse (dotX + 16.0f, dotY - 3.0f, 6.0f, 6.0f);
}

void MixMindEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header
    auto header = bounds.removeFromTop (JP::headerH);
    titleLabel.setBounds (header.withLeft (14).withWidth (300));
    licenseButton.setBounds (header.withLeft (getWidth() - 140).withWidth (90)
                             .withHeight (24).withY (8));
    statusLabel.setBounds (header.withLeft (getWidth() - 240).withWidth (90));

    // Right: presets panel
    auto sidebar = bounds.removeFromRight (JP::sidebarW);
    strawPanel.setBounds (sidebar);

    // Left: pipe visualizer
    auto pipeArea = bounds.removeFromLeft (JP::pipeW);
    pipeVis.setBounds (pipeArea);

    // Center: chat
    chatComponent.setBounds (bounds);
}

// ── License dialog ────────────────────────────────────────────────────────────
void MixMindEditor::showLicenseDialog()
{
    auto& lm = audioProcessor.getLicenseManager();

    juce::String msg;
    if (lm.isLicensed())
        msg = "Licensed \xe2\x80\x94 unlimited prompts\n\nKey: " + lm.getLicenseKey()
              + "\n\nEnter a new key to switch:";
    else if (lm.getFreePromptsRemaining() > 0)
        msg = "Welcome to JuicePipe MixMind.\n\n" + juce::String (lm.getFreePromptsRemaining())
              + " free prompts remaining.\n\nEnter a license key for unlimited access:";
    else
        msg = "All free prompts used.\n\nEnter a license key to continue:";

    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::QuestionIcon, "MixMind License", msg, "OK", "Cancel", this,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 0) return;
            juce::AlertWindow w ("Enter License Key", "Paste your license key:",
                                 juce::AlertWindow::QuestionIcon);
            w.addTextEditor ("key", audioProcessor.getLicenseManager().getLicenseKey(),
                             "MM-XXXXXXXXXXXXXXXX");
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
        })
    );
    lm.markWelcomeShown();
}

void MixMindEditor::updateLicenseDisplay()
{
    auto& lm = audioProcessor.getLicenseManager();
    if (lm.isLicensed())
    {
        licenseButton.setButtonText ("LICENSED");
        licenseButton.setColour (juce::TextButton::textColourOffId, JP::success);
    }
    else
    {
        auto r = lm.getFreePromptsRemaining();
        licenseButton.setButtonText (juce::String (r) + " FREE");
        licenseButton.setColour (juce::TextButton::textColourOffId,
                                 r <= 3 ? JP::warning : JP::accent());
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
    systemPrompt += "\n\n=== LIVE TELEMETRY ===\n";
    systemPrompt += audioProcessor.audioAnalyzer.getAnalysisAsJson() + "\n\n";
    systemPrompt +=
        "You are JuicePipe Core, an expert C++ DSP audio analyzer and mixing assistant.\n"
        "You receive structured telemetry (FFT spectral energy, Goniometer phase, Metering) "
        "and return surgical mix corrections.\n\n"
        "CRITICAL RULES:\n"
        "1. Cross-reference telemetry: if phase_correlation < 0 in high frequencies, check for phase \n"
        "   cancellation. If crest_factor < 6dB and true_peak_db > 0dB, detect over-compression.\n"
        "   If sub_bass_energy is high but sub_bass_correlation < 0.85, warn about low-end instability.\n"
        "2. Be specific: cite exact Hz ranges and dB values from the telemetry.\n"
        "3. Suggest EQ moves with precise freq_hz, recommended_gain_db, recommended_q, filter_type, channel.\n"
        "4. Identify frequency zones for visual overlay with freq_start_hz, freq_end_hz, label.\n"
        "5. If phase_correlation < 0.2, provide specific phase_warning advice.\n"
        "6. If mid/side imbalance detected, give routing_advice.\n\n"
        "RESPOND in valid JSON matching this schema:\n"
        "{\n"
        "  \"summary\": \"Short 1-sentence diagnostic.\",\n"
        "  \"status_severity\": \"info|warning|critical\",\n"
        "  \"eq_suggestions\": [{\"freq_hz\": 250, \"recommended_gain_db\": -2.0, \"recommended_q\": 1.2, \"filter_type\": \"Bell\", \"channel\": \"Mid\", \"reason\": \"...\"}],\n"
        "  \"visual_overlay_targets\": [{\"freq_start_hz\": 200, \"freq_end_hz\": 300, \"label\": \"Mud Build-Up\", \"color_hex\": \"#FF5555\"}],\n"
        "  \"phase_warning\": \"optional phase advice or null\",\n"
        "  \"routing_advice\": \"optional routing advice or null\"\n"
        "}\n"
        "Output the JSON payload ONLY. No markdown, no conversational text.";

    audioProcessor.getApiClient().send (systemPrompt, history,
        [this] (ApiClient::Result result)
        {
            waitingForReply = false;
            chatComponent.setInputEnabled (true);

            if (result.success)
            {
                // Try to parse as structured AI analysis first
                auto analysis = AIAnalysis::fromJson (result.text);
                juce::String displayText;

                if (analysis.valid && analysis.statusSeverity.isNotEmpty())
                {
                    displayText = analysis.toDisplayText();
                    // If display text is empty, fall back to raw
                    if (displayText.isEmpty())
                        displayText = result.text;
                }
                else
                {
                    displayText = result.text;
                }

                chatComponent.finalizeAI (thinkingBubble, displayText);
                history.push_back ({ ChatMessage::Role::Assistant, displayText });
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
}

void MixMindEditor::timerCallback()
{
    ++dotPhase;

    // Feed pipe visualizer with real-time audio data
    pipeVis.updateFromJson (audioProcessor.audioAnalyzer.getAnalysisAsJson());

    if (waitingForReply)
        repaint();
}
