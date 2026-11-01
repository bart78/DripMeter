# DripMeter

An acoustic **ceiling-leak drop monitor** for the Guition JC3248W535 (ESP32-S3 + 3.5" 480×320-capable AXS15231B QSPI display, here used in 320×480 portrait).

A piezo disc, taped to the plastic tub under the leak, hears each droplet's "clack". The device detects, timestamps, logs, charts, and serves the data over Wi-Fi — so you can correlate drips with showers, washing machines, heating cycles, or confirm a leak has stopped.

This README is also the story of *how* the detection was made to work: nearly every subsystem was debugged against real recorded data with an offline simulator before being trusted (see [Method](#method--the-journey)).

---

## Hardware

| Part | Detail |
|---|---|
| Board | Guition JC3248W535 (ESP32-S3-WROOM-1) — **8 MB flash** on this unit, despite 16 MB in the spec sheet (the bootloader reads the true chip size; a 16 MB partition scheme crash-loops at boot) |
| Display | AXS15231B, **QSPI** (CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39), backlight GPIO 1 — no LovyanGFX/TFT_eSPI driver exists for this controller; Arduino_GFX's `Arduino_AXS15231B` is the proven path |
| Sensor | Piezo ceramic disc, port P2: **red → GPIO 6** (ADC1_CH5), **yellow → GPIO 7** driven LOW as a "virtual ground", black floating. 12-bit, 11 dB attenuation (~0.76 mV/count) |
| USB | Native USB only (USB-Serial-JTAG) — flash with `upload_protocol = esp-builtin` |

**Mounting matters.** The piezo is a contact microphone: rigid coupling on the drop-impact zone (stiff tape/clamp/epoxy) gives the best signal-to-noise. Soft tape dulls transients.

**Measured physical limits** (from labeled recordings, in raw ADC units): ambient noise floor ≈ 0–2, human-audible drops ≈ 6+, typical drops 10–50. Anything below ~4–5 raw is indistinguishable from the room *by ear as well as by firmware* — the only fix there is mechanical or electronic gain.

## Software

```
src/
  config.h          every pin, threshold, and timing — the "manual"
  drop_event.h      event struct + lock-free SPSC ring buffer
  drop_sampler.*    FreeRTOS sampler (core 1, priority 20): AC-RMS envelope,
                    adaptive threshold, release/refractory timing, template
                    matching with noise subtraction, WAV recorder ring
  drop_template.*   learned waveform templates (drop + noise), peak-aligned,
                    RMS-normalized, rate-normalized correlation
  histogram.*       24 h buckets, today + yesterday (midnight rollover)
  drop_log.*         append-only LittleFS CSV + config-versioned markers
  time_service.*     SNTP (KST), localtime helpers
  dashboard.*        320×480 display: clock/date, WIFI/IP, histogram w/
                    yesterday ghost, live telemetry footer
  web_export.*       HTTP on port 80 (own task, core 0): status page w/ SVG
                    chart, calibration bench w/ live chart, CSV/JSON/WAV
  tuning.*           NVS-persisted calibration knobs
  main.cpp           wiring + WiFi-via-events (the loop never calls WiFi APIs)
tools/               offline analysis scripts (see below)
data/                your labeled recordings + logs
```

### Detection pipeline

```
raw ADC (16.7 kS/s, envelope-only mode; ~8.5 kS/s with matching on)
  → sliding AC-RMS envelope (128-sample window, 16-sample steps)
  → adaptive trigger: threshold = baseline + margin + min(noise×mult, margin)
  → trigger → 80 ms peak capture → 200 ms refractory → retriggerable
    400 ms quiet-release (one tap = one event, ring tails swallowed)
  → in "both" mode the envelope onset arms a 250 ms window for the
    CORRELATION gate to confirm
  → correlation: live window is peak-centered, resampled to the template's
    sample rate, then compared to the learned drop template MINUS
    0.6 × the learned noise template (adaptive noise cancellation)
  → event {epoch µs, raw peak, env peak, match} → SPSC ring → CSV
```

### Data & web

- **`/`** — status page: live stats + 24 h histogram chart (today solid, yesterday dimmed)
- **`/cal`** — calibration bench: live waveform chart (drag the red threshold line!), auto-applying knobs, one-click auto-calibration, template/noise recording
- **`/drops.csv`** — `ISO_Timestamp,Epoch,PeakADC,EnvPeak,Match`, plus `#cfg,vN,...` lines that record **which settings produced the surrounding data** (events are matched to their config by timestamp)
- **`/status.json`** — machine-readable snapshot incl. both histogram arrays
- **`/record`** + **`/rec.wav`** — rolling 60 s audio recorder for manual labeling
- Hotspot fallback: the device always serves `DripMeter` AP → `http://192.168.4.1`

### Build

Requires **pioarduino** (Arduino core 3.3.x on IDF 5.5 — the official core 3.0.x boot-loops on this board) and PlatformIO Core ≥ 6.2:

```bash
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/pioarduino/pioarduino-core-installer/pioarduino/get-platformio.py
python3 get-platformio.py
~/.platformio/penv/bin/pio run --upload-port /dev/cu.usbmodemXXXX -t upload
```

## Method — the journey

The interesting part, honestly. What looks like "detect loud noises" turned into a serious signal-isolation problem, and the winning move was to **stop iterating on hardware and build a faithful offline simulator** (`tools/`) that runs the exact firmware pipeline against recorded+labeled sessions. Highlights:

1. **8 MB flash, not 16** — the bootloader *told* us (`flash chip size 0x800000`) after a day of silent boot loops that turned out to be partition-table rejection.
2. **A task at priority 24 that `taskYIELD()`s never yields to anything** — `yield` re-schedules *itself* when it's the highest-priority ready task. The UI froze completely. The sampler now bursts + `vTaskDelay`s.
3. **Blocking WiFi calls in the UI loop** (`status`/`RSSI`/SNTP with the TCPIP lock) deadlocked the display whenever the radio was flaky → all WiFi state now arrives via events into atomics; loop lives on core 0, sampler owns core 1.
4. **The detector filter was wrong for the signal**: a first-difference "sharpness" envelope attenuates low frequencies ~20× — a tub "thock" looked like noise. Replaced with a true AC-RMS amplitude envelope.
5. **The sample rate changes with CPU load** (match mode costs half the rate) — a template recorded at 16 kS/s correlated at ~0.2 against live windows at 8.5 kS/s until matching became **rate-normalized** (resample the live slice to the template's rate).
6. **Normalized correlation is meaningless on near-silent windows** (0/0) — everything scored 0.95. An energy gate fixed it… and zero-padding the peak-centered slice re-created the same fake-0.95 later ("953 everywhere"). Edge-replication fixed that one.
7. **"Both" mode can't mean "simultaneously"**: the loudness gate peaks at the onset, the correlation peaks 30–60 ms later — requiring both at once made detection a coin flip. The onset now *arms* a hold window for the correlation.
8. **The noise-subtraction filter was your idea, and the data proved it**: silence events correlate 0.03 with the drop template while drops hit 0.28 — subtracting 0.6 × the noise-template correlation pushes silence to −0.10 and leaves drops at +0.29. Clean separation at a threshold of ~0.15.
9. **The ground-truth discipline**: you labeled recordings in Audacity's spectral view; the simulator measured the human-audibility floor (6.4 raw units) and the FP-rate curve; every default in `config.h` is derived from those numbers.

## Future work

The machinery is deliberately generic — a learnable, rate-normalized, noise-cancelling **transient classifier** with versioned data logging. Anything that makes a repeatable noise is fair game:

- **Machinery health**: pumps, HVAC blowers, washing machines — learn "normal" and alarm on deviation; bearing wear shows up as a changing signature long before failure
- **Home event correlation**: the original motivation — map drips/hums/clunks onto showers, dishwashers, heating
- **Counting things**: drip coffee machines, animal water feeders, printer pages, door cycles
- **Watchdog mode**: "this machine should knock every 30 s — alert me when it stops"

Nearest-term code improvements:
- multi-template **classification** (the second template slot already generalizes; add labeled classes + argmax)
- **spectral features** (the first analysis showed drops dominate 200–400 Hz with 43–54 dB gaps — a band-limited envelope would add that rejection for free)
- on-device **clustering** of unknown transients into "new signatures detected"
- OTA updates + HTTPS, battery profile, and a proper web dashboard with per-event audio playback

## License

Use it, break it, fix it — consider it MIT. If it catches your leak, tell me the story.
