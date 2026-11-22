# Pocket Foundry: research notes for the UT99 (469f, Linux arm64, SDL3) Android port

Source: https://github.com/JulioFerrero/pocket-foundry (shallow clone, Nov 2026, app v0.9.x, versionCode 65), plus its
companion repo https://github.com/JulioFerrero/pocket-foundry-drivers (the downloadable GPU drivers). Paths below are
relative to the repo root unless prefixed `pfd/` (drivers repo). Nothing from either repo was executed.

What it is: an Android app (`dev.pocketfoundry`, minSdk 29, **targetSdk 34**, arm64-v8a only) that runs the **unmodified
official Factorio Linux arm64 glibc binary** on the phone, imported by the user. No emulation, no proot, no chroot.

---

## TL;DR architecture

```
 Android app process (bionic, Kotlin)                       separate Linux processes (glibc), spawned via ProcessBuilder
 ------------------------------------                       -----------------------------------------------------------
 LauncherActivity / Session.kt                              nativeLibraryDir/libld-linux.so  --library-path files/usr/lib
   |- AudioBridge  (AF_UNIX server -> AudioTrack)  <------      files/game/bin/arm64/factorio   (glibc 2.44 Termux-patched)
   |- InputBridge  (AF_UNIX server <- Gamepads/VirtualPad) ->      |- libSDL3 (patched 3.4.20): x11 video, "pocketfoundry"
   |- com.termux.x11.MainActivity (renders X screen on            |    audio driver, virtual joystick over socket
   |     a SurfaceView; touch/keys -> X events)                    |- Mesa Zink (GL) -> Vulkan ICD:
   |                                                               |     Turnip (KGSL, Mesa X11 WSI -> DRI3/Present)
 app_process CmdEntryPoint = Termux:X11 "lorie" X server  <---X11--|     or Android's own vendor Vulkan via libhybris
   (bionic, libXlorie.so from jniLibs; socket in files/usr/tmp)    |- pocketfoundry-wm (tiny xcb WM, forces fullscreen)
```

---

## 1. Running a glibc arm64 binary on Android

**Mechanism: exec the glibc dynamic loader, shipped as a fake JNI library in `nativeLibraryDir`, and pass the game
binary to it as an argument.** Not a custom in-process ELF loader, not proot, not a chroot.

- `runtime/assemble/build-assemble.sh` (end): `cp $USR/lib/ld-linux-aarch64.so.1 "$APK/jniLibs/arm64-v8a/libld-linux.so"`
  with the comment "The loader ships as a 'library': Android extracts those to the one place an app may exec from."
- `app/build.gradle`: `packaging.jniLibs.useLegacyPackaging true` ("Extracted native libraries are the only files an
  app may exec on Android 10+; the glibc loader is one."). `jniLibs.srcDirs += '../runtime/out/apk/jniLibs'`,
  `assets.srcDirs += '../runtime/out/apk/assets'`.
- `app/src/main/java/app/pocketfoundry/core/runtime/Program.kt`: `ProcessBuilder(listOf(paths.loader.path,
  "--library-path", libraries) + command)`; `paths.loader = File(nativeLibs, "libld-linux.so")`
  (`core/system/Paths.kt`). Environment is cleared and rebuilt: `HOME`, `TMPDIR`, `XDG_RUNTIME_DIR`
  (= `files/usr/tmp`), `PATH=/system/bin`, `LANG=C.UTF-8`, `XLOCALEDIR`, `GCONV_PATH`, a POSIX `TZ` computed from
  Java (no tzdata shipped), `DISPLAY=<path to X socket>`. stdout/stderr redirected to a log file. The PID is found by
  scanning `/proc/*/stat` for a child whose cmdline contains the program.
- **W^X / SELinux (targetSdk >= 29):** only `execve` of `app_data_file` is denied. The only thing exec'd is the loader,
  which lives in `nativeLibraryDir` (`apk_data_file`, exec allowed). The game binary and all .so files sit in
  `filesDir` and are `mmap(PROT_EXEC)`'d by ld.so, which untrusted_app is still allowed to do (same as `dlopen` from
  app storage). Consequences they handle:
  - Anything the game would `exec` itself fails, so glibc is patched so `system()`/`popen()` use `/system/bin/sh`
    (`runtime/glibc/build-glibc.sh`, sed of `@TERMUX_PREFIX@/bin/sh`).
  - Factorio re-execs itself through `/proc/self/exe` (which is the loader) after mod changes, so ld.so runs with
    only the game's args. Patch `runtime/glibc/patches/rtld-program-restart.patch`: if the first arg is an unknown
    option and `POCKETFOUNDRY_PROGRAM` is in the env, run that program; library path from `GLIBC_LD_LIBRARY_PATH`.
  - The X server is not exec'd as a file at all: `/system/bin/app_process -Xnoimage-dex2oat / com.termux.x11.CmdEntryPoint :0 -ac`
    with `CLASSPATH=<apk>` and `XLORIE_LIB=<nativeLibraryDir>/libXlorie.so` (`core/game/Session.kt` startXServer;
    termux-x11 patch 0001).
  - Helper tools (`pocketfoundry-wm`, drivercheck, speedtest) are glibc programs in `files/usr/bin`, launched the
    same way through the loader.
