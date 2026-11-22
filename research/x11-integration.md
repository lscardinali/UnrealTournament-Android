# Termux:X11 integration (X server + display) for the UT99 Android port

Status (2026-11-21): Termux:X11 is in the project, patched, and builds on the Windows host.
`gradlew.bat :lorie:assembleDebug` and `gradlew.bat :app:assembleDebug` both pass, including a clean native
rebuild started from PowerShell. The debug APK ships `lib/arm64-v8a/libXlorie.so` (3 MB stripped). The Kotlin glue
compiles, but nothing calls it yet: it isn't wired into MainActivity, and it hasn't been run on the device.

This follows pocket-foundry's architecture (see `pocket-foundry.md`): Termux:X11's X server runs as its own
`app_process` process. The game connects to it as an ordinary X11 client from the glibc process. Termux:X11's own
display activity shows the X screen.

## What was added

| Path (under `D:\Developer\UTAndroid\Android`) | What |
|---|---|
| `third_party/termux-x11/` | `git clone https://github.com/termux/termux-x11` at **e3a1657b781f7a880160d7ce5cdd48b866e062dd** (the commit pocket-foundry pins), with its 16 submodules checked out recursively at depth 1. This is a plain clone, not a submodule, because our project isn't a git repo. |
| `third_party/patches/termux-x11/0001..0018-*.patch` | pocket-foundry's patches, copied as-is and converted to LF line endings (see fixes). |
| `third_party/patches/termux-x11/0019-windows-host-build.patch` | Ours: the Windows build fixes listed below. All 19 patches reproduce the tree from the pinned commit. |
| `third_party/patches/.gitattributes` | `*.patch -text`, so a future git checkout doesn't add CRLF to the patches. |
| `third_party/shell-loader-parent/README.md` | pocket-foundry's empty parent directory for `:shell-loader:stub`. |
| `third_party/pocketfoundry-wm/pocketfoundry-wm.c` | A copy of pocket-foundry's tiny window manager (GPL-3.0). It isn't built yet (see the WM section). |
| `tools/setup-x11.sh` | Rerunnable setup, adapted from pocket-foundry's `tools/setup.sh`. It clones and pins the repo, initialises the submodules with symlinks off and autocrlf off, and applies the patches. It records the applied set in `.git/utandroid-patches`. Run it in Git Bash. |
| `settings.gradle.kts` | Adds `:lorie` → `third_party/termux-x11/lorie` and `:shell-loader:stub` → `third_party/termux-x11/shell-loader/stub` (parent `:shell-loader` → `third_party/shell-loader-parent`), the same way pocket-foundry does. |
| `app/build.gradle.kts` | Only `implementation(project(":lorie"))`. |
| `gradle.properties` | `termuxX11NdkVersion=28.2.13676358` and `org.gradle.configuration-cache.problems=warn` (see fixes). |
| `app/src/main/AndroidManifest.xml` | Sets `android:name=".x11.UTApplication"`, with `tools:replace="android:name,android:allowBackup"` because `:lorie` declares `LorieApp` and `allowBackup=false`. Overrides `com.termux.x11.MainActivity`: removes its launcher intent filters, uses our label and icon, and sets `sensorLandscape`. Removes Termux:X11's `KeyInterceptor` accessibility service and the `WRITE_SECURE_SETTINGS` permission. |
| `app/src/main/java/com/lscardinali/unrealtournament/x11/` | `XServer.kt`, `XDisplay.kt`, `UTApplication.kt` (API below). |

Host change: I installed `bison` (3.8.2, plus `m4`) into the existing MSYS2 at `C:\msys64` with
`pacman -S bison`. The `info` post-install hook failed, which is harmless. No bison existed anywhere on the
machine, and `:lorie` needs it to generate xkbcomp's parser.

## Build fixes, one by one

1. **CRLF patches.** In the pocket-foundry clone the `.patch` files were checked out with CRLF (autocrlf), so every
   `git apply` failed with "patch does not apply". Fix: strip the `\r` (`sed -i 's/\r$//'`). The setup script
   does this every time, and `.gitattributes` marks the patches `-text`.
