// ============================================================================
//  web_export.h — HTTP server: unified dashboard + collapsible tuning scope
//
//    GET /            unified single-page dashboard: live KPIs, 24h histogram,
//                     WAV recorder controls, and collapsible live signal scope
//                     + visual tuning bench
//    GET /cal         redirects to /#tuning for bookmark compatibility
//    GET /drops.csv   the full append-only CSV log (streamed)
//    GET /status.json machine-readable snapshot incl. histogram arrays
//    GET /stream.json compact 512-point peak-held envelope + state stream
//    GET /set         apply + persist calibration parameters
//    GET /calrun      start/stop/apply water quantity scale calibration
//    GET /record      save rolling 60s PSRAM audio ring to /rec.wav
//    GET /rec.wav     download saved WAV recording
//    GET /clearlog    wipe /drops.csv and reset today's histogram
//    POST /update     OTA firmware update over Wi-Fi
// ============================================================================
#pragma once

#include <WebServer.h>
#include "drop_sampler.h"
#include "drop_log.h"
#include "histogram.h"

class WebExport
{
public:
    WebExport(DropSampler &s, DropLog &l, Histogram &h)
        : _sampler(s), _log(l), _histo(h) {}

    void begin();
    void handleClient()
    {
        if (_started)
            _server.handleClient();
    }

private:
    static void taskEntry(void *arg);   // dedicated server task (Core 0)
    void onRoot();
    void onCsv();
    void onStatus();
    void onStream();
    void onSet();
    void onCalRun();
    void onClearLog();
    void onRecord();
    void onRecWav();

    // Shared helpers.
    void histogramSVG(String &out, uint8_t nowIdx);

    DropSampler &_sampler;
    DropLog     &_log;
    Histogram   &_histo;
    WebServer    _server{80};
    bool         _started = false;
};
