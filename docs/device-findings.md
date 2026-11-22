# Device / feasibility findings (Retroid Pocket 6)

- Device: Retroid Pocket 6, `kalama` / QCS8550 (Snapdragon 8 Gen 2, Adreno 740), Android 13, 11.5 GB RAM
- Display: 1080x1920 physical (portrait panel, used in landscape), density 369
- `/dev/kgsl-3d0` is `crw-rw-rw-` → Mesa Turnip (KGSL backend) can talk to the GPU from an app process
- System Vulkan: `vulkan.adreno.so` (bionic, not usable directly from a glibc process)
- GameNative (`app.gamenative`) is installed

## Test: run the OldUnreal 469f Linux arm64 binaries via a glibc loader (adb shell, /data/local/tmp/ut)

glibc source: Debian 12 `libc6_2.36-9+deb12u14_arm64.deb`, `libstdc++6_12.2.0-14+deb12u1`, `libgcc-s1_12.2.0-14+deb12u1`.

```
cd /data/local/tmp/ut/game/System
HOME=/data/local/tmp/ut/home GCONV_PATH=/data/local/tmp/ut/glibc/gconv \
  ./ld.so --library-path /data/local/tmp/ut/game/System:/data/local/tmp/ut/glibc ./ucc-bin-arm64 help
```

Result: **works** — engine initializes (Version 469 rev f), creates UnrealTournament.ini/User.ini in `$HOME/.utpg/System`, binds Engine.so/Core.so.

Gotchas found:
1. **iconv required**: UT aborts with "Could not initialize iconv" unless `GCONV_PATH` points at glibc gconv modules.
   Minimal set: `gconv-modules`, `gconv-modules.d/`, `CP1252.so`, `UTF-16.so`, `UNICODE.so`, `UTF-32.so`.
2. **Base directory comes from `/proc/self/exe`**: when ld.so lives outside `System/`, UT fails with `MisingIni`.
   Works when the loader is placed inside `System/`. In the APK the loader must live in `nativeLibraryDir`
   (W^X), so we need either an LD_PRELOAD shim that rewrites `readlink("/proc/self/exe")` or an rtld patch
   (pocket-foundry has `rtld-program-restart.patch`).
3. `HOME` must be set (preference dir `$HOME/.utpg/System`).
4. adb-shell is not the app seccomp domain; must re-verify inside the app (clone3/rseq etc.; pocket-foundry uses Termux-patched glibc for this).

## Test 2: /proc/self/exe shim + full game boot (headless)

- `native/utshim/libutshim.so` (LD_PRELOAD, freestanding, built with NDK clang `--target=aarch64-unknown-linux-gnu`)
  redirects `readlink("/proc/self/exe")` to `$UT_EXE_PATH`. With it, ucc/ut-bin work with the loader outside System/.
- `ut-bin-arm64 DM-Deck16][ -nosound` with `SDL_VIDEO_DRIVER=offscreen`:
  - Engine init OK, loads DeathMatchPlus + DM-Deck16][ map, SDL viewport created.
  - **SDL3 detects the RP6 built-in pad as "Xbox Wireless Controller"** (25 buttons, 1 hat, 6 axes) via the
    SDL *Joystick* API (SDLDrv clamps to 12 buttons unless `IgnoreHat=True`). NOTE: under adb shell we have
    `/dev/input` access (input group); an app process will NOT — need an SDL virtual joystick bridge.
  - Default render device is OpenGLDrv (fails: no GL) → falls back to SoftDrv → asserts on
    `SDL_SetWindowRelativeMouseMode` (unsupported by offscreen driver). Expected; needs a real video backend.

## Test 3: in-app exec + Debian trixie runtime + Turnip (2026-11-21)

- APK with Debian ld-linux in jniLibs (`libld-linux.so`, `useLegacyPackaging`) runs `ucc-bin-arm64 help` from the
  app process: exit 0. **logcat: "seccomp disabled by setenforce 0"** — RP6 firmware is SELinux-permissive with
  app seccomp off, so stock Debian glibc works here. Other devices need Termux-patched glibc (clone3/rseq/…).
- Runtime now built by `native/runtime/fetch_debs.py` from Debian **trixie** (glibc 2.41) — required because
  pocket-foundry's Turnip (`turnip-9fb060e6d2d5`, Mesa 27.0-devel, glibc) needs GLIBC_2.38.
- Cross-compiling glibc programs works on Windows: NDK clang `--target=aarch64-linux-gnu --sysroot=native/runtime/out/root
  -fuse-ld=lld --rtlib=libgcc --unwindlib=libgcc`.
- `vkprobe` as app uid: **Turnip Adreno (TM) 740, Vulkan 1.4.363**, instance exts include VK_KHR_xlib_surface /
  VK_KHR_xcb_surface / VK_EXT_headless_surface. Env: `VK_DRIVER_FILES=<icd.json>` `TU_DEBUG=sysmem`.

## Milestone: UT469f running in the APK (2026-11-21)

Chain: GameActivity (Termux:X11 display subclass) → XServer (lorie via app_process) → utandroid-wm →
ut-bin-arm64 via trixie ld-linux + utshim → our libSDL3 (x11 + utandroid) → VulkanDrv → Turnip (MESA_VK_WSI_DEBUG=sw)
→ X11 → lorie → Android surface. Audio: ALAudio → our OpenAL Soft 1.24.3 (sdl3 backend only) → SDL utandroid →
AudioTrack (48 kHz stereo float). Input: Android pad → InputBridge (dead zone, triggers→buttons) → socket →
SDL virtual joystick "UTAndroid controller" (16 buttons / 0 hats / 4 axes) → SDLDrv Joy1..16, JoyX/Y/Z/R.

Fixes on the way:
- First-run renderer dialog: force `[FirstRun] FirstRun=469`.
- "Not shut down properly" dialog: UT's marker is `$HOME/.utpg/System/Running.ini`; deleted before each launch.
- SDLDrv enumerates joysticks once (no hot-plug): the pad is attached synchronously in SDL_InitJoysticks.
- Start = Escape key (menu / skip intro / close menu); Joy10 unbound to avoid a double toggle.
- Loader in jniLibs must match the libc version (trixie ld-linux), or: `undefined symbol: __tunable_is_initialized`.

Dev loop: `bash native/sdl/build-sdl.sh`, `gradlew :app:assembleDebug`, `bash native/deploy-dev.sh [--no-apk] [--game]`.
Logs: filesDir/logs/{x11,wm,ut}.log, home/.utpg/System/UnrealTournament.log.
