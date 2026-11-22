# WoWForeverywhere: research notes for the UT99 Android port

Source: https://github.com/YoruDev-Ryland/WoWForeverywhere, shallow clone at commit `c65c2c0` (2026-11-20).
Read from a local clone (nothing run).
All paths below are relative to that clone.

## TL;DR

- WoWForeverywhere is a **re-skinned GameNative fork**. GameNative itself is built on Pluvia and Winlator. Package `app.wowforeverywhere`, namespace `app.gamenative`. It is a fork of jaredgei/wow-forever-android (`README.md` "Credits", `docs/superpowers/specs/2026-10-18-wow-foreverywhere-design.md`).
- The game is Blizzard's **Windows ARM64 client** (`Wow-arm64.exe` / `WowB-ARM64.exe`). It runs under **Proton 11 ARM64EC (a bionic build of Wine)**, with **DXVK 2.4.1**, then the **Bionic Vulkan wrapper ICD**, then **Turnip loaded through adrenotools**. Nothing is emulated as x86.
- **Process launch:** the app runs bionic ELF files from its data dir by calling `/system/bin/linker64 <elf> args` (`ProcessHelper.java`). Child `execve` calls are rewritten by a **closed-source LD_PRELOAD shim, `libredirect-bionic-wx.so`**. Daemons such as PulseAudio ship as `lib*.so` files in `jniLibs` and run from `nativeLibraryDir`. proot is in the tree but disabled.
- **Display:** a **Java X11 server** (Winlator `com.winlator.xserver`) listens on a unix socket. It supports DRI3, Present, MIT-SHM and XInput2. DRI3 pixmaps arrive as **AHardwareBuffer** fds. The app composites them onto the Android Surface through a Vulkan renderer (`libvulkan_renderer.so`) or ASurfaceControl.
- **Controllers:** Java maps Android gamepads into a 64-byte **shared-memory/futex block**. An LD_PRELOAD shim (`evshim.c`) inside the Wine process turns that block into an **SDL2 virtual joystick**.
- **Audio:** a **bionic PulseAudio daemon** with `module-aaudio-sink` reaches AAudio. Wine's winepulse connects to its unix socket. ALSA-over-socket is the alternative.
- **For UT99 (glibc + SDL3 + Vulkan):** WoWFE runs nothing glibc-native. Ideas that carry over: the exec-via-loader trick, the Java X server with AHB presentation, the PulseAudio socket, the shm virtual-joystick shim and the adrenotools/Turnip handling. None of the binaries can be dropped in as-is, because they are bionic and the game is glibc.

---

## 1. Runtime stack and process launching

### What runs
- `app/src/main/java/app/gamenative/ui/screen/wow/WoWForeverScreen.kt` (around lines 890-1110) builds a Winlator "container" JSON:
  - `containerVariant = "bionic"`
  - `wineVersion = proton-11.0-90624-arm64ec-1`
  - `dxwrapper = dxvk-2.4.1-wow-aarch64-test`
  - `graphicsDriver = "Wrapper"` with `graphicsDriverVersion = Turnip-WoW-scheduler-test`, or `Turnip-V32-RP6sched` for Adreno 8xx
  - `displayRenderer = "vulkan"`
  - `fexcoreVersion 2609-0`
  - `execArgs -d3d11`
  - env vars such as `MESA_VK_WSI_PRESENT_MODE=mailbox`, `PULSE_LATENCY_MSEC=144`, `WINEESYNC=0` and `ZINK_*`
