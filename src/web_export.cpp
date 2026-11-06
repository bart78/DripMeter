// ============================================================================
//  web_export.cpp — unified single-page dashboard + visual tuning scope
// ============================================================================
#include "web_export.h"
#include "time_service.h"
#include "tuning.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <stdio.h>
#include <math.h>
#include <atomic>

// Loop liveness counter maintained by main's loop() (diagnostic).
extern std::atomic<uint32_t> gLoopMs;

namespace
{
    static const char * stateLabel(uint8_t st)
    {
        switch (st)
        {
        case 1:  return "CAPTURING";
        case 2:  return "RELEASE";
        default: return "ARMED";
        }
    }

    // Modern CSS architecture using :root design tokens, logical properties,
    // native <details>/<summary> disclosure, and :not(:disabled) selectors.
    static const char PAGE_CSS[] PROGMEM = R"rawliteral(
<style>
@layer base, components, utilities;
:root {
  --bg-page: #0b0e14;
  --bg-card: #131822;
  --bg-elevated: #1a2130;
  --bg-input: #0f131c;
  --border-subtle: #263042;
  --border-focus: #38bdf8;
  --text-main: #edf2f7;
  --text-muted: #8b9bb4;
  --accent-cyan: #38bdf8;
  --accent-blue: #0284c7;
  --accent-green: #22c55e;
  --accent-amber: #f59e0b;
  --accent-red: #f43f5e;
  --accent-purple: #a855f7;
  --radius-md: 10px;
  --radius-sm: 6px;
  --font-sans: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
@layer base {
  body {
    font-family: var(--font-sans);
    background: var(--bg-page);
    color: var(--text-main);
    margin: 0;
    padding-block: 0.55rem 1.8rem;
    padding-inline: 0.7rem;
    line-height: 1.35;
  }
  main, header {
    max-width: 820px;
    margin-inline: auto;
  }
  h1, h2, h3 {
    margin-block: 0;
    font-weight: 650;
    letter-spacing: -0.02em;
  }
  a {
    color: var(--accent-cyan);
    text-decoration: none;
  }
  a:hover {
    text-decoration: underline;
  }
}
@layer components {
  .topbar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 0.45rem;
    padding-block-end: 0.5rem;
    margin-block-end: 0.55rem;
    border-block-end: 1px solid var(--border-subtle);
  }
  .brand {
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
  }
  .brand h1 {
    font-size: 1.15rem;
  }
  .brand-sub {
    font-size: 0.78rem;
    color: var(--text-muted);
    font-family: var(--font-mono);
  }
  .pills {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem;
    align-items: center;
  }
  .pill {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    padding: 0.18rem 0.5rem;
    border-radius: 999px;
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    color: var(--text-muted);
  }
  .pill-armed {
    color: var(--accent-green);
    border-color: rgba(34, 197, 94, 0.35);
    background: rgba(34, 197, 94, 0.08);
  }
  .pill-cap {
    color: var(--accent-cyan);
    border-color: rgba(56, 189, 248, 0.45);
    background: rgba(56, 189, 248, 0.12);
  }
  .pill-rel {
    color: var(--accent-amber);
    border-color: rgba(245, 158, 11, 0.45);
    background: rgba(245, 158, 11, 0.12);
  }
  .kpi-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(148px, 1fr));
    gap: 0.45rem;
    margin-block-end: 0.55rem;
  }
  .card {
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-md);
    padding: 0.55rem 0.75rem;
  }
  .kpi-label {
    font-size: 0.68rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--text-muted);
    margin-block-end: 0.12rem;
  }
  .kpi-val {
    font-family: var(--font-mono);
    font-size: 1.32rem;
    font-weight: 700;
    color: var(--text-main);
    line-height: 1.1;
  }
  .kpi-accent {
    color: var(--accent-cyan);
  }
  .kpi-sub {
    font-size: 0.72rem;
    color: var(--text-muted);
    margin-block-start: 0.18rem;
    font-family: var(--font-mono);
  }
  .sec-head {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: center;
    gap: 0.4rem;
    margin-block-end: 0.4rem;
  }
  .sec-head h2 {
    font-size: 0.92rem;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 0.25rem 0.65rem;
    font-size: 0.72rem;
    color: var(--text-muted);
    font-family: var(--font-mono);
    line-height: 1.25;
  }
  .dot {
    display: inline-block;
    width: 7px;
    height: 7px;
    border-radius: 2px;
    margin-inline-end: 3px;
    vertical-align: middle;
  }
  .toolbar {
    display: flex;
    flex-wrap: nowrap;
    gap: 0.4rem;
    align-items: center;
    margin-block-start: 0.45rem;
    padding-block-start: 0.45rem;
    border-block-start: 1px solid var(--border-subtle);
  }
  .sel {
    font-family: var(--font-sans);
    font-size: 0.8rem;
    min-height: 34px;
    padding-inline: 0.5rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-input);
    color: var(--text-main);
    max-width: 190px;
  }
  .btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 0.3rem;
    font-family: var(--font-sans);
    font-size: 0.8rem;
    font-weight: 550;
    min-height: 34px;
    padding-inline: 0.65rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-elevated);
    color: var(--text-main);
    cursor: pointer;
    text-decoration: none;
    white-space: nowrap;
  }
  .btn:hover:not(:disabled) {
    border-color: var(--accent-cyan);
    background: #222b3d;
    text-decoration: none;
  }
  .btn:focus-visible, input:focus-visible, select:focus-visible, summary:focus-visible {
    outline: 2px solid var(--border-focus);
    outline-offset: 2px;
  }
  .btn:disabled {
    opacity: 0.55;
    cursor: default;
  }
  .btn-danger {
    color: #fda4af;
    border-color: rgba(244, 63, 94, 0.35);
  }
  .btn-danger:hover:not(:disabled) {
    border-color: var(--accent-red);
    background: rgba(244, 63, 94, 0.14);
  }
  /* Collapsible Disclosure Tabs */
  details.tuning-drawer {
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-md);
    margin-block-start: 0.55rem;
    overflow: hidden;
  }
  details.tuning-drawer > summary {
    list-style: none;
    cursor: pointer;
    padding: 0.6rem 0.85rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    background: var(--bg-elevated);
    font-weight: 600;
    font-size: 0.9rem;
    user-select: none;
  }
  details.tuning-drawer > summary::-webkit-details-marker {
    display: none;
  }
  details.tuning-drawer > summary::after {
    content: "Show Knobs ▾";
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 500;
    color: var(--accent-cyan);
    padding: 0.18rem 0.5rem;
    border-radius: var(--radius-sm);
    border: 1px solid rgba(56, 189, 248, 0.3);
    background: rgba(56, 189, 248, 0.08);
  }
  details.tuning-drawer[open] > summary {
    border-block-end: 1px solid var(--border-subtle);
  }
  details.tuning-drawer[open] > summary::after {
    content: "Hide Knobs ▴";
  }
  details.qty-drawer > summary::after {
    content: "Calibrate ▾";
  }
  details.qty-drawer[open] > summary::after {
    content: "Hide ▴";
  }
  details.pat-drawer > summary::after {
    content: "Baseline & Log ▾";
  }
  details.pat-drawer[open] > summary::after {
    content: "Hide ▴";
  }
  .tuning-body {
    padding: 0.75rem 0.85rem;
  }
  .scope-wrap {
    position: relative;
    margin-block-end: 0.45rem;
  }
  canvas#scope-cv {
    width: 100%;
    height: auto;
    display: block;
    background: #0d1119;
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-sm);
    touch-action: pan-y;
    cursor: default;
  }
  canvas#scope-cv.unlocked {
    touch-action: none;
    cursor: ns-resize;
  }
  /* Responsive sticky tables for Pattern Baseline & Event Logger */
  .table-wrapper {
    overflow: auto;
    max-inline-size: 100%;
    max-block-size: min(260px, 50vh);
    container-type: inline-size;
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-sm);
    background: var(--bg-input);
    margin-block-top: 0.45rem;
  }
  .table-wrapper table {
    width: 100%;
    border-collapse: separate;
    border-spacing: 0;
    font-family: var(--font-mono);
    font-size: 0.75rem;
    text-align: left;
  }
  .table-wrapper caption {
    text-align: left;
    padding: 0.35rem 0.55rem;
    font-family: var(--font-sans);
    font-weight: 600;
    font-size: 0.78rem;
    color: var(--accent-cyan);
    background: var(--bg-elevated);
    border-block-end: 1px solid var(--border-subtle);
  }
  .table-wrapper thead {
    position: sticky;
    inset-block-start: 0;
    z-index: 2;
    background: var(--bg-elevated);
  }
  .table-wrapper th, .table-wrapper td {
    padding: 0.32rem 0.5rem;
    border-block-end: 1px solid var(--border-subtle);
    white-space: nowrap;
  }
  .table-wrapper tbody th {
    position: sticky;
    inset-inline-start: 0;
    z-index: 1;
    background: var(--bg-card);
    color: var(--text-main);
    font-weight: 600;
  }
  .ev-text {
    font-family: var(--font-sans);
    font-size: 0.85rem;
    min-height: 34px;
    padding: 0.2rem 0.5rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-card);
    color: var(--text-main);
    box-sizing: border-box;
  }
  .cal-steps {
    margin: 0.5rem 0 0.85rem 1.2rem;
    padding: 0;
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .cal-steps li {
    margin-block-end: 0.3rem;
  }
  .cal-steps strong {
    color: var(--text-main);
  }
  .cal-banner {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-sm);
    padding: 0.75rem 0.9rem;
    margin-block-end: 0.85rem;
  }
  .cal-metrics {
    display: flex;
    flex-wrap: wrap;
    gap: 1rem;
    font-family: var(--font-mono);
    font-size: 0.85rem;
  }
  .cal-row {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-end;
    gap: 0.75rem;
    margin-block-top: 0.65rem;
  }
  .cal-field {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .cal-field label {
    font-size: 0.78rem;
    color: var(--text-muted);
    font-weight: 600;
  }
  .cal-field input[type="number"] {
    width: 7.5rem;
    font-family: var(--font-mono);
    font-size: 0.95rem;
    min-height: 40px;
    padding: 0.25rem 0.5rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-card);
    color: var(--text-main);
    box-sizing: border-box;
  }
  .chk-row {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    padding-block-end: 0.85rem;
    margin-block-end: 0.85rem;
    border-block-end: 1px solid var(--border-subtle);
  }
  .chk-label {
    display: inline-flex;
    align-items: center;
    gap: 0.55rem;
    font-weight: 600;
    font-size: 0.95rem;
    cursor: pointer;
  }
  .chk-label input[type="checkbox"] {
    width: 1.15rem;
    height: 1.15rem;
    accent-color: var(--accent-cyan);
    cursor: pointer;
  }
  .scope-bar {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: center;
    gap: 0.5rem;
    margin-block-start: 0.5rem;
    font-size: 0.8rem;
    color: var(--text-muted);
  }
  fieldset.knob-group {
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-md);
    padding: 0.85rem 1rem;
    margin-block: 0 1rem;
    background: var(--bg-input);
  }
  fieldset.knob-group > legend {
    font-size: 0.85rem;
    font-weight: 650;
    color: var(--accent-cyan);
    padding-inline: 0.4rem;
  }
  .knob-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(290px, 1fr));
    gap: 0.85rem;
    margin-block-start: 0.4rem;
  }
  .knob-card {
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-sm);
    padding: 0.75rem 0.85rem;
    transition: border-color 0.15s ease;
  }
  .knob-card:hover, .knob-card:focus-within {
    border-color: var(--accent-cyan);
  }
  .knob-top {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 0.5rem;
    margin-block-end: 0.3rem;
  }
  .knob-top label {
    font-weight: 600;
    font-size: 0.92rem;
    color: var(--text-main);
  }
  .knob-badge {
    font-family: var(--font-mono);
    font-size: 0.82rem;
    color: var(--accent-cyan);
  }
  .knob-controls {
    display: flex;
    align-items: center;
    gap: 0.65rem;
    margin-block-end: 0.4rem;
  }
  .knob-controls input[type="range"] {
    flex: 1;
    accent-color: var(--accent-cyan);
    min-height: 36px;
    cursor: pointer;
  }
  .knob-controls input[type="number"] {
    width: 5.2rem;
    font-family: var(--font-mono);
    font-size: 1rem;
    min-height: 40px;
    padding: 0.25rem 0.45rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-input);
    color: var(--text-main);
    box-sizing: border-box;
  }
  input:invalid:user-invalid {
    border-color: var(--accent-red);
  }
  .knob-desc {
    font-size: 0.8rem;
    color: var(--text-muted);
    margin: 0;
  }
  .save-status {
    font-family: var(--font-mono);
    font-size: 0.82rem;
    color: var(--accent-green);
  }
}
</style>
)rawliteral";
}