- **glibc:** GNU glibc **2.44** (submodule `runtime/glibc/src` -> github.com/kraj/glibc at tag glibc-2.44), with
  Termux's Android patches vendored from `termux-pacman/glibc-packages` commit c2b00b9e (`runtime/glibc/termux-patches/SOURCE`,
  MIT for the patches). Built with Debian's aarch64 cross gcc in a Debian 13 arm64 Docker container
  (`runtime/Dockerfile`, `runtime/deps.sh` pinned to one `snapshot.debian.org` date). Key patches:
  - Seccomp: syscalls Android's app seccomp filter kills are rerouted/faked (`fakesyscall.json`): e.g. `faccessat2`
    -> `faccessat`, `statx` -> generic, `epoll_pwait2` emulated, `set*uid/gid` -> return 0, and
    `clone3, rseq, set_robust_list, io_uring_*, futex_waitv, sem*/msg* (SysV IPC), mbind, ...` -> `ENOSYS`.
    `clone3.S` deleted. SysV shm emulated over ashmem (`shmem-android.c`).
  - `dl-execstack.c.patch`: never mprotect the stack executable.
  - Paths: all `/etc`, `/tmp`, `/var` prefixed with `$PREFIX` (`set-dirs.patch` etc.), passwd/group from Android IDs
    (`android_passwd_group.c`), `/dev/std*` -> `/proc/self/fd/*`.
  - **PREFIX is baked in at build time:** `PREFIX=/data/data/dev.pocketfoundry/files/usr` (`runtime/build.sh`). The
    runtime must be unpacked exactly there; `Runtime.misplaced()` detects a second user/work profile. A fork needs its
    own package path and a rebuild.
- **Runtime payload:** `assets/runtime.tar.xz` (glibc libs, Debian 13 arm64 libs such as libstdc++.so.6, libX11/xcb*,
  libvulkan.so.1 (Khronos loader), libdrm, Mesa zink/GLX/EGL, libhybris, patched SDL3, xkb data, X11 locale, C.UTF-8
  locale built with localedef, gconv subset). Unpacked to `files/usr` on first run / version change
  (`core/runtime/Runtime.kt`, `install()`), with a native liblzma JNI (`app/src/main/cpp/xz_jni.c`) for speed.
  `needed.py` walks DT_NEEDED to copy the transitive closure from Debian.
- Process lifecycle (`Session.kt`): pause = `SIGSTOP` when the activity leaves the foreground, `SIGCONT` on resume;
  quit = SIGTERM then SIGKILL after 10 s; a `specialUse` foreground service keeps it alive; the game's log is tailed
  to detect "no window" / "DEVICE LOST" / crash markers. `GLIBC_TUNABLES` lowers malloc mmap/trim thresholds to 1 MB
  to save RAM. `/etc/resolv.conf` written from ConnectivityManager DNS servers; Android properties dumped via
  `getprop` for libhybris.

