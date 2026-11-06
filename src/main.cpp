// ============================================================================
//  DripMeter — acoustic ceiling-leak drop monitor
//  Board: Guition JC3248W535 (ESP32-S3, 320x480 AXS15231B QSPI display)
//
//  Task layout:
//    * drop_sampler task (core 1, prio 20) — dedicated ADC polling, never
//      blocked by rendering or WiFi; pushes DropEvents into an SPSC ring.
//    * loopTask (core 0, prio 1) — drains the ring, appends to the CSV
//      log, maintains NTP, and renders the dashboard on a 1 s cadence.
//    * drip_web task (core 0, prio 3) — serves the unified HTTP dashboard
//      and calibration scope on port 80.
//
//  Data flow:   piezo -> ADC1_CH5 -> SPSC ring -> LittleFS CSV + histogram
//                                                                 -> canvas
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "esp_task_wdt.h"
#include "esp_system.h"
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

// Loop liveness heartbeat for /status.json (diagnostic).
std::atomic<uint32_t> gLoopMs{0};

// Last completed setup stage — rendered into the dashboard footer as
// "SYS: <stage>" during boot so a stuck boot names its failing stage on screen.
static char gStage[24] = "boot";
static bool gFirstTimeSync = false;

static void setStage(const char *s);

static DashSnapshot buildSnapshot()
{
    DashSnapshot s;

    s.timeValid    = TimeService::localTime(s.now);
    s.wifiUp       = gWifiUp.load();
    s.wifiRssi     = (int8_t)gWifiRssi.load();
    s.ntpOk        = TimeService::synced();
    s.fsOk         = gLog.fsOk();
    s.fsFreeBytes  = gLog.totalBytes() - gLog.usedBytes();
    s.fsTotalBytes = gLog.totalBytes();
    s.logOk        = gLog.ok();

    s.dropsToday   = gHisto.anchored() ? gHisto.total() : 0;
    s.histo        = &gHisto;

    s.haveLastDrop    = (gSampler.lastDropEpochUs() != 0);
    s.lastDropEpochUs = gSampler.lastDropEpochUs();
    s.lastPeak        = gSampler.lastPeak();

    s.liveRaw       = gSampler.rawADC();
    s.envelope      = gSampler.envelope();
    s.threshold     = gSampler.threshold();
    s.totalTriggers = gSampler.triggerCount();
    s.sampleRate    = gSampler.samplesPerSecond();
    s.stage         = gStage;
    s.ip[0] = 0;
    strlcpy(s.ip, gWifiIp, sizeof(s.ip));

    return s;
}

static void setStage(const char *s)
{
    snprintf(gStage, sizeof(gStage), "%s", s);
    Serial.printf("[BOOT] %s\n", s);
    gUi.render(buildSnapshot());   // visible immediately, even mid-setup
}

