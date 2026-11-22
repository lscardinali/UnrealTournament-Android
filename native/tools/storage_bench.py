#!/usr/bin/env python3
"""
Storage benchmark for the installed game (native build), over adb. Run when the device is free:

    python -I native/tools/storage_bench.py [map]

1. Raw reads of the game's data folders with 1 KB and 64 KB requests (dd), internal vs the SD card folder the setup
   uses: what bionic's 1 KB stdio buffer cost the engine, and what the 64 KB buffer in shim.c (g_fopen) buys.
   Page cache: each file is read once per block size, a different file each time, so runs don't warm each other.
2. Map load time: launches the game straight into [map] (default DM-Deck16][) and times from `am start` to the
   log line "Bringing <map> up for play" (cold start: app + engine + map). Run it with the install on internal
   storage and on the SD card to compare.
"""
import os
import shutil
import re
import subprocess
import sys
import time

ADB = shutil.which("adb") or os.path.join(
    os.environ.get("ANDROID_HOME")
    or os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "Android", "Sdk"),
    "platform-tools", "adb")
PKG = "com.lscardinali.unrealtournament"


def sh(cmd, root_app=False):
    """adb shell; root_app runs it as the app (run-as) without a nested shell, so arguments need no quoting."""
    args = [ADB, "shell"] + (["run-as", PKG] + cmd.split() if root_app else [cmd])
    return subprocess.run(args, capture_output=True, text=True).stdout


def dd_rate(path, bs):
    r = subprocess.run([ADB, "shell", "run-as", PKG, "dd", f"if={path}", "of=/dev/null", f"bs={bs}"],
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    m = re.search(r"([\d.]+) ?([KMG])?B?/s", out)  # toybox dd: "75 M/s"
    if not m:
        return out.strip().splitlines()[-1] if out.strip() else "?"
    v = float(m.group(1)) * {"K": 1e-3, "M": 1, "G": 1e3, None: 1e-6}[m.group(2)]
    return f"{v:7.1f} MB/s"


def raw_reads():
    game = "files/game"
    maps = sh(f"ls -L {game}/Maps", root_app=True).split()
    maps = [m for m in maps if m.endswith(".unr")]
    target = sh(f"readlink {game}/Maps", root_app=True).strip() or "(internal)"
    print(f"Maps folder: {target}")
    sizes = {}
    for m in maps:
        try:
            sizes[m] = int(sh(f"stat -L -c %s {game}/Maps/{m}", root_app=True).strip() or 0)
        except ValueError:
            sizes[m] = 0
    big = sorted(maps, key=lambda m: -sizes[m])[:4]
    for bs, f in zip(["1k", "64k", "1k", "64k"], big):
        print(f"  bs={bs:>3}  {f:<28} {dd_rate(f'{game}/Maps/{f}', bs)}")


def load_time(map_name):
    sh(f"am force-stop {PKG}")
    time.sleep(1)
    sh("logcat -c")
    t0 = time.time()
    sh(f"am start -n {PKG}/.MainActivity --es args '{map_name}?Game=Botpack.DeathMatchPlus'")
    log = "files/home/.utpg/System/UnrealTournament.log"
    while time.time() - t0 < 90:
        if f"Bringing Level {map_name}" in sh(f"cat {log}", root_app=True):
            print(f"  {map_name}: playable {time.time() - t0:.1f} s after am start")
            return
        time.sleep(0.25)
    print("  timed out")


if __name__ == "__main__":
    raw_reads()
    load_time(sys.argv[1] if len(sys.argv) > 1 else "DM-Deck16][")
    sh(f"am force-stop {PKG}")
