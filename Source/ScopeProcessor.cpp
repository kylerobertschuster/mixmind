#include "ScopeProcessor.h"
#include "ScopeEditor.h"

ScopeProcessor::ScopeProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

juce::AudioProcessorEditor* ScopeProcessor::createEditor() { return new ScopeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ScopeProcessor(); }
