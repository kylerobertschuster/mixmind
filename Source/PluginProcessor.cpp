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

    // Log-frequency (and log-Q) parameter ranges, so the host knob is even per octave.
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                 [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); } };
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    float parseHz (const juce::String& s)
    {
        const float v = s.retainCharacters ("0123456789.").getFloatValue();
        return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
    }

    constexpr float kBandDefaultHz[ParametricEq::kNumBands] = { 60, 150, 400, 1000, 2500, 5000, 10000, 16000 };
}

juce::String MixMindProcessor::bandParamId (int band, const char* field)
{
    return "b" + juce::String (band + 1) + field;
}

juce::AudioProcessorValueTreeState::ParameterLayout MixMindProcessor::createParameterLayout()
{
    auto percent = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; });

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "shapeEnable", 1 }, "Shape", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "shapeMode", 1 }, "Mode",
                                                              juce::StringArray ("Auto", "Manual"), 0),
                std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "shapeAmount", 1 }, "Amount",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f, percent),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "matchStereo", 1 }, "Match Stereo",
                                                              juce::StringArray ("Linked", "Mid/Side"), 0));

    auto hz = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return hzText (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseHz (t); });
    auto db = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String::formatted ("%+.1f dB", v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-+").getFloatValue(); });
    auto qText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); });

    for (int b = 0; b < ParametricEq::kNumBands; ++b)
    {
        const auto name = "Band " + juce::String (b + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { bandParamId (b, "On"), 1 }, name + "On", false),
                    std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandParamId (b, "Type"), 1 }, name + "Type",
                                                                  ParametricEq::typeNames(), 0),
                    std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { bandParamId (b, "Freq"), 1 }, name + "Freq",
                                                                  logRange (ParametricEq::kMinHz, ParametricEq::kMaxHz),
                                                                  kBandDefaultHz[b], hz),
                    std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { bandParamId (b, "Gain"), 1 }, name + "Gain",
                                                                  juce::NormalisableRange<float> (-ParametricEq::kMaxGainDb, ParametricEq::kMaxGainDb, 0.01f),
                                                                  0.0f, db),
                    std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { bandParamId (b, "Q"), 1 }, name + "Q",
                                                                  logRange (ParametricEq::kMinQ, ParametricEq::kMaxQ), 1.0f, qText),
                    std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandParamId (b, "Slope"), 1 }, name + "Slope",
                                                                  ParametricEq::slopeNames(), 0),
                    std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandParamId (b, "Place"), 1 }, name + "Placement",
                                                                  ParametricEq::placementNames(), 0));
    }
    return layout;
}

MixMindProcessor::MixMindProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixMindParams", createParameterLayout())
{
    for (int b = 0; b < ParametricEq::kNumBands; ++b)
    {
        auto& bp = bandParams[(size_t) b];
        bp.on        = dynamic_cast<juce::AudioParameterBool*>   (parameters.getParameter (bandParamId (b, "On")));
        bp.type      = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (bandParamId (b, "Type")));
        bp.freq      = dynamic_cast<juce::AudioParameterFloat*>  (parameters.getParameter (bandParamId (b, "Freq")));
        bp.gain      = dynamic_cast<juce::AudioParameterFloat*>  (parameters.getParameter (bandParamId (b, "Gain")));
        bp.q         = dynamic_cast<juce::AudioParameterFloat*>  (parameters.getParameter (bandParamId (b, "Q")));
        bp.slope     = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (bandParamId (b, "Slope")));
        bp.placement = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (bandParamId (b, "Place")));
        jassert (bp.on && bp.type && bp.freq && bp.gain && bp.q && bp.slope && bp.placement);
    }

    // Constant latency: the shaper is a pure kLatency delay when not shaping.
    setLatencySamples (ShaperProcessor::kLatency);
    startTimerHz (kDesignHz);
}

ParametricEq::Band MixMindProcessor::readBand (int band) const
{
    const auto& bp = bandParams[(size_t) band];
    ParametricEq::Band b;
    b.on        = bp.on->get();
    b.type      = (ParametricEq::Type) bp.type->getIndex();
    b.freq      = bp.freq->get();
    b.gainDb    = bp.gain->get();
    b.q         = bp.q->get();
    b.slope     = ParametricEq::slopeFromIndex (bp.slope->getIndex());
    b.placement = (ParametricEq::Placement) bp.placement->getIndex();
    return b;
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
    // FIR length plus a generous allowance for a high-Q band ringing out.
    return (double) ShaperProcessor::kTapCount / getCurrentSampleRate() + 0.25;
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
    outputAnalyzer.prepare (sr, maxBlock);
    shaper.prepare (sr, maxBlock);
    for (int b = 0; b < ParametricEq::kNumBands; ++b)
        eq.setBand (b, readBand (b));
    eq.prepare (sr, maxBlock);
    setLatencySamples (ShaperProcessor::kLatency);
}

