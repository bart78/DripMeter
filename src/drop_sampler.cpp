// ============================================================================
//  drop_sampler.cpp — implementation
//
//  Detection algorithm (all inside the FreeRTOS task on dedicated Core 1):
//
//    1. Poll the ADC continuously. Each water drop impact produces a sharp
//       200-400 Hz acoustic transient on the biased piezo disc.
//    2. Pass raw samples through a 150 Hz one-pole high-pass filter to reject
//       low-frequency structure-borne rumble, then maintain a sliding 128-sample
//       AC-RMS amplitude envelope updated every 16 samples (~0.85 ms).
//    3. Compute the adaptive trigger threshold:
//         threshold = envBase + margin + min(envNoise * noiseMult, margin)
//    4. On an upward crossing (while armed and past refractoryMs):
//         - Latch trigger timestamp (gettimeofday) and seed `peak` with the
//           maximum raw ADC value observed across the triggering 16-sample
//           window (so initial sub-millisecond spikes are never missed).
//         - Track the highest `raw` and `env` readings across `captureWindowMs`.
//         - Push the completed DropEvent to the lock-free SPSC ring.
//         - Lock out re-triggers until the envelope remains below threshold
//           continuously for `releaseMs` (swallowing tub ring-tails and
//           secondary splash ripples so one drop = one event).
// ============================================================================
#include "drop_sampler.h"
#include <sys/time.h>
#include <math.h>

void DropSampler::begin()
{
    // Rolling WAV recorder ring in PSRAM (~2.2 MB for 60 s @ ~18.5 kS/s).
    _wavCap  = (uint32_t)WAV_SECONDS * 19000;
    _wavRing = (int16_t *)ps_malloc(_wavCap * sizeof(int16_t));
    if (_wavRing)
        DBG_PRINTF("[WAV] recorder ring ready (%u s)\n", (unsigned)WAV_SECONDS);
    else
        DBG_PRINTLN("[WAV] PSRAM alloc failed - recorder disabled");

    xTaskCreatePinnedToCore(
        taskEntry,              // trampoline
        "drop_sampler",         // task name
        SAMPLER_STACK_BYTES,    // stack
        this,                   // argument
        SAMPLER_PRIORITY,       // priority 20 on isolated Core 1
        &_task,                 // handle
        SAMPLER_CORE);          // Core 1 (UI, WiFi, and Web live on Core 0)
}

int16_t DropSampler::wavRingAt(uint32_t i) const
{
    const uint32_t fill = _wavFill.load(std::memory_order_relaxed);
    if (!_wavRing || i >= fill)
        return 0;
    const uint32_t start = (fill >= _wavCap) ? _wavPos : 0;
    return _wavRing[(start + i) % _wavCap];
}

void DropSampler::taskEntry(void *arg)
{
    static_cast<DropSampler *>(arg)->run();
    vTaskDelete(nullptr);       // never reached
}

