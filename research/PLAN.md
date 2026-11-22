# UT99 469f → Android (Retroid Pocket 6): architecture plan

Source reports: `pocket-foundry.md`, `wowforeverywhere.md`, `winlator-gamenative.md`, `architecture-options.md`,
`controller-input.md`, `device-findings.md` (hands-on results).

## Already proven on the RP6 (adb shell)
- Debian glibc 2.36 loader runs `ut-bin-arm64`; engine boots and loads DM-Deck16][ (headless).
- Needs: `GCONV_PATH` (iconv), `HOME`, and `native/utshim` (readlink `/proc/self/exe` → `$UT_EXE_PATH`).
- SDL3 sees the built-in pad through the raw SDL_Joystick API (adb shell only — apps can't open `/dev/input`).

## Option A — separate glibc process (the pocket-foundry recipe)  ← recommended first
| Layer | Choice |
|---|---|
| Launch | `libld-linux.so` (ld-linux renamed) in jniLibs, `useLegacyPackaging`, ProcessBuilder; game + glibc libs in filesDir |
| libc | glibc with the Termux Android patches (seccomp: clone3/rseq/set_robust_list/SysV shm), rebuilt for our package prefix |
| Display | Termux:X11 as a module (or Winlator's LGPL Java X server) + tiny WM forcing fullscreen |
| GPU | Mesa Turnip (KGSL, glibc build) → Mesa X11 WSI; `TU_DEBUG=sysmem` on Adreno 740; set `GameRenderDevice=VulkanDrv` |
| SDL | **our own libSDL3.so.0** replacing the bundled one: stock X11 video + `pocketfoundry`-style audio/gamepad socket drivers |
| Controller | Android KeyEvent/MotionEvent → unix socket → SDL virtual joystick, **reshaped for SDLDrv**: radial deadzone, 6 axes max, triggers as buttons, 16-button layout, stick→mouse when relative mouse is off (menus) |
| Audio | OpenAL Soft rebuilt (glibc, SDL3 backend or PulseAudio) → socket → AudioTrack/AAudio |
| Data | copy from the user's existing UT install (SAF picker or adb push during dev) |
| Licence | GPL-3 if Termux:X11 is used (LGPL path: Winlator's Java X server) |

Effort: ~3–5 weeks to MVP, much of it already done upstream by pocket-foundry (same GPU, 60 FPS Factorio).
Cost: one frame copy/composite via X (zero-copy needs AHB patch), extra moving parts.

## Option B — in-process custom ELF loader (most native)
Load ut-bin/Core/Engine/… into the app process, map glibc imports to bionic (~30 real wrappers),
libSDL3 → real Android SDL3, Vulkan → Qualcomm system driver (zero copy), OpenAL → NDK build.
Blockers: Core.so's 64 KB static `thread_local` (needs custom TLS scheme), GNU libstdc++ locale internals,
no debugger visibility. Effort 6–9 weeks, high risk. Good phase-2 target once A runs.

## Controller / UI
- 469f has no XInput on Linux (WinDrv uses WinMM, not even DInput/XInput); SDLDrv uses raw SDL_Joystick, no deadzone.
- All fixes live in *our* SDL3 (both options): gamepad-backed joystick, deadzone, trigger→button, menu cursor emulation.
- Bindings: `[Engine.Input]` block in `controller-input.md`; force `UseJoystick=True` each launch.
- Menus: stick→mouse in SDL when relative mouse is off (A=click, B=Esc); launcher can boot straight into
  `Map?Game=...?Mutator=...` with MinPlayers pre-written. No usable gamepad-menu mod exists.

## Milestones (Option A)
1. Toolchain: arm64 Docker build of pocket-foundry runtime retargeted to our package (glibc, SDL3, Turnip, OpenAL).
2. APK: loader launch + data install + utshim; reaches SDL window on X (SoftDrv) inside the app.
3. VulkanDrv on Turnip at native 1080p.
4. Controller bridge + bindings + menu cursor.
5. Audio bridge.
6. Launcher UI (controller-navigable) + polish.