- `XServerScreen.kt` (around line 3645) picks `BionicProgramLauncherComponent` for bionic containers. `GlibcProgramLauncherComponent` (box64 glibc path) still exists but WoW does not use it.
- The command is `wine explorer /desktop=shell,<WxH> <game cmd>` (`XServerScreen.kt` around line 3672). For ARM64EC, `BionicProgramLauncherComponent.getFinalCommand()` runs `wine` directly and sets `HODLL=libwow64fex.dll` or `wowbox64.dll` for any x64 code. Otherwise it runs `box64 wine`.
- Rootfs ("imagefs"): `imagefs_bionic.txz` comes from `https://downloads.gamenative.app/` (`PluviaMain.kt:479`, `utils/Net.kt:78`, `ImageFsInstaller.java:128`). It is unpacked into `filesDir/imagefs` and contains bionic-linked Wine userland libs, SDL2, Vulkan loader, ICD JSONs, fonts and ALSA config.

### How exec works despite targetSdk 36 W^X/SELinux
- `app/build.gradle.kts`: flavor `modern`, `minSdk 29`, **`targetSdk 36`**, arm64-v8a only. It sets `MODERN_ANDROID=true` and `PRELOAD_BIONIC_SO="libredirect-bionic-wx.so"`. It also sets `jniLibs.useLegacyPackaging = true`, and the manifest has `android:extractNativeLibs="true"`.
- **Trick 1, run through the system linker.** `com/winlator/core/ProcessHelper.java` lines 207, 311 and 344 contain:

  ```java
  if (BuildConfig.MODERN_ANDROID) command = "/system/bin/linker64 " + command;
  ```

  Every guest program (`wine`, `wineserver`, `box64`, PulseAudio) runs as `linker64 /data/data/<pkg>/files/imagefs/.../wine ...`. The app never calls `execve` on an `app_data_file`. The system linker (an exec-allowed system file) mmaps the ELF instead. The app still loads `.so` files from the data dir through `LD_LIBRARY_PATH`, which confirms that mmap-exec of `app_data_file` is still allowed at targetSdk 36. This is the Termux-style trick, and **it only works for bionic ELF files**.
- **Trick 2, LD_PRELOAD exec redirection.** `BionicProgramLauncherComponent.java` sets these:
  - `LD_PRELOAD = libandroid-sysvshm.so : libevshim.so : libgamenative_dns_v4mapped.so : libredirect-bionic-wx.so`
  - `REDIRECT_EXEC__PROC_SELF_EXE=<wine>`, which fixes `/proc/self/exe`, since it would otherwise point at linker64

  `libredirect-bionic-wx.so` and `-minimal.so` are under `app/src/modern/assets/` and copied into imagefs by `ImageFsInstaller.ensureBionicLib()`. They are **proprietary and closed-source** (`THIRD_PARTY_NOTICES` "libredirect.so"). The notices say they adapt process launches for Android 15+ and handle W^X; without them "most Windows games ... will fail to start" on Android 15+. Comments in `BionicProgramLauncherComponent.resolveLibredirectPreload()` mention a "strict W^X kernel" and an Android 17 deadlock in the full shim.
- **Trick 3, executables in jniLibs.** `libpulseaudio.so` in `app/src/main/jniLibs/arm64-v8a/` is actually the PulseAudio daemon executable. It runs from `nativeLibraryDir`, which allows exec (`PulseAudioComponent.java` around line 228). Legacy packaging guarantees these files are extracted to disk.
- **proot** source is in `app/src/main/cpp/proot/`, but `add_subdirectory(proot)` is commented out in `app/src/main/cpp/CMakeLists.txt`. `GuestProgramLauncherComponent.exec()` (around line 260) would run `libproot.so` with `--rootfs` and binds, but `libproot.so` is not shipped. This is a dead legacy path.
- There is no custom ELF loader and no chroot. One `XEnvironment` holds the components: SysVSHM server, X server, Pulse/ALSA server, the optional Vortek/VirGL renderer server, and the guest launcher.

## 2. Graphics path to the Android Surface

