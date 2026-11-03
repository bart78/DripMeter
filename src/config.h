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
//   Yellow wire (sensor "-")   -> GPIO 7   driven LOW as a "virtual ground"
//   Red wire    (signal "S")   -> GPIO 6   ADC1_CH5, 12-bit, 11 dB attenuation
//   Black wire  (sensor "+")   -> floating (not connected)
//
//  NOTE (production hardening): a piezo disc can produce spikes beyond 3V3
//  and below 0V, which the ESP32-S3 pads clamp through their ESD diodes.
//  For long-term deployments, add a 10k series resistor in the red wire,
//  a BAT54S dual Schottky clamp (GPIO6 to GND and to 3V3) and a 1M bleed
//  resistor across the piezo. The firmware below works as-is either way.
#define PIN_PIEZO_SIGNAL       6        // ADC1_CH5
#define PIN_PIEZO_GND          7        // configured OUTPUT LOW in setup()

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
// 5b. Rolling WAV recorder (for listening + manual labeling)
//     Keeps the last WAV_SECONDS of audio in PSRAM; /record saves it to
//     flash as /rec.wav (single file, overwritten every time).
// ---------------------------------------------------------------------------
#define WAV_SECONDS             60        // ring length (~2.2 MB PSRAM)

// ---------------------------------------------------------------------------
// 5. Drop detection — ADAPTIVE amplitude envelope (see drop_sampler.cpp)
//
//    A real drop impact is IMPULSE-LIKE: broadband, "clap"-shaped, a few ms.
//    The detector now works on the AC-RMS amplitude envelope over a sliding
//    ~7.7 ms window — a faithful amplitude measure for low-frequency
//    "thock" content down to ~130 Hz, unlike a first-difference sharpness
//    measure which attenuates low frequencies.
//
//        threshold = envBaseline + max(ENV_MIN_MARGIN, TRIGGER_NOISE_MULT x envNoise)
//
//    Tuning: watch RAW / ENV / THR in the footer. Idle ENV = noise floor;
//    typing / claps / real drops each push ENV to some level. Set
//    ENV_MIN_MARGIN (or the noise multiplier) so THR sits between the
//    largest nuisance (keyboard) and the smallest real drop — the adaptive
//    part then tracks slow environmental drift for you.
//
//    Practical floor (hardware limits): at ADC_11db one count = 0.76 mV.
//    If real drops do not lift ENV above the nuisance noise level, the
//    piezo needs more MECHANICAL gain (rigid mounting near the impact
//    point) or an ELECTRONIC gain/bias stage — firmware cannot separate
//    overlapping signatures.
// ---------------------------------------------------------------------------
#define ADC_ATTENUATION         ADC_11db   // 11dB: ~0-3100mV span, 0.76mV/ct

#define ENV_WINDOW              16         // envelope update every N samples
#define ENV_RMS_WINDOW          128        // AC-RMS window length (~7.7 ms at
                                           // 16.7 kS/s: amplitude-faithful for
                                           // content down to ~130 Hz, unlike
                                           // a first-difference sharpness
                                           // measure which attenuates low
                                           // frequencies)
#define ENV_HIGHPASS_HZ         150        // one-pole high-pass on the raw
                                           // stream before the RMS: kills the
                                           // 30-150 Hz structure-borne rumble
                                           // floor (measured from the silent
                                           // sessions) while drops (200-400
                                           // Hz) pass untouched
#define ENV_MIN_MARGIN          5          // floor margin above envelope
                                           // baseline. Measured from the
                                           // labelled sessions: the human-
                                           // audibility floor is ~6.4 raw,
                                           // local noise max ~4.7.
#define TRIGGER_NOISE_MULT      4          // adaptive boost factor; the boost
                                           // is capped at the margin itself
#define NOISE_ATTACK_ALPHA      8          // noise EMA: slow rise (1/256)
#define NOISE_DECAY_ALPHA       6          // noise EMA: fast fall (1/64) so
                                           // drop ring-tails don't inflate
                                           // the threshold for long
#define BASELINE_ALPHA          6          // env baseline EMA factor (1/64)
#define THR_MAX                 3900       // clamp: keep clear of rail clip

#define REFRACTORY_MS          200      // minimum gap after an impact before
                                        // a new one can register (runtime-
                                        // tunable; 1000/refract per s max)
#define RELEASE_MS             400      // ring-tail suppression: after an
                                        // event, the detector stays locked
                                        // until the signal has been QUIET
                                        // for this long (retriggerable —
                                        // each noisy sample resets it).
                                        // Raise if one tap = many drips.
