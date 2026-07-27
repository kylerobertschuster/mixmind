#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MeterProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class MeterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    MeterEditor (MeterProcessor& p);
    ~MeterEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeterBar (juce::Graphics& g, float x, float y, float w, float h,
                       float& animVal, float target, float min, float max,
                       const juce::String& label, const juce::String& unit,
                       juce::Colour col, bool showPeak = false);
    void showLicenseDialog();
    void updateLicenseDisplay();

    MeterProcessor& proc;
    JuicePipeLAF laf;

    juce::Label titleLabel;
    juce::TextButton licenseButton { "LICENSE" };

    float aLufs  { -60 }, aPeak  { -60 }, aCrest { 0 },
          aPhase { 1 },   aStereo{ 0.5f }, aBass { 0 },
          aMid   { 0 },   aHigh  { 0 };

    int dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterEditor)
};
