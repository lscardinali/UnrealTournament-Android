# Fully native UT469 on Android: evaluation and plan (2026-11-22)

"Fully native" = the game's code runs **inside the Android app process**, on Android's own libc (bionic), drawing
with **Qualcomm's system Vulkan driver** straight into the app's `SurfaceView`, with stock **SDL3 for Android**
(input, window, lifecycle) and **OpenAL Soft on AAudio**. No glibc process, no X server, no Turnip, no sockets,
no frame copies. Builds on `architecture-options.md` (option B) with what we learned shipping option A.

## Why bother (measured on option A today, DM-Deck16][ at 1080p/120 Hz)

| Cost in option A | Today | Native |
|---|---|---|
| X server process | 30–55% of a core | gone |
| Termux:X11 renderer + app | ~25% of a core | SDL's surface only |
| Per-frame copies (GPU readback → X socket → X buffer → GL composite) | ~3–4 ms of the 8.3 ms budget | zero-copy (vendor WSI) |
| Frame pacing | custom pacer + 5.5 ms lead | Vulkan FIFO on the real display (or `VK_GOOGLE_display_timing`) |
| GPU driver | Turnip (community Mesa) | Qualcomm's driver (Turnip still possible via adrenotools) |
| Portability | needs SELinux-permissive firmware or a Termux-patched glibc | works on any Android 10+ arm64 device |
| Lifecycle | pause/resume/kill handled around three processes | standard SDLActivity |

Expected result: same 120 fps with far more headroom (better battery/thermals, room for 1080p + effects), plus
correct vsync for free.

## What we must build

```
APK
├─ Java: SDLActivity (+ our launcher, overlay, settings)          ← stock SDL3 Android project
├─ libSDL3.so, libSDL3_ttf.so  (NDK)                              ← stock, + small hooks
├─ libopenal.so (NDK, AAudio) + sndfile, mpg123, xmp, lame (NDK)   ← rebuilds, no code changes
├─ libutloader.so  (NDK)  ← NEW: ELF loader + glibc→bionic shim + TLS + game thread
└─ game files (unchanged OldUnreal Linux arm64 .so files + libstdc++.so.6 / libgcc_s.so.1 from a glibc distro)
```

### 1. ELF loader (new, ~1–2k lines C)
Maps ut-bin-arm64 (PIE), Core/Engine/SDLDrv/Render/VulkanDrv/ALAudio/IpDrv/…, libstdc++.so.6, libgcc_s.so.1.
The binaries use only 6 relocation types (ABS64, GLOB_DAT, JUMP_SLOT, RELATIVE, TLSDESC, TLS_TPREL64): no IFUNC,
COPY, TEXTREL or BTI. Import resolution order: shim table → our loaded modules → bionic `dlsym` (SDL3, OpenAL, …:
same C ABI). The engine's own `dlopen("VulkanDrv.so")` comes back into the loader; `libvulkan.so.1` maps to the
system `libvulkan.so`.