## 2. Graphics path

**An X server (Termux:X11) inside the app, and the game draws with X11 + Vulkan WSI into it.** No custom SDL video
backend; SDL3 uses its stock **x11** driver (`SDL_VIDEO_DRIVER=x11`, `core/runtime/Sdl.kt`).

- **X server:** Termux:X11 ("lorie", GPL-3.0) is a git submodule (`third_party/termux-x11`), included as Gradle
  module `:lorie` (`settings.gradle`) and patched by `tools/setup.sh` with `third_party/patches/termux-x11/0001..0018`
  (load libXlorie from `XLORIE_LIB`, own port, hidden cursor, landscape, logging, arm64-only, size-optimized build,
  renderer lock fix, etc.). Its `com.termux.x11.MainActivity` is the game screen (declared in the app manifest with the
  launcher intent removed); it shows the X root window on an Android SurfaceView. The X server (bionic native) runs in
  a separate `app_process` process; the X socket is `files/usr/tmp/.X11-unix/X0` and `DISPLAY` is set to that path.
  Termux:X11 supports DRI3 with dma-buf fds and with **Android hardware buffers (DRI3 modifier 1255)**.
- **Window manager:** `runtime/programs/pocketfoundry-wm.c`, ~xcb, maps every window full screen
  (`_NET_WM_STATE_FULLSCREEN`) because SDL's fullscreen request is otherwise ignored.
- **GPU drivers (Factorio is OpenGL, so everything goes through Mesa Zink; GL -> Vulkan):**
  - Runtime Mesa: `runtime/mesa/build-mesa.sh`, Mesa from `lfdevs/mesa-for-android-container` (tag
    mesa-27.0.0-devel-20261025-r1), `-Dplatforms=x11 -Dgallium-drivers=zink -Dvulkan-drivers= -Dglx=dri -Dglvnd=enabled`.
  - **Turnip (Adreno, incl. 740)** is a downloadable "catalog" driver built in `pfd/` (`pfd/drivers/turnip.json`):
    same Mesa fork, `-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl,msm -Dplatforms=x11`, talking to
    `/dev/kgsl-3d0` directly from the app sandbox. WSI = Mesa's own X11 WSI (DRI3/Present to Termux:X11). Patches:
    `mesa-x11-wait-for-the-frame-before-presenting.patch` (always CPU-wait the fence before PresentPixmap: KGSL dma-bufs
    have no implicit fences, otherwise black flashes), `mesa-x11-android-hardware-buffers.patch` (optional: if env
    `POCKETFOUNDRY_HYBRIS` names libhybris-common, swapchain images are allocated as AHardwareBuffers via
    `android_dlopen("libnativewindow.so")` and sent with modifier 1255, avoiding Termux:X11's per-frame CPU upload of
    dma-bufs, "at 3168x1440 and 60 FPS, 40% of a core"; I found no place that sets this env in either repo, so it
    looks opt-in/experimental), and an a635 macrotile fix. Selected with `VK_DRIVER_FILES=<icd json>`.
    Device-specific env "fixes": `pfd/fixes/adreno-740-android-16.json` (`FD_DEV_FEATURES=enable_tp_ubwc_flag_hint=1`
    on Android 16+); option `TU_DEBUG=noubwc` for black flashes. Tested 60 FPS on Galaxy S23 / AYN Thor (Adreno 740),
    which is the same GPU as the Retroid Pocket 6.
  - **"Phone's Vulkan"** (built-in, for Mali/PowerVR/Xclipse): **libhybris** (`runtime/hybris/build-hybris.sh`, taowen/libhybris
    branch arlinux + Halium android-headers) loads Android's bionic vendor Vulkan driver into the glibc process and
    exposes it as an ICD (`hybris_icd.json`), plus a compat layer `VK_LAYER_HYBRIS_compat` (incl. MaliBridge) and
    Khronos's timeline-semaphore layer. Patch `runtime/hybris/patches/0001-x11-present-through-dri3-without-tawc-dri.patch`
    makes its X11 WSI present AHardwareBuffers to Termux:X11 via DRI3/Present (modifier 1255). Needs
    `HYBRIS_PROPERTY_FILE` (props dumped by the app) and `HYBRIS_VNDK_VERSION`.
  - Others: Freedreno GL, PanVK, RADV-Xclipse, llvmpipe CPU, MobileGlues (GL -> phone GLES through libhybris EGL,
    offscreen pbuffer then copied to AHBs, `pfd/gles/mgx11.c`).
  - Driver selection/fallback, catalog download (ECDSA-signed `catalog.json`, SHA-256 checked), driver self-test
    (`core/drivers/DriverCheck.kt`, `runtime/programs/pocketfoundry-drivercheck.c`) and per-driver env are in
    `core/drivers/Drivers.kt` / `Catalog.kt`.
