// ============================================================================
//  drop_template.cpp — implementation
// ============================================================================
#include "drop_template.h"
#include <LittleFS.h>
#include <math.h>
#include <string.h>

namespace
{
    const uint32_t TPL_MAGIC = 0x44524F50UL;   // "DROP"
}

void DropTemplate::learn(const int16_t *buf, uint16_t len, uint16_t fs)
{
    if (!buf || len == 0)
        return;
    if (len > kLen)
        len = kLen;

    // DC-remove.
    float mean = 0.0f;
    for (uint16_t i = 0; i < len; ++i)
        mean += buf[i];
    mean /= (float)len;

    // Normalize to a fixed RMS (scale = 1024).
    float rms = 0.0f;
    for (uint16_t i = 0; i < len; ++i)
    {
        const float f = (float)buf[i] - mean;
        rms += f * f;
    }
    rms = sqrtf(rms / (float)len);
    if (rms < 1.0f)
        return;                         // silence: keep the old template

    const float scale = 1024.0f / rms;
    memset(_t, 0, sizeof(_t));
    for (uint16_t i = 0; i < len; ++i)
        _t[i] = (int16_t)lroundf(((float)buf[i] - mean) * scale);

    _fs = fs ? fs : 20000;
    _valid = true;
    _tNorm = sqrtf((float)kLen) * 1024.0f;

    DBG_PRINTF("[TPL] learned template (%u samples @ %u S/s)\n", len, fs);
}

float DropTemplate::match(const int16_t *win, float minRms) const
{
    if (!_valid || !win)
        return 0.0f;

    float mean = 0.0f;
    for (uint16_t i = 0; i < kLen; ++i)
        mean += win[i];
    mean /= (float)kLen;

    float dot = 0.0f, wnorm = 0.0f;
    for (uint16_t i = 0; i < kLen; ++i)
    {
        const float f = (float)win[i] - mean;
        dot   += f * _t[i];
        wnorm += f * f;
    }

    // Energy gate: silent windows produce unstable normalized scores.
    const float rms = sqrtf(wnorm / (float)kLen);
    if (rms < minRms || rms <= 0.0f)
        return 0.0f;
    return dot / (sqrtf(wnorm) * _tNorm);
}

// ---------------------------------------------------------------------------
// Robust learning: one capture, peak-aligned + normalized, merged into the
// running average.
// ---------------------------------------------------------------------------
void DropTemplate::addCapture(const int16_t *buf, uint16_t len, uint16_t fs)
{
    if (!buf || len == 0)
        return;
    if (len > kLen)
        len = kLen;

    // 1) Locate the loudest sample — the onset anchor.
    int16_t  pmax = 0;
    uint16_t pidx = 0;
    for (uint16_t i = 0; i < len; ++i)
    {
        const int16_t a = buf[i] < 0 ? (int16_t)-buf[i] : buf[i];
        if (a > pmax)
        {
            pmax = a;
            pidx = i;
        }
    }
    _lastPeak = pmax;
    if (pmax < 8)
    {
        DBG_PRINTF("[TPL] capture rejected (peak %d = silence/garbage)\n", pmax);
        return;
    }

    // 2) Time-align: shift so the peak sits at kAlign. Samples that fall
    //    outside the window become zero (fine for averaging).
    int16_t tmp[kLen];
    for (uint16_t i = 0; i < kLen; ++i)
    {
        const int32_t src = (int32_t)i + (int32_t)pidx - (int32_t)kAlign;
        tmp[i] = (src >= 0 && src < (int32_t)kLen) ? buf[src] : 0;
    }

    // 3) DC-remove + RMS-normalize (scale = 1024).
    float mean = 0.0f;
    for (uint16_t i = 0; i < kLen; ++i)
        mean += tmp[i];
    mean /= (float)kLen;

    float rms = 0.0f;
    for (uint16_t i = 0; i < kLen; ++i)
    {
        const float f = (float)tmp[i] - mean;
        rms += f * f;
    }
    rms = sqrtf(rms / (float)kLen);
    if (rms < 1.0f)
        return;

    const float scale = 1024.0f / rms;

    // 4) Merge into the running average.
    for (uint16_t i = 0; i < kLen; ++i)
        _acc[i] += ((float)tmp[i] - mean) * scale;
    if (_avgCount < 255)
        ++_avgCount;

    // 5) Commit the averaged template (clamped to int16).
    float norm2 = 0.0f;
    for (uint16_t i = 0; i < kLen; ++i)
    {
        float v = _acc[i] / (float)_avgCount;
        if (v > 30000.0f) v = 30000.0f;
        if (v < -30000.0f) v = -30000.0f;
        _t[i] = (int16_t)lroundf(v);
        norm2 += (float)_t[i] * (float)_t[i];
    }

    _fs = fs ? fs : 20000;
    _valid = true;
    _tNorm = sqrtf(norm2);
    if (_tNorm < 1.0f)
        _tNorm = 1.0f;

    DBG_PRINTF("[TPL] capture #%u merged (peak %d, %u S/s)\n",
                  _avgCount, pmax, fs);
}

bool DropTemplate::save() const
{
    if (!_valid)
        return false;
    fs::File f = LittleFS.open(_path, "w");
    if (!f)
        return false;
    f.write((const uint8_t *)&TPL_MAGIC, 4);
    const uint16_t len = kLen;
    f.write((const uint8_t *)&len, 2);
    f.write((const uint8_t *)&_fs, 2);
    f.write((const uint8_t *)&_avgCount, 1);
    f.write((const uint8_t *)&_lastPeak, 2);
    f.write((const uint8_t *)_t, sizeof(_t));
    f.close();
    return true;
}

bool DropTemplate::load()
{
    fs::File f = LittleFS.open(_path, "r");
    if (!f)
        return false;

    uint32_t magic = 0;
    uint16_t len = 0, fsRate = 0;
    if (f.read((uint8_t *)&magic, 4) != 4 || magic != TPL_MAGIC)
    {
        f.close();
        return false;
    }
    f.read((uint8_t *)&len, 2);
    f.read((uint8_t *)&fsRate, 2);
    if (len != kLen || fsRate < 1000)
    {
        f.close();
        return false;
    }
    // Capture count + last peak (newer file format; older files simply
    // lack them — treat as unknown).
    const size_t expectNew = 4 + 2 + 2 + 1 + 2 + sizeof(_t);
    if (f.size() >= expectNew)
    {
        f.read((uint8_t *)&_avgCount, 1);
        f.read((uint8_t *)&_lastPeak, 2);
    }
    else
    {
        _avgCount = 0;
        _lastPeak = 0;
    }
    if (f.read((uint8_t *)_t, sizeof(_t)) != sizeof(_t))
    {
        f.close();
        return false;
    }
    f.close();

    _fs = fsRate;
    _valid = true;
    _tNorm = sqrtf((float)kLen) * 1024.0f;
    Serial.printf("[TPL] loaded template from %s (%u S/s)\n", _path, _fs);
    return true;
}

void DropTemplate::forget()
{
    _valid = false;
    _avgCount = 0;
    _lastPeak = 0;
    memset(_t, 0, sizeof(_t));
    memset(_acc, 0, sizeof(_acc));
    _tNorm = 1.0f;
    LittleFS.remove(_path);
    DBG_PRINTLN("[TPL] template forgotten");
}
