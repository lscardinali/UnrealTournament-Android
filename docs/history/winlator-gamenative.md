# Running native glibc aarch64 `ut-bin-arm64` on Android: what we can reuse from Winlator, its forks and GameNative

Target: UT99 OldUnreal 469f, Linux ARM64 glibc build, on Retroid Pocket 6 (SD 8 Gen 2 / Adreno 740, Android 13).
The game's platform layer is all SDL3 (`libSDL3.so.0`) plus OpenAL Soft (`libopenal.so.1`). No Wine and no Box64.

## Sources examined

These are shallow clones, read only. Their scripts were not run.

| Repo | Commit | Notes |
|---|---|---|
| github.com/brunodev85/winlator | 30aafbd (2026-11-02) | Top-level repo. It holds `android_alsa/`, `glibc_patches/` and `installable_components/`; the app is a submodule. |
| github.com/brunodev85/winlator-app | f8a211c (2026-11-20) | The real Winlator 11.2 app (Java X server, audio, launch code, bundled assets). |
| github.com/brunodev85/vortek, gladio | a7746fb, 3861b0a | Vulkan and GL forwarding clients (LGPL-2.1). |
| github.com/utkarshdalal/GameNative | 458fe62 (2026-11-21) | Kotlin app with an embedded fork of the Winlator Java code (`app/src/main/java/com/winlator`). |
| github.com/StevenMXZ/Winlator-Ludashi | de12f25 (2026-11-20) | A "Winlator Bionic" / cmod fork. Wine is built for bionic. It has a new C++ X server (`cpp/ludashix11`). |
| github.com/termux-pacman/glibc-packages (sparse checkout of `gpkg/glibc`) | 3d99b66 | Upstream of the Android glibc patches that Winlator uses. |

Paths below are relative to each repo root.

I unpacked and inspected Winlator's bundled assets with readelf/strings: `app/src/main/assets/rootfs.tzst`, `graphics_driver/turnip-26.2.0.tzst`, `box64/*.tzst`, `pulseaudio.tzst` and `rootfs_patches.tzst`.

---

## 1. The glibc runtime

### Where the rootfs comes from and which glibc it uses

- Winlator ships `winlator-app/app/src/main/assets/rootfs.tzst` (65 MB, zstd tar). The app extracts it to `/data/data/com.winlator/files/rootfs` (`java/com/winlator/xenvironment/RootFS.java`, `RootFSInstaller.java`; the legacy name was `imagefs`).
- **glibc 2.39** (`libc.so.6` contains "GNU C Library (GNU libc) stable release version 2.39"; the highest symbol version is GLIBC_2.39). Our binaries need at most GLIBC_2.34, so this is sufficient.
- It is a **Termux-pacman-style glibc**. It was built with `--prefix=/data/data/com.winlator/files/rootfs`, `slibdir=rtlddir=<prefix>/lib`, and the Android patch set from termux-pacman `gpkg/glibc` (see `winlator/glibc_patches/...android_sysvshm.c`; the credit is in the README). Every library has `RUNPATH=/data/data/com.winlator/files/rootfs/lib` and every binary has `PT_INTERP=/data/data/com.winlator/files/rootfs/lib/ld-linux-aarch64.so.1`.
  - `ld.so` hardcodes `<prefix>/etc/ld.so.cache` and `<prefix>/lib/`.
  - libxcb hardcodes `<prefix>/tmp/.X11-unix/X`.
  - libX11 hardcodes `<prefix>/usr/share/X11/...`.
  - The Vulkan loader hardcodes `<prefix>/etc/xdg` and `<prefix>/usr/share`.
  - The Turnip ICD JSON uses `library_path=/data/data/com.winlator/files/rootfs/lib/libvulkan_freedreno.so`.
  - **The whole rootfs is therefore tied to the package name `com.winlator`.** To reuse these binaries as they are, we would need to rebuild with our own prefix, or binary-patch the prefix to a string of the same length.
- Contents relevant to us, all aarch64 glibc builds in `/usr/lib`:
  - Runtime: `ld-linux-aarch64.so.1`, `libc/libm/libdl/libpthread/librt/libresolv`, `libstdc++.so.6.0.32`, `libgcc_s.so.1`.
  - X11: `libX11`, `libX11-xcb`, `libXext`, `libXi`, `libXrandr`, `libXcursor`, `libXfixes`, `libXrender`, `libXcomposite`, `libXxf86vm`, `libxcb` plus `xcb-dri3/present/randr/shm/sync/render`.
  - Graphics: `libdrm`, `libvulkan.so.1.3.301`.
  - Audio: `libasound.so.2`, `libpulse.so.0` (17.0).
  - Other: `libz`, `libzstd`, `libfreetype`, `libfontconfig`.
  - **Missing:** `libxkbcommon`, `libudev`, `libdbus`, `libatomic.so.1`. Our `libopenal.so.1` NEEDs `libatomic.so.1`, so we must ship it ourselves (it comes from GCC).
