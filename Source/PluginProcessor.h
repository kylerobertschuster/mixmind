#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include "AudioAnalyzer.h"
#include "ReferenceAnalyzer.h"
#include "ShaperProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMind — reference-matching spectrum shaper + analyzer.
//    · AUTO   mode: reshapes the live spectrum toward the loaded reference.
//    · MANUAL mode: reshapes toward the hand-drawn trace curve.
//  Analysis (BS.1770 metering + averaged FFT) runs continuously on the input;
//  the shaper is the insert path with a constant kLatency.
//
//  The processor owns everything that must outlive the editor window: the
//  reference analysis, the trace, and the match-filter design loop (a
//  message-thread timer). All three are saved with the session; the reference
//  analysis is cached in the state so a session reopens with its reference
//  even if the audio file has moved.
// ─────────────────────────────────────────────────────────────────────────────
class MixMindProcessor : public juce::AudioProcessor,
                         private juce::Timer,
                         private juce::AsyncUpdater
{
public:
    using TracePoint = ShaperProcessor::TracePoint;
    enum class RefStatus { none, loading, ready, failed };

    MixMindProcessor();
    ~MixMindProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MixMind"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ── Reference track (message thread unless noted) ───────────────────────
    void loadReference (const juce::File& file);     // async; any thread
    void clearReference();
    std::shared_ptr<const ReferenceAnalyzer::Result> getReference() const;
    RefStatus    getReferenceStatus() const;
    juce::String getReferenceMessage() const;       // file being loaded, or last error
    int          getReferenceVersion() const { return refVersion.load(); }
    juce::String getReferenceWildcard() const { return referenceAnalyzer.getWildcard(); }
    bool         canLoadAsReference (const juce::File& f) const { return referenceAnalyzer.canRead (f); }

    // The reference spectrum on the live analyzer grid (empty if none).
    const std::vector<float>& getReferenceBins();

    // ── Trace (manual target), stored as (Hz, dB) sorted by frequency ───────
    std::vector<TracePoint> getTrace() const;
    void setTrace (std::vector<TracePoint> points);
    int  getTraceVersion() const { return traceVersion.load(); }

    // ── Match curve currently designed (dB per analyzer bin; may be empty) ──
    const std::vector<float>& getCorrectionDb() const { return correctionDb; }

    double getCurrentSampleRate() const;

    juce::AudioProcessorValueTreeState parameters;
    AudioAnalyzer   audioAnalyzer;
    ShaperProcessor shaper;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void timerCallback() override;          // match-filter design loop
    void handleAsyncUpdate() override;      // starts loads requested off the message thread
    void startLoad (const juce::File& file);
    void installReference (int generation, std::shared_ptr<const ReferenceAnalyzer::Result> result,
                           const juce::String& error);
    void runShaper (juce::AudioBuffer<float>&, bool shapeOn);

    // Reference state (guarded by stateLock — read by getStateInformation on
    // whatever thread the host uses).
    ReferenceAnalyzer referenceAnalyzer;    // used only by the loader thread
    juce::ThreadPool  loader { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                        .withThreadName ("MixMind reference loader") };
    mutable juce::CriticalSection stateLock;
    std::shared_ptr<const ReferenceAnalyzer::Result> reference;
    RefStatus    refStatus { RefStatus::none };
    juce::String refMessage;
    juce::File   pendingLoad;
    std::atomic<int>  loadGeneration { 0 };
    std::atomic<int>  refVersion { 0 };
    std::atomic<bool> shuttingDown { false };

    std::vector<TracePoint> trace;
    std::atomic<int> traceVersion { 0 };

    // Design-loop state (message thread only).
    std::vector<float> refGrid, correctionDb, target, taps, postedTaps;
    int    refGridVersion { -1 };
    double refGridRate { 0.0 };
    struct DesignInputs
    {
        bool manual = false; float amount = -1.0f; int refVersion = -1, traceVersion = -1; double rate = 0.0;
        bool operator!= (const DesignInputs& o) const
        {
            return manual != o.manual || ! juce::exactlyEqual (amount, o.amount) || refVersion != o.refVersion
                || traceVersion != o.traceVersion || ! juce::exactlyEqual (rate, o.rate);
        }
    } lastDesign;
    int  ticksSinceDesign { 0 };
    bool postedIdentity { false };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MixMindProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
