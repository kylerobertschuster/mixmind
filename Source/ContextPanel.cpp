#include "ContextPanel.h"

ContextPanel::ContextPanel()
{
    // ── License key (first thing user sees) ─────────────────────────────
    licenseLabel.setFont (juce::Font ("Courier New", 11.0f, juce::Font::plain));
    licenseLabel.setColour (juce::Label::textColourId, MM::text2);
    addAndMakeVisible (licenseLabel);

    licenseBox.setFont (juce::Font ("Courier New", 13.0f, juce::Font::plain));
    licenseBox.setTextToShowWhenEmpty ("MM-XXXXXXXXXXXXXXXX", MM::text3);
    licenseBox.onTextChange = [this]
    {
        licenseKey = licenseBox.getText();
        if (onLicenseKeyChanged) onLicenseKeyChanged (licenseKey);
    };
    addAndMakeVisible (licenseBox);

    // ── Genre ────────────────────────────────────────────────────────────
    addSectionLabel (genreLabel);
    genreBox.addItem ("Hip-Hop / Trap",      1);
    genreBox.addItem ("House / Techno",       2);
    genreBox.addItem ("Lo-fi / Chill",        3);
    genreBox.addItem ("Pop",                  4);
    genreBox.addItem ("R&B / Soul",           5);
    genreBox.addItem ("Drum & Bass",          6);
    genreBox.addItem ("Ambient",              7);
    genreBox.addItem ("Rock / Alternative",   8);
    genreBox.addItem ("Jazz / Neo-soul",      9);
    genreBox.addItem ("Metal",               10);
    genreBox.addItem ("Orchestral / Film",   11);
    genreBox.addItem ("Other",               12);
    genreBox.setSelectedId (0, juce::dontSendNotification);
    genreBox.setTextWhenNothingSelected ("— select genre —");
    addAndMakeVisible (genreLabel);
    addAndMakeVisible (genreBox);

    // ── DAW ──────────────────────────────────────────────────────────────
    dawBox.addItem ("Ableton Live",  1);
    dawBox.addItem ("FL Studio",     2);
    dawBox.addItem ("Logic Pro",     3);
    dawBox.addItem ("Pro Tools",     4);
    dawBox.addItem ("Reaper",        5);
    dawBox.addItem ("Studio One",    6);
    dawBox.addItem ("Bitwig",        7);
    dawBox.addItem ("Cubase",        8);
    dawBox.addItem ("GarageBand",    9);
    dawBox.setSelectedId (0, juce::dontSendNotification);
    dawBox.setTextWhenNothingSelected ("— select DAW —");
    addAndMakeVisible (dawLabel);
    addAndMakeVisible (dawBox);

    // ── Level chips ───────────────────────────────────────────────────────
    addAndMakeVisible (levelLabel);
    for (auto& name : { "Beginner", "Intermediate", "Advanced", "Pro" })
    {
        auto* chip = levelChips.add (new ChipButton (name));
        addAndMakeVisible (chip);
    }
    levelChips[0]->setToggleState (true, juce::dontSendNotification);

    // Single-select behaviour: clear others when one is clicked
    for (int i = 0; i < levelChips.size(); ++i)
    {
        levelChips[i]->onClick = [this, i]
        {
            for (int j = 0; j < levelChips.size(); ++j)
                levelChips[j]->setToggleState (j == i, juce::dontSendNotification);
        };
    }

    // ── Problem chips ─────────────────────────────────────────────────────
    addAndMakeVisible (problemLabel);
    for (auto& name : { "Low End", "Mids", "Highs", "Vocals", "Dynamics", "Stereo", "Glue", "Loudness" })
    {
        auto* chip = problemChips.add (new ChipButton (name));
        addAndMakeVisible (chip);
    }

    // ── Key / BPM ─────────────────────────────────────────────────────────
    keyBpmBox.setFont (juce::Font ("Courier New", 13.0f, juce::Font::plain));
    keyBpmBox.setTextToShowWhenEmpty ("e.g. C minor, 140 BPM", MM::text3);
    addAndMakeVisible (keyBpmLabel);
    addAndMakeVisible (keyBpmBox);


    // ── Quick prompts ─────────────────────────────────────────────────────
    addAndMakeVisible (quickLabel);
    for (const auto& qp : quickPrompts)
    {
        auto* btn = quickBtns.add (new juce::TextButton (qp.label));
        const auto prompt = qp.prompt;
        btn->onClick = [this, prompt]
        {
            quickPromptText = prompt;
            if (onQuickPrompt) onQuickPrompt();
        };
        addAndMakeVisible (btn);
    }
}

