#include "MeterProcessor.h"
#include "MeterEditor.h"

MeterProcessor::MeterProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

juce::AudioProcessorEditor* MeterProcessor::createEditor() { return new MeterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MeterProcessor(); }
