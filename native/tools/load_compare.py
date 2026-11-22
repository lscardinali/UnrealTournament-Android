#!/usr/bin/env python3
"""
Load / power comparison of the native build vs the X11 (glibc process + X server) build, same map, over adb.

    python -I load_compare.py [seconds per build] [map URL]

Per build: launch into the map, let it settle, then sample every ~0.5 s:
  - CPU time of every process of the app's uid (game, X server, app), as % of one core
  - average clock of each CPU cluster (policy) and GPU (kgsl), GPU busy %, GPU temperature
  - battery current (negative = draining); while charging this is net charge current, so only the *difference*
    between builds means something (more load -> less net charge)
"""
import os
import statistics
import subprocess
import sys
import time

ADB = os.path.expandvars(r"C:\Users\lucas\AppData\Local\Android\Sdk\platform-tools\adb.exe")
PKG = "com.lscardinali.unrealtournament"
TICKS = 100.0  # USER_HZ


def sh(cmd):
    return subprocess.run([ADB, "shell", cmd], capture_output=True, text=True).stdout


def uid_of_app():
    out = sh(f"dumpsys package {PKG} | grep -m1 userId=")
    return int(out.strip().split("userId=")[1].split()[0])


SAMPLE = r"""
for p in /proc/[0-9]*; do
  u=$(stat -c %u $p 2>/dev/null); [ "$u" = "UID" ] && echo "P $(cat $p/stat 2>/dev/null)";
done
for c in /sys/devices/system/cpu/cpufreq/policy*; do echo "C ${c##*policy} $(cat $c/scaling_cur_freq)"; done
echo "G $(cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage) $(cat /sys/class/kgsl/kgsl-3d0/devfreq/cur_freq) $(cat /sys/class/kgsl/kgsl-3d0/temp)"
echo "B $(cat /sys/class/power_supply/battery/current_now) $(cat /sys/class/power_supply/battery/voltage_now)"
echo "T $(cat /proc/uptime)"
"""


def sample(uid):
    out = sh(SAMPLE.replace("UID", str(uid)))
    procs, clusters, gpu, bat, t = {}, {}, None, None, None
    for line in out.splitlines():
        if line.startswith("P "):
            stat = line[2:]
            pid = int(stat.split()[0])
            name = stat[stat.index("(") + 1:stat.rindex(")")]
            f = stat[stat.rindex(")") + 2:].split()
            procs[pid] = (name, int(f[11]) + int(f[12]))
        elif line.startswith("C "):
            _, pol, khz = line.split()
            clusters[int(pol)] = int(khz) / 1000
        elif line.startswith("G "):
            parts = line.split()
            gpu = (float(parts[1].rstrip("%")), int(parts[3]) / 1e6, int(parts[4]) / 1000)
        elif line.startswith("B "):
            parts = line.split()
            bat = (int(parts[1]) / 1e6, int(parts[2]) / 1e6)
        elif line.startswith("T "):
            t = float(line.split()[1])
    return procs, clusters, gpu, bat, t


def measure(label, extras, url, seconds):
    sh(f"am force-stop {PKG}")
    time.sleep(1)
    sh(f"am start -n {PKG}/.MainActivity {extras} --es args '{url}'")
    time.sleep(25)                                   # load + settle
    sh("input gamepad keyevent BUTTON_R2")          # ready up: bots start fighting
    time.sleep(5)
    uid = uid_of_app()
    first = sample(uid)
    clusters, gpus, bats = [], [], []
    end = time.time() + seconds
    while time.time() < end:
        s = sample(uid)
        clusters.append(s[1])
        if s[2]: gpus.append(s[2])
        if s[3]: bats.append(s[3])
    last = sample(uid)
    dt = last[4] - first[4]
    cpu = {}
    for pid, (name, ticks) in last[0].items():
        if pid in first[0]:
            cpu[name] = cpu.get(name, 0) + (ticks - first[0][pid][1]) / TICKS / dt * 100
    total = sum(cpu.values())
    print(f"\n== {label}  ({dt:.0f} s)")
    for name, pct in sorted(cpu.items(), key=lambda kv: -kv[1]):
        if pct >= 1: print(f"   CPU {name:<28} {pct:6.1f} %")
    print(f"   CPU total                        {total:6.1f} %  (of one core)")
    pols = sorted({p for c in clusters for p in c})
    print("   CPU clocks avg   " + "  ".join(f"cluster{p}: {statistics.mean(c[p] for c in clusters if p in c):.0f} MHz" for p in pols))
    if gpus:
        print(f"   GPU busy avg {statistics.mean(g[0] for g in gpus):.0f} %  clock avg {statistics.mean(g[1] for g in gpus):.0f} MHz"
              f"  temp {gpus[-1][2]:.0f} C")
    if bats:
        a = statistics.mean(b[0] for b in bats)
        v = statistics.mean(b[1] for b in bats)
        print(f"   battery current avg {a:+.3f} A at {v:.2f} V ({a * v:+.2f} W net)")
    return total


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
    url = sys.argv[2] if len(sys.argv) > 2 else "DM-Deck16][?Game=Botpack.DeathMatchPlus"
    native = measure("native (in-process, Qualcomm Vulkan)", "", url, seconds)
    x11 = measure("X11 (glibc process + X server + Turnip)", "--ez x11 true", url, seconds)
    print(f"\nCPU: native uses {native:.0f}% vs X11 {x11:.0f}% of a core ({(1 - native / x11) * 100:.0f}% less)")
    sh(f"am force-stop {PKG}")


if __name__ == "__main__":
    main()
