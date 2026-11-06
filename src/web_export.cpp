// ============================================================================
//  web_export.cpp — unified single-page dashboard + visual tuning scope
// ============================================================================
#include "web_export.h"
#include "time_service.h"
#include "tuning.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <stdio.h>
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
    padding-block: 1rem 2.5rem;
    padding-inline: 1rem;
    line-height: 1.45;
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
    gap: 0.75rem;
    padding-block-end: 0.85rem;
    margin-block-end: 1rem;
    border-block-end: 1px solid var(--border-subtle);
  }
  .brand {
    display: flex;
    align-items: baseline;
    gap: 0.6rem;
  }
  .brand h1 {
    font-size: 1.35rem;
  }
  .brand-sub {
    font-size: 0.85rem;
    color: var(--text-muted);
    font-family: var(--font-mono);
  }
  .pills {
    display: flex;
    flex-wrap: wrap;
    gap: 0.45rem;
    align-items: center;
  }
  .pill {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    padding: 0.25rem 0.6rem;
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
    grid-template-columns: repeat(auto-fit, minmax(175px, 1fr));
    gap: 0.75rem;
    margin-block-end: 1rem;
  }
  .card {
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-md);
    padding: 0.9rem 1rem;
  }
  .kpi-label {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-muted);
    margin-block-end: 0.25rem;
  }
  .kpi-val {
    font-family: var(--font-mono);
    font-size: 1.65rem;
    font-weight: 700;
    color: var(--text-main);
    line-height: 1.15;
  }
  .kpi-accent {
    color: var(--accent-cyan);
  }
  .kpi-sub {
    font-size: 0.78rem;
    color: var(--text-muted);
    margin-block-start: 0.3rem;
    font-family: var(--font-mono);
  }
  .sec-head {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    align-items: baseline;
    gap: 0.5rem;
    margin-block-end: 0.65rem;
  }
  .sec-head h2 {
    font-size: 1rem;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 0.85rem;
    font-size: 0.78rem;
    color: var(--text-muted);
    font-family: var(--font-mono);
  }
  .dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 2px;
    margin-inline-end: 4px;
    vertical-align: middle;
  }
  .toolbar {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    align-items: center;
    margin-block-start: 0.8rem;
    padding-block-start: 0.75rem;
    border-block-start: 1px solid var(--border-subtle);
  }
  .btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 0.35rem;
    font-family: var(--font-sans);
    font-size: 0.88rem;
    font-weight: 550;
    min-height: 40px;
    padding-inline: 0.85rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-subtle);
    background: var(--bg-elevated);
    color: var(--text-main);
    cursor: pointer;
    text-decoration: none;
  }
  .btn:hover:not(:disabled) {
    border-color: var(--accent-cyan);
    background: #222b3d;
    text-decoration: none;
  }
  .btn:focus-visible, input:focus-visible, summary:focus-visible {
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
  /* Collapsible Tuning Disclosure */
  details.tuning-drawer {
    background: var(--bg-card);
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-md);
    margin-block-start: 1rem;
    overflow: hidden;
  }
  details.tuning-drawer > summary {
    list-style: none;
    cursor: pointer;
    padding: 0.95rem 1.1rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    background: var(--bg-elevated);
    font-weight: 600;
    font-size: 1rem;
    user-select: none;
  }
  details.tuning-drawer > summary::-webkit-details-marker {
    display: none;
  }
  details.tuning-drawer > summary::after {
    content: "Show Knobs ▾";
    font-family: var(--font-mono);
    font-size: 0.78rem;
    font-weight: 500;
    color: var(--accent-cyan);
    padding: 0.25rem 0.6rem;
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
  .tuning-body {
    padding: 1rem 1.1rem;
  }
  .scope-wrap {
    position: relative;
    margin-block-end: 1rem;
  }
  canvas#scope-cv {
    width: 100%;
    height: auto;
    display: block;
    background: #0d1119;
    border: 1px solid var(--border-subtle);
    border-radius: var(--radius-sm);
    touch-action: none;
    cursor: ns-resize;
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
    _server.on("/status.json", HTTP_GET, [this]() { onStatus(); });
    _server.on("/stream.json", HTTP_GET, [this]() { onStream(); });
    _server.on("/set", HTTP_GET, [this]() { onSet(); });
    _server.on("/clearlog", HTTP_GET, [this]() { onClearLog(); });
    _server.on("/record", HTTP_GET, [this]() { onRecord(); });
    _server.on("/rec.wav", HTTP_GET, [this]() { onRecWav(); });
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
    page.reserve(16384);
    page += "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<title>DripMeter</title>";
    page += FPSTR(PAGE_CSS);
    page += "</head><body>";

    char buf[2048];
    const uint8_t st = _sampler.detectorState();
    const char *stClass = (st == 1) ? "pill-cap" : ((st == 2) ? "pill-rel" : "pill-armed");

    snprintf(buf, sizeof(buf),
        "<header class='topbar'>"
        "<div class='brand'>"
        "<h1>DripMeter</h1>"
        "<span class='brand-sub' id='hdr-time'>%s %s</span>"
        "</div>"
        "<div class='pills'>"
        "<span class='pill %s' id='hdr-state'>%s</span>"
        "<span class='pill' id='hdr-sps'>%.1f kS/s</span>"
        "<span class='pill' id='hdr-wifi'>Wi-Fi %d dBm</span>"
        "</div>"
        "</header>"
        "<main>"
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
        stClass, stateLabel(st),
        (float)_sampler.samplesPerSecond() / 1000.0f,
        (int)WiFi.RSSI(),
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

    // --- 24h Histogram Card ------------------------------------------------
    page += "<section class='card' aria-label='24-hour distribution'>"
            "<div class='sec-head'>"
            "<h2>24-Hour Drop Distribution (15-min buckets)</h2>"
            "<div class='legend'>"
            "<span><i class='dot' style='background:#0284c7'></i>Today</span>"
            "<span><i class='dot' style='background:#38bdf8'></i>Current bucket</span>"
            "<span><i class='dot' style='background:#2b384e'></i>Yesterday</span>"
            "<span><i class='dot' style='background:#f43f5e'></i>Now</span>"
            "</div></div>"
            "<div id='histo-box'>";
    page += svg;
    page += "</div>"
            "<div class='toolbar'>"
            "<a class='btn' href='/drops.csv'>Download drops.csv</a>"
            "<a class='btn' href='/status.json'>status.json</a>"
            "<a class='btn' href='/bootlog.csv'>bootlog.csv</a>"
            "<button type='button' class='btn' id='btn-rec'>Save Last 60s Audio (.wav)</button>"
            "<a class='btn' id='link-wav' href='/rec.wav' style='display:none'>Download rec.wav</a>"
            "<span id='rec-msg' class='kpi-sub' aria-live='polite'></span>"
            "<button type='button' class='btn btn-danger' id='btn-clear' style='margin-inline-start:auto'>Clear Log</button>"
            "</div></section>";

    // --- Live Signal Scope Card (Always visible) + Collapsible Tuning ------
    page += "<section class='card' style='margin-block-start:1rem' aria-label='Live Signal Scope'>"
            "<div class='sec-head'>"
            "<h2>Live Signal Scope (5.2s window)</h2>"
            "<div style='display:flex;gap:0.5rem;align-items:center'>"
            "<span id='save-badge' class='save-status' aria-live='polite'></span>"
            "<button type='button' class='btn' id='btn-pause' style='min-height:32px;padding-inline:0.65rem;font-size:0.8rem'>Pause Scope</button>"
            "</div></div>"
            "<div class='scope-wrap'>"
            "<canvas id='scope-cv' width='760' height='220'></canvas>"
            "<div class='scope-bar'>"
            "<div class='legend'>"
            "<span><i class='dot' style='background:#38bdf8'></i>Envelope</span>"
            "<span><i class='dot' style='background:#f43f5e'></i>Threshold (drag line)</span>"
            "<span><i class='dot' style='background:#64748b'></i>Baseline</span>"
            "<span><i class='dot' style='background:#22c55e'></i>Capture window</span>"
            "<span><i class='dot' style='background:#f59e0b'></i>Release / Refractory holdoff</span>"
            "</div></div></div>"

            "<details class='tuning-drawer' id='tuning-drawer' open>"
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
        "</form></div></details></section></main>",
        (unsigned)_sampler.refractoryMs(),
        (unsigned)(_sampler.refractoryMs() > 1000 ? 1000 : _sampler.refractoryMs()),
        (unsigned)_sampler.refractoryMs());
    page += buf;

    // --- Client-side interactive script (no inline handlers) ---------------
    page += R"rawliteral(
<script>
(() => {
  const drawer = document.getElementById('tuning-drawer');
  const cv = document.getElementById('scope-cv');
  const ctx = cv.getContext('2d');
  const form = document.getElementById('tune-form');
  const saveBadge = document.getElementById('save-badge');

  let paused = false;
  let activeKnob = null;
  let lastStream = {
    env: [],
    st: [],
    base: 0,
    noise: 0,
    thr: 15,
    sps: 18000
  };
  let ymax = 64;
  let lastDropEpochUs = 0;

  if (sessionStorage.getItem('dm_tuning_open') === '0' && location.hash !== '#tuning') {
    drawer.open = false;
  }
  drawer.addEventListener('toggle', () => {
    sessionStorage.setItem('dm_tuning_open', drawer.open ? '1' : '0');
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
    this.textContent = paused ? 'Resume Scope' : 'Pause Scope';
  });

  document.getElementById('btn-reset').addEventListener('click', async () => {
    await fetch('/set?reset=1&ajax=1');
    location.reload();
  });

  document.getElementById('btn-clear').addEventListener('click', async () => {
    if (!confirm('Wipe the entire drop log and reset today\'s histogram?')) return;
    await fetch('/clearlog');
    pollStatus();
  });

  document.getElementById('btn-rec').addEventListener('click', async function() {
    const msg = document.getElementById('rec-msg');
    const wavLink = document.getElementById('link-wav');
    this.disabled = true;
    msg.textContent = 'Saving 60s WAV to flash...';
    try {
      const j = await (await fetch('/record')).json();
      if (j.ok) {
        msg.textContent = `Saved ${j.seconds}s @ ${j.rate} Hz`;
        wavLink.style.display = 'inline-flex';
      } else {
        msg.textContent = 'Failed: ' + (j.err || 'error');
      }
    } catch (e) {
      msg.textContent = 'Request failed';
    }
    this.disabled = false;
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
  let dragging = false;
  function yToMargin(py) {
    const padT = 22, padB = 22;
    const plotH = cv.height - padT - padB;
    const v = Math.round((1 - (py - padT) / plotH) * ymax);
    return Math.max(5, Math.min(500, v - (lastStream.base || 0)));
  }
  cv.addEventListener('pointerdown', e => {
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
    if (dragging) {
      const m = yToMargin(py);
      document.getElementById('in-margin').value = m;
      document.getElementById('rng-margin').value = Math.min(120, m);
      document.getElementById('lbl-margin').textContent = m;
      drawScope();
    } else {
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

  async function pollStatus() {
    try {
      const j = await (await fetch('/status.json')).json();
      if (j.time) {
        const tpart = j.time.slice(11, 19);
        document.getElementById('hdr-time').textContent = `${tpart} ${j.time_synced ? '(NTP)' : '(unsynced)'}`;
      }
      const stEl = document.getElementById('hdr-state');
      stEl.textContent = j.state || 'ARMED';
      stEl.className = 'pill ' + (j.state === 'CAPTURING' ? 'pill-cap' : (j.state === 'RELEASE' ? 'pill-rel' : 'pill-armed'));
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
    char head[900];
    snprintf(head, sizeof(head),
        "{\"time\":%s,\"time_synced\":%s,\"epoch_us\":%lld,"
        "\"drops_today\":%lu,\"triggers_total\":%lu,"
        "\"last_drop\":%s,\"last_drop_epoch\":%lld,\"last_peak\":%u,\"last_env_peak\":%u,"
        "\"raw\":%u,\"envelope\":%u,\"env_base\":%u,\"env_noise\":%u,\"threshold\":%u,"
        "\"state\":\"%s\",\"sps\":%u,"
        "\"margin\":%u,\"noise_mult\":%u,"
        "\"capture_ms\":%u,\"release_ms\":%u,\"refractory_ms\":%u,"
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
        (unsigned)_sampler.samplesPerSecond(),
        (unsigned)_sampler.margin(), (unsigned)_sampler.noiseMult(),
        (unsigned)_sampler.captureWindowMs(), (unsigned)_sampler.releaseMs(),
        (unsigned)_sampler.refractoryMs(),
        WiFi.status() == WL_CONNECTED ? "true" : "false",
        (int)WiFi.RSSI(), ipStr.c_str(),
        (unsigned)(_log.totalBytes() - _log.usedBytes()),
        (unsigned)_log.totalBytes(), (unsigned)_log.fileSize(),
        (unsigned long)(millis() / 1000),
        (unsigned long)(millis() - gLoopMs.load()),
        BUCKET_MINUTES, (unsigned)nowBucket);

    String js;
    js.reserve(3200);
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
    _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                        _sampler.refractoryMs(), _sampler.releaseMs(),
                        _sampler.captureWindowMs());
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
                            _sampler.captureWindowMs());
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
                        _sampler.captureWindowMs());

    if (_server.hasArg("ajax"))
    {
        _server.send(200, "application/json", "{\"ok\":1}");
        return;
    }
    _server.sendHeader("Location", "/#tuning");
    _server.send(302, "text/plain", "");
}
