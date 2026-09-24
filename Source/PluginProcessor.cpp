#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <vector>

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
    // The match filter is designed off the audio thread, but it must not be the
    // editor's job: a host with no window open would never reshape anything,
    // including in the seconds after a session is recalled. ~4 Hz matches the
    // throttle the editor used.
    startTimer (250);
}

MixMindProcessor::~MixMindProcessor()
{
    stopTimer();
}

void MixMindProcessor::prepareToPlay (double sr, int blockSize)
{
    audioAnalyzer.prepare (sr, blockSize);
    shaper.prepare (sr, blockSize);

    // The host can re-prepare mid-session (sample-rate change, buffer change,
    // offline bounce) while a non-zero latency is still reported. JUCE's
    // latencySamples is a plain member that prepareToPlay never clears, so if
    // the Shape toggle is off afterwards, `lat != reportedLatency` is never
    // true and the host keeps compensating for a delay that no longer exists.
    // Reporting 0 explicitly is what stops the value going stale.
    reportedLatency = 0;
    setLatencySamples (0);

    prepared = true;
    designMatchFilter();   // a recalled session must shape without waiting a tick
}

double MixMindProcessor::getTailLengthSeconds() const
{
    // A kTapCount-tap linear-phase FIR rings for kTapCount samples after the
    // input stops. Reporting 0 lets a host truncate that tail on an offline
    // bounce; 1024 samples at 44.1 kHz is the conservative upper bound.
    return (double) ShaperProcessor::kTapCount / 44100.0;
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

// ── Match filter design (message thread) ──────────────────────────────────

void MixMindProcessor::timerCallback()
{
    designMatchFilter();
}

void MixMindProcessor::designMatchFilter()
{
    if (! prepared)
        return;

    if (parameters.getRawParameterValue ("shapeEnable")->load() <= 0.5f)
        return;

    const bool  autoMode = parameters.getRawParameterValue ("shapeMode")->load() < 0.5f;
    const float amount   = parameters.getRawParameterValue ("shapeAmount")->load();
    const int   n        = AudioAnalyzer::numBins;

    std::vector<float> target ((size_t) n);

    if (autoMode)
    {
        if (! referenceAnalyzer.hasReference())
            return;

        std::copy (referenceAnalyzer.getBins(), referenceAnalyzer.getBins() + n, target.begin());
    }
    else
    {
        const auto curve = getTraceCurve();

        if (curve.size() < 2)
            return;

        ShaperProcessor::buildTargetFromCurve (curve, n, audioAnalyzer.getSampleRate(), target);
    }

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), audioAnalyzer.getFFTBins(), n, amount, taps);
    shaper.setFilter (taps.data(), (int) taps.size());
}

// ── Drawn trace curve ─────────────────────────────────────────────────────

MixMindState::TraceCurve MixMindProcessor::getTraceCurve() const
{
    const juce::SpinLock::ScopedLockType sl (traceLock);
    return traceCurve;
}

void MixMindProcessor::setTraceCurve (MixMindState::TraceCurve curve)
{
    const juce::SpinLock::ScopedLockType sl (traceLock);
    traceCurve = std::move (curve);
}

bool MixMindProcessor::hasTrace() const
{
    const juce::SpinLock::ScopedLockType sl (traceLock);
    return traceCurve.size() >= 2;
}

void MixMindProcessor::clearTrace()
{
    setTraceCurve ({});
}

void MixMindProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    // The reference and the drawn curve are neither automatable nor scalar, so
    // AudioProcessorValueTreeState has nowhere to put them. They ride along as
    // a child of the state tree, which replaceState() swaps in one piece.
    juce::ValueTree match (MixMindState::matchTree);
    match.appendChild (MixMindState::traceToTree (getTraceCurve()), nullptr);
    referenceAnalyzer.writeToTree (match);

    state.removeChild (state.getChildWithName (MixMindState::matchTree), nullptr);
    state.appendChild (match, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MixMindProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    // Read the match data out before replaceState(), which swaps the whole tree.
    const auto match = tree.getChildWithName (MixMindState::matchTree);

    parameters.replaceState (tree);

    setTraceCurve (MixMindState::treeToTrace (match.getChildWithName (MixMindState::traceTree)));

    // Fails closed to "no reference" on a truncated payload, and clears whatever
    // was loaded before, so a bad session cannot leave half a spectrum behind.
    referenceAnalyzer.readFromTree (match);

    designMatchFilter();   // shape now, rather than up to 250 ms from now
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
