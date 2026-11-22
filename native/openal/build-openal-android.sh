#!/bin/bash
# OpenAL Soft (same 1.24.3 source as the glibc build) for Android/bionic, for the native (in-process) port.
# Output: native/openal/out-android/libopenal.so — the game's libopenal.so.1 imports resolve to it (utloader alias).
# Its mixer runs on its own (bionic) threads and never calls into the game, so it needs nothing from our TLS.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="/c/Users/lucas/AppData/Local/Android/Sdk/cmake/4.1.2/bin:$PATH"
NDK=/c/Users/lucas/AppData/Local/Android/Sdk/ndk/28.2.13676358
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"

[ -d "$HERE/src" ] || git -c advice.detachedHead=false -c core.autocrlf=false clone -q --depth 1 -b 1.24.3 \
    https://github.com/kcat/openal-soft "$HERE/src"

cmake -S "$HERE/src" -B "$HERE/build-android" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
    -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=OFF -DALSOFT_EMBED_HRTF_DATA=ON \
    -DALSOFT_BACKEND_OPENSL=ON -DALSOFT_REQUIRE_OPENSL=ON -DALSOFT_BACKEND_OBOE=OFF \
    -DALSOFT_BACKEND_SDL3=OFF -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_BACKEND_WAVE=ON
cmake --build "$HERE/build-android"

mkdir -p "$HERE/out-android"
"$STRIP" --strip-unneeded -o "$HERE/out-android/libopenal.so" "$HERE/build-android/libopenal.so"
ls -la "$HERE/out-android"