void setup()
{
    // The default 5s task watchdog aborts during long one-time boot work
    // (LittleFS first-format / mount recovery takes 5-15s). We monitor
    // liveness ourselves via loop_ms_ago in /status.json instead.
    esp_task_wdt_deinit();

    // Boot diary: one line per boot with reset reason so reboots can be
    // diagnosed after the fact without continuous NVS flash wear.
    {
        const char *reason = "other";
        switch (esp_reset_reason())
        {
        case ESP_RST_POWERON:   reason = "poweron"; break;
        case ESP_RST_SW:        reason = "sw"; break;
        case ESP_RST_PANIC:     reason = "panic"; break;
        case ESP_RST_BROWNOUT:  reason = "brownout"; break;
        case ESP_RST_WDT:       reason = "wdt"; break;
        case ESP_RST_DEEPSLEEP: reason = "deepsleep"; break;
        case ESP_RST_USB:       reason = "usb"; break;
        default: break;
        }
        if (LittleFS.begin(false))
        {
            fs::File f = LittleFS.open("/bootlog.csv", FILE_APPEND);
            if (f)
            {
                f.printf("%ld,%s\n", (long)time(nullptr), reason);
                f.close();
            }
            LittleFS.end();
        }
    }

    Serial.begin(115200);
    {
        // Console = USB-Serial-JTAG (HWCDC). Wait briefly for a host to
        // attach, bounded so a headless boot is unaffected.
        const uint32_t t0 = millis();
        while (!Serial && (millis() - t0) < 3000) { /* spin */ }
    }
    Serial.println();
    Serial.println("=== DripMeter boot ===");

    // --- 1. Piezo sensor (ADC setup runs inside sampler task on Core 1) ----
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

    // --- 5. Storage & Calibration --------------------------------------------
    gLog.begin();      // failure is non-fatal: sampling continues, CSV off
    setStage("storage");

    Tuning::load(gSampler);
    gLog.appendSettings(gSampler.margin(), gSampler.noiseMult(),
                        gSampler.refractoryMs(), gSampler.releaseMs(),
                        gSampler.captureWindowMs(),
                        gSampler.qtyEnabled(), gSampler.ulPerDropTenths());

    // --- 6. Wi-Fi -------------------------------------------------------------
    WiFi.setSleep(false);               // keep the radio responsive (SNTP)
    WiFi.mode(WIFI_AP_STA);             // STA + always-on fallback hotspot
    WiFi.softAP("DripMeter", nullptr, 1, 0, 1);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onWifiEvent);          // all further WiFi state via events
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WIFI] connecting to \"%s\" ...\n", WIFI_SSID);
    Serial.printf("[WIFI] AP up: DripMeter @ %s\n",
                  WiFi.softAPIP().toString().c_str());
    gWeb.begin();                       // unified dashboard on port 80
    setStage("wifi");

    // --- 7. Sampler (dedicated Core 1 task) ----------------------------------
    gSampler.begin();
    setStage("running");

    gUi.render(buildSnapshot());
}

void loop()
{
    // Loop liveness for /status.json (loop_ms_ago).
    gLoopMs.store(millis(), std::memory_order_relaxed);

    // --- 1. Drain the sampler ring: log + histogram + prompt redraw --------
    DropEvent ev;
    while (gSampler.pop(ev))
    {
        gSampler.recordCalDrop(ev.envPeak);
        const uint16_t volUl = gSampler.estimateDropUl(ev.envPeak);
        gLog.append(ev, gSampler.qtyEnabled() ? volUl : 0);

        struct tm tmv;
        if (TimeService::localTime(tmv))
            gHisto.record(tmv, (time_t)(ev.epochUs / 1000000LL), volUl);

        gUi.noteEvent();
        DBG_PRINTF("[DROP] peak=%u env=%u vol=%uuL epoch=%lld\n",
                   ev.peak, ev.envPeak, volUl, (long long)ev.epochUs);
    }

    // --- 2. Time maintenance + one-time histogram rebuild after first sync -
    TimeService::tick();
    if (!gFirstTimeSync && TimeService::synced())
    {
        gFirstTimeSync = true;
        struct tm tmv;
        if (TimeService::localTime(tmv))
            gLog.rebuildToday(gHisto, tmv, &gSampler);
        Serial.printf("[NTP] first sync OK (TZ=%s)\n", TZ_STRING);
    }

    // --- 3. Histogram midnight rollover -------------------------------------
    {
        struct tm tmv;
        if (TimeService::localTime(tmv))
            gHisto.rolloverIfNeeded(tmv);
    }

    // --- 4. Log compaction housekeeping --------------------------------------
    gLog.maintain();

    // --- 5. Render the dashboard on its cadence ------------------------------
    gUi.renderIfDue(buildSnapshot());

    // Optional serial diagnostic heartbeat (no NVS flash writes in loop!).
#if DRIPMETER_SERIAL_LOG
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
#endif

    vTaskDelay(pdMS_TO_TICKS(1));
}
