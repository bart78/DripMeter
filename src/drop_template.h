// ============================================================================
//  drop_template.h — learned drop signature for matched-filter detection
//
//  A real drop's piezo response is recorded once (web: /learn), normalized
//  (DC removed, unit energy) and persisted to LittleFS as /template.bin.
//  Detection in MATCH mode runs a sliding normalized cross-correlation of
//  the live stream against this template — the optimal linear detector for
//  a known signal in noise, far more selective than any amplitude knob.
// ============================================================================
#pragma once

#include <stdint.h>
#include "config.h"

class DropTemplate
{
public:
    static const uint16_t kLen = 512;      // samples (~30 ms at 16.7 kS/s)
    static const uint16_t kAlign = 128;    // peak anchor inside the window
                                            // (captures AND live windows are
                                            // peak-aligned to this before
                                            // correlation)

    bool valid() const { return _valid; }
    uint16_t sampleRate() const { return _fs; }

    // Build a normalized template from a raw int16 capture.
    void learn(const int16_t *buf, uint16_t len, uint16_t fs);

    // Robust learning: peak-align + normalize one capture and MERGE it into
    // a running average (up to 8 captures). Imperfect single drops are fine
    // — repeat /learn until the average settles.
    void addCapture(const int16_t *buf, uint16_t len, uint16_t fs);

    uint8_t  avgCount() const { return _avgCount; }
    uint16_t lastCapturePeak() const { return _lastPeak; }

    // Normalized cross-correlation of a window against the template,
    // range [-1, 1]. Returns 0 when no template is loaded or when the
    // window's RMS is below minRms (energy gate: normalized correlation is
    // numerically meaningless on near-silent windows).
    float match(const int16_t *win, float minRms = 0.0f) const;

    // Persistence (LittleFS; path configurable so drop and noise templates
    // can coexist as separate files).
    bool load();
    bool save() const;
    void forget();
    void setPath(const char *p) { _path = p; }

    const int16_t *data() const { return _t; }

private:
    const char *_path = "/template.bin";
    int16_t  _t[kLen] = {0};
    float    _acc[kLen] = {0.0f};        // running capture average
    float    _tNorm = 1.0f;              // |template| after normalization
    uint16_t _fs = 20000;
    uint8_t  _avgCount = 0;
    uint16_t _lastPeak = 0;
    bool     _valid = false;
};
