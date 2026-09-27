#include "AiWorker.h"
#include <exception>

namespace
{
    void setMessage (AiResult& r, const juce::String& text)
    {
        text.copyToUTF8 (r.message, sizeof (r.message));
    }

    juce::String summary (const AiPayload& p)
    {
        juce::String s (p.numChanges);
        s << (p.numChanges == 1 ? " parameter change" : " parameter changes");
        if (p.setsTrace)      s << (p.numTracePoints == 0 ? ", trace cleared" : ", new trace");
        if (p.numClamped > 0) s << ", " << p.numClamped << " clamped into range";
        return s;
    }
}

AiWorker::AiWorker (const juce::Array<juce::AudioProcessorParameter*>& params)
    : juce::Thread ("MixMind AI worker"), firewall (params)
{
}

AiWorker::~AiWorker()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);   // a well-behaved backend returns as soon as shouldCancel() is true
}

void AiWorker::setBackend (Backend b)
{
    const juce::ScopedLock sl (requestLock);
    backend = std::move (b);
}

int AiWorker::submit (const juce::String& text)
{
    int id = 0;
    {
        const juce::ScopedLock sl (requestLock);
        id = nextId++;
        requests.push_back ({ id, text });
    }
    if (! isThreadRunning())
        startThread();   // started on first use: an instance that never asks costs no thread
    notify();
    return id;
}

bool AiWorker::pop (AiResult& out)
{
    const auto scope = fifo.read (1);
    if (scope.blockSize1 == 0) return false;
    out = slots[(size_t) scope.startIndex1];
    return true;
}

void AiWorker::run()
{
    while (! threadShouldExit())
    {
        Request request;
        Backend fn;
        {
            const juce::ScopedLock sl (requestLock);
            if (! requests.empty())
            {
                request = std::move (requests.front());
                requests.pop_front();
                fn = backend;
            }
        }
        if (request.id == 0)
        {
            wait (-1);   // submit() / the destructor notify
            continue;
        }

        const auto result = handle (request, fn);
        if (threadShouldExit()) return;
        push (result);
    }
}

AiResult AiWorker::handle (const Request& request, const Backend& fn)
{
    AiResult r;
    r.requestId = request.id;
    if (! fn)
    {
        setMessage (r, "no AI model is configured");
        return r;
    }

    // An exception escaping this thread would take the host down with it.
    try
    {
        juce::String reply;
        const auto call = fn (request.text, reply, [this] { return threadShouldExit(); });
        if (call.failed())
        {
            setMessage (r, call.getErrorMessage());
            return r;
        }

        const auto verdict = firewall.validate (reply, r.payload);
        r.status = verdict.wasOk() ? AiResult::Status::accepted : AiResult::Status::rejected;
        setMessage (r, verdict.wasOk() ? summary (r.payload) : verdict.getErrorMessage());
    }
    catch (const std::exception& e)
    {
        r = {};
        r.requestId = request.id;
        setMessage (r, juce::String ("the model call threw: ") + e.what());
    }
    catch (...)
    {
        r = {};
        r.requestId = request.id;
        setMessage (r, "the model call threw");
    }
    return r;
}

void AiWorker::push (const AiResult& r)
{
    // Wait for room rather than drop a result. Only this thread ever waits;
    // the consumer's pop() never does.
    while (! threadShouldExit())
    {
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0)
            {
                slots[(size_t) scope.startIndex1] = r;
                return;
            }
        }
        wait (5);
    }
}
