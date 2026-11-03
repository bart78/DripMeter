// ============================================================================
//  web_export.h — tiny HTTP server: status page + calibration bench + data
//
//    GET /            status display: live stats + 24h histogram chart
//                     (today solid, yesterday dimmed) with a "now" marker
//    GET /cal         calibration bench: live RAW/ENV/MATCH/THR levels,
//                     tuning form, drop-template record/forget
//    GET /drops.csv   the full append-only log (streamed, chunked)
//    GET /status.json machine-readable snapshot incl. histogram arrays
//
//  Served from the loop task via server.handleClient() — non-blocking, and
//  the sampler task (higher priority) keeps catching drops even while a
//  CSV download is streaming.
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
    static void taskEntry(void *arg);   // dedicated server task
    void onRoot();
    void onCal();
    void onCsv();
    void onStatus();
    void onStream();
    void onSet();
    void onLearn();
    void onLearnNoise();
    void onForget();
    void onForgetNoise();
    void onClearLog();
    void onAutoCal();
    void onCalDump();
    void onRecord();
    void onRecWav();
    void onUploadTemplate();
    void onUploadNoise();

    // Shared helpers.
    void histogramSVG(String &out, uint8_t nowIdx);

    DropSampler &_sampler;
    DropLog     &_log;
    Histogram   &_histo;
    WebServer    _server{80};
    bool         _started = false;
};
