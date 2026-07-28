#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ThreeFXProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class ThreeFXEditor : public juce::AudioProcessorEditor
{
public:
    ThreeFXEditor (ThreeFXProcessor& p);
    ~ThreeFXEditor() override { setLookAndFeel(nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void showLicenseDialog();
    void updateLicenseDisplay();
    ThreeFXProcessor& proc;
    JuicePipeLAF laf;
    juce::Label titleLabel;
    juce::Label phaseLabel{{""},"PHASE"}, flangeLabel{{""},"FLANGE"}, delayLabel{{""},"DELAY"};
    juce::TextButton licenseButton{"LICENSE"};
    juce::Slider pRate,pDepth,fRate,fDepth,dTime,dFb,dMix;
    juce::Label pRateL,pDepthL,fRateL,fDepthL,dTimeL,dFbL,dMixL;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreeFXEditor)
};
