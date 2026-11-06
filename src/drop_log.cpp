// ============================================================================
//  drop_log.cpp — implementation
// ============================================================================
#include "drop_log.h"
#include "time_service.h"
#include <sys/time.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

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

    // Torn-write guard: if the file does not end with a newline, the last
    // append was interrupted by a power loss. Quarantine it and start a
    // fresh log instead of parsing garbage forever.
    if (_size > 0)
    {
        bool torn = false;
        uint32_t existingCfgCount = 0;
        fs::File chk = LittleFS.open(LOG_PATH, "r");
        if (chk)
        {
            chk.seek(_size - 1);
            uint8_t last = 0;
            if (chk.read(&last, 1) == 1 && last != '\n')
                torn = true;
            else
            {
                chk.seek(0);
                while (chk.available())
                {
                    String l = chk.readStringUntil('\n');
                    if (l.startsWith("#cfg,"))
                        ++existingCfgCount;
                }
            }
            chk.close();
        }
        _cfgVersion = existingCfgCount;

        if (torn)
        {
            _file.close();
            _open = false;
            char bad[40];
            snprintf(bad, sizeof(bad), "/drops_bad_%ld.csv", (long)time(nullptr));
            LittleFS.rename(LOG_PATH, bad);
            Serial.printf("[FS] quarantined torn log as %s\n", bad);
            _cfgVersion = 0;
            reopen();
        }
    }

    // Fresh file -> write the CSV header line first.
    if (_size == 0)
    {
        _file.print(LOG_HEADER);
        _file.print('\n');
        _file.flush();
        _size  = _file.size();
        _lines = 1;     // the header row
        Serial.println("[FS] created " LOG_PATH " with header");
    }
    else
    {
        Serial.printf("[FS] opened %s (%u bytes, %lu cfg entries)\n",
                      LOG_PATH, (unsigned)_size, (unsigned long)_cfgVersion);
    }

    return true;
}

void DropLog::reopen()
{
    _file = LittleFS.open(LOG_PATH, FILE_APPEND);
    _open = (bool)_file;
    if (_open)
        _size = _file.size();
}

bool DropLog::append(const DropEvent &ev, uint16_t volUl)
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
                 (unsigned)ev.peak, (unsigned)ev.envPeak, (unsigned)volUl);
    _file.flush();          // persist before acknowledging the event
    _size  = _file.size();
    ++_lines;

    return true;
}

void DropLog::appendSettings(uint16_t margin, uint8_t nmult,
                             uint16_t refr, uint16_t rel, uint16_t cap,
                             bool qtyEn, uint16_t uldTenths)
{
    if (!_open)
        return;
    ++_cfgVersion;

    _file.printf("#cfg,v%lu,%lld,margin=%u,nmult=%u,refr=%u,rel=%u,cap=%u,qen=%d,uld=%.1f\n",
                 (unsigned long)_cfgVersion,
                 (long long)time(nullptr),
                 (unsigned)margin, (unsigned)nmult,
                 (unsigned)refr, (unsigned)rel, (unsigned)cap,
                 (int)qtyEn, (float)uldTenths * 0.1f);
    _file.flush();
    _size  = _file.size();
    ++_lines;
}

uint32_t DropLog::rebuildToday(Histogram &h, const struct tm &today,
                               const DropSampler *sampler)
{
    if (!_open)
        return 0;

    // Yesterday's local date, for the dim comparison layer.
    struct tm ydayTm;
    {
        time_t ydayT = (time_t)0;
        struct tm tt = today;
        tt.tm_hour = 12;                        // avoid DST edge effects
        tt.tm_min  = 0;
        tt.tm_sec  = 0;
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
    uint32_t lineNum = 0;
    while (rd.available())
    {
        if ((++lineNum & 0xFF) == 0)
            yield();
        line = rd.readStringUntil('\n');
        if (line.length() < 3 || line[0] == '#' || line.startsWith("ISO_"))
            continue;                       // header / junk

        // Columns: ISO_Timestamp,Epoch,PeakADC,EnvPeak[,Volume_uL]
        const int comma1 = line.indexOf(',');
        if (comma1 <= 0)
            continue;
        const int comma2 = line.indexOf(',', comma1 + 1);
        if (comma2 <= comma1)
            continue;

        // Epoch column holds decimal seconds. Validate defensively: a
        // torn write can leave a garbage final line (NaN/huge epochs).
        const String epochStr = line.substring(comma1 + 1, comma2);
        const double ev = atof(epochStr.c_str());
        if (!isfinite(ev) || ev < TIME_VALID_EPOCH || ev > 4.1e9)
            continue;                       // corrupt or pre-sync: skip

        // Parse EnvPeak (4th column) so we can always compute calibrated volume.
        uint16_t envPeak = 31;
        const int comma3 = line.indexOf(',', comma2 + 1);
        if (comma3 > comma2)
        {
            const int comma4 = line.indexOf(',', comma3 + 1);
            const String envStr = (comma4 > comma3)
                                  ? line.substring(comma3 + 1, comma4)
                                  : line.substring(comma3 + 1);
            const int ep = envStr.toInt();
            if (ep > 0 && ep < 4096)
                envPeak = (uint16_t)ep;
        }

        const uint16_t volUl = sampler ? sampler->estimateDropUl(envPeak) : (DEFAULT_UL_TENTHS / 10);

        const time_t t = (time_t)ev;
        h.addRolling(t, volUl);

        struct tm tmv;
        localtime_r(&t, &tmv);
        if (h.sameDate(tmv))
        {
            h.add(tmv, volUl);
            ++matched;
        }
        else if ((uint8_t)tmv.tm_year == (uint8_t)ydayTm.tm_year &&
                 (uint8_t)tmv.tm_mon  == (uint8_t)ydayTm.tm_mon &&
                 (uint8_t)tmv.tm_mday == (uint8_t)ydayTm.tm_mday)
        {
            h.addYesterday(tmv, volUl);
        }
    }
    rd.close();

    Serial.printf("[FS] rebuilt histogram: %u drops logged today (%u uL)\n",
                  matched, (unsigned)h.totalVolTodayUl());
    return matched;
}

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
    _cfgVersion = 0;
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

bool DropLog::compact()
{
    const char *tmpPath = "/drops.tmp";

    // --- pass 1: count data lines (header excluded) -----------------------
    uint32_t dataLines = 0;
    {
        fs::File rd = LittleFS.open(LOG_PATH, "r");
        if (!rd)
            return false;
        uint32_t lineNum = 0;
        while (rd.available())
        {
            if ((++lineNum & 0xFF) == 0)
                yield();
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
    uint32_t lineNum = 0;
    String lastSkippedCfg;
    bool wroteLastSkippedCfg = false;
    while (rd.available())
    {
        if ((++lineNum & 0xFF) == 0)
            yield();
        String line = rd.readStringUntil('\n');
        if (line.startsWith("#cfg,"))
        {
            if (seen < skipLines)
            {
                lastSkippedCfg = line;
                continue;
            }
        }
        else if (line.length() >= 3 && line[0] != '#')
        {
            ++seen;
            if (seen <= skipLines)
                continue;
            if (!wroteLastSkippedCfg && lastSkippedCfg.length() > 0)
            {
                wr.print(lastSkippedCfg);
                wr.print('\n');
                wroteLastSkippedCfg = true;
            }
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
