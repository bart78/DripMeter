// ============================================================================
//  drop_sampler.h — high-rate piezo ADC sampling on a dedicated core
// ============================================================================
#pragma once

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "drop_event.h"
#include "drop_template.h"

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

    // Live telemetry for the dashboard (all lock-free reads).
    uint16_t rawADC()            const { return _raw.load(std::memory_order_relaxed); }
    uint16_t envelope()          const { return _envelope.load(std::memory_order_relaxed); }
    uint16_t envBase()           const { return _envBase.load(std::memory_order_relaxed); }
    uint16_t matchScore()        const { return _matchScore.load(std::memory_order_relaxed); }
    uint16_t threshold()         const { return _threshold.load(std::memory_order_relaxed); }
    uint32_t triggerCount()      const { return _triggers.load(std::memory_order_relaxed); }
    uint32_t samplesPerSecond()  const { return _sps.load(std::memory_order_relaxed); }
    int64_t  lastDropEpochUs()   const { return _lastEpochUs.load(std::memory_order_relaxed); }
    uint16_t lastPeak()          const { return _lastPeak.load(std::memory_order_relaxed); }

    // Runtime sensitivity calibration (persisted via Tuning in NVS).
    void     setMargin(uint16_t m) { _margin.store(m, std::memory_order_relaxed); }
    void     setNoiseMult(uint8_t n) { _noiseMult.store(n, std::memory_order_relaxed); }
    uint16_t margin()      const { return _margin.load(std::memory_order_relaxed); }
    uint8_t  noiseMult()   const { return _noiseMult.load(std::memory_order_relaxed); }

    // Rate-limiting knobs (also persisted). See the throughput notes in
    // config.h: refractory sets the maximum drop rate; release suppresses
    // ring-tail re-triggers (one tap = one event).
    void     setRefractoryMs(uint16_t ms) { _refractoryMs.store(ms, std::memory_order_relaxed); }
    void     setCaptureWindowMs(uint16_t ms) { _captureMs.store(ms, std::memory_order_relaxed); }
    void     setReleaseMs(uint16_t ms) { _releaseMs.store(ms, std::memory_order_relaxed); }
    uint16_t refractoryMs()   const { return _refractoryMs.load(std::memory_order_relaxed); }
    uint16_t captureWindowMs() const { return _captureMs.load(std::memory_order_relaxed); }
    uint16_t releaseMs()      const { return _releaseMs.load(std::memory_order_relaxed); }

    // Detection mode: 0 = envelope only, 1 = template match only,
    // 2 = BOTH (envelope AND match — recommended for noisy rooms),
    // 3 = EITHER (envelope OR match).
    void     setDetectMode(uint8_t m) { _detectMode.store(m & 3, std::memory_order_relaxed); }
    uint8_t  detectMode() const       { return _detectMode.load(std::memory_order_relaxed); }
    void  setMatchThr(uint16_t milli) { _matchThr.store(milli, std::memory_order_relaxed); }
    uint16_t matchThr() const   { return _matchThr.load(std::memory_order_relaxed); }
    void     setMatchMinRms(uint8_t v) { _matchMinRms.store(v, std::memory_order_relaxed); }
    uint8_t  matchMinRms() const { return _matchMinRms.load(std::memory_order_relaxed); }

    DropTemplate       &templateStore()       { return _tpl; }
    const DropTemplate &templateStore() const { return _tpl; }
    bool templateLearned() const { return _tpl.valid(); }
    DropTemplate       &noiseStore()          { return _noiseTpl; }
    bool noiseLearned() const { return _noiseTpl.valid(); }

    // One-shot learning: arm, then make drops (or play the noise source).
    // The target template is chosen by the endpoint used.
    void armRecord()
    {
        _learnTarget.store(0, std::memory_order_relaxed);
        _recordArmed.store(true, std::memory_order_relaxed);
    }
    void armNoiseRecord()
    {
        _learnTarget.store(1, std::memory_order_relaxed);
        _recordArmed.store(true, std::memory_order_relaxed);
    }

    // Rolling WAV recorder (PSRAM ring of the last WAV_SECONDS of audio).
    bool     wavOK()   const { return _wavRing != nullptr; }
    uint32_t wavCap()  const { return _wavCap; }
    uint32_t wavFill() const { return _wavFill.load(std::memory_order_relaxed); }
    // Logical index 0 = oldest sample still in the ring.
    int16_t wavRingAt(uint32_t i) const;

    bool recordArmed() const { return _recordArmed.load(std::memory_order_relaxed); }
    uint16_t learnBestPeak() const { return _learnBestPeak.load(std::memory_order_relaxed); }
    int16_t learnCapAt(uint16_t i) const
    {
        return (i < DropTemplate::kLen) ? _learnCap[i] : 0;
    }

    // Auto-calibration: measure 10 s of quiet, then ask for a few drops,
    // then auto-compute the margin (applied by the caller) and average the
    // drop captures into the template.
    void startAutoCal();
    uint8_t  calPhase()    const { return _calPhase.load(std::memory_order_relaxed); }
    uint8_t  calDropCount() const { return _calDropCount.load(std::memory_order_relaxed); }
    uint16_t calNoiseCeil() const { return _calNoiseCeil.load(std::memory_order_relaxed); }
    uint16_t calDropPeak(uint8_t i) const
    {
        return (i < 4) ? _calDropPeaks[i].load(std::memory_order_relaxed) : 0;
    }
    bool calDone() const { return _calDone.load(std::memory_order_relaxed); }

    // Decimated history for the live chart AND the calibration flight
    // recorder (~50 s across the screen at 4096 points; phase-stamped so
    // a dumped session shows which phase every sample belonged to).
    static const uint16_t kEnvHist = 4096;
    uint16_t envHistAt(uint16_t i) const { return _envHist[i & (kEnvHist - 1)]; }
    uint16_t matchHistAt(uint16_t i) const { return _matchHist[i & (kEnvHist - 1)]; }
    uint8_t  phaseHistAt(uint16_t i) const { return _phaseHist[i & (kEnvHist - 1)]; }
    uint32_t envHistCount() const { return _envHistCount.load(std::memory_order_relaxed); }

    // Raw captures of the calibration candidates (for offline analysis).
    uint8_t calCapCount() const { return _calCapCount.load(std::memory_order_relaxed); }
    int16_t calCapAt(uint8_t n, uint16_t i) const
    {
        return (n < CAL_MAX_DROPS && i < DropTemplate::kLen)
                   ? _calCaps[n][i]
                   : 0;
    }

