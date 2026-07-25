#pragma once
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ─────────────────────────────────────────────────────────────────────────────
//  LicenseManager
//  Tracks free prompt count per machine. First 10 prompts are free,
//  then requires a license key. Data is stored in a local file tied to
//  the hardware — not reusable across devices.
// ─────────────────────────────────────────────────────────────────────────────
class LicenseManager
{
public:
    static constexpr int kFreePromptLimit = 10;

    LicenseManager();

    // Returns true if the user can send a prompt (either within free limit or licensed)
    bool canPrompt() const;

    // Call this AFTER a successful prompt to decrement the free count
    void recordPrompt();

    // License key management
    bool isLicensed() const;
    void setLicenseKey (const juce::String& key);
    juce::String getLicenseKey() const;

    // Free prompts remaining (only meaningful if not licensed)
    int getFreePromptsRemaining() const;
    int getTotalPromptsUsed() const;
    bool hasShownWelcome() const;
    void markWelcomeShown();

    // Returns a status string for the UI
    juce::String getStatus() const;

private:
    void load();
    void save();

    juce::File getDataFile() const;

    int          freePromptsUsed  { 0 };
    juce::String licenseKey;
    bool         welcomeShown     { false };
    juce::String machineId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
