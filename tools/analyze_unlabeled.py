#!/usr/bin/env python3
# ============================================================================
# analyze_unlabeled.py — find drops in an unlabeled session recording, learn
# a template the way the firmware does, and measure the discrimination.
#
# Uses the same pipeline as the firmware:
#   env = AC-RMS, 128-sample window, 16-sample steps
#   candidates = envelope peaks above noise x 3, refractory-separated
#   template = peak-aligned + normalized average of the strongest N
#   scores  = sliding peak-aligned correlation (+ energy gate)
# ============================================================================
import wave, math, sys
import numpy as np

WAV = sys.argv[1] if len(sys.argv) > 1 else 'data/drip (1).wav'
N_DROPS = int(sys.argv[2]) if len(sys.argv) > 2 else 10

KLEN, KALIGN, STEP, ENV_WIN = 512, 128, 16, 128

w = wave.open(WAV, 'rb')
rate = w.getframerate()
raw = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
n = len(raw)
print(f"{WAV}: {rate} Hz, {n/rate:.1f} s, peak {abs(raw).max():.0f}")

cs  = np.concatenate([[0.0], np.cumsum(raw)])
cs2 = np.concatenate([[0.0], np.cumsum(raw ** 2)])
env = []
for i in range(ENV_WIN, n + 1, STEP):
    s  = cs[i] - cs[i - ENV_WIN]
    s2 = cs2[i] - cs2[i - ENV_WIN]
    mean = s / ENV_WIN
    var = s2 / ENV_WIN - mean * mean
    env.append(math.sqrt(max(var, 0.0)))
env = np.array(env)
env_t = (np.arange(len(env)) * STEP + ENV_WIN) / rate

noise = np.percentile(env, 50)
print(f"noise median {noise:.1f}, env max {env.max():.1f}")

# candidate onsets: env above gate, refractory-separated
gate = max(3 * noise, 6.0)
refr = int(0.8 * rate / STEP)
cands = []
last = -10**9
for k, e in enumerate(env):
    if e > gate and k - last > refr:
        cands.append(k)
        last = k
# pick the N strongest by peak env
peaks = sorted(cands, key=lambda k: -env[k])[:N_DROPS]
peaks = sorted(peaks)
print(f"detected {len(cands)} candidates; kept {len(peaks)} strongest:")
for p in peaks:
    print(f"    drop @ {env_t[p]:7.2f}s  env peak {env[p]:6.1f}")

def capture_for(k):
    onset_sample = k * STEP + ENV_WIN
    a = onset_sample - 128
    if a < 0: a = 0
    return raw[a:a + KLEN]

def add_capture(buf):
    b = np.zeros(KLEN)
    if len(buf) < KLEN: return None
    pidx = int(np.argmax(np.abs(buf)))
    for i in range(KLEN):
        src = i + pidx - KALIGN
        if 0 <= src < KLEN:
            b[i] = buf[src]
    b = b - b.mean()
    r = np.sqrt((b ** 2).mean())
    if r < 1: return None
    return b * (1024.0 / r)

caps = [add_capture(capture_for(p)) for p in peaks]
caps = [c for c in caps if c is not None]
tmpl = np.mean(caps, axis=0)
tnorm = np.sqrt((tmpl ** 2).sum())
print(f"template from {len(caps)} captures")

# sliding score trace
nwin = (n - KLEN) // STEP + 1
scores = np.zeros(nwin)
wrms = np.zeros(nwin)
for i, s0 in enumerate(range(0, n - KLEN + 1, STEP)):
    win = raw[s0:s0 + KLEN]
    wrms[i] = np.sqrt((win ** 2).mean())
    pidx = int(np.argmax(np.abs(win)))
    a = np.zeros(KLEN)
    for j in range(KLEN):
        src = j + pidx - KALIGN
        if 0 <= src < KLEN:
            a[j] = win[src]
    a = a - a.mean()
    wnorm = np.sqrt((a ** 2).sum())
    if wnorm > 0:
        scores[i] = float(np.dot(a, tmpl) / (wnorm * tnorm)) * 1000.0
scores_t = np.arange(nwin) * STEP / rate

# leave-one-out-ish: score each kept drop in +-0.4s
print("\nscore at each kept drop:")
ds = []
for p in peaks:
    t0 = env_t[p]
    m0 = int((t0 - 0.4) * rate / STEP)
    m1 = int((t0 + 0.4) * rate / STEP)
    m0 = max(0, m0); m1 = min(nwin - 1, m1)
    s = scores[m0:m1].max()
    ds.append(s)
    print(f"    @ {t0:7.2f}s  score {s:6.1f}  (rms {wrms[m0:m1].max():5.1f})")

mask = np.ones(nwin, dtype=bool)
for p in peaks:
    t0 = env_t[p]
    m0 = int((t0 - 1.0) * rate / STEP)
    m1 = int((t0 + 1.0) * rate / STEP)
    mask[max(0,m0):min(nwin,m1)] = False

print("\nenergy-gated discrimination (gate in WAV units; firmware raw = /8):")
for g in (8, 12, 16, 24, 32):
    gm = mask & (wrms >= g)
    if gm.sum() == 0:
        print(f"  gate {g:3d}: no noise windows pass")
        continue
    ns = np.sort(scores[gm])
    dmax = max(wrms[a:b].max() for (a,b) in
               [(max(0,int((env_t[p]-0.4)*rate/STEP)),
                 min(nwin-1,int((env_t[p]+0.4)*rate/STEP))) for p in peaks])
    ds_ok = [d for (d, p) in zip(ds, peaks)
             if wrms[max(0,int((env_t[p]-0.4)*rate/STEP)):
                     min(nwin-1,int((env_t[p]+0.4)*rate/STEP))+1].max() >= g]
    print(f"  gate {g:3d}: noise windows {gm.sum():5d} | noise med {np.median(ns):6.1f} "
          f"p90 {np.percentile(ns,90):6.1f} max {ns.max():6.1f} | "
          f"drops passing {len(ds_ok)}/{len(ds)} min-score {min(ds_ok) if ds_ok else 0:.0f} | "
          f"sep {min(ds_ok)-ns.max() if ds_ok else 0:.0f}")
