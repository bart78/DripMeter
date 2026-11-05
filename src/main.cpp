// ============================================================================
//  DripMeter — acoustic ceiling-leak drop monitor
//  Board: Guition JC3248W535 (ESP32-S3, 320x480 AXS15231B QSPI display)
//
//  Task layout:
//    * drop_sampler task (core 1, prio 24) — tight ADC polling, never
//      blocked by rendering; pushes DropEvents into an SPSC ring.
//    * loopTask (core 1, prio 1) — drains the ring, appends to the CSV
//      log, maintains Wi-Fi/NTP, and renders the dashboard on a 1 s
//      cadence. No delay() anywhere.
//
//  Data flow:   piezo -> ADC1_CH5 -> ring buffer -> LittleFS CSV + histogram
//                                                                   -> canvas
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "esp_task_wdt.h"
#include <atomic>

#include "config.h"
#include "drop_sampler.h"
#include "time_service.h"
#include "drop_log.h"
#include "histogram.h"
#include "dashboard.h"
#include "tuning.h"
#include "web_export.h"

// ---------------------------------------------------------------------------
// Singletons
// ---------------------------------------------------------------------------
static DropSampler gSampler;
static DropLog     gLog;
static Histogram   gHisto;
static Dashboard   gUi;
static WebExport   gWeb(gSampler, gLog, gHisto);

// ---------------------------------------------------------------------------
// Wi-Fi state — event-driven ONLY. The loop task must never call into the
// WiFi stack: blocking calls there (RSSI/status on a flaky radio, or the
// TCPIP lock around SNTP) deadlock the UI loop. Events update these
// atomics from the WiFi task's own context instead.
// ---------------------------------------------------------------------------
static std::atomic<bool>     gWifiUp{false};
static std::atomic<int32_t>  gWifiRssi{-120};
static char                  gWifiIp[16] = {0};   // written by event cb only

static void onWifiEvent(WiFiEvent_t ev)
{
    if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    {
        gWifiUp = true;
        gWifiRssi = WiFi.RSSI();
        strlcpy(gWifiIp, WiFi.localIP().toString().c_str(), sizeof(gWifiIp));
        TimeService::forceResync();     // TCPIP lock: safe in event context
        Serial.printf("[WIFI] connected, IP %s, RSSI %d\n", gWifiIp, (int)gWifiRssi);
    }
    else if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    {
        gWifiUp = false;
        gWifiRssi = -120;
        gWifiIp[0] = 0;
        Serial.println("[WIFI] disconnected (auto-reconnect armed)");
    }
}

// Loop liveness heartbeat for the status page (diagnostic).
std::atomic<uint32_t> gLoopMs{0};

// Last completed setup stage — rendered into the dashboard footer as
// "SYS: <stage>" so a stuck boot names its failing stage on screen.
static char gStage[24] = "boot";
static bool gFirstTimeSync = false;

static void setStage(const char *s);

// ---------------------------------------------------------------------------
// Assemble the immutable snapshot handed to the renderer.
// ---------------------------------------------------------------------------
static DashSnapshot buildSnapshot()
{
    DashSnapshot s;

    s.timeValid = TimeService::localTime(s.now);
    s.wifiUp    = gWifiUp.load();
    s.wifiRssi  = (int8_t)gWifiRssi.load();
    s.ntpOk     = TimeService::synced();
    s.fsOk      = gLog.fsOk();
    s.fsFreeBytes = gLog.totalBytes() - gLog.usedBytes();
    s.fsTotalBytes = gLog.totalBytes();
    s.logOk     = gLog.ok();

    s.dropsToday = gHisto.anchored() ? gHisto.total() : 0;
    s.histo      = &gHisto;

    s.haveLastDrop    = (gSampler.lastDropEpochUs() != 0);
    s.lastDropEpochUs = gSampler.lastDropEpochUs();
    s.lastPeak        = gSampler.lastPeak();

    s.liveRaw    = gSampler.rawADC();
    s.envelope   = gSampler.envelope();
    s.matchScore = gSampler.matchScore();
    s.threshold  = gSampler.threshold();
    s.totalTriggers = gSampler.triggerCount();
    s.sampleRate = gSampler.samplesPerSecond();
    s.detectMode = gSampler.detectMode();
    s.stage      = gStage;
    s.ip[0] = 0;
    strlcpy(s.ip, gWifiIp, sizeof(s.ip));

    return s;
}

// ---------------------------------------------------------------------------
// Boot-stage helper: every completed setup step is stamped into the footer
// ("SYS: ..."), so a stuck boot names its failing stage on screen.
// ---------------------------------------------------------------------------
static void setStage(const char *s)
{
    snprintf(gStage, sizeof(gStage), "%s", s);
    Serial.printf("[BOOT] %s\n", s);
    gUi.render(buildSnapshot());   // visible immediately, even mid-setup
}

