// ============================================================================
//  DripMeter — central configuration
//  Board : Guition JC3248W535 (ESP32-S3, 3.5" 320x480 AXS15231B, QSPI)
//  Edit the WiFi / timezone / threshold values below, then flash.
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
// 0. Debug console
// ---------------------------------------------------------------------------
// Runtime Serial logging is OFF by default: on this board the console is
// the USB-Serial-JTAG port and writes can block when no host is reading,
// which would stall the sampler/UI/web. Only setup-time prints remain.
#define DRIPMETER_SERIAL_LOG     0

#if DRIPMETER_SERIAL_LOG
#define DBG_PRINTF   Serial.printf
#define DBG_PRINTLN  Serial.println
#else
#define DBG_PRINTF(...)  do { } while (0)
#define DBG_PRINTLN(...) do { } while (0)
#endif

// ---------------------------------------------------------------------------
// 1. Piezo sensor wiring (board expansion port P2, 8-pin JST SH)
// ---------------------------------------------------------------------------
//   Signal wire ("S")  -> GPIO 6 (ADC1_CH5, 12-bit, 11 dB attenuation)
//   Power / Ground     -> Board 3.3V (+) and GND (-) header pins
//
//  With the powered piezo/amp setup, idle noise floor sits at 0-2 raw ADC
//  counts while real water drops produce peak swings of 90-220 raw counts
//  and AC-RMS envelopes of 20-46 counts.
#define PIN_PIEZO_SIGNAL       6        // ADC1_CH5

// ---------------------------------------------------------------------------
// 2. Display bus (fixed by the JC3248W535 PCB — do not change)
// ---------------------------------------------------------------------------
#define PIN_LCD_BL             1        // backlight enable / PWM
#define PIN_LCD_CS             45
#define PIN_LCD_SCK            47
#define PIN_LCD_D0             21
#define PIN_LCD_D1             48
#define PIN_LCD_D2             40
#define PIN_LCD_D3             39
#define LCD_WIDTH              320
#define LCD_HEIGHT             480
#define LCD_ROTATION           0        // native portrait orientation
#define BL_BRIGHTNESS          230      // 0..255 LEDC duty

// ---------------------------------------------------------------------------
// 3. Wi-Fi — REPLACE WITH YOUR CREDENTIALS
// ---------------------------------------------------------------------------
// Credentials live in src/secret_config.h (gitignored) — see the .example.
#if __has_include("secret_config.h")
#include "secret_config.h"
#else
#define WIFI_SSID              "YOUR_SSID"
#define WIFI_PASSWORD          "YOUR_PASSWORD"
#endif
#define WIFI_RECONNECT_MS      15000UL  // retry interval while disconnected

// ---------------------------------------------------------------------------
// 4. Time (SNTP) and timezone
// ---------------------------------------------------------------------------
#define NTP_SERVER_1           "pool.ntp.org"
#define NTP_SERVER_2           "time.nist.gov"
#define TIME_VALID_EPOCH       1600000000L         // epochs > this = "synced"
// POSIX TZ string (sign is INVERTED: "CET-1" == UTC+1):
//   UTC          : "UTC0"
//   Korea (KST)  : "KST-9"
//   Europe/Berlin: "CET-1CEST,M3.5.0,M10.5.0/3"
//   New York     : "EST5EDT,M3.2.0,M11.1.0"
#define TZ_STRING              "KST-9"

// ---------------------------------------------------------------------------
// 5. Rolling WAV recorder (for diagnostic listening + offline verification)
//    Keeps the last WAV_SECONDS of audio in PSRAM; /record saves it to
//    flash as /rec.wav (single file, overwritten every time).
// ---------------------------------------------------------------------------
#define WAV_SECONDS             60        // ring length (~2.2 MB PSRAM)

