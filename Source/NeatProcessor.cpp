#include "NeatProcessor.h"
#include "NeatEditor.h"
#include <cmath>

NeatProcessor::NeatProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

void NeatProcessor::prepareToPlay(double rate, int bs)
{
    sr = rate;
    for (int e=0; e<3; ++e) {
        delaySize[e] = (int)(sr * 0.5);
        delayBuf[e].setSize(2,delaySize[e]);
        delayBuf[e].clear();
        delayPos[e] = 0;
    }
}

void NeatProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getNumChannels()>1 ? buffer.getWritePointer(1) : L;

    for (int e=0; e<3; ++e) {
        auto* dL = delayBuf[e].getWritePointer(0);
        auto* dR = delayBuf[e].getWritePointer(1);
        int rpt = (int)repeats[e];
        if (rpt < 1) rpt = 1;
        int delLen = (int)(sr * 0.125f / (e+1)); // 1/8, 1/4, 3/8 note

        for (int s=0; s<n; ++s) {
            // Write input
            dL[delayPos[e]] = L[s];
            dR[delayPos[e]] = R[s];

            // Repeat loop
            float sumL = 0, sumR = 0;
            for (int r=0; r<rpt; ++r) {
                int readPos = (delayPos[e] - delLen*(r+1) + delaySize[e]) % delaySize[e];
                float atten = std::pow(decay[e], (float)(r+1));
                sumL += dL[readPos] * atten;
                sumR += dR[readPos] * atten;
            }

            // Pitch shift via phase accumulation
            float semitones = pitch[e];
            float rate = std::pow(2.0f, semitones/12.0f);
            pitchPhase[e] += rate;
            if (pitchPhase[e] >= delaySize[e]) pitchPhase[e] -= delaySize[e];
            int pRead = (int)pitchPhase[e] % delaySize[e];
            int pNext = (pRead+1) % delaySize[e];
            float frac = pitchPhase[e] - (int)pitchPhase[e];
            float pL = dL[pRead] * (1-frac) + dL[pNext] * frac;
            float pR = dR[pRead] * (1-frac) + dR[pNext] * frac;

            L[s] = L[s] * (1.0f-mix) + (sumL * 0.3f + pL * 0.7f) * mix;
            R[s] = R[s] * (1.0f-mix) + (sumR * 0.3f + pR * 0.7f) * mix;

            // Brickwall limiter — prevent hearing damage
            L[s] = std::tanh (L[s] * 0.85f);
            R[s] = std::tanh (R[s] * 0.85f);

            delayPos[e] = (delayPos[e]+1) % delaySize[e];
        }
    }
}

juce::AudioProcessorEditor* NeatProcessor::createEditor() { return new NeatEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeatProcessor(); }