- `rootfs_patches.tzst` adds `usr/lib/alsa-lib/libasound_module_pcm_android_aserver.so`, which is the ALSA plugin that talks to the Java ALSA server.
- The termux-pacman recipe is now at glibc **2.44** (`gpkg/glibc/build.sh`: `TERMUX_PKG_VERSION=2.44`).

### Why a patched glibc is mandatory

Android's app seccomp filter kills the process with SIGSYS on syscalls outside its allowlist. termux-pacman's `gpkg/glibc/fakesyscall.json` lists the syscalls they had to stub.

- **Made to return ENOSYS:** `clone3`, `rseq`, `set_robust_list`, `get_robust_list`, `futex_waitv`, `io_uring_*`, `pidfd_*`, `sem*`, `msg*`, `mq_open`, `mbind`, `set_mempolicy`, `kcmp`, `name_to_handle_at`, `open_by_handle_at`, `landlock_create_ruleset`, and a few others.
- **Rerouted to allowed equivalents:** `faccessat2` to `faccessat`, `fchmodat2`, `statx` to a generic implementation, `epoll_pwait2`, `close_range`, and so on.
- **Made to return 0:** `set*uid` / `set*gid`.
- **SysV shared memory:** `shmget`, `shmat`, `shmdt` and `shmctl` are reimplemented over a Unix-socket server (`shmem-android.c`; Winlator's version is `android_sysvshm.c`, served by Java `SysVSharedMemoryComponent`).
- **Other patches:** `disable-clone3.patch`, `mprotect.c` (works around EACCES on `mprotect(PROT_EXEC)` of app-data mappings), `set-dirs.patch` (prefix paths) and Android passwd/group support.

Stock Debian or Ubuntu glibc calls `set_robust_list` and `rseq` on every thread start, so it **will die immediately in an app process**.
The Ludashi notices file confirms the same symptom for a generic-Linux 7-Zip build: "inherits the app's seccomp-bpf filter and hits a syscall the filter TRAPs, dying with SIGSYS" (`Winlator-Ludashi/THIRD_PARTY_NOTICES.txt`).

### How they exec glibc binaries (W^X)

- **Winlator:** `targetSdkVersion 28` (`winlator-app/app/build.gradle`, `minSdk 26`; manifest has `extractNativeLibs=true`). With targetSdk ≤ 28, SELinux still allows `execve` of files in the app's data dir.
  - `GuestProgramLauncherComponent.execGuestProgram()` simply runs `ProcessBuilder("<rootfs>/usr/local/bin/box64 ...")` (`core/ProcessHelper.exec`).
  - The kernel loads the ELF's PT_INTERP (the rootfs ld.so).
  - No proot and no linker trick are involved.
- **Ludashi (cmod):** also targetSdk 28 (`app/build.gradle` line 164: "We're preserving Winlator's request to keep targetSdkVersion at 28"). It also vendors `cpp/proot` and `cpp/patchelf`.
- **GameNative:** has flavors (`app/build.gradle.kts` 134-170).
  - `legacy` uses **targetSdk 28** and allows glibc containers.
  - `modern` uses **targetSdk 36**. It forces bionic-only Wine (`CommunityConfigService.kt:944` `allowGlibc = !BuildConfig.MODERN_ANDROID`).
  - `modern` runs every binary as `/system/bin/linker64 <binary>` (`com/winlator/core/ProcessHelper.java:209,309,342`). The executable file is the system linker, which then mmaps the target from app data.
  - It also relies on a **closed-source** `libredirect-bionic-wx.so` preload to make child `exec`s work on Android 15+ (`THIRD_PARTY_NOTICES`, the "libredirect.so (Proprietary)" section).
  - So nobody in this ecosystem runs **glibc** under targetSdk ≥ 29.
- **Options for us with targetSdk ≥ 29 (not tested by any of these projects):**
  - Ship the patched `ld-linux-aarch64.so.1` inside the APK as `jniLibs/arm64-v8a/libldlinux.so`, together with `useLegacyPackaging=true` / `extractNativeLibs=true`.
  - Exec it from `nativeLibraryDir`, which is `apk_data_file` and therefore exec-allowed (that is how Winlator runs `libpulseaudio.so` and Ludashi runs `lib7zz.so`).
  - Call it as `libldlinux.so --library-path <dirs> <path>/ut-bin-arm64 args...`. ld.so then only mmaps `ut-bin` and the `.so` files from app data.
  - mmap(PROT_EXEC) of app data is believed to still be allowed for targetSdk 29+; only `execve` is blocked. This must be verified on the device.
  - ut-bin is a single process and does not fork/exec helpers, which makes this much easier than Wine.
  - Caveat: `/proc/self/exe` will point at the loader. Check whether UT 469 derives its base dir from it; it has `$ORIGIN` RUNPATH, which ld.so resolves correctly.
  - **Recommendation:** start with targetSdk 28, which works today and sideloads fine on Android 13. Try the nativeLibraryDir loader trick later if Play Store distribution matters.

### Environment variables they set

From `GuestProgramLauncherComponent.java` and `XServerDisplayActivity.setupXEnvironment()`, Winlator upstream:

```
HOME=<rootfs>/home/xuser  USER=xuser  TMPDIR=<rootfs>/tmp  DISPLAY=:0
PATH=<rootfs>/opt/wine/bin:<rootfs>/usr/local/bin:<rootfs>/usr/bin
LD_LIBRARY_PATH=<rootfs>/usr/lib
ANDROID_SYSVSHM_SERVER=<rootfs>/tmp/.sysvshm/SM0         # read by patched glibc shm*
ANDROID_ALSA_SERVER=<rootfs>/tmp/.sound/AS0  ANDROID_ASERVER_USE_SHM=true   (ALSA mode)
PULSE_SERVER=<rootfs>/tmp/.sound/PS0  PULSE_LATENCY_MSEC=..                 (Pulse mode)
MESA_DEBUG=silent MESA_NO_ERROR=1
TU_DEBUG=noconform[,sysmem|gmem],deck_emu  MESA_VK_WSI_PRESENT_MODE=..  MESA_VK_WSI_NATIVE_MEM_IMPORTED=1  TU_OVERRIDE_HEAP_SIZE=..
```

- GameNative adds `FONTCONFIG_PATH` and an `LD_PRELOAD` of `libandroid-sysvshm.so` / `libredirect.so` (`GlibcProgramLauncherComponent.java` 170-215).
- Ludashi adds `ALSA_CONFIG_PATH`, `ALSA_PLUGIN_DIR`, `SSL_CERT_*`, `ANDROID_RESOLV_DNS` (DNS taken from `ConnectivityManager`), `FAKE_EVDEV_DIR` and `FAKE_EVDEV_VIBRATION` (`GuestProgramLauncherComponent.java` 380-470).
- **DNS:** Android has no `/etc/resolv.conf`. The Termux glibc reads `<prefix>/etc/resolv.conf`, so the app must write one; the Ludashi fork passes the DNS server through an env var instead. UT's master-server lookup needs this.

---

## 2. Graphics

### In-app X server

- **Winlator:** a pure-Java X11 server (`winlator-app/app/src/main/java/com/winlator/xserver/*`, about 4.3k lines plus `xconnector` epoll JNI in `cpp/winlator/src/xconnector_epoll.c`).
  - It listens on the Unix socket `<rootfs>/tmp/.X11-unix/X0` (`xconnector/UnixSocketConfig.java`).
  - Rendering is GLES on a GLSurfaceView (`renderer/GLRenderer.java`).
  - Extensions (`XServer.java` ~201): BIG-REQUESTS, MIT-SHM, **DRI3 1.0**, **Present**, SYNC, GLX, Generic Event, **XInputExtension 2.1** (including XI_RawMotion and raw buttons, which SDL relative mouse mode uses).
  - **No RANDR, XKB or XFIXES.**
- **GameNative:** a copy of the same Java server (`app/src/main/java/com/winlator/xserver`), with roughly the same extension set.
- **Ludashi:**
  - The Java server adds **RANDR** and Composite.
  - Since 2026-11-20 it also has a native C++ X server, `app/src/main/cpp/ludashix11` (with `NativeX11Component.java` and `widget/NativeX11View`), using xorgproto headers.
  - It is very new and GPL-3.0.

### How Vulkan apps present (the "direct rendering" DRI3/AHardwareBuffer path)

1. The guest runs a glibc Turnip (Mesa `libvulkan_freedreno.so`) that talks **directly to `/dev/kgsl-3d0`** (strings: `tu_knl_kgsl.cc`). No adrenotools are involved and it is not the Qualcomm blob.
   - Its NEEDED list is `libxcb-dri3`, `libxcb-present`, `libxcb-randr`, `libxcb-shm`, `libX11-xcb`, `libdrm`, `libz`, `libstdc++`.
   - Surfaces: `VK_KHR_xcb_surface`, `VK_KHR_xlib_surface`, `VK_KHR_wayland_surface`, `VK_EXT_headless_surface`.
2. Mesa's X11 WSI uses DRI3 plus Present. In Winlator's patched Mesa, `MESA_VK_WSI_NATIVE_MEM_IMPORTED=1` (`contentdialog/TurnipConfigDialog.java:62`, labelled "directRendering") makes the WSI call **`DRI3 BufferFromPixmap`**.
3. The Java server answers by allocating an **AHardwareBuffer** (`renderer/GPUImage.java`, `cpp/winlator/src/gpu_image.c`: `AHardwareBuffer_allocate`, `AHardwareBuffer_getNativeHandle()->data[0]` as the fd) and returns its dmabuf fd (`xserver/extensions/DRI3Extension.java` `bufferFromPixmap`).
4. Turnip imports that fd as VkDeviceMemory and renders into it. The X server samples it zero-copy through `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)` and composites with GLES.
   - That is one extra GPU blit, but no CPU copies.
5. **The source of the `MESA_VK_WSI_NATIVE_MEM_IMPORTED` Mesa patch is not in any of these repos.** A grep finds only the env var in Java. Mesa is MIT, so they are not obliged to publish it.
   - If we cannot get the patch, either use the binary, or write our own WSI patch or layer.
   - Without the patch, Mesa falls back to `PixmapFromBuffer`: the server just mmaps the fd and uploads it on the CPU (`pixmapFromFd`, which is slow).
6. **Turnip settings:**
   - Winlator forces `TU_DEBUG=sysmem` on Adreno 7xx except 710/720/732 (`TurnipConfigDialog.isForceSYSMEM`). **Adreno 740 gets sysmem.**
   - It also always adds `deck_emu` and `noconform`.
   - Ludashi's default env also has `TU_DEBUG=noconform,sysmem` (`container/Container.java:30`).
7. **Vortek** (Winlator, LGPL) is the alternative: a guest Vulkan ICD that serializes calls to a server in the app process, which calls Android's `libvulkan` or an adrenotools driver.
   - It is useful when Turnip lacks features. For an Adreno 740 with Turnip it is an extra hop we don't need.
8. The bionic forks (Ludashi, GameNative) load **bionic** Turnip builds through adrenotools plus a "wrapper" ICD (for example `Winlator-Ludashi/app/src/main/assets/graphics_driver/adrenotools-turnip27.0.0.tzst` holds `vulkan.ad07xx.so`; there is also `AdrenotoolsManager.java` with `ADRENOTOOLS_DRIVER_PATH`).
   - **Those builds, and K11MCH1/whitebelyash AdrenoToolsDrivers, cannot be loaded into a glibc process.**
   - Only glibc Turnip builds work for us: Winlator's `turnip-24.1/25.0/26.0.3/26.2.0.tzst` and GameNative's `downloads.gamenative.app/graphics_driver/turnip-25.x.tzst`. Verify with readelf that NEEDED lists `libc.so.6`.

### Can a native SDL3 X11 Vulkan app use it?

Yes, in principle. SDL3 dlopens `libX11.so.6`, `libXext.so.6`, `libX11-xcb`, `libXi`, `libXrandr`, `libXcursor`, `libXfixes` and `libvulkan.so.1` (strings in our `libSDL3.so.0`), and `VulkanDrv` calls `SDL_Vulkan_CreateSurface`, which gives `VK_KHR_xlib_surface`. That is exactly the path DXVK and Wine's winevulkan use in Winlator.

Risks:
- **Missing extensions:**
  - Without RANDR (upstream Winlator), SDL3 falls back to StdXlib modes: a single fixed mode equal to the X screen size. The X screen size must therefore match what we want, for example 1920x1080 or the device panel.
  - Without XKB, keysyms come from core `GetKeyboardMapping`.
  - SDL3 also probes XInput2 (present), XFixes (absent; only needed for pointer confinement) and XIM (absent, fails gracefully).
- **Unimplemented or partial requests** in the Java server may produce X errors. SDL3 installs its own error handler, so expect some debugging.
- **Fullscreen:** SDL sends `_NET_WM_STATE_FULLSCREEN` messages, but there is no real window manager. Size the window to the screen instead (`ForceFullscreen` in Winlator does this for Wine).
- **libxcb socket path:** Winlator's libxcb is patched to `<prefix>/tmp/.X11-unix/X`. Stock libxcb first tries the **abstract** socket `@/tmp/.X11-unix/X0`. If we build our own libs, have the X server also listen on that abstract name and the socket path problem goes away. Android `LocalServerSocket` uses the abstract namespace.

### Best Turnip for Adreno 740

- A740 (a7xx gen 2) has been supported upstream since Mesa 24.0, and support has kept improving through 25.x and 26.x.
- **Ready-made:** Winlator's `graphics_driver/turnip-26.2.0.tzst`. It is Mesa 26.2.0, glibc, KGSL, with the direct-rendering WSI patch, and it is what Winlator 11.2 defaults to (`DefaultVersion`).
- **Fallbacks:** Winlator `installable_components/turnip/turnip-26.0.3.tzst` and `turnip-25.0.0.tzst`.
- **Own build:** current Mesa with `-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dplatforms=x11 -Dgallium-drivers=` against our glibc sysroot. Without the Winlator WSI patch this loses zero-copy presentation.
- **Start with** `TU_DEBUG=sysmem` (matching Winlator) and `MESA_VK_WSI_PRESENT_MODE=mailbox` or `fifo`, then A/B test gmem.

---

## 3. Input

- **Keyboard and mouse into X:**
  - `XServerDisplayActivity.dispatchKeyEvent` calls `xServer.keyboard.onKeyEvent` (`xserver/Keyboard.java`, `XKeycode.java`), which maps Android keycodes to X keycodes and sends KeyPress/KeyRelease to clients.
  - Mouse and touch go through `widget/TouchpadView` (touch-as-touchpad, external mouse, pointer capture), then `xServer.injectPointerMove`/buttons, and `XInputExtension.sendRawMotion/sendRawButtonState` for XI2 raw events (`XInputExtension.java:240-260`).
  - The on-screen controls overlay is `inputcontrols/*` plus `InputControlsView`, which maps virtual buttons to keys, mouse or gamepad. The `.icp` profiles are in `winlator/input_controls/`.
- **Gamepad, upstream Winlator:**
  - Java `winhandler/WinHandler.java` and `GamepadHandler.java` read Android `InputDevice` gamepads (`inputcontrols/ExternalController`).
  - They send the state over **UDP 127.0.0.1:7946/7947** to `winhandler.exe` and custom XInput/DInput DLLs inside Wine. This is Windows-only and useless for us.
- **Gamepad paths usable by a native SDL3 process (Android apps cannot open `/dev/input` or `/dev/uinput`):**
  1. **Ludashi "fake evdev":**
     - `Winlator-Ludashi/app/src/main/cpp/winlator/fakeinput.cpp` (about 600 lines) is an LD_PRELOAD shim.
     - It hooks `open/openat/stat/fstat/scandir/inotify_add_watch/ioctl/write/close`. Any access to `/dev/input/eventN` is redirected to a regular file in `$FAKE_EVDEV_DIR`. It answers `EVIOCGVERSION/GID/GNAME/GBIT/GABS/GKEY/GPROP/GRAB` with an Xbox 360-like device and handles `EVIOCSFF`/`EVIOCRMFF` force feedback.
     - Rumble is sent to the abstract socket `@winlator_vibration`.
     - The Java writer is `inputcontrols/FakeInputWriter.java`: 24-byte `input_event` records (BTN_A.., ABS_X/Y/RX/RY/Z/RZ, HAT0) written to `<rootfs>/dev/input/eventN`.
     - It works with an **unmodified SDL3** evdev joystick backend when udev is unavailable. Our SDL3 can only dlopen `libudev`, and none is in the rootfs, so SDL falls back to inotify/scandir on `/dev/input`.
     - It is built with the NDK (bionic) today. **For us it must be recompiled against glibc**, which is trivial, and the default dir changed.
     - Watch out for SDL3's HIDAPI and `SDL_JOYSTICK_LINUX_*` hints.
  2. **GameNative "evshim":**
     - `GameNative/app/src/main/cpp/evshim/evshim.c` is an LD_PRELOAD library. In its constructor it dlopens SDL2, calls `SDL_Init(JOYSTICK)` and `SDL_JoystickAttachVirtualEx` for up to 4 pads.
     - Threads read a 64-byte-per-pad **shared-memory file** written by Java, with a futex `seq` word. Rumble goes back through a second futex word.
     - Java side: `com/winlator/winhandler/WinHandler.java` (`notifyStateChanged`, `waitForRumble`).
     - It is SDL2-specific (`libSDL2-2.0.so.0`) and a bionic build. **The idea ports directly to SDL3:** `SDL_AttachVirtualJoystick(SDL_VirtualJoystickDesc*)` plus `SDL_SetJoystickVirtualAxis/Button/Hat`, giving a proper gamepad mapping with no evdev emulation.
     - Since we ship our own `libSDL3.so.0` (zlib licence), we can even compile this into a custom SDL3 build instead of using LD_PRELOAD.
  - **Recommendation:** an SDL3 virtual joystick (evshim-style shm plus futex) built into our own SDL3 build, or as an LD_PRELOAD glibc shim. It has fewer moving parts than fake evdev and directly yields an `SDL_Gamepad` with a standard mapping.

---

## 4. Audio

- **PulseAudio mode** (the Winlator default for Wine, `PulseAudioComponent.java`):
  - A **bionic** PulseAudio 13.0 daemon ships as `jniLibs/arm64-v8a/libpulseaudio.so` plus `libpulsecore-13.0.so`, `libpulsecommon-13.0.so`, `libsndfile`, `libltdl`, and so on.
  - It is exec'd from `nativeLibraryDir` with modules from `assets/pulseaudio.tzst`: `module-native-protocol-unix.so`, `module-aaudio-sink.so` (NDK r22b build) and `libprotocol-native.so`.
  - Generated `default.pa`:
    ```
    load-module module-native-protocol-unix auth-anonymous=1 auth-cookie-enabled=0 socket=<rootfs>/tmp/.sound/PS0
    load-module module-aaudio-sink volume=.. performance_mode=..
    set-default-sink AAudioSink
    ```
  - It runs with `--disable-shm=true --exit-idle-time=-1`.
  - The guest's glibc `libpulse.so.0` (17.0) connects through `PULSE_SERVER`. The protocol is compatible across versions.
  - GameNative and Ludashi ship their own variants (`pulseaudio-gamenative-*.tzst`, `jniLibs/libgn_pulse*.so`).
- **ALSA mode:**
  - The glibc ALSA plugin `libasound_module_pcm_android_aserver.so` (source in `winlator/android_alsa/module_pcm_android_aserver.c`, config `android_aserver.conf` sets `pcm.!default`) connects to `$ANDROID_ALSA_SERVER`.
  - The Java `alsaserver/ALSAClient.java` writes to an `AudioTrack` (PERFORMANCE_MODE_LOW_LATENCY), with an optional shared-memory ring.
- **How our game reaches AAudio:** `ALAudio.so` uses OpenAL Soft 1.24.3. Our `libopenal.so.1` dlopens `libpulse.so.0` and `libasound.so.2`.
  - Both Winlator paths work as they are: glibc `libpulse` → bionic `pulseaudio` daemon → `module-aaudio-sink` → AAudio, or glibc `libasound` + the aserver plugin → Java AudioTrack.
  - SDL3 audio can also use `libpulse`.
- **Leanest option:** write a tiny OpenAL Soft or SDL3 backend ourselves, a Unix socket or shm ring into an AAudio/Oboe stream in the app process. It is about 300 lines, removes PulseAudio entirely, and gives the lowest latency.
- **Fastest to get working:** reuse the bionic PulseAudio daemon plus glibc `libpulse`. This is the proven path; Winlator's README even suggests raising latency to 90 ms for Unreal Gold crackle.

---

## 5. Licences and binary blobs

| Component | Licence | Notes |
|---|---|---|
| Winlator app (Java X server, ALSA server, input controls, `cpp/winlator`) | **LGPL-2.1** (`winlator-app/LICENSE`) | Reusable in our app. Modifications to those files must be published under LGPL. |
| vortek, gladio | LGPL-2.1 | |
| libadrenotools (vendored in winlator-app and GameNative) | BSD-2-Clause | |
| glibc (Winlator rootfs, termux-pacman patches) | LGPL-2.1+ | Provide source and patches (termux-pacman glibc-packages). |
| libX11/xcb/Xext/... | MIT/X11 | |
| Mesa Turnip | MIT | Winlator's WSI patch source is not published. |
| Vulkan loader | Apache-2.0 | |
| alsa-lib, android_aserver plugin | LGPL-2.1 | |
| PulseAudio (bionic daemon and modules, glibc libpulse) | LGPL-2.1 | Some modules can be GPL depending on their deps. libsndfile is LGPL. |
| box64 | MIT | We don't need it. |
| Wine, wine-mono, wine-gecko | LGPL / various | We don't need them. |
| GameNative | **GPL-3.0** | evshim.c is GPL-3.0. Proprietary blobs in the APK: `libredirect.so` / `libredirect-bionic-wx.so` (closed, it exists to rewrite `com.winlator` paths and handle Android 15 exec) and `libsteambootstrap.so`. `libgamenative_drm.so` is GPL-3.0 (github.com/GameNative/gamenative-drm). |
| Winlator-Ludashi | **GPL-3.0** | fakeinput.cpp, ludashix11. Vendors proot (GPL-2.0) and patchelf (GPL-3.0). Bundles Termux bionic binaries and 7-Zip (LGPL with unRAR restriction). |
| Our side: SDL3 | zlib | OpenAL Soft is LGPL-2.1. |

- **Practical consequence:** taking code from GameNative or Ludashi (evshim, fakeinput, ludashix11) makes the result GPL-3.0. Reimplementing those ideas is easy; they are small, so writing our own avoids GPL.
- Winlator code is LGPL-2.1. It is fine to embed in a closed or MIT app if we publish our changes to those files and allow relinking.
- **Binary blobs Winlator redistributes, all prebuilt:**
  - `rootfs.tzst` (a whole glibc userland plus Wine)
  - Turnip, Zink, VirGL, Vortek, Gladio tzst
  - `box64-*.tzst`, `dxvk`, `vkd3d`, `wined3d`
  - `pulseaudio.tzst` plus jniLibs (PulseAudio, glib, fluidsynth, oboe, opus, vorbis, FLAC, libc++_shared)
  - `wine-mono` / `wine-gecko` MSIs
  - Windows exes in `rootfs_patches.tzst` (7-Zip, `winhandler.exe`, fonts, which include MS core fonts)
  - **Nothing is proprietary to Qualcomm.** Turnip talks to the KGSL kernel driver directly.

---

## 6. Recommendation: the minimal embed for `ut-bin-arm64`

### Architecture (MVP)

```
Android app (targetSdk 28, arm64, extractNativeLibs)
 ├─ Activity: SurfaceView/GLSurfaceView + Winlator Java X server (xserver/*, xconnector/*, renderer/*, cpp/winlator/*)
 │     listens <root>/tmp/.X11-unix/X0 (+ abstract @/tmp/.X11-unix/X0), screen = 1920x1080 or panel size,
 │     DRI3 BufferFromPixmap → AHardwareBuffer → EGLImage (zero-copy present)
 ├─ Input: Android KeyEvent/MotionEvent → X keyboard / XI2 raw mouse; gamepad → shm file → SDL3 virtual joystick
 ├─ Audio: either bionic pulseaudio (libpulseaudio.so + module-aaudio-sink) or own AAudio socket backend
 ├─ SysV-shm server (Winlator SysVSharedMemoryComponent) for any shmget (MIT-SHM software paths)
 └─ ProcessBuilder:  <root>/lib/ld-linux-aarch64.so.1 --library-path <ut>/System:<root>/usr/lib  <ut>/System/ut-bin-arm64
       env: DISPLAY=:0  HOME TMPDIR  VK_DRIVER_FILES=<root>/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json
            TU_DEBUG=noconform,sysmem  MESA_VK_WSI_NATIVE_MEM_IMPORTED=1  MESA_VK_WSI_PRESENT_MODE=fifo|mailbox
            PULSE_SERVER=<root>/tmp/.sound/PS0  ANDROID_SYSVSHM_SERVER=<root>/tmp/.sysvshm/SM0
            SDL_VIDEO_DRIVER=x11  SDL_AUDIO_DRIVER=pulseaudio  LD_PRELOAD=<shim for gamepad (if not built into SDL3)>
```

Invoke ld.so explicitly, because `ut-bin` has PT_INTERP `/lib/ld-linux-aarch64.so.1`, which does not exist on Android. Alternatively, patchelf the interpreter.

### Minimal glibc userland (about 10-15 MB instead of 65 MB)

- **Core runtime:** `ld-linux-aarch64.so.1`, `libc.so.6`, `libm.so.6`, `libdl`, `libpthread`, `librt`, `libresolv`, `libnss_files`, `libnss_dns`, `libstdc++.so.6`, `libgcc_s.so.1`, `libatomic.so.1`. libatomic is not in Winlator's rootfs; take it from the same GCC.
- **X11:** `libX11`, `libX11-xcb`, `libxcb` (+dri3, present, randr, shm, sync, xfixes), `libXext`, `libXi`, `libXrandr`, `libXcursor`, `libXfixes`, `libXrender`, `libXau`, `libXdmcp`, plus `usr/share/X11/{XKeysymDB,locale}` and `etc/resolv.conf`.
- **Vulkan:** `libvulkan.so.1`, Turnip `libvulkan_freedreno.so` plus ICD JSON, `libdrm.so.2`, `libz`, `libzstd`, `libexpat` (Mesa).
- **Audio:** `libpulse.so.0` + `libpulsecommon-17.0.so` (+ `libdbus`? check NEEDED) or `libasound.so.2` + the aserver plugin.
- `libfreetype` is not needed, because SDL3_ttf links it statically or uses its own copy; check NEEDED.

### Two ways to get the glibc userland

- **A, fastest to get running:** set our applicationId to something of exactly the same length as `com.winlator` (12 characters, for example `com.ut99andr`, which is also 12). Binary-patch the string `/data/data/com.winlator/files/rootfs` in the extracted libs. All ELF strings and the Turnip ICD JSON have fixed length, and the ld.so cache gets rebuilt or ignored. This is hacky but lets us reuse Winlator's tested glibc 2.39, libX11/xcb and patched Turnip 26.2 on day one.
- **B, proper:** build our own sysroot with the termux-pacman `gpkg/glibc` recipe (2.44, our prefix) plus the X11/xcb/libpulse/alsa recipes from termux-pacman `gpkg/*`. Build Mesa Turnip for KGSL ourselves.
  - We would still lack Winlator's unpublished WSI direct-rendering patch. Either ask brunodev85 for it, reimplement it (an AHB-fd import path in `wsi_common_x11.c`), or keep their binary.

### Effort estimate (one experienced dev)

| Work item | Effort |
|---|---|
| App skeleton, rootfs extraction, process launch through ld.so, logcat piping | 2-4 days |
| Integrate Winlator X server (strip Wine/WinHandler/container parts) and get SDL3 X11 plus Vulkan presenting (expect X protocol gaps: RANDR stub, `_NET_WM` fullscreen, focus, cursor hide/confine for relative mouse) | 1-2 weeks |
| Gamepad: SDL3 virtual joystick shim plus Java InputDevice → shm writer, with rumble | 3-5 days |
| Audio through PulseAudio reuse: 1-2 days. Own AAudio backend: 3-5 days | 1-5 days |
| Touch overlay (reuse Winlator `inputcontrols` LGPL) | 3-5 days |
| glibc and Turnip rebuild with our own prefix (option B) | 1-2 weeks, can be deferred |

### Risks and how to mitigate them

1. **Seccomp SIGSYS.** Only use the Android-patched glibc. Stock glibc dies on `set_robust_list`/`rseq`/`clone3`. Set `GLIBC_TUNABLES=glibc.pthread.rseq=0` as an extra safeguard.
2. **W^X.** Stay on targetSdk 28. Moving to ≥29 needs the "loader in `nativeLibraryDir`" trick (untested for glibc). GameNative's closed `libredirect-bionic-wx` shows the multi-process case is hard; our single-process case is easier.
3. **Hardcoded `com.winlator` prefix** in every reused binary: patch the strings or rebuild.
4. **SDL3 X11 driver vs a minimal Java X server.**
   - The server has no RANDR upstream (Ludashi's Java server has RANDR), no XKB and no XFIXES.
   - Plan to add a RANDR 1.x stub, or rely on SDL3's StdXlib fallback, and to set the screen size to the desired render resolution.
   - Do test SDL3 relative mouse, which uses XI2 raw events; those exist.
5. **SDL3 gamepad:** `/dev/input` and `/dev/uinput` are inaccessible to apps, and there is no libudev. Use an SDL3 virtual joystick (an evshim-like design, reimplemented to avoid GPL-3) or a glibc rebuild of Ludashi's fakeinput (GPL-3).
6. **MIT-SHM / SysV shm:** only used by software paths (SDL window framebuffer, Mesa sw WSI). Run Winlator's `SysVSharedMemoryComponent` and the patched glibc `shm*`, or hide MIT-SHM.
7. **Zero-copy present depends on Winlator's unpublished Turnip WSI patch.** Without it, presentation falls back to a CPU copy through `PixmapFromBuffer` and mmap.
   - **Long-term "most native" alternative:** a small Vulkan WSI layer in the guest that allocates swapchain images as AHardwareBuffers obtained from the app over a Unix socket (as dmabuf fds) and presents them through `ASurfaceTransaction`/SurfaceControl. Pair it with a custom SDL3 video backend, so no X server is needed at all. GameNative's `libgamenative_drm.so` (GPL-3) and Vortek do comparable things.
8. **DNS / networking:** write `<prefix>/etc/resolv.conf` from `ConnectivityManager` (the Ludashi fork uses the `ANDROID_RESOLV_DNS` env var for this). Android has no `/etc/resolv.conf`.
9. **Lifecycle:** suspend the guest on pause with SIGSTOP/SIGCONT (`GuestProgramLauncherComponent.onPause/onResume`). Keep a foreground service (`services/ForegroundService`) so audio and the process survive.
