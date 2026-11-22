#!/bin/bash
# Build our libSDL3.so.0 (glibc aarch64) on Windows: SDL 3.4.20 + utandroid gamepad/audio bridge.
# Output: native/sdl/out/libSDL3.so.0 (replaces the libSDL3.so.0 OldUnreal bundles in System/).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$(cd "$HERE/.." && pwd)"
CMAKE_DIR=/c/Users/lucas/AppData/Local/Android/Sdk/cmake/4.1.2/bin
export PATH="$CMAKE_DIR:$PATH"

[ -d "$HERE/src" ] || git -c advice.detachedHead=false -c core.autocrlf=false clone -q --depth 1 -b release-3.4.20 https://github.com/libsdl-org/SDL "$HERE/src"
python -I "$HERE/apply_utandroid.py" "$HERE/src"

# Point SDL at the versioned X11 libs so it dlopens their SONAMEs (libX11.so.6), not the dev names.
L="$NATIVE/runtime/out/root/usr/lib/aarch64-linux-gnu"
X11_LIBS=""
for n in X11:libX11 XEXT:libXext XCURSOR:libXcursor XI:libXi XFIXES:libXfixes XRANDR:libXrandr XSS:libXss; do
    f=$(ls "$L/${n#*:}".so.*.*.* | head -1)
    X11_LIBS="$X11_LIBS -D${n%%:*}_LIB=$f"
done

cmake -S "$HERE/src" -B "$HERE/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NATIVE/toolchain-aarch64-linux-gnu.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_INSTALL=OFF -DSDL_RPATH=OFF -DSDL_UTANDROID=ON \
    -DSDL_X11=ON -DSDL_X11_SHARED=ON -DSDL_X11_XTEST=OFF -DSDL_X11_XSCRNSAVER=OFF \
    -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=ON \
    -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_SNDIO=OFF -DSDL_OSS=OFF \
    -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=ON \
    -DSDL_VIRTUAL_JOYSTICK=ON -DSDL_HIDAPI=ON -DSDL_HIDAPI_JOYSTICK=OFF -DSDL_HIDAPI_LIBUSB=OFF \
    -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF \
    -DSDL_DUMMYCAMERA=OFF -DSDL_CAMERA=OFF -DSDL_OPENVR=OFF -DSDL_FRIBIDI=OFF -DSDL_LIBTHAI=OFF \
    -DSDL_GPU=OFF -DSDL_RENDER_VULKAN=OFF \n    $X11_LIBS
cmake --build "$HERE/build"

mkdir -p "$HERE/out"
real=$(ls "$HERE"/build/libSDL3.so.0.* | head -1)
/c/Users/lucas/AppData/Local/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe --strip-unneeded -o "$HERE/out/libSDL3.so.0" "$real"
ls -la "$HERE/out"
