// ============================================================================
//  dashboard.cpp — implementation
// ============================================================================
#include "dashboard.h"
#include "time_service.h"

#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Fonts (u8g2, auto-used by Arduino_GFX once <U8g2lib.h> is available)
// ---------------------------------------------------------------------------
static const uint8_t *const F_CLOCK = u8g2_font_helvB24_tf;    // status clock
static const uint8_t *const F_NUM   = u8g2_font_logisoso24_tf; // big numbers
static const uint8_t *const F_MED   = u8g2_font_helvB14_tf;    // medium text
static const uint8_t *const F_SMALL = u8g2_font_helvB10_tf;    // labels
static const uint8_t *const F_TINY  = u8g2_font_6x10_tf;       // footer

// ---------------------------------------------------------------------------
// Palette (dark, high-contrast)
// ---------------------------------------------------------------------------
#define C_BG      rgb(14, 17, 22)
#define C_PANEL   rgb(23, 28, 36)
#define C_PANEL2  rgb(29, 36, 46)
#define C_BORDER  rgb(44, 54, 68)
#define C_TEXT    rgb(234, 240, 246)
#define C_DIM     rgb(135, 148, 165)
#define C_ACCENT  rgb(57, 194, 255)
#define C_BAR     rgb(47, 168, 232)
#define C_BAR_HI  rgb(96, 210, 255)
#define C_NOW     rgb(255, 82, 82)
#define C_OK      rgb(67, 209, 124)
#define C_WARN    rgb(255, 176, 46)
#define C_GRID    rgb(35, 43, 54)
#define C_BAR_DIM rgb(46, 58, 76)      // yesterday ghost bars
#define C_ADC     rgb(249, 115, 22)    // today avg PeakADC strength line
#define C_ADC_DIM rgb(154, 91, 37)     // yesterday avg PeakADC strength line

// ---------------------------------------------------------------------------
// Geometry (320x480 portrait)
// ---------------------------------------------------------------------------
#define STATUS_H       46
#define STATS_Y        54
#define STATS_H        64
#define TILE_W         100
#define TILE_X0        6
#define TITLE_Y        128
#define PLOT_X0        10
#define PLOT_X1        310
#define PLOT_Y0        146
#define PLOT_Y1        456
#define HOUR_LABEL_Y   458
#define FOOTER_Y       468

bool Dashboard::begin()
{
    // Fixed QSPI wiring of the JC3248W535 (CS, SCK, D0..D3).
    _bus = new Arduino_ESP32QSPI(
        PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);

    // The panel needs no external reset line (GFX_NOT_DEFINED); the driver
    // issues the AXS15231B init sequence itself.
    _panel = new Arduino_AXS15231B(
        _bus, GFX_NOT_DEFINED, LCD_ROTATION, false, LCD_WIDTH, LCD_HEIGHT);

    // Full-frame canvas — its 300 KB framebuffer lands in PSRAM.
    _gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, _panel, 0, 0, LCD_ROTATION);

    if (!_gfx->begin())
    {
        Serial.println("[UI] display init FAILED");
        return false;
    }
    _gfx->setRotation(LCD_ROTATION);

    // Splash — the rest of setup() may take a few seconds (Wi-Fi etc.).
    _gfx->fillScreen(C_BG);
    textCenter(160, 170, F_CLOCK, C_TEXT, "DRIP METER");
    textCenter(160, 224, F_MED,   C_DIM,  "acoustic leak monitor");
    textCenter(160, 246, F_MED,   C_DIM,  "booting ...");
    _gfx->flush();

    _ok = true;
    Serial.println("[UI] display ready (320x480, AXS15231B QSPI)");
    return true;
}

void Dashboard::noteEvent()
{
    _eventPending = true;
    _lastEventMs  = millis();
}

void Dashboard::renderIfDue(const DashSnapshot &s)
{
    if (!_ok)
        return;

    const uint32_t now = millis();
    const bool periodic = (now - _lastRenderMs) >= UI_REFRESH_MS;
    const bool eventDriven = _eventPending &&
                             (now - _lastEventMs) >= UI_EVENT_REDRAW_MIN_MS;

    if (periodic || eventDriven)
    {
        _eventPending = false;
        render(s);
        _lastRenderMs = now;
    }
}

void Dashboard::render(const DashSnapshot &s)
{
    if (!_ok)
        return;
    _gfx->fillScreen(C_BG);
    drawStatusBar(s);
    drawStats(s);
    drawHistogram(s);
    drawFooter(s);
    _gfx->flush();
}

uint8_t Dashboard::fontAscent(const uint8_t *font)
{
    u8g2_t u8g2;
    memset(&u8g2, 0, sizeof(u8g2));
    u8g2_SetFont(&u8g2, font);
    return u8g2_GetAscent(&u8g2);
}