void WebExport::begin()
{
    if (_started)
        return;

    _server.on("/", HTTP_GET, [this]() { onRoot(); });
    _server.on("/cal", HTTP_GET, [this]() {
        _server.sendHeader("Location", "/#tuning");
        _server.send(302, "text/plain", "");
    });
    _server.on("/drops.csv", HTTP_GET, [this]() { onCsv(); });
    _server.on("/events.csv", HTTP_GET, [this]() { onEventsCsv(); });
    _server.on("/status.json", HTTP_GET, [this]() { onStatus(); });
    _server.on("/stream.json", HTTP_GET, [this]() { onStream(); });
    _server.on("/set", HTTP_GET, [this]() { onSet(); });
    _server.on("/calrun", HTTP_GET, [this]() { onCalRun(); });
    _server.on("/eventlog", HTTP_GET, [this]() { onEventLog(); });
    _server.on("/pause", HTTP_GET, [this]() { onPause(); });
    _server.on("/clearlog", HTTP_GET, [this]() { onClearLog(); });
    _server.on("/record", HTTP_GET, [this]() { onRecord(); });
    _server.on("/rec.wav", HTTP_GET, [this]() { onRecWav(); });
    _server.on("/update", HTTP_POST,
        [this]() {
            const bool ok = !Update.hasError();
            _server.sendHeader("Connection", "close");
            _server.send(ok ? 200 : 500, "application/json",
                         ok ? "{\"ok\":1,\"msg\":\"OTA success, rebooting\"}"
                            : "{\"ok\":0,\"err\":\"OTA failed\"}");
            delay(400);
            if (ok) ESP.restart();
        },
        [this]() {
            HTTPUpload &up = _server.upload();
            if (up.status == UPLOAD_FILE_START) {
                Serial.printf("[OTA] start: %s\n", up.filename.c_str());
                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                Update.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                if (Update.end(true))
                    Serial.printf("[OTA] complete: %u bytes\n", up.totalSize);
                else
                    Update.printError(Serial);
            }
        });
    _server.on("/bootlog.csv", HTTP_GET, [this]() {
        fs::File rd = LittleFS.open("/bootlog.csv", "r");
        if (!rd) { _server.send(404, "text/plain", "no boot log yet"); return; }
        _server.setContentLength(rd.size());
        _server.sendHeader("Content-Type", "text/csv");
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/csv", "");
        uint8_t b2[1024];
        while (rd.available()) { size_t n = rd.read(b2, sizeof(b2)); _server.client().write(b2, n); }
        rd.close();
        _server.client().stop();
    });
    _server.onNotFound([this]() {
        _server.send(404, "text/plain",
                     "not found - try / /drops.csv /status.json /stream.json");
    });
    _server.begin();
    _started = true;

    // Run the server in its OWN task on CORE 0 (the WiFi/UI core): a
    // stalled/dead client connection can never freeze drop detection on Core 1.
    xTaskCreatePinnedToCore(
        taskEntry, "drip_web", 16384, this, 3, nullptr, 0);
    Serial.println("[WEB] http server on port 80 (core 0, dedicated task)");
}

