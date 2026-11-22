#!/usr/bin/env python3
"""
On-screen frame pacing of the game's SurfaceView, from `dumpsys SurfaceFlinger --latency <layer>`.

    python -I sf_latency.py [seconds] [layer substring, default: the X11 GameActivity's SurfaceView]

SurfaceFlinger keeps the last ~127 frames of a layer: per frame (desired present, actual present, frame ready), ns.
We sample repeatedly for [seconds], merge, and report intervals between actual presents in refresh periods:
1 = a new frame every refresh (smooth), 2+ = a refresh showed the previous frame again (a visible stutter).
"""
import collections
import os
import shutil
import subprocess
import sys
import time

ADB = shutil.which("adb") or os.path.join(
    os.environ.get("ANDROID_HOME")
    or os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "Android", "Sdk"),
    "platform-tools", "adb")
LAYER_HINT = "SurfaceView[com.lscardinali.unrealtournament/com.lscardinali.unrealtournament.GameActivity](BLAST)"


def adb(*args):
    return subprocess.run([ADB, "shell", *args], capture_output=True, text=True).stdout


def layer_name(hint):
    for line in adb("dumpsys", "SurfaceFlinger", "--list").splitlines():
        if hint in line and "(BLAST)" in line and line.startswith("SurfaceView"):
            return line.strip()
    raise SystemExit("game SurfaceView layer not found (is the game on screen?)")


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
    hint = sys.argv[2] if len(sys.argv) > 2 else LAYER_HINT
    name = layer_name(hint)
    presents = set()
    period = None
    end = time.time() + seconds
    while time.time() < end:
        out = adb("dumpsys", "SurfaceFlinger", "--latency", f"'{name}'").splitlines()
        if out and out[0].strip().isdigit():
            period = int(out[0])
        for line in out[1:]:
            parts = line.split()
            if len(parts) == 3:
                actual = int(parts[1])
                if 0 < actual < 2**62:  # INT64_MAX = still pending
                    presents.add(actual)
        time.sleep(0.4)
    p = sorted(presents)
    if len(p) < 10 or not period:
        raise SystemExit(f"too few frames ({len(p)})")
    gaps = [b - a for a, b in zip(p, p[1:])]
    buckets = collections.Counter(max(1, round(g / period)) for g in gaps)
    span = (p[-1] - p[0]) / 1e9
    print(f"layer: {name}")
    print(f"refresh period {period / 1e6:.2f} ms ({1e9 / period:.1f} Hz); {len(p)} frames over {span:.1f} s "
          f"= {len(p) / span:.1f} fps on screen")
    total = sum(buckets.values())
    for k in sorted(buckets):
        label = "on time" if k == 1 else f"held {k} refreshes (stutter)"
        print(f"  {k} x period: {buckets[k]:5d}  ({100 * buckets[k] / total:5.1f}%)  {label}")


if __name__ == "__main__":
    main()
