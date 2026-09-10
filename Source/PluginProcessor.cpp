#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MixMindProcessor::createParameterLayout()
{
    using P = juce::AudioProcessorValueTreeState::Parameter;
    return {
        std::make_unique<juce::AudioParameterBool>   ("shapeEnable", "Shape",  false),
        std::make_unique<juce::AudioParameterChoice> ("shapeMode",   "Mode",   juce::StringArray ("Auto", "Manual"), 0),
        std::make_unique<P>                          ("shapeAmount", "Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f)
    };
}

MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixMindParams", createParameterLayout())
{
}

void MixMindProcessor::prepareToPlay (double sr, int blockSize)
{
    audioAnalyzer.prepare (sr, blockSize);
    shaper.prepare (sr, blockSize);
    reportedLatency = 0;
}

void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Analysis always runs (read side).
    audioAnalyzer.process (buffer);

    // Shaper — the insert path, driven by the automatable Shape parameter.
    const bool shapeOn = parameters.getRawParameterValue ("shapeEnable")->load() > 0.5f;
    shaper.setEnabled (shapeOn);

    const int lat = shapeOn ? ShaperProcessor::kLatency : 0;
    if (lat != reportedLatency)
    {
        setLatencySamples (lat);
        reportedLatency = lat;
    }

    if (shapeOn)
    {
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getNumChannels() >= 2 ? buffer.getWritePointer (1) : L;
        shaper.process (L, R, L, R, buffer.getNumSamples());
    }
}

juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}

void MixMindProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MixMindProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