void WebExport::taskEntry(void *arg)
{
    WebExport *w = static_cast<WebExport *>(arg);
    for (;;)
    {
        w->_server.handleClient();
        w->_server.client().setTimeout(3000);   // bound per-request stalls
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void WebExport::histogramSVG(String &out, uint8_t nowIdx)
{
    const int16_t BASE = 118;
    uint16_t mx = _histo.max();
    if (mx == 0)
        mx = 1;

    out.reserve(8192);
    out += "<svg id='histo-svg' viewBox='0 0 302 126' role='img' "
           "aria-label='24-hour drop histogram' xmlns='http://www.w3.org/2000/svg' "
           "style='width:100%;height:auto;background:#0d1119;border-radius:6px;"
           "border:1px solid #263042;display:block'>";

    char r[128];
    for (uint8_t q = 1; q < 4; ++q)
    {
        const int16_t gy = 2 + (116 * q) / 4;
        snprintf(r, sizeof(r),
                 "<line x1='0' y1='%d' x2='302' y2='%d' stroke='#1a2230' stroke-width='1'/>",
                 gy, gy);
        out += r;
    }

    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        const int16_t x = 3 + i * 3;
        const uint16_t cy = _histo.bucketYesterday(i);
        const uint16_t ct = _histo.bucket(i);
        if (cy)
        {
            int16_t h = (int16_t)((uint32_t)cy * 114 / mx);
            if (h < 1) h = 1;
            snprintf(r, sizeof(r),
                     "<rect x='%d' y='%d' width='2' height='%d' fill='#2b384e'/>",
                     x, BASE - h, h);
            out += r;
        }
        if (ct)
        {
            int16_t h = (int16_t)((uint32_t)ct * 114 / mx);
            if (h < 1) h = 1;
            snprintf(r, sizeof(r),
                     "<rect x='%d' y='%d' width='2' height='%d' fill='%s'/>",
                     x, BASE - h, h, (i == nowIdx) ? "#38bdf8" : "#0284c7");
            out += r;
        }
    }

    const int16_t nx = 3 + nowIdx * 3 + 1;
    snprintf(r, sizeof(r),
             "<line x1='%d' y1='2' x2='%d' y2='118' stroke='#f43f5e' stroke-width='1'/>",
             nx, nx);
    out += r;
    for (uint8_t h = 0; h <= 4; ++h)
    {
        const int16_t tx = (h == 4) ? 290 : (3 + h * 72);
        snprintf(r, sizeof(r),
                 "<text x='%d' y='125' font-size='6.5' font-family='monospace' fill='#8b9bb4'>%02d:00</text>",
                 tx, h * 6);
        out += r;
    }
    snprintf(r, sizeof(r),
             "<text x='298' y='10' text-anchor='end' font-size='6.5' font-family='monospace' fill='#8b9bb4'>max %u</text>",
             (unsigned)_histo.max());
    out += r;
    out += "</svg>";
}

// ---------------------------------------------------------------------------
// / — UNIFIED SINGLE-PAGE DASHBOARD + COLLAPSIBLE TUNING SCOPE
// ---------------------------------------------------------------------------
void WebExport::onRoot()
{
    struct tm tmv;
    const bool haveTm = TimeService::localTime(tmv);

    char timeStr[24] = "--:--:--";
    if (haveTm)
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    char lastIso[48] = "never";
    if (_sampler.lastDropEpochUs() != 0)
        if (!TimeService::formatISO(_sampler.lastDropEpochUs(), lastIso, sizeof(lastIso)))
            snprintf(lastIso, sizeof(lastIso), "(clock unsynced)");

    uint8_t nowIdx = 0;
    if (haveTm)
        nowIdx = Histogram::indexFor(tmv);

    uint32_t ydayTotal = 0;
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
        ydayTotal += _histo.bucketYesterday(i);

    String svg;
    histogramSVG(svg, nowIdx);

    String page;
    page.reserve(30720);
    page += "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<title>DripMeter</title>";
    page += FPSTR(PAGE_CSS);
    page += "</head><body>";

    char buf[2400];
    const uint8_t st = _sampler.detectorState();
    const bool isPaused = _sampler.paused();
    const char *stClass = isPaused ? "pill-rel" : ((st == 1) ? "pill-cap" : ((st == 2) ? "pill-rel" : "pill-armed"));
    const char *stText  = isPaused ? "PAUSED" : stateLabel(st);

    snprintf(buf, sizeof(buf),
        "<header class='topbar'>"
        "<div class='brand'>"
        "<h1>DripMeter</h1>"
        "<span class='brand-sub' id='hdr-time'>%s %s</span>"
        "</div>"
        "<div class='pills'>"
        "<button type='button' class='btn' id='btn-pause-log' style='min-height:26px;padding:0.15rem 0.55rem;font-size:0.75rem;border-color:%s;color:%s'>%s</button>"
        "<span class='pill %s' id='hdr-state'>%s</span>"
        "<span class='pill' id='hdr-sps'>%.1f kS/s</span>"
        "<span class='pill' id='hdr-wifi'>Wi-Fi %d dBm</span>"
        "</div>"
        "</header>"
        "<main>"
        "<div id='pause-banner' class='cal-banner' style='display:%s;margin-block-end:0.55rem;border-color:#f59e0b;background:rgba(245,158,11,0.12)'>"
        "<span style='color:#fbbf24;font-size:0.8rem'><strong>Logging Paused</strong> — Live Scope stays active so you can reposition &amp; test the box without recording fake drops.</span>"
        "<div style='display:flex;gap:0.4rem;flex-wrap:wrap'>"
        "<button type='button' class='btn' id='btn-resume-banner' style='border-color:#22c55e;color:#4ade80'>Resume Logging</button>"
        "<button type='button' class='btn' id='btn-trim-5m'>Undo Last 5m</button>"
        "<button type='button' class='btn' id='btn-trim-15m'>Undo Last 15m</button>"
        "</div></div>"
        "<section class='kpi-grid' aria-label='Live summary metrics'>"
        "<div class='card'>"
        "<div class='kpi-label'>Drops Today</div>"
        "<div class='kpi-val kpi-accent' id='kpi-today'>%lu</div>"
        "<div class='kpi-sub'>Yesterday: <span id='kpi-yday'>%lu</span> · Total triggers: <span id='kpi-trig'>%lu</span></div>"
        "</div>"
        "<div class='card'>"
        "<div class='kpi-label'>Last Drop</div>"
        "<div class='kpi-val' id='kpi-ago'>--</div>"
        "<div class='kpi-sub' id='kpi-last-iso'>%s</div>"
        "</div>"
        "<div class='card'>"
        "<div class='kpi-label'>Last Impact Peak</div>"
        "<div class='kpi-val'><span id='kpi-peak'>%u</span> <span style='font-size:0.9rem;color:#8b9bb4'>raw</span></div>"
        "<div class='kpi-sub'>Envelope peak: <span id='kpi-envpeak'>%u</span></div>"
        "</div>"
        "<div class='card'>"
        "<div class='kpi-label'>Live Envelope / Threshold</div>"
        "<div class='kpi-val'><span id='kpi-env'>%u</span> <span style='font-size:1rem;color:#8b9bb4'>/ <span id='kpi-thr'>%u</span></span></div>"
        "<div class='kpi-sub'>Baseline: <span id='kpi-base'>%u</span> · Raw ADC: <span id='kpi-raw'>%u</span></div>"
        "</div>"
        "</section>",
        timeStr, TimeService::synced() ? "(NTP)" : "(unsynced)",
        isPaused ? "#22c55e" : "#f59e0b",
        isPaused ? "#4ade80" : "#fbbf24",
        isPaused ? "Resume Logging" : "Pause Logging",
        stClass, stText,
        (float)_sampler.samplesPerSecond() / 1000.0f,
        (int)WiFi.RSSI(),
        isPaused ? "flex" : "none",
        (unsigned long)_histo.total(),
        (unsigned long)ydayTotal,
        (unsigned long)_sampler.triggerCount(),
        lastIso,
        (unsigned)_sampler.lastPeak(),
        (unsigned)_sampler.lastEnvPeak(),
        (unsigned)_sampler.envelope(),
        (unsigned)_sampler.threshold(),
        (unsigned)_sampler.envBase(),
        (unsigned)_sampler.rawADC());
    page += buf;

    // --- Water Quantity KPI Row (visible when "Measure quantity" is checked) ---
    const uint32_t nowEpoch = (uint32_t)(TimeService::epochUs() / 1000000LL);
    const float vol1hMl = (float)_histo.lastHourVolUl(nowEpoch) / 1000.0f;
    const uint32_t drops1h = _histo.lastHourDrops(nowEpoch);
    const float vol24hMl = (float)_histo.last24hVolUl(nowEpoch) / 1000.0f;
    const uint32_t drops24h = _histo.last24hDrops(nowEpoch);
    const float volTodayMl = (float)_histo.totalVolTodayUl() / 1000.0f;
    const float volAvg24hMl = (float)_histo.rolling24hAvgVolUl(nowEpoch) / 1000.0f;
    const float uldUl = (float)_sampler.ulPerDropTenths() / 10.0f;
    const float dpm = (uldUl > 0.1f) ? (1000.0f / uldUl) : 20.0f;

    snprintf(buf, sizeof(buf),
        "<section class='kpi-grid' id='vol-kpi-grid' style='display:%s' aria-label='Water quantity metrics'>"
        "<div class='card'>"
        "<div class='kpi-label'>Last Hour Volume</div>"
        "<div class='kpi-val kpi-accent'><span id='kpi-vol-1h'>%.2f</span> <span style='font-size:0.95rem;color:#8b9bb4'>mL</span></div>"
        "<div class='kpi-sub' id='kpi-vol-1h-sub'>%lu drops in last 60m</div>"
        "</div>"
        "<div class='card'>"
        "<div class='kpi-label'>Last 24 Hours Volume</div>"
        "<div class='kpi-val'><span id='kpi-vol-24h'>%.2f</span> <span style='font-size:0.95rem;color:#8b9bb4'>mL</span></div>"
        "<div class='kpi-sub' id='kpi-vol-24h-sub'>%lu drops · Today: %.2f mL</div>"
        "</div>"
        "<div class='card'>"
        "<div class='kpi-label'>24h Rolling Average</div>"
        "<div class='kpi-val'><span id='kpi-vol-avg'>%.2f</span> <span style='font-size:0.95rem;color:#8b9bb4'>mL/day</span></div>"
        "<div class='kpi-sub' id='kpi-vol-trend'>Calibrated: %.1f &micro;L/drop (~%.1f drops/mL)</div>"
        "</div>"
        "</section>",
        _sampler.qtyEnabled() ? "grid" : "none",
        vol1hMl, (unsigned long)drops1h,
        vol24hMl, (unsigned long)drops24h, volTodayMl,
        volAvg24hMl, uldUl, dpm);
    page += buf;

    // --- 24h Histogram Card ------------------------------------------------
    page += "<section class='card' aria-label='24-hour distribution'>"
            "<div class='sec-head'>"
            "<h2>24-Hour Drop Distribution (15m)</h2>"
            "<div class='legend'>"
            "<span><i class='dot' style='background:#0284c7'></i>Today</span>"
            "<span><i class='dot' style='background:#38bdf8'></i>Now bucket</span>"
            "<span><i class='dot' style='background:#2b384e'></i>Yday</span>"
            "<span><i class='dot' style='background:#f43f5e'></i>Now</span>"
            "</div></div>"
            "<div id='histo-box'>";
    page += svg;
    page += "</div>"
            "<div class='toolbar'>"
            "<select id='sel-export' class='sel' aria-label='Export file, save audio, or undo handling noise'>"
            "<option value='/drops.csv'>drops.csv (Drop Log)</option>"
            "<option value='rec_wav'>Save &amp; Get 60s Audio (.wav)</option>"
            "<option value='/events.csv'>events.csv (Event Log)</option>"
            "<option value='trim_5'>Undo Last 5m Drops (Handling Noise)</option>"
            "<option value='trim_15'>Undo Last 15m Drops (Handling Noise)</option>"
            "<option value='/status.json'>status.json (Snapshot)</option>"
            "<option value='/bootlog.csv'>bootlog.csv (Boot Log)</option>"
            "</select>"
            "<button type='button' class='btn' id='btn-export'>Go</button>"
            "<span id='rec-msg' class='kpi-sub' aria-live='polite'></span>"
            "<button type='button' class='btn btn-danger' id='btn-clear' style='margin-inline-start:auto'>Clear Log</button>"
            "</div></section>";

    // --- Live Signal Scope Card (Always visible) + Collapsible Tuning ------
    page += "<section class='card' style='margin-block-start:0.55rem' aria-label='Live Signal Scope'>"
            "<div class='sec-head'>"
            "<h2>Live Signal Scope (5.2s)</h2>"
            "<div style='display:flex;gap:0.4rem;align-items:center'>"
            "<span id='save-badge' class='save-status' aria-live='polite'></span>"
            "<button type='button' class='btn' id='btn-pause' style='min-height:28px;padding-inline:0.55rem;font-size:0.75rem'>Freeze Scope</button>"
            "</div></div>"
            "<div class='scope-wrap'>"
            "<canvas id='scope-cv' width='760' height='165'></canvas>"
            "<div class='scope-bar'>"
            "<div class='legend'>"
            "<span><i class='dot' style='background:#38bdf8'></i>Env</span>"
            "<span><i class='dot' style='background:#f43f5e'></i><span id='thr-legend'>Thr (locked)</span></span>"
            "<span><i class='dot' style='background:#64748b'></i>Base</span>"
            "<span><i class='dot' style='background:#22c55e'></i>Capture</span>"
            "<span><i class='dot' style='background:#f59e0b'></i>Holdoff</span>"
            "</div></div></div>"

            "<details class='tuning-drawer' id='tuning-drawer'>"
            "<summary>Detection Tuning Knobs</summary>"
            "<div class='tuning-body'>"
            "<form id='tune-form' action='/set' method='GET'>"
            "<fieldset class='knob-group'>"
            "<legend>1. Amplitude Sensitivity — What counts as a drop?</legend>"
            "<div class='knob-grid'>";

    snprintf(buf, sizeof(buf),
        "<div class='knob-card' data-focus-knob='margin'>"
        "<div class='knob-top'>"
        "<label for='in-margin'>Trigger Margin</label>"
        "<span class='knob-badge'><span id='lbl-margin'>%u</span> raw</span>"
        "</div>"
        "<div class='knob-controls'>"
        "<input type='range' id='rng-margin' min='5' max='120' value='%u' aria-label='Trigger Margin slider'>"
        "<input type='number' id='in-margin' name='margin' min='5' max='500' value='%u' required aria-describedby='desc-margin'>"
        "</div>"
        "<p class='knob-desc' id='desc-margin'>"
        "Minimum height above the quiet baseline required to trigger a drop. "
        "With the powered piezo, idle noise is 0–2 and real drops hit 20–46 (15 is ideal)."
        "</p></div>"

        "<div class='knob-card' data-focus-knob='noisemult'>"
        "<div class='knob-top'>"
        "<label for='in-nmult'>Ambient Noise Guard</label>"
        "<span class='knob-badge'><span id='lbl-nmult'>%u</span>&times; noise</span>"
        "</div>"
        "<div class='knob-controls'>"
        "<input type='range' id='rng-nmult' min='0' max='12' value='%u' aria-label='Ambient Noise Guard slider'>"
        "<input type='number' id='in-nmult' name='noisemult' min='0' max='20' value='%u' required aria-describedby='desc-nmult'>"
        "</div>"
        "<p class='knob-desc' id='desc-nmult'>"
        "Automatically lifts the threshold during sustained room noise (appliances, footsteps), "
        "capped at +1&times; Margin so it never runs away."
        "</p></div>"
        "</div></fieldset>",
        (unsigned)_sampler.margin(),
        (unsigned)(_sampler.margin() > 120 ? 120 : _sampler.margin()),
        (unsigned)_sampler.margin(),
        (unsigned)_sampler.noiseMult(),
        (unsigned)(_sampler.noiseMult() > 12 ? 12 : _sampler.noiseMult()),
        (unsigned)_sampler.noiseMult());
    page += buf;

    page += "<fieldset class='knob-group'>"
            "<legend>2. Timing &amp; Anti-Double-Count — Every drop counted once</legend>"
            "<div class='knob-grid'>";

    snprintf(buf, sizeof(buf),
        "<div class='knob-card' data-focus-knob='captwin'>"
        "<div class='knob-top'>"
        "<label for='in-cap'>Peak Capture Window</label>"
        "<span class='knob-badge'><span id='lbl-cap'>%u</span> ms</span>"
        "</div>"
        "<div class='knob-controls'>"
        "<input type='range' id='rng-cap' min='20' max='200' step='5' value='%u' aria-label='Peak Capture Window slider'>"
        "<input type='number' id='in-cap' name='captwin' min='20' max='300' value='%u' required aria-describedby='desc-cap'>"
        "</div>"
        "<p class='knob-desc' id='desc-cap'>"
        "Green zone after trigger onset where the detector tracks the drop's highest PeakADC and EnvPeak (60 ms catches the full impact)."
        "</p></div>"

        "<div class='knob-card' data-focus-knob='release'>"
        "<div class='knob-top'>"
        "<label for='in-rel'>Ring-Tail Release</label>"
        "<span class='knob-badge'><span id='lbl-rel'>%u</span> ms</span>"
        "</div>"
        "<div class='knob-controls'>"
        "<input type='range' id='rng-rel' min='40' max='500' step='10' value='%u' aria-label='Ring-Tail Release slider'>"
        "<input type='number' id='in-rel' name='release' min='40' max='1000' value='%u' required aria-describedby='desc-rel'>"
        "</div>"
        "<p class='knob-desc' id='desc-rel'>"
        "Amber quiet-holdoff after capture: the signal must stay below threshold for this long before re-arming, swallowing tub vibrations and splashes."
        "</p></div>",
        (unsigned)_sampler.captureWindowMs(),
        (unsigned)(_sampler.captureWindowMs() > 200 ? 200 : _sampler.captureWindowMs()),
        (unsigned)_sampler.captureWindowMs(),
        (unsigned)_sampler.releaseMs(),
        (unsigned)(_sampler.releaseMs() > 500 ? 500 : _sampler.releaseMs()),
        (unsigned)_sampler.releaseMs());
    page += buf;

    snprintf(buf, sizeof(buf),
        "<div class='knob-card' data-focus-knob='refract'>"
        "<div class='knob-top'>"
        "<label for='in-refr'>Minimum Drop Spacing (Refractory)</label>"
        "<span class='knob-badge'><span id='lbl-refr'>%u</span> ms</span>"
        "</div>"
        "<div class='knob-controls'>"
        "<input type='range' id='rng-refr' min='80' max='1000' step='10' value='%u' aria-label='Minimum Drop Spacing slider'>"
        "<input type='number' id='in-refr' name='refract' min='80' max='2000' value='%u' required aria-describedby='desc-refr'>"
        "</div>"
        "<p class='knob-desc' id='desc-refr'>"
        "Hard minimum onset-to-onset time between two drops. At 200 ms, rapid bursts up to 5 drops/sec are tracked without merging."
        "</p></div>"
        "</div></fieldset>"
        "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:0.5rem'>"
        "<span class='kpi-sub'>Changes apply automatically and persist across reboots.</span>"
        "<button type='button' class='btn' id='btn-reset'>Reset Tuning to Defaults</button>"
        "</div>"
        "</form></div></details></section>",
        (unsigned)_sampler.refractoryMs(),
        (unsigned)(_sampler.refractoryMs() > 1000 ? 1000 : _sampler.refractoryMs()),
        (unsigned)_sampler.refractoryMs());
    page += buf;

    // --- Collapsible Pattern Baseline & Targeted Event Logger Drawer -------
    page += "<details class='tuning-drawer pat-drawer' id='pattern-drawer'>"
            "<summary>Pattern Baseline &amp; Targeted Event Logger</summary>"
            "<div class='tuning-body'>"
            "<div class='cal-banner' style='margin-block-end:0.55rem'>"
            "<div class='cal-metrics'>"
            "<span>Passive Baseline: <strong id='bl-rate'>-- drops/hr</strong> (<strong id='bl-ml-rate'>-- mL/hr</strong>)</span>"
            "<span>Mean Burst: <strong id='bl-burst-sz'>-- drops</strong> (<strong id='bl-burst-ml'>-- mL</strong>)</span>"
            "<span>Burst Gap: <strong id='bl-burst-gap'>-- min</strong></span>"
            "</div>"
            "<button type='button' class='btn' id='btn-bl-refresh'>Refresh Baseline</button>"
            "</div>"

            "<div tabindex='0' role='region' aria-labelledby='bl-cap' class='table-wrapper'>"
            "<table>"
            "<caption id='bl-cap'>Always-On Passive Baseline by Time Window (Drops, Volume &amp; Bursts)</caption>"
            "<thead><tr>"
            "<th>Window</th><th>Drops/hr</th><th>Vol (mL/hr)</th><th>Bursts/hr</th><th>Avg Burst</th><th>vs. Baseline</th>"
            "</tr></thead>"
            "<tbody id='tb-baseline'>"
            "<tr><td colspan='6'>Loading baseline from /drops.csv...</td></tr>"
            "</tbody></table></div>"

            "<fieldset class='knob-group' style='margin-block-top:0.65rem;margin-block-end:0.55rem'>"
            "<legend>Targeted Isolation-Test &amp; Event Logger (/events.csv)</legend>"
            "<div class='cal-row' style='margin-block-top:0.2rem'>"
            "<div class='cal-field' style='flex:1;min-width:170px'>"
            "<label for='in-ev-label'>Event / Isolation Test Label</label>"
            "<input type='text' id='in-ev-label' class='ev-text' list='ev-presets' placeholder='e.g. 301호 Main Water OFF'>"
            "<datalist id='ev-presets'>"
            "<option value='301호 Main Water Valve OFF'>"
            "<option value='301호 Heating (난방) OFF'>"
            "<option value='301호 Water Purifier (정수기) OFF'>"
            "<option value='301호 Washing Machine / Laundry'>"
            "<option value='301호 Master Bath (욕실2) Shower'>"
            "<option value='Wall Orangina Bottle Emptied'>"
            "<option value='Rain / Wet Weather'>"
            "</datalist>"
            "</div>"
            "<div class='cal-field' style='width:9rem'>"
            "<label for='in-ev-note'>Note / Wall Bottle mL</label>"
            "<input type='text' id='in-ev-note' class='ev-text' placeholder='e.g. 40 mL'>"
            "</div>"
            "<button type='button' class='btn' id='btn-ev-start'>Start Test Window</button>"
            "<button type='button' class='btn' id='btn-ev-stop' disabled>Stop Window</button>"
            "<button type='button' class='btn' id='btn-ev-instant'>Log Instant Note</button>"
            "<span id='ev-active-pill' class='pill' style='display:none'></span>"
            "</div>"
            "<div tabindex='0' role='region' aria-labelledby='ev-cap' class='table-wrapper'>"
            "<table>"
            "<caption id='ev-cap'>Logged Isolation Tests &amp; Events vs. Passive Baseline</caption>"
            "<thead><tr>"
            "<th>Event / Test</th><th>Window</th><th>Dur</th><th>Drops (mL)</th><th>Rate (mL/hr)</th><th>vs. Baseline</th>"
            "</tr></thead>"
            "<tbody id='tb-events'>"
            "<tr><td colspan='6'>No targeted events logged yet.</td></tr>"
            "</tbody></table></div>"
            "</fieldset>"

            "<div tabindex='0' role='region' aria-labelledby='burst-cap' class='table-wrapper'>"
            "<table>"
            "<caption id='burst-cap'>Auto-Detected Recent Bursts / Episodes (gap &gt; 60s)</caption>"
            "<thead><tr>"
            "<th>#</th><th>Start Time</th><th>Drops</th><th>Vol (mL)</th><th>Dur</th><th>Gap Since Prev</th><th>Avg Env</th>"
            "</tr></thead>"
            "<tbody id='tb-bursts'>"
            "<tr><td colspan='7'>Loading bursts...</td></tr>"
            "</tbody></table></div>"
            "</div></details>";

    // --- Collapsible Water Quantity & Scale Calibration Drawer -------------
    snprintf(buf, sizeof(buf),
        "<details class='tuning-drawer qty-drawer' id='qty-drawer'>"
        "<summary>Water Quantity &amp; Scale Calibration</summary>"
        "<div class='tuning-body'>"
        "<div class='chk-row'>"
        "<label class='chk-label' for='chk-qen'>"
        "<input type='checkbox' id='chk-qen' %s>"
        "Measure water quantity (log Volume_uL in CSV &amp; show mL KPIs)"
        "</label>"
        "<span class='pill' id='lbl-cal-summary'>%.1f &micro;L/drop (~%.1f drops/mL)</span>"
        "</div>",
        _sampler.qtyEnabled() ? "checked" : "",
        uldUl, dpm);
    page += buf;

    page += "<fieldset class='knob-group'>"
            "<legend>In-Place Scale Calibration (No Pouring / Decanting Required)</legend>"
            "<ol class='cal-steps'>"
            "<li><strong>Zero / Tare Scale:</strong> Place a small scale under your angled collection tub and zero it (or enter its current weight in <em>Before / Tare (g)</em> below).</li>"
            "<li><strong>Capture Drops:</strong> Click <strong>Start Calibration Run</strong>. Let real drops fall until the badge turns green (<strong>45+ drops = Ready to Weigh &#10003;</strong>, ~1.5% accuracy).</li>"
            "<li><strong>Enter Weight:</strong> Click <strong>Stop Run</strong>, read the scale in grams (<strong>1.00 g = 1.00 mL</strong>), and click <strong>Save Calibration</strong>.</li>"
            "</ol>"
            "<div class='cal-banner'>"
            "<div class='cal-metrics'>"
            "<span class='pill pill-armed' id='cal-badge'>Idle</span>"
            "<span>Drops: <strong id='cal-drops'>0</strong> / 45+</span>"
            "<span>Elapsed: <strong id='cal-elapsed'>0s</strong></span>"
            "<span>Avg EnvPeak: <strong id='cal-env'>--</strong></span>"
            "</div>"
            "<div style='display:flex;gap:0.45rem;flex-wrap:wrap'>"
            "<button type='button' class='btn' id='btn-cal-start'>Start Calibration Run</button>"
            "<button type='button' class='btn' id='btn-cal-stop' disabled>Stop Run</button>"
            "<button type='button' class='btn' id='btn-cal-reset'>Reset</button>"
            "</div></div>";

    snprintf(buf, sizeof(buf),
        "<div class='cal-row'>"
        "<div class='cal-field'>"
        "<label for='in-cal-drops'>Drops Counted (Piezo or Visual)</label>"
        "<input type='number' id='in-cal-drops' step='1' min='1' max='50000' placeholder='auto or manual'>"
        "</div>"
        "<div class='cal-field'>"
        "<label for='in-cal-tare'>Before / Tare (g)</label>"
        "<input type='number' id='in-cal-tare' step='0.01' min='0' max='5000' value='0.00'>"
        "</div>"
        "<div class='cal-field'>"
        "<label for='in-cal-grams'>After Weight (g = mL)</label>"
        "<input type='number' id='in-cal-grams' step='0.01' min='0.05' max='5000' placeholder='e.g. 2.25'>"
        "</div>"
        "<button type='button' class='btn' id='btn-cal-apply'>Save Calibration</button>"
        "<div class='cal-field' style='margin-inline-start:auto'>"
        "<label for='in-manual-ul'>Direct Override (&micro;L/drop)</label>"
        "<div style='display:flex;gap:0.4rem'>"
        "<input type='number' id='in-manual-ul' step='0.5' min='5' max='500' value='%.1f'>"
        "<button type='button' class='btn' id='btn-manual-ul'>Set &micro;L</button>"
        "</div></div></div>"
        "<div style='margin-block-start:0.55rem;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap'>"
        "<span class='kpi-sub'>Tip: You can enter any drop count (visual count of 1 burst, or a multi-hour catch) + net grams.</span>"
        "<span id='cal-msg' class='save-status' aria-live='polite'></span>"
        "</div></fieldset></div></details></main>",
        uldUl);
    page += buf;

    // --- Client-side interactive script (no inline handlers) ---------------
    page += R"rawliteral(
<script>
(() => {
  const drawer = document.getElementById('tuning-drawer');
  const qtyDrawer = document.getElementById('qty-drawer');
  const patDrawer = document.getElementById('pattern-drawer');
  const cv = document.getElementById('scope-cv');
  const ctx = cv.getContext('2d');
  const form = document.getElementById('tune-form');
  const saveBadge = document.getElementById('save-badge');
  const thrLegend = document.getElementById('thr-legend');

  let paused = false;
  let activeKnob = null;
  let curUld = 50.0;
  let lastStream = {
    env: [],
    st: [],
    base: 0,
    noise: 0,
    thr: 15,
    sps: 18000
  };
  let ymax = 64;

  // Lock scope threshold dragging unless "Detection Tuning Knobs" is open
  function syncScopeLock() {
    const unlocked = drawer.open;
    cv.classList.toggle('unlocked', unlocked);
    if (thrLegend) {
      thrLegend.textContent = unlocked ? 'Thr (drag)' : 'Thr (locked)';
    }
  }

  if (location.hash === '#tuning' || sessionStorage.getItem('dm_tuning_open') === '1') {
    drawer.open = true;
  }
  syncScopeLock();
  drawer.addEventListener('toggle', () => {
    sessionStorage.setItem('dm_tuning_open', drawer.open ? '1' : '0');
    syncScopeLock();
  });

  if (sessionStorage.getItem('dm_qty_open') === '1') {
    qtyDrawer.open = true;
  }
  qtyDrawer.addEventListener('toggle', () => {
    sessionStorage.setItem('dm_qty_open', qtyDrawer.open ? '1' : '0');
  });

  if (sessionStorage.getItem('dm_pat_open') === '1') {
    patDrawer.open = true;
  }
  patDrawer.addEventListener('toggle', () => {
    sessionStorage.setItem('dm_pat_open', patDrawer.open ? '1' : '0');
    if (patDrawer.open) refreshPatternAnalysis();
  });

  // Water Quantity & Scale Calibration controls
  const chkQen = document.getElementById('chk-qen');
  const volGrid = document.getElementById('vol-kpi-grid');
  const calMsg = document.getElementById('cal-msg');
  function flashCalMsg(txt) {
    calMsg.textContent = txt;
    setTimeout(() => { if (calMsg.textContent === txt) calMsg.textContent = ''; }, 3000);
  }

  chkQen.addEventListener('change', async () => {
    const en = chkQen.checked ? 1 : 0;
    volGrid.style.display = en ? 'grid' : 'none';
    await fetch(`/calrun?action=toggle&qen=${en}`);
    flashCalMsg(en ? 'Quantity tracking enabled ✓' : 'Quantity tracking hidden');
    pollStatus();
  });

  document.getElementById('btn-cal-start').addEventListener('click', async () => {
    await fetch('/calrun?action=start');
    flashCalMsg('Calibration run started — let 45+ drops fall');
    pollStatus();
  });

  document.getElementById('btn-cal-stop').addEventListener('click', async () => {
    await fetch('/calrun?action=stop');
    flashCalMsg('Run stopped — enter scale weight (g) below');
    pollStatus();
  });

  document.getElementById('btn-cal-reset').addEventListener('click', async () => {
    await fetch('/calrun?action=reset');
    flashCalMsg('Counter reset');
    pollStatus();
  });

  document.getElementById('btn-cal-apply').addEventListener('click', async () => {
    const tare = parseFloat(document.getElementById('in-cal-tare').value) || 0;
    const after = parseFloat(document.getElementById('in-cal-grams').value);
    const manDrops = parseInt(document.getElementById('in-cal-drops').value, 10) || 0;
    if (isNaN(after) || after <= tare) {
      alert('Please enter an After Weight (in grams) greater than the Tare weight.');
      return;
    }
    const netGrams = (after - tare).toFixed(3);
    const qDrops = manDrops > 0 ? `&drops=${manDrops}` : '';
    const r = await (await fetch(`/calrun?action=apply&grams=${netGrams}${qDrops}`)).json();
    if (r.ok) {
      chkQen.checked = true;
      volGrid.style.display = 'grid';
      document.getElementById('in-manual-ul').value = Number(r.ul_per_drop).toFixed(1);
      flashCalMsg(`Saved ${Number(r.ul_per_drop).toFixed(1)} µL/drop ✓`);
      pollStatus();
      if (patDrawer.open) refreshPatternAnalysis();
    } else {
      alert(r.err || 'Enter a drop count in "Drops Counted" (or run a calibration capture first).');
    }
  });

  document.getElementById('btn-manual-ul').addEventListener('click', async () => {
    const ul = parseFloat(document.getElementById('in-manual-ul').value);
    if (isNaN(ul) || ul < 5 || ul > 500) {
      alert('Enter a drop volume between 5 and 500 µL.');
      return;
    }
    const r = await (await fetch(`/calrun?action=set_ul&ul=${ul.toFixed(1)}`)).json();
    if (r.ok) {
      chkQen.checked = true;
      volGrid.style.display = 'grid';
      flashCalMsg(`Set to ${Number(r.ul_per_drop).toFixed(1)} µL/drop ✓`);
      pollStatus();
    }
  });

  // Synchronize range sliders and number inputs + highlight active parameter on graph
  const pairs = [
    ['rng-margin', 'in-margin', 'lbl-margin'],
    ['rng-nmult',  'in-nmult',  'lbl-nmult'],
    ['rng-cap',    'in-cap',    'lbl-cap'],
    ['rng-rel',    'in-rel',    'lbl-rel'],
    ['rng-refr',   'in-refr',   'lbl-refr']
  ];
  let applyTimer = null;
  function scheduleApply() {
    clearTimeout(applyTimer);
    saveBadge.textContent = 'Applying...';
    applyTimer = setTimeout(async () => {
      if (!form.checkValidity()) return;
      const q = new URLSearchParams(new FormData(form));
      q.append('ajax', '1');
      try {
        const r = await fetch('/set?' + q.toString());
        if (r.ok) {
          saveBadge.textContent = 'Saved ✓';
          setTimeout(() => { if (saveBadge.textContent === 'Saved ✓') saveBadge.textContent = ''; }, 2000);
        }
      } catch (e) {
        saveBadge.textContent = 'Save error';
      }
    }, 280);
  }

  for (const [rId, nId, lId] of pairs) {
    const rng = document.getElementById(rId);
    const num = document.getElementById(nId);
    const lbl = document.getElementById(lId);
    rng.addEventListener('input', () => {
      num.value = rng.value;
      lbl.textContent = rng.value;
      drawScope();
      scheduleApply();
    });
    num.addEventListener('input', () => {
      num.setCustomValidity('');
      if (num.value !== '') {
        rng.value = Math.min(Number(rng.max), Math.max(Number(rng.min), Number(num.value)));
        lbl.textContent = num.value;
        drawScope();
        scheduleApply();
      }
    });
  }

  document.querySelectorAll('.knob-card').forEach(card => {
    const k = card.getAttribute('data-focus-knob');
    card.addEventListener('pointerenter', () => { activeKnob = k; drawScope(); });
    card.addEventListener('pointerleave', () => { if (activeKnob === k) { activeKnob = null; drawScope(); } });
    card.addEventListener('focusin', () => { activeKnob = k; drawScope(); });
    card.addEventListener('focusout', () => { if (activeKnob === k) { activeKnob = null; drawScope(); } });
  });

  form.addEventListener('submit', e => {
    e.preventDefault();
    scheduleApply();
  });

  document.getElementById('btn-pause').addEventListener('click', function() {
    paused = !paused;
    this.textContent = paused ? 'Unfreeze Scope' : 'Freeze Scope';
  });

  // Pause Data Gathering (keeps Live Scope running while ignoring handling bumps)
  async function toggleLogPause(forceAction) {
    const act = forceAction || 'toggle';
    await fetch(`/pause?action=${act}`);
    pollStatus();
  }
  document.getElementById('btn-pause-log').addEventListener('click', () => toggleLogPause('toggle'));
  document.getElementById('btn-resume-banner').addEventListener('click', () => toggleLogPause('resume'));

  async function trimRecentDrops(mins) {
    if (!confirm(`Remove any drops recorded in the last ${mins} minutes (e.g. accidental handling bumps)?`)) return;
    const msg = document.getElementById('rec-msg');
    msg.textContent = `Trimming last ${mins}m...`;
    try {
      const j = await (await fetch(`/pause?action=trim&mins=${mins}`)).json();
      msg.textContent = `Removed ${j.removed || 0} drops from last ${mins}m ✓`;
      setTimeout(() => { if (msg.textContent.startsWith('Removed')) msg.textContent = ''; }, 4000);
      pollStatus();
      if (patDrawer.open) refreshPatternAnalysis();
    } catch (e) {
      msg.textContent = 'Trim failed';
    }
  }
  document.getElementById('btn-trim-5m').addEventListener('click', () => trimRecentDrops(5));
  document.getElementById('btn-trim-15m').addEventListener('click', () => trimRecentDrops(15));

  document.getElementById('btn-reset').addEventListener('click', async () => {
    await fetch('/set?reset=1&ajax=1');
    location.reload();
  });

  document.getElementById('btn-clear').addEventListener('click', async () => {
    if (!confirm('Wipe the entire drop log and reset today\'s histogram?')) return;
    await fetch('/clearlog');
    pollStatus();
    if (patDrawer.open) refreshPatternAnalysis();
  });

  document.getElementById('btn-export').addEventListener('click', async function() {
    const sel = document.getElementById('sel-export').value;
    const msg = document.getElementById('rec-msg');
    if (sel === 'trim_5') {
      await trimRecentDrops(5);
      return;
    }
    if (sel === 'trim_15') {
      await trimRecentDrops(15);
      return;
    }
    if (sel === 'rec_wav') {
      this.disabled = true;
      msg.textContent = 'Saving 60s WAV...';
      try {
        const j = await (await fetch('/record')).json();
        if (j.ok) {
          msg.textContent = `Saved ${j.seconds}s WAV`;
          location.href = '/rec.wav';
        } else {
          msg.textContent = 'Err: ' + (j.err || 'fail');
        }
      } catch (e) {
        msg.textContent = 'Request failed';
      }
      this.disabled = false;
    } else {
      location.href = sel;
    }
  });

  // --- Pattern Baseline & Targeted Event Logger Engine ---
  function escHtml(s) {
    return String(s || '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  }

  async function refreshPatternAnalysis() {
    try {
      const [csvTxt, evTxt] = await Promise.all([
        fetch('/drops.csv').then(r => r.ok ? r.text() : ''),
        fetch('/events.csv').then(r => r.ok ? r.text() : '')
      ]);
      const drops = [];
      for (const raw of csvTxt.split('\n')) {
        const line = raw.trim();
        if (!line || line.startsWith('#') || line.startsWith('ISO')) continue;
        const p = line.split(',');
        if (p.length < 3) continue;
        const ep = parseInt(p[1], 10);
        if (!ep || ep < 1700000000) continue;
        const pk = parseInt(p[2], 10) || 0;
        const env = p.length > 3 ? (parseInt(p[3], 10) || 0) : 0;
        const rawVol = p.length > 4 ? (parseInt(p[4], 10) || 0) : 0;
        const volUl = rawVol > 0 ? rawVol : curUld;
        drops.push({ iso: p[0], ep, pk, env, volUl });
      }

      const nowEp = drops.length ? Math.max(Math.floor(Date.now() / 1000), drops[drops.length - 1].ep) : Math.floor(Date.now() / 1000);
      const firstEp = drops.length ? drops[0].ep : nowEp;
      const totalSpanHr = Math.max(0.1, (nowEp - firstEp) / 3600);

      // Group into bursts (gap > 60s starts a new burst)
      const bursts = [];
      for (const d of drops) {
        if (!bursts.length || (d.ep - bursts[bursts.length - 1].endEp) > 60) {
          bursts.push({
            startIso: d.iso,
            startEp: d.ep,
            endEp: d.ep,
            drops: 1,
            volUl: d.volUl,
            envSum: d.env
          });
        } else {
          const b = bursts[bursts.length - 1];
          b.endEp = d.ep;
          b.drops++;
          b.volUl += d.volUl;
          b.envSum += d.env;
        }
      }

      const totalVolMl = drops.reduce((a, d) => a + d.volUl, 0) / 1000.0;
      const baseDropsHr = drops.length / totalSpanHr;
      const baseMlHr = totalVolMl / totalSpanHr;
      // Separate main bursts (>=3 drops) from 1-2 drop trailing stragglers for accurate burst sizing
      const mainBursts = bursts.filter(b => b.drops >= 3);
      const refBursts = mainBursts.length ? mainBursts : bursts;
      const refBurstDrops = refBursts.reduce((a, b) => a + b.drops, 0);
      const refBurstMl = refBursts.reduce((a, b) => a + b.volUl, 0) / 1000.0;
      const meanBurstDrops = refBursts.length ? (refBurstDrops / refBursts.length) : 0;
      const meanBurstMl = refBursts.length ? (refBurstMl / refBursts.length) : 0;
      let gapSumMin = 0, gapCnt = 0;
      for (let i = 1; i < refBursts.length; i++) {
        const gMin = (refBursts[i].startEp - refBursts[i - 1].startEp) / 60.0;
        if (gMin < 180) { gapSumMin += gMin; gapCnt++; }
      }
      const meanGapMin = gapCnt > 0 ? (gapSumMin / gapCnt) : 0;

      document.getElementById('bl-rate').textContent = `${baseDropsHr.toFixed(1)} drops/hr`;
      document.getElementById('bl-ml-rate').textContent = `${baseMlHr.toFixed(2)} mL/hr`;
      document.getElementById('bl-burst-sz').textContent = `${meanBurstDrops.toFixed(1)} drops`;
      document.getElementById('bl-burst-ml').textContent = `${meanBurstMl.toFixed(2)} mL`;
      document.getElementById('bl-burst-gap').textContent = gapCnt > 0 ? `${meanGapMin.toFixed(1)} min` : '--';

      function hourOf(iso) {
        return parseInt(iso.slice(11, 13), 10) || 0;
      }
      function isWeekend(ep) {
        const day = new Date(ep * 1000).getDay();
        return day === 0 || day === 6;
      }

      // Compute exact logged hours in each window by walking [firstEp, nowEp] in 60s steps
      let obsDaySec = 0, obsNightSec = 0, obsWkdaySec = 0, obsWkndSec = 0;
      for (let t = firstEp; t < nowEp; t += 60) {
        const dt = new Date(t * 1000);
        const hr = dt.getHours();
        const dy = dt.getDay();
        if (hr >= 7 && hr < 23) obsDaySec += 60;
        else if (hr >= 0 && hr < 7) obsNightSec += 60;
        if (dy === 0 || dy === 6) obsWkndSec += 60;
        else obsWkdaySec += 60;
      }

      function summarizeWindow(label, filterFn, spanHr) {
        if (spanHr < 0.08) {
          return `<tr><th>${label}</th><td colspan='5' style='color:#8b9bb4;text-align:left'>Not logged yet (0.0h observed in this window)</td></tr>`;
        }
        const sub = drops.filter(filterFn);
        const subBursts = refBursts.filter(b => filterFn({ ep: b.startEp, iso: b.startIso }));
        const dHr = sub.length / spanHr;
        const mlHr = (sub.reduce((a, d) => a + d.volUl, 0) / 1000.0) / spanHr;
        const bHr = subBursts.length / spanHr;
        const subBurstDrops = subBursts.reduce((a, b) => a + b.drops, 0);
        const avgB = subBursts.length ? `${(subBurstDrops / subBursts.length).toFixed(1)}d` : '--';
        let vs = '--';
        if (baseDropsHr > 0.1) {
          const ratio = dHr / baseDropsHr;
          const pct = Math.round((ratio - 1) * 100);
          vs = `${pct >= 0 ? '+' : ''}${pct}% (${ratio.toFixed(2)}x)`;
        }
        return `<tr><th>${label}</th><td>${dHr.toFixed(1)}</td><td>${mlHr.toFixed(2)} mL</td><td>${bHr.toFixed(2)}</td><td>${avgB}</td><td>${vs}</td></tr>`;
      }

      let blRows = '';
      blRows += summarizeWindow('Last 1 Hour', d => d.ep >= nowEp - 3600, Math.min(1.0, totalSpanHr));
      blRows += summarizeWindow('Last 6 Hours', d => d.ep >= nowEp - 21600, Math.min(6.0, totalSpanHr));
      blRows += summarizeWindow('Daytime (07–23h)', d => { const h = hourOf(d.iso); return h >= 7 && h < 23; }, obsDaySec / 3600.0);
      blRows += summarizeWindow('Deep Night (00–07h)', d => { const h = hourOf(d.iso); return h >= 0 && h < 7; }, obsNightSec / 3600.0);
      blRows += summarizeWindow('Weekdays (Mon–Fri)', d => !isWeekend(d.ep), obsWkdaySec / 3600.0);
      blRows += summarizeWindow('Weekend (Sat–Sun)', d => isWeekend(d.ep), obsWkndSec / 3600.0);
      document.getElementById('tb-baseline').innerHTML = blRows;

      // Render recent bursts (newest first, up to 25)
      let bHtml = '';
      const startIdx = Math.max(0, bursts.length - 25);
      for (let i = bursts.length - 1; i >= startIdx; i--) {
        const b = bursts[i];
        const dur = b.endEp - b.startEp;
        const gapMin = i > 0 ? ((b.startEp - bursts[i - 1].startEp) / 60.0).toFixed(1) + 'm' : '--';
        const avgEnv = (b.envSum / b.drops).toFixed(1);
        bHtml += `<tr><th>#${i + 1}</th><td>${escHtml(b.startIso.slice(5, 19).replace('T', ' '))}</td><td>${b.drops}</td><td>${(b.volUl / 1000).toFixed(2)} mL</td><td>${dur}s</td><td>${gapMin}</td><td>${avgEnv}</td></tr>`;
      }
      document.getElementById('tb-bursts').innerHTML = bHtml || "<tr><td colspan='7'>No bursts recorded yet.</td></tr>";

      // Render Targeted Events & Isolation Tests
      const evRows = [];
      for (const raw of evTxt.split('\n')) {
        const line = raw.trim();
        if (!line || line.startsWith('StartISO')) continue;
        const p = line.split(',');
        if (p.length < 4) continue;
        evRows.push({
          iso: p[0],
          sEp: parseInt(p[1], 10) || 0,
          eEp: parseInt(p[2], 10) || 0,
          label: p[3],
          note: p.slice(4).join(',')
        });
      }
      let evHtml = '';
      for (let i = evRows.length - 1; i >= 0; i--) {
        const ev = evRows[i];
        const isPoint = (ev.eEp <= ev.sEp + 5);
        const durSec = Math.max(1, ev.eEp - ev.sEp);
        const wDrops = drops.filter(d => d.ep >= ev.sEp && d.ep <= (isPoint ? ev.sEp + 900 : ev.eEp));
        const wMl = wDrops.reduce((a, d) => a + d.volUl, 0) / 1000.0;
        const wHrs = isPoint ? 0.25 : (durSec / 3600.0);
        const wMlHr = wMl / wHrs;
        const wDropsHr = wDrops.length / wHrs;
        let vsBase = '--';
        if (baseDropsHr > 0.1) {
          const pct = Math.round(((wDropsHr - baseDropsHr) / baseDropsHr) * 100);
          vsBase = `${pct >= 0 ? '+' : ''}${pct}%`;
        }
        const winStr = escHtml(ev.iso.slice(5, 16).replace('T', ' '));
        const durStr = isPoint ? 'Note (+15m)' : formatDur(durSec);
        const lblStr = escHtml(ev.label) + (ev.note ? ` <span style='color:#8b9bb4'>(${escHtml(ev.note)})</span>` : '');
        evHtml += `<tr><th>${lblStr}</th><td>${winStr}</td><td>${durStr}</td><td>${wDrops.length} (${wMl.toFixed(2)} mL)</td><td>${wMlHr.toFixed(2)} mL/hr</td><td>${vsBase}</td></tr>`;
      }
      document.getElementById('tb-events').innerHTML = evHtml || "<tr><td colspan='6'>No targeted events logged yet. Use the form above to log a test or note.</td></tr>";
    } catch (e) {}
  }

  document.getElementById('btn-bl-refresh').addEventListener('click', refreshPatternAnalysis);

  document.getElementById('btn-ev-start').addEventListener('click', async () => {
    const lbl = document.getElementById('in-ev-label').value.trim();
    const note = document.getElementById('in-ev-note').value.trim();
    if (!lbl) { alert('Enter an Event / Test label first.'); return; }
    await fetch(`/eventlog?action=start&label=${encodeURIComponent(lbl)}&note=${encodeURIComponent(note)}`);
    pollStatus();
  });

  document.getElementById('btn-ev-stop').addEventListener('click', async () => {
    const note = document.getElementById('in-ev-note').value.trim();
    await fetch(`/eventlog?action=stop&note=${encodeURIComponent(note)}`);
    pollStatus();
    refreshPatternAnalysis();
  });

  document.getElementById('btn-ev-instant').addEventListener('click', async () => {
    const lbl = document.getElementById('in-ev-label').value.trim();
    const note = document.getElementById('in-ev-note').value.trim();
    if (!lbl) { alert('Enter an Event / Note label first.'); return; }
    await fetch(`/eventlog?action=instant&label=${encodeURIComponent(lbl)}&note=${encodeURIComponent(note)}`);
    document.getElementById('in-ev-note').value = '';
    refreshPatternAnalysis();
  });

  // Render the live scope + visual knob explanations
  function currentKnobs() {
    return {
      margin: Number(document.getElementById('in-margin').value) || 15,
      nmult:  Number(document.getElementById('in-nmult').value)  || 3,
      cap:    Number(document.getElementById('in-cap').value)    || 60,
      rel:    Number(document.getElementById('in-rel').value)    || 120,
      refr:   Number(document.getElementById('in-refr').value)   || 200
    };
  }

  function drawScope(hoverX = null) {
    const W = cv.width, H = cv.height;
    const padT = 22, padB = 22;
    const plotH = H - padT - padB;
    const data = lastStream.env;
    const st = lastStream.st;
    const k = currentKnobs();
    const base = lastStream.base || 0;
    const boost = Math.min((lastStream.noise || 0) * k.nmult, k.margin);
    const thr = base + k.margin + boost;

    let maxD = 0;
    for (let i = 0; i < data.length; i++) if (data[i] > maxD) maxD = data[i];
    const targetMax = Math.max(maxD * 1.25, thr * 1.8, 48);
    ymax = Math.ceil(targetMax / 8) * 8;

    const Y = v => padT + (1 - Math.min(Math.max(v, 0), ymax) / ymax) * plotH;

    ctx.fillStyle = '#0d1119';
    ctx.fillRect(0, 0, W, H);

    // Horizontal grid lines
    ctx.strokeStyle = '#192231';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let q = 1; q <= 3; q++) {
      const gy = padT + (plotH * q) / 4;
      ctx.moveTo(0, gy);
      ctx.lineTo(W, gy);
    }
    ctx.stroke();

    const yBase = Y(base);
    const yMargin = Y(base + k.margin);
    const yThr = Y(thr);

    // 1) Shaded Margin Zone (base -> base + margin)
    ctx.fillStyle = (activeKnob === 'margin') ? 'rgba(244, 63, 94, 0.20)' : 'rgba(244, 63, 94, 0.06)';
    ctx.fillRect(0, yMargin, W, Math.max(0, yBase - yMargin));

    // 2) Shaded Adaptive Noise Guard Zone (base + margin -> thr)
    if (boost > 0 || activeKnob === 'noisemult') {
      const demoBoost = boost > 0 ? boost : Math.round(k.margin * 0.45);
      const yDemoThr = Y(base + k.margin + demoBoost);
      ctx.fillStyle = (activeKnob === 'noisemult') ? 'rgba(245, 158, 11, 0.25)' : 'rgba(245, 158, 11, 0.12)';
      ctx.fillRect(0, yDemoThr, W, Math.max(0, yMargin - yDemoThr));
    }

    // 3) Live event state bands (Capture = green, Release/Refractory = amber)
    const n = data.length;
    if (n > 1) {
      const dx = W / (n - 1);
      for (let i = 0; i < n; i++) {
        if (st[i] === 1) {
          ctx.fillStyle = 'rgba(34, 197, 94, 0.22)';
          ctx.fillRect(i * dx, padT, Math.ceil(dx) + 1, plotH);
        } else if (st[i] === 2) {
          ctx.fillStyle = 'rgba(245, 158, 11, 0.16)';
          ctx.fillRect(i * dx, padT, Math.ceil(dx) + 1, plotH);
        }
      }

      // 4) Envelope waveform trace
      ctx.beginPath();
      for (let i = 0; i < n; i++) {
        const x = i * dx, y = Y(data[i]);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 1.6;
      ctx.stroke();

      ctx.lineTo(W, H - padB);
      ctx.lineTo(0, H - padB);
      ctx.closePath();
      ctx.fillStyle = 'rgba(56, 189, 248, 0.13)';
      ctx.fill();
    }

    // 5) Baseline dashed line
    ctx.strokeStyle = '#64748b';
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(0, yBase);
    ctx.lineTo(W, yBase);
    ctx.stroke();
    ctx.setLineDash([]);

    // 6) Threshold line (draggable)
    ctx.strokeStyle = '#f43f5e';
    ctx.lineWidth = (activeKnob === 'margin') ? 2.2 : 1.5;
    ctx.beginPath();
    ctx.moveTo(0, yThr);
    ctx.lineTo(W, yThr);
    ctx.stroke();
    ctx.lineWidth = 1;

    // Labels
    ctx.font = '11px ui-monospace, monospace';
    ctx.fillStyle = '#f43f5e';
    ctx.fillText(`THR ${thr} (base ${base} + margin ${k.margin}${boost ? ' + guard ' + boost : ''})`, 8, Math.max(14, yThr - 5));
    ctx.fillStyle = '#8b9bb4';
    ctx.fillText(`scale 0..${ymax}`, W - 95, 14);
    ctx.fillText('-5.2s', 8, H - 6);
    ctx.fillText('now', W - 30, H - 6);

    // 7) Interactive visual diagram overlay when hovering/focusing timing knobs
    if (activeKnob === 'captwin' || activeKnob === 'release' || activeKnob === 'refract') {
      const boxX = W * 0.18, boxW = W * 0.64;
      const totalMs = Math.max(k.cap + k.rel + 80, k.refr + 80, 350);
      const msToPx = ms => (ms / totalMs) * boxW;
      const capW = msToPx(k.cap);
      const relW = msToPx(k.rel);
      const refrW = msToPx(k.refr);

      ctx.fillStyle = 'rgba(11, 14, 20, 0.86)';
      ctx.fillRect(boxX - 12, padT + 8, boxW + 24, plotH - 16);
      ctx.strokeStyle = '#263042';
      ctx.strokeRect(boxX - 12, padT + 8, boxW + 24, plotH - 16);

      // Capture Window block
      ctx.fillStyle = (activeKnob === 'captwin') ? 'rgba(34, 197, 94, 0.38)' : 'rgba(34, 197, 94, 0.18)';
      ctx.fillRect(boxX, padT + 26, capW, plotH - 58);
      ctx.strokeStyle = '#22c55e';
      ctx.strokeRect(boxX, padT + 26, capW, plotH - 58);

      // Release Holdoff block
      ctx.fillStyle = (activeKnob === 'release') ? 'rgba(245, 158, 11, 0.38)' : 'rgba(245, 158, 11, 0.18)';
      ctx.fillRect(boxX + capW, padT + 26, relW, plotH - 58);
      ctx.strokeStyle = '#f59e0b';
      ctx.strokeRect(boxX + capW, padT + 26, relW, plotH - 58);

      // Synthetic drop envelope curve inside diagram
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 2;
      ctx.beginPath();
      const baseD = padT + plotH - 38;
      ctx.moveTo(boxX - 8, baseD);
      ctx.lineTo(boxX, baseD - 18);
      ctx.lineTo(boxX + capW * 0.3, padT + 38);
      ctx.lineTo(boxX + capW * 0.75, baseD - 28);
      ctx.lineTo(boxX + capW + relW * 0.25, baseD - 6);
      ctx.lineTo(boxX + boxW, baseD);
      ctx.stroke();
      ctx.lineWidth = 1;

      // Refractory span arrow at bottom of diagram
      const ry = padT + plotH - 20;
      ctx.strokeStyle = (activeKnob === 'refract') ? '#c084fc' : '#8b9bb4';
      ctx.beginPath();
      ctx.moveTo(boxX, ry);
      ctx.lineTo(boxX + refrW, ry);
      ctx.moveTo(boxX, ry - 4);
      ctx.lineTo(boxX, ry + 4);
      ctx.moveTo(boxX + refrW, ry - 4);
      ctx.lineTo(boxX + refrW, ry + 4);
      ctx.stroke();

      ctx.fillStyle = '#edf2f7';
      ctx.fillText(`Capture (${k.cap}ms)`, boxX + 4, padT + 21);
      ctx.fillStyle = '#fbbf24';
      ctx.fillText(`Quiet Release (${k.rel}ms)`, boxX + capW + 4, padT + 21);
      ctx.fillStyle = '#c084fc';
      ctx.fillText(`Min Drop Spacing / Refractory: ${k.refr}ms`, boxX + 4, ry - 5);
    }

    // Hover readout
    if (hoverX !== null && n > 1 && !activeKnob) {
      const idx = Math.max(0, Math.min(n - 1, Math.round((hoverX / W) * (n - 1))));
      const tAgo = -((n - 1 - idx) * 0.01).toFixed(2);
      ctx.strokeStyle = '#475569';
      ctx.beginPath();
      ctx.moveTo(hoverX, padT);
      ctx.lineTo(hoverX, H - padB);
      ctx.stroke();
      ctx.fillStyle = '#edf2f7';
      ctx.fillText(`t=${tAgo}s  env=${data[idx]}`, Math.min(W - 130, hoverX + 6), padT + 14);
    }
  }

  // Drag the red threshold line directly on the canvas to adjust Margin
  // ONLY when the Detection Tuning Knobs drawer is open (prevents phone swipe accidents).
  let dragging = false;
  function yToMargin(py) {
    const padT = 22, padB = 22;
    const plotH = cv.height - padT - padB;
    const v = Math.round((1 - (py - padT) / plotH) * ymax);
    return Math.max(5, Math.min(500, v - (lastStream.base || 0)));
  }
  cv.addEventListener('pointerdown', e => {
    if (!drawer.open) return;
    dragging = true;
    cv.setPointerCapture(e.pointerId);
    activeKnob = 'margin';
    const r = cv.getBoundingClientRect();
    const py = (e.clientY - r.top) * (cv.height / r.height);
    const m = yToMargin(py);
    document.getElementById('in-margin').value = m;
    document.getElementById('rng-margin').value = Math.min(120, m);
    document.getElementById('lbl-margin').textContent = m;
    drawScope();
  });
  cv.addEventListener('pointermove', e => {
    const r = cv.getBoundingClientRect();
    const px = (e.clientX - r.left) * (cv.width / r.width);
    const py = (e.clientY - r.top) * (cv.height / r.height);
    if (dragging && drawer.open) {
      const m = yToMargin(py);
      document.getElementById('in-margin').value = m;
      document.getElementById('rng-margin').value = Math.min(120, m);
      document.getElementById('lbl-margin').textContent = m;
      drawScope();
    } else if (e.pointerType === 'mouse') {
      drawScope(px);
    }
  });
  cv.addEventListener('pointerup', () => {
    if (!dragging) return;
    dragging = false;
    activeKnob = null;
    drawScope();
    scheduleApply();
  });
  cv.addEventListener('pointerleave', () => {
    if (!dragging) drawScope(null);
  });

  // Update histogram SVG client-side from /status.json
  function renderHistogramClient(today, yday, nowIdx) {
    let mx = 1;
    for (let i = 0; i < 96; i++) {
      if (today[i] > mx) mx = today[i];
      if (yday[i] > mx) mx = yday[i];
    }
    const BASE = 118;
    let s = "<svg id='histo-svg' viewBox='0 0 302 126' role='img' aria-label='24-hour drop histogram' xmlns='http://www.w3.org/2000/svg' style='width:100%;height:auto;background:#0d1119;border-radius:6px;border:1px solid #263042;display:block'>";
    for (let q = 1; q < 4; q++) {
      const gy = 2 + Math.floor((116 * q) / 4);
      s += `<line x1='0' y1='${gy}' x2='302' y2='${gy}' stroke='#1a2230' stroke-width='1'/>`;
    }
    for (let i = 0; i < 96; i++) {
      const x = 3 + i * 3;
      if (yday[i]) {
        const h = Math.max(1, Math.floor((yday[i] * 114) / mx));
        s += `<rect x='${x}' y='${BASE - h}' width='2' height='${h}' fill='#2b384e'/>`;
      }
      if (today[i]) {
        const h = Math.max(1, Math.floor((today[i] * 114) / mx));
        const c = (i === nowIdx) ? '#38bdf8' : '#0284c7';
        s += `<rect x='${x}' y='${BASE - h}' width='2' height='${h}' fill='${c}'/>`;
      }
    }
    const nx = 3 + nowIdx * 3 + 1;
    s += `<line x1='${nx}' y1='2' x2='${nx}' y2='118' stroke='#f43f5e' stroke-width='1'/>`;
    for (let h = 0; h <= 4; h++) {
      const tx = (h === 4) ? 290 : (3 + h * 72);
      const hh = String(h * 6).padStart(2, '0');
      s += `<text x='${tx}' y='125' font-size='6.5' font-family='monospace' fill='#8b9bb4'>${hh}:00</text>`;
    }
    const trueMax = Math.max(...today, ...yday, 0);
    s += `<text x='298' y='10' text-anchor='end' font-size='6.5' font-family='monospace' fill='#8b9bb4'>max ${trueMax}</text></svg>`;
    document.getElementById('histo-box').innerHTML = s;
  }

  function formatAgo(sec) {
    if (sec < 0) sec = 0;
    if (sec < 60) return `${Math.floor(sec)}s ago`;
    if (sec < 3600) return `${Math.floor(sec / 60)}m ${Math.floor(sec % 60)}s ago`;
    if (sec < 86400) return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m ago`;
    return `${Math.floor(sec / 86400)}d ago`;
  }

  function formatDur(sec) {
    if (!sec || sec < 0) return '0s';
    if (sec < 60) return `${Math.floor(sec)}s`;
    if (sec < 3600) return `${Math.floor(sec / 60)}m ${Math.floor(sec % 60)}s`;
    return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m`;
  }

  async function pollStatus() {
    try {
      const j = await (await fetch('/status.json')).json();
      if (j.time) {
        const tpart = j.time.slice(11, 19);
        document.getElementById('hdr-time').textContent = `${tpart} ${j.time_synced ? '(NTP)' : '(unsynced)'}`;
      }
      const isLogPaused = !!j.paused;
      const stEl = document.getElementById('hdr-state');
      stEl.textContent = isLogPaused ? 'PAUSED' : (j.state || 'ARMED');
      stEl.className = 'pill ' + (isLogPaused ? 'pill-rel' : (j.state === 'CAPTURING' ? 'pill-cap' : (j.state === 'RELEASE' ? 'pill-rel' : 'pill-armed')));
      const pBtn = document.getElementById('btn-pause-log');
      if (pBtn) {
        pBtn.textContent = isLogPaused ? 'Resume Logging' : 'Pause Logging';
        pBtn.style.borderColor = isLogPaused ? '#22c55e' : '#f59e0b';
        pBtn.style.color = isLogPaused ? '#4ade80' : '#fbbf24';
      }
      const pBan = document.getElementById('pause-banner');
      if (pBan) pBan.style.display = isLogPaused ? 'flex' : 'none';
      document.getElementById('hdr-sps').textContent = `${((j.sps || 0) / 1000).toFixed(1)} kS/s`;
      document.getElementById('hdr-wifi').textContent = `Wi-Fi ${j.rssi} dBm`;

      document.getElementById('kpi-today').textContent = j.drops_today;
      document.getElementById('kpi-trig').textContent = j.triggers_total;
      document.getElementById('kpi-peak').textContent = j.last_peak;
      document.getElementById('kpi-envpeak').textContent = j.last_env_peak || 0;
      document.getElementById('kpi-env').textContent = j.envelope;
      document.getElementById('kpi-thr').textContent = j.threshold;
      document.getElementById('kpi-base').textContent = j.env_base || 0;
      document.getElementById('kpi-raw').textContent = j.raw;

      if (j.last_drop) document.getElementById('kpi-last-iso').textContent = j.last_drop;
      if (j.last_drop_epoch > 0 && j.epoch_us > 0) {
        document.getElementById('kpi-ago').textContent = formatAgo((j.epoch_us - j.last_drop_epoch) / 1e6);
      } else {
        document.getElementById('kpi-ago').textContent = '--';
      }

      // Water Quantity KPIs & Calibration state
      if (typeof j.qty_enabled === 'boolean') {
        chkQen.checked = j.qty_enabled;
        volGrid.style.display = j.qty_enabled ? 'grid' : 'none';
      }
      const uld = j.ul_per_drop || 50.0;
      curUld = uld;
      const dpm = uld > 0.1 ? (1000.0 / uld) : 20.0;
      document.getElementById('lbl-cal-summary').textContent = `${uld.toFixed(1)} µL/drop (~${dpm.toFixed(1)} drops/mL)`;
      document.getElementById('kpi-vol-1h').textContent = (j.vol_1h_ml || 0).toFixed(2);
      document.getElementById('kpi-vol-1h-sub').textContent = `${j.drops_1h || 0} drops in last 60m`;
      document.getElementById('kpi-vol-24h').textContent = (j.vol_24h_ml || 0).toFixed(2);
      document.getElementById('kpi-vol-24h-sub').textContent = `${j.drops_24h || 0} drops · Today: ${(j.vol_today_ml || 0).toFixed(2)} mL`;
      document.getElementById('kpi-vol-avg').textContent = (j.vol_24h_avg_ml || 0).toFixed(2);

      const trendEl = document.getElementById('kpi-vol-trend');
      if (j.vol_prev24h_ml && j.vol_prev24h_ml > 0.05) {
        const diffPct = Math.round(((j.vol_24h_ml - j.vol_prev24h_ml) / j.vol_prev24h_ml) * 100);
        const sign = diffPct > 0 ? '+' : '';
        const arrow = diffPct > 5 ? '▲ (growing)' : (diffPct < -5 ? '▼ (shrinking)' : '● (steady)');
        trendEl.textContent = `${sign}${diffPct}% vs prior 24h ${arrow}`;
      } else {
        const hrs = Math.max(0.1, (j.log_span_s || 0) / 3600).toFixed(1);
        trendEl.textContent = `Pace over ${hrs}h log · ${uld.toFixed(1)} µL/drop`;
      }

      // Active Isolation-Test state
      const evAct = !!j.ev_active;
      document.getElementById('btn-ev-start').disabled = evAct;
      document.getElementById('btn-ev-stop').disabled = !evAct;
      const evPill = document.getElementById('ev-active-pill');
      if (evAct) {
        evPill.style.display = 'inline-block';
        evPill.className = 'pill pill-cap';
        evPill.textContent = `Active: ${j.ev_label || 'Test'} (${formatDur(j.ev_elapsed_s || 0)})`;
      } else {
        evPill.style.display = 'none';
      }

      // Calibration run live banner
      const cDrops = j.cal_drops || 0;
      const cTarget = j.cal_target || 45;
      const cRun = !!j.cal_running;
      document.getElementById('cal-drops').textContent = cDrops;
      const calDropsIn = document.getElementById('in-cal-drops');
      if (calDropsIn) calDropsIn.placeholder = cDrops > 0 ? `auto (${cDrops})` : 'e.g. 16';
      document.getElementById('cal-elapsed').textContent = formatDur(j.cal_elapsed_s || 0);
      document.getElementById('cal-env').textContent = cDrops > 0 ? ((j.cal_env_sum || 0) / cDrops).toFixed(1) : '--';
      document.getElementById('btn-cal-start').disabled = cRun;
      document.getElementById('btn-cal-stop').disabled = !cRun;
      const cBadge = document.getElementById('cal-badge');
      if (cRun) {
        if (cDrops >= cTarget) {
          cBadge.textContent = `Ready to Weigh ✓ (${cDrops} drops)`;
          cBadge.className = 'pill pill-armed';
        } else {
          cBadge.textContent = `Collecting (${cDrops}/${cTarget})...`;
          cBadge.className = 'pill pill-rel';
        }
      } else if (cDrops > 0) {
        cBadge.textContent = cDrops >= cTarget ? `Stopped — Ready (${cDrops} drops)` : `Stopped (${cDrops} drops)`;
        cBadge.className = cDrops >= cTarget ? 'pill pill-armed' : 'pill pill-cap';
      } else {
        cBadge.textContent = 'Idle';
        cBadge.className = 'pill';
      }

      if (j.histogram && j.histogram.today && j.histogram.yesterday) {
        const ydSum = j.histogram.yesterday.reduce((a, b) => a + b, 0);
        document.getElementById('kpi-yday').textContent = ydSum;
        renderHistogramClient(j.histogram.today, j.histogram.yesterday, j.histogram.now_bucket || 0);
      }
    } catch (e) {}
  }

  async function pollStream() {
    if (paused) return;
    try {
      const j = await (await fetch('/stream.json')).json();
      lastStream = j;
      document.getElementById('kpi-raw').textContent = j.raw;
      document.getElementById('kpi-env').textContent = j.env.length ? j.env[j.env.length - 1] : 0;
      document.getElementById('kpi-thr').textContent = j.thr;
      document.getElementById('kpi-base').textContent = j.base;
      if (!dragging) drawScope();
    } catch (e) {}
  }

  setInterval(pollStatus, 3000);
  setInterval(pollStream, 450);
  pollStatus();
  pollStream();
  if (patDrawer.open) refreshPatternAnalysis();
})();
</script>
</body></html>
)rawliteral";

    _server.send(200, "text/html", page);
}

// ---------------------------------------------------------------------------
// /drops.csv — stream the log.
// ---------------------------------------------------------------------------
void WebExport::onCsv()
{
    fs::File rd = LittleFS.open(LOG_PATH, "r");
    if (!rd)
    {
        _server.send(500, "text/plain", "log unavailable");
        return;
    }

    _server.setContentLength(rd.size());
    _server.sendHeader("Content-Type", "text/csv");
    _server.sendHeader("Content-Disposition", "attachment; filename=\"drops.csv\"");
    _server.sendHeader("Connection", "close");
    _server.send(200, "text/csv", "");

    uint8_t buf[2048];
    while (rd.available())
    {
        const size_t n = rd.read(buf, sizeof(buf));
        _server.client().write(buf, n);
    }
    rd.close();
    _server.client().stop();
}

// ---------------------------------------------------------------------------
// /events.csv — stream the targeted event & isolation-test log.
// ---------------------------------------------------------------------------
void WebExport::onEventsCsv()
{
    if (!LittleFS.exists(EVENT_LOG_PATH))
    {
        _server.send(200, "text/csv", String(EVENT_LOG_HEADER) + "\n");
        return;
    }
    fs::File rd = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!rd)
    {
        _server.send(200, "text/csv", String(EVENT_LOG_HEADER) + "\n");
        return;
    }
    _server.setContentLength(rd.size());
    _server.sendHeader("Content-Type", "text/csv");
    _server.sendHeader("Content-Disposition", "attachment; filename=\"events.csv\"");
    _server.sendHeader("Connection", "close");
    _server.send(200, "text/csv", "");

    uint8_t buf[1024];
    while (rd.available())
    {
        const size_t n = rd.read(buf, sizeof(buf));
        _server.client().write(buf, n);
    }
    rd.close();
    _server.client().stop();
}

// ---------------------------------------------------------------------------
// /status.json — machine-readable snapshot (incl. both histogram arrays).
// ---------------------------------------------------------------------------
void WebExport::onStatus()
{
    struct tm tmv;
    const bool haveTm = TimeService::localTime(tmv);

    char timeStr[32] = "null";
    uint8_t nowBucket = 0;
    if (haveTm)
    {
        snprintf(timeStr, sizeof(timeStr),
                 "\"%04d-%02d-%02dT%02d:%02d:%02d\"",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        nowBucket = Histogram::indexFor(tmv);
    }

    char lastIso[48] = "null";
    if (_sampler.lastDropEpochUs() != 0)
    {
        char tmp[48];
        if (TimeService::formatISO(_sampler.lastDropEpochUs(), tmp, sizeof(tmp)))
            snprintf(lastIso, sizeof(lastIso), "\"%s\"", tmp);
    }

    const String ipStr = WiFi.localIP().toString();
    const uint32_t nowEpoch = (uint32_t)(TimeService::epochUs() / 1000000LL);
    const uint32_t cStart = _sampler.calStartEpoch();
    const uint32_t cStop = _sampler.calStopEpoch();
    const uint32_t cElapsed = _sampler.calRunning()
                                  ? ((nowEpoch >= cStart && cStart > 0) ? (nowEpoch - cStart) : 0)
                                  : ((cStop >= cStart && cStart > 0) ? (cStop - cStart) : 0);
    const uint32_t evElapsed = (_evActive && nowEpoch >= _evStartEpoch && _evStartEpoch > 0)
                                  ? (nowEpoch - _evStartEpoch) : 0;

    char head[1600];
    snprintf(head, sizeof(head),
        "{\"time\":%s,\"time_synced\":%s,\"epoch_us\":%lld,"
        "\"drops_today\":%lu,\"triggers_total\":%lu,"
        "\"last_drop\":%s,\"last_drop_epoch\":%lld,\"last_peak\":%u,\"last_env_peak\":%u,"
        "\"raw\":%u,\"envelope\":%u,\"env_base\":%u,\"env_noise\":%u,\"threshold\":%u,"
        "\"state\":\"%s\",\"paused\":%s,\"sps\":%u,"
        "\"margin\":%u,\"noise_mult\":%u,"
        "\"capture_ms\":%u,\"release_ms\":%u,\"refractory_ms\":%u,"
        "\"qty_enabled\":%s,\"ul_per_drop\":%.1f,\"cal_avg_env\":%.1f,"
        "\"vol_today_ml\":%.2f,\"vol_yday_ml\":%.2f,"
        "\"vol_1h_ml\":%.2f,\"drops_1h\":%lu,"
        "\"vol_24h_ml\":%.2f,\"drops_24h\":%lu,\"vol_prev24h_ml\":%.2f,"
        "\"vol_24h_avg_ml\":%.2f,\"log_span_s\":%lu,"
        "\"cal_running\":%s,\"cal_drops\":%lu,\"cal_env_sum\":%lu,\"cal_elapsed_s\":%lu,\"cal_target\":%u,"
        "\"ev_active\":%s,\"ev_label\":\"%s\",\"ev_elapsed_s\":%lu,"
        "\"wifi_up\":%s,\"rssi\":%d,\"ip\":\"%s\","
        "\"fs_free\":%u,\"fs_total\":%u,\"log_bytes\":%u,"
        "\"uptime_s\":%lu,\"loop_ms_ago\":%lu,"
        "\"histogram\":{\"bucket_minutes\":%d,\"now_bucket\":%u,\"today\":[",
        timeStr, TimeService::synced() ? "true" : "false",
        (long long)TimeService::epochUs(),
        (unsigned long)_histo.total(),
        (unsigned long)_sampler.triggerCount(),
        lastIso, (long long)_sampler.lastDropEpochUs(),
        (unsigned)_sampler.lastPeak(), (unsigned)_sampler.lastEnvPeak(),
        (unsigned)_sampler.rawADC(), (unsigned)_sampler.envelope(),
        (unsigned)_sampler.envBase(), (unsigned)_sampler.envNoise(),
        (unsigned)_sampler.threshold(),
        stateLabel(_sampler.detectorState()),
        _sampler.paused() ? "true" : "false",
        (unsigned)_sampler.samplesPerSecond(),
        (unsigned)_sampler.margin(), (unsigned)_sampler.noiseMult(),
        (unsigned)_sampler.captureWindowMs(), (unsigned)_sampler.releaseMs(),
        (unsigned)_sampler.refractoryMs(),
        _sampler.qtyEnabled() ? "true" : "false",
        (float)_sampler.ulPerDropTenths() / 10.0f,
        (float)_sampler.calAvgEnvTenths() / 10.0f,
        (float)_histo.totalVolTodayUl() / 1000.0f,
        (float)_histo.totalVolYdayUl() / 1000.0f,
        (float)_histo.lastHourVolUl(nowEpoch) / 1000.0f,
        (unsigned long)_histo.lastHourDrops(nowEpoch),
        (float)_histo.last24hVolUl(nowEpoch) / 1000.0f,
        (unsigned long)_histo.last24hDrops(nowEpoch),
        (float)_histo.prev24hVolUl(nowEpoch) / 1000.0f,
        (float)_histo.rolling24hAvgVolUl(nowEpoch) / 1000.0f,
        (unsigned long)_histo.logSpanSeconds(nowEpoch),
        _sampler.calRunning() ? "true" : "false",
        (unsigned long)_sampler.calDrops(),
        (unsigned long)_sampler.calEnvSum(),
        (unsigned long)cElapsed,
        (unsigned)CAL_TARGET_DROPS,
        _evActive ? "true" : "false",
        _evLabel,
        (unsigned long)evElapsed,
        WiFi.status() == WL_CONNECTED ? "true" : "false",
        (int)WiFi.RSSI(), ipStr.c_str(),
        (unsigned)(_log.totalBytes() - _log.usedBytes()),
        (unsigned)_log.totalBytes(), (unsigned)_log.fileSize(),
        (unsigned long)(millis() / 1000),
        (unsigned long)(millis() - gLoopMs.load()),
        BUCKET_MINUTES, (unsigned)nowBucket);

    String js;
    js.reserve(3600);
    js += head;
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        if (i) js += ',';
        js += String(_histo.bucket(i));
    }
    js += "],\"yesterday\":[";
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        if (i) js += ',';
        js += String(_histo.bucketYesterday(i));
    }
    js += "]}}";

    _server.send(200, "application/json", js);
}

