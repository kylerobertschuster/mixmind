#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    const char* envUrl = std::getenv ("MIXMIND_SERVER_URL");
    apiClient.setServerUrl (envUrl && strlen(envUrl) ? envUrl : "https://getjuicepipe.com");

    const char* envKey = std::getenv ("MIXMIND_LICENSE_KEY");
    if (envKey && strlen(envKey))
    {
        licenseManager.setLicenseKey (envKey);
        apiClient.setLicenseKey (envKey);
    }

    apiClient.setMaxTokens (2500);
}

void MixMindProcessor::prepareToPlay (double sr, int blockSize)
{
    audioAnalyzer.prepare (sr, blockSize);
}

void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    audioAnalyzer.process (buffer);
    // Audio passes through unchanged — MixMind is an analysis tool
}

juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
