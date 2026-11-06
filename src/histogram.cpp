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

void Histogram::add(const struct tm &t, uint16_t volUl)
{
    const uint8_t idx = indexFor(t);
    if (_bToday[idx] < 0xFFFF)
        ++_bToday[idx];
    _volTodayUl += volUl;
}

void Histogram::addYesterday(const struct tm &t, uint16_t volUl)
{
    const uint8_t idx = indexFor(t);
    if (_bYesterday[idx] < 0xFFFF)
        ++_bYesterday[idx];
    _volYesterdayUl += volUl;
}

void Histogram::addRolling(time_t epochSec, uint16_t volUl)
{
    if (epochSec < TIME_VALID_EPOCH)
        return;

    if (_firstEpochSec == 0 || epochSec < _firstEpochSec)
        _firstEpochSec = epochSec;

    _totalLogVolUl += volUl;
    ++_totalLogDrops;

    // 1) 1-minute slot (Last Hour)
    const uint32_t mTag = (uint32_t)(epochSec / 60);
    const uint8_t  mIdx = (uint8_t)(mTag % kMinSlots);
    if (_minTag[mIdx] != mTag)
    {
        _minTag[mIdx]   = mTag;
        _minVolUl[mIdx] = 0;
        _minDrops[mIdx] = 0;
    }
    _minVolUl[mIdx] += volUl;
    if (_minDrops[mIdx] < 0xFFFF)
        ++_minDrops[mIdx];

    // 2) 15-minute slot (Last 24h & Prev 24h)
    const uint32_t qTag = (uint32_t)(epochSec / 900);
    const uint16_t qIdx = (uint16_t)(qTag % kQtrSlots);
    if (_qtrTag[qIdx] != qTag)
    {
        _qtrTag[qIdx]   = qTag;
        _qtrVolUl[qIdx] = 0;
        _qtrDrops[qIdx] = 0;
    }
    _qtrVolUl[qIdx] += volUl;
    if (_qtrDrops[qIdx] < 0xFFFF)
        ++_qtrDrops[qIdx];
}

void Histogram::clearRolling()
{
    memset(_minTag,   0, sizeof(_minTag));
    memset(_minVolUl, 0, sizeof(_minVolUl));
    memset(_minDrops, 0, sizeof(_minDrops));
    memset(_qtrTag,   0, sizeof(_qtrTag));
    memset(_qtrVolUl, 0, sizeof(_qtrVolUl));
    memset(_qtrDrops, 0, sizeof(_qtrDrops));
    _firstEpochSec = 0;
    _totalLogVolUl = 0;
    _totalLogDrops = 0;
}

void Histogram::record(const struct tm &t, time_t epochSec, uint16_t volUl)
{
    rolloverIfNeeded(t);
    if (!_anchored)
        resetForDate(t);
    add(t, volUl);
    if (epochSec >= TIME_VALID_EPOCH)
        addRolling(epochSec, volUl);
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
    _volYesterdayUl = _volTodayUl;
    _volTodayUl     = 0;
    _year = (uint8_t)now.tm_year;
    _mon  = (uint8_t)now.tm_mon;
    _mday = (uint8_t)now.tm_mday;
}

void Histogram::clear()
{
    memset(_bToday, 0, sizeof(_bToday));
    memset(_bYesterday, 0, sizeof(_bYesterday));
    _volTodayUl     = 0;
    _volYesterdayUl = 0;
    clearRolling();
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

uint32_t Histogram::lastHourVolUl(time_t nowSec) const
{
    if (nowSec < TIME_VALID_EPOCH)
        return 0;
    const uint32_t nowMin = (uint32_t)(nowSec / 60);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < kMinSlots; ++i)
    {
        if (_minTag[i] != 0 && nowMin >= _minTag[i] && (nowMin - _minTag[i]) < 60)
            sum += _minVolUl[i];
    }
    return sum;
}

uint32_t Histogram::lastHourDrops(time_t nowSec) const
{
    if (nowSec < TIME_VALID_EPOCH)
        return 0;
    const uint32_t nowMin = (uint32_t)(nowSec / 60);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < kMinSlots; ++i)
    {
        if (_minTag[i] != 0 && nowMin >= _minTag[i] && (nowMin - _minTag[i]) < 60)
            sum += _minDrops[i];
    }
    return sum;
}

uint32_t Histogram::last24hVolUl(time_t nowSec) const
{
    if (nowSec < TIME_VALID_EPOCH)
        return 0;
    const uint32_t nowQtr = (uint32_t)(nowSec / 900);
    uint32_t sum = 0;
    for (uint16_t i = 0; i < kQtrSlots; ++i)
    {
        if (_qtrTag[i] != 0 && nowQtr >= _qtrTag[i] && (nowQtr - _qtrTag[i]) < 96)
            sum += _qtrVolUl[i];
    }
    return sum;
}

uint32_t Histogram::last24hDrops(time_t nowSec) const
{
    if (nowSec < TIME_VALID_EPOCH)
        return 0;
    const uint32_t nowQtr = (uint32_t)(nowSec / 900);
    uint32_t sum = 0;
    for (uint16_t i = 0; i < kQtrSlots; ++i)
    {
        if (_qtrTag[i] != 0 && nowQtr >= _qtrTag[i] && (nowQtr - _qtrTag[i]) < 96)
            sum += _qtrDrops[i];
    }
    return sum;
}

uint32_t Histogram::prev24hVolUl(time_t nowSec) const
{
    if (nowSec < TIME_VALID_EPOCH)
        return 0;
    const uint32_t nowQtr = (uint32_t)(nowSec / 900);
    uint32_t sum = 0;
    for (uint16_t i = 0; i < kQtrSlots; ++i)
    {
        if (_qtrTag[i] != 0 && nowQtr >= _qtrTag[i])
        {
            const uint32_t age = nowQtr - _qtrTag[i];
            if (age >= 96 && age < 192)
                sum += _qtrVolUl[i];
        }
    }
    return sum;
}

uint32_t Histogram::logSpanSeconds(time_t nowSec) const
{
    if (_firstEpochSec == 0 || nowSec <= _firstEpochSec)
        return 0;
    return (uint32_t)(nowSec - _firstEpochSec);
}

uint32_t Histogram::rolling24hAvgVolUl(time_t nowSec) const
{
    if (_firstEpochSec == 0 || _totalLogVolUl == 0 || nowSec < TIME_VALID_EPOCH)
        return 0;
    double spanSec = (double)(nowSec > _firstEpochSec ? (nowSec - _firstEpochSec) : 0);
    // Floor the span at 15 minutes (900s) so a brand-new log doesn't divide by ~1s.
    if (spanSec < 900.0)
        spanSec = 900.0;
    const double days = spanSec / 86400.0;
    return (uint32_t)((double)_totalLogVolUl / days + 0.5);
}