// ---------------------------------------------------------------------------
// /clearlog — wipe the CSV + reset today's/yesterday's on-screen counts.
// ---------------------------------------------------------------------------
void WebExport::onClearLog()
{
    _sampler.discardEvents();   // drop pre-wipe events still in the ring
    _log.clearLog();
    LittleFS.remove(EVENT_LOG_PATH);
    _evActive = false;
    _evStartEpoch = 0;
    _evStartIso[0] = '\0';
    _evLabel[0] = '\0';
    _evNote[0] = '\0';
    _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                        _sampler.refractoryMs(), _sampler.releaseMs(),
                        _sampler.captureWindowMs(),
                        _sampler.qtyEnabled(), _sampler.ulPerDropTenths());
    struct tm tmv;
    if (TimeService::localTime(tmv))
        _histo.resetForDate(tmv);
    _server.sendHeader("Location", "/");
    _server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// /record — save the rolling WAV ring to /rec.wav (single file, overwritten).
// ---------------------------------------------------------------------------
void WebExport::onRecord()
{
    if (!_sampler.wavOK())
    {
        _server.send(500, "application/json", "{\"ok\":0,\"err\":\"recorder disabled\"}");
        return;
    }
    const uint32_t fill = _sampler.wavFill();
    const uint32_t rate = _sampler.samplesPerSecond() ? _sampler.samplesPerSecond() : 18000;

    fs::File f = LittleFS.open("/rec.wav", "w");
    if (!f)
    {
        _server.send(500, "application/json", "{\"ok\":0,\"err\":\"fs write failed\"}");
        return;
    }

    // WAV header: PCM, mono, 16-bit (ESP32 is little-endian — matches WAV).
    uint8_t hdr[44] = {0};
    const uint32_t chunkSize = 36 + fill * 2;
    const uint16_t audioFmt = 1, ch = 1, blockAlign = 2, bits = 16;
    const uint32_t fmtSize = 16, byteRate = rate * 2, dataSize = fill * 2;
    memcpy(hdr + 0, "RIFF", 4);    memcpy(hdr + 4, &chunkSize, 4);
    memcpy(hdr + 8, "WAVE", 4);    memcpy(hdr + 12, "fmt ", 4);
    memcpy(hdr + 16, &fmtSize, 4); memcpy(hdr + 20, &audioFmt, 2);
    memcpy(hdr + 22, &ch, 2);      memcpy(hdr + 24, &rate, 4);
    memcpy(hdr + 28, &byteRate, 4); memcpy(hdr + 32, &blockAlign, 2);
    memcpy(hdr + 34, &bits, 2);    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &dataSize, 4);
    f.write(hdr, 44);

    int16_t buf[4096];
    for (uint32_t i = 0; i < fill; i += 4096)
    {
        const uint16_t n = (fill - i) < 4096 ? (uint16_t)(fill - i) : 4096;
        for (uint16_t j = 0; j < n; ++j)
            buf[j] = _sampler.wavRingAt(i + j);
        f.write((const uint8_t *)buf, (size_t)n * 2);
    }
    f.close();

    char j[128];
    snprintf(j, sizeof(j), "{\"ok\":1,\"seconds\":%lu,\"bytes\":%lu,\"rate\":%lu}",
             (unsigned long)(fill / rate),
             (unsigned long)(fill * 2 + 44), (unsigned long)rate);
    _server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// /rec.wav — download the saved recording.
