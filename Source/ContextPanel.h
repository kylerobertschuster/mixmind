#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  SessionContext  — plain data struct passed to the system prompt builder
// ─────────────────────────────────────────────────────────────────────────────
struct SessionContext
{
    juce::String genre;
    juce::String daw;
    juce::String level      { "beginner" };
    juce::String problems;
    juce::String keyBpm;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ChipButton  — a toggle-style chip button
// ─────────────────────────────────────────────────────────────────────────────
class ChipButton : public juce::ToggleButton
{
public:
    explicit ChipButton (const juce::String& label) { setButtonText (label); setClickingTogglesState (true); }
    const juce::String& getValue() const { return getButtonText(); }
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChipButton)
};

// ─────────────────────────────────────────────────────────────────────────────
//  ContextPanel
// ─────────────────────────────────────────────────────────────────────────────
class ContextPanel : public juce::Component
{
public:
    std::function<void()> onQuickPrompt;    // fires with quickPromptText set
    std::function<void(const juce::String&)> onLicenseKeyChanged;
    juce::String          quickPromptText;
    juce::String          licenseKey;

    ContextPanel();
    ~ContextPanel() override = default;

    void paint  (juce::Graphics&) override;
    void resized() override;

    SessionContext getContext() const;

    // Build the system prompt from current context
    juce::String buildSystemPrompt() const;

private:
    // ── Widgets ────────────────────────────────────────────────────────────
    juce::Label    genreLabel   { {}, "GENRE" };
    juce::ComboBox genreBox;

    juce::Label    dawLabel     { {}, "DAW" };
    juce::ComboBox dawBox;

    juce::Label    levelLabel   { {}, "EXPERIENCE" };
    juce::OwnedArray<ChipButton> levelChips;

    juce::Label    problemLabel { {}, "PROBLEM AREAS" };
    juce::OwnedArray<ChipButton> problemChips;

    juce::Label    keyBpmLabel  { {}, "KEY / BPM" };
    juce::TextEditor keyBpmBox;

    juce::Label    quickLabel   { {}, "QUICK PROMPTS" };

    juce::Label    licenseLabel { {}, "LICENSE KEY" };
    juce::TextEditor licenseBox;

    struct QuickPrompt { juce::String label; juce::String prompt; };
    const std::vector<QuickPrompt> quickPrompts {
        { "Kick tips",         "Give me 3 specific, actionable tips for my kick drum right now." },
        { "Fix muddy mix",     "My mix sounds muddy in the low-mids. Diagnose and fix it step by step." },
        { "Vocal placement",   "How do I get my vocals to sit properly in the mix for my genre?" },
        { "Compression guide", "Give me a compression approach tailored to my genre and experience level." },
        { "More energy",       "My track lacks energy and punch. Give me actionable steps to fix it." },
        { "Master bus chain",  "Walk me through an ideal master bus chain for my genre." },
        { "Reference tracks",  "How should I use reference tracks? What specifically should I compare?" },
        { "EQ cheat sheet",    "Give me an EQ cheat sheet for the most common mix problems in my genre." },
    };
    juce::OwnedArray<juce::TextButton> quickBtns;

    // Layout helpers
    void addSectionLabel (juce::Label& lbl);
    static int chipRowHeight (int numChips, int panelWidth, int chipW = 70, int gap = 5);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContextPanel)
};