// ---------------------------------------------------------------------------
// 6. Drop detection — Adaptive AC-RMS Amplitude Envelope (see drop_sampler.cpp)
//
//    Pipeline:
//      1. 150 Hz one-pole high-pass filter rejects low-frequency building/pipe
//         rumble while passing 200-400 Hz drop impacts untouched.
//      2. Sliding AC-RMS over ENV_RMS_WINDOW (128 samples, ~7 ms) updated
//         every ENV_WINDOW (16 samples, ~0.85 ms).
//      3. Adaptive threshold:
//           threshold = envBase + margin + min(envNoise * noiseMult, margin)
//      4. Trigger -> track peak across CAPTURE_WINDOW_MS -> hold off during
//         RELEASE_MS of continuous quiet (swallowing tub ring-down) and enforce
//         REFRACTORY_MS minimum onset-to-onset spacing.
// ---------------------------------------------------------------------------
#define ADC_ATTENUATION         ADC_11db   // 11dB: ~0-3100mV span, 0.76mV/ct

#define ENV_WINDOW              16         // envelope update every N samples
#define ENV_RMS_WINDOW          128        // AC-RMS window length (~7 ms)
#define ENV_HIGHPASS_HZ         150        // one-pole high-pass cutoff (Hz)

#define ENV_MIN_MARGIN          15         // default trigger margin above baseline
                                           // (powered piezo: noise blips <= 8,
                                           // real drops 20-46 -> 15 sits cleanly
                                           // in the dead-zone)
#define TRIGGER_NOISE_MULT      3          // adaptive noise boost multiplier
                                           // (capped at 1x margin)
#define NOISE_ATTACK_ALPHA      8          // noise EMA: slow rise (1/256)
#define NOISE_DECAY_ALPHA       6          // noise EMA: fast fall (1/64)
#define BASELINE_ALPHA          6          // env baseline EMA factor (1/64)
#define THR_MAX                 3900       // clamp: keep clear of rail clip

#define CAPTURE_WINDOW_MS       60         // peak-tracking window after trigger (ms)
#define RELEASE_MS              120        // quiet time below threshold required to
                                           // re-arm after capture (swallows ring-tail)
#define REFRACTORY_MS           200        // minimum onset-to-onset spacing (ms);
                                           // 200 ms supports up to 5 drops/sec

// ---------------------------------------------------------------------------
// 7. Sampler task (FreeRTOS)
// ---------------------------------------------------------------------------
#define DROPMETER_ADC_ENABLED   1
#define SAMPLER_CORE            1          // dedicated core (UI/WiFi/Web on core 0)
#define SAMPLER_PRIORITY        20         // high priority on isolated core 1
#define SAMPLER_STACK_BYTES     8192       // task stack size
#define RING_CAPACITY           64         // SPSC event ring, power of two

// ---------------------------------------------------------------------------
// 8. LittleFS persistence & Water Quantity Calibration
//    (partitions_8MB.csv gives the LittleFS partition ~5.7 MB; the log is
//    rate-limited by compaction well before that)
// ---------------------------------------------------------------------------
#define LOG_PATH                "/drops.csv"
#define LOG_HEADER              "ISO_Timestamp,Epoch,PeakADC,EnvPeak,Volume_uL"
#define LOG_MAX_BYTES           (3UL * 1024UL * 1024UL)  // ~90 days @ 1 drop/min
#define LOG_MIN_FREE_BYTES      (512UL * 1024UL)         // compact when tight
#define LOG_KEEP_FRACTION       0.66f                    // keep newest 2/3
#define LOG_COMPACT_COOLDOWN_MS 3600000UL                // at most once/hour

#define DEFAULT_QTY_ENABLED     0          // 1 = measure & display water volume (mL)
#define DEFAULT_UL_TENTHS       500        // 50.0 uL (0.050 mL) default per drop
#define DEFAULT_CAL_ENV_TENTHS  315        // 31.5 default mean EnvPeak reference
#define CAL_TARGET_DROPS        45         // ~3 bursts (~2.3 g) for high scale confidence

// ---------------------------------------------------------------------------
// 9. Histogram
// ---------------------------------------------------------------------------
#define BUCKET_MINUTES          15         // 96 buckets cover a full day
#define NUM_BUCKETS             (1440 / BUCKET_MINUTES)

// ---------------------------------------------------------------------------
// 10. UI cadence
// ---------------------------------------------------------------------------
#define UI_REFRESH_MS           1000       // full redraw period
#define UI_EVENT_REDRAW_MIN_MS  150        // min. spacing of event-triggered
                                           // redraws (debounce)
