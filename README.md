# DripMeter

An acoustic **ceiling-leak drop monitor** for the Guition JC3248W535 (ESP32-S3 + 3.5" 480×320-capable AXS15231B QSPI display, used in 320×480 portrait).

A powered piezo disc + amplifier coupled to the catchment tub hears each droplet's impact. The device detects, timestamps, logs, charts, and serves the data over Wi-Fi — so you can track leak patterns, correlate drips with appliances or heating/condensate cycles, and confirm when a leak has stopped.

---

## Hardware

| Part | Detail |
|---|---|
| Board | Guition JC3248W535 (ESP32-S3-WROOM-1) — **8 MB Flash** (`partitions_8MB.csv`, ~6.16 MB LittleFS) + **8 MB QSPI PSRAM** |
| Display | AXS15231B, **QSPI** (CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39), backlight GPIO 1 — driven via Arduino_GFX's `Arduino_AXS15231B` with a PSRAM-backed double-buffered canvas |
| Sensor | Powered piezo ceramic disc + amp on expansion port P2: **signal → GPIO 6** (`ADC1_CH5`, 12-bit, 11 dB attenuation ~0.76 mV/count), **power/ground → board 3.3V (`+`) and GND (`-`) headers** |
| USB | Native USB (USB-Serial-JTAG) — flash with `upload_protocol = esp-builtin` |

**Signal-to-Noise with Powered Piezo:** Idle baseline sits at `0–1` raw counts (`env = 0–1`), while real water drops produce `PeakADC` swings of `100–220` and `EnvPeak` values of `20–46`.

---

## Software Architecture

```
src/
  config.h          central pinout, thresholds, and timing defaults
  drop_event.h      event struct + lock-free SPSC ring buffer (64 slots)
  drop_sampler.*    FreeRTOS sampler (dedicated Core 1, priority 20):
                    150 Hz high-pass, 128-sample AC-RMS envelope, adaptive
                    threshold, peak capture window, quiet-release & refractory
                    holdoff, and rolling 60 s PSRAM WAV recorder
  histogram.*       24 h buckets (15-min intervals), today + yesterday
  drop_log.*        append-only LittleFS CSV + versioned #cfg markers + compaction
  time_service.*    SNTP (KST), localtime & ISO-8601 helpers
  dashboard.*       320×480 on-device display: clock/date, WIFI/IP, 24 h
                    histogram w/ yesterday ghost, live telemetry footer
  web_export.*      unified single-page HTTP dashboard on port 80 (Core 0):
                    KPI cards, live-updating 24 h SVG histogram, live signal
                    scope with visual knob overlays, collapsible tuning drawer,
                    and CSV/JSON/WAV endpoints
  tuning.*          NVS-persisted calibration parameters (namespace "drip2")
  main.cpp          setup/loop wiring + event-driven Wi-Fi state
tools/              offline analysis & simulation scripts
data/               historical recorded sessions + logs
```

### Detection Pipeline (Core 1)

```
raw ADC (~9 kS/s continuous on isolated Core 1)
  → 150 Hz one-pole high-pass filter (rejects structure-borne rumble & DC offset)
  → sliding AC-RMS amplitude envelope (128-sample window, 16-sample steps)
  → 500 ms boot baseline warm-up + adaptive baseline / noise EMAs
  → adaptive trigger: threshold = envBase + margin + min(envNoise × noiseMult, margin)
  → trigger → 60 ms peak capture (seeded with triggering 16-sample window max)
            → 120 ms retriggerable quiet-release (swallows tub ring-tails & splashes)
            → 200 ms minimum onset-to-onset refractory spacing (up to 5 drops/s)
  → DropEvent {epochUs, peak, envPeak} → lock-free SPSC ring → LittleFS CSV
```

### Detection Tuning Parameters

| Parameter | Query Key | Default | Range | Purpose |
|---|---|---|---|---|
| **Trigger Margin** | `margin` | `15` raw | `5–500` | Minimum height above `envBase` required to trigger a drop. |
| **Ambient Noise Guard** | `noisemult` | `3`× | `0–20` | Lifts threshold by `envNoise × noisemult` during room noise (capped at `+1× margin`). |
| **Peak Capture Window** | `captwin` | `60` ms | `20–300` | Window after trigger onset during which the highest `PeakADC` and `EnvPeak` are tracked. |
| **Ring-Tail Release** | `release` | `120` ms | `40–1000` | Retriggerable quiet holdoff: envelope must stay below threshold this long to re-arm. |
| **Min Drop Spacing** | `refract` | `200` ms | `80–2000` | Hard minimum onset-to-onset time between two drops. |

---

## Web Interface & Endpoints

- **`/`** — Unified single-page dashboard (`/cal` redirects to `/#tuning`):
  - **KPI Cards:** Drops Today (with Yesterday comparison and total triggers), Last Drop relative & ISO timestamp, Last Impact Peak (`PeakADC` & `EnvPeak`), and Live Envelope / Threshold.
  - **24-Hour Drop Distribution:** Live-updating 96-bucket (15-min) SVG histogram comparing Today (cyan/blue) against Yesterday (slate).
  - **Live Signal Scope (5.2 s window):** Streams 512 peak-held envelope points and detector state bands (`ARMED`, green `CAPTURING`, amber `RELEASE`). Drag the red threshold line directly on the scope to adjust `margin`, or hover/focus any tuning card to see an interactive visual diagram on the scope.
  - **Collapsible Detection Tuning Drawer:** Show or hide the 5 tuning controls (`Show Knobs ▾` / `Hide Knobs ▴`); changes apply automatically and persist to NVS.
- **`/drops.csv`** — `ISO_Timestamp,Epoch,PeakADC,EnvPeak`, plus `#cfg,vN,...` lines recording the active settings whenever parameters change.
- **`/status.json`** — Machine-readable snapshot including live telemetry, system health, and both 24 h histogram arrays.
- **`/stream.json`** — Compact 512-point peak-held envelope and detector state trace (`env`, `st`, `raw`, `thr`, `base`, `noise`, `sps`).
- **`/record`** + **`/rec.wav`** — Saves the rolling 60 s PSRAM audio ring buffer to `/rec.wav` and downloads it as a 16-bit mono PCM WAV file.
- **`/bootlog.csv`** — Reset-reason diary written once per boot (`poweron`, `sw`, `panic`, `brownout`, `wdt`, `usb`).
- **`/clearlog`** — Wipes `/drops.csv`, stamps `#cfg,v1` with current settings, and resets the on-screen histogram.
- **Hotspot fallback** — The device always runs a fallback `DripMeter` AP at `http://192.168.4.1`.

### Storage & Flash Wear Protection

- **`/drops.csv` Rotation:** Capped at `1.2 MB` (`~25,000` drops) on the `6.16 MB` LittleFS partition. When reached, [`DropLog::compact()`](src/drop_log.cpp) atomically trims the oldest 25% of rows and retains the newest 75% (`~18,750` drops) indefinitely.
- **PSRAM Audio Ring:** Continuous 60 s audio buffering runs entirely in PSRAM (`~2.28 MB`) with zero Flash writes until `/record` is explicitly clicked (which overwrites a single `/rec.wav` file).
- **`/bootlog.csv` Cap:** Automatically rotates at `4 KB`.

---

## Build & Flash

Requires **pioarduino** (Arduino core 3.3.x on ESP-IDF 5.5) and PlatformIO Core:

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
```

## License

MIT.