private:
    static void taskEntry(void *arg);   // FreeRTOS trampoline
    void run();                         // the actual sampling loop

    SPSCRingBuffer<DropEvent, RING_CAPACITY> _ring;
    TaskHandle_t _task = nullptr;
    DropTemplate _tpl;              // drop signature ("/template.bin")
    DropTemplate _noiseTpl;         // noise signature ("/noise.bin")
    std::atomic<uint8_t> _learnTarget{0};   // 0 = drop, 1 = noise

    std::atomic<uint16_t> _raw{0};          // most recent ADC sample
    std::atomic<uint16_t> _envelope{0};     // transient envelope level
    std::atomic<uint16_t> _envBase{0};      // quiet envelope baseline
    std::atomic<uint16_t> _matchScore{0};   // latest correlation (milli)
    std::atomic<uint16_t> _threshold{ENV_MIN_MARGIN}; // adaptive trigger
    std::atomic<uint32_t> _triggers{0};     // total impacts since boot
    std::atomic<uint32_t> _sps{0};          // measured sample rate
    std::atomic<int64_t>  _lastEpochUs{0};  // timestamp of last impact
    std::atomic<uint16_t> _lastPeak{0};     // peak raw ADC of last impact
    std::atomic<uint16_t> _margin{ENV_MIN_MARGIN};  // calibration knob
    std::atomic<uint8_t>  _noiseMult{TRIGGER_NOISE_MULT};
    std::atomic<uint16_t> _refractoryMs{REFRACTORY_MS};
    std::atomic<uint16_t> _captureMs{CAPTURE_WINDOW_MS};
    std::atomic<uint16_t> _releaseMs{RELEASE_MS};
    std::atomic<uint8_t>  _detectMode{0};          // 0 env / 1 match /
                                                   // 2 both / 3 either
    std::atomic<uint16_t> _matchThr{MATCH_THR_DEFAULT};
    std::atomic<uint8_t>  _matchMinRms{MATCH_MIN_RMS};
    std::atomic<bool>     _recordArmed{false};
    std::atomic<uint16_t> _learnBestPeak{0};
    int16_t               _learnCap[DropTemplate::kLen] = {0};

    // Auto-calibration state (driven by the sampler task).
    std::atomic<uint8_t>  _calPhase{0};             // 0 idle / 1 quiet / 2 drop
    std::atomic<uint32_t> _calStartMs{0};
    std::atomic<uint16_t> _calNoiseCeil{0};
    std::atomic<uint16_t> _calDropPeaks[CAL_MAX_DROPS];
    std::atomic<uint8_t>  _calDropCount{0};
    std::atomic<bool>     _calDone{false};

    // Decimated history rings for the live chart + calibration dump.
    uint16_t              _envHist[kEnvHist] = {0};
    uint16_t              _matchHist[kEnvHist] = {0};
    uint8_t               _phaseHist[kEnvHist] = {0};
    std::atomic<uint32_t> _envHistCount{0};

    // Raw captures of calibration candidates.
    int16_t               _calCaps[CAL_MAX_DROPS][DropTemplate::kLen] = {0};
    std::atomic<uint8_t>  _calCapCount{0};

    // Rolling WAV ring (PSRAM).
    int16_t              *_wavRing = nullptr;
    uint32_t              _wavCap = 0;
    uint32_t              _wavPos = 0;
    std::atomic<uint32_t> _wavFill{0};
};