void DropSampler::run()
{
    // --- One-time ADC setup, in THIS task's context only --------------------
    uint16_t initRaw = 0;
#if DROPMETER_ADC_ENABLED
    analogReadResolution(12);                       // 0..4095
    initRaw = (uint16_t)analogRead(PIN_PIEZO_SIGNAL); // attach pin first
    analogSetPinAttenuation(PIN_PIEZO_SIGNAL, ADC_ATTENUATION);
    initRaw = (uint16_t)analogRead(PIN_PIEZO_SIGNAL);
#endif

    // --- High-pass + AC-RMS envelope state ----------------------------------
    // Initialize hpA and hpPrev BEFORE entering the loop so there is no
    // filter switchover step-transient at t = 1.0s after boot.
    int16_t  rmsRing[ENV_RMS_WINDOW] = {0};
    uint16_t rmsPos   = 0;
    int32_t  rmsSum   = 0;
    int32_t  rmsSumSq = 0;
    uint16_t rmsFill  = 0;
    uint16_t winIdx   = 0;
    uint16_t winRawMax = 0;     // max raw sample inside the current 16-sample window

    float    hpPrev    = (float)(int16_t)initRaw;
    float    hpPrevOut = 0.0f;
    float    hpA       = expf(-6.2831853f * (float)ENV_HIGHPASS_HZ / 18000.0f);
    if (hpA < 0.80f) hpA = 0.80f;
    if (hpA > 0.98f) hpA = 0.98f;

    int32_t  envBase  = 0;              // EMA of quiet envelope level
    int32_t  envNoise = 1;              // EMA of |env - envBase|
    uint16_t thr      = _margin.load(std::memory_order_relaxed);
    _threshold.store(thr, std::memory_order_relaxed);

    // --- Trigger, capture, and release state machine ------------------------
    const uint32_t bootMs = millis();
    uint32_t lastHitMs   = 0;           // millis() of last accepted trigger
    uint32_t capStartMs  = 0;           // start of peak-tracking window
    uint16_t capLenMs    = CAPTURE_WINDOW_MS;
    uint16_t peak        = 0;           // running raw peak of current capture
    uint16_t trigEnvPeak = 0;           // running env peak of current capture
    int64_t  trigEpochUs = 0;           // epoch at trigger onset
    bool     capturing   = false;
    bool     armed       = false;       // warm up baseline for 500 ms before arming
    bool     warmedUp    = false;
    uint32_t quietSince  = 0;

    // --- Decimated scope history (peak-hold over 8 envelope steps ~7ms) -----
    uint8_t  histSub     = 0;
    uint16_t histPeakEnv = 0;
    uint8_t  histMaxSt   = 0;

    // --- Sample-rate metering (updated once per second) ---------------------
    uint32_t spsWindow  = 0;
    uint32_t spsStartMs = millis();
    uint32_t idleYield  = 0;

    for (;;)
    {
#if DROPMETER_ADC_ENABLED
        const uint16_t raw = (uint16_t)analogRead(PIN_PIEZO_SIGNAL);
#else
        const uint16_t raw = (uint16_t)(_raw.load(std::memory_order_relaxed) + 1);
#endif
        const uint32_t now = millis();

        _raw.store(raw, std::memory_order_relaxed);
        if (raw > winRawMax)
            winRawMax = raw;

        // Track raw peak sample-by-sample during an active capture window.
        if (capturing && raw > peak)
            peak = raw;

        // One-pole 150 Hz high-pass filter before the RMS window.
        const float cur = (float)(int16_t)raw;
        const float hp  = (cur - hpPrev) + hpA * hpPrevOut;
        hpPrev    = cur;
        hpPrevOut = hp;

        // Rolling WAV ring: store high-passed AC signal scaled to 16-bit PCM.
        if (_wavRing)
        {
            int32_t pcm = (int32_t)hp * 8;
            if (pcm > 32767)  pcm = 32767;
            if (pcm < -32768) pcm = -32768;
            _wavRing[_wavPos] = (int16_t)pcm;
            if (++_wavPos >= _wavCap)
                _wavPos = 0;
            const uint32_t fill = _wavFill.load(std::memory_order_relaxed);
            if (fill < _wavCap)
                _wavFill.store(fill + 1, std::memory_order_relaxed);
        }

        // Sliding AC-RMS amplitude envelope bookkeeping.
        const int16_t r = (int16_t)hp;
        if (rmsFill < ENV_RMS_WINDOW)
        {
            rmsSum   += r;
            rmsSumSq += (int32_t)r * r;
            ++rmsFill;
        }
        else
        {
            const int16_t old = rmsRing[rmsPos];
            rmsSum   += (int32_t)r - old;
            rmsSumSq += (int32_t)r * r - (int32_t)old * old;
        }
        rmsRing[rmsPos] = r;
        rmsPos = (rmsPos + 1) & (ENV_RMS_WINDOW - 1);

        // --- Envelope window boundary (every 16 samples, ~0.85 ms) ----------
        if (++winIdx >= ENV_WINDOW)
        {
            winIdx = 0;
            const uint16_t curWinPeak = winRawMax;
            winRawMax = 0;

            uint16_t env = 0;
            if (rmsFill >= ENV_RMS_WINDOW)
            {
                const float mean = (float)rmsSum / (float)ENV_RMS_WINDOW;
                const float var  = (float)rmsSumSq / (float)ENV_RMS_WINDOW - mean * mean;
                env = (var > 0.0f) ? (uint16_t)sqrtf(var) : 0;
            }
            _envelope.store(env, std::memory_order_relaxed);

            const uint16_t marginFloor = _margin.load(std::memory_order_relaxed);
            const uint16_t refractMs   = _refractoryMs.load(std::memory_order_relaxed);

            if (capturing)
            {
                if (env > trigEnvPeak)
                    trigEnvPeak = env;
                if (curWinPeak > peak)
                    peak = curWinPeak;

                // Capture window complete: publish DropEvent and enter release holdoff.
                if (now - capStartMs >= capLenMs)
                {
                    _lastPeak.store(peak, std::memory_order_relaxed);
                    _lastEnvPeak.store(trigEnvPeak, std::memory_order_relaxed);
                    if (!_paused.load(std::memory_order_relaxed))
                    {
                        DropEvent ev;
                        ev.epochUs = trigEpochUs;
                        ev.peak    = peak;
                        ev.envPeak = trigEnvPeak;
                        if (_ring.push(ev))
                        {
                            _lastEpochUs.store(trigEpochUs, std::memory_order_relaxed);
                        }
                    }
                    capturing  = false;
                    armed      = false;
                    quietSince = (env < thr) ? now : 0;
                }
            }
            else
            {
                if (!warmedUp)
                {
                    if (envBase == 0 && env > 0)
                        envBase = env;
                    else
                        envBase += ((int32_t)env - envBase) >> 3;
                    if (now - bootMs >= 500)
                    {
                        warmedUp = true;
                        armed    = true;
                    }
                }
                else if (!armed)
                {
                    // Retriggerable quiet-release: stay locked out until the signal
                    // has stayed below threshold continuously for releaseMs.
                    if (env < thr)
                    {
                        if (quietSince == 0)
                            quietSince = now;
                        else if (now - quietSince >= _releaseMs.load(std::memory_order_relaxed))
                        {
                            armed      = true;
                            quietSince = 0;
                        }
                    }
                    else
                    {
                        quietSince = 0;
                    }
                }

                const bool pastRefract = (lastHitMs == 0) || (now - lastHitMs >= refractMs);
                // If locked in RELEASE for >2s, ambient noise floor has shifted upward;
                // allow baseline EMA to adapt so the detector never deadlocks.
                const bool stuckInRelease = !armed && warmedUp && (now - lastHitMs >= 2000);

                if (warmedUp && armed && pastRefract && env > thr)
                {
                    // --- New drop impact accepted ---------------------------
                    lastHitMs   = now;
                    capStartMs  = now;
                    capLenMs    = _captureMs.load(std::memory_order_relaxed);
                    peak        = curWinPeak > raw ? curWinPeak : raw;
                    trigEnvPeak = env;
                    capturing   = true;

                    struct timeval tv;
                    gettimeofday(&tv, nullptr);
                    trigEpochUs = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
                    if (!_paused.load(std::memory_order_relaxed))
                        _triggers.fetch_add(1, std::memory_order_relaxed);
                }
                else if (!warmedUp || env <= thr || stuckInRelease)
                {
                    // Update baseline & ambient noise EMAs only on non-triggering
                    // samples (or prolonged >2s ambient shifts) so drop transients
                    // don't inflate the baseline.
                    const uint8_t noiseMult = _noiseMult.load(std::memory_order_relaxed);

                    const int32_t dev = (int32_t)env - envBase;
                    envNoise += (abs(dev) - envNoise) >>
                                (dev > 0 ? NOISE_ATTACK_ALPHA : NOISE_DECAY_ALPHA);
                    if (envNoise < 0)
                        envNoise = 0;
                    envBase += dev >> (stuckInRelease ? 5 : BASELINE_ALPHA);
                    if (envBase < 0)
                        envBase = 0;

                    _envBase.store((uint16_t)envBase, std::memory_order_relaxed);
                    _envNoise.store((uint16_t)envNoise, std::memory_order_relaxed);

                    uint32_t boost = (uint32_t)envNoise * noiseMult;
                    if (boost > marginFloor)
                        boost = marginFloor;
                    int32_t t = envBase + (int32_t)marginFloor + (int32_t)boost;
                    if (t > THR_MAX)
                        t = THR_MAX;
                    if (t < 4)
                        t = 4;
                    thr = (uint16_t)t;
                    _threshold.store(thr, std::memory_order_relaxed);
                }
            }

            // Current detector state: 0 = armed, 1 = capturing, 2 = release/refractory holdoff
            const uint8_t curState = capturing ? 1 : ((!armed || (lastHitMs != 0 && (now - lastHitMs < refractMs))) ? 2 : 0);
            _state.store(curState, std::memory_order_relaxed);

            // Peak-holding decimated history for the live web scope (every 12 env
            // updates ≈ 10 ms per point -> 512 points ≈ 5.2 seconds of high-detail trace).
            if (env > histPeakEnv)
                histPeakEnv = env;
            if (curState == 1 || (curState == 2 && histMaxSt == 0))
                histMaxSt = curState;

            if (++histSub >= 12)
            {
                histSub = 0;
                const uint32_t hIdx =
                    _envHistCount.load(std::memory_order_relaxed) & (kEnvHist - 1);
                _envHist[hIdx]   = histPeakEnv;
                _stateHist[hIdx] = histMaxSt;
                _envHistCount.fetch_add(1, std::memory_order_relaxed);
                histPeakEnv = 0;
                histMaxSt   = 0;
            }
        }

        // Sample-rate metering + smooth high-pass coefficient update (1 Hz).
        if (now - spsStartMs >= 1000)
        {
            _sps.store(spsWindow, std::memory_order_relaxed);
            if (spsWindow > 2000)
            {
                float a = expf(-6.2831853f * (float)ENV_HIGHPASS_HZ / (float)spsWindow);
                if (a < 0.80f) a = 0.80f;
                if (a > 0.98f) a = 0.98f;
                hpA = a;
            }
            spsWindow  = 0;
            spsStartMs = now;
        }
        else
        {
            ++spsWindow;
        }

        // Only yield to Core 1 IDLE task occasionally (~every 4096 samples) AND
        // only while armed and dead-quiet so we NEVER create a 1ms blind spot
        // during a drop onset or capture window.
        if (++idleYield >= 4096)
        {
            if (armed && !capturing && _envelope.load(std::memory_order_relaxed) <= (uint16_t)(envBase + 2))
            {
                idleYield = 0;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }
}
