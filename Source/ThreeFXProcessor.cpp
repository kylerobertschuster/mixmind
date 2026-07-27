#include "ThreeFXProcessor.h"
#include "ThreeFXEditor.h"
#include <cmath>

ThreeFXProcessor::ThreeFXProcessor()
    : AudioProcessor (BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

void ThreeFXProcessor::prepareToPlay (double rate, int bs)
{
    sr = rate;
    delaySize = (int)(sr * 2.0);
    delayBuf.setSize (2, delaySize);
    delayBuf.clear();
    delayPos = 0;
}

void ThreeFXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getNumChannels()>1 ? buffer.getWritePointer(1) : L;
    auto* dL = delayBuf.getWritePointer(0);
    auto* dR = delayBuf.getWritePointer(1);

    for (int s = 0; s < n; ++s)
    {
        float dryL = L[s], dryR = R[s];
        float wetL = dryL, wetR = dryR;

        // Phase shifter (all-pass filter modulated by LFO)
        phaseLFO += phaseRate * 0.05f;
        if (phaseLFO > juce::MathConstants<float>::twoPi) phaseLFO -= juce::MathConstants<float>::twoPi;
        float phShift = std::sin(phaseLFO) * phaseDepth * 0.5f + 0.5f;
        wetL = wetL * (1.0f - phShift) + wetL * phShift * 0.8f; // crude all-pass
        wetR = wetR * (1.0f - phShift) + wetR * phShift * 0.8f;

        // Flanger (short modulated delay)
        flangeLFO += flangeRate * 0.05f;
        if (flangeLFO > juce::MathConstants<float>::twoPi) flangeLFO -= juce::MathConstants<float>::twoPi;
        int flSamps = (int)((std::sin(flangeLFO) * flangeDepth + flangeDepth) * sr * 0.005f) + 1;
        int flRead = (delayPos - flSamps + delaySize) % delaySize;
        wetL = wetL * 0.5f + dL[flRead] * 0.5f;
        wetR = wetR * 0.5f + dR[flRead] * 0.5f;

        // Write flanger feedback to delay buffer
        dL[delayPos] = wetL;
        dR[delayPos] = wetR;

        // Digital delay
        int delSamps = (int)(delayTime * sr);
        if (delSamps >= delaySize) delSamps = delaySize - 1;
        int delRead = (delayPos - delSamps + delaySize) % delaySize;
        wetL = wetL * (1.0f - delayFb) + dL[delRead] * delayFb;
        wetR = wetR * (1.0f - delayFb) + dR[delRead] * delayFb;
        dL[delayPos] += wetL * delayFb;
        dR[delayPos] += wetR * delayFb;

        delayPos = (delayPos + 1) % delaySize;

        // Mix
        L[s] = dryL * (1.0f - mix) + wetL * mix;
        R[s] = dryR * (1.0f - mix) + wetR * mix;
    }
}

juce::AudioProcessorEditor* ThreeFXProcessor::createEditor() { return new ThreeFXEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ThreeFXProcessor(); }