// ---------------------------------------------------------------------------
void WebExport::onRecWav()
{
    fs::File rd = LittleFS.open("/rec.wav", "r");
    if (!rd)
    {
        _server.send(404, "text/plain", "no recording yet - use /record first");
        return;
    }
    _server.setContentLength(rd.size());
    _server.sendHeader("Content-Type", "audio/wav");
    _server.sendHeader("Content-Disposition", "attachment; filename=\"drip.wav\"");
    _server.sendHeader("Connection", "close");
    _server.send(200, "audio/wav", "");

    uint8_t buf[2048];
    while (rd.available())
    {
        const size_t n = rd.read(buf, sizeof(buf));
        _server.client().write(buf, n);
    }
    rd.close();
    _server.client().stop();
}

// ---------------------------------------------------------------------------
// /stream.json — compact 512-point peak-held envelope + state history (~3 KB)
// ---------------------------------------------------------------------------
void WebExport::onStream()
{
    String js;
    js.reserve(4096);
    js += "{\"env\":[";

    const uint32_t cnt = _sampler.envHistCount();
    for (uint16_t i = 0; i < DropSampler::kEnvHist; ++i)
    {
        if (i)
            js += ',';
        const int32_t idx = (int32_t)cnt - (int32_t)DropSampler::kEnvHist + i;
        js += String(idx >= 0 ? _sampler.envHistAt((uint32_t)idx) : 0);
    }
    js += "],\"st\":[";
    for (uint16_t i = 0; i < DropSampler::kEnvHist; ++i)
    {
        if (i)
            js += ',';
        const int32_t idx = (int32_t)cnt - (int32_t)DropSampler::kEnvHist + i;
        js += String(idx >= 0 ? _sampler.stateHistAt((uint32_t)idx) : 0);
    }
    char tail[192];
    snprintf(tail, sizeof(tail),
             "],\"raw\":%u,\"thr\":%u,\"base\":%u,\"noise\":%u,\"sps\":%u}",
             (unsigned)_sampler.rawADC(),
             (unsigned)_sampler.threshold(),
             (unsigned)_sampler.envBase(),
             (unsigned)_sampler.envNoise(),
             (unsigned)_sampler.samplesPerSecond());
    js += tail;

    _server.send(200, "application/json", js);
}

