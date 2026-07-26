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
    setResizeLimits (900, 560, 1600, 1000);

    auto fonts = HostTheme::getFonts();

    // ── Header ───────────────────────────────────────────────────────────
    titleLabel.setText ("JuicePipe  \xe2\x80\x94  MixMind", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (fonts.heading, 12.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, JP::text.withAlpha (0.6f));
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions (fonts.ui, 10.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, JP::textMuted);
    statusLabel.setJustificationType (juce::Justification::right);
    setStatus ("READY");
    addAndMakeVisible (statusLabel);

    // Big black license button on pink background
    licenseButton.setButtonText ("ENTER LICENSE KEY");
    licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
    licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    licenseButton.onClick = [this] { showLicenseDialog(); };
    addAndMakeVisible (licenseButton);
    updateLicenseDisplay();

    // ── Analyzer canvas — center, multi-mode telemetry ───────────────────
    addAndMakeVisible (analyzer);

    // ── Straw panel — right ──────────────────────────────────────────────
    strawPanel.onQuickPrompt = [this] { handleUserMessage (strawPanel.quickPromptText); };
    addAndMakeVisible (strawPanel);

    // ── Chat — bottom ────────────────────────────────────────────────────
    chatComponent.onSendMessage = [this] (const juce::String& t) { handleUserMessage (t); };
    addAndMakeVisible (chatComponent);

    startTimerHz (60);

    if (!audioProcessor.getLicenseManager().isLicensed())
        juce::Timer::callAfterDelay (500, [this] { showLicenseDialog(); });
}

MixMindEditor::~MixMindEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ── Layout: header top, analyzer center, chat bottom, presets right ────────
void MixMindEditor::paint (juce::Graphics& g)
{
    g.fillAll (JP::bg);
    auto w = (float)getWidth(), h = (float)getHeight();

    // Header
    g.setColour (JP::surface);
    g.fillRect (0.0f, 0.0f, w, (float)JP::headerH);
    g.setColour (JP::border);
    g.drawLine (0.0f, (float)JP::headerH, w, (float)JP::headerH, 1.0f);

    // Glass highlight
    g.setGradientFill (juce::ColourGradient (
        JP::glassHighlight, 0.0f, 0.0f,
        juce::Colours::transparentBlack, 0.0f, (float)JP::headerH, false));
    g.fillRect (0.0f, 0.0f, w, (float)JP::headerH);

    // Right sidebar divider
    float sx = w - (float)JP::sidebarW;
    g.setColour (JP::border);
    g.drawLine (sx, (float)JP::headerH, sx, h, 0.5f);

    // Status dots
    float dotY = JP::headerH * 0.5f, dotX = w - 50.0f;
    float a = waitingForReply ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.08f)) : 0.4f;
    auto acc = HostTheme::getColors().accent;
    g.setColour (waitingForReply ? acc.withAlpha (a) : JP::textDim);
    g.fillEllipse (dotX, dotY - 3.0f, 6.0f, 6.0f);

    bool listening = !audioProcessor.audioAnalyzer.getAnalysisAsJson().contains ("-100");
    float la = listening ? 0.3f + 0.7f * std::abs (std::sin ((float)dotPhase * 0.04f)) : 0.2f;
    g.setColour (listening ? juce::Colours::cyan.withAlpha (la) : JP::textDim);
    g.fillEllipse (dotX + 16.0f, dotY - 3.0f, 6.0f, 6.0f);
}

void MixMindEditor::resized()
{
    auto b = getLocalBounds();

    // Header
    auto header = b.removeFromTop (JP::headerH);
    titleLabel.setBounds   (header.withLeft (14).withWidth (300));
    licenseButton.setBounds(header.withLeft (getWidth() - 220).withWidth (180).withHeight (28).withY (6));
    statusLabel.setBounds  (header.withLeft (getWidth() - 240).withWidth (90));

    // Right: presets panel
    auto sidebar = b.removeFromRight (JP::sidebarW);
    strawPanel.setBounds (sidebar);

    // Split remaining space: analyzer (top 55%) / chat (bottom 45%)
    auto chatArea = b.removeFromBottom ((int)(b.getHeight() * 0.45f));
    analyzer.setBounds (b);
    chatComponent.setBounds (chatArea);
}