void ContextPanel::paint (juce::Graphics& g)
{
    g.fillAll (MM::surface);
}

void ContextPanel::resized()
{
    const int w   = getWidth();
    const int pad = 14;
    const int innerW = w - pad * 2;
    int y = pad;

    auto placeLabel = [&](juce::Label& lbl)
    {
        lbl.setBounds (pad, y, innerW, 16);
        y += 20;
    };

    auto placeControl = [&](juce::Component& c, int h)
    {
        c.setBounds (pad, y, innerW, h);
        y += h + 12;
    };

    auto placeChips = [&](juce::OwnedArray<ChipButton>& chips)
    {
        const int chipGap = 5;
        const int chipH   = 26;
        int x = pad;
        int rowY = y;

        for (auto* chip : chips)
        {
            int chipW = juce::Font (13.0f).getStringWidth (chip->getButtonText()) + 20;
            if (x + chipW > w - pad)
            {
                x = pad;
                rowY += chipH + chipGap;
            }
            chip->setBounds (x, rowY, chipW, chipH);
            x += chipW + chipGap;
        }
        y = rowY + chipH + 12;
    };

    // License key — first thing the user sees
    placeLabel  (licenseLabel);
    placeControl (licenseBox, 30);
    y += 4;

    // Genre
    placeLabel  (genreLabel);
    placeControl (genreBox, 30);

    // DAW
    placeLabel  (dawLabel);
    placeControl (dawBox, 30);

    // Level
    placeLabel  (levelLabel);
    placeChips  (levelChips);

    // Problems
    placeLabel  (problemLabel);
    placeChips  (problemChips);

    // Key / BPM
    placeLabel  (keyBpmLabel);
    placeControl (keyBpmBox, 30);


    // Divider
    y += 4;

    // Quick prompts
    placeLabel (quickLabel);
    for (auto* btn : quickBtns)
    {
        btn->setBounds (pad, y, innerW, 28);
        y += 32;
    }
}

SessionContext ContextPanel::getContext() const
{
    SessionContext ctx;

    ctx.genre  = genreBox.getText();
    ctx.daw    = dawBox.getText();
    ctx.keyBpm = keyBpmBox.getText();

    for (auto* chip : levelChips)
        if (chip->getToggleState())
            ctx.level = chip->getValue().toLowerCase();

    juce::StringArray probs;
    for (auto* chip : problemChips)
        if (chip->getToggleState())
            probs.add (chip->getValue());
    ctx.problems = probs.joinIntoString (", ");

    return ctx;
}

juce::String ContextPanel::buildSystemPrompt() const
{
    auto ctx = getContext();

    juce::String sys;
    sys << "You are MIXMIND — a world-class mix engineer AI assistant embedded inside a producer's DAW plugin. "
           "You give sharp, specific, immediately actionable mixing advice. No filler.\n\n"
           "SESSION CONTEXT:\n"
        << "- Genre: "     << (ctx.genre.isEmpty()   ? "unspecified" : ctx.genre)   << "\n"
        << "- DAW: "       << (ctx.daw.isEmpty()     ? "unspecified" : ctx.daw)     << "\n"
        << "- Level: "     << ctx.level                                               << "\n"
        << "- Problem areas: " << (ctx.problems.isEmpty() ? "not specified" : ctx.problems) << "\n";

    if (ctx.keyBpm.isNotEmpty())
        sys << "- Key/BPM: " << ctx.keyBpm << "\n";

    sys << "\nRULES:\n"
           "1. Be direct and technical. Skip pleasantries.\n"
           "2. Tailor advice to the genre and DAW when mentioned.\n"
           "3. Calibrate depth to level — simple language for beginners, parameter-level detail for advanced/pro.\n"
           "4. Number your tips. Each tip must be discrete and actionable.\n"
           "5. Include specific frequency ranges, ratios, thresholds, or plugin suggestions when relevant.\n"
           "6. Diagnose before prescribing — name what is likely causing the problem.\n"
           "7. Dense signal-to-noise. No filler phrases like 'great question'.\n"
           "8. If something depends on taste, say so briefly then give the most common pro approach.\n";

    return sys;
}

void ContextPanel::addSectionLabel (juce::Label& lbl)
{
    lbl.setFont (juce::Font ("Courier New", 11.0f, juce::Font::plain));
    lbl.setColour (juce::Label::textColourId, MM::text2);
}