### 2. glibc → bionic shim (new)
242 glibc symbols in the game modules (+ libstdc++'s). ~200 are pass-through (same name and arm64 ABI);
~25–30 need real wrappers (`architecture-options.md` §1.3): `sigaction` (struct layout), `getaddrinfo`
(addrinfo field order), `sysconf` (constants), `__ctype_b_loc`, stdio/environ data symbols, `dl_iterate_phdr`
(so C++ exceptions find our modules), `__libc_start_main`, `pthread_create` (TLS), iconv name fix-ups.
libstdc++ needs a "C"-locale stand-in for glibc's locale internals. References: libhybris `hooks.c`, Box64
`wrappedlibc`, mcpelauncher `libc-shim`.

### 3. Thread-local storage (the one novel piece) — **feasibility confirmed on the RP6**
Every module reaches Core's 64 KB `UnGuardBlockTLS::Data` through **one** GOT slot per module
(`R_AARCH64_TLS_TPREL64`, counted: 1 per module) holding a fixed offset from the thread pointer; bionic can't add
64 KB of static TLS after start-up.
Plan: give every engine thread (our game thread + the engine's own `pthread_create`s via the shim) a large stack,
carve a reserved block at its top, and set that GOT slot to the block's (negative) offset from TP.
**Probe (`native/tools/tlsprobe.c`, run on the RP6):** for every `pthread_create`d thread — default, 1 MB, 8 MB,
16 MB stacks — `TP − stack top = 848 bytes`, constant; a 256 KB carve at the stack top is usable. So one constant
offset is valid on every engine thread. (The value differs inside the app process — more static TLS — but is
constant there too; measured at start.)
Preconditions checked: the game modules import **no** SDL event watchers/filters and **no** OpenAL callbacks, only
`pthread_create` — engine code never runs on threads we didn't create.

### 4. SDL3 for Android + interposer
Stock SDL3 Android backend gives window, `VK_KHR_android_surface`, lifecycle, joysticks. Our existing work moves
into a thin interposer on the `SDL_*Joystick*` imports (we resolve them, so we can wrap them):
stable 16-button/4-axis layout, radial dead zone, triggers as buttons, Start→Escape, menu cursor (relative mode
off → stick moves mouse; on Android SDL the cursor is drawn by UT/SDL, no X server quirks), look-axis frame-time
scaling, window focus. Same logic as `SDL_utandroid.c`, no sockets.

### 5. Audio
OpenAL Soft 1.24.3 built with the NDK (AAudio/OpenSL backends) + libsndfile, mpg123, libxmp, lame. ALAudio's
`al*`/`alc*` imports resolve to it by name.

### 6. App
Port from option A: launcher, `GameConfig` ini handling, perf overlay (FPS from a `vkQueuePresentKHR` hook in the
loader), data install. Drop: X server module, Termux:X11, glibc runtime, Turnip, bridges, pacer.

## Plan with go/no-go gates

Everything until M2 runs as a **plain bionic test executable over adb** (fast loop, no APK).

| Step | Work | Gate (stop and reconsider if it fails) |
|---|---|---|
| **M0** loader spike | Load Core.so + libstdc++ + libgcc_s; minimal shim; TLS carve | `UnGuardBlockTLS` access works on 2+ threads; a C++ `throw` inside Core is caught across modules |
| **M1** ucc | Run `ucc-bin-arm64 help` and a package load (`ucc batchexport` or similar) | Same output as the glibc run (we have it); iconv/ini/file I/O right |
| **M2** game in the app | SDLActivity + NDK SDL3/SDL3_ttf; ut-bin on the game thread; SoftDrv first, then VulkanDrv on the system driver | Intro renders at 1080p/120 Hz, zero-copy |
| **M3** audio | NDK OpenAL Soft + codecs | Music and effects play |
| **M4** input + app features | Interposer, ini config, overlay, launcher | Feature parity with option A |
| **M5** hardening | Signals via libsigchain (never take SIGSEGV from ART), pause/resume, crash logs with a module map, `nm` diff script for future OldUnreal patches | 30-minute session without issues; resume after background |

Option A stays the shipping build until M4 passes; both can live in the same repo.

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| A struct-ABI mismatch we miss → rare crash | medium | Wrapper audit against glibc/bionic headers; debug build with guard values; crash log with module offsets |
| libstdc++ locale internals | medium | "C" locale only; fallback: build GCC's libstdc++ for bionic |
| An engine thread we didn't create touches TLS | low (checked imports) | TLS block sentinel check in the shim's `pthread_create` trampoline; abort with a clear log |
| Qualcomm Vulkan driver bug with VulkanDrv | low–medium | Swap in Turnip-for-Android via libadrenotools (in-process only — another native win) |
| Debugging custom-loaded code (lldb doesn't see it) | certain | Publish modules through `r_debug`/GDB JIT interface; module-map log + addr2line |
| Future OldUnreal patch adds imports | certain, small | Scripted import diff fails loudly at load |

## Effort

About 5–7 weeks of focused work for one engineer: loader 1–2 wk, shim + libstdc++ 2 wk, SDL/OpenAL/codecs/app
1 wk, input/features/hardening 1–2 wk. The two technical unknowns (TLS, cross-module exceptions) are both
covered by M0, which is a few days, so the big uncertainty is retired early and cheaply.

## Alternative worth asking for first: an official Android build

OldUnreal builds 469 for Linux arm64 with SDL3; SDL3 already targets Android and the NDK toolchain is close to
what they use. A bionic build from them would make all of the above unnecessary (no loader, no shim, no TLS
trick). Worth a message to the OldUnreal team (forum/Discord) with this prototype as a demonstration of interest —
in parallel, not instead.

## Intermediate options (if native stalls)

| Option | Gain | Cost |
|---|---|---|
| A + zero-copy Mesa (AHardwareBuffer swapchain → Termux:X11 DRI3) | removes ~2–3 ms copies and most X CPU | needs a Linux build env for Mesa (WSL/Docker); keeps X |
| A without X (custom SDL video backend + AHB swapchain shared to the app) | removes X server entirely | ~3–4 weeks, still Turnip and two processes |
| glibc process + libhybris (vendor Vulkan in the glibc process, AHB swapchain) | vendor driver | pocket-foundry tried and left it disabled; two libcs in one process; not simpler than native |

## Spike results (2026-11-22, RP6 over adb, `native/utloader/`)

`bash native/utloader/build.sh && bash native/utloader/run-spike.sh [ucc <args>]` — a plain bionic executable
(`utl-spike`) with the loader (`loader.c`), TLS carve (`tls.c`) and glibc shim (`shim.c`).

**M0 — passed (8/8):** libgcc_s + GNU libstdc++ (Debian trixie) + UT's Core.so + a glibc test library load and run
their constructors; Core's own initial-exec TLS (`UnGuardBlockTLS::GetBackTrace`) on two threads; cross-module
TPREL64 lands in each thread's carved block at the predicted address; per-thread isolation; TLSDESC with initial
value; exception thrown in libstdc++ caught in another custom-loaded module; own exception unwound through 21
frames. Measured TP − stack top = 848 bytes; 256 KB block at TP−299008 (Core's 64 KB + libstdc++ + libgcc_s).

**M1 — passed:** `ucc-bin-arm64 help` runs main() to completion (exit 0) inside the bionic process — no glibc
anywhere. `ucc batchexport Engine.u class uc <dir>` loads Editor.so/Editor.u via the engine's own dlopen,
runs the script VM and exports 88 classes, clean log.

Shim additions found by running (trap stubs named each one): gettext family (identity), pthread cancel
(no-op), WINDOWS-1252 iconv (bionic lacks it; UT needs it for ANSI<->wide text), `bcmp`, `readlink
(/proc/self/exe)` → loaded program, `__cxa_thread_atexit_impl` (bionic's looks up the dso in its own linker and
aborts for our modules → own per-thread list), `sysconf` 249/250, `dlsym(module)` searching dependencies,
clearing bionic's stale `dlerror`.

Known gaps for M2+: static destructors / process exit order (harness `_exit`s), libSDL3.so.0 is OldUnreal's glibc
SDL loaded through the loader (M2 replaces it with Android SDL3), signals untested, no UI/render yet.

## M2 result (2026-11-22): the game runs natively in the app

`MainActivity --ez native true` → `NativeGameActivity` (SDLActivity) → `libutloader.so` SDL_main → UT's unmodified
`ut-bin-arm64` on a utl thread, Android SDL3/SDL3_ttf standing in for the game's glibc SDL, VulkanDrv on
**Qualcomm's driver** (Adreno 740, Vulkan 1.3.128, driver 512.676.53), straight to the SurfaceView.

Measured in DM-Deck16][ at 1920×1080/120 Hz: **119.7 fps on screen, 100% of refreshes on time** (SurfaceFlinger,
plain FIFO, no pacer); **~55% of one core for the whole app** (option A: game ~50% + X 30–55% + app ~25%); no
frame copies. Validation layer (`--ez vkdebug true`, debug builds) clean after the fixes below.

What it took (all in native/utloader):
- `_start` must run on the thread's **own stack** (a malloc'd stack made ART's fault handling die silently).
- Signals: `signal()`/`sigaction()` both through libsigchain; the engine never gets SEGV/BUS/ILL/FPE/ABRT/TRAP/SYS
  (its handlers assume our TLS block); its `sigaltstack` is ignored (too small for ART's handler).
- SDL interposer: `SDL_SetHint(SDL_VIDEO_DRIVER, "x11,wayland")` → android; relative paths for `SDL_LoadBMP` /
  `TTF_OpenFont` made absolute (Android SDL reads relative paths from APK assets); the splash is skipped (Android
  SDL allows one window) and its error box swallowed.
- Vulkan interposer (wrapped vkGetInstanceProcAddr/vkGetDeviceProcAddr), found with the Khronos validation layer:
  `compositeAlpha` OPAQUE → INHERIT (Android surfaces), `preTransform` → IDENTITY (VulkanDrv echoed the panel's
  rotation without rotating: sideways image), and every command pool gets RESET_COMMAND_BUFFER (VulkanDrv re-begins
  command buffers; Turnip resets implicitly, Adreno failed the begin and crashed in vkCmdPipelineBarrier).
- Crash logger in libutloader (pc/lr/backtrace as module+offset, flushes UT's log); this firmware writes no
  tombstones.

Next: M3 audio (NDK OpenAL Soft → AAudio; currently `-nosound`), M4 input (Android SDL reports the RP6 pad as
>16 buttons/6 axes: port the SDL_utandroid layout, dead zones, Start→Escape, menu cursor, look scaling as an
SDL_Joystick interposer), overlay/launcher, then M5 hardening (exit/destructors, pause/resume).

## M3 result (2026-11-22): audio in the native build

OpenAL Soft 1.24.3 built for Android (`native/openal/build-openal-android.sh`: OpenSL ES backend, static libc++),
shipped as `libopenal.so`; the game's `libopenal.so.1` maps to it (utloader alias). Its mixer runs on its own bionic
threads and never calls into the game. The decoders ALAudio needs (libxmp for .umx music, libsndfile, mpg123,
lame) load as OldUnreal's glibc builds through the loader. Shim additions: `__fprintf_chk`, `__sigsetjmp`
(mapped straight to bionic `sigsetjmp`: it returns twice, so no wrapper; glibc's jmp_buf is larger than bionic's).
On device: OpenAL "Initialized backend opensl", default device at 44.1 kHz, Android reports an OpenSL ES player
from our pid in state "started" with an active mixer track. Sound is now on by default in the native launch
(`--ez nosound true` turns it off).

## Lifecycle fixes (2026-11-22): background / return

- **Audio kept playing in the background**: SDL pauses only its own audio devices; OpenAL has its own OpenSL ES
  device. The shim tracks the devices ALAudio opens (`alcOpenDevice`/`alcCloseDevice`) and pauses/resumes them
  with ALC_SOFT_pause_device from an **SDL event watch** registered by the loader (WILL_ENTER_BACKGROUND /
  WILL_ENTER_FOREGROUND). The watch is needed: SDL blocks the game thread inside the next event pump before
  `SDL_PollEvent` could return these events. Verified: Android reports our player "stopped" in the background,
  "started" after return. SDL's own block-on-pause keeps the game thread paused while in the background.
- **Black screen after returning**: Android destroys the SurfaceView's surface in the background and makes a new
  one; VulkanDrv's VkSurfaceKHR pointed at the dead one. The Vulkan interposer keeps VulkanDrv's surface handle as
  a stand-in: when SDL's ANativeWindow (`SDL.window.android.window`) changes, it creates a fresh surface, reports
  `VK_ERROR_OUT_OF_DATE_KHR` from the next acquire so VulkanDrv rebuilds its swapchain, and maps the stand-in to
  the current surface in every surface query and in `vkCreateSwapchainKHR` (no `oldSwapchain` across surfaces).
  `SURFACE_LOST` from acquire/present is reported as `OUT_OF_DATE`. Verified: picture back after Home → return.

## M4 (2026-11-22): controller + overlays in the native build

- **Same controller code as the X11 build**, compiled into the Android SDL (`-DSDL_UTANDROID=ON`) in a
  *native-pad mode* (`$UTANDROID_NATIVE_PAD`, set by libutloader): no socket; the virtual "UTAndroid controller" is
  attached at joystick init and each `SDL_UpdateJoysticks` poll reads the real pad through SDL's gamepad API
  (positional A/B/X/Y), shaped in C exactly as the Kotlin InputBridge did (radial dead zone 0.18, triggers ->
  buttons with hysteresis 0.45/0.30). Start->Escape, menu cursor, keyboard focus and look scaling are shared code.
  Rumble goes to the real pad.
- **Real pad hidden**: the shim filters `SDL_GetJoysticks` to "UTAndroid controller". UT log: 1 joystick,
  16 buttons, 0 hats, 4 axes — same as the X11 build, same User.ini bindings.
- **Shared state** (libutloader `shared_state`, address passed as `$UTANDROID_SHARED`): presented-frame count
  (incremented in the `vkQueuePresentKHR` wrapper; drives look scaling) and menu/cursor/window size.
- **Menu cursor**: Android draws no system cursor without a real pointer, so SDL moves an invisible mouse
  (`SDL_SendMouseMotion`) and `CursorOverlay` draws an arrow from `nativeCursorState` (JNI) each display frame.
- **Perf overlay** in the native activity (`inProcess` mode: one process), FPS from the loader's frame count;
  L3+R3 toggles. Measured mid-match: 119.6 fps, 8.4 ms, whole app ~75% of one core.

**M4 confirmed by hands-on play (2026-11-22):** controls, menu cursor and overlay work in the native build.

## M5 (2026-11-22): hardening + native as default

- **Default launch** is the native build; `--ez x11 true` opens the X server build (fallback).
- **Exit**: UT's console `exit` → clean engine shutdown ("Object subsystem successfully closed"), process ends;
  libutloader also `_exit`s after `main` returns, so a relaunch never re-runs main() over a used engine.
- **Crash log** also written to `files/logs/crash.txt` (pc/lr/backtrace as module+offset).
- **Heap pointer tagging off** (`android:allowNativeHeapPointerTagging="false"`): Android 11+ malloc returns
  top-byte-tagged pointers (0xb4...), which UT's glibc-built engine never saw; an intermittent crash in
  `ULinkerLoad`/`StaticFindObject` (garbage pointer) when the console opened the menu root window. Not reproduced
  after the change (one retest).
- Stale `dlerror` from our own failed lookups cleared (loader + SDL bridge); `files/game/Save` created at launch
  (UT's `SavePath=../Save`; the install ships no Save folder).
- Debug logging removed (InputBridge, SDL heartbeat back to info); release build signed with the debug key for
  sideloading (`app-release.apk` 6.8 MB, no validation layer, stripped).
- Device tests: 3 background/return cycles (audio pause/resume + surface rebuild each time); 60 s on-screen:
  7270 frames, 119.6 fps, 2 repeated frames.

### Load / power: native vs X11 (native/tools/load_compare.py, DM-Deck16][, bots fighting, ~87 s each, on AC)

| | Native | X11 build |
|---|---|---|
| CPU, all app processes | 60.5% of a core | 143.4% (game 51 + X server 62 + app 30) |
| CPU clocks cluster0 / cluster3 | 1252 / 1248 MHz | 1317 / 1793 MHz |
| GPU busy @ 401 MHz | 41% | 54% |
| GPU temperature | 41 C | 46 C |
| Net battery current (charging) | +5.66 W | +4.26 W (≈1.4 W more load) |
