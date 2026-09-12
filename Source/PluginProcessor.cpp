#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MixMindProcessor::createParameterLayout()
{
    using P = juce::AudioProcessorValueTreeState::Parameter;
    return {
        std::make_unique<juce::AudioParameterBool>   ("shapeEnable", "Shape",  false),
        std::make_unique<juce::AudioParameterChoice> ("shapeMode",   "Mode",   juce::StringArray ("Auto", "Manual"), 0),
        std::make_unique<P>                          ("shapeAmount", "Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f),

        // Which part of the stereo signal the shaper matches. Saved with the
        // project (losing an M/S setting on reload is not acceptable), but not
        // automatable: a change re-aims the FIR design, which is a GUI-thread
        // job, and stepping it per automation sample would only queue redesigns
        // the audio thread can never keep up with.
        std::make_unique<juce::AudioParameterChoice> ("channelMode", "Channel",
                                                      channelModeChoices(), 0,
                                                      juce::AudioParameterChoiceAttributes().withAutomatable (false))
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

    // Channel mode re-aims both the analysis and the FIR. Read from the parameter
    // rather than from the editor, so the plugin routes identically with no UI
    // open and comes back right after a session reload.
    const int modeIdx = juce::jlimit (0, (int) allChannelModes().size() - 1,
                                      juce::roundToInt (parameters.getRawParameterValue ("channelMode")->load()));
    const auto mode = allChannelModes()[(size_t) modeIdx];
    audioAnalyzer.setChannelMode (mode);
    shaper.setChannelMode (mode);

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