int16_t Dashboard::strWidth(const uint8_t *font, const char *s)
{
    u8g2_t u8g2;
    memset(&u8g2, 0, sizeof(u8g2));
    u8g2_SetFont(&u8g2, font);
    return (int16_t)u8g2_GetUTF8Width(&u8g2, s);
}

void Dashboard::textTop(int16_t x, int16_t yTop, const uint8_t *font,
                        uint16_t color, const char *s)
{
    _gfx->setFont(font);
    _gfx->setTextColor(color);
    _gfx->setCursor(x, yTop + fontAscent(font));
    _gfx->print(s);
}

void Dashboard::textRight(int16_t xRight, int16_t yTop, const uint8_t *font,
                          uint16_t color, const char *s)
{
    textTop(xRight - strWidth(font, s), yTop, font, color, s);
}

void Dashboard::textCenter(int16_t cx, int16_t yTop, const uint8_t *font,
                           uint16_t color, const char *s)
{
    textTop(cx - strWidth(font, s) / 2, yTop, font, color, s);
}

void Dashboard::drawStatusBar(const DashSnapshot &s)
{
    char buf[48];

    _gfx->fillRect(0, 0, LCD_WIDTH, STATUS_H, C_PANEL);
    _gfx->drawFastHLine(0, STATUS_H - 1, LCD_WIDTH, C_BORDER);

    // Clock (left). "--:--:--" until the first successful NTP sync.
    if (s.timeValid)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 s.now.tm_hour, s.now.tm_min, s.now.tm_sec);
    else
        snprintf(buf, sizeof(buf), "--:--:--");
    textTop(8, 4, F_CLOCK, C_TEXT, buf);

    // Date (left, under the clock).
    if (s.timeValid)
    {
        char dbuf[24];
        if (strftime(dbuf, sizeof(dbuf), "%a %Y-%m-%d", &s.now) > 0)
            textTop(10, 32, F_TINY, C_DIM, dbuf);
    }

    // Wi-Fi + IP (right, line 1); fallback = the device's own hotspot.
    if (s.wifiUp)
        snprintf(buf, sizeof(buf), "WIFI %s", s.ip[0] ? s.ip : "-");
    else
        snprintf(buf, sizeof(buf), "AP 192.168.4.1");
    textRight(312, 6, F_SMALL, s.wifiUp ? C_OK : C_WARN, buf);

    // NTP + storage (right, line 2).
    if (s.fsOk)
        snprintf(buf, sizeof(buf), "%s  FS %.1fM",
                 s.ntpOk ? "NTP OK" : "NTP ..",
                 (float)s.fsFreeBytes / (1024.0f * 1024.0f));
    else
        snprintf(buf, sizeof(buf), "%s  FS ERR",
                 s.ntpOk ? "NTP OK" : "NTP ..");
    textRight(312, 24, F_SMALL, C_DIM, buf);
}

void Dashboard::drawStats(const DashSnapshot &s)
{
    char buf[32];

    const struct { const char *label; } tiles[3] = {
        {"TODAY"}, {"LAST DRIP"}, {"PEAK ADC"}
    };
    const int16_t tileX[3] = {TILE_X0, TILE_X0 + TILE_W + 4, TILE_X0 + 2 * (TILE_W + 4)};

    for (uint8_t i = 0; i < 3; ++i)
    {
        const int16_t x = tileX[i];
        _gfx->fillRoundRect(x, STATS_Y, TILE_W, STATS_H, 8, C_PANEL);
        _gfx->drawRoundRect(x, STATS_Y, TILE_W, STATS_H, 8, C_BORDER);
        textCenter(x + TILE_W / 2, STATS_Y + 6, F_SMALL, C_DIM, tiles[i].label);

        switch (i)
        {
        case 0:     // drops recorded today
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)s.dropsToday);
            textCenter(x + TILE_W / 2, STATS_Y + 26, F_NUM, C_ACCENT, buf);
            break;

        case 1:     // time since the last impact ("4m 12s")
        {
            if (!s.haveLastDrop)
            {
                snprintf(buf, sizeof(buf), "--");
            }
            else
            {
                const int64_t nowUs = TimeService::epochUs();
                int64_t secs = (nowUs - s.lastDropEpochUs) / 1000000LL;
                if (secs < 0)
                    secs = 0;
                if (secs < 60)
                    snprintf(buf, sizeof(buf), "%llds", (long long)secs);
                else if (secs < 3600)
                    snprintf(buf, sizeof(buf), "%lldm %llds",
                             (long long)(secs / 60), (long long)(secs % 60));
                else if (secs < 86400)
                    snprintf(buf, sizeof(buf), "%lldh %lldm",
                             (long long)(secs / 3600),
                             (long long)((secs % 3600) / 60));
                else
                    snprintf(buf, sizeof(buf), "%lldd",
                             (long long)(secs / 86400));
            }
            textCenter(x + TILE_W / 2, STATS_Y + 26, F_MED, C_TEXT, buf);
            break;
        }

        case 2:     // peak raw ADC of the last impact
            snprintf(buf, sizeof(buf), "%u", (unsigned)s.lastPeak);
            textCenter(x + TILE_W / 2, STATS_Y + 26, F_NUM, C_ACCENT, buf);
            break;
        }
    }
}

