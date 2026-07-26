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

    apiClient.setMaxTokens(2500);
}

void MixMindProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    audioAnalyzer.prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    eqChain.prepare (spec);
}

void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Transport
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue()) currentBpm = *pos->getBpm();
            if (pos->getTimeSignature().hasValue())
            {
                timeSigNumerator   = pos->getTimeSignature()->numerator;
                timeSigDenominator = pos->getTimeSignature()->denominator;
            }
            isPlaying = pos->getIsPlaying();
        }
    }

    audioAnalyzer.process (buffer);

    // Apply EQ if active
    if (eqActive)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        eqChain.process (ctx);
    }

    juce::String context =
        "{\"audio\": "      + audioAnalyzer.getAnalysisAsJson()         + ","
        "\"bpm\": "        + juce::String (currentBpm, 1)              + ","
        "\"time_sig\": \"" + juce::String (timeSigNumerator)
                           + "/" + juce::String (timeSigDenominator)   + "\","
        "\"is_playing\": " + (isPlaying ? "true" : "false")
        + "}";
    apiClient.setSessionContext (context);
}

void MixMindProcessor::applyEQ (const std::vector<EQSuggestion>& suggestions)
{
    // Reset all filters
    for (size_t i = 0; i < 8; ++i)
        *eqChain.get<0>() = *Coefficients::makeAllPass (currentSampleRate, 1000.0f);

    eqChain.setBypassed<0> (true);
    eqChain.setBypassed<1> (true);
    eqChain.setBypassed<2> (true);
    eqChain.setBypassed<3> (true);
    eqChain.setBypassed<4> (true);
    eqChain.setBypassed<5> (true);
    eqChain.setBypassed<6> (true);
    eqChain.setBypassed<7> (true);

    // Apply up to 8 EQ bands
    for (size_t i = 0; i < suggestions.size() && i < 8; ++i)
    {
        auto& s = suggestions[i];
        float freq = s.freqHz;
        float gain = s.gainDb;
        float q    = s.q;

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        if (s.filterType == "Bell" || s.filterType == "Peak")
            coeffs = Coefficients::makePeakFilter (currentSampleRate, freq, q, juce::Decibels::decibelsToGain (gain));
        else if (s.filterType == "LowShelf")
            coeffs = Coefficients::makeLowShelf (currentSampleRate, freq, q, juce::Decibels::decibelsToGain (gain));
        else if (s.filterType == "HighShelf")
            coeffs = Coefficients::makeHighShelf (currentSampleRate, freq, q, juce::Decibels::decibelsToGain (gain));
        else if (s.filterType == "HighPass" || s.filterType == "HP")
            coeffs = Coefficients::makeHighPass (currentSampleRate, freq, q);
        else if (s.filterType == "LowPass" || s.filterType == "LP")
            coeffs = Coefficients::makeLowPass (currentSampleRate, freq, q);
        else
            coeffs = Coefficients::makePeakFilter (currentSampleRate, freq, q, juce::Decibels::decibelsToGain (gain));

        switch (i) {
            case 0: *eqChain.get<0>() = *coeffs; eqChain.setBypassed<0> (false); break;
            case 1: *eqChain.get<1>() = *coeffs; eqChain.setBypassed<1> (false); break;
            case 2: *eqChain.get<2>() = *coeffs; eqChain.setBypassed<2> (false); break;
            case 3: *eqChain.get<3>() = *coeffs; eqChain.setBypassed<3> (false); break;
            case 4: *eqChain.get<4>() = *coeffs; eqChain.setBypassed<4> (false); break;
            case 5: *eqChain.get<5>() = *coeffs; eqChain.setBypassed<5> (false); break;
            case 6: *eqChain.get<6>() = *coeffs; eqChain.setBypassed<6> (false); break;
            case 7: *eqChain.get<7>() = *coeffs; eqChain.setBypassed<7> (false); break;
        }
    }

    eqActive = !suggestions.empty();
}

void MixMindProcessor::clearEQ()
{
    eqActive = false;
}

juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}

void MixMindProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream (dest, true);
}

void MixMindProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