// ---------------------------------------------------------------------------
// /set — apply + persist calibration values.
// ---------------------------------------------------------------------------
void WebExport::onSet()
{
    if (_server.hasArg("reset"))
    {
        Tuning::reset(_sampler);
        _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                            _sampler.refractoryMs(), _sampler.releaseMs(),
                            _sampler.captureWindowMs(),
                            _sampler.qtyEnabled(), _sampler.ulPerDropTenths());
        if (_server.hasArg("ajax"))
        {
            _server.send(200, "application/json", "{\"ok\":1}");
            return;
        }
        _server.sendHeader("Location", "/#tuning");
        _server.send(302, "text/plain", "");
        return;
    }

    if (_server.hasArg("margin"))
    {
        const long m = _server.arg("margin").toInt();
        if (m >= 5 && m <= 500)
            _sampler.setMargin((uint16_t)m);
    }
    if (_server.hasArg("noisemult"))
    {
        const long n = _server.arg("noisemult").toInt();
        if (n >= 0 && n <= 20)
            _sampler.setNoiseMult((uint8_t)n);
    }
    if (_server.hasArg("captwin"))
    {
        const long c = _server.arg("captwin").toInt();
        if (c >= 20 && c <= 300)
            _sampler.setCaptureWindowMs((uint16_t)c);
    }
    if (_server.hasArg("release"))
    {
        const long r = _server.arg("release").toInt();
        if (r >= 40 && r <= 1000)
            _sampler.setReleaseMs((uint16_t)r);
    }
    if (_server.hasArg("refract"))
    {
        const long r = _server.arg("refract").toInt();
        if (r >= 80 && r <= 2000)
            _sampler.setRefractoryMs((uint16_t)r);
    }

    Tuning::save(_sampler);
    _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                        _sampler.refractoryMs(), _sampler.releaseMs(),
                        _sampler.captureWindowMs(),
                        _sampler.qtyEnabled(), _sampler.ulPerDropTenths());

    if (_server.hasArg("ajax"))
    {
        _server.send(200, "application/json", "{\"ok\":1}");
        return;
    }
    _server.sendHeader("Location", "/#tuning");
    _server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// /calrun — start/stop/reset/apply water quantity & in-place scale calibration