```
WoW (D3D11, ARM64 PE) -> DXVK 2.4.1 (aarch64/arm64ec) -> winevulkan -> Vulkan loader (bionic, imagefs)
  -> VK_ICD_FILENAMES=.../vulkan/icd.d/wrapper_icd.aarch64.json  (Bionic Vulkan wrapper, leegao)
     -> adrenotools: ADRENOTOOLS_DRIVER_PATH / _HOOKS_PATH / _DRIVER_NAME -> Turnip .so (custom WoW scheduler builds)
  -> WSI: Mesa-style X11 WSI over DISPLAY=:0  -> Java X server (unix socket imagefs/tmp/.X11-unix/X0)
     DRI3 PixmapFromBuffers with modifier 1255 = AHardwareBuffer fd  (1274 = shm fallback)
     Present extension for flips
  -> Java side: AHBImage/GPUImage texture -> VulkanRenderer (libvulkan_renderer.so, VulkanRendererContext.cpp,
     ANativeWindow + AHardwareBuffer import) or ASurfaceRenderer (ASurfaceControl direct scanout) -> SurfaceView
```

Key files:
- `XServerScreen.kt` lines 4700-4870 contain the wrapper env: `WRAPPER_VK_VERSION`, `WRAPPER_EMULATE_BCN`, `WRAPPER_RESOURCE_TYPE`, `WRAPPER_MAX_IMAGE_COUNT`, `MESA_VK_WSI_PRESENT_MODE`, `TU_DEBUG`, and `MESA_VK_WSI_DEBUG=sw` when DRI3 is off. It also sets `VK_ICD_FILENAMES` to `wrapper_icd` or `freedreno_icd`.
- `com/winlator/contents/AdrenotoolsManager.java` (around line 190) sets `ADRENOTOOLS_*`.
- `app/gamenative/wow/GraphicsDrivers.kt` handles driver zips (adrenotools `meta.json` format) in `filesDir/contents/adrenotools/<name>`. Bundled drivers come from `tools/fetch_components.sh`.
- `com/winlator/xserver/extensions/DRI3Extension.java` lines 130-180 handle AHB and shm pixmaps. `PresentExtension.java`, `MITSHMExtension.java` and `XInput2Extension.java` are alongside.
- `com/winlator/renderer/{VulkanRenderer,ASurfaceRenderer,GLRenderer,AHBImage,GPUImage}.java`.
- `app/src/main/cpp/winlator/VulkanRendererContext.cpp` is the compositor. It also hosts the LSFG frame-gen pipeline (`cpp/lsfg/*`).
- `app/src/main/cpp/asurfacerenderer/*` uses ASurfaceControl through dlsym.
- Other renderer options exist but WoW does not use them: Vortek (`libvortekrenderer.so`, a closed Winlator component that serializes Vulkan to a host-side server), VirGL (`cpp/virglrenderer`) and Zink for GL.
- WoW does not use Turnip's own KGSL path with a direct Android surface. Presentation always goes through the X server.

## 3. Gamepad and controller input

- **Android side:** `com/winlator/winhandler/WinHandler.java` and `com/winlator/inputcontrols/{ControllerManager,ExternalController,GamepadState}.java`, plus `ui/screen/xserver/PhysicalControllerHandler.kt`, read `KeyEvent` and `MotionEvent` from `InputDevice` sources. They pack them into a 64-byte struct mmapped from `filesDir/gamepad_shm/gamepad.mem`, plus `gamepad1..3.mem` for up to 4 players (`WinHandler.java` around lines 731-752).
- They call JNI `notifyStateChanged()`, which bumps a futex sequence word (`cpp/evshim/evshim.c`).
- **Guest side:** `libevshim.so` is LD_PRELOADed into Wine. Its constructor `dlopen`s **`libSDL2-2.0.so.0`** from imagefs and calls `SDL_JoystickAttachVirtualEx` (GAMECONTROLLER type). A thread waits on the futex and pushes axes, buttons and hat through `SDL_JoystickSetVirtual*`.
- Rumble flows back the same way: `OnRumble` writes into the shm, and Java's `waitForRumble` drives the Android `Vibrator`.
- The shm layout is documented at the top of `evshim.c`: `seq`, `lx/ly/rx/ry/lt/rt` as int16, `btn[15]`, `hat`, rumble, `rumble_seq`, `connected`.
- Wine's `winebus.sys` SDL backend exposes the virtual pad to the game as XInput. Env hints are set in `XServerScreen.kt` around line 3585 (`SDL_XINPUT_ENABLED`, `SDL_JOYSTICK_HIDAPI`, and so on). WoW's native gamepad mode is turned on through Config.wtf (`GamePadEnable "1"`, `WoWForeverScreen.kt` around line 1182).
- There is no evdev or uinput emulation; evshim is named after the idea but uses SDL.
- **Keyboard, mouse and touch** go through the Java X server as X events: `XServer`, `Keyboard`, `Pointer`, XInput2 raw motion, plus the on-screen `InputControlsView` and `TouchpadView`.