void MixMindProcessor::runChain (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    const int numIn = getTotalNumInputChannels();
    const int n     = buffer.getNumSamples();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (numIn == 0) return;

    // A NaN / Inf from upstream would poison the filters and, worse, the
    // long-term match spectrum (an average never recovers from NaN): treat
    // non-finite input as silence.
    juce::AudioBuffer<float> io (buffer.getArrayOfWritePointers(), juce::jmin (numIn, 2), n);
    for (int ch = 0; ch < io.getNumChannels(); ++ch)
    {
        float* d = io.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    // Analysis sees the input (pre-shaper), so the match never chases its own output.
    audioAnalyzer.process (io);

    float* L = buffer.getWritePointer (0);
    float* R = numIn >= 2 ? buffer.getWritePointer (1) : nullptr;

    shaper.setEnabled (! bypassed && parameters.getRawParameterValue ("shapeEnable")->load() > 0.5f);
    shaper.process (L, R, n);

    if (! bypassed)
    {
        for (int b = 0; b < ParametricEq::kNumBands; ++b)
            eq.setBand (b, readBand (b));
        eq.process (L, R, n);
    }

    // Last line of defence: never hand the host a non-finite sample. If one
    // ever appears, silence the block and start the filters from clean state.
    bool finite = true;
    for (int ch = 0; ch < io.getNumChannels() && finite; ++ch)
    {
        const float* d = io.getReadPointer (ch);
        for (int i = 0; i < n && finite; ++i) finite = std::isfinite (d[i]);
    }
    if (! finite)
    {
        io.clear();
        shaper.reset();
        eq.reset();
    }

    outputAnalyzer.process (io);
}

void MixMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    runChain (buffer, false);
}

void MixMindProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass must keep the reported latency, or delay compensation
    // shifts this track against the others. Identity = a kLatency delay; the
    // bands are skipped.
    juce::ScopedNoDenormals noDenormals;
    runChain (buffer, true);
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
        refGrid.clear();
        refSideGrid.clear();
        if (auto ref = getReference())
        {
            refGrid.resize ((size_t) AudioAnalyzer::numBins);
            ReferenceAnalyzer::mapToGrid (*ref, rate, refGrid.data(), AudioAnalyzer::numBins);
            if (ref->hasSide())
            {
                refSideGrid.resize ((size_t) AudioAnalyzer::numBins);
                ReferenceAnalyzer::mapToGrid (*ref, rate, refSideGrid.data(), AudioAnalyzer::numBins, true);
            }
        }
    }
    return refGrid;
}

