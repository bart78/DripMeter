#!/usr/bin/env python3
# ============================================================================
# offline_match_test.py — simulate the firmware match pipeline on the
# labelled session and measure discrimination WITHOUT touching hardware.
#
# Pipeline mirrored from drop_sampler.cpp / drop_template.cpp:
#   * AC-RMS envelope: 128-sample window, updates every 16 samples
#   * learn: capture 128 pre-onset + fill to 512, peak-align to kAlign=128,
#            DC-remove, RMS-normalize (scale 1024), average captures
#   * detect: slide every 16 samples, peak-align live window to 128,
#            normalized correlation -> score 0..999
# ============================================================================
import wave, math, csv
import numpy as np

WAV = 'data/drip.wav'
LABELS = 'data/drops-1.txt'
KLEN = 512
KALIGN = 128
STEP = 16
ENV_WIN = 128

w = wave.open(WAV, 'rb')
rate = w.getframerate()
raw = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
n = len(raw)
print(f"wav {rate} Hz, {n/rate:.1f} s")

labels = []
with open(LABELS) as f:
    for row in csv.reader(f, delimiter='\t'):
        if len(row) >= 3:
            labels.append((float(row[0]), float(row[1]), row[2]))
print(f"{len(labels)} labels")

# --- firmware envelope -----------------------------------------------------
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

quiet = np.percentile(env, 50)
print(f"env: quiet median {quiet:.1f}, max {env.max():.1f}")

# --- learn: capture from each label, firmware-style -------------------------
def capture_for(label_s):
    # onset = first envelope update after the label start whose env is above
    # quiet + margin/2  (firmware gate, margin ~15 -> gate 7.5)
    gate = quiet + 7.5
    j = int(label_s / (STEP / rate)) - int(ENV_WIN / STEP)
    for k in range(max(0, j), len(env)):
        if env[k] > gate:
            onset_sample = k * STEP + ENV_WIN
            a = onset_sample - 128
            return raw[a:a + KLEN]
        if env_t[k] > label_s + 0.8:   # don't chase forever
            break
    return None

def add_capture(buf):
    # peak-align to KALIGN
    b = np.zeros(KLEN)
    pidx = int(np.argmax(np.abs(buf)))
    for i in range(KLEN):
        src = i + pidx - KALIGN
        if 0 <= src < KLEN:
            b[i] = buf[src]
    b = b - b.mean()
    r = np.sqrt((b ** 2).mean())
    if r < 1:
        return None
    return b * (1024.0 / r)

caps = []
for (a, b_, nm) in labels:
    c = capture_for(a)
    if c is not None and len(c) == KLEN:
        norm = add_capture(c)
        if norm is not None:
            caps.append(norm)
print(f"captured {len(caps)}/{len(labels)} drops for the template")

tmpl = np.mean(caps, axis=0)
tnorm = np.sqrt((tmpl ** 2).sum())
print(f"template from {len(caps)} captures")

# --- detect: sweep the whole recording --------------------------------------
def window_grid():
    for i, s0 in enumerate(range(0, n - KLEN + 1, STEP)):
        yield i, raw[s0:s0 + KLEN]

scores_t = (np.arange((n - KLEN) // STEP + 1) * STEP) / rate

# window RMS (for energy gating)
wrms = np.array([np.sqrt((w ** 2).mean()) for _, w in window_grid()])

def corr_trace(template, tnorm, gate=0.0, env_profile=False):
    out = np.zeros(len(wrms))
    for i, win in window_grid():
        if wrms[i] < gate:
            continue
        if env_profile:
            # 32-point envelope profile of the window (RMS over 16-sample chunks)
            v = np.sqrt((win.reshape(32, 16).astype(np.float64) ** 2).mean(axis=1))
            v = np.log10(1.0 + v)
            vn = np.sqrt((v ** 2).sum())
            if vn < 1e-6:
                continue
            out[i] = float(np.dot(v, template) / (vn * tnorm)) * 1000.0
        else:
            pidx = int(np.argmax(np.abs(win)))
            a = np.zeros(KLEN)
            for j in range(KLEN):
                src = j + pidx - KALIGN
                if 0 <= src < KLEN:
                    a[j] = win[src]
            a = a - a.mean()
            wnorm = np.sqrt((a ** 2).sum())
            if wnorm > 0:
                out[i] = float(np.dot(a, template) / (wnorm * tnorm)) * 1000.0
    return out

# --- label / noise masks ----------------------------------------------------
label_idx = []
for (a, b_, nm) in labels:
    i0 = int((a - 0.3) * rate / STEP)
    i1 = int((a + 0.3) * rate / STEP)
    label_idx.append((max(0, i0), min(len(wrms) - 1, i1)))

tail_idx = []
for (a, b_, nm) in labels:
    i0 = int((a - 1.2) * rate / STEP)
    i1 = int((b_ + 1.2) * rate / STEP)
    tail_idx.append((max(0, i0), min(len(wrms) - 1, i1)))

print("\nwindow RMS (WAV units) around each label:")
for i, (a_, b_) in enumerate(label_idx):
    print(f"    #{i+1:2d} rms {wrms[a_:b_].max():6.1f}   (env peak in WAV units; "
          f"firmware raw = /8)")

def report(name, tr):
    ds = [tr[a_:b_].max() for (a_, b_) in label_idx]
    mask = np.ones(len(tr), dtype=bool)
    for (a_, b_) in tail_idx:
        mask[a_:b_] = False
    ns = np.sort(tr[mask])
    print(f"{name:34s} min-drop {min(ds):6.1f} | noise med {np.median(ns):6.1f} "
          f"p90 {np.percentile(ns,90):6.1f} p99 {np.percentile(ns,99):6.1f} "
          f"max {ns.max():6.1f} | sep {min(ds)-ns.max():6.0f}")

# Variant A: raw correlation, no gate (current firmware)
report("A raw-corr", corr_trace(tmpl, tnorm))

# Variant B: raw correlation + energy gates
for g in (16, 24, 32, 48, 64):
    report(f"B raw-corr gate_rms>={g}", corr_trace(tmpl, tnorm, gate=g))

# Variant C: envelope-profile correlation + energy gates
et, etn = env_template()
for g in (24, 48):
    report(f"C env-profile gate_rms>={g}", corr_trace(et, etn, gate=g, env_profile=True))
