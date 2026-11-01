// ============================================================================
//  tuning.h — persistent sensitivity calibration (NVS-backed)
//
//  The detection margin / noise multiplier are runtime-tunable (via the web
//  page) and survive reboots via Preferences (NVS).
// ============================================================================
#pragma once

#include "drop_sampler.h"

namespace Tuning
{
    // Load saved values (or defaults) into the sampler. Call once in setup.
    void load(DropSampler &s);

    // Persist the sampler's current values. Call after a /set request.
    void save(DropSampler &s);

    // Reset to compile-time defaults and persist.
    void reset(DropSampler &s);
}