2. **Symlinks in submodules.** `pixman` has 17 symlinks, all in `.gitlab-ci.d` CI files. Without Developer Mode
   Windows refuses them ("Permission denied") and the submodule checkout aborts. Fix: `core.symlinks=false` for the
   submodules, so the links become small text files. Nothing that gets built uses them. The main repo and the other
   submodules have no symlinks.
3. **NDK 29.0.14206865 isn't installed.** Termux:X11's `lorie/version.gradle` pins it. Fix (in 0019):
   `ext.termuxX11NdkVersion = project.findProperty("termuxX11NdkVersion") ?: "29.0.14206865"`, plus
   `termuxX11NdkVersion=28.2.13676358` in our `gradle.properties`. The build was verified to use NDK 28.2. The app
   module doesn't set `ndkVersion`, since it builds no native code of its own. If you add CMake to `:app`, use the
   same property, as pocket-foundry does (`ndkVersion termuxX11NdkVersion`).
4. **Configuration cache.** Our project enables it. `lorie/build.gradle` (BuildConfig COMMIT) and `version.gradle`
   run `"git rev-parse …".execute()` while configuring, which fails the build with "external process started". Fix:
   `org.gradle.configuration-cache.problems=warn`. The cache still gets stored ("2 problems"). Because our root
   directory isn't a git repo, the version string comes out as `…-nongit-…`, which is harmless.
5. **No bison.** `find_package(BISON REQUIRED)` is used for xkbcomp's `xkbparse.y`. Fix: install MSYS2 bison (see
   above), and in 0019, on a Windows host, add `$ENV{LORIE_HOST_TOOLS}`, `C:/msys64/usr/bin` and
   `C:/Program Files/Git/usr/bin` to `CMAKE_PROGRAM_PATH`. CMake then finds bison even from Android Studio or
   PowerShell, where those directories aren't on PATH.
6. **`target_apply_patch` used `bash -c "patch …"`.** On Windows, `bash` may resolve to WSL's
   `System32\bash.exe`, and failures were silent, which would leave pixman, xkbcomp, libX11, Xtrans, xserver and
   libepoxy unpatched. Fix (0019): on a Windows host, call `patch.exe` from Git for Windows directly
   (`find_program(LORIE_PATCH patch REQUIRED)`). It keeps the reverse dry-run first, so it's still idempotent, and
   it now fails the configure step loudly if a patch doesn't apply.
7. **`cmake -E create_symlink`** (Xtrans directory, `xshmfence.h`) fails without Developer Mode. Fix (0019): a
   `lorie_link_or_copy()` helper that copies on Windows and symlinks elsewhere. The Xtrans copy was moved after its
   patch is applied, so the copy contains the patched sources.
8. **`makekeys` built with `/usr/bin/gcc`.** It's a host tool that generates `ks_tables.h`. Fix (0019): on Windows,
   `find_program(LORIE_HOST_CC NAMES gcc cc clang REQUIRED)`. Here that resolves to llvm-mingw's `gcc.exe` from
   WinGet, and the `cmd` `&&` and `>` in the custom command work as they are.

Nothing in AGP 9.5.2 or Gradle 9.6 needed a change. pocket-foundry uses AGP 9.3.1, and `:lorie` builds as-is, even
with `compileSdkVersion 34` and Java 9 source/target. The `:lorie` script finds its host app by looking for a
`com.android.application` project that has `implementation project(':lorie')`. It reads `applicationId` from it,
which goes into BuildConfig and the broadcast's target package, so `:app` must keep that dependency in
`implementation`.

## API (`com.lscardinali.unrealtournament.x11`)

