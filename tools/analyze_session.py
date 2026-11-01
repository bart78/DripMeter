#!/usr/bin/env python3
# ============================================================================
# analyze_session.py — labelled-drop session analysis (offline, on the Mac)
#
# Inputs (data/):
#   drip.wav        the device recording
#   drops-1.txt     Audacity label export (start end name)
#   drops.csv       the device's drop log (ISO,epoch,peak)
#
# Outputs: detection report vs. labels, per-drop SNR, spectra, template.
# ============================================================================
import sys, wave, struct, csv, math
import numpy as np

WAV = 'data/drip.wav'
LABELS = 'data/drops-1.txt'
CSV = 'data/drops.csv'

# --- load wav --------------------------------------------------------------
w = wave.open(WAV, 'rb')
rate = w.getframerate()
n = w.getnframes()
raw = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64)
t = np.arange(n) / rate
print(f"[wav] {rate} Hz, {n/rate:.1f} s, peak {abs(raw).max():.0f}, rms {np.sqrt((raw**2).mean()):.1f}")

# --- labels ----------------------------------------------------------------
labels = []
with open(LABELS) as f:
    rd = csv.reader(f, delimiter='\t')
    for row in rd:
        if len(row) >= 3:
            labels.append((float(row[0]), float(row[1]), row[2]))
print(f"[labels] {len(labels)} drops:")
for i, (a, b, nm) in enumerate(labels):
    print(f"    #{i+1:2d}  {a:7.3f}s - {b:7.3f}s  ({nm})")

# --- device log ------------------------------------------------------------
log = []
with open(CSV) as f:
    rd = csv.reader(f)
    next(rd, None)
    for row in rd:
        if len(row) < 3: continue
        try:
            log.append((float(row[1]), float(row[2])))
        except ValueError:
            continue
print(f"[log] {len(log)} entries in drops.csv")

# --- envelope of the wav (same AC-RMS concept as firmware) -----------------
win = int(rate * 0.0077)   # ~128 samples at device rate
s = np.convolve(np.abs(raw), np.ones(win)/win, mode='same')
s = np.convolve(raw**2, np.ones(win)/win, mode='same') - np.convolve(raw, np.ones(win)/win, mode='same')**2
env = np.sqrt(np.clip(s, 0, None))

# noise floor = median envelope of the quietest 50%
quiet = np.median(env)
p90 = np.percentile(env, 90)
print(f"[env] quiet median {quiet:.1f}  p90 {p90:.1f}  max {env.max():.1f}")

# --- per-drop SNR ----------------------------------------------------------
print("\n[per-drop]")
drop_envs = []
for i, (a, b, nm) in enumerate(labels):
    i0, i1 = int(a*rate), int(b*rate)
    seg = env[i0:i1]
    pk = seg.max() if len(seg) else 0
    snr = pk / quiet if quiet > 0 else 0
    drop_envs.append((a, pk, snr))
    print(f"    #{i+1:2d} @ {a:7.3f}s  env_peak {pk:7.1f}  SNR {snr:5.1f}x")

# --- alignment: match device log against labels ----------------------------
# device epoch -> wav-time offset unknown; try pattern matching
best = None
if log:
    best = (0, 0, 0)
    t0 = labels[0][0]
    # candidate offsets: for each log entry as anchor of the first label
    # (log entry time - first label time)
    for (et, _) in log:
        off = et - t0
        matches = 0
        used = set()
        for (la, _, _) in labels:
            for k, (lt, _) in enumerate(log):
                if abs(lt - (la + off)) < 0.7 and k not in used:
                    matches += 1
                    used.add(k)
                    break
        if matches > best[1]:
            best = (off, matches, len(used))
    off, matches, _ = best
    print(f"\n[detection] best alignment: {matches}/{len(labels)} labelled drops "
          f"appear in the device log")
    if matches:
        # false positives: log entries within the wav window not near labels
        wav_start = labels[0][0] - (labels[0][0])  # placeholder
        # use offset: log time -> wav time = t - off
        in_win = [lt for (lt, _) in log if 0 <= lt - off <= n/rate]
        near = [lt for (lt, _) in log
                if any(abs((lt - off) - la) < 0.7 for (la, _, _) in labels)]
        fp = len(in_win) - len(set(near))
        print(f"[detection] events inside window: {len(in_win)}, matched: {len(set(near))}, "
              f"unmatched (possible FP): {fp}")
        print("    device log inside window (wav-time s):",
              [f"{lt-off:.2f}" for lt in sorted(in_win)][:40])

# --- spectra: drop vs noise -------------------------------------------------
print("\n[spectra] mean drop spectrum vs noise spectrum (freq, drop_dB, noise_dB):")
def spec(x, start_s, dur_s):
    a = int(start_s*rate); b = int((start_s+dur_s)*rate)
    if b - a < 64: return None, None
    X = np.fft.rfft(x[a:b])
    fr = np.fft.rfftfreq(b-a, 1/rate)
    return fr, np.abs(X)

# fixed 0.25s windows so all spectra share the same frequency grid
fr_n, N = spec(raw, 1.0, 0.25)
drop_sp = []
for (a, b, _) in labels:
    fr, D = spec(raw, a, 0.25)
    if D is not None and len(D) == len(N):
        drop_sp.append(D)
if drop_sp and N is not None:
    D = np.mean(drop_sp, axis=0)
    for f in [100, 200, 400, 800, 1600, 3200, 6400]:
        i = int(np.argmin(np.abs(fr_n - f)))
        dd = 20*math.log10(D[i]+1) if i < len(D) else 0
        nn = 20*math.log10(N[i]+1) if i < len(N) else 0
        print(f"    {f:5d} Hz   drop {dd:6.1f} dB   noise {nn:6.1f} dB   gap {dd-nn:6.1f} dB")

# --- template quality: peak-aligned average drop waveform ------------------
print("\n[template] peak-aligned average drop waveform stats:")
tlen = int(rate * 0.02)
stack = []
for (a, _, _) in labels:
    i0 = int(a*rate)
    seg = raw[i0:i0+tlen]
    if len(seg) < tlen: continue
    seg = seg - seg.mean()
    r = np.sqrt((seg**2).mean())
    if r > 1: stack.append(seg / r)
if stack:
    tmpl = np.mean(stack, axis=0)
    print(f"    {len(stack)} captures stacked, template rms {np.sqrt((tmpl**2).mean()):.3f}")
    # self-correlation of each capture against the template
    for i, sgm in enumerate(stack):
        print(f"    capture #{i+1}: corr with template {float(np.dot(sgm, tmpl) / np.sqrt((sgm**2).sum() * (tmpl**2).sum())):.2f}")
else:
    print("    none usable")
