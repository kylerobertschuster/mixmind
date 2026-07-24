#include "PluginEditor.h"

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


    startTimerHz (30); // Higher frequency for smoother UI pulses
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
    statusLabel.setBounds (header.withLeft (getWidth() - 200).withWidth (190));

    auto sidebar = bounds.removeFromLeft (kSidebarW);
    contextPanel.setBounds (sidebar);
    chatComponent.setBounds (bounds);
}

// ── Message handling ──────────────────────────────────────────────────────────
void MixMindEditor::handleUserMessage (const juce::String& text)
{
    if (waitingForReply) return;
    if (text.trim().isEmpty()) return;

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
