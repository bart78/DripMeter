// ============================================================================
//  drop_log.h — durable, append-only CSV event log on LittleFS
//
//  Every accepted drop impact is appended (and flushed — power-loss safe at
//  the file level) to /drops.csv:
//
//      ISO_Timestamp,Epoch,PeakADC,EnvPeak
//      2026-11-06T12:07:45+0900,1793934465.801,171,40
//
//  The file is the single source of truth: it survives resets, and at boot
//  it is replayed to rebuild today's on-screen histogram. When the file
//  grows past LOG_MAX_BYTES (or the filesystem runs low), the oldest third
//  is compacted away in a safe copy+rename dance.
// ============================================================================
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include "config.h"
#include "drop_event.h"
#include "histogram.h"

class DropLog
{
public:
    // Mount LittleFS (format on first boot), open the log for append and
    // write the CSV header if the file is new. Returns false on failure.
    bool begin();

    // Append one event as a CSV line and flush. Requires a synced clock
    // for a meaningful ISO timestamp; the epoch field is always written.
    bool append(const DropEvent &ev);

    // Append a settings snapshot line ('#cfg,...') so any later analysis
    // knows which configuration produced the surrounding data.
    void appendSettings(uint16_t margin, uint8_t nmult,
                        uint16_t refr, uint16_t rel, uint16_t cap);

    // Replay the log and rebuild the histogram for the given local date:
    // today's counts plus yesterday's (the dim comparison layer).
    uint32_t rebuildToday(Histogram &h, const struct tm &today);

    // Non-blocking housekeeping: compact when size/free-space limits are
    // exceeded (rate-limited by LOG_COMPACT_COOLDOWN_MS). Call from loop().
    void maintain();

    // Delete the log and start a fresh one (header only). Used before a
    // clean measurement session.
    bool clearLog();

    bool     fsOk()       const { return _fsOk; }
    bool     ok()         const { return _open; }
    size_t   fileSize()   const { return _size; }
    uint32_t lineCount()  const { return _lines; }

    // LittleFS capacity figures for the status bar.
    size_t usedBytes()  const { return _fsOk ? LittleFS.usedBytes()  : 0; }
    size_t totalBytes() const { return _fsOk ? LittleFS.totalBytes() : 0; }

private:
    void reopen();
    bool compact();

    fs::File _file;
    bool     _fsOk       = false;
    bool     _open       = false;
    size_t   _size       = 0;
    uint32_t _lines      = 0;
    uint32_t _cfgVersion = 0;
    uint32_t _lastCompactMs = 0;
};
