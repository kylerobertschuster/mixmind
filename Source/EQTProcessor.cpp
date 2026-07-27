#include "EQTProcessor.h"
#include "EQTEditor.h"

EQTProcessor::EQTProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

void EQTProcessor::prepareToPlay (double sr, int bs)
{
    sampleRate = sr;
    audioAnalyzer.prepare (sr, bs);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32)bs, 2 };
    chain.prepare (spec);
}

void EQTProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    audioAnalyzer.process (buffer);

    if (eqActive)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        chain.process (ctx);
    }
}

void EQTProcessor::setBands (const std::vector<EQBand>& bands)
{
    for (int i = 0; i < 8; ++i)
    {
        bool bp = true;
        auto setBypass = [&](int idx) {
            switch(idx) { case 0: chain.setBypassed<0>(bp); break; case 1: chain.setBypassed<1>(bp); break; case 2: chain.setBypassed<2>(bp); break; case 3: chain.setBypassed<3>(bp); break; case 4: chain.setBypassed<4>(bp); break; case 5: chain.setBypassed<5>(bp); break; case 6: chain.setBypassed<6>(bp); break; case 7: chain.setBypassed<7>(bp); break; }
        };
        setBypass(i);
    }

    for (size_t i = 0; i < bands.size() && i < 8; ++i)
    {
        auto& b = bands[i];
        if (!b.active) continue;
        Coeffs::Ptr c;
        float g = juce::Decibels::decibelsToGain (b.gain);
        switch (b.type) {
            case 1: c = Coeffs::makeLowShelf  (sampleRate, b.freq, b.q, g); break;
            case 2: c = Coeffs::makeHighShelf (sampleRate, b.freq, b.q, g); break;
            case 3: c = Coeffs::makeHighPass  (sampleRate, b.freq, b.q);    break;
            case 4: c = Coeffs::makeLowPass   (sampleRate, b.freq, b.q);    break;
            default: c = Coeffs::makePeakFilter(sampleRate, b.freq, b.q, g); break;
        }
        auto set = [&](int idx) {
            switch(idx) { case 0: chain.get<0>().coefficients=c; chain.setBypassed<0>(false); break; case 1: chain.get<1>().coefficients=c; chain.setBypassed<1>(false); break; case 2: chain.get<2>().coefficients=c; chain.setBypassed<2>(false); break; case 3: chain.get<3>().coefficients=c; chain.setBypassed<3>(false); break; case 4: chain.get<4>().coefficients=c; chain.setBypassed<4>(false); break; case 5: chain.get<5>().coefficients=c; chain.setBypassed<5>(false); break; case 6: chain.get<6>().coefficients=c; chain.setBypassed<6>(false); break; case 7: chain.get<7>().coefficients=c; chain.setBypassed<7>(false); break; }
        };
        set ((int)i);
    }
    eqActive = !bands.empty();
}

void EQTProcessor::clearBands() { eqActive = false; }

juce::AudioProcessorEditor* EQTProcessor::createEditor() { return new EQTEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EQTProcessor(); }
