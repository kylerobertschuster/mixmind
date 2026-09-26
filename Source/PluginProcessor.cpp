#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    const juce::Identifier kStateTag     { "MixMindState" };
    const juce::Identifier kReferenceTag { "Reference" };
    const juce::Identifier kTraceTag     { "Trace" };
    const juce::Identifier kPointTag     { "Point" };

    constexpr int kDesignHz        = 30;   // design-loop tick rate
    constexpr int kIdleDesignTicks = 8;    // re-design every ~270 ms as the live spectrum drifts
}

juce::AudioProcessorValueTreeState::ParameterLayout MixMindProcessor::createParameterLayout()
{
    auto percent = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; });

    return {
        std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "shapeEnable", 1 }, "Shape", false),
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "shapeMode", 1 }, "Mode",
                                                      juce::StringArray ("Auto", "Manual"), 0),
        std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "shapeAmount", 1 }, "Amount",
                                                      juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f, percent)
    };
}

MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixMindParams", createParameterLayout())
{
    // Constant latency: the shaper is a pure kLatency delay when not shaping.
    setLatencySamples (ShaperProcessor::kLatency);
    startTimerHz (kDesignHz);
}

MixMindProcessor::~MixMindProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    shuttingDown.store (true);
    loader.removeAllJobs (true, -1);   // the job polls shuttingDown every chunk, so this is prompt
}

double MixMindProcessor::getCurrentSampleRate() const
{
    const double sr = getSampleRate();
    return sr > 0.0 ? sr : audioAnalyzer.getSampleRate();
}

double MixMindProcessor::getTailLengthSeconds() const
{
    return (double) ShaperProcessor::kTapCount / getCurrentSampleRate();
}

bool MixMindProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MixMindProcessor::prepareToPlay (double sr, int maxBlock)
{
    audioAnalyzer.prepare (sr, maxBlock);
    shaper.prepare (sr, maxBlock);
    setLatencySamples (ShaperProcessor::kLatency);
}

void MixMindProcessor::runShaper (juce::AudioBuffer<float>& buffer, bool shapeOn)
{
    const int numIn = getTotalNumInputChannels();
    const int n     = buffer.getNumSamples();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (numIn == 0) return;

    // Analysis sees the input (pre-shaper), so the match never chases its own output.
    juce::AudioBuffer<float> input (buffer.getArrayOfWritePointers(), juce::jmin (numIn, 2), n);
    audioAnalyzer.process (input);

    shaper.setEnabled (shapeOn);
    shaper.process (buffer.getWritePointer (0), numIn >= 2 ? buffer.getWritePointer (1) : nullptr, n);
}

void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    runShaper (buffer, parameters.getRawParameterValue ("shapeEnable")->load() > 0.5f);
}

void MixMindProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass must keep the reported latency, or delay compensation
    // shifts this track against the others. Identity = a kLatency delay.
    juce::ScopedNoDenormals noDenormals;
    runShaper (buffer, false);
}

