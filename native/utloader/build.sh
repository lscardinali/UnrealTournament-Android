#!/bin/bash
# Build the utloader spike (bionic executable) and its glibc test library.
#   out/utl-spike       NDK clang, aarch64-linux-android33
#   out/libthrowtest.so NDK clang, aarch64-linux-gnu against the Debian sysroot (GNU libstdc++), like UT's modules
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$(cd "$HERE/.." && pwd)"
source "$NATIVE/android-env.sh"
BIN="$NDK_BIN"
SYSROOT="$(cd "$NATIVE/runtime/out/root" && pwd -W)"
mkdir -p "$HERE/out"

"$BIN/clang$EXE" --target=aarch64-linux-android33 -O2 -g -Wall -Wno-unused-function -fno-omit-frame-pointer \
    -o "$HERE/out/utl-spike" "$HERE/spike.c" "$HERE/loader.c" "$HERE/tls.c" "$HERE/shim.c" -ldl -llog -lm

"$BIN/clang++$EXE" --target=aarch64-linux-gnu --sysroot="$SYSROOT" -shared -fPIC -O2 -fuse-ld=lld \
    --rtlib=libgcc --unwindlib=libgcc -stdlib=libstdc++ -Wl,-soname,libthrowtest.so \
    -o "$HERE/out/libthrowtest.so" "$HERE/test/throwtest.cpp"

"$BIN/llvm-readelf$EXE" -r "$HERE/out/libthrowtest.so" | grep -E "TPREL|TLSDESC" | sed 's/^/  testlib reloc: /'
ls -la "$HERE/out"

# Android library for the app (native port): SDLActivity -> SDL_main -> UT's ut-bin-arm64 on a utl thread
"$BIN/clang$EXE" --target=aarch64-linux-android28 -shared -fPIC -O2 -g -Wall -Wno-unused-function \
    -fno-omit-frame-pointer -Wl,-soname,libutloader.so \
    -o "$HERE/out/libutloader.so" "$HERE/main_android.c" "$HERE/loader.c" "$HERE/tls.c" "$HERE/shim.c" -ldl -llog -lm
ls -la "$HERE/out/libutloader.so"
