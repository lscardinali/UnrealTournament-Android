# Sourced by the build scripts: locates the Android SDK, NDK and CMake, and puts adb on PATH.
#   SDK:   $ANDROID_HOME, $ANDROID_SDK_ROOT, or the default install (%LOCALAPPDATA%\Android\Sdk, ~/Android/Sdk, ~/Library/Android/sdk)
#   NDK:   $ANDROID_NDK_HOME, else $SDK/ndk/28.2.13676358 (what the committed libraries use), else the newest
#   CMake: the newest under $SDK/cmake, else the one on PATH
# Exports SDK, NDK, NDK_BIN (LLVM toolchain bin dir) and EXE (".exe" on Windows, else "").
_first_dir() { for d in "$@"; do [ -n "$d" ] && [ -d "$d" ] && { echo "$d"; return; }; done; }
_newest() { ls -1d "$1"/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$::'; }

_local_appdata=""
[ -n "${LOCALAPPDATA:-}" ] && _local_appdata="$(cygpath -u "$LOCALAPPDATA" 2>/dev/null || echo "$LOCALAPPDATA")"
SDK="$(_first_dir "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "${_local_appdata:+$_local_appdata/Android/Sdk}" \
                  "$HOME/Android/Sdk" "$HOME/Library/Android/sdk")"
[ -n "$SDK" ] || { echo "Android SDK not found: set ANDROID_HOME" >&2; exit 1; }
SDK="$(cygpath -u "$SDK" 2>/dev/null || echo "$SDK")"

# Prefer the NDK the committed libraries were built with, so rebuilds match them.
NDK_VERSION=28.2.13676358
NDK="${ANDROID_NDK_HOME:-$(_first_dir "$SDK/ndk/$NDK_VERSION" "$(_newest "$SDK/ndk")")}"
[ -n "$NDK" ] && [ -d "$NDK" ] || { echo "Android NDK not found: install it with the SDK manager or set ANDROID_NDK_HOME" >&2; exit 1; }
NDK="$(cygpath -u "$NDK" 2>/dev/null || echo "$NDK")"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) _host=windows-x86_64; EXE=.exe ;;
    Darwin) _host=darwin-x86_64; EXE= ;;
    *) _host=linux-x86_64; EXE= ;;
esac
NDK_BIN="$NDK/toolchains/llvm/prebuilt/$_host/bin"

_cmake="$(_newest "$SDK/cmake")"
[ -n "$_cmake" ] && export PATH="$_cmake/bin:$PATH"
export PATH="$PATH:$SDK/platform-tools"
export SDK NDK NDK_BIN EXE
