#!/usr/bin/env python3
# ============================================================================
# capture_env.py — record the device's live envelope trace to a CSV while
# the user performs a scripted drop-ladder, so the detection limit can be
# measured instead of guessed.
#
# Usage:  python3 capture_env.py <seconds> <out.csv>
#   polls /stream.json ~ every 0.25s, logs t, env, thr, match
# ============================================================================
import sys, time, json, urllib.request

SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 60
OUT = sys.argv[2] if len(sys.argv) > 2 else 'data/ladder.csv'
URL = 'http://192.168.0.77/stream.json'

t0 = time.time()
rows = 0
with open(OUT, 'w') as f:
    f.write('t,env,thr,match\n')
    while time.time() - t0 < SECONDS:
        try:
            with urllib.request.urlopen(URL, timeout=2) as r:
                j = json.load(r)
            f.write(f"{time.time()-t0:.2f},{j['env'][-1]},{j['thr']},{j['match_score']}\n")
            rows += 1
        except Exception as e:
            print('poll err:', e)
        time.sleep(0.25)
print(f"captured {rows} samples -> {OUT}")