- Not used: no direct ANativeWindow/EGL surface bridging into the game process, no SDL Android backend, no Wayland.

## 3. Input

Two channels:
- **Touch, mouse, keyboard -> X server.** Termux:X11's MainActivity converts Android touch/key events into X input
  (`com.termux.x11.input.InputEventSender`, patch 0008 for held keys). The app wraps the activity's `Window.Callback`
  with `ui/controller/InputRouter.kt`: fingers on the on-screen controller go to `ControllerView`, other fingers to
  `PointerGestures` (tap/drag/long-press/pinch -> X mouse via `InputStub`), Back opens the launcher. SDL gets these as
  normal X11 events. Text input relies on Xlib locale + Compose files (shipped).
- **Gamepad -> a virtual SDL joystick over a Unix socket.** `core/controller/Gamepads.kt` reads Android `KeyEvent`
  (BUTTON_A/B/X/Y, L1/R1, thumbs, select/start, D-pad keys, L2/R2 as fallback) and `MotionEvent` axes
  (X/Y, Z/RZ or RX/RY, LTRIGGER/BRAKE, RTRIGGER/GAS, HAT_X/Y) -> `VirtualPad` -> `InputBridge.kt`, which serves
  `files/usr/tmp/pocketfoundry-input`. Wire format: 4-byte messages `{'B'|'A', index, int16 LE}` (SDL gamepad
  button/axis order; triggers rescaled to -32768..32767), and `'R'` rumble back to the app (vibrator of the pad or
  phone). In the game process, `runtime/sdl/pocketfoundry/SDL_pocketfoundry.c` starts a thread on `SDL_InitJoysticks`
  (patched `src/joystick/SDL_joystick.c`), connects, and creates an `SDL_AttachVirtualJoystick` gamepad
  (VID/PID 045e:0b12, Xbox Series) driven by `SDL_SetJoystickVirtualButton/Axis`. `SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000`
  hides any real `/dev/input` devices to avoid double input. Built-in handheld controls are detected
  (`isGamepad`: non-external devices need sticks), so a Retroid's pad works this way.
- `KEYCODE_BUTTON_MODE` (home) opens the pause menu.

## 4. Audio

Custom SDL audio driver, Unix socket, Java AudioTrack.
- `SDL_pocketfoundry.c`: `AudioBootStrap POCKETFOUNDRYAUDIO_bootstrap` (`demand_only`, selected by
  `SDL_AUDIO_DRIVER=pocketfoundry`). Opens `$POCKETFOUNDRY_AUDIO` socket, forces F32LE / 2 ch / 48 kHz (SDL converts),
  sends header `"PFA1" + int32 rate + int32 channels`, then raw frames; `PlayDevice` is a blocking `send()`, so the
  socket backpressure is the clock (small `SO_SNDBUF`).
- `core/game/AudioBridge.kt`: `LocalServerSocket` on a filesystem path (not abstract namespace, for privacy),
  reads header, creates `AudioTrack` (PCM_FLOAT, USAGE_GAME, LOW_LATENCY, buffer 2048 frames) and `write(..., WRITE_BLOCKING)`.