void Dashboard::drawHistogram(const DashSnapshot &s)
{
    char buf[48];

    // Title + legend for hourly bars + strength line.
    textTop(PLOT_X0, TITLE_Y, F_SMALL, C_DIM, "DROPS/HR & STRENGTH (24H)");
    textRight(PLOT_X1, TITLE_Y, F_TINY, C_ADC, "LINE=ADC");

    const int16_t plotH   = PLOT_Y1 - PLOT_Y0;            // 310 px
    const int16_t plotW   = PLOT_X1 - PLOT_X0;            // 300 px
    const int16_t usableH = plotH - 18;                   // 292 px (headroom for top labels)

    // Aggregate 96 x 15m buckets into 24 x 1-hour fat bars + weighted Avg PeakADC
    uint32_t todayH[24] = {0};
    uint32_t ydayH[24]  = {0};
    uint32_t adcTH[24]  = {0};
    uint32_t adcYH[24]  = {0};
    uint32_t trueMaxHr  = 0;
    uint32_t maxDrops   = 60;
    uint32_t maxAdc     = 80;
    uint32_t dayDropsT  = 0;
    uint32_t dayAdcSumT = 0;

    if (s.histo)
    {
        for (uint8_t h = 0; h < 24; ++h)
        {
            uint32_t tCnt = 0, yCnt = 0, tW = 0, yW = 0;
            for (uint8_t q = 0; q < 4; ++q)
            {
                const uint8_t idx = h * 4 + q;
                const uint16_t ct = s.histo->bucket(idx);
                const uint16_t cy = s.histo->bucketYesterday(idx);
                tCnt += ct;
                yCnt += cy;
                tW   += (uint32_t)ct * s.histo->bucketAvgAdc(idx);
                yW   += (uint32_t)cy * s.histo->bucketYesterdayAvgAdc(idx);
            }
            todayH[h] = tCnt;
            ydayH[h]  = yCnt;
            adcTH[h]  = tCnt ? ((tW + (tCnt / 2)) / tCnt) : 0;
            adcYH[h]  = yCnt ? ((yW + (yCnt / 2)) / yCnt) : 0;
            dayDropsT  += tCnt;
            dayAdcSumT += tW;
            if (tCnt > trueMaxHr) trueMaxHr = tCnt;
            if (yCnt > trueMaxHr) trueMaxHr = yCnt;
            if (tCnt > maxDrops)  maxDrops  = tCnt;
            if (yCnt > maxDrops)  maxDrops  = yCnt;
            if (adcTH[h] > maxAdc) maxAdc = adcTH[h];
            if (adcYH[h] > maxAdc) maxAdc = adcYH[h];
        }
    }
    maxDrops = ((maxDrops + 19) / 20) * 20;
    if (maxDrops < 60) maxDrops = 60;
    maxAdc = ((maxAdc + 24) / 25) * 25;
    if (maxAdc < 100) maxAdc = 100;

    // Plot background + quarter-height grid lines.
    _gfx->fillRect(PLOT_X0, PLOT_Y0, plotW, plotH, C_PANEL2);
    _gfx->drawRect(PLOT_X0, PLOT_Y0, plotW, plotH, C_BORDER);
    for (uint8_t q = 1; q < 4; ++q)
        _gfx->drawFastHLine(PLOT_X0 + 1, PLOT_Y0 + 16 + (usableH * q) / 4,
                            plotW - 2, C_GRID);

    // Top-left max drops/hr label & top-right average ADC strength label.
    snprintf(buf, sizeof(buf), "MAX %lu/H (YDAY=DIM)", (unsigned long)trueMaxHr);
    textTop(PLOT_X0 + 4, PLOT_Y0 + 3, F_TINY, C_DIM, buf);

    const uint32_t avgDayAdc = dayDropsT ? ((dayAdcSumT + (dayDropsT / 2)) / dayDropsT) : 0;
    snprintf(buf, sizeof(buf), "AVG %lu ADC", (unsigned long)avgDayAdc);
    textRight(PLOT_X1 - 4, PLOT_Y0 + 3, F_TINY, C_ADC, buf);

    // 24 hourly slots across 288 px (12 px per hour: 5px Yday + 5px Today + 2px gap).
    const int16_t baseX = PLOT_X0 + 6;                    // 16..304
    const int16_t slotW = 12;
    const int16_t barW  = 5;

    uint8_t nowIdx = 0;
    if (s.timeValid)
        nowIdx = Histogram::indexFor(s.now);
    const uint8_t nowHour = nowIdx / 4;

    for (uint8_t h = 0; h < 24; ++h)
    {
        const int16_t xSlot = baseX + h * slotW;
        const uint32_t cy = ydayH[h];
        if (cy > 0)
        {
            int16_t bh = (int16_t)((cy * (uint32_t)usableH) / maxDrops);
            if (bh < 2) bh = 2;
            if (bh > usableH) bh = usableH;
            _gfx->fillRect(xSlot, PLOT_Y1 - bh, barW, bh, C_BAR_DIM);
        }

        const uint32_t ct = todayH[h];
        if (ct > 0)
        {
            int16_t bh = (int16_t)((ct * (uint32_t)usableH) / maxDrops);
            if (bh < 2) bh = 2;
            if (bh > usableH) bh = usableH;
            _gfx->fillRect(xSlot + barW, PLOT_Y1 - bh, barW, bh,
                           (h == nowHour) ? C_BAR_HI : C_BAR);
        }
    }

    // Yesterday Avg Strength (PeakADC) dim amber overlay line.
    int16_t prevX = -1, prevY = -1;
    for (uint8_t h = 0; h < 24; ++h)
    {
        if (adcYH[h] == 0)
            continue;
        const int16_t cx = baseX + h * slotW + barW;
        int16_t cy = PLOT_Y1 - (int16_t)((adcYH[h] * (uint32_t)usableH) / maxAdc);
        if (cy < PLOT_Y0 + 16) cy = PLOT_Y0 + 16;
        if (prevX >= 0)
            _gfx->drawLine(prevX, prevY, cx, cy, C_ADC_DIM);
        prevX = cx;
        prevY = cy;
    }

    // Today Avg Strength (PeakADC) bright orange 2px line + node dots.
    prevX = -1;
    prevY = -1;
    for (uint8_t h = 0; h < 24; ++h)
    {
        if (adcTH[h] == 0)
            continue;
        const int16_t cx = baseX + h * slotW + barW;
        int16_t cy = PLOT_Y1 - (int16_t)((adcTH[h] * (uint32_t)usableH) / maxAdc);
        if (cy < PLOT_Y0 + 16) cy = PLOT_Y0 + 16;
        if (prevX >= 0)
        {
            _gfx->drawLine(prevX, prevY, cx, cy, C_ADC);
            _gfx->drawLine(prevX, prevY - 1, cx, cy - 1, C_ADC);
        }
        _gfx->fillCircle(cx, cy, 2, C_ADC);
        prevX = cx;
        prevY = cy;
    }

    // "Now" marker (only meaningful while the clock is synced).
    if (s.timeValid && nowIdx < Histogram::kBuckets)
    {
        const int16_t x = baseX + (nowIdx * (24 * slotW)) / Histogram::kBuckets;
        _gfx->drawFastVLine(x, PLOT_Y0 + 16, usableH, C_NOW);
        _gfx->fillTriangle(x - 3, PLOT_Y0 + 15, x + 3, PLOT_Y0 + 15, x, PLOT_Y0 + 21, C_NOW);
    }

    // Hour labels 00..24 (every 6th hour).
    for (uint8_t h = 0; h <= 4; ++h)
    {
        snprintf(buf, sizeof(buf), "%02d", h * 6);
        const int16_t cx = baseX + (h * 6) * slotW;
        if (h == 0)
            textTop(PLOT_X0, HOUR_LABEL_Y, F_TINY, C_DIM, buf);
        else if (h == 4)
            textRight(PLOT_X1, HOUR_LABEL_Y, F_TINY, C_DIM, buf);
        else
            textTop(cx - strWidth(F_TINY, buf) / 2, HOUR_LABEL_Y, F_TINY, C_DIM, buf);
    }
}

void Dashboard::drawFooter(const DashSnapshot &s)
{
    char buf[64];
    if (s.sampleRate > 0)
    {
        snprintf(buf, sizeof(buf),
                 "RAW %u  ENV %u / THR %u  TRIG %lu  %.1fkHz",
                 (unsigned)s.liveRaw, (unsigned)s.envelope,
                 (unsigned)s.threshold, (unsigned long)s.totalTriggers,
                 (float)s.sampleRate / 1000.0f);
    }
    else
    {
        snprintf(buf, sizeof(buf),
                 "RAW %u  ENV %u / THR %u  TRIG %lu  SYS:%s",
                 (unsigned)s.liveRaw, (unsigned)s.envelope,
                 (unsigned)s.threshold, (unsigned long)s.totalTriggers,
                 s.stage ? s.stage : "?");
    }
    textTop(PLOT_X0, FOOTER_Y, F_TINY, C_DIM, buf);
}
