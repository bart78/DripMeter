#!/usr/bin/env python3
# ============================================================================
# build_template.py — build a device-format template.bin from a WAV + labels.
#
# Usage:
#   python3 build_template.py <recording.wav> <labels.txt> [out.bin]
#   labels.txt = Audacity label export:  start<TAB>end<TAB>name
#
# Output format (matches DropTemplate::save):
#   magic "DROP"(4) | len u16 (512) | fs u16 | avgCount u8 | lastPeak u16 |
#   int16 template[512]  (peak-aligned, DC-free, RMS-normalized average)
# ============================================================================
import sys, wave, csv, struct, math
import numpy as np

KLEN, KALIGN = 512, 128

wav_p = sys.argv[1]
lab_p = sys.argv[2]
out_p = sys.argv[3] if len(sys.argv) > 3 else 'template.bin'

w = wave.open(wav_p, 'rb')
fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)

labels = []
with open(lab_p) as f:
    for row in csv.reader(f, delimiter='\t'):
        if len(row) >= 3:
            try:
                labels.append((float(row[0]), float(row[1])))
            except ValueError:
                pass
print(f"{wav_p}: {fs} Hz, {len(labels)} labels")

captures = []
for (a, b) in labels:
    i0 = int(a * fs)
    seg = x[i0:i0 + KLEN]
    if len(seg) < KLEN:
        continue
    # peak-align to KALIGN
    pidx = int(np.argmax(np.abs(seg)))
    al = np.zeros(KLEN)
    for i in range(KLEN):
        src = i + pidx - KALIGN
        if 0 <= src < KLEN:
            al[i] = seg[src]
    al = al - al.mean()
    r = np.sqrt((al ** 2).mean())
    if r < 1:
        continue
    captures.append(al * (1024.0 / r))
    print(f"  label {a:7.2f}s -> capture kept (peak {int(np.abs(seg).max())})")

if not captures:
    print("no usable captures")
    sys.exit(1)

tmpl = np.mean(captures, axis=0).clip(-30000, 30000).astype(np.int16)
last_peak = int(max(np.abs(np.array(captures)).max(), 1))

# also compute the true capture peak in raw device units (/8 from WAV)
with open(out_p, 'wb') as f:
    f.write(b'DROP')
    f.write(struct.pack('<H', KLEN))
    f.write(struct.pack('<H', fs))
    f.write(struct.pack('<B', len(captures)))
    f.write(struct.pack('<H', last_peak // 8 + 1))
    f.write(tmpl.tobytes())

print(f"wrote {out_p} ({4+2+2+1+2+KLEN*2} bytes, avg {len(captures)} captures @ {fs} Hz)")
print(f"upload with: curl -X POST --data-binary @{out_p} http://DEVICE_IP/tpl.bin")
