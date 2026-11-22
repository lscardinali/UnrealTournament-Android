#!/bin/bash
# Dev deploy: install the APK and copy the glibc runtime + our native overrides into the app's filesDir via run-as.
# Game data (files/game) is copied separately with --game (once; ~800 MB).
#   bash native/deploy-dev.sh [--game] [--no-apk]
set -euo pipefail
export MSYS_NO_PATHCONV=1 PATH="$PATH:/c/Users/lucas/AppData/Local/Android/Sdk/platform-tools"
PKG=com.lscardinali.unrealtournament
ROOT="$(cd "$(dirname "$0")/.." && pwd -W)"   # Windows-style path for adb.exe
NATIVE="$ROOT/native"
STAGE=/data/local/tmp/ut-stage

if [[ " $* " != *" --no-apk "* ]]; then
    adb install -r "$ROOT/Android/app/build/outputs/apk/debug/app-debug.apk"
fi

adb shell "rm -rf $STAGE && mkdir -p $STAGE/override $STAGE/bin $STAGE/usr/share"
adb push -q "$NATIVE/runtime/out/lib" "$STAGE/glibc"
adb push -q "$NATIVE/runtime/out/vulkan/." "$STAGE/glibc/"
adb push -q "$NATIVE/runtime/out/share/X11" "$STAGE/usr/share/X11"
adb push -q "$NATIVE/sdl/out/libSDL3.so.0" "$NATIVE/openal/out/libopenal.so.1" "$STAGE/override/"
adb push -q "$NATIVE/build/utandroid-wm" "$STAGE/bin/"

GAME=0
if [[ " $* " == *" --game "* ]]; then
    GAME=1
    adb shell "rm -rf /data/local/tmp/ut-game && mkdir -p /data/local/tmp/ut-game"
    adb push -q "$ROOT/linux-arm64/System" /data/local/tmp/ut-game/System
    adb push -q "$ROOT/linux-arm64/SystemLocalized" /data/local/tmp/ut-game/SystemLocalized
    for d in Maps Textures Sounds Music Help Web; do
        adb push -q "$ROOT/UnrealTournament/$d" /data/local/tmp/ut-game/$d
    done
fi

adb shell "chmod -R a+rX $STAGE /data/local/tmp/ut-game 2>/dev/null; run-as $PKG sh -c '
    cd files && rm -rf glibc override bin usr &&
    cp -r $STAGE/glibc $STAGE/override $STAGE/bin $STAGE/usr . &&
    chmod 755 bin/* &&
    if [ $GAME = 1 ]; then rm -rf game && cp -r /data/local/tmp/ut-game game; fi
    du -sh glibc override bin usr game 2>/dev/null'"
