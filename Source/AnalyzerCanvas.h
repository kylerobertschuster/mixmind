#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AnalyzerCanvas  — real FFT spectrum display only
// ─────────────────────────────────────────────────────────────────────────────
class AnalyzerCanvas : public juce::Component,
                       private juce::Timer
{
public:
    AnalyzerCanvas();
    ~AnalyzerCanvas() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Feed real FFT bin data directly
    void updateBins (const float* bins, int numBins);

private:
    void timerCallback() override;
    void drawGrid (juce::Graphics& g);
    void drawSpectrum (juce::Graphics& g);

    static constexpr int kNumBins = 1024;
    float smoothBins[kNumBins] { 0 };
    float targetBins[kNumBins] { 0 };
    float peakHold[kNumBins]   { 0 };

    bool hasSignal { false };

    float plotLeft   { 40 };
    float plotRight  { 0 };
    float plotTop    { 24 };
    float plotBottom { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerCanvas)
};
