// ============================================================================
//  drop_sampler.cpp — implementation
//
//  Detection algorithm (all inside the FreeRTOS task, no blocking calls):
//
//    1. Poll the ADC in a tight loop. A falling drop produces an IMPULSE —
//       a sharp, "clap"-like broadband transient — that the piezo turns
//       into a fast voltage spike.
//    2. Take the FIRST DIFFERENCE of the stream (d = raw[n] - raw[n-1]):
//       DC offset and slow baseline wander vanish, sharp onsets are
//       strongly emphasised. Rectify and peak-hold over ENV_WINDOW samples
//       to form the attack envelope.
//    3. An adaptive threshold rides just above the envelope's noise floor
//       (EMA of quiet samples): threshold = envBase + margin, where margin
//       = max(ENV_MIN_MARGIN, TRIGGER_NOISE_MULT x noise). Sustained hum
//       barely moves the first difference; claps spike it hard.
//    4. On a crossing, latch a candidate impact: remember the precise
//       epoch (gettimeofday), keep sampling for CAPTURE_WINDOW_MS while
//       tracking the highest RAW reading — that maximum is the event's
//       peak. The 400 ms refractory window swallows ringing, splash
//       droplets and double-bounces so one impact yields one event.
//    5. Push the finished DropEvent into the SPSC ring.
//
//  NOTE on context: ALL analogRead() calls live inside THIS task. Calling
//  them from the loop task froze the whole core on this core/BSP combo.
// ============================================================================
#include "drop_sampler.h"
#include <sys/time.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DropSampler::begin()
{
    _noiseTpl.setPath("/noise.bin");

    // Rolling WAV recorder ring in PSRAM (~2.2 MB for 60 s @ ~18 kS/s).
    _wavCap = (uint32_t)WAV_SECONDS * 18000;
    _wavRing = (int16_t *)ps_malloc(_wavCap * sizeof(int16_t));
    if (_wavRing)
        DBG_PRINTF("[WAV] recorder ring ready (%u s @ up to 18kS/s)\n",
                   (unsigned)WAV_SECONDS);
    else
        DBG_PRINTLN("[WAV] PSRAM alloc failed - recorder disabled");

    xTaskCreatePinnedToCore(
        taskEntry,              // trampoline
        "drop_sampler",         // task name
        SAMPLER_STACK_BYTES,    // stack
        this,                   // argument
        SAMPLER_PRIORITY,       // priority (configMAX_PRIORITIES-1 = 24)
        &_task,                 // handle (optional)
        SAMPLER_CORE);          // core 1, same core as the UI — the sampler
                                // preempts rendering, never the reverse
}

int16_t DropSampler::wavRingAt(uint32_t i) const
{
    const uint32_t fill = _wavFill.load(std::memory_order_relaxed);
    if (!_wavRing || i >= fill)
        return 0;
    const uint32_t start = (fill >= _wavCap) ? _wavPos : 0;
    return _wavRing[(start + i) % _wavCap];
}

