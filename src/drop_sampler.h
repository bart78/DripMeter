// ============================================================================
//  drop_sampler.h — high-rate piezo ADC sampling on a dedicated core
// ============================================================================
#pragma once

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "drop_event.h"

class DropSampler
{
public:
    // Spawn the FreeRTOS sampling task. All ADC work happens inside it.
    void begin();

    // Consumer side (application loop) — drain buffered drop events.
    bool pop(DropEvent &out) { return _ring.pop(out); }

    // Discard any buffered events (used when wiping the log so events
    // captured just before the wipe don't land in the fresh file).
    void discardEvents() { DropEvent d; while (_ring.pop(d)) {} }

    // Live telemetry for the dashboard & web UI (all lock-free reads).
    uint16_t rawADC()            const { return _raw.load(std::memory_order_relaxed); }
    uint16_t envelope()          const { return _envelope.load(std::memory_order_relaxed); }
    uint16_t envBase()           const { return _envBase.load(std::memory_order_relaxed); }
    uint16_t envNoise()          const { return _envNoise.load(std::memory_order_relaxed); }
    uint16_t threshold()         const { return _threshold.load(std::memory_order_relaxed); }
    uint32_t triggerCount()      const { return _triggers.load(std::memory_order_relaxed); }
    uint32_t samplesPerSecond()  const { return _sps.load(std::memory_order_relaxed); }
    int64_t  lastDropEpochUs()   const { return _lastEpochUs.load(std::memory_order_relaxed); }
    uint16_t lastPeak()          const { return _lastPeak.load(std::memory_order_relaxed); }
    uint16_t lastEnvPeak()       const { return _lastEnvPeak.load(std::memory_order_relaxed); }
    // 0 = armed (ready), 1 = capturing peak, 2 = release/refractory holdoff
    uint8_t  detectorState()     const { return _state.load(std::memory_order_relaxed); }

    // Runtime sensitivity & timing calibration (persisted via Tuning in NVS).
    void     setMargin(uint16_t m)          { _margin.store(m, std::memory_order_relaxed); }
    void     setNoiseMult(uint8_t n)        { _noiseMult.store(n, std::memory_order_relaxed); }
    void     setCaptureWindowMs(uint16_t m) { _captureMs.store(m, std::memory_order_relaxed); }
    void     setReleaseMs(uint16_t m)       { _releaseMs.store(m, std::memory_order_relaxed); }
    void     setRefractoryMs(uint16_t m)    { _refractoryMs.store(m, std::memory_order_relaxed); }

    uint16_t margin()          const { return _margin.load(std::memory_order_relaxed); }
    uint8_t  noiseMult()       const { return _noiseMult.load(std::memory_order_relaxed); }
    uint16_t captureWindowMs() const { return _captureMs.load(std::memory_order_relaxed); }
    uint16_t releaseMs()       const { return _releaseMs.load(std::memory_order_relaxed); }
    uint16_t refractoryMs()    const { return _refractoryMs.load(std::memory_order_relaxed); }

    // Peak-holding decimated history for the live web scope (512 points).
    static const uint16_t kEnvHist = 512;
    uint16_t envHistAt(uint32_t i)   const { return _envHist[i & (kEnvHist - 1)]; }
    uint8_t  stateHistAt(uint32_t i) const { return _stateHist[i & (kEnvHist - 1)]; }
    uint32_t envHistCount() const { return _envHistCount.load(std::memory_order_relaxed); }

    // Rolling WAV recorder (PSRAM ring of the last WAV_SECONDS of audio).
    bool     wavOK()   const { return _wavRing != nullptr; }
    uint32_t wavCap()  const { return _wavCap; }
    uint32_t wavFill() const { return _wavFill.load(std::memory_order_relaxed); }
    // Logical index 0 = oldest sample still in the ring.
    int16_t  wavRingAt(uint32_t i) const;

private:
    static void taskEntry(void *arg);   // FreeRTOS trampoline
    void run();                         // the actual sampling loop

    SPSCRingBuffer<DropEvent, RING_CAPACITY> _ring;
    TaskHandle_t _task = nullptr;

    std::atomic<uint16_t> _raw{0};                  // most recent ADC sample
    std::atomic<uint16_t> _envelope{0};             // transient AC-RMS envelope
    std::atomic<uint16_t> _envBase{0};              // quiet envelope baseline
    std::atomic<uint16_t> _envNoise{0};             // ambient envelope noise EMA
    std::atomic<uint16_t> _threshold{ENV_MIN_MARGIN}; // live adaptive trigger
    std::atomic<uint32_t> _triggers{0};             // total impacts since boot
    std::atomic<uint32_t> _sps{0};                  // measured sample rate
    std::atomic<int64_t>  _lastEpochUs{0};          // timestamp of last impact
    std::atomic<uint16_t> _lastPeak{0};             // peak raw ADC of last impact
    std::atomic<uint16_t> _lastEnvPeak{0};          // peak envelope of last impact
    std::atomic<uint8_t>  _state{0};                // 0=armed, 1=capture, 2=release

    // Calibration knobs (loaded from NVS at boot, updated via /set).
    std::atomic<uint16_t> _margin{ENV_MIN_MARGIN};
    std::atomic<uint8_t>  _noiseMult{TRIGGER_NOISE_MULT};
    std::atomic<uint16_t> _captureMs{CAPTURE_WINDOW_MS};
    std::atomic<uint16_t> _releaseMs{RELEASE_MS};
    std::atomic<uint16_t> _refractoryMs{REFRACTORY_MS};

    // Peak-holding decimated history ring for the live web scope.
    uint16_t              _envHist[kEnvHist]   = {0};
    uint8_t               _stateHist[kEnvHist] = {0};
    std::atomic<uint32_t> _envHistCount{0};

    // Rolling WAV ring (PSRAM).
    int16_t              *_wavRing = nullptr;
    uint32_t              _wavCap  = 0;
    uint32_t              _wavPos  = 0;
    std::atomic<uint32_t> _wavFill{0};
};
