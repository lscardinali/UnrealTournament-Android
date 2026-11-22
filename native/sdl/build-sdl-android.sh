#!/bin/bash
# Android (bionic, arm64-v8a) builds of SDL3 and SDL3_ttf for the native (in-process) port.
# Output: native/sdl/out-android/{libSDL3.so,libSDL3_ttf.so}
# The game's libSDL3.so.0 / libSDL3_ttf.so.0 imports resolve to these (utloader maps the names).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="/c/Users/lucas/AppData/Local/Android/Sdk/cmake/4.1.2/bin:$PATH"
NDK=/c/Users/lucas/AppData/Local/Android/Sdk/ndk/28.2.13676358
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
ANDROID=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a
         -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release)

# SDL3 (same source tree as the glibc build) with SDL_UTANDROID in native-pad mode (controller shaping)
cmake -S "$HERE/src" -B "$HERE/build-android" -G Ninja "${ANDROID[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_UTANDROID=ON -DSDL_VIRTUAL_JOYSTICK=ON
cmake --build "$HERE/build-android"

# SDL3_ttf with vendored FreeType + HarfBuzz
[ -d "$HERE/ttf-src" ] || git -c advice.detachedHead=false -c core.autocrlf=false clone -q --depth 1 \
    --recurse-submodules --shallow-submodules -b release-3.2.2 https://github.com/libsdl-org/SDL_ttf "$HERE/ttf-src"
cmake -S "$HERE/ttf-src" -B "$HERE/build-ttf-android" -G Ninja "${ANDROID[@]}" \
    -DSDL3_DIR="$HERE/build-android" -DBUILD_SHARED_LIBS=ON -DSDLTTF_VENDORED=ON -DSDLTTF_SAMPLES=OFF \
    -DSDLTTF_HARFBUZZ=ON -DSDLTTF_PLUTOSVG=OFF -DSDLTTF_INSTALL=OFF
cmake --build "$HERE/build-ttf-android"

mkdir -p "$HERE/out-android"
"$STRIP" --strip-unneeded -o "$HERE/out-android/libSDL3.so" "$HERE/build-android/libSDL3.so"
"$STRIP" --strip-unneeded -o "$HERE/out-android/libSDL3_ttf.so" "$(ls "$HERE"/build-ttf-android/libSDL3_ttf.so* | head -1)"
ls -la "$HERE/out-android"
