#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "ReflexProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class ReflexEditor : public juce::AudioProcessorEditor
{
public:
    ReflexEditor (ReflexProcessor& p);
    ~ReflexEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showLicenseDialog();
    void updateLicenseDisplay();

    ReflexProcessor& proc;
    JuicePipeLAF laf;

    juce::Label titleLabel;
    juce::TextButton licenseButton { "LICENSE" };

    juce::Slider freqSlider, resSlider, mixSlider, delaySlider, fbSlider, reverbSlider, seqSlider;
    juce::Label freqLabel, resLabel, mixLabel, delayLabel, fbLabel, reverbLabel, seqLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReflexEditor)
};
