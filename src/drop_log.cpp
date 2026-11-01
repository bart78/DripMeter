// ============================================================================
//  drop_log.cpp — implementation
// ============================================================================
#include "drop_log.h"
#include "time_service.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
bool DropLog::begin()
{
    // LittleFS.begin(true) formats the partition on first boot.
    if (!LittleFS.begin(true))
    {
        Serial.println("[FS] LittleFS mount failed");
        return false;
    }
    _fsOk = true;

    reopen();
    if (!_open)
    {
        Serial.println("[FS] cannot open " LOG_PATH " for append");
        return false;
    }

    // Fresh file -> write the CSV header line first.
    if (_size == 0)
    {
        _file.print(LOG_HEADER);
        _file.print('\n');
        _file.flush();
        _size = _file.size();
        _lines = 1;     // the header row
        Serial.println("[FS] created " LOG_PATH " with header");
    }
    else
    {
        Serial.printf("[FS] opened %s (%u bytes)\n", LOG_PATH, (unsigned)_size);
    }

    return true;
}

// ---------------------------------------------------------------------------
void DropLog::reopen()
{
    _file = LittleFS.open(LOG_PATH, FILE_APPEND);
    _open = (bool)_file;
    if (_open)
        _size = _file.size();
}

// ---------------------------------------------------------------------------
bool DropLog::append(const DropEvent &ev)
{
    if (!_open)
        return false;

    // ISO-8601 local timestamp with UTC offset ("%z").
    char iso[48];
    bool haveIso = TimeService::formatISO(ev.epochUs, iso, sizeof(iso));
    if (!haveIso)
        snprintf(iso, sizeof(iso), "1970-01-01T00:00:00+0000");

    // Epoch as seconds.milliseconds (kept numeric for easy parsing).
    const int64_t sec  = ev.epochUs / 1000000LL;
    const int64_t msec = (ev.epochUs % 1000000LL) / 1000LL;

    _file.printf("%s,%lld.%03lld,%u,%u,%u\n", iso,
                 (long long)sec, (long long)msec,
                 (unsigned)ev.peak, (unsigned)ev.envPeak, (unsigned)ev.match);
    _file.flush();          // persist before acknowledging the event
    _size  = _file.size();
    ++_lines;

    return true;
}

// ---------------------------------------------------------------------------
// Settings snapshot: '#cfg,v<N>,<epoch>,margin=..,...' — events are matched
// to their producing config by taking the latest line with epoch <= the
// event's epoch. The version tag makes eyeballing easier.
// ---------------------------------------------------------------------------
void DropLog::appendSettings(uint16_t margin, uint8_t nmult, uint16_t mthr,
                             uint8_t mrm, uint16_t refr, uint16_t rel,
                             uint16_t cap, uint8_t mode)
{
    if (!_open)
        return;
    static uint32_t cfgVersion = 0;
    ++cfgVersion;

    _file.printf("#cfg,v%lu,%lld,margin=%u,nmult=%u,mthr=%u,mrm=%u,"
                 "refr=%u,rel=%u,cap=%u,mode=%u\n",
                 (unsigned long)cfgVersion,
                 (long long)time(nullptr),
                 (unsigned)margin, (unsigned)nmult, (unsigned)mthr,
                 (unsigned)mrm, (unsigned)refr, (unsigned)rel,
                 (unsigned)cap, (unsigned)mode);
    _file.flush();
    _size  = _file.size();
    ++_lines;
}