void DropSampler::startAutoCal()
{
    _calNoiseCeil.store(0, std::memory_order_relaxed);
    _calDropCount.store(0, std::memory_order_relaxed);
    _calCapCount.store(0, std::memory_order_relaxed);
    _calDone.store(false, std::memory_order_relaxed);
    _calPhase.store(1, std::memory_order_relaxed);   // quiet first
    _calStartMs.store(millis(), std::memory_order_relaxed);
    DBG_PRINTLN("[CAL] auto-calibration started (quiet phase)");
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

void DropSampler::taskEntry(void *arg)
{
    static_cast<DropSampler *>(arg)->run();
    vTaskDelete(nullptr);       // never reached
}

void DropSampler::run()
{
    // --- One-time ADC setup, in THIS task's context only --------------------
#if DROPMETER_ADC_ENABLED
    analogReadResolution(12);                       // 0..4095
    (void)analogRead(PIN_PIEZO_SIGNAL);             // attach pin to the ADC
                                                    // oneshot bus (core 3.3.x
                                                    // requirement before
                                                    // attenuation)
    analogSetPinAttenuation(PIN_PIEZO_SIGNAL, ADC_ATTENUATION);
#endif

    // --- Amplitude-envelope state ------------------------------------------
    // Sliding AC-RMS over ENV_RMS_WINDOW samples: a faithful amplitude
    // measure for content from ~130 Hz up (unlike a first-difference
    // "sharpness" measure, which attenuates low frequencies).
    int16_t  rmsRing[ENV_RMS_WINDOW] = {0};
    uint16_t rmsPos   = 0;
    int32_t  rmsSum   = 0;
    int32_t  rmsSumSq = 0;
    uint16_t rmsFill  = 0;
    uint16_t winIdx   = 0;

    int32_t  envBase  = 0;              // EMA of the quiet envelope level
    int32_t  envNoise = 4;              // EMA of |env - envBase|
    uint16_t thr      = ENV_MIN_MARGIN;
    _threshold.store(thr, std::memory_order_relaxed);

    // Matched-filter machinery: a rolling history of the last kLen samples
    // feeds both the sliding correlation and the /learn capture.
    const uint16_t kLen = DropTemplate::kLen;
    int16_t ring[kLen];
    memset(ring, 0, sizeof(ring));
    uint16_t ringPos = 0;               // next write slot
    int16_t  winBuf[kLen];              // chronological window scratch
    int16_t  resamBuf[kLen];            // rate-normalized window scratch
    int16_t  alignedBuf[kLen];          // peak-aligned window scratch

    // /learn capture state.
    bool     recording = false;
    uint16_t recIdx    = 0;
    uint32_t recArmedMs = 0;
    int16_t  recBuf[kLen];
    uint16_t bestPeak = 0;

    uint32_t lastHitMs  = 0;            // millis() of last accepted trigger
    uint32_t capStartMs = 0;            // start of the peak-tracking window
    uint16_t capLenMs   = CAPTURE_WINDOW_MS;  // latched at trigger time
    uint16_t peak       = 0;            // running raw peak of current capture
    bool     capturing  = false;
    int64_t  trigEpochUs = 0;           // epoch at the accepted trigger
    uint16_t trigEnvPeak = 0;           // env peak across the capture
    uint16_t trigMatch   = 0;           // correlation at the trigger

    // Ring-tail suppression: after an event the detector stays LOCKED until
    // the envelope has been quiet for releaseMs (retriggerable).
    bool     armed      = true;
    uint32_t quietSince = 0;

    // "both"-mode fusion state: envelope onset arms a hold window for the
    // correlation to confirm the drop.
    bool     candArmed    = false;
    uint32_t candDeadline = 0;

    // Auto-calibration locals (state machine driven here).
    bool     calCapActive = false;
    uint32_t calCapStart  = 0;
    uint32_t calLastMs    = 0;
    uint16_t calPeakEnv   = 0;

    // Sample-rate metering (informational, updated once per second).
    uint32_t spsWindow  = 0;
    uint32_t spsStartMs = millis();
    uint32_t histCnt    = 0;

    for (;;)
    {
#if DROPMETER_ADC_ENABLED
        const uint16_t raw = (uint16_t)analogRead(PIN_PIEZO_SIGNAL);
#else
        const uint16_t raw = (uint16_t)(_raw.load(std::memory_order_relaxed) + 1);
#endif
        const uint32_t now = millis();

        _raw.store(raw, std::memory_order_relaxed);

        // Rolling WAV ring: centre on the live DC level (the piezo's bias
        // can sit anywhere from 0 to mid-scale) and scale to 16-bit audio.
        if (_wavRing)
        {
            const int32_t dc = rmsSum / (rmsFill ? rmsFill : 1);
            _wavRing[_wavPos] = (int16_t)(((int32_t)raw - dc) << 3);
            if (++_wavPos >= _wavCap)
                _wavPos = 0;
            uint32_t fill = _wavFill.load(std::memory_order_relaxed);
            if (fill < _wavCap)
                _wavFill.store(fill + 1, std::memory_order_relaxed);
        }

        // Rolling history (also consumed by the /learn capture).
        ring[ringPos] = (int16_t)raw;
        ringPos = (ringPos + 1) & (kLen - 1);

        // Sliding AC-RMS amplitude envelope bookkeeping.
        const int16_t r = (int16_t)raw;
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
        rmsPos = (rmsPos + 1) % ENV_RMS_WINDOW;

        // Track the raw peak across the capture window (per sample).
        if (capturing && raw > peak)
            peak = raw;

        // /learn + autocal capture: after the onset gate fired, fill the
        // template buffer with live samples until it is complete.
        if (recording)
        {
            recBuf[recIdx++] = (int16_t)raw;
            if (recIdx >= kLen)
            {
                // How loud was this candidate?
                uint16_t capPeak = 0;
                for (uint16_t j = 0; j < kLen; ++j)
                {
                    const int16_t v = recBuf[j];
                    const uint16_t a = v < 0 ? (uint16_t)-v : (uint16_t)v;
                    if (a > capPeak)
                        capPeak = a;
                }

                const bool learnArmed = _recordArmed.load(std::memory_order_relaxed);
                const uint8_t ph = _calPhase.load(std::memory_order_relaxed);

                if (learnArmed)
                {
                    // Manual /learn: keep ONLY the loudest candidate of the
                    // arm window — noise blips that beat us to the gate get
                    // replaced by the real drop when it arrives.
                    if (capPeak > bestPeak)
                    {
                        bestPeak = capPeak;
                        memcpy(_learnCap, recBuf, sizeof(recBuf));
                        _learnBestPeak.store(capPeak, std::memory_order_relaxed);
                        DBG_PRINTF("[TPL] candidate peak %u kept\n", capPeak);
                    }
                }
                else if (ph == 2 ||
                         (ph == 0 && _calDone.load(std::memory_order_relaxed) &&
                          _calCapCount.load(std::memory_order_relaxed) <
                              _calDropCount.load(std::memory_order_relaxed)))
                {
                    // Autocal session: learn each candidate immediately
                    // (they're gated above the measured noise).
                    _tpl.addCapture(recBuf, kLen,
                                    _sps.load() ? _sps.load() : 20000);
                    _tpl.save();

                    const uint8_t n = _calCapCount.load(std::memory_order_relaxed);
                    if (n < CAL_MAX_DROPS)
                    {
                        memcpy(_calCaps[n], recBuf, sizeof(recBuf));
                        _calCapCount.store(n + 1, std::memory_order_relaxed);
                    }
                }

                recording = false;
                if (!learnArmed)
                {
                    recArmedMs = 0;
                    _recordArmed.store(false, std::memory_order_relaxed);
                }
            }
        }

        // --- window boundary: one envelope sample ----------------------------
        if (++winIdx >= ENV_WINDOW)
        {
            winIdx = 0;

            // AC-RMS amplitude of the last ENV_RMS_WINDOW samples.
            uint16_t env = 0;
            if (rmsFill >= ENV_RMS_WINDOW)
            {
                const float mean = (float)rmsSum / ENV_RMS_WINDOW;
                const float var  = (float)rmsSumSq / ENV_RMS_WINDOW - mean * mean;
                env = (var > 0.0f) ? (uint16_t)sqrtf(var) : 0;
            }
            _envelope.store(env, std::memory_order_relaxed);

            // Decimated history for the live web chart + calibration
            // flight recorder (phase-stamped).
            if ((histCnt++ & 15) == 0)
            {
                const uint32_t hIdx =
                    _envHistCount.load(std::memory_order_relaxed) & (kEnvHist - 1);
                _envHist[hIdx] = env;
                _matchHist[hIdx] = _matchScore.load(std::memory_order_relaxed);
                _phaseHist[hIdx] = _calPhase.load(std::memory_order_relaxed);
                _envHistCount.fetch_add(1, std::memory_order_relaxed);
            }

            const uint16_t marginFloor = _margin.load(std::memory_order_relaxed);

            // --- Auto-calibration state machine (mode-independent) ---------
            const uint8_t calPh = _calPhase.load(std::memory_order_relaxed);
            if (calPh == 1)
            {
                // Quiet phase: track the loudest ambient envelope.
                uint16_t ceilNow = _calNoiseCeil.load(std::memory_order_relaxed);
                if (env > ceilNow)
                    _calNoiseCeil.store(env, std::memory_order_relaxed);
                if (now - _calStartMs.load(std::memory_order_relaxed) >= CAL_QUIET_MS)
                {
                    _calPhase.store(2, std::memory_order_relaxed);
                    _calStartMs.store(now, std::memory_order_relaxed);
                    DBG_PRINTF("[CAL] quiet done (noise ceil %u) - MAKE DROPS NOW\n",
                                  _calNoiseCeil.load(std::memory_order_relaxed));
                }
            }
            else if (calPh == 2)
            {
                const uint16_t noiseCeil = _calNoiseCeil.load(std::memory_order_relaxed);
                const uint16_t gate = noiseCeil + 20;
                if (calCapActive)
                {
                    if (env > calPeakEnv)
                        calPeakEnv = env;
                    if (now - calCapStart >= 250)
                    {
                        // Only count candidates that CLEARLY beat the
                        // ambient noise, or the auto margin lands in the
                        // noise and everything triggers.
                        if (calPeakEnv > noiseCeil * 3 / 2)
                        {
                            const uint8_t n = _calDropCount.load(std::memory_order_relaxed);
                            if (n < CAL_MAX_DROPS)
                                _calDropPeaks[n].store(calPeakEnv, std::memory_order_relaxed);
                            _calDropCount.store(n + 1, std::memory_order_relaxed);
                            DBG_PRINTF("[CAL] drop #%u peak env %u\n", n + 1, calPeakEnv);
                        }
                        else
                        {
                            DBG_PRINTF("[CAL] ignored candidate %u (noise %u)\n",
                                       calPeakEnv, noiseCeil);
                        }
                        calCapActive = false;
                    }
                }
                else if (env > gate && now - calLastMs > 500)
                {
                    calLastMs    = now;
                    calCapActive = true;
                    calCapStart  = now;
                    calPeakEnv   = env;
                    // Also capture this drop into the template (averaged).
                    if (!recording && !_recordArmed.load(std::memory_order_relaxed))
                    {
                        for (uint16_t j = 0; j < 128; ++j)
                            recBuf[j] = ring[(ringPos - 128 + j) & (kLen - 1)];
                        recIdx = 128;
                        recording = true;
                    }
                }
                if (now - _calStartMs.load(std::memory_order_relaxed) >= CAL_DROP_MS ||
                    _calDropCount.load(std::memory_order_relaxed) >= CAL_MAX_DROPS)
                {
                    _calPhase.store(0, std::memory_order_relaxed);
                    _calDone.store(true, std::memory_order_relaxed);
                    DBG_PRINTLN("[CAL] drop phase complete");
                }
            }

            // /learn arm/onset handling (mode-independent): the record gate
            // is deliberately looser than the trigger so soft drops learn.
            // Candidates are collected for the whole arm window and only
            // the LOUDEST one becomes the template (see the capture
            // completion above), so noise that beats us to the gate gets
            // replaced by the real drop when it arrives.
            if (_recordArmed.load(std::memory_order_relaxed))
            {
                if (recArmedMs == 0)
                {
                    recArmedMs = now;
                    bestPeak = 0;
                    _learnBestPeak.store(0, std::memory_order_relaxed);
                }
                if (now - recArmedMs > RECORD_ARM_TIMEOUT_MS)
                {
                    if (bestPeak >= LEARN_MIN_PEAK)
                    {
                        const bool noiseTarget =
                            _learnTarget.load(std::memory_order_relaxed) == 1;
                        DropTemplate &dst = noiseTarget ? _noiseTpl : _tpl;
                        dst.addCapture(_learnCap, kLen,
                                       _sps.load() ? _sps.load() : 20000);
                        dst.save();
                        DBG_PRINTF("[TPL] learned best %s candidate (peak %u)\n",
                                   noiseTarget ? "NOISE" : "drop", bestPeak);
                    }
                    else
                    {
                        DBG_PRINTLN("[TPL] arm window ended with no usable candidate");
                    }
                    _recordArmed.store(false, std::memory_order_relaxed);
                    recArmedMs = 0;
                }
                else if (!recording && env > LEARN_MIN_ENV)
                {
                    // Onset: seed the buffer with 128 pre-onset samples from
                    // the ring, then fill the rest live (above).
                    for (uint16_t j = 0; j < 128; ++j)
                        recBuf[j] = ring[(ringPos - 128 + j) & (kLen - 1)];
                    recIdx = 128;
                    recording = true;
                    DBG_PRINTLN("[TPL] onset captured, filling window ...");
                }
            }

            // Matched-filter check at every envelope boundary: the window
            // slides by ENV_WINDOW samples per evaluation. Computed
            // whenever the mode involves matching (1/2/3). The live window
            // is PEAK-ALIGNED to the same anchor the template was aligned
            // to, otherwise identical waveforms correlate ~zero whenever
            // the peak lands elsewhere in the window.
            const uint8_t dmode = _detectMode.load(std::memory_order_relaxed);
            bool matchedHit = false;
            if (dmode != 0 && _tpl.valid())
            {
                const uint16_t thrMilli = _matchThr.load(std::memory_order_relaxed);
                // Assemble the chronological window from the ring.
                for (uint16_t j = 0; j < kLen; ++j)
                    winBuf[j] = ring[(ringPos + j) & (kLen - 1)];

                // Locate the loudest sample — the anchor for the slice.
                int16_t  pmax = 0;
                uint16_t pidx = 0;
                for (uint16_t j = 0; j < kLen; ++j)
                {
                    const int16_t v = winBuf[j];
                    const uint16_t a = v < 0 ? (uint16_t)-v : (uint16_t)v;
                    if (a > pmax)
                    {
                        pmax = a;
                        pidx = j;
                    }
                }

                // Resample a time-slice CENTERED ON THE PEAK to the
                // template's sample rate. (Resampling the raw window's
                // oldest half missed drops half the time — the peak fell
                // outside the kept region.)
                const uint16_t tplFs = _tpl.sampleRate();
                const uint32_t curSps = _sps.load(std::memory_order_relaxed);
                const float tplDurIn =
                    (tplFs > 1000 && curSps > 1000)
                        ? (float)kLen * (float)curSps / (float)tplFs
                        : (float)kLen;
                const float startF =
                    (float)pidx - tplDurIn * ((float)DropTemplate::kAlign / (float)kLen);
                for (uint16_t j = 0; j < kLen; ++j)
                {
                    const float src = startF + tplDurIn * ((float)j / (float)kLen);
                    // EDGE REPLICATION (not zero fill): zero padding made
                    // every window correlate ~0.95 with everything.
                    if (src <= 0.0f)
                    {
                        resamBuf[j] = winBuf[0];
                        continue;
                    }
                    if (src >= (float)(kLen - 1))
                    {
                        resamBuf[j] = winBuf[kLen - 1];
                        continue;
                    }
                    const int32_t i0 = (int32_t)src;
                    const float frac = src - (float)i0;
                    resamBuf[j] = (int16_t)(
                        (float)winBuf[i0] * (1.0f - frac) +
                        (float)winBuf[i0 + 1] * frac);
                }

                // The slice is already peak-anchored at kAlign.
                memcpy(alignedBuf, resamBuf, sizeof(alignedBuf));

                // Residual scoring with noise subtraction:
                //   score = corr(drop) - W x corr(noise)
                // (measured: drops ~+290, silence ~-100 at W=0.6).
                const float mDrop = _tpl.match(alignedBuf,
                    (float)_matchMinRms.load(std::memory_order_relaxed));
                float mResidual = mDrop;
                if (_noiseTpl.valid())
                {
                    const float mNoise = _noiseTpl.match(alignedBuf, 0.0f);
                    mResidual = mDrop - MATCH_NOISE_WEIGHT * mNoise;
                }
                int32_t score = (int32_t)(mResidual * 1000.0f);
                if (score < 0)
                    score = 0;
                if (score > 999)
                    score = 999;
                _matchScore.store((uint16_t)score, std::memory_order_relaxed);
                if ((int32_t)(mResidual * 1000.0f) >= (int32_t)thrMilli)
                    matchedHit = true;
            }

            if (capturing)
            {
                // Track the envelope peak across the capture window (per
                // boundary) alongside the raw peak.
                if (env > trigEnvPeak)
                    trigEnvPeak = env;

                // Capture window complete: publish and go quiet again.
                if (now - capStartMs >= capLenMs)
                {
                    DropEvent ev;
                    ev.epochUs = trigEpochUs;
                    ev.peak    = peak;
                    ev.envPeak = trigEnvPeak;
                    ev.match   = trigMatch;
                    if (_ring.push(ev))
                    {
                        _lastEpochUs.store(trigEpochUs, std::memory_order_relaxed);
                        _lastPeak.store(peak, std::memory_order_relaxed);
                    }
                    capturing = false;
                    // Lock out the ring tail: re-arm only after a stretch
                    // of genuine quiet (retriggerable release).
                    armed      = false;
                    quietSince = 0;
                }
            }
            else
            {
                // Retriggerable release: while the tail is still noisy,
                // keep pushing the re-arm point out.
                if (!armed)
                {
                    if (env < thr)
                    {
                        if (quietSince == 0)
                            quietSince = now;
                        else if (now - quietSince >=
                                 _releaseMs.load(std::memory_order_relaxed))
                        {
                            armed = true;
                            quietSince = 0;
                        }
                    }
                    else
                    {
                        quietSince = 0;
                    }
                }

                // Fused trigger decision per mode:
                //   0 envelope only, 1 match only, 2 BOTH, 3 EITHER.
                // "both" = an envelope ONSET arms a short hold window for
                // the correlation to confirm — the two conditions peak at
                // different times, so requiring them simultaneously makes
                // detection a coin flip.
                const bool envOk   = (env > thr);
                const bool matchOk = (dmode != 0) && _tpl.valid() && matchedHit;
                bool trig = false;
                switch (dmode)
                {
                case 1:  trig = matchOk; break;
                case 2:
                    if (envOk && armed && now - lastHitMs >= _refractoryMs.load(std::memory_order_relaxed))
                    {
                        candArmed = true;
                        candDeadline = now + MATCH_HOLD_MS;
                    }
                    if (candArmed)
                    {
                        if (matchOk)
                        {
                            trig = true;
                            candArmed = false;
                        }
                        else if ((int32_t)(now - candDeadline) >= 0)
                        {
                            candArmed = false;
                        }
                    }
                    break;
                case 3:  trig = envOk || matchOk; break;
                default: trig = envOk; break;
                }
                if (trig && armed)
                {
                    if (now - lastHitMs >= _refractoryMs.load(std::memory_order_relaxed))
                    {
                        // --- new impact -------------------------------------------
                        lastHitMs    = now;
                        capStartMs   = now;
                        capLenMs     = _captureMs.load(std::memory_order_relaxed);
                        peak         = raw;
                        trigEnvPeak  = env;
                        trigMatch    = _matchScore.load(std::memory_order_relaxed);
                        capturing    = true;
                        struct timeval tv;
                        gettimeofday(&tv, nullptr);
                        trigEpochUs  = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
                        _triggers.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else
                {
                    // Adaptive law (every non-triggering sample feeds it):
                    //   threshold = baseline + margin + min(noise x mult, margin)
                    // The adaptive boost is BOUNDED by the margin itself, so
                    // it can never runaway above your setting — it just
                    // lifts the trigger while the room is noisy and lets it
                    // fall back (asymmetric, fast decay) once quiet returns.
                    const uint8_t noiseMult = _noiseMult.load(std::memory_order_relaxed);

                    const int32_t dev = (int32_t)env - envBase;
                    envNoise += (abs(dev) - envNoise) >>
                                (dev > 0 ? NOISE_ATTACK_ALPHA : NOISE_DECAY_ALPHA);
                    envBase  += dev >> BASELINE_ALPHA;
                    _envBase.store((uint16_t)(envBase > 0 ? envBase : 0),
                                   std::memory_order_relaxed);

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
        }

        // Sample-rate metering (informational only).
        if (now - spsStartMs >= 1000)
        {
            _sps.store(spsWindow, std::memory_order_relaxed);
            spsWindow  = 0;
            spsStartMs = now;
        }
        else
        {
            ++spsWindow;
        }

        // Cooperate with the UI task every N samples. IMPORTANT: this must
        // BLOCK (vTaskDelay), not taskYIELD() — a plain yield from the
        // highest-priority task just gets re-scheduled onto itself and
        // starves the UI thread forever.
        static uint32_t coop = 0;
        if (++coop >= SAMPLER_YIELD_EVERY)
        {
            coop = 0;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