// ── License dialog ────────────────────────────────────────────────────────
void MixMindEditor::showLicenseDialog()
{
    auto& lm = audioProcessor.getLicenseManager();

    juce::String title = "MixMind License";
    juce::String msg;
    if (lm.isLicensed())
        msg = "Licensed. Enter a new key:";
    else if (lm.getFreePromptsRemaining() > 0)
        msg = juce::String (lm.getFreePromptsRemaining()) + " free prompts. Paste key:";
    else
        msg = "All free prompts used. Paste key:";

    juce::AlertWindow w (title, msg, juce::AlertWindow::QuestionIcon, this);
    w.addTextEditor ("key", lm.getLicenseKey(), 300, 24);
    w.addButton ("Activate", 1, juce::KeyPress::returnKey);
    w.addButton ("Cancel", 0, juce::KeyPress::escapeKey);

    if (w.runModalLoop() == 1)
    {
        auto key = w.getTextEditorContents ("key").trim();
        if (key.isNotEmpty())
        {
            lm.setLicenseKey (key);
            audioProcessor.getApiClient().setLicenseKey (key);
            updateLicenseDisplay();
        }
    }

    lm.markWelcomeShown();
}

void MixMindEditor::updateLicenseDisplay()
{
    auto& lm = audioProcessor.getLicenseManager();
    if (lm.isLicensed())
    {
        licenseButton.setButtonText ("LICENSED");
        licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
        licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    }
    else
    {
        auto r = lm.getFreePromptsRemaining();
        licenseButton.setButtonText (juce::String (r) + " FREE  \xe2\x86\x92  ENTER KEY");
        licenseButton.setColour (juce::TextButton::buttonColourId, JP::surfaceRaised);
        licenseButton.setColour (juce::TextButton::textColourOffId, JP::text);
    }
}

// ── Message handling ──────────────────────────────────────────────────────
void MixMindEditor::handleUserMessage (const juce::String& text)
{
    if (waitingForReply) return;
    if (text.trim().isEmpty()) return;

    auto& lm = audioProcessor.getLicenseManager();
    if (!lm.canPrompt()) { showLicenseDialog(); return; }

    chatComponent.addUserMessage (text);
    history.push_back ({ ChatMessage::Role::User, text });

    thinkingBubble  = chatComponent.addAIThinking();
    waitingForReply = true;
    chatComponent.setInputEnabled (false);
    setStatus ("THINKING");
    repaint();

    // Build system prompt with AI Co-Pilot instructions
    auto systemPrompt = strawPanel.buildSystemPrompt();
    systemPrompt += "\n\n=== LIVE TELEMETRY ===\n";
    systemPrompt += audioProcessor.audioAnalyzer.getAnalysisAsJson() + "\n\n";

    // Include user's interactive EQ state
    juce::String eqState = analyzer.getEQStateJson();
    if (eqState != "[]")
    {
        systemPrompt += "EQ STATE (user has placed these interactive EQ points): " + eqState + "\n";
        systemPrompt += "Consider these EQ points in your analysis. The user may want you to refine them.\n\n";
    }

    systemPrompt +=
        "You are JuicePipe Core, an expert C++ DSP audio analyzer.\n"
        "Cross-reference: if phase_correlation < 0 and highs are strong → phase cancellation.\n"
        "If crest_factor < 6dB and true_peak_db > 0 → over-compression/clipping.\n"
        "If sub_bass_energy is high but sub_bass_correlation < 0.85 → low-end phase instability.\n\n"
        "Respond STRICTLY in JSON:\n"
        "{\"summary\":\"...\",\"status_severity\":\"info|warning|critical\","
        "\"eq_suggestions\":[{\"freq_hz\":250,\"recommended_gain_db\":-2.0,\"recommended_q\":1.2,"
        "\"filter_type\":\"Bell\",\"channel\":\"Mid\",\"reason\":\"...\"}],"
        "\"visual_overlay_targets\":[{\"freq_start_hz\":200,\"freq_end_hz\":300,"
        "\"label\":\"Mud\",\"color_hex\":\"#FF5555\"}],"
        "\"phase_warning\":\"nullable\",\"routing_advice\":\"nullable\"}\n"
        "JSON ONLY. No markdown, no conversation.";

    audioProcessor.getApiClient().send (systemPrompt, history,
        [this] (ApiClient::Result result)
        {
            waitingForReply = false;
            chatComponent.setInputEnabled (true);

            if (result.success)
            {
                // Parse structured AI response
                auto analysis = AIAnalysis::fromJson (result.text);
                juce::String display;
                if (analysis.valid)
                {
                    display = analysis.toDisplayText();
                    if (display.isEmpty()) display = result.text;

                    // Push overlays to the analyzer canvas
                    analyzer.setAIAnalysis (analysis);
                }
                else
                {
                    display = result.text;
                }

                chatComponent.finalizeAI (thinkingBubble, display);
                history.push_back ({ ChatMessage::Role::Assistant, display });
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

    // Feed analyzer with live telemetry every frame
    analyzer.updateTelemetry (audioProcessor.audioAnalyzer.getAnalysisAsJson());

    if (waitingForReply) repaint();
}