- Why not AAudio directly: "The game's glibc process can't load Android's libraries" (comment in SDL_pocketfoundry.c).
  (libhybris could in principle, but they chose the socket.)

## 5. Game data provisioning

- User downloads the official Linux arm64 `.tar.xz`, picks it via the system file picker (SAF `ACTION_OPEN_DOCUMENT`
  Uri). `core/game/GameImport.kt` streams it through `contentResolver.openInputStream` -> native xz -> commons-compress
  tar (`core/system/Archives.kt`, path-traversal checked, exec bits/symlinks kept) into `files/game.importing`, then
  renames to `files/game`. APK contains no game data.
- Writable data (config, saves, mods) live in `files/factorio`; exposed to the Files app via a
  `DocumentsProvider` (`core/saves/GameFilesProvider.kt`, authority `${applicationId}.files`). Config written by the app
  (`FactorioConfig.kt`) and passed with `--config`.

## 6. Build system and license

- Android: Gradle (AGP, compileSdk 36, NDK version from Termux:X11's `version.gradle`), Kotlin, one tiny CMake/NDK
  lib (`app/src/main/cpp`, liblzma via FetchContent). `:lorie` (Termux:X11) builds its X server natively via its own
  CMake. `tools/build.sh` wraps gradle; `tools/fdroid-build.sh` for reproducible F-Droid builds.
- Linux runtime: `runtime/build.sh all` builds pieces `glibc mesa sdl programs hybris` then `assemble` in an arm64
  Debian 13 Docker container (or `--here` cross-compile on x86-64 Debian, with qemu-aarch64 for running localedef).
  All sources are submodules pinned to commits; Debian packages from a fixed snapshot; reproducible tar/xz.
  Output: `runtime/out/apk/{assets/runtime.tar.xz, runtime.version, runtime.prefix, jniLibs/arm64-v8a/libld-linux.so}`.
  No opaque prebuilt blobs in git (drivers are CI-built in pfd and downloaded at runtime).
- License: **GPL-3.0-only** (forced by Termux:X11). Bundled: glibc LGPL-2.1, Mesa MIT, SDL zlib (their SDL additions
  are explicitly zlib, `SDL_pocketfoundry.c` header), libhybris Apache-2.0 (its Vulkan bits GPL-3.0 incl. MaliBridge),
  Vulkan-ExtensionLayer Apache-2.0, Termux glibc patches MIT. pfd scripts GPL-3.0. Reusing code means a GPL-3.0 app
  (fine for an open port; UT's own binaries are user-supplied and not linked into the GPL work, as here with Factorio).

## 7. Reuse for UT99 469f (SDL3, VulkanDrv, OpenAL, Adreno 740)

### Directly reusable (largely as-is)
1. **Launch model**: loader in jniLibs + `Program.kt` env/ProcessBuilder + `Runtime.kt` unpack + Termux glibc build
   (`runtime/glibc/*`). Glibc 2.44 is newer than UT's 2.34 requirement, which is fine. Change `PREFIX` to your package
   (`/data/data/<your.pkg>/files/usr`) and rebuild. Keep the rtld restart patch idea only if UT re-execs itself.
2. **Termux:X11 + patches + pocketfoundry-wm + Session start/stop/pause (SIGSTOP/SIGCONT) + foreground service.**
3. **Turnip from pfd** (`pfd/drivers/turnip.json` + the three Mesa patches, `pfd/fixes/adreno-740-android-16.json`).
   For UT, **skip Zink entirely**: VulkanDrv speaks Vulkan, so the chain is SDL3 x11 window ->
   `SDL_Vulkan_CreateSurface` (VK_KHR_xlib/xcb_surface) -> Khronos `libvulkan.so.1` -> Turnip ICD (`VK_DRIVER_FILES`) ->
   Mesa X11 WSI -> DRI3/Present -> Termux:X11. Consider enabling the AHB WSI path (`POCKETFOUNDRY_HYBRIS` pointing at
   libhybris-common) to avoid the CPU upload cost, and the libhybris ICD as a fallback ("phone's Vulkan" through
   Qualcomm's own driver).
4. **SDL3 additions**: `runtime/sdl/pocketfoundry/SDL_pocketfoundry.[ch]` + patch 0001 (audio driver + virtual gamepad)
   + `AudioBridge.kt`, `InputBridge.kt`, `Gamepads.kt`, `VirtualPad.kt`. zlib-licensed on the SDL side.
5. Runtime assembly (`build-assemble.sh` + `needed.py`) to pull X11/xcb/libstdc++/libvulkan closures from Debian.
6. SAF import with native xz untar, DocumentsProvider for user files.

### SDL-specific differences / what to change
- **Factorio links SDL3 statically** and uses `SDL3_DYNAMIC_API=<runtime libSDL3.so.0>` to redirect to the patched SDL.
  **UT ships `libSDL3.so.0` dynamically**, so simply replace it (put the patched build first in `--library-path` or
  overwrite the one in `System/`). It must be the same or newer SDL3 ABI than UT was built against (they use 3.4.20).
- Their SDL build turns off `SDL_GPU` and `SDL_RENDER_VULKAN` but keeps `-DSDL_VULKAN=ON` and `-DSDL_X11=ON`, which is
  what VulkanDrv needs. Keep `SDL_LIBURING=OFF` (io_uring is seccomp-killed).
- Frame pacing/FPS counter hooks only `SDL_GL_SwapWindow` (patch 0002); with Vulkan they do nothing. Pace via VulkanDrv
  or a Vulkan layer if you want a cap.
- `SDL_HINT_VIDEO_X11_SCALING_FACTOR` < 1 (patch 0003) is irrelevant for UT (no HiDPI UI logic).
- **Audio: UT uses OpenAL (`libopenal.so.1`), not SDL audio.** Options: (a) build OpenAL Soft with its SDL backend
  (check whether your OpenAL Soft version has SDL3; older ones only have SDL2) so it lands on the patched SDL's
  `pocketfoundry` driver; (b) add a small OpenAL Soft backend that speaks the same `PFA1` socket protocol; (c) load
  AAudio/OpenSL inside the glibc process via libhybris (more fragile). (a) or (b) reuse `AudioBridge.kt` unchanged.
  Also check whether 469f's ALAudio has an SDL audio fallback.
- **Input:** keyboard/mouse arrive as X11 events (works for UT menus/console). Gamepad: the virtual SDL gamepad works if
  UT 469f's SDL backend handles SDL gamepad/joystick events (verify; otherwise map pad buttons to X key events in the
  Kotlin layer, e.g. via Termux:X11's `InputStub`/key injection). FPS needs mouse-look; Factorio's touch gestures map to
  absolute cursor moves, so for UT you'd want relative motion (SDL relative mouse mode over X11 uses XInput2 raw
  motion/warping; Termux:X11 supports pointer capture for physical mice, but stick -> mouse-look is best done by UT's
  own joystick axes).
- **/proc/self/exe is the loader** (`libld-linux.so`). If UT locates its `System/` dir from `/proc/self/exe`, it will
  resolve to nativeLibraryDir. Mitigate by `chdir` into `System/` before launch and/or ld.so `--argv0`, or a glibc
  patch similar to `rtld-program-restart.patch`.
- Vulkan-only removes the need for Mesa Zink/GLX/glvnd and MobileGlues from the runtime; keep `libvulkan.so.1`, xcb
  dri3/present/sync/xfixes/shm, libdrm, libexpat, libzstd, libz, libX11-xcb, libxshmfence (the `driver_seeds` list in
  `build-assemble.sh`).
- Android 13 on Retroid Pocket 6 with Adreno 740: matches tested configurations (Galaxy S23/AYN Thor, Turnip, 60 FPS for
  Factorio). The Android 16 UBWC fix is not needed on 13.
- Everything is arm64-only, landscape, minSdk 29, targetSdk 34: same constraints apply to you.
