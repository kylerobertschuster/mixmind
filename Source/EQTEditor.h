#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EQTProcessor.h"
#include "LookAndFeel.h"
#include "LicenseManager.h"

class EQTEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    EQTEditor (EQTProcessor& p);
    ~EQTEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void drawSpectrum (juce::Graphics& g);
    void drawEQBands  (juce::Graphics& g);
    void drawEQCurve  (juce::Graphics& g);
    void applyBands();
    void showLicenseDialog();
    void updateLicenseDisplay();

    EQTProcessor& proc;
    JuicePipeLAF laf;

    juce::Label titleLabel;
    juce::TextButton licenseButton { "LICENSE" };
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton clearButton { "CLEAR" };

    static constexpr int kNumBins = 512;
    float smoothBins[kNumBins] { 0 };
    float peakHold[kNumBins]   { 0 };
    bool hasSignal { false };

    std::vector<EQTProcessor::EQBand> bands;
    int draggingIdx { -1 };
    int dotPhase { 0 };

    float plotLeft { 44 }, plotRight { 0 }, plotTop { 24 }, plotBottom { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQTEditor)
};
