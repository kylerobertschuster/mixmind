#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <deque>
#include <functional>
#include <type_traits>
#include "AiFirewall.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AiResult — what the worker hands back for one request: the validated
//  payload, or why there is none. Fixed size, no heap, so it moves through the
//  lock-free FIFO by plain copy.
// ─────────────────────────────────────────────────────────────────────────────
struct AiResult
{
    enum class Status : int
    {
        accepted,   // payload passed the firewall
        rejected,   // the model answered, the firewall refused it (message says why)
        failed      // no answer: no backend, backend error, or it threw
    };

    int requestId { 0 };
    Status status { Status::failed };
    AiPayload payload;
    char message[192] {};   // UTF-8, NUL-terminated, truncated on a character boundary
};
static_assert (std::is_trivially_copyable_v<AiResult>, "AiResult must stay plain data for the lock-free FIFO");

// ─────────────────────────────────────────────────────────────────────────────
//  AiWorker — runs LLM requests on its own thread and hands the results to a
//  single consumer through a lock-free single-producer / single-consumer FIFO.
//
//    submit()  (message thread)  → request list → worker thread
//    worker:   backend (network / inference, may block) → AiFirewall → FIFO
//    pop()     (the single consumer) ← FIFO
//
//  The backend is only ever called on the worker thread. Nothing here is
//  touched by the audio thread; results reach the DSP through the processor
//  (see MixMindProcessor::drainAi).
// ─────────────────────────────────────────────────────────────────────────────
class AiWorker : private juce::Thread
{
public:
    // Blocking model call, run on the worker thread. Fill `reply` with the
    // model's JSON answer, or return a failure. Long calls must poll
    // `shouldCancel` (and use timeouts) so the plugin can close promptly.
    using Backend = std::function<juce::Result (const juce::String& request, juce::String& reply,
                                                const std::function<bool()>& shouldCancel)>;

    static constexpr int kQueueSize = 8;   // AbstractFifo holds kQueueSize − 1 results

    explicit AiWorker (const juce::Array<juce::AudioProcessorParameter*>& params);
    ~AiWorker() override;

    void setBackend (Backend);                    // never from the audio thread
    int  submit (const juce::String& request);    // message thread; returns the request id
    bool pop (AiResult& out);                     // single consumer only; lock-free, never blocks

private:
    struct Request { int id = 0; juce::String text; };

    void run() override;
    AiResult handle (const Request&, const Backend&);
    void push (const AiResult&);

    const AiFirewall firewall;

    // Request side (message thread ↔ worker). Never touched by the audio thread.
    juce::CriticalSection requestLock;
    std::deque<Request> requests;
    Backend backend;
    int nextId { 1 };

    // Result side: lock-free SPSC ring.
    juce::AbstractFifo fifo { kQueueSize };
    std::array<AiResult, kQueueSize> slots {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiWorker)
};