// ---------------------------------------------------------------------------
uint32_t DropLog::rebuildToday(Histogram &h, const struct tm &today)
{
    if (!_open)
        return 0;

    // Yesterday's local date, for the dim comparison layer.
    struct tm ydayTm;
    {
        time_t ydayT = (time_t)0;
        struct tm tt = today;
        tt.tm_hour = 12;                        // avoid DST edge effects
        tt.tm_min = 0;
        tt.tm_sec = 0;
        ydayT = mktime(&tt) - 86400;
        localtime_r(&ydayT, &ydayTm);
    }

    h.resetForDate(today);
    uint32_t matched = 0;

    fs::File rd = LittleFS.open(LOG_PATH, "r");
    if (!rd)
        return 0;

    // Stream the file line by line — constant RAM regardless of size.
    String line;
    while (rd.available())
    {
        line = rd.readStringUntil('\n');
        if (line.length() < 3 || line[0] == '#')
            continue;                       // header / junk

        // Skip to the Epoch column (second CSV field).
        const int comma1 = line.indexOf(',');
        if (comma1 <= 0)
            continue;
        const int comma2 = line.indexOf(',', comma1 + 1);
        if (comma2 <= comma1)
            continue;

        // Epoch column holds decimal seconds.
        const String epochStr = line.substring(comma1 + 1, comma2);
        const time_t t = (time_t)atof(epochStr.c_str());
        if (t < TIME_VALID_EPOCH)
            continue;                       // recorded before the first sync

        struct tm tmv;
        localtime_r(&t, &tmv);
        if (h.sameDate(tmv))
        {
            h.add(tmv);
            ++matched;
        }
        else if ((uint8_t)tmv.tm_year == (uint8_t)ydayTm.tm_year &&
                 (uint8_t)tmv.tm_mon  == (uint8_t)ydayTm.tm_mon &&
                 (uint8_t)tmv.tm_mday == (uint8_t)ydayTm.tm_mday)
        {
            h.addYesterday(tmv);
        }
    }
    rd.close();

    Serial.printf("[FS] rebuilt histogram: %u drops logged today\n", matched);
    return matched;
}

// ---------------------------------------------------------------------------
void DropLog::maintain()
{
    if (!_open || !_fsOk)
        return;

    const uint32_t now = millis();
    if (now - _lastCompactMs < LOG_COMPACT_COOLDOWN_MS)
        return;

    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    const bool full  = _size > LOG_MAX_BYTES;
    const bool tight = freeBytes < LOG_MIN_FREE_BYTES;

    if (!full && !tight)
        return;

    _lastCompactMs = now;
    DBG_PRINTF("[FS] compacting %s (%u bytes, %u free)...\n",
                  LOG_PATH, (unsigned)_size, (unsigned)freeBytes);
    if (compact())
        DBG_PRINTLN("[FS] compaction done");
    else
        DBG_PRINTLN("[FS] compaction FAILED (log left untouched)");
}

// ---------------------------------------------------------------------------
bool DropLog::clearLog()
{
    if (_open)
    {
        _file.close();
        _open = false;
    }
    if (!LittleFS.remove(LOG_PATH))
    {
        reopen();
        return false;
    }
    reopen();
    if (_open)
    {
        _file.print(LOG_HEADER);
        _file.print('\n');
        _file.flush();
        _size  = _file.size();
        _lines = 1;
    }
    DBG_PRINTLN("[FS] log cleared");
    return _open;
}

// ---------------------------------------------------------------------------
// Copy the header plus the newest LOG_KEEP_FRACTION of all lines into a
// temporary file, then atomically swap it into place. If anything fails the
// original file is left untouched.
bool DropLog::compact()
{
    const char *tmpPath = "/drops.tmp";

    // --- pass 1: count data lines (header excluded) -----------------------
    uint32_t dataLines = 0;
    {
        fs::File rd = LittleFS.open(LOG_PATH, "r");
        if (!rd)
            return false;
        while (rd.available())
        {
            String line = rd.readStringUntil('\n');
            if (line.length() >= 3 && line[0] != '#')
                ++dataLines;
        }
        rd.close();
    }

    if (dataLines == 0)
        return false;

    const uint32_t keepLines =
        (uint32_t)((float)dataLines * LOG_KEEP_FRACTION) + 1;
    const uint32_t skipLines = dataLines - keepLines;

    // --- pass 2: write header + last keepLines lines to the temp file -----
    fs::File rd = LittleFS.open(LOG_PATH, "r");
    fs::File wr = LittleFS.open(tmpPath, "w");
    if (!rd || !wr)
    {
        if (rd) rd.close();
        if (wr) { wr.close(); LittleFS.remove(tmpPath); }
        return false;
    }

    uint32_t seen = 0;
    while (rd.available())
    {
        String line = rd.readStringUntil('\n');
        if (line.length() >= 3 && line[0] != '#')
        {
            ++seen;
            if (seen <= skipLines)
                continue;
        }
        wr.print(line);
        wr.print('\n');
    }
    wr.flush();
    rd.close();
    wr.close();

    // --- swap --------------------------------------------------------------
    _file.close();
    _open = false;
    if (!LittleFS.remove(LOG_PATH) || !LittleFS.rename(tmpPath, LOG_PATH))
    {
        LittleFS.remove(tmpPath);       // best effort cleanup
        reopen();
        return false;
    }

    reopen();
    if (_open)
    {
        _size  = _file.size();
        _lines = keepLines + 1;         // data kept + header
    }
    return _open;
}
