#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
 
MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // ── Server configuration ────────────────────────────────────────────────
    const char* envUrl = std::getenv("MIXMIND_SERVER_URL");
    if (envUrl && strlen(envUrl) > 0)
        apiClient.setServerUrl(envUrl);
    else
        apiClient.setServerUrl("https://getjuicepipe.com");

    // License key from env overrides local storage
    const char* envKey = std::getenv("MIXMIND_LICENSE_KEY");
    if (envKey && strlen(envKey) > 0)
        licenseManager.setLicenseKey(envKey);

    // Sync license key to API client
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
 
    // 1. Get host transport state
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
 
    // 2. DSP Analysis
    audioAnalyzer.process (buffer);
 
    // 3. Build full session context JSON and push to API client
    //    Sent as part of the system prompt on every API call,
    //    giving the model real-time awareness of mix state, transport, and session metadata.
    juce::String context =
        "{"
        "\"audio\": "      + audioAnalyzer.getAnalysisAsJson()         + ","
        "\"bpm\": "        + juce::String (currentBpm, 1)              + ","
        "\"time_sig\": \"" + juce::String (timeSigNumerator)
                           + "/" + juce::String (timeSigDenominator)   + "\","
        "\"is_playing\": " + (isPlaying ? "true" : "false")            + ","
        "\"genre\": \""    + savedGenre                                 + "\","
        "\"daw\": \""      + savedDaw                                   + "\""
        "}";
 
    apiClient.setSessionContext (context);
 
    // (buffer perfectly bypassed — MixMind is a utility plugin, not an FX plugin)
}
 
juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}
 
void MixMindProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream stream (dest, true);
    stream.writeString (savedGenre);
    stream.writeString (savedDaw);
}
 
void MixMindProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream stream (data, static_cast<size_t> (size), false);
    savedGenre = stream.readString();
    savedDaw   = stream.readString();
}
 
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
 