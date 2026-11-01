#!/usr/bin/env python3
# ============================================================================
# floor_settings.py — run the FIRMWARE's exact trigger state machine over a
# recording and find the lowest settings that capture every simulated drop
# without false events.
# ============================================================================
import wave, math, sys
import numpy as np

WAV = sys.argv[1] if len(sys.argv) > 1 else 'data/drip (1).wav'
STEP, ENV_WIN = 16, 128

w = wave.open(WAV, 'rb')
rate = w.getframerate()
raw = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
n = len(raw)

cs  = np.concatenate([[0.0], np.cumsum(raw)])
cs2 = np.concatenate([[0.0], np.cumsum(raw ** 2)])
env = []
for i in range(ENV_WIN, n + 1, STEP):
    s  = cs[i] - cs[i - ENV_WIN]
    s2 = cs2[i] - cs2[i - ENV_WIN]
    mean = s / ENV_WIN
    var = s2 / ENV_WIN - mean * mean
    env.append(math.sqrt(max(var, 0.0)))
env = np.array(env) / 8.0          # raw units
env_t = np.arange(len(env)) * STEP / rate

# ground truth: the 10 strongest envelope peaks (the user's drops)
gate = max(3 * np.percentile(env, 50), 0.75)
refr_s = int(0.8 * rate / STEP)
cands, last = [], -10**9
for k, e in enumerate(env):
    if e > gate and k - last > refr_s:
        cands.append(k); last = k
peaks = sorted(sorted(cands, key=lambda k: -env[k])[:10])

def simulate(margin, release_ms, refr_ms, cap_ms):
    """Firmware logic: env trigger + capture + retriggerable release."""
    thr = margin                       # adaptive part adds <= margin; floor
    events = []
    armed, capturing, quiet_since = True, False, 0
    last_hit = -10**9
    cap_end = 0
    dt = STEP / rate * 1000.0          # ms per env sample
    t = 0.0
    for e in env:
        t += dt
        if capturing:
            if t - cap_end >= 0:
                capturing = False
                armed, quiet_since = False, 0
        elif not armed:
            if e < thr:
                if quiet_since == 0:
                    quiet_since = t
                elif t - quiet_since >= release_ms:
                    armed = True
            else:
                quiet_since = 0
        if armed and not capturing and e > thr and (t - last_hit) >= refr_ms:
            events.append(t / 1000.0)   # seconds
            last_hit = t
            capturing = True
            cap_end = t + cap_ms
    return events

def match_score(events, tol=0.6):
    hit = 0
    for p in peaks:
        tp = env_t[p]
        if any(abs(tp - e) < tol for e in events):
            hit += 1
    return hit, len(events)

print("settings sweep (margin_raw, release, refr, cap): drops captured / total events")
best = []
for margin in (2, 3, 4, 5, 6, 8, 10):
    for release in (100, 250, 500, 800):
        ev = simulate(margin, release, 200, 80)
        h, total = match_score(ev)
        mark = " <== lowest" if h == 10 and total == 10 and not best else ""
        if h == 10 and total == 10 and not best:
            best.append((margin, release))
        print(f"  m={margin:2d} rel={release:4d}ms refr=200 cap=80 : "
              f"{h:2d}/10 drops, {total:2d} events{mark}")

print("\nbest (10/10 drops, zero extras):", best)
