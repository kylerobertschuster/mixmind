#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "PresetManager.h"

// ─────────────────────────────────────────────────────────────────────────────
//  StrawPanel  — the "straw" sidebar for track type, genre, focus, presets
// ─────────────────────────────────────────────────────────────────────────────
class StrawPanel : public juce::Component
{
public:
    std::function<void()>                 onQuickPrompt;
    std::function<void(PresetManager::TrackType)> onTrackTypeChanged;
    juce::String                          quickPromptText;

    StrawPanel();
    ~StrawPanel() override = default;

    void paint  (juce::Graphics&) override;
    void resized() override;

    PresetManager& getPresetManager() { return presetManager; }

    // Build system prompt incorporating preset context
    juce::String buildSystemPrompt() const;

private:
    PresetManager presetManager;

    // Track type
    juce::Label    trackLabel   { {}, "TRACK TYPE" };
    juce::ComboBox trackBox;

    // Genre (dynamically populated based on track type)

    // Focus
    juce::Label    focusLabel   { {}, "FOCUS" };
    juce::ComboBox focusBox;

    // Quick prompts

    // Template chain
    juce::Label    chainLabel   { {}, "SIGNAL CHAIN" };
    juce::Label    chainText;

    // Helpers
    void updateChain();
    void addSectionLabel (juce::Label& lbl);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrawPanel)
};
