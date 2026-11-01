// ============================================================================
//  time_service.cpp — implementation
//
//  Design: the ESP32-S3 has no battery-backed RTC, so after power-up the
//  clock starts at (or near) the Unix epoch. SNTP fixes that in the
//  background as soon as Wi-Fi is up; until then TimeService::synced() is
//  false, the dashboard shows "--:--:--" and drop timestamps are recorded
//  with their raw RTC-counter epoch (still logged, flagged on screen).
//
//  API notes (Arduino-ESP32 core 3.3.x):
//    * configTzTime() installs the POSIX timezone AND arms lwIP SNTP.
//      The legacy configTime(gmt, dst, ...) must NOT be used here — it
//      overwrites TZ with a fixed-offset string computed from its args.
//    * The IDF-legacy sntp_* shims were removed in core 3.3; the lwIP SNTP
//      API (sntp_init / sntp_stop / sntp_set_sync_interval) is what remains
//      and is used below.
// ============================================================================
#include "time_service.h"
#include "config.h"
#include <sys/time.h>
#include "lwip/apps/sntp.h"
#include "lwip/priv/tcpip_priv.h"   // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE

volatile bool TimeService::_synced = false;

void TimeService::begin()
{
    // TZ string + servers in one call; the RTC counter itself stays UTC.
    // (lwIP SNTP re-polls hourly on its own — no runtime interval API in
    // the IDF 5.5 lwIP used by core 3.3.x.)
    configTzTime(TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);
}

void TimeService::tick()
{
    if (!_synced && time(nullptr) > TIME_VALID_EPOCH)
    {
        _synced = true;
        Serial.println("[NTP] clock synced");
    }
    // A clock that regressed below the sanity line (SNTP not re-syncing
    // after a long offline stretch) is treated as unsynced again.
    else if (_synced && time(nullptr) < TIME_VALID_EPOCH)
    {
        _synced = false;
    }
}

void TimeService::forceResync()
{
    // Same stop+init dance the core itself uses: re-arms the SNTP thread
    // (its server list survives the stop) so it polls as soon as the
    // network is reachable again. lwIP requires the TCPIP core lock around
    // sntp_stop/sntp_init (see the assert in sntp.c).
#ifdef CONFIG_LWIP_TCPIP_CORE_LOCKING
    if (!sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER))
        LOCK_TCPIP_CORE();
#endif
    if (sntp_enabled())
        sntp_stop();
    sntp_init();
#ifdef CONFIG_LWIP_TCPIP_CORE_LOCKING
    if (sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER))
        UNLOCK_TCPIP_CORE();
#endif
}

bool TimeService::synced()
{
    return _synced;
}

bool TimeService::localTime(struct tm &out)
{
    if (!_synced)
        return false;
    const time_t now = time(nullptr);
    if (now < TIME_VALID_EPOCH)
        return false;
    localtime_r(&now, &out);
    return true;
}

int64_t TimeService::epochUs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

bool TimeService::formatISO(int64_t epochUs, char *buf, size_t len)
{
    const time_t t = (time_t)(epochUs / 1000000LL);
    if (t < TIME_VALID_EPOCH)
        return false;
    struct tm tmv;
    localtime_r(&t, &tmv);
    return strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", &tmv) > 0;
}
