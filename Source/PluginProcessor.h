#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include "AiWorker.h"
#include "AudioAnalyzer.h"
#include "MeasurementHistory.h"
#include "ParametricEq.h"
#include "ReferenceAnalyzer.h"
#include "ShaperProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixMind — reference-matching spectrum shaper + analyzer.
//    · AUTO   mode: reshapes the live spectrum toward the loaded reference
//                   (linked, or mid and side separately).
//    · MANUAL mode: reshapes toward the hand-drawn trace curve.
//  Signal path: input analyzer → match EQ (linear-phase FIR, constant
//  kLatency) → parametric bands → output analyzer. The match is designed from
//  the input, so it never chases its own output; the output analyzer is what
//  the YOU curve and readouts show.
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
    using juce::AudioProcessor::processBlock;           // float only; keep the double
    using juce::AudioProcessor::processBlockBypassed;   // overloads visible, not hidden

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

    // The reference spectrum on the live analyzer grid (empty if none), and
    // its side spectrum (empty if none, or a pre-M/S cached analysis).
    const std::vector<float>& getReferenceBins();
    const std::vector<float>& getReferenceSideBins();

    // ── Trace (manual target), stored as (Hz, dB) sorted by frequency ───────
    std::vector<TracePoint> getTrace() const;
    void setTrace (std::vector<TracePoint> points);
    int  getTraceVersion() const { return traceVersion.load(); }

    // ── Match curves currently designed (dB per analyzer bin; may be empty).
    //    The side curve is only non-empty while matching mid/side. ─────────
    const std::vector<float>& getCorrectionDb() const     { return correctionDb; }
    const std::vector<float>& getCorrectionSideDb() const { return correctionSideDb; }

    // ── Parametric bands ────────────────────────────────────────────────────
    struct BandParams
    {
        juce::AudioParameterBool*   on        = nullptr;
        juce::AudioParameterChoice* type      = nullptr;
        juce::AudioParameterFloat*  freq      = nullptr;
        juce::AudioParameterFloat*  gain      = nullptr;
        juce::AudioParameterFloat*  q         = nullptr;
        juce::AudioParameterChoice* slope     = nullptr;
        juce::AudioParameterChoice* placement = nullptr;
    };
    static juce::String bandParamId (int band, const char* field);   // band is 0-based
    const BandParams& getBandParams (int band) const { return bandParams[(size_t) band]; }
    ParametricEq::Band readBand (int band) const;                    // any thread

    double getCurrentSampleRate() const;

    // What Mix Doctor observes: the output analyzer's measurement frames,
    // drained by the design loop (message thread; runs with the editor closed).
    const MeasurementHistory& getMeasurementHistory() const { return history; }

    // ── AI assistant (message thread). Requests run on the AI worker, never on
    //    the audio thread; validated results come back through its lock-free
    //    FIFO. An accepted result is only a suggestion (ADR-003): nothing
    //    changes until the user approves it, and then it is applied as
    //    host-visible parameter gestures (and a trace edit) that the DSP picks
    //    up lock-free. ─────────────────────────────────────────────────────────
    struct AiStatus
    {
        int requestId { 0 };                           // 0 = no result yet
        AiResult::Status status { AiResult::Status::failed };
        juce::String message;                          // summary, or why there is no suggestion
        bool awaitingApproval { false };               // a suggestion is pending
        int applied { 0 };                             // after approval: parameters changed (+1 for a trace)
    };
    void setAiBackend (AiWorker::Backend backend)       { ai.setBackend (std::move (backend)); }
    int  submitAiRequest (const juce::String& request) { return ai.submit (request); }
    const AiStatus& getLastAiStatus() const            { return aiStatus; }

    // The pending suggestion (only ever the latest result's), or nullptr.
    const AiPayload* getAiSuggestion() const           { return aiStatus.awaitingApproval ? &aiSuggestion : nullptr; }
    int  approveAiSuggestion();                        // the user's click; returns what changed
    void dismissAiSuggestion()                         { aiStatus.awaitingApproval = false; }

    juce::AudioProcessorValueTreeState parameters;
    AudioAnalyzer   audioAnalyzer;    // input (pre-processing): match source
    AudioAnalyzer   outputAnalyzer;   // output: what the listener hears
    ShaperProcessor shaper;
    ParametricEq    eq;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void timerCallback() override;          // match-filter design loop
    void handleAsyncUpdate() override;      // starts loads requested off the message thread
    void startLoad (const juce::File& file);
    void installReference (int generation, std::shared_ptr<const ReferenceAnalyzer::Result> result,
                           const juce::String& error);
    void runChain (juce::AudioBuffer<float>&, bool bypassed);
    void drainAi();
    int  applyAiPayload (const AiPayload&);

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

    std::array<BandParams, ParametricEq::kNumBands> bandParams;

    // Design-loop state (message thread only).
    std::vector<float> refGrid, refSideGrid, correctionDb, correctionSideDb, target, sideTarget;
    std::vector<float> taps, sideTaps, postedTaps, postedSideTaps;
    int    refGridVersion { -1 };
    double refGridRate { 0.0 };
    struct DesignInputs
    {
        bool manual = false, midSide = false; float amount = -1.0f; int refVersion = -1, traceVersion = -1; double rate = 0.0;
        bool operator!= (const DesignInputs& o) const
        {
            return manual != o.manual || midSide != o.midSide || ! juce::exactlyEqual (amount, o.amount)
                || refVersion != o.refVersion || traceVersion != o.traceVersion || ! juce::exactlyEqual (rate, o.rate);
        }
    } lastDesign;
    int  ticksSinceDesign { 0 };
    bool postedIdentity { false };

    // Declared last so it is destroyed first: its thread stops before anything
    // else goes away.
    MeasurementHistory history;

    AiWorker ai { getParameters() };
    AiStatus aiStatus;
    AiPayload aiSuggestion;   // valid while aiStatus.awaitingApproval

    JUCE_DECLARE_WEAK_REFERENCEABLE (MixMindProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindProcessor)
};
