#!/usr/bin/env python3
# ============================================================================
# analyze_noise.py — can the noise be filtered out? Cross-correlate silence
# events vs drop events against both templates, and test variants:
#   A) full-window correlation (current firmware)
#   B) ring-only correlation (post-peak 3/4 of the window — the click is
#      generic, the ring is the signature)
# ============================================================================
import wave, math, sys
import numpy as np

KLEN, KALIGN, STEP, ENV_WIN = 512, 128, 16, 128

def load_wav(p):
    w = wave.open(p, 'rb')
    r = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
    return x, r

def env_trace(x, r):
    cs  = np.concatenate([[0.0], np.cumsum(x)])
    cs2 = np.concatenate([[0.0], np.cumsum(x ** 2)])
    env = []
    for i in range(ENV_WIN, len(x) + 1, STEP):
        s  = cs[i] - cs[i - ENV_WIN]
        s2 = cs2[i] - cs2[i - ENV_WIN]
        m = s / ENV_WIN
        v = s2 / ENV_WIN - m * m
        env.append(math.sqrt(max(v, 0.0)))
    return np.array(env)

def captures(x, r, gate_mult=3.0, n_keep=12, refr=1.0):
    env = env_trace(x, r)
    noise = np.percentile(env, 50)
    gate = max(gate_mult * noise, 0.5)
    refr_s = int(refr * r / STEP)
    cands, last = [], -10**9
    for k, e in enumerate(env):
        if e > gate and k - last > refr_s:
            cands.append(k); last = k
    cands = sorted(sorted(cands, key=lambda k: -env[k])[:n_keep])
    out = []
    for k in cands:
        onset = k * STEP + ENV_WIN
        a = max(0, onset - 128)
        seg = x[a:a + KLEN]
        if len(seg) < KLEN:
            continue
        seg = seg - seg.mean()
        rr = np.sqrt((seg ** 2).mean())
        if rr > 1:
            out.append(seg / rr)
    return out

def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    return float(np.dot(a, b) / (np.sqrt((a**2).sum() * (b**2).sum())))

def ring_part(sig):
    """post-peak portion: from peak to end, zero the rest"""
    p = int(np.argmax(np.abs(sig)))
    v = np.zeros_like(sig)
    v[p:] = sig[p:]
    return v

sil, sr = load_wav('data/silence.wav')
drp, dr = load_wav('data/drip (1).wav')

scaps = captures(sil, sr, n_keep=12)
dcaps = captures(drp, dr, n_keep=10)
print(f"silence captures: {len(scaps)}, drop captures: {len(dcaps)}")

sT = np.mean(scaps, axis=0)
dT = np.mean(dcaps, axis=0)

print("\n== A) full-window correlation (current firmware behavior) ==")
for name, caps in (("silence", scaps), ("drops  ", dcaps)):
    sd = [corr(c, dT) for c in caps]
    ss = [corr(c, sT) for c in caps]
    print(f"  {name}: vs dropTpl  min {min(sd):.2f} med {np.median(sd):.2f} max {max(sd):.2f}"
          f" | vs noiseTpl min {min(ss):.2f} med {np.median(ss):.2f} max {max(ss):.2f}")

print("\n== B) ring-only (post-peak) correlation ==")
for name, caps in (("silence", scaps), ("drops  ", dcaps)):
    rc = [ring_part(c) for c in caps]
    rT_d = ring_part(dT)
    rT_s = ring_part(sT)
    sd = [corr(c, rT_d) for c in rc]
    ss = [corr(c, rT_s) for c in rc]
    print(f"  {name}: vs dropTpl  min {min(sd):.2f} med {np.median(sd):.2f} max {max(sd):.2f}"
          f" | vs noiseTpl min {min(ss):.2f} med {np.median(ss):.2f} max {max(ss):.2f}")

# residual scoring: score = corr(dropTpl) - k * corr(noiseTpl), on full windows
print("\n== C) residual score = corr(drop) - k*corr(noise), full windows ==")
for k in (0.0, 0.5, 1.0):
    line = f"  k={k}: "
    for name, caps in (("silence", scaps), ("drops", dcaps)):
        v = [corr(c, dT) - k * corr(c, sT) for c in caps]
        line += f"{name} [{min(v):.2f} .. {max(v):.2f}] med {np.median(v):.2f}   "
    print(line)