```kotlin
// Once, e.g. at app start or before the first game (writes Termux:X11's prefs: fullscreen, landscape,
// hidden cutout, no extra-keys bar, direct touch, screen never idles, raw hardware keys).
XDisplay.configure(context, XDisplay.Resolution.Native)   // or Scaled(150) / Exact("1280x720")

// Off the main thread: starts app_process → com.termux.x11.CmdEntryPoint :0 -ac, waits for the socket (≤30 s).
XServer.start(context, log = File(context.filesDir, "logs/x11.log"))

// Show the X screen (Termux:X11's MainActivity, fullscreen, sensorLandscape). It connects when the server
// broadcasts CmdEntryPoint.ACTION_START (LorieApp.Receiver → UTApplication), showing "Not connected" until then.
XDisplay.show(activity)

// Then start the game (glibc) with the client environment merged in:
GlibcProgram(context).start("UnrealTournament", args, log, extraEnv = XServer.clientEnvironment(context) + ...)

XServer.stop()   // when the game exits
```

- **Display / socket.** Display `:0`. The socket is `filesDir/tmp/.X11-unix/X0`, i.e.
  `/data/user/0/com.lscardinali.unrealtournament/files/tmp/.X11-unix/X0`. **The client must get
  `DISPLAY=<that absolute path>`**, not `:0`, because there's no `/tmp` on Android. libxcb treats a DISPLAY that
  starts with `/` as the socket path itself. pocket-foundry does exactly this (`Program.kt`:
  `DISPLAY=paths.xSocket.path`). The other option is `:0` with a client libX11/xcb patched to look in `$TMPDIR`.
- **TMPDIR must match.** The server's TMPDIR is `filesDir/tmp`, which equals `GlibcProgram.tmpDir`.
  `XServer.clientEnvironment()` returns `DISPLAY`, `TMPDIR` and `XDG_RUNTIME_DIR`.
- **Server environment** (set by `XServer.start`): `CLASSPATH=<our base.apk>`, `TMPDIR`,
  `XLORIE_LIB=<nativeLibraryDir>/libXlorie.so` (patch 0001; works because we use `useLegacyPackaging=true`),
  `XKB_CONFIG_ROOT` if `filesDir/usr/share/X11/xkb` exists, and no `LD_PRELOAD`.
- **XKB data is required and not shipped yet.** The server refuses to start without xkeyboard-config data. It looks
  for `XKB_CONFIG_ROOT`, then `dirname(TMPDIR)/usr/share/X11/xkb` (= `filesDir/usr/share/X11/xkb`). Put a copy of
  `share/X11/xkb` there, from Debian or Termux `xkeyboard-config`, about 1 MB as a tarball. pocket-foundry ships it
  in its runtime tarball. Fonts are optional, looked up at `dirname(TMPDIR)/usr/share/fonts/X11` etc.
- **Input.** Termux:X11's activity handles touch, mouse, keyboard and gamepads (as keys). For a custom controller
  overlay, pocket-foundry hooks `MainActivity` from outside with `ActivityLifecycleCallbacks` (`GameScreens.kt`,
  `PointerGestures.kt`, using `activity.lorieView.sendMouseEvent`, patch 0009 makes the fields public). That's the
  pattern to copy later.
- **Process lifetime.** The server dies with our app's process only if we stop it. Call `XServer.stop()`. A stale
  socket or lock is deleted on the next start.

I didn't write our own "GameActivity" with a LorieView. Termux:X11's `MainActivity` is about 1,000 lines of
connection, surface, input and IME handling that LorieView depends on, and pocket-foundry also reuses it unchanged
and adds its UI from outside. If we later want our own activity, subclassing isn't straightforward
(`singleInstance`, static `MainActivity.getInstance`). The lifecycle-callbacks approach is the tested one.

## pocket-foundry's window manager

Without a WM, SDL's fullscreen request is ignored and the window opens at its requested size in the corner of the X
screen. `pocketfoundry-wm.c` (≈120 lines, xcb + xcb-randr) handles this. It takes SubstructureRedirect on the root
and announces EWMH (`_NET_SUPPORTING_WM_CHECK`, `_NET_WM_STATE_FULLSCREEN`). It maps every window at full screen size
and resizes them on RandR screen changes. pocket-foundry builds it with its glibc toolchain
(`$CC -O2 -o pocketfoundry-wm pocketfoundry-wm.c -lxcb -lxcb-randr`). It runs it as a glibc program after the X server
is up and before the game (`Session.start`: `Program.start(paths, [windowManager], wmLog)`), with the same DISPLAY,
and sends it SIGTERM on stop. For UT we'd need the same: cross-compile it for aarch64 glibc and ship it next to the
game, or run it through the glibc loader like the game. Another option is to skip it and make UT open a window
exactly the size of the X screen (SDL fullscreen-desktop, or a fixed resolution equal to `XDisplay.Resolution`),
but then nothing handles a resolution change.