#define CAPTURE_WINDOW_MS      120      // raw-peak-track window after trigger.
                                        // Drops closer than this merge into
                                        // one event; runtime-tunable. A sharp
                                        // clap's peak is caught in the first
                                        // ~20 ms, so this can go much lower
                                        // for high-rate work. Hardware-side
                                        // resolution is ~0.5 ms (one envelope
                                        // window); sustained > ~10/s would
                                        // want batched flash writes.

// Auto-calibration session timings (see /autocal).
#define CAL_QUIET_MS           10000    // listen to the environment first
#define CAL_DROP_MS            20000    // then collect drops for this long
#define CAL_MAX_DROPS          4        // (or until this many drops)

// Matched-filter mode (learned drop template; see drop_template.h).
// Detection is mode-selectable at runtime: 0 = adaptive envelope (default),
// 1 = template correlation — the optimal detector for a known signature.
#define MATCH_THR_DEFAULT       75      // residual correlation threshold
                                        // (milli). With noise subtraction,
                                        // drops sit ~+290, silence ~-100
                                        // (measured on the labelled data).
#define MATCH_NOISE_WEIGHT      0.6f    // score = corr(drop) - W x corr(noise)
                                        // derived offline from the labelled
                                        // session (drops 802+, noise <640)
#define MATCH_MIN_RMS           3       // energy gate (raw units): windows
                                        // quieter than this never match —
                                        // normalized correlation is unstable
                                        // on near-silence (0/0), which is
                                        // why match mode fired on noise
#define MATCH_HOLD_MS           250     // "both" mode: an envelope onset
                                        // arms this long a window for the
                                        // correlation to confirm the drop
                                        // (envelope peaks at the onset,
                                        // correlation peaks ~30-60 ms
                                        // later — they never coincide)
#define MATCH_EVAL_EVERY        16      // correlation every N samples
#define RECORD_ARM_TIMEOUT_MS   30000   // /learn arms for this long (several
                                        // straw drops fit; only the LOUDEST
                                        // candidate of the window is kept,
                                        // and each press AVERAGES into the
                                        // template — repeat as desired)
#define LEARN_MIN_ENV           2       // capture gate for /learn: anything
                                        // above this envelope level is a
                                        // candidate (soft drops included —
                                        // the loudest still wins). Learning
                                        // is expected to happen in a QUIET
                                        // room, so the gate can sit barely
                                        // above silence.
#define LEARN_MIN_PEAK          6       // a kept candidate must at least
                                        // peak this high (raw counts)

// ---------------------------------------------------------------------------
// 6. Sampler task (FreeRTOS)
// ---------------------------------------------------------------------------
#define DROPMETER_ADC_ENABLED   1
#define SAMPLER_CORE           1        // ADC task core (WiFi lives on core 0)
#define SAMPLER_PRIORITY       20       // well above the UI (1) but below the
                                        // timer/IPC tasks (22+) — the top two
                                        // priority levels are too risky
#define SAMPLER_STACK_BYTES    10240    // run() carries several 512-sample
                                        // int16 buffers + a resample buffer
                                        // on the stack
#define SAMPLER_YIELD_EVERY    256      // yield to the UI task every N samples
#define RING_CAPACITY          32       // SPSC ring, power of two

// ---------------------------------------------------------------------------
// 7. LittleFS persistence
//    (partitions_8MB.csv gives the LittleFS partition ~5.7 MB; the log is
//    rate-limited by compaction well before that)
// ---------------------------------------------------------------------------
#define LOG_PATH               "/drops.csv"
#define LOG_HEADER             "ISO_Timestamp,Epoch,PeakADC,EnvPeak,Match"
#define LOG_MAX_BYTES          (3UL * 1024UL * 1024UL)  // ~90 days @ 1 drop/min
#define LOG_MIN_FREE_BYTES     (512UL * 1024UL)         // compact when tight
#define LOG_KEEP_FRACTION      0.66f                    // keep newest 2/3
#define LOG_COMPACT_COOLDOWN_MS 3600000UL               // at most once/hour

// ---------------------------------------------------------------------------
// 8. Histogram
// ---------------------------------------------------------------------------
#define BUCKET_MINUTES         15       // 96 buckets cover a full day
#define NUM_BUCKETS            (1440 / BUCKET_MINUTES)

// ---------------------------------------------------------------------------
// 9. UI cadence
// ---------------------------------------------------------------------------
#define UI_REFRESH_MS          1000     // full redraw period
#define UI_EVENT_REDRAW_MIN_MS 150      // min. spacing of event-triggered
                                        // redraws (debounce)
