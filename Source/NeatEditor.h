#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "NeatProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class NeatEditor : public juce::AudioProcessorEditor
{
public:
    NeatEditor(NeatProcessor& p);
    ~NeatEditor() override { setLookAndFeel(nullptr); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void showLicenseDialog();
    void updateLicenseDisplay();
    NeatProcessor& proc;
    JuicePipeLAF laf;
    juce::Label titleLabel;
    juce::TextButton licenseButton{"LICENSE"};
    juce::Slider rpt[3], decayS[3], pitchS[3], mixS;
    juce::Label rptL[3], decayL[3], pitchL[3], mixL;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeatEditor)
};