## 4. Audio path

- **Default** (`Container.DEFAULT_AUDIO_DRIVER = "pulseaudio"`): `PulseAudioComponent.java` writes `default.pa` with these lines:
  - `module-native-protocol-unix socket=<imagefs>/tmp/.sound/PS0 auth-anonymous=1`
  - `module-aaudio-sink`
  - `module-pipe-source` for the mic, if enabled

  It runs `nativeLibraryDir/libpulseaudio.so -n --file=default.pa --daemonize ...`. The PulseAudio libs (`libpulse*.so`, `libsndfile.so`, `libltdl.so`) are in `jniLibs/arm64-v8a`. The AAudio sink module comes from `assets/pulseaudio-gamenative-20260919.tzst`.
- The guest gets `PULSE_SERVER=<imagefs>/tmp/.sound/PS0`, and Wine's winepulse.drv connects to it.
- **Alternative ALSA mode:** `ALSAServerComponent` with `cpp/winlator/alsa_client.c` (AAudio) serves a custom ALSA plugin over the socket `/tmp/.sound/AS0` (`ANDROID_ALSA_SERVER`, `android_aserver.conf`).
- The PulseAudio native protocol does not depend on the client's ABI, so a **glibc libpulse client can talk to this bionic daemon**. That is directly relevant to OpenAL Soft's PulseAudio backend.

## 5. Provisioning, build system, licenses and prebuilt blobs

### Game files
- `app/gamenative/wow/install/*`, `wow/tact/*` and `wow/casc/*` contain a Kotlin TACT/CASC client. It downloads the ARM64 client and data straight from Blizzard's CDN to internal storage or SD (about 0.5 GB of binaries, about 67 GB of data). It verifies files and can resume. It can also scan for an existing install.
- Python reference: `tools/download_wow_arm64.py`.
- It needs `MANAGE_EXTERNAL_STORAGE` and `requestLegacyExternalStorage`.
- Wine drives are mapped as `G:` to the game root, plus `D:` to Download.

### Build
- Gradle with Kotlin and Compose, single module `:app`, flavor `modern`, NDK 27.3.
- **No `externalNativeBuild` is wired up.** The C/C++ under `app/src/main/cpp` (CMake referencing `extras/adrenotools` and `lsfg-vk-android`, which are missing from the repo) is reference only. Every native `.so` is a prebuilt checked into `app/src/main/jniLibs` and `app/src/modern/jniLibs`.
- Build steps: `tools/fetch_components.sh`, then `./gradlew assembleModernRelease` (`.github/workflows/release.yml`).

### Prebuilt blobs and where they come from
- **`tools/fetch_components.sh`** downloads these into `assets/bundled_components/` and checks them against `tools/components.sha256`:
  - From `github.com/jaredgei/wow-forever-android/releases/download/components-v1`:
    - `proton-11.0-90624-arm64ec.wcp`
    - `dxvk-2.4.1-wow-aarch64-test.wcp`
    - `turnip-wow-scheduler-test.zip`
  - From `raw.githubusercontent.com/arusiasotto/wow-forever-a840`:
    - `turnip-V32-RP6sched.zip`
