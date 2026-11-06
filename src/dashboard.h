// ============================================================================
//  dashboard.h — 320x480 status dashboard on the AXS15231B QSPI display
//
//  Rendering strategy: everything is drawn into a PSRAM-backed
//  Arduino_Canvas (double-buffered, no flicker), then pushed to the panel
//  in one flush(). The AXS15231B's QSPI interface is known to be
//  intolerant of many small window updates, so full-frame pushes are the
//  correct pattern for this board.
// ============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include "config.h"
#include "histogram.h"

// Immutable snapshot of the world, handed to the renderer. The dashboard
// never reads shared state directly — the loop assembles this struct, so
// rendering stays deterministic and race-free.
struct DashSnapshot
{
    bool          timeValid = false;
    struct tm     now = {};            // local time (valid iff timeValid)
    bool          wifiUp = false;
    int8_t        wifiRssi = 0;
    char          ip[16] = {0};
    bool          ntpOk = false;
    bool          fsOk = false;
    size_t        fsFreeBytes = 0;
    size_t        fsTotalBytes = 0;
    bool          logOk = false;
    uint32_t      dropsToday = 0;
    const Histogram *histo = nullptr;
    bool          haveLastDrop = false;
    int64_t       lastDropEpochUs = 0;
    uint16_t      lastPeak = 0;
    uint16_t      liveRaw = 0;
    uint16_t      envelope = 0;
    uint16_t      threshold = 0;
    uint32_t      totalTriggers = 0;
    uint32_t      sampleRate = 0;
    const char   *stage = "boot";     // last completed setup stage
};

class Dashboard
{
public:
    // Build the QSPI bus / AXS15231B panel / PSRAM canvas chain and show
    // a splash screen. Returns false if the display could not start.
    bool begin();

    bool ok() const { return _ok; }

    // Request an out-of-schedule redraw (e.g. right after a drop event).
    void noteEvent();

    // Redraw on the UI_REFRESH_MS cadence, or sooner after noteEvent()
    // (debounced to UI_EVENT_REDRAW_MIN_MS). Non-blocking by design.
    void renderIfDue(const DashSnapshot &s);

    // Full immediate redraw.
    void render(const DashSnapshot &s);

private:
    // --- tiny text engine (u8g2 fonts through Arduino_GFX) -----------------
    // Arduino_GFX treats the cursor y as the glyph BASELINE for u8g2 fonts,
    // so helpers here place text by its TOP edge for predictable layouts.
    uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return _gfx->color565(r, g, b);
    }
    uint8_t  fontAscent(const uint8_t *font);
    int16_t  strWidth(const uint8_t *font, const char *s);
    void     textTop(int16_t x, int16_t yTop, const uint8_t *font,
                     uint16_t color, const char *s);
    void     textRight(int16_t xRight, int16_t yTop, const uint8_t *font,
                       uint16_t color, const char *s);
    void     textCenter(int16_t cx, int16_t yTop, const uint8_t *font,
                        uint16_t color, const char *s);

    // --- layout sections ---------------------------------------------------
    void drawStatusBar(const DashSnapshot &s);
    void drawStats(const DashSnapshot &s);
    void drawHistogram(const DashSnapshot &s);
    void drawFooter(const DashSnapshot &s);

    Arduino_DataBus    *_bus   = nullptr;
    Arduino_AXS15231B  *_panel = nullptr;
    Arduino_Canvas     *_gfx   = nullptr;

    bool     _ok = false;
    uint32_t _lastRenderMs = 0;
    uint32_t _lastEventMs  = 0;
    bool     _eventPending = false;
};
