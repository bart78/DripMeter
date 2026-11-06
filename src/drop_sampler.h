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

    // --- Pause Data Gathering (Live Scope stays active) ---------------------
    void     setPaused(bool p)
    {
        _paused.store(p, std::memory_order_relaxed);
        if (p)
            discardEvents();
    }
    bool     paused() const { return _paused.load(std::memory_order_relaxed); }

    // --- Water Quantity & In-Place Scale Calibration ------------------------
    void     setQtyEnabled(bool en)          { _qtyEnabled.store(en, std::memory_order_relaxed); }
    void     setUlPerDropTenths(uint16_t t)  { _ulPerDropTenths.store(t, std::memory_order_relaxed); }
    void     setCalAvgEnvTenths(uint16_t t)  { _calAvgEnvTenths.store(t, std::memory_order_relaxed); }

    bool     qtyEnabled()      const { return _qtyEnabled.load(std::memory_order_relaxed); }
    uint16_t ulPerDropTenths() const { return _ulPerDropTenths.load(std::memory_order_relaxed); }
    uint16_t calAvgEnvTenths() const { return _calAvgEnvTenths.load(std::memory_order_relaxed); }

    // Estimate volume in microliters (uL) for a single drop impact.
    // Uses 80% calibrated mean drop mass + 20% EnvPeak ratio (clamped 0.5x..1.5x)
    // so residual water film variance cancels out while onset/tail drops scale naturally.
    uint16_t estimateDropUl(uint16_t envPeak) const
    {
        const float baseUl = (float)_ulPerDropTenths.load(std::memory_order_relaxed) * 0.1f;
        const float avgEnv = (float)_calAvgEnvTenths.load(std::memory_order_relaxed) * 0.1f;
        float ratio = (avgEnv > 1.0f && envPeak > 0) ? ((float)envPeak / avgEnv) : 1.0f;
        if (ratio < 0.5f) ratio = 0.5f;
        if (ratio > 1.5f) ratio = 1.5f;
        const float blend = 0.80f + 0.20f * ratio;
        int32_t ul = (int32_t)(baseUl * blend + 0.5f);
        if (ul < 1) ul = 1;
        if (ul > 5000) ul = 5000;
        return (uint16_t)ul;
    }

    // Live scale calibration counter (Start -> collect drops -> Stop & Weigh).
    void startCalRun(int64_t nowEpochSec)
    {
        _calDrops.store(0, std::memory_order_relaxed);
        _calEnvSum.store(0, std::memory_order_relaxed);
        _calStartEpoch.store(nowEpochSec, std::memory_order_relaxed);
        _calStopEpoch.store(0, std::memory_order_relaxed);
        _calRunning.store(true, std::memory_order_relaxed);
    }
    void stopCalRun(int64_t nowEpochSec)
    {
        if (_calRunning.load(std::memory_order_relaxed))
        {
            _calStopEpoch.store(nowEpochSec, std::memory_order_relaxed);
            _calRunning.store(false, std::memory_order_relaxed);
        }
    }
    void resetCalRun()
    {
        _calRunning.store(false, std::memory_order_relaxed);
        _calDrops.store(0, std::memory_order_relaxed);
        _calEnvSum.store(0, std::memory_order_relaxed);
        _calStartEpoch.store(0, std::memory_order_relaxed);
        _calStopEpoch.store(0, std::memory_order_relaxed);
    }
    void recordCalDrop(uint16_t envPeak)
    {
        if (_calRunning.load(std::memory_order_relaxed))
        {
            _calDrops.fetch_add(1, std::memory_order_relaxed);
            _calEnvSum.fetch_add(envPeak, std::memory_order_relaxed);
        }
    }
    bool     calRunning()    const { return _calRunning.load(std::memory_order_relaxed); }
    uint32_t calDrops()      const { return _calDrops.load(std::memory_order_relaxed); }
    uint32_t calEnvSum()     const { return _calEnvSum.load(std::memory_order_relaxed); }
    int64_t  calStartEpoch() const { return _calStartEpoch.load(std::memory_order_relaxed); }
    int64_t  calStopEpoch()  const { return _calStopEpoch.load(std::memory_order_relaxed); }

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

    // Water quantity & calibration session state.
    std::atomic<bool>     _paused{false};
    std::atomic<bool>     _qtyEnabled{DEFAULT_QTY_ENABLED != 0};
    std::atomic<uint16_t> _ulPerDropTenths{DEFAULT_UL_TENTHS};
    std::atomic<uint16_t> _calAvgEnvTenths{DEFAULT_CAL_ENV_TENTHS};
    std::atomic<bool>     _calRunning{false};
    std::atomic<uint32_t> _calDrops{0};
    std::atomic<uint32_t> _calEnvSum{0};
    std::atomic<int64_t>  _calStartEpoch{0};
    std::atomic<int64_t>  _calStopEpoch{0};

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