- **Checked-in assets:**
  - `box86_64/box64-0.4.2-bionic.tzst`
  - `wowbox64/wowbox64-0.4.2.tzst`
  - `pulseaudio-gamenative-20260919.tzst`
  - `redirect.tzst`
  - `lsfg_vk` layer JSON
  - `modern/assets/libredirect-bionic-wx*.so`
- **Checked-in jniLibs:**
  - Main: `libevshim`, `libahbimage`, `libasurface_renderer`, `libkgslshim` (closed), `liblsfg-vk-layer`, `libgnoverlay`, `libgamenative_dns_v4mapped`, the PulseAudio set, `libc++_shared`
  - Modern: `libwinlator`, `libwinlator_11`, `libvulkan_renderer`, `libvortekrenderer` (Winlator, closed), `libvirglrenderer`, `libxconnectorpatch`, `libpatchelf`, `libextras`, and `libmain_hook`/`libhook_impl` (adrenotools hooks)
- **Downloaded at runtime from `downloads.gamenative.app`:**
  - imagefs
  - `extras.tzst`
  - container patterns
  - wrapper and turnip and zink tzsts (`assets/graphics_driver_download.json`, `container_files_download.json`)
  - core drivers
  - Winlator content profiles from `raw.githubusercontent.com/longjunyu2/winlator`

### Licenses
- The app is GPL-3.0 (`LICENSE`).
- Closed, proprietary binaries are described in `THIRD_PARTY_NOTICES`: `libredirect`, `libkgslshim`, launcher helpers, and the Steam bootstrap (not present here). Vortek is also closed.
- Other licenses:
  - lsfg-vk: MIT, relicensed GPL-3.0 after 2.0
  - dxbc: zlib
  - OpenAL Soft: LGPL-2.1
  - Scuffle auth helper: MIT (`assets/LICENSE.scuffle`)
  - Kenney prompts: CC0
- The notices file admits it "is not exhaustive". Wine, Proton, DXVK, Mesa, box64 and FEX are LGPL, zlib, MIT or GPL inside the archives.
- **Copying code from this repo pulls in GPL-3.0. libredirect and kgslshim cannot be redistributed or modified.**

## 6. Reuse assessment for a native glibc aarch64 SDL3 + Vulkan game (UT99 469f)

### Hard mismatch: libc
Every guest binary in WoWFE is **bionic**: Wine, SDL2, the Vulkan loader, the wrapper and PulseAudio. The `/system/bin/linker64 <elf>` trick cannot load a glibc ELF, because UT's `PT_INTERP` is `/lib/ld-linux-aarch64.so.1`. UT needs one of these:
- **(a) A glibc dynamic loader started as a program.** Ship `ld-linux-aarch64.so.1` as `jniLibs/arm64-v8a/libldlinux.so`, so it is extracted to `nativeLibraryDir`, which allows exec, the same way WoWFE ships `libpulseaudio.so`. Run `libldlinux.so --library-path <glibc rootfs libs>:<UT System> <UT>/System/ut-bin ...`. ld.so then mmaps the executable and libs from app data. WoWFE's use of `LD_LIBRARY_PATH` into imagefs shows that mmap-exec from app data still works at targetSdk 36. UT spawns no children, so no execve shim (libredirect equivalent) should be needed. It still needs a minimal glibc runtime (libc, libm, libpthread, libdl, libstdc++, libgcc_s, plus whatever SDL3 and OpenAL `dlopen`).
- **(b) proot** (source in `app/src/main/cpp/proot`, GPL-2) built as `libproot.so` plus `libproot-loader.so`. This is the older Winlator glibc approach (`GuestProgramLauncherComponent.exec`). It is slower because of ptrace, but it handles `/lib` paths and exec transparently.
- **(c) Recompiling the game for bionic or Android**, if source is available. That is outside WoWFE's scope.

### Reusable as design or code (all GPL-3.0 unless noted)

