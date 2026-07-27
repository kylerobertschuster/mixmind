#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ScopeProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class ScopeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    ScopeEditor (ScopeProcessor& p);
    ~ScopeEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawGoniometer (juce::Graphics& g);
    void drawReadouts (juce::Graphics& g);
    void showLicenseDialog();
    void updateLicenseDisplay();

    ScopeProcessor& proc;
    JuicePipeLAF laf;

    juce::Label titleLabel;
    juce::TextButton licenseButton { "LICENSE" };

    static constexpr int gonioSize = 512;
    float gonioL[gonioSize] { 0 };
    float gonioR[gonioSize] { 0 };
    int gonioIdx { 0 };
    int gonioCount { 0 };

    int dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeEditor)
};
