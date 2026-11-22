#!/bin/bash
# Build OpenAL Soft (glibc aarch64) with only its SDL3 backend, so UT's ALAudio plays through our SDL
# (utandroid audio driver -> Android AudioTrack). Output: native/openal/out/libopenal.so.1
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$(cd "$HERE/.." && pwd)"
export PATH="/c/Users/lucas/AppData/Local/Android/Sdk/cmake/4.1.2/bin:$PATH"
STRIP=/c/Users/lucas/AppData/Local/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe

[ -d "$HERE/src" ] || git -c advice.detachedHead=false -c core.autocrlf=false clone -q --depth 1 -b 1.24.3 \
    https://github.com/kcat/openal-soft "$HERE/src"

cmake -S "$HERE/src" -B "$HERE/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NATIVE/toolchain-aarch64-linux-gnu.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL3_DIR="$NATIVE/sdl/build" \
    -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=OFF \
    -DALSOFT_BACKEND_SDL3=ON -DALSOFT_REQUIRE_SDL3=ON \
    -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_ALSA=OFF \
    -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF \
    -DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_SOLARIS=OFF -DALSOFT_BACKEND_SDL2=OFF \
    -DALSOFT_BACKEND_WAVE=ON -DALSOFT_EMBED_HRTF_DATA=ON
cmake --build "$HERE/build"

mkdir -p "$HERE/out"
real=$(ls "$HERE"/build/libopenal.so.1.* | head -1)
"$STRIP" --strip-unneeded -o "$HERE/out/libopenal.so.1" "$real"
ls -la "$HERE/out"
