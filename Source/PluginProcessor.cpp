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
    auto chain = &eqChain;
    chain->reset();

    // Bypass all
    auto setBypass = [&](int idx, bool b) {
        switch(idx) {
            case 0: chain->setBypassed<0>(b); break; case 1: chain->setBypassed<1>(b); break;
            case 2: chain->setBypassed<2>(b); break; case 3: chain->setBypassed<3>(b); break;
            case 4: chain->setBypassed<4>(b); break; case 5: chain->setBypassed<5>(b); break;
            case 6: chain->setBypassed<6>(b); break; case 7: chain->setBypassed<7>(b); break;
        }
    };

    for (int i = 0; i < 8; ++i) setBypass(i, true);

    for (size_t i = 0; i < suggestions.size() && i < 8; ++i)
    {
        auto& s = suggestions[i];
        auto coeffs = Coefficients::makePeakFilter (currentSampleRate, s.freqHz, s.q,
                                                      juce::Decibels::decibelsToGain (s.gainDb));

        if (s.filterType == "LowShelf")
            coeffs = Coefficients::makeLowShelf (currentSampleRate, s.freqHz, s.q,
                                                  juce::Decibels::decibelsToGain (s.gainDb));
        else if (s.filterType == "HighShelf")
            coeffs = Coefficients::makeHighShelf (currentSampleRate, s.freqHz, s.q,
                                                   juce::Decibels::decibelsToGain (s.gainDb));
        else if (s.filterType == "HighPass" || s.filterType == "HP")
            coeffs = Coefficients::makeHighPass (currentSampleRate, s.freqHz, s.q);
        else if (s.filterType == "LowPass" || s.filterType == "LP")
            coeffs = Coefficients::makeLowPass (currentSampleRate, s.freqHz, s.q);

        auto setCoeffs = [&](int idx) {
            switch(idx) {
                case 0: chain->get<0>().coefficients = coeffs; break;
                case 1: chain->get<1>().coefficients = coeffs; break;
                case 2: chain->get<2>().coefficients = coeffs; break;
                case 3: chain->get<3>().coefficients = coeffs; break;
                case 4: chain->get<4>().coefficients = coeffs; break;
                case 5: chain->get<5>().coefficients = coeffs; break;
                case 6: chain->get<6>().coefficients = coeffs; break;
                case 7: chain->get<7>().coefficients = coeffs; break;
            }
        };

        setCoeffs((int)i);
        setBypass((int)i, false);
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
