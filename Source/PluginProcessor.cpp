#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
 
MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    const char* envUrl = std::getenv("MIXMIND_SERVER_URL");
    if (envUrl && strlen(envUrl) > 0)
        apiClient.setServerUrl(envUrl);
    else
        apiClient.setServerUrl("https://getjuicepipe.com");

    const char* envKey = std::getenv("MIXMIND_LICENSE_KEY");
    if (envKey && strlen(envKey) > 0)
        licenseManager.setLicenseKey(envKey);

    if (licenseManager.isLicensed())
        apiClient.setLicenseKey (licenseManager.getLicenseKey());

    apiClient.setMaxTokens(1000);
}
 
void MixMindProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    audioAnalyzer.prepare (sampleRate, samplesPerBlock);
}
 
void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
 
    if (auto* ph = getPlayHead())
    {
        if (auto positionInfo = ph->getPosition())
        {
            if (positionInfo->getBpm().hasValue())
                currentBpm = *positionInfo->getBpm();
            if (positionInfo->getTimeSignature().hasValue())
            {
                timeSigNumerator   = positionInfo->getTimeSignature()->numerator;
                timeSigDenominator = positionInfo->getTimeSignature()->denominator;
            }
            isPlaying = positionInfo->getIsPlaying();
        }
    }
 
    audioAnalyzer.process (buffer);

    juce::String context =
        "{"
        "\"audio\": "      + audioAnalyzer.getAnalysisAsJson()         + ","
        "\"bpm\": "        + juce::String (currentBpm, 1)              + ","
        "\"time_sig\": \"" + juce::String (timeSigNumerator)
                           + "/" + juce::String (timeSigDenominator)   + "\","
        "\"is_playing\": " + (isPlaying ? "true" : "false")
        + "}";

    apiClient.setSessionContext (context);
}
 
juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}
 
void MixMindProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream (dest, true);
}
 
void MixMindProcessor::setStateInformation (const void*, int)
{
}
 
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
