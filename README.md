<p align="center">
  <img src="Android/app/src/main/res/drawable-nodpi/ic_launcher_logo.png" alt="Unreal Tournament" width="112" height="112">
  &nbsp;&nbsp;&nbsp;
  <img src="Android/app/src/unreal/res/drawable-nodpi/ic_launcher_logo.png" alt="Unreal Gold" width="112" height="112">
</p>

<h1 align="center">Unreal Tournament & Unreal Gold for Android</h1>

**Unreal Tournament** (1999, Game of the Year Edition) and **Unreal Gold** (Unreal and Return to Na Pali) on Android
handhelds, running the [OldUnreal](https://www.oldunreal.com/) community patches' own **Linux arm64** game binaries
natively inside the app. No emulation, no X server, no Linux container. Built and tuned on the Retroid Pocket 6
(Snapdragon 8 Gen 2), where both run at 120 fps at 1920×1080.

> **These ports exist because of [OldUnreal](https://www.oldunreal.com/).** Everything that makes them possible, from
> the 469 and 227 patches and their native Linux arm64 builds to the renderers, the modern SDL3 and OpenAL support,
> and the official installers and download mirrors, is their work, done for free over many years. This project is
> only a thin layer that brings their builds to Android handhelds. If you enjoy it, please
> **[donate to OldUnreal](https://www.oldunreal.com/phpBB3/donate)** to help cover their servers and development
> hardware, and join their forums and wiki.

**The apps contain no game files.** On first launch each one downloads its game from OldUnreal's servers, the same
way OldUnreal's official installers do.

## The two apps

They're separate apps built from the same code, and can be installed side by side.

| | Unreal Tournament | Unreal Gold |
|---|---|---|
| APK | `UnrealTournament-Android-<version>.apk` | `UnrealGold-Android-<version>.apk` |
| Package | `com.lscardinali.unrealtournament` | `com.lscardinali.unreal` |
| Game download | UT GOTY CD image (650 MB) + Bonus Pack 4, from OldUnreal's mirrors | Unreal Gold CD image (680 MB), from OldUnreal's mirrors |
| Patch | latest 469 ([UnrealTournamentPatches](https://github.com/OldUnreal/UnrealTournamentPatches)) | latest 227 ([Unreal-testing](https://github.com/OldUnreal/Unreal-testing)) |
| Renderer | Vulkan (Vulkan 1.1 required) | XOpenGL in OpenGL ES mode (GLES 3.2 required) |
| Installed size | about 800 MB | about 810 MB |
| Online play | server browser, join, host | as on PC |

## Features (both apps)

- **Native:** the game's own Linux arm64 build loaded in-process by a small ELF loader, Android SDL3 for input,
  window and audio, OpenAL Soft, and the phone's own GPU driver.
- **First-run setup:** downloads the game and the latest OldUnreal patch, checks every download against its SHA-256,
  and installs to internal storage or the SD card.
- **Controller first:**
  - every menu, dialog, list, tab, text field and key binding works with the D-pad;
  - an on-screen pad keyboard for typing;
  - the touchscreen works as an optional touchpad for the mouse cursor.
- **Mod → Handheld Settings** in the game's menus: gyro aiming (on/off, sensitivity, invert), look stick response
  curve, turn boost, performance overlay, and a 60 fps limit for battery life.

## Requirements

- Android 9 or newer, 64-bit ARM (arm64-v8a).
- UT: Vulkan 1.1. Unreal Gold: OpenGL ES 3.2.
- A controller: built in (handhelds) or connected. Touch only drives the menu cursor.
- About 1.7 GB free during setup (about 800 MB once installed).

### Tested devices: we need your reports!

So far both apps have **only been tested on the Retroid Pocket 6 (Snapdragon 8 Gen 2, Adreno 740, Android 13)**.
Other 64-bit Android devices should work, but nobody has tried them yet.

If you run either app on anything else, please
[open an issue](https://github.com/lscardinali/UnrealTournament-Android/issues/new) and tell us:

- which app, the device, chipset and Android version;
- whether setup, the menus, playing and online play work;
- the frame rate and anything odd (turn on the overlay with L3 + R3 or in Mod → Handheld Settings);
- if it crashes, the log from `adb logcat -s utloader UTAndroid SDL AndroidRuntime`.

Reports of what works are just as useful as bug reports.

| Device | Chipset | Unreal Tournament | Unreal Gold |
|---|---|---|---|
| Retroid Pocket 6 | Snapdragon 8 Gen 2 | Works: 120 fps at 1080p, controller, gyro, online | Works: 120 fps at 1080p, controller, gyro |

## Installing

1. Download the APK for the game you want from the [releases page](https://github.com/lscardinali/UnrealTournament-Android/releases)
   and install it.
2. Open it. The setup screen explains what it will download and asks where to install. With an SD card, maps,
   textures, sounds and music go on the card; the program files (about 130–180 MB) always stay on internal storage.
3. Accept the Epic Games Terms of Service and install. It takes a minute or two on a fast connection.
4. Play. Start opens the menus.

To reinstall or move the game later (your saves are kept):

- UT: `adb shell am start -n com.lscardinali.unrealtournament/.MainActivity --ez setup true`
- Unreal Gold: `adb shell am start -n com.lscardinali.unreal/com.lscardinali.unrealtournament.MainActivity --ez setup true`

## Controls

Defaults; change them in Options → Preferences → Controls (with the pad), and the gyro in Mod → Handheld Settings.

| Button | Unreal Tournament | Unreal Gold |
|---|---|---|
| Left stick | Move / strafe | Move / strafe |
| Right stick | Look (hold fully sideways for a turn boost) | Look (turn boost too) |
| Gyro | Fine aim (turn the handheld) | Fine aim |
| R2 / L2 | Fire / alternate fire | Fire / alternate fire |
| A / B | Jump / crouch | Jump / crouch |
| X | Translocator | Use the selected item |
| Y | Throw weapon | Best weapon |
| R1 / L1 | Next / previous weapon | Next / previous weapon |
| D-pad | Flak cannon, shock rifle, sniper rifle, rocket launcher | Left/right: previous/next item; up: quick save; down: throw weapon |
| L3 / R3 | Walk / feign death | Walk / centre view |
| Select | Scores | Translator |
| Start | Menu | Menu |
| L3 + R3 | Show / hide the performance overlay | Show / hide the performance overlay |

In the menus (both):

| Button | Action |
|---|---|
| D-pad | Move between controls (a yellow box shows the focus), move along the menu bar and in lists |
| Left / Right | Adjust a slider or dropdown |
| A | Select, press, toggle, open a list or the on-screen keyboard |
| B | Back / close |
| R1 / L1 | Next / previous tab |
| Select | Open the menu bar |
| Touchscreen | Slide to move the mouse cursor, tap to click |

On-screen keyboard: D-pad picks a key, A types, B deletes, X adds a space, Y is Shift, L1/R1 move the caret,
Start closes it.

## Building from source

The build runs on Windows with Git Bash, Android Studio (its JDK and the Android SDK/NDK) and Python 3. The scripts
find the SDK through `ANDROID_HOME` (or the standard install folder) and prefer NDK 28.2.13676358.

1. Native libraries (each script clones its upstream source at a pinned version, see `native/SOURCES.md`):
   ```
   bash native/sdl/build-sdl-android.sh        # SDL3 + SDL3_ttf with the controller bridge
   bash native/openal/build-openal-android.sh  # OpenAL Soft (OpenSL ES backend)
   bash native/utloader/build.sh               # libutloader.so: ELF loader + glibc-to-bionic shim
   ```
   Copy the resulting `.so` files to `Android/app/src/main/jniLibs/arm64-v8a/` (committed builds are already there).
2. Runtime libraries (`libgcc_s.so.1`, `libstdc++.so.6`, from Debian): `python native/runtime/fetch_debs.py`, then
   copy them to `Android/app/src/main/assets/runtime/` (already committed).
3. PadUI (the controller UI, UnrealScript), compiled with the Windows `UCC.exe` of a patched install (already
   committed):
   - UT: `bash native/padui/build-padui.sh`, using a UT 469 install in `UnrealTournament/`;
   - Unreal Gold: `bash native/padui/build-padui-227.sh`, using an Unreal 227 install in `Unreal/` (or `UNREAL=path`).
     The source is shared; lines tagged `//#UT` or `//#227` are only built for that game.
4. The apps (`cd Android`): `./gradlew assembleUtRelease` (UT), `./gradlew assembleUnrealRelease` (Unreal Gold), or
   `./gradlew assembleRelease` (both). Release signing reads `Android/keystore.properties` (`storeFile`,
   `storePassword`, `keyAlias`, `keyPassword`); without it release builds are signed with the debug key.

Maintenance: add `-PdebuggableRelease` to build the same release but debuggable, so `adb shell run-as` can reach an
installed copy (for example to back up or restore saves). Installing the normal build over it keeps the data.

PC unit tests (the CD image reader, map decompressor and both installers against real data, when present):
`./gradlew testUtDebugUnitTest testUnrealDebugUnitTest`.

## How it works

- `native/utloader`: an ELF loader that maps the game's glibc-linked binaries into the Android process and resolves
  their libc calls to bionic through a shim, including TLS, signals, locale and dlopen. The game runs on its own
  thread. SDL3 and OpenAL calls go to the app's Android builds; Vulkan and OpenGL ES go to the system driver.
- `native/sdl/utandroid`: a virtual controller in SDL that shapes the real pad (dead zones, look curve, frame-rate
  independent look, gyro axes) and turns it into menu keys.
- `native/padui`: an UnrealScript root window that gives every UWindow dialog a D-pad focus box, the pad keyboard
  and the Handheld Settings window.
- `Android/app`: one app per game as build flavors (`ut`, `unreal`). `setup/Product.kt` holds what differs: downloads,
  patch, install layout, setup screen look. It also contains the setup (download, CD image reader, `.uz` map
  decompressor, installer), the game activity, overlays and ini defaults (`GameConfig` for UT, `UnrealConfig` for
  Unreal Gold).
- `docs/`: design notes and measurements, starting with [docs/README.md](docs/README.md).
  [docs/unreal-227-spike.md](docs/unreal-227-spike.md) covers what Unreal Gold needed on top of UT (the GLES shader
  rewrite, EGL after resume, 227's file layout).

## Legal and licences

- **Unreal, Unreal Gold and Unreal Tournament** are © Epic Games. This project ships none of their files. The setup
  downloads the CD images (and UT's Bonus Pack 4) from OldUnreal's mirrors (archive.org as a fallback for UT). The
  Epic Games Terms of Service (https://legal.epicgames.com/en-US/epicgames/tos) apply to the use and distribution of
  the games, and the setup asks you to accept them, as OldUnreal's installers do.
- **OldUnreal's 469 and 227 patches** are downloaded from GitHub
  ([UnrealTournamentPatches](https://github.com/OldUnreal/UnrealTournamentPatches),
  [Unreal-testing](https://github.com/OldUnreal/Unreal-testing)): see their licences, installed with the game.
- Shipped in the APKs, with their licences:
  - SDL3 and SDL3_ttf: zlib licence. SDL3_ttf is built with FreeType (FreeType Licence) and HarfBuzz (MIT).
  - OpenAL Soft: LGPL-2.1, built unmodified from https://github.com/kcat/openal-soft (1.24.3) as a separate shared
    library (`native/openal/build-openal-android.sh` rebuilds it).
  - libgcc_s and libstdc++ (Debian): GPL-3.0 with the GCC Runtime Library Exception.
  - Apache Commons Compress and Commons IO/Lang: Apache-2.0. XZ for Java: 0BSD. AndroidX, Jetpack Compose and
    Kotlin: Apache-2.0.
- This project's own code (loader, shim, SDL bridge, PadUI, apps): MIT, see `LICENSE`. It's permissive on purpose,
  so OldUnreal or anyone else can reuse any of it, including in closed-source builds.

Unreal and Unreal Tournament are trademarks of Epic Games, Inc. This is an unofficial fan project, not affiliated
with or endorsed by Epic Games or OldUnreal.

## Thanks

All credit for keeping Unreal and Unreal Tournament alive goes to the **OldUnreal** team and contributors. These
apps build on their 469 and 227 patches, and the games run on their code. Please support them:
**https://www.oldunreal.com/phpBB3/donate**.
