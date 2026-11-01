#!/usr/bin/env python3
# ============================================================================
# compare_events.py — pull the drop log and separate "silence" events from
# "drop-like" events using the recorded signal columns.
#
# Usage:
#   python3 compare_events.py [http://host]     (fetches /drops.csv)
#   python3 compare_events.py local.csv        (analyzes a local file)
#
# Prints every event with its signal columns and shows the natural split
# between the two populations (gap in env/match space).
# ============================================================================
import sys, csv, urllib.request
from collections import defaultdict
import numpy as np

SRC = sys.argv[1] if len(sys.argv) > 1 else 'http://192.168.0.77/drops.csv'

if SRC.startswith('http'):
    rows = list(csv.reader(urllib.request.urlopen(SRC).read().decode().splitlines()))
else:
    with open(SRC) as f:
        rows = list(csv.reader(f))

header = rows[0]
data = []
cfgs = []       # (epoch, version, text)
for r in rows[1:]:
    if not r or not r[0]:
        continue
    if r[0].startswith('#'):
        try:
            cfgs.append((float(r[1].split(',')[1]), r[1], ','.join(r[2:])))
        except (IndexError, ValueError):
            pass
        continue
    if len(r) < 3 or not r[1]:
        continue
    try:
        data.append({
            'iso': r[0],
            't': float(r[1]),
            'peak': float(r[2]),
            'env': float(r[3]) if len(r) > 3 and r[3] else 0.0,
            'match': float(r[4]) if len(r) > 4 and r[4] else 0.0,
        })
    except ValueError:
        continue

def cfg_for(t):
    cur = None
    for (ce, ver, txt) in cfgs:
        if ce <= t:
            cur = (ver, txt)
        else:
            break
    return cur

print(f"{len(data)} events, {len(cfgs)} config versions\n")
print("  #  time(iso)             peak   env   match  cfg")
for i, d in enumerate(data):
    c = cfg_for(d['t'])
    print(f"{i+1:3d}  {d['iso'][:19]:19s}  {d['peak']:5.0f} {d['env']:5.1f} "
          f"{d['match']:6.0f}  {c[0] if c else '?'}")
print("\nconfig timeline:")
for (ce, ver, txt) in cfgs:
    print(f"  {ver:12s}  {txt}")

if len(data) >= 2:
    envs = np.array([d['env'] for d in data])
    mats = np.array([d['match'] for d in data])
    print(f"\nenv  : min {envs.min():.1f} med {np.median(envs):.1f} max {envs.max():.1f}")
    print(f"match: min {mats.min():.0f} med {np.median(mats):.0f} max {mats.max():.0f}")
    # suggested split: events above median env+match are drop-like
    lo = np.median(envs)
    dropish = sum(1 for d in data if d['env'] > lo * 1.5 or d['match'] > 700)
    print(f"suggested: {dropish} look drop-like, {len(data)-dropish} look like silence "
          f"(env > {lo*1.5:.1f} or match > 700)")
