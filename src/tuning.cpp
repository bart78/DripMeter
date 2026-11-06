// ============================================================================
//  tuning.cpp — implementation
// ============================================================================
#include "tuning.h"
#include "config.h"
#include <Preferences.h>

namespace
{
    // NVS namespace bumped to "drip2" for the powered-piezo envelope architecture
    // so stale unpowered-piezo holdoffs (e.g. rel=800ms) don't carry over.
    const char *NVS_NS   = "drip2";
    const char *KEY_MRG  = "margin";
    const char *KEY_NMT  = "noisemult";
    const char *KEY_CAPW = "captwin";
    const char *KEY_RELS = "release";
    const char *KEY_REFR = "refract";
    const char *KEY_QEN  = "q_en";
    const char *KEY_QULD = "q_uld";
    const char *KEY_QENV = "q_env";
}

void Tuning::load(DropSampler &s)
{
    Preferences p;
    if (p.begin(NVS_NS, true))
    {
        const uint16_t m    = p.getUShort(KEY_MRG, ENV_MIN_MARGIN);
        const uint8_t  n    = p.getUChar(KEY_NMT, TRIGGER_NOISE_MULT);
        const uint16_t capw = p.getUShort(KEY_CAPW, CAPTURE_WINDOW_MS);
        const uint16_t rels = p.getUShort(KEY_RELS, RELEASE_MS);
        const uint16_t refr = p.getUShort(KEY_REFR, REFRACTORY_MS);
        const bool     qen  = p.getBool(KEY_QEN, DEFAULT_QTY_ENABLED != 0);
        const uint16_t quld = p.getUShort(KEY_QULD, DEFAULT_UL_TENTHS);
        const uint16_t qenv = p.getUShort(KEY_QENV, DEFAULT_CAL_ENV_TENTHS);
        p.end();

        s.setMargin((m >= 5 && m <= 500) ? m : ENV_MIN_MARGIN);
        s.setNoiseMult((n <= 20) ? n : TRIGGER_NOISE_MULT);
        s.setCaptureWindowMs((capw >= 20 && capw <= 300) ? capw : CAPTURE_WINDOW_MS);
        s.setReleaseMs((rels >= 40 && rels <= 1000) ? rels : RELEASE_MS);
        s.setRefractoryMs((refr >= 80 && refr <= 2000) ? refr : REFRACTORY_MS);
        s.setQtyEnabled(qen);
        s.setUlPerDropTenths((quld >= 50 && quld <= 5000) ? quld : DEFAULT_UL_TENTHS);
        s.setCalAvgEnvTenths((qenv >= 10 && qenv <= 10000) ? qenv : DEFAULT_CAL_ENV_TENTHS);

        Serial.printf("[TUNE] margin=%u nmult=%u cap=%ums rel=%ums refr=%ums qty=%d uld=%.1fuL\n",
                      s.margin(), s.noiseMult(),
                      s.captureWindowMs(), s.releaseMs(), s.refractoryMs(),
                      (int)s.qtyEnabled(), (float)s.ulPerDropTenths() * 0.1f);
    }
}

void Tuning::save(DropSampler &s)
{
    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        p.putUShort(KEY_MRG, s.margin());
        p.putUChar(KEY_NMT, s.noiseMult());
        p.putUShort(KEY_CAPW, s.captureWindowMs());
        p.putUShort(KEY_RELS, s.releaseMs());
        p.putUShort(KEY_REFR, s.refractoryMs());
        p.putBool(KEY_QEN, s.qtyEnabled());
        p.putUShort(KEY_QULD, s.ulPerDropTenths());
        p.putUShort(KEY_QENV, s.calAvgEnvTenths());
        p.end();
        DBG_PRINTF("[TUNE] saved margin=%u nmult=%u cap=%ums rel=%ums refr=%ums qty=%d\n",
                   s.margin(), s.noiseMult(),
                   s.captureWindowMs(), s.releaseMs(), s.refractoryMs(),
                   (int)s.qtyEnabled());
    }
}

void Tuning::reset(DropSampler &s)
{
    s.setMargin(ENV_MIN_MARGIN);
    s.setNoiseMult(TRIGGER_NOISE_MULT);
    s.setCaptureWindowMs(CAPTURE_WINDOW_MS);
    s.setReleaseMs(RELEASE_MS);
    s.setRefractoryMs(REFRACTORY_MS);
    save(s);
}
