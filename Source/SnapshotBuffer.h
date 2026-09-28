#pragma once
#include <array>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
//  SnapshotBuffer — hands the newest value of a large struct from one writer
//  thread to one reader thread with no locks, no allocation and no tearing
//  (a triple buffer). The writer fills back() and calls publish(); the reader
//  calls latest() and gets the newest complete value. The writer never
//  touches the slot the reader holds, so the reader sees one whole value —
//  never half of one and half of the next. Both sides are wait-free.
//
//  One writer thread and one reader thread at a time. The reference latest()
//  returns stays valid and unchanged until the reader calls latest() again.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class SnapshotBuffer
{
public:
    T& back() noexcept { return slots[(size_t) backIndex]; }   // writer

    void publish() noexcept                                    // writer
    {
        backIndex = middle.exchange (backIndex | kFresh, std::memory_order_acq_rel) & kIndexMask;
    }

    const T& latest() noexcept                                 // reader
    {
        if ((middle.load (std::memory_order_acquire) & kFresh) != 0)
            frontIndex = middle.exchange (frontIndex, std::memory_order_acq_rel) & kIndexMask;
        return slots[(size_t) frontIndex];
    }

private:
    static constexpr int kIndexMask = 3, kFresh = 4;

    std::array<T, 3> slots {};
    int backIndex  { 0 };            // writer only
    int frontIndex { 2 };            // reader only
    std::atomic<int> middle { 1 };   // the slot in between, | kFresh when unread
};