// ---------------------------------------------------------------------------
void setup()
{
    // The default 5s task watchdog aborts during long one-time boot work
    // (LittleFS first-format / mount recovery takes 5-15s). We monitor
    // liveness ourselves via loop_ms_ago in /status.json instead.
    esp_task_wdt_deinit();

    Serial.begin(115200);
    {
        // Console = USB-Serial-JTAG (HWCDC). Wait briefly for a host to
        // attach, bounded so a headless boot is unaffected.
        const uint32_t t0 = millis();
        while (!Serial && (millis() - t0) < 3000) { /* spin */ }
    }
    Serial.println();
    Serial.println("=== DripMeter boot ===");

    // --- 1. Piezo sensor (per hardware spec) -------------------------------
    // Only the virtual ground is set up here. All ADC work (resolution,
    // pin attach, attenuation) runs inside the sampler task — analogRead
    // from this (UI) thread deadlocks the core on this core/BSP combo.
    pinMode(PIN_PIEZO_GND, OUTPUT);
    digitalWrite(PIN_PIEZO_GND, LOW);              // virtual ground
    setStage("piezo");

    // --- 2. Display backlight (LEDC PWM on GPIO1) --------------------------
    ledcAttach(PIN_LCD_BL, 1000, 8);   // pin, 1 kHz, 8-bit (core 3.3 API)
    ledcWrite(PIN_LCD_BL, BL_BRIGHTNESS);
    setStage("backlight");

    // --- 3. Display ----------------------------------------------------------
    if (!gUi.begin())
    {
        Serial.println("[FATAL] display init failed");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    setStage("display");

    // --- 4. Time (SNTP armed now; first sync lands once Wi-Fi is up) -------
    TimeService::begin();
    setStage("ntp");

    // --- 5. Storage ----------------------------------------------------------
    gLog.begin();      // failure is non-fatal: sampling continues, CSV off
    setStage("storage");

    // Learned drop template (matched-filter mode) + persisted calibration.
    gSampler.templateStore().load();
    gSampler.noiseStore().load();
    Tuning::load(gSampler);
    gLog.appendSettings(gSampler.margin(), gSampler.noiseMult(),
                        gSampler.matchThr(), gSampler.matchMinRms(),
                        gSampler.refractoryMs(), gSampler.releaseMs(),
                        gSampler.captureWindowMs(), gSampler.detectMode());

    // --- 6. Wi-Fi -------------------------------------------------------------
    WiFi.setSleep(false);               // keep the radio responsive (SNTP)
    WiFi.mode(WIFI_AP_STA);             // STA + always-on fallback hotspot:
    WiFi.softAP("DripMeter", nullptr, 1, 0, 1);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onWifiEvent);          // all further WiFi state via events
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WIFI] connecting to \"%s\" ...\n", WIFI_SSID);
    Serial.printf("[WIFI] AP up: DripMeter @ %s\n",
                  WiFi.softAPIP().toString().c_str());
    gWeb.begin();                       // /  /drops.csv  /status.json  /set
    setStage("wifi");

    // --- 7. Sampler (task only; ADC pin config already staged above) --------
    gSampler.begin();
    setStage("running");

    // The dashboard is already live from the staged renders above.
    gUi.render(buildSnapshot());
}

// ---------------------------------------------------------------------------
void loop()
{
    // Loop liveness for the status page (see /status.json loop_ms_ago).
    gLoopMs.store(millis(), std::memory_order_relaxed);

    // --- 1. Drain the sampler ring: log + histogram + prompt redraw --------
    DropEvent ev;
    while (gSampler.pop(ev))
    {
        gLog.append(ev);

        struct tm tmv;
        if (TimeService::localTime(tmv))
            gHisto.record(tmv);          // buckets only while the clock is sane

        gUi.noteEvent();
        DBG_PRINTF("[DROP] peak=%u epoch=%lld\n",
                   ev.peak, (long long)ev.epochUs);
    }

    // --- 2. Time maintenance + one-time histogram rebuild after first sync -
    TimeService::tick();
    if (!gFirstTimeSync && TimeService::synced())
    {
        gFirstTimeSync = true;
        struct tm tmv;
        if (TimeService::localTime(tmv))
            gLog.rebuildToday(gHisto, tmv);
        Serial.printf("[NTP] first sync OK (TZ=%s)\n", TZ_STRING);
    }

    // --- 3. Wi-Fi: nothing to do here — state arrives via events (see
    //        onWifiEvent); auto-reconnect is handled by the stack. The loop
    //        must never block on WiFi APIs.

    // --- 4. Histogram midnight rollover -------------------------------------
    {
        struct tm tmv;
        if (TimeService::localTime(tmv))
            gHisto.rolloverIfNeeded(tmv);
    }

    // --- 5. Log compaction housekeeping --------------------------------------
    gLog.maintain();

    // (Web requests are served by their own dedicated task — nothing to do
    //  here, and no client stall can block this loop anymore.)

    // --- 6. Render the dashboard on its cadence ------------------------------
    gUi.renderIfDue(buildSnapshot());

    // Heartbeat (diagnostic only; silent unless DRIPMETER_SERIAL_LOG).
    {
        static uint32_t lastBeat = 0;
        if (millis() - lastBeat >= 5000)
        {
            lastBeat = millis();
            DBG_PRINTF("[ALIVE] up=%lus heap=%u raw=%u env=%u thr=%u drops=%lu\n",
                       (unsigned long)(millis() / 1000),
                       ESP.getFreeHeap(),
                       gSampler.rawADC(), gSampler.envelope(), gSampler.threshold(),
                       (unsigned long)gSampler.triggerCount());
        }
    }

    // Loop yields a tick; the sampler preempts it at priority 24 anyway.
    vTaskDelay(pdMS_TO_TICKS(1));
}
