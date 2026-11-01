// ============================================================================
//  histogram.h — 24-hour drop distribution in fixed-size time buckets
//
//  NUM_BUCKETS (default 96) buckets of BUCKET_MINUTES minutes each cover
//  one local day. TWO days are kept in RAM: today's counts (rendered in
//  full color) and yesterday's (rendered as a dim ghost behind them) so
//  day-to-day shifts — washing machine days, shower times, heating cycles —
//  are visible at a glance. Counts are rebuilt from the append-only CSV log
//  at boot and at midnight today's array is swapped into yesterday's.
// ============================================================================
#pragma once

#include <stdint.h>
#include <time.h>
#include "config.h"

class Histogram
{
public:
    static const uint8_t kBuckets = NUM_BUCKETS;

    // Anchor the histogram to the given local date and clear both days.
    void resetForDate(const struct tm &t);

    // True when t falls on the same local day the histogram is anchored to.
    bool sameDate(const struct tm &t) const;

    // Increment the bucket t belongs to (today's or yesterday's array).
    // Caller must have called resetForDate()/rolloverIfNeeded() first
    // (rebuild path) or use record().
    void add(const struct tm &t);
    void addYesterday(const struct tm &t);

    // Convenience: rollover at midnight if needed, then count one drop.
    void record(const struct tm &t);

    // Call once per second from the loop; at midnight, today's counts move
    // to the yesterday array and today starts empty.
    void rolloverIfNeeded(const struct tm &now);

    void clear();

    uint32_t total() const;             // drops in the anchored (today) day
    uint16_t max() const;               // tallest bucket across BOTH days
    uint16_t bucket(uint8_t i) const;   // today, 0..kBuckets-1
    uint16_t bucketYesterday(uint8_t i) const;
    const uint16_t *buckets() const { return _bToday; }
    const uint16_t *bucketsYesterday() const { return _bYesterday; }

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
    bool     _anchored = false;
    uint8_t  _year = 0;     // tm_year (years since 1900)
    uint8_t  _mon  = 0;     // 0..11
    uint8_t  _mday = 0;     // 1..31
};
