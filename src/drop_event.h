// ============================================================================
//  drop_event.h — event payload + lock-free SPSC ring buffer
//
//  The ADC sampler task (producer, core 1, priority 20) pushes DropEvent
//  records here; the application loop (consumer, core 0) drains them.
//  Screen redraws and file writes never block the sampler — no drop transient
//  is lost to rendering or I/O work.
//
//  Correctness notes:
//    * Capacity must be a power of two; indices wrap with a bitmask.
//    * Single producer + single consumer: index updates use
//      acquire/release ordering only — no locks, no disabled interrupts.
//    * On overflow the NEWEST event is dropped (a full ring means the
//      consumer is stalled; older events are still being flushed).
// ============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>

// One detected drop impact.
struct DropEvent
{
    int64_t  epochUs;   // trigger instant, microseconds since Unix epoch (UTC)
    uint16_t peak;      // peak raw ADC reading within the trigger + capture window
    uint16_t envPeak;   // peak AC-RMS envelope during the capture window
};

template <typename T, size_t N>
class SPSCRingBuffer
{
    static_assert((N & (N - 1)) == 0, "SPSCRingBuffer capacity must be a power of two");
    static_assert(N > 0, "SPSCRingBuffer capacity must be non-zero");

    T                  _buf[N];
    std::atomic<uint32_t> _read{0};
    std::atomic<uint32_t> _write{0};

public:
    // Producer: returns false when the ring is full (event dropped).
    bool push(const T &item)
    {
        const uint32_t w = _write.load(std::memory_order_relaxed);
        const uint32_t r = _read.load(std::memory_order_acquire);
        if (w - r >= N)          // full
            return false;
        _buf[w & (N - 1)] = item;
        _write.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumer: returns false when the ring is empty.
    bool pop(T &out)
    {
        const uint32_t r = _read.load(std::memory_order_relaxed);
        if (r == _write.load(std::memory_order_acquire))
            return false;
        out = _buf[r & (N - 1)];
        _read.store(r + 1, std::memory_order_release);
        return true;
    }

    uint32_t count() const
    {
        return _write.load(std::memory_order_relaxed) -
               _read.load(std::memory_order_relaxed);
    }
};