juce::AudioProcessorEditor* MixMindProcessor::createEditor()
{
    return new MixMindEditor (*this);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Reference loading
// ─────────────────────────────────────────────────────────────────────────────

void MixMindProcessor::loadReference (const juce::File& file)
{
    {
        const juce::ScopedLock sl (stateLock);
        pendingLoad = file;
    }
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}

void MixMindProcessor::handleAsyncUpdate()
{
    juce::File file;
    {
        const juce::ScopedLock sl (stateLock);
        std::swap (file, pendingLoad);
    }
    if (file != juce::File())
        startLoad (file);
}

void MixMindProcessor::startLoad (const juce::File& file)
{
    const int gen = ++loadGeneration;
    {
        const juce::ScopedLock sl (stateLock);
        refStatus  = RefStatus::loading;
        refMessage = file.getFileNameWithoutExtension();
    }
    ++refVersion;

    // Created here, on the message thread; the job only copies it.
    juce::WeakReference<MixMindProcessor> weakThis (this);

    loader.addJob ([this, weakThis, gen, file]
    {
        auto result = std::make_shared<ReferenceAnalyzer::Result>();
        juce::String error;
        const bool ok = referenceAnalyzer.analyse (file, *result, error, [this, gen]
        {
            return shuttingDown.load() || loadGeneration.load() != gen;
        });

        if (shuttingDown.load() || loadGeneration.load() != gen)
            return;

        std::shared_ptr<const ReferenceAnalyzer::Result> done;
        if (ok) done = std::move (result);

        juce::MessageManager::callAsync ([weakThis, gen, done, error]
        {
            if (auto* p = weakThis.get())
                p->installReference (gen, done, error);
        });
    });
}

void MixMindProcessor::installReference (int generation, std::shared_ptr<const ReferenceAnalyzer::Result> result,
                                         const juce::String& error)
{
    if (generation != loadGeneration.load()) return;   // superseded by a newer load

    {
        const juce::ScopedLock sl (stateLock);
        if (result != nullptr)
        {
            reference  = std::move (result);
            refStatus  = RefStatus::ready;
            refMessage = {};
        }
        else
        {
            // Keep whatever reference was loaded before; just report.
            refStatus  = RefStatus::failed;
            refMessage = error;
        }
    }
    ++refVersion;
}

void MixMindProcessor::clearReference()
{
    ++loadGeneration;   // cancels an in-flight load
    {
        const juce::ScopedLock sl (stateLock);
        reference.reset();
        refStatus  = RefStatus::none;
        refMessage = {};
    }
    ++refVersion;
}

std::shared_ptr<const ReferenceAnalyzer::Result> MixMindProcessor::getReference() const
{
    const juce::ScopedLock sl (stateLock);
    return reference;
}

MixMindProcessor::RefStatus MixMindProcessor::getReferenceStatus() const
{
    const juce::ScopedLock sl (stateLock);
    return refStatus;
}

juce::String MixMindProcessor::getReferenceMessage() const
{
    const juce::ScopedLock sl (stateLock);
    return refMessage;
}

const std::vector<float>& MixMindProcessor::getReferenceBins()
{
    const double rate = getCurrentSampleRate();
    const int version = refVersion.load();
    if (version != refGridVersion || ! juce::exactlyEqual (rate, refGridRate))
    {
        refGridVersion = version;
        refGridRate    = rate;
        if (auto ref = getReference())
        {
            refGrid.resize ((size_t) AudioAnalyzer::numBins);
            ReferenceAnalyzer::mapToGrid (*ref, rate, refGrid.data(), AudioAnalyzer::numBins);
        }
        else
        {
            refGrid.clear();
        }
    }
    return refGrid;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Trace
// ─────────────────────────────────────────────────────────────────────────────

std::vector<MixMindProcessor::TracePoint> MixMindProcessor::getTrace() const
{
    const juce::ScopedLock sl (stateLock);
    return trace;
}

void MixMindProcessor::setTrace (std::vector<TracePoint> points)
{
    points.erase (std::remove_if (points.begin(), points.end(),
                                  [] (const TracePoint& p) { return ! (p.hz > 0.0f) || ! std::isfinite (p.db); }),
                  points.end());
    std::sort (points.begin(), points.end(), [] (const TracePoint& a, const TracePoint& b) { return a.hz < b.hz; });
    {
        const juce::ScopedLock sl (stateLock);
        trace = std::move (points);
    }
    ++traceVersion;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Match-filter design loop (message thread). Runs whether or not the editor
//  is open; the correction curve is designed even with SHAPE off so the UI
//  can preview it.
// ─────────────────────────────────────────────────────────────────────────────

void MixMindProcessor::timerCallback()
{
    DesignInputs in;
    in.manual       = parameters.getRawParameterValue ("shapeMode")->load() > 0.5f;
    in.amount       = parameters.getRawParameterValue ("shapeAmount")->load();
    in.refVersion   = refVersion.load();
    in.traceVersion = traceVersion.load();
    in.rate         = getCurrentSampleRate();

    const bool changed = in != lastDesign;
    if (! changed && ++ticksSinceDesign < kIdleDesignTicks) return;
    ticksSinceDesign = 0;
    lastDesign = in;

    constexpr int n = AudioAnalyzer::numBins;
    bool haveTarget = false;

    if (! in.manual)
    {
        const auto& bins = getReferenceBins();
        if (! bins.empty()) { target = bins; haveTarget = true; }
    }
    else
    {
        const auto points = getTrace();
        if (points.size() >= 2)
        {
            ShaperProcessor::buildTargetFromCurve (points, n, in.rate, target);
            haveTarget = true;
        }
    }

    if (! haveTarget || ! audioAnalyzer.hasLongTermSpectrum())
    {
        correctionDb.clear();
        if (! postedIdentity) { shaper.setFilter (nullptr, 0); postedIdentity = true; }
        return;
    }

    ShaperProcessor::buildMatchFilter (target.data(), audioAnalyzer.getLongTermBins(), n, in.rate,
                                       in.amount, taps, &correctionDb);

    // Skip re-posting a filter that has not really changed: every post costs
    // the audio thread a crossfade (two convolutions for kFadeSamples).
    float change = postedIdentity || postedTaps.size() != taps.size() ? 1.0f : 0.0f;
    for (size_t i = 0; i < taps.size() && change < 1.0e-5f; ++i)
        change = juce::jmax (change, std::abs (taps[i] - postedTaps[i]));
    if (change < 1.0e-5f) return;

    shaper.setFilter (taps.data(), (int) taps.size());
    postedTaps = taps;
    postedIdentity = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────────────────────────────────────

void MixMindProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (kStateTag);
    root.setProperty ("version", 2, nullptr);
    root.appendChild (parameters.copyState(), nullptr);

    {
        const juce::ScopedLock sl (stateLock);

        if (reference != nullptr && reference->isValid())
        {
            juce::ValueTree r (kReferenceTag);
            r.setProperty ("path",        reference->path, nullptr);
            r.setProperty ("name",        reference->name, nullptr);
            r.setProperty ("sampleRate",  reference->sampleRate, nullptr);
            r.setProperty ("fftSize",     reference->fftSize, nullptr);
            r.setProperty ("duration",    reference->durationSeconds, nullptr);
            r.setProperty ("lufs",        reference->lufs, nullptr);
            r.setProperty ("truePeak",    reference->truePeakDb, nullptr);
            r.setProperty ("rms",         reference->rmsDb, nullptr);
            r.setProperty ("width",       reference->stereoWidth, nullptr);
            r.setProperty ("phase",       reference->phaseCorr, nullptr);
            juce::MemoryBlock spec (reference->spectrum.data(), reference->spectrum.size() * sizeof (float));
            r.setProperty ("spectrum",    spec.toBase64Encoding(), nullptr);
            root.appendChild (r, nullptr);
        }

        if (! trace.empty())
        {
            juce::ValueTree t (kTraceTag);
            for (const auto& p : trace)
            {
                juce::ValueTree pt (kPointTag);
                pt.setProperty ("hz", p.hz, nullptr);
                pt.setProperty ("db", p.db, nullptr);
                t.appendChild (pt, nullptr);
            }
            root.appendChild (t, nullptr);
        }
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void MixMindProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;

    const auto root = juce::ValueTree::fromXml (*xml);

    // Sessions saved before v2 hold just the parameter tree.
    if (root.hasType (parameters.state.getType()))
    {
        parameters.replaceState (root);
        return;
    }
    if (! root.hasType (kStateTag)) return;

    const auto params = root.getChildWithName (parameters.state.getType());
    if (params.isValid())
        parameters.replaceState (params);

    // Trace.
    std::vector<TracePoint> points;
    for (const auto& pt : root.getChildWithName (kTraceTag))
        points.push_back ({ (float) pt.getProperty ("hz"), (float) pt.getProperty ("db") });
    setTrace (std::move (points));

    // Reference: prefer the cached analysis; fall back to re-reading the file.
    const auto r = root.getChildWithName (kReferenceTag);
    if (! r.isValid())
    {
        clearReference();
        return;
    }

    auto cached = std::make_shared<ReferenceAnalyzer::Result>();
    cached->path            = r.getProperty ("path").toString();
    cached->name            = r.getProperty ("name").toString();
    cached->sampleRate      = r.getProperty ("sampleRate");
    cached->fftSize         = r.getProperty ("fftSize");
    cached->durationSeconds = r.getProperty ("duration");
    cached->lufs            = r.getProperty ("lufs");
    cached->truePeakDb      = r.getProperty ("truePeak");
    cached->rmsDb           = r.getProperty ("rms");
    cached->stereoWidth     = r.getProperty ("width");
    cached->phaseCorr       = r.getProperty ("phase");

    juce::MemoryBlock spec;
    if (spec.fromBase64Encoding (r.getProperty ("spectrum").toString()))
    {
        const auto* f = static_cast<const float*> (spec.getData());
        cached->spectrum.assign (f, f + spec.getSize() / sizeof (float));
    }

    if (cached->isValid() && cached->sampleRate > 0.0)
    {
        ++loadGeneration;   // a restored reference supersedes any in-flight load
        {
            const juce::ScopedLock sl (stateLock);
            reference  = std::move (cached);
            refStatus  = RefStatus::ready;
            refMessage = {};
        }
        ++refVersion;
    }
    else if (juce::File::isAbsolutePath (cached->path))
    {
        loadReference (juce::File (cached->path));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixMindProcessor();
}
