#!/bin/bash
# Push and run the utloader spike on the device over adb (no APK, runs as the shell user).
#   bash native/utloader/run-spike.sh          M0 checks
#   bash native/utloader/run-spike.sh ucc help M0 + run ucc-bin-arm64 help through the loader
set -uo pipefail
export MSYS_NO_PATHCONV=1
source "$(dirname "$0")/../android-env.sh"
HERE="$(cd "$(dirname "$0")" && pwd -W)"
NATIVE="$(cd "$(dirname "$0")/.." && pwd -W)"
ROOT="$(cd "$(dirname "$0")/../.." && pwd -W)"
D=/data/local/tmp/utl

adb shell "mkdir -p $D/glibc $D/test $D/home"
adb push -q "$HERE/out/utl-spike" "$D/"
adb push -q "$HERE/out/libthrowtest.so" "$D/test/"
adb push -q "$NATIVE/runtime/out/lib/libstdc++.so.6" "$NATIVE/runtime/out/lib/libgcc_s.so.1" "$D/glibc/"
# The game's System dir (Linux arm64 469f); pushed once.
adb shell "[ -f $D/System/Core.so ]" || adb push -q "$ROOT/linux-arm64/System" "$D/System"
adb shell "chmod 755 $D/utl-spike; cd $D/System && HOME=$D/home $D/utl-spike $D/glibc $D/System $D/test $*"
