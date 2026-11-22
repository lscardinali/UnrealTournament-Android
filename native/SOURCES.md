# Upstream sources

These are cloned next to the build scripts and are not part of this repository (see `.gitignore`).

| Folder | Repository | Revision |
|---|---|---|
| `native/sdl/src` | https://github.com/libsdl-org/SDL | `release-3.4.20` (`f00dc919ca35b9756316e86c4777392de442f9b2`), then `native/sdl/apply_utandroid.py` |
| `native/sdl/ttf-src` | https://github.com/libsdl-org/SDL_ttf | `release-3.2.2` (`a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b`) |
| `native/openal/src` | https://github.com/kcat/openal-soft | `1.24.3` (`dc7d7054a5b4f3bec1dc23a42fd616a0847af948`) |
| `Android/third_party/termux-x11` | https://github.com/termux/termux-x11 | fetched and patched by `Android/tools/setup-x11.sh` (`e3a1657`) |

Other inputs:

- Debian trixie glibc runtime: `native/runtime/fetch_debs.py` → `native/runtime/out`.
- Game: OldUnreal UT 469f Linux arm64 (`linux-arm64/`) plus the retail data (`UnrealTournament/`).
- PadUI is compiled with the Windows `UCC.exe` of the retail install: `native/padui/build-padui.sh`.

`Android/app/src/main/jniLibs/arm64-v8a/*.so` are build outputs of the scripts above, committed so the APK builds
without rebuilding the native side.