const std::vector<float>& MixMindProcessor::getReferenceSideBins()
{
    getReferenceBins();   // refreshes both grids
    return refSideGrid;
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
    drainAi();   // first, so this tick's design already sees what the AI changed
    history.drain (outputAnalyzer);

    DesignInputs in;
    in.manual       = parameters.getRawParameterValue ("shapeMode")->load() > 0.5f;
    in.midSide      = parameters.getRawParameterValue ("matchStereo")->load() > 0.5f;
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
    sideTarget.clear();

    if (! in.manual)
    {
        const auto& bins = getReferenceBins();
        if (! bins.empty()) { target = bins; haveTarget = true; }
        if (in.midSide) sideTarget = getReferenceSideBins();
    }
    else
    {
        // The trace is a tonal target for the whole mix: MANUAL is always linked.
        const auto points = getTrace();
        if (points.size() >= 2)
        {
            ShaperProcessor::buildTargetFromCurve (points, n, in.rate, target);
            haveTarget = true;
        }
    }

    float midOffset = 0.0f;
    // One consistent snapshot of the input analyzer for the whole design.
    const auto& live = audioAnalyzer.getSpectra();
    const bool ok = haveTarget && live.hasLongTerm()
                 && ShaperProcessor::buildCorrection (target.data(), live.longTerm, n, in.rate,
                                                      in.amount, correctionDb, nullptr, &midOffset);
    if (! ok)
    {
        correctionDb.clear();
        correctionSideDb.clear();
        if (! postedIdentity) { shaper.setFilter (nullptr, 0); postedIdentity = true; }
        return;
    }

    // Mid/side: the side is matched with the mid's level offset, so its level
    // relative to the mid — the width, per band — follows the reference while
    // overall loudness stays put. A side with nothing to compare (a mono mix)
    // is left alone.
    const bool midSide = ! sideTarget.empty();
    if (midSide && ! ShaperProcessor::buildCorrection (sideTarget.data(), live.longTermSide, n,
                                                       in.rate, in.amount, correctionSideDb, &midOffset))
        correctionSideDb.assign ((size_t) n, 0.0f);
    if (! midSide)
        correctionSideDb.clear();

    ShaperProcessor::designFromCurve (correctionDb, taps);
    if (midSide) ShaperProcessor::designFromCurve (correctionSideDb, sideTaps);
    else         sideTaps.clear();

    // Skip re-posting a filter that has not really changed: every post costs
    // the audio thread a crossfade (two convolutions for kFadeSamples).
    const auto differs = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size()) return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) >= 1.0e-5f) return true;
        return false;
    };
    if (! postedIdentity && ! differs (taps, postedTaps) && ! differs (sideTaps, postedSideTaps)) return;

    if (midSide) shaper.setMidSideFilters (taps.data(), sideTaps.data(), (int) taps.size());
    else         shaper.setFilter (taps.data(), (int) taps.size());
    postedTaps     = taps;
    postedSideTaps = sideTaps;
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
            if (reference->hasSide())
            {
                juce::MemoryBlock side (reference->sideSpectrum.data(), reference->sideSpectrum.size() * sizeof (float));
                r.setProperty ("sideSpectrum", side.toBase64Encoding(), nullptr);
            }
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

    // The state may come from a project file someone else wrote: a spectrum
    // must be finite, non-negative magnitudes or it is not used (the file is
    // re-read instead, if it exists). isValid() checks rate and sizes.
    const auto decode = [&r] (const char* key, std::vector<float>& out)
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (r.getProperty (key).toString()))
        {
            const auto* f = static_cast<const float*> (block.getData());
            out.assign (f, f + block.getSize() / sizeof (float));
            if (! std::all_of (out.begin(), out.end(), [] (float v) { return std::isfinite (v) && v >= 0.0f; }))
                out.clear();
        }
    };
    decode ("spectrum", cached->spectrum);
    decode ("sideSpectrum", cached->sideSpectrum);

    if (cached->isValid())
    {
        // Analyses cached before mid/side matching have no side spectrum:
        // use the cache now and refresh it from the file if it is still there.
        const juce::File file (juce::File::isAbsolutePath (cached->path) ? cached->path : juce::String());
        const bool refresh = ! cached->hasSide() && file.existsAsFile();

        ++loadGeneration;   // a restored reference supersedes any in-flight load
        {
            const juce::ScopedLock sl (stateLock);
            reference  = std::move (cached);
            refStatus  = RefStatus::ready;
            refMessage = {};
        }
        ++refVersion;

        if (refresh) loadReference (file);
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

// ─────────────────────────────────────────────────────────────────────────────
//  AI results (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void MixMindProcessor::drainAi()
{
    // Each result supersedes the previous suggestion: only the answer to the
    // latest request can be applied.
    AiResult r;
    while (ai.pop (r))
    {
        const bool accepted = r.status == AiResult::Status::accepted;
        if (accepted) aiSuggestion = r.payload;
        aiStatus = { r.requestId, r.status, juce::String::fromUTF8 (r.message), accepted, 0 };
    }
}

int MixMindProcessor::approveAiSuggestion()
{
    if (! aiStatus.awaitingApproval) return 0;
    aiStatus.awaitingApproval = false;
    aiStatus.applied = applyAiPayload (aiSuggestion);
    return aiStatus.applied;
}

int MixMindProcessor::applyAiPayload (const AiPayload& p)
{
    // Applied exactly as if the user had moved the controls: the host sees
    // the edits (automation, undo), the session saves them, the editor's
    // attachments follow, and the audio thread reads the new values from the
    // parameters' atomics. Never from the audio thread: notifying listeners
    // there can post messages and block.
    const auto& list = getParameters();
    int applied = 0;
    for (int i = 0; i < p.numChanges; ++i)
    {
        const auto& change = p.changes[(size_t) i];
        auto* param = list[change.param];
        if (param == nullptr || juce::exactlyEqual (param->getValue(), change.value)) continue;
        param->beginChangeGesture();
        param->setValueNotifyingHost (change.value);
        param->endChangeGesture();
        ++applied;
    }
    if (p.setsTrace)
    {
        setTrace ({ p.trace.begin(), p.trace.begin() + p.numTracePoints });
        ++applied;
    }
    return applied;
}