## Open questions

1. **GPU path: DRI3 / AHardwareBuffer.** lorie implements DRI3 v2 `PixmapFromBuffers` (InitOutput.c
   `loriePixmapFromFds`) only for these modifiers: `1255` AHARDWAREBUFFER_SOCKET_FD (an AHB sent over a unix
   socket), `1256` the flipped variant, and `1274` RAW_MMAPPABLE_FD (shareable memory), plus LINEAR/INVALID through
   raw fds. A stock Mesa X11 WSI (Turnip/Zink over DRI3) sends dma-buf fds with real modifiers, and lorie rejects
   them. Mesa needs Termux's patched WSI, which exports AHBs with the 1255 modifier. Termux's mesa
   `wsi_common_x11` patches, or the `mesa-for-android-container` fork that pocket-foundry uses
   (`runtime/mesa/src`), are candidates. pocket-foundry doesn't currently use DRI3 for drawing. Its MobileGlues path
   draws offscreen (libhybris EGL, `EGL_PLATFORM=null`), and a shim (`libmgx11.so`) copies frames into AHBs that the
   X server shows. Its Zink/Turnip path uses its own Mesa build (see `pocket-foundry.md`). Pass `-disable-dri3` to
   `XServer.start` when using lavapipe or llvmpipe. UT 469f's renderers are OpenGL (XOpenGLDrv / OpenGLDrv), so the
   choices are the same as pocket-foundry's. To decide.
2. **Client environment for UT.** At least `DISPLAY=<socket path>`, `TMPDIR`, `XDG_RUNTIME_DIR`, `HOME`,
   `XLOCALEDIR` (libX11 locale data, if the game uses XIM/Xlib text, which SDL2 does lightly), and `LANG=C.UTF-8`.
   SDL needs `SDL_VIDEODRIVER=x11` (default when DISPLAY is set). For GL, the driver's variables: for Zink,
   `MESA_LOADER_DRIVER_OVERRIDE=zink`, `VK_DRIVER_FILES=<turnip icd json>`, `GALLIUM_DRIVER=zink`. For MobileGlues,
   see pocket-foundry `Drivers.mobileGlues`. Also `SDL_VIDEO_X11_SCALING_FACTOR` if UI scale matters. Our glibc
   runtime also needs libX11, libxcb, libXext, libXrandr, libXi, libXcursor and libXau/Xdmcp. Do UT's shipped SDL2
   and these libraries accept a socket-path DISPLAY? libxcb ≥ 1.12 does. Check what's bundled.
3. **XKB and locale data**: we need to ship them (see above).
4. **Knock port.** Patch 0003 moves Termux:X11's TCP knock port to 7894 so the real Termux:X11 (7892) can be
   installed alongside. pocket-foundry also uses 7894, so running both games at once would clash. Change it to, say,
   7896 in a 0020 patch if that matters.
5. **Release builds.** Not tried. `:lorie`'s consumer ProGuard rules keep `CmdEntryPoint.main` and the JNI
   classes, but our app's `optimization { packageScope = … }` block is AGP-9-specific. Check `assembleRelease` and
   that the `app_process` entry point survives R8.
6. **Activity export.** The overridden `com.termux.x11.MainActivity` stays `exported="true"`, as in
   pocket-foundry. Consider `android:exported="false"` with `tools:replace`.
7. **Not verified on the device.** That the broadcast reaches `UTApplication` on Android 13 (patches 0007/0012/0017
   cover the ROM quirks seen so far), the startup time of `app_process` on the RP6, and the display activity's
   behaviour on the 1080p panel.
