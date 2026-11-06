// ============================================================================
//  histogram.h — 24-hour drop distribution + rolling water volume accumulators
//
//  NUM_BUCKETS (default 96) buckets of BUCKET_MINUTES minutes each cover
//  one local day. TWO days are kept in RAM: today's counts (rendered in
//  full color) and yesterday's (rendered as a dim ghost behind them).
//
//  Also maintains rolling microliter (uL) accumulators for:
//    - Last 60 minutes (1-minute slots)
//    - Last 24 hours & Previous 24 hours (15-minute slots)
//    - Multi-day 24-hour rolling average across the entire CSV log span
// ============================================================================
#pragma once

#include <stdint.h>
#include <time.h>
#include "config.h"

class Histogram
{
public:
    static const uint8_t  kBuckets  = NUM_BUCKETS;
    static const uint8_t  kMinSlots = 60;
    static const uint16_t kQtrSlots = 192; // 48 hours of 15-min slots

    // Anchor the histogram to the given local date and clear both days.
    void resetForDate(const struct tm &t);

    // True when t falls on the same local day the histogram is anchored to.
    bool sameDate(const struct tm &t) const;

    // Increment the bucket t belongs to (today's or yesterday's array).
    void add(const struct tm &t, uint16_t volUl = 0);
    void addYesterday(const struct tm &t, uint16_t volUl = 0);

    // Add a drop at epochSec into the rolling 1h / 24h / multi-day accumulators.
    void addRolling(time_t epochSec, uint16_t volUl);

    // Clear only the rolling volume/drop accumulators (used before log replay).
    void clearRolling();

    // Convenience: rollover at midnight if needed, then count one drop + volume.
    void record(const struct tm &t, time_t epochSec = 0, uint16_t volUl = 0);

    // Call once per second from the loop; at midnight, today's counts move
    // to the yesterday array and today starts empty.
    void rolloverIfNeeded(const struct tm &now);

    void clear();

    uint32_t total() const;             // drops in the anchored (today) day
    uint32_t totalVolTodayUl() const { return _volTodayUl; }
    uint32_t totalVolYdayUl()  const { return _volYesterdayUl; }
    uint16_t max() const;               // tallest bucket across BOTH days
    uint16_t bucket(uint8_t i) const;   // today, 0..kBuckets-1
    uint16_t bucketYesterday(uint8_t i) const;
    const uint16_t *buckets() const { return _bToday; }
    const uint16_t *bucketsYesterday() const { return _bYesterday; }

    // Rolling volume & drop metrics relative to nowSec:
    uint32_t lastHourVolUl(time_t nowSec) const;
    uint32_t lastHourDrops(time_t nowSec) const;
    uint32_t last24hVolUl(time_t nowSec) const;
    uint32_t last24hDrops(time_t nowSec) const;
    uint32_t prev24hVolUl(time_t nowSec) const;
    uint32_t rolling24hAvgVolUl(time_t nowSec) const;
    uint32_t logSpanSeconds(time_t nowSec) const;

    bool anchored() const { return _anchored; }

    // Bucket index for a time of day.
    static uint8_t indexFor(const struct tm &t)
    {
        const uint16_t mins = (uint16_t)t.tm_hour * 60 + (uint16_t)t.tm_min;
        return (uint8_t)(mins / BUCKET_MINUTES);
    }

private:
    uint16_t _bToday[kBuckets]     = {0};
    uint16_t _bYesterday[kBuckets] = {0};
    uint32_t _volTodayUl           = 0;
    uint32_t _volYesterdayUl       = 0;

    // 60 x 1-minute slots for exact Last Hour volume & drops
    uint32_t _minTag[kMinSlots]    = {0};
    uint32_t _minVolUl[kMinSlots]  = {0};
    uint16_t _minDrops[kMinSlots]  = {0};

    // 192 x 15-minute slots for exact Last 24h and Prev 24h volume & drops
    uint32_t _qtrTag[kQtrSlots]    = {0};
    uint32_t _qtrVolUl[kQtrSlots]  = {0};
    uint16_t _qtrDrops[kQtrSlots]  = {0};

    // Multi-day log span for 24-hour rolling average
    time_t   _firstEpochSec        = 0;
    uint64_t _totalLogVolUl        = 0;
    uint32_t _totalLogDrops        = 0;

    bool     _anchored = false;
    uint8_t  _year = 0;     // tm_year (years since 1900)
    uint8_t  _mon  = 0;     // 0..11
    uint8_t  _mday = 0;     // 1..31
};
