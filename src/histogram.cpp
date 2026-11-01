// ============================================================================
//  histogram.cpp — implementation
// ============================================================================
#include "histogram.h"
#include <string.h>

void Histogram::resetForDate(const struct tm &t)
{
    clear();
    _year = (uint8_t)t.tm_year;
    _mon  = (uint8_t)t.tm_mon;
    _mday = (uint8_t)t.tm_mday;
    _anchored = true;
}

bool Histogram::sameDate(const struct tm &t) const
{
    return _anchored &&
           (uint8_t)t.tm_year == _year &&
           (uint8_t)t.tm_mon  == _mon &&
           (uint8_t)t.tm_mday == _mday;
}

void Histogram::add(const struct tm &t)
{
    const uint8_t idx = indexFor(t);
    if (_bToday[idx] < 0xFFFF)
        ++_bToday[idx];
}

void Histogram::addYesterday(const struct tm &t)
{
    const uint8_t idx = indexFor(t);
    if (_bYesterday[idx] < 0xFFFF)
        ++_bYesterday[idx];
}

void Histogram::record(const struct tm &t)
{
    rolloverIfNeeded(t);
    if (!_anchored)
        resetForDate(t);
    add(t);
}

void Histogram::rolloverIfNeeded(const struct tm &now)
{
    if (!_anchored)
        return;
    if (sameDate(now))
        return;

    // Midnight: yesterday's data becomes the dim comparison layer, today
    // starts fresh. (The CSV log keeps the full history either way.)
    memcpy(_bYesterday, _bToday, sizeof(_bToday));
    memset(_bToday, 0, sizeof(_bToday));
    _year = (uint8_t)now.tm_year;
    _mon  = (uint8_t)now.tm_mon;
    _mday = (uint8_t)now.tm_mday;
}

void Histogram::clear()
{
    memset(_bToday, 0, sizeof(_bToday));
    memset(_bYesterday, 0, sizeof(_bYesterday));
}

uint32_t Histogram::total() const
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < kBuckets; ++i)
        sum += _bToday[i];
    return sum;
}

uint16_t Histogram::max() const
{
    uint16_t m = 0;
    for (uint8_t i = 0; i < kBuckets; ++i)
    {
        if (_bToday[i] > m)
            m = _bToday[i];
        if (_bYesterday[i] > m)
            m = _bYesterday[i];
    }
    return m;
}

uint16_t Histogram::bucket(uint8_t i) const
{
    return (i < kBuckets) ? _bToday[i] : 0;
}

uint16_t Histogram::bucketYesterday(uint8_t i) const
{
    return (i < kBuckets) ? _bYesterday[i] : 0;
}