// ---------------------------------------------------------------------------
void WebExport::onCalRun()
{
    const String action = _server.arg("action");
    const uint32_t nowEpoch = (uint32_t)(TimeService::epochUs() / 1000000LL);

    if (action == "toggle")
    {
        const bool en = (_server.arg("qen").toInt() != 0);
        _sampler.setQtyEnabled(en);
        Tuning::save(_sampler);
        _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                            _sampler.refractoryMs(), _sampler.releaseMs(),
                            _sampler.captureWindowMs(),
                            _sampler.qtyEnabled(), _sampler.ulPerDropTenths());
    }
    else if (action == "start")
    {
        _sampler.startCalRun(nowEpoch);
    }
    else if (action == "stop")
    {
        _sampler.stopCalRun(nowEpoch);
    }
    else if (action == "reset")
    {
        _sampler.resetCalRun();
    }
    else if (action == "apply")
    {
        _sampler.stopCalRun(nowEpoch);
        const float grams = _server.arg("grams").toFloat();
        uint32_t drops = _sampler.calDrops();
        if (_server.hasArg("drops") && _server.arg("drops").toInt() > 0)
            drops = (uint32_t)_server.arg("drops").toInt();

        if (grams <= 0.01f || drops == 0)
        {
            _server.send(400, "application/json",
                         "{\"ok\":0,\"err\":\"Record drops in a calibration run before applying scale weight.\"}");
            return;
        }
        // 1.00 g water = 1,000 uL = 10,000 tenths of uL
        long ulTenths = lroundf((grams * 10000.0f) / (float)drops);
        if (ulTenths < 50) ulTenths = 50;       // min 5.0 uL/drop
        if (ulTenths > 5000) ulTenths = 5000;   // max 500.0 uL/drop
        _sampler.setUlPerDropTenths((uint16_t)ulTenths);

        if (_sampler.calEnvSum() > 0 && _sampler.calDrops() > 0)
        {
            long envTenths = lroundf(((float)_sampler.calEnvSum() * 10.0f) / (float)_sampler.calDrops());
            if (envTenths >= 50 && envTenths <= 2000)
                _sampler.setCalAvgEnvTenths((uint16_t)envTenths);
        }
        _sampler.setQtyEnabled(true);
        Tuning::save(_sampler);
        _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                            _sampler.refractoryMs(), _sampler.releaseMs(),
                            _sampler.captureWindowMs(),
                            _sampler.qtyEnabled(), _sampler.ulPerDropTenths());

        struct tm tmv;
        if (TimeService::localTime(tmv))
            _log.rebuildToday(_histo, tmv, &_sampler);
    }
    else if (action == "set_ul")
    {
        const float ul = _server.arg("ul").toFloat();
        long ulTenths = lroundf(ul * 10.0f);
        if (ulTenths < 50) ulTenths = 50;
        if (ulTenths > 5000) ulTenths = 5000;
        _sampler.setUlPerDropTenths((uint16_t)ulTenths);
        _sampler.setQtyEnabled(true);
        Tuning::save(_sampler);
        _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                            _sampler.refractoryMs(), _sampler.releaseMs(),
                            _sampler.captureWindowMs(),
                            _sampler.qtyEnabled(), _sampler.ulPerDropTenths());

        struct tm tmv;
        if (TimeService::localTime(tmv))
            _log.rebuildToday(_histo, tmv, &_sampler);
    }

    char out[256];
    snprintf(out, sizeof(out),
             "{\"ok\":1,\"qty_enabled\":%s,\"ul_per_drop\":%.1f,\"cal_avg_env\":%.1f,"
             "\"cal_running\":%s,\"cal_drops\":%lu}",
             _sampler.qtyEnabled() ? "true" : "false",
             (float)_sampler.ulPerDropTenths() / 10.0f,
             (float)_sampler.calAvgEnvTenths() / 10.0f,
             _sampler.calRunning() ? "true" : "false",
             (unsigned long)_sampler.calDrops());
    _server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// /eventlog — start/stop/instant targeted event & isolation test logger
