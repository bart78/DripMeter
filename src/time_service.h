// ============================================================================
//  time_service.h — SNTP clock synchronisation and local-time helpers
// ============================================================================
#pragma once

#include <Arduino.h>
#include <time.h>

class TimeService
{
public:
    // Install the POSIX timezone and arm SNTP (configTime with UTC offset 0 —
    // the RTC counter itself is kept in UTC; localtime_r applies TZ_STRING).
    static void begin();

    // Non-blocking maintenance: refresh the sync flag. Call from loop().
    static void tick();

    // Kick SNTP once more (used right after Wi-Fi (re)connects).
    static void forceResync();

    // True once the clock is credible (see TIME_VALID_EPOCH).
    static bool synced();

    // Fills *out with the current local wall-clock time; returns false and
    // leaves *out untouched while the clock is not yet synced.
    static bool localTime(struct tm &out);

    // Microseconds since the Unix epoch (monotonic within a session even
    // before NTP sync — it simply reads the RTC counter).
    static int64_t epochUs();

    // Format epochUs into ISO-8601-with-offset using local time.
    // Returns false if the clock is not synced yet.
    static bool formatISO(int64_t epochUs, char *buf, size_t len);

private:
    static volatile bool _synced;
};
