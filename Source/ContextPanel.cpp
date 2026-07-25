#include "ContextPanel.h"

StrawPanel::StrawPanel()
{
    // ── Track type ────────────────────────────────────────────────────────
    addSectionLabel (trackLabel);
    for (auto& name : PresetManager::getAllTrackTypeNames())
        trackBox.addItem (name, trackBox.getNumItems() + 1);
    trackBox.setSelectedId (1, juce::dontSendNotification); // Master default
    trackBox.onChange = [this]
    {
        auto idx = trackBox.getSelectedItemIndex();
        auto type = static_cast<PresetManager::TrackType> (idx);
        presetManager.setTrackType (type);
        updateGenreBox();
        updateQuickPrompts();
        updateChain();
        if (onTrackTypeChanged) onTrackTypeChanged (type);
    };
    addAndMakeVisible (trackLabel);
    addAndMakeVisible (trackBox);

    // ── Genre ─────────────────────────────────────────────────────────────
    addSectionLabel (genreLabel);
    updateGenreBox();
    genreBox.setSelectedId (1, juce::dontSendNotification);
    genreBox.onChange = [this]
    {
        presetManager.setGenre (genreBox.getText());
        updateQuickPrompts();
        updateChain();
    };
    addAndMakeVisible (genreLabel);
    addAndMakeVisible (genreBox);

    // ── Focus ─────────────────────────────────────────────────────────────
    addSectionLabel (focusLabel);
    for (auto& name : PresetManager::getAllFocusNames())
        focusBox.addItem (name, focusBox.getNumItems() + 1);
    focusBox.setSelectedId (1, juce::dontSendNotification); // Clean default
    focusBox.onChange = [this]
    {
        auto idx = focusBox.getSelectedItemIndex();
        auto focus = static_cast<PresetManager::Focus> (idx);
        presetManager.setFocus (focus);
        updateChain();
    };
    addAndMakeVisible (focusLabel);
    addAndMakeVisible (focusBox);

    // ── Quick prompts ─────────────────────────────────────────────────────
    addSectionLabel (quickLabel);
    updateQuickPrompts();
    addAndMakeVisible (quickLabel);

    // ── Signal chain ──────────────────────────────────────────────────────
    addSectionLabel (chainLabel);
    chainText.setFont (juce::Font ("Courier New", 11.0f, juce::Font::plain));
    chainText.setColour (juce::Label::textColourId, MM::text2);
    chainText.setJustificationType (juce::Justification::topLeft);
    updateChain();
    addAndMakeVisible (chainLabel);
    addAndMakeVisible (chainText);
}

void StrawPanel::paint (juce::Graphics& g)
{
    g.fillAll (MM::surface);

    // Glass highlight at top of straw
    g.setGradientFill (juce::ColourGradient (
        MM::glass.withAlpha (0.15f), 0, 0,
        MM::glass.withAlpha (0.0f), 0, 40, false));
    g.fillRect (0, 0, getWidth(), 40);

    // Right edge "glass" line — the straw tube
    g.setColour (MM::glassBorder);
    g.drawLine ((float)getWidth() - 0.5f, 0, (float)getWidth() - 0.5f, (float)getHeight(), 0.5f);
}

void StrawPanel::resized()
{
    const int w   = getWidth();
    const int pad = 14;
    const int innerW = w - pad * 2;
    int y = pad;

    auto placeLabel_ = [&](juce::Label& lbl)
    {
        lbl.setBounds (pad, y, innerW, 16);
        y += 18;
    };

    auto placeControl = [&](juce::Component& c, int h)
    {
        c.setBounds (pad, y, innerW, h);
        y += h + 10;
    };

    // Track type
    placeLabel_  (trackLabel);
    placeControl (trackBox, 30);

    // Genre
    placeLabel_  (genreLabel);
    placeControl (genreBox, 30);

    // Focus
    placeLabel_  (focusLabel);
    placeControl (focusBox, 30);

    y += 6;

    // Quick prompts
    placeLabel_ (quickLabel);
    for (auto* btn : quickBtns)
    {
        btn->setBounds (pad, y, innerW, 26);
        y += 30;
    }

    y += 6;

    // Signal chain
    placeLabel_ (chainLabel);
    int chainH = getHeight() - y - pad;
    chainText.setBounds (pad, y, innerW, juce::jmax (40, chainH));
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void StrawPanel::updateGenreBox()
{
    genreBox.clear();
    for (auto& name : PresetManager::getGenresForTrackType (presetManager.getTrackType()))
        genreBox.addItem (name, genreBox.getNumItems() + 1);
    genreBox.setSelectedId (1, juce::dontSendNotification);
    presetManager.setGenre (genreBox.getText());
}

void StrawPanel::updateQuickPrompts()
{
    quickBtns.clear();
    auto prompts = presetManager.getQuickPrompts();

    for (const auto& prompt : prompts)
    {
        auto* btn = quickBtns.add (new juce::TextButton (prompt));
        auto text = prompt;
        btn->onClick = [this, text]
        {
            quickPromptText = text;
            if (onQuickPrompt) onQuickPrompt();
        };
        addAndMakeVisible (btn);
    }
    resized();
}

void StrawPanel::updateChain()
{
    auto chain = presetManager.getTemplateChain();
    chainText.setText (chain.joinIntoString ("\n"), juce::dontSendNotification);
}

juce::String StrawPanel::buildSystemPrompt() const
{
    juce::String sys;
    sys << presetManager.buildPresetPrompt() << "\n\n";
    sys << "RULES:\n"
           "1. Be direct and technical. Skip pleasantries.\n"
           "2. Tailor advice to the track type, genre, and focus.\n"
           "3. Number your tips. Each tip must be discrete and actionable.\n"
           "4. Include specific frequency ranges, ratios, thresholds, or plugin suggestions.\n"
           "5. Diagnose before prescribing — name what is likely causing the problem.\n"
           "6. Dense signal-to-noise. No filler.\n"
           "7. If something depends on taste, say so briefly then give the most common pro approach.\n";
    return sys;
}

void StrawPanel::addSectionLabel (juce::Label& lbl)
{
    lbl.setFont (juce::Font ("Courier New", 10.0f, juce::Font::bold));
    lbl.setColour (juce::Label::textColourId, MM::accentDim);
}
