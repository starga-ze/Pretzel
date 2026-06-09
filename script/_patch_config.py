#!/usr/bin/env python3
"""One-shot: merge new keys from source running-config into /etc/pretzel/running-config.json"""
import json, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, "config", "running-config.json")
DST  = "/etc/pretzel/running-config.json"

def deep_merge(src, dst):
    changed = False
    for k, v in src.items():
        if k not in dst:
            dst[k] = v; changed = True
        elif isinstance(v, dict) and isinstance(dst[k], dict):
            if deep_merge(v, dst[k]): changed = True
    return changed

with open(SRC) as f: src = json.load(f)
with open(DST) as f: dst = json.load(f)

if deep_merge(src, dst):
    with open(DST, "w") as f: json.dump(dst, f, indent=4)
    print("[OK] Merged new keys into", DST)
    print("     icmpd.tuning.probe =", json.dumps(dst["icmpd"]["tuning"]["probe"], indent=4))
else:
    print("[OK] Already up-to-date:", DST)