| Piece | Files | Use for UT |
|---|---|---|
| Exec model (lib*.so in jniLibs, `useLegacyPackaging`, `extractNativeLibs`) | `app/build.gradle.kts`, `ProcessHelper.java`, `PulseAudioComponent.java` | Apply to a glibc `ld.so` (option a) |
| Java X11 server with DRI3 AHB/MIT-SHM/Present/XInput2 and SurfaceView compositor | `com/winlator/xserver/**`, `xconnector/**`, `renderer/**`, `cpp/winlator/*`, `asurfacerenderer/*` | Lets UT's **unmodified Linux libSDL3 (x11 backend)** create a window, plus keyboard and mouse. Vulkan presentation needs a glibc Vulkan ICD whose X11 WSI produces DRI3 buffers the server understands. AHB modifier 1255 is a wrapper convention; a glibc Mesa Turnip/KGSL would likely fall back to the shm path (1274) or `MESA_VK_WSI_DEBUG=sw` CPU copies. That works but is slower |
| PulseAudio daemon (bionic) with module-aaudio-sink on a unix socket | `PulseAudioComponent.java`, jniLibs `libpulse*` | **Direct reuse.** Set `PULSE_SERVER=unix:<path>` for UT. libopenal (glibc) uses its PulseAudio backend with a glibc `libpulse.so.0` in the rootfs. ALSA-socket is an alternative |
| Gamepad shm + futex + virtual SDL joystick | `cpp/evshim/evshim.c`, `WinHandler.java`, `inputcontrols/*` | Good pattern. Port evshim to **glibc** and the **SDL3** API (`SDL_AttachVirtualJoystick`, `SDL_SetJoystickVirtualAxis/Button/Hat`, `SDL_OpenJoystick`), LD_PRELOAD it into UT, and `dlopen("libSDL3.so.0")`. Java side is reusable as-is |
| Turnip / adrenotools driver management | `GraphicsDrivers.kt`, `AdrenotoolsManager.java`, `fetch_components.sh` | Concept only. adrenotools is for bionic processes. A glibc process needs a **glibc-built Mesa Turnip with the KGSL backend** (`VK_ICD_FILENAMES` to `freedreno_icd`) or a glibc bridge to the bionic system Vulkan |
| Bionic Vulkan wrapper (leegao) | downloaded `wrapper*.tzst` | Bionic ICD; not loadable into a glibc process |
| On-screen controls, touchpad, radial menu, perf HUD | `widget/*`, `ui/screen/xserver/*` | UI reusable if the X-server architecture is adopted |
| Frame-gen (lsfg) | `cpp/lsfg`, `liblsfg-vk-layer.so` | Optional and unneeded |

### Only relevant to Windows/Wine games
- Proton/Wine ARM64EC, DXVK, VKD3D, wowbox64/FEX/box64
- Wine registry, prefix and container management (`com/winlator/container`, `WineUtils`, `WineRegistryEditor`)
- `winhandler` process control
- `libredirect` (Wine execve and `/proc/self/exe`) and SysVSHM shim (Wine esync and X MIT-SHM)
- Vortek, VirGL and Zink
- Battle.net, CASC and addon code
- DNS v4mapped shim

### Recommended takeaways for UT99
1. Prefer the **glibc `ld.so` in jniLibs** launch model over proot. It is the closest analogue to WoWFE's `linker64` trick. Verify it on-device on the RP6 with Android 13 and targetSdk 34+.
2. Choose a display strategy. Either:
   - reuse **Winlator's Java X server** and keep the stock Linux SDL3, which is fast to stand up but adds a WSI and AHB integration problem for Vulkan; or
   - better long-term, write a **custom glibc SDL3 video backend or shim** that renders into an `ANativeWindow` handed over a socket. This needs a glibc-to-bionic Vulkan bridge, or Turnip/KGSL rendering into AHB-backed images exported to the Java side, the way WoWFE's DRI3 AHB path does.
3. **Audio:** copy the PulseAudio-daemon approach unchanged.
4. **Gamepad:** port evshim to SDL3 and glibc.
