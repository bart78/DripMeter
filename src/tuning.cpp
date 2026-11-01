// ============================================================================
//  tuning.cpp — implementation
// ============================================================================
#include "tuning.h"
#include "config.h"
#include <Preferences.h>

namespace
{
    const char *modeName(uint8_t m)
    {
        switch (m)
        {
        case 1: return "MATCH";
        case 2: return "BOTH";
        case 3: return "EITHER";
        default: return "ENV";
        }
    }

    const char *NVS_NS  = "drip";
    const char *KEY_MRG = "margin";
    const char *KEY_NMT = "noisemult";
    const char *KEY_MODE = "mode";
    const char *KEY_MTHR = "matchthr";
    const char *KEY_MRMS = "mrm";
    const char *KEY_REFR = "refract";
    const char *KEY_CAPW = "captwin";
    const char *KEY_RELS = "release";
}

void Tuning::load(DropSampler &s)
{
    Preferences p;
    if (p.begin(NVS_NS, true))
    {
        const uint16_t m = p.getUShort(KEY_MRG, ENV_MIN_MARGIN);
        const uint8_t  n = p.getUChar(KEY_NMT, TRIGGER_NOISE_MULT);
        const uint8_t  mode = p.getUChar(KEY_MODE, 0);
        const uint16_t mthr = p.getUShort(KEY_MTHR, MATCH_THR_DEFAULT);
        const uint8_t  mrms = p.getUChar(KEY_MRMS, MATCH_MIN_RMS);
        const uint16_t refr = p.getUShort(KEY_REFR, REFRACTORY_MS);
        const uint16_t capw = p.getUShort(KEY_CAPW, CAPTURE_WINDOW_MS);
        const uint16_t rels = p.getUShort(KEY_RELS, RELEASE_MS);
        p.end();

        s.setMargin((m >= 3 && m <= 2000) ? m : ENV_MIN_MARGIN);
        s.setNoiseMult((n >= 2 && n <= 40) ? n : TRIGGER_NOISE_MULT);
        s.setDetectMode(mode);
        s.setMatchThr((mthr >= 30 && mthr <= 950) ? mthr : MATCH_THR_DEFAULT);
        s.setMatchMinRms((mrms >= 2 && mrms <= 20) ? mrms : MATCH_MIN_RMS);
        s.setRefractoryMs((refr >= 30 && refr <= 5000) ? refr : REFRACTORY_MS);
        s.setCaptureWindowMs((capw >= 20 && capw <= 1000) ? capw : CAPTURE_WINDOW_MS);
        s.setReleaseMs((rels >= 50 && rels <= 10000) ? rels : RELEASE_MS);

        Serial.printf("[TUNE] margin=%u nmult=%u mode=%s mthr=%u refr=%ums cap=%ums rel=%ums\n",
                      s.margin(), s.noiseMult(),
                      modeName(s.detectMode()), s.matchThr(),
                      s.refractoryMs(), s.captureWindowMs(), s.releaseMs());
    }
}

void Tuning::save(DropSampler &s)
{
    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        p.putUShort(KEY_MRG, s.margin());
        p.putUChar(KEY_NMT, s.noiseMult());
        p.putUChar(KEY_MODE, s.detectMode());
        p.putUShort(KEY_MTHR, s.matchThr());
        p.putUChar(KEY_MRMS, s.matchMinRms());
        p.putUShort(KEY_REFR, s.refractoryMs());
        p.putUShort(KEY_CAPW, s.captureWindowMs());
        p.putUShort(KEY_RELS, s.releaseMs());
        p.end();
        DBG_PRINTF("[TUNE] saved margin=%u nmult=%u mode=%s mthr=%u refr=%ums cap=%ums rel=%ums\n",
                      s.margin(), s.noiseMult(),
                      modeName(s.detectMode()), s.matchThr(),
                      s.refractoryMs(), s.captureWindowMs(), s.releaseMs());
    }
}

void Tuning::reset(DropSampler &s)
{
    s.setMargin(ENV_MIN_MARGIN);
    s.setNoiseMult(TRIGGER_NOISE_MULT);
    s.setDetectMode(0);
    s.setMatchThr(MATCH_THR_DEFAULT);
    s.setMatchMinRms(MATCH_MIN_RMS);
    s.setRefractoryMs(REFRACTORY_MS);
    s.setCaptureWindowMs(CAPTURE_WINDOW_MS);
    s.setReleaseMs(RELEASE_MS);
    save(s);
}