// ---------------------------------------------------------------------------
static String sanitizeCsvField(String s)
{
    s.replace(",", ";");
    s.replace("\"", "'");
    s.replace("\r", " ");
    s.replace("\n", " ");
    s.trim();
    return s;
}

static void appendEventCsvRow(const char *startIso, uint32_t startEpoch,
                              uint32_t endEpoch, const char *label, const char *note)
{
    const bool exists = LittleFS.exists(EVENT_LOG_PATH);
    fs::File f = LittleFS.open(EVENT_LOG_PATH, exists ? "a" : "w");
    if (!f)
        return;
    if (!exists || f.size() == 0)
    {
        f.println(EVENT_LOG_HEADER);
    }
    char line[512];
    snprintf(line, sizeof(line), "%s,%lu,%lu,%s,%s",
             startIso,
             (unsigned long)startEpoch,
             (unsigned long)endEpoch,
             label,
             note);
    f.println(line);
    f.close();
}

void WebExport::onEventLog()
{
    const String action = _server.arg("action");
    const int64_t nowUs = TimeService::epochUs();
    const uint32_t nowEpoch = (uint32_t)(nowUs / 1000000LL);
    char nowIso[48] = "1970-01-01T00:00:00";
    TimeService::formatISO(nowUs, nowIso, sizeof(nowIso));

    const String label = sanitizeCsvField(_server.arg("label"));
    const String note = sanitizeCsvField(_server.arg("note"));

    if (action == "start")
    {
        // If an event was already active, close and save it first
        if (_evActive && _evStartEpoch > 0)
        {
            appendEventCsvRow(_evStartIso, _evStartEpoch, nowEpoch, _evLabel, _evNote);
        }
        _evActive = true;
        _evStartEpoch = nowEpoch;
        snprintf(_evStartIso, sizeof(_evStartIso), "%s", nowIso);
        snprintf(_evLabel, sizeof(_evLabel), "%s", label.length() ? label.c_str() : "Isolation Test");
        snprintf(_evNote, sizeof(_evNote), "%s", note.c_str());
    }
    else if (action == "stop")
    {
        if (_evActive && _evStartEpoch > 0)
        {
            if (note.length())
                snprintf(_evNote, sizeof(_evNote), "%s", note.c_str());
            appendEventCsvRow(_evStartIso, _evStartEpoch, nowEpoch, _evLabel, _evNote);
        }
        _evActive = false;
        _evStartEpoch = 0;
        _evStartIso[0] = '\0';
        _evLabel[0] = '\0';
        _evNote[0] = '\0';
    }
    else if (action == "instant")
    {
        const char *lbl = label.length() ? label.c_str() : "Note";
        appendEventCsvRow(nowIso, nowEpoch, nowEpoch, lbl, note.c_str());
    }
    else if (action == "clear")
    {
        LittleFS.remove(EVENT_LOG_PATH);
        _evActive = false;
        _evStartEpoch = 0;
        _evStartIso[0] = '\0';
        _evLabel[0] = '\0';
        _evNote[0] = '\0';
    }

    const uint32_t evElapsed = (_evActive && nowEpoch >= _evStartEpoch && _evStartEpoch > 0)
                                   ? (nowEpoch - _evStartEpoch) : 0;
    char out[384];
    snprintf(out, sizeof(out),
             "{\"ok\":1,\"ev_active\":%s,\"ev_label\":\"%s\",\"ev_elapsed_s\":%lu}",
             _evActive ? "true" : "false",
             _evLabel,
             (unsigned long)evElapsed);
    _server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// /pause — pause/resume drop logging or trim recent handling-noise drops
// ---------------------------------------------------------------------------
void WebExport::onPause()
{
    const String action = _server.arg("action");
    uint32_t removed = 0;

    if (action == "pause")
    {
        _sampler.setPaused(true);
    }
    else if (action == "resume")
    {
        _sampler.setPaused(false);
    }
    else if (action == "toggle")
    {
        _sampler.setPaused(!_sampler.paused());
    }
    else if (action == "trim")
    {
        long mins = _server.arg("mins").toInt();
        if (mins < 1) mins = 5;
        if (mins > 120) mins = 120;
        _sampler.discardEvents();
        const double nowSec = (double)TimeService::epochUs() / 1000000.0;
        const double cutoff = nowSec - (double)(mins * 60);
        removed = _log.trimSince(cutoff);
        struct tm tmv;
        if (TimeService::localTime(tmv))
            _log.rebuildToday(_histo, tmv, &_sampler);
    }

    char out[128];
    snprintf(out, sizeof(out), "{\"ok\":1,\"paused\":%s,\"removed\":%lu}",
             _sampler.paused() ? "true" : "false",
             (unsigned long)removed);
    _server.send(200, "application/json", out);
}
