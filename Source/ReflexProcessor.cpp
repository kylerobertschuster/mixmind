#include "ReflexProcessor.h"
#include "ReflexEditor.h"
#include <cmath>

ReflexProcessor::ReflexProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

void ReflexProcessor::prepareToPlay (double sr, int bs)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32)bs, 2 };

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, resFreq, resonance);
    resonatorL.coefficients = coeffs;
    resonatorR.coefficients = coeffs;
    resonatorL.prepare (spec);
    resonatorR.prepare (spec);

    delaySize = (int)(sr * 2.0);
    delayBuffer.setSize (2, delaySize);
    delayBuffer.clear();
    delayWritePos = 0;

    for (auto& c : combL) { c.idx = 0; c.fb = 0.5f; juce::zeromem (c.buffer, sizeof(c.buffer)); }
    for (auto& c : combR) { c.idx = 0; c.fb = 0.5f; juce::zeromem (c.buffer, sizeof(c.buffer)); }
}

void ReflexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    // Update resonator frequency from sequence
    float noteFreq = resFreq * std::pow (2.0f, sequence[seqStep] / 12.0f);
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, noteFreq, resonance);
    resonatorL.coefficients = coeffs;
    resonatorR.coefficients = coeffs;

    // Transient detection
    float rms = buffer.getRMSLevel (0, 0, n);
    envelope = envelope * 0.95f + rms * 0.05f;
    samplesSinceTrigger += n;
    if (rms > envelope * 2.0f && samplesSinceTrigger > (int)(sampleRate * seqRate))
    {
        seqStep = (seqStep + 1) % kSeqLen;
        samplesSinceTrigger = 0;
    }

    // Process: dry -> resonator -> delay -> reverb
    juce::AudioBuffer<float> wet (2, n);
    wet.copyFrom (0, 0, buffer, 0, 0, n);
    if (buffer.getNumChannels() > 1) wet.copyFrom (1, 0, buffer, 1, 0, n);

    // Resonator
    {
        juce::dsp::AudioBlock<float> block (wet);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        if (buffer.getNumChannels() > 1) { resonatorL.process (ctx); resonatorR.process (ctx); }
        else resonatorL.process (ctx);
    }

    // Delay
    {
        int delaySamps = (int)(delayTime * sampleRate);
        if (delaySamps >= delaySize) delaySamps = delaySize - 1;
        auto* dL = delayBuffer.getWritePointer (0);
        auto* dR = delayBuffer.getWritePointer (1);
        auto* wL = wet.getWritePointer (0);
        auto* wR = wet.getNumChannels() > 1 ? wet.getWritePointer (1) : wL;

        for (int s = 0; s < n; ++s)
        {
            int readPos = (delayWritePos - delaySamps + delaySize) % delaySize;
            float dl = dL[readPos];
            float dr = dR[readPos];
            dL[delayWritePos] = wL[s] + dl * feedback;
            dR[delayWritePos] = wR[s] + dr * feedback;
            wL[s] = wL[s] * (1.0f - feedback) + dl * feedback;
            wR[s] = wR[s] * (1.0f - feedback) + dr * feedback;
            delayWritePos = (delayWritePos + 1) % delaySize;
        }
    }

    // Simple reverb (4 comb filters per channel)
    if (reverbAmt > 0.01f)
    {
        float rv = reverbAmt * 0.3f;
        for (int s = 0; s < n; ++s)
        {
            float sumL = 0, sumR = 0;
            int lengths[] = { 1116, 1188, 1277, 1356 };
            for (int c = 0; c < 4; ++c)
            {
                auto& cl = combL[c]; auto& cr = combR[c];
                cl.buffer[cl.idx] = wet.getSample (0, s) * rv + cl.buffer[cl.idx] * cl.fb;
                sumL += cl.buffer[cl.idx]; cl.idx = (cl.idx + 1) % lengths[c];
                if (buffer.getNumChannels() > 1)
                {
                    cr.buffer[cr.idx] = wet.getSample (1, s) * rv + cr.buffer[cr.idx] * cr.fb;
                    sumR += cr.buffer[cr.idx]; cr.idx = (cr.idx + 1) % lengths[c];
                }
            }
            lpL = lpL * 0.7f + sumL * 0.3f;
            wet.setSample (0, s, wet.getSample (0, s) + lpL);
            if (buffer.getNumChannels() > 1) { lpR = lpR * 0.7f + sumR * 0.3f; wet.setSample (1, s, wet.getSample (1, s) + lpR); }
        }
    }

    // Mix dry/wet
    for (int s = 0; s < n; ++s)
    {
        L[s] = L[s] * (1.0f - mix) + wet.getSample (0, s) * mix;
        if (buffer.getNumChannels() > 1) R[s] = R[s] * (1.0f - mix) + wet.getSample (1, s) * mix;
    }
}

juce::AudioProcessorEditor* ReflexProcessor::createEditor() { return new ReflexEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReflexProcessor(); }
