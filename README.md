# Unreal Tournament for Android

Unreal Tournament (1999, Game of the Year Edition) on Android handhelds, running the
[OldUnreal](https://www.oldunreal.com/) 469 community patch's own **Linux arm64** game binaries natively inside the
app. No emulation, no X server, no Linux container. Built and tuned on the Retroid Pocket 6 (Snapdragon 8 Gen 2),
where it runs at a locked 120 fps at 1920×1080.

**The app contains no game files.** On first launch it downloads the game from OldUnreal's servers, the same way
OldUnreal's official installers do.

## Features

- Native performance: UT's Linux arm64 build loaded in-process by a small ELF loader, Android SDL3 for input,
  window and audio, and the phone's own Vulkan driver.
- First-run setup: downloads UT GOTY, Bonus Pack 4 and the latest 469 patch, installs to internal storage or the
  SD card, and checks every download against its SHA-256.
- Controller first:
  - every menu, dialog, list, tab, text field and key binding works with the D-pad;
  - an on-screen pad keyboard for typing;
  - the touchscreen works as an optional touchpad for the mouse cursor.
- Gyro aiming, a look stick response curve, turn boost, and optional auto-aim for offline games.
- Handheld Settings in the game's menus: gyro, stick response, performance overlay, and a 60 fps limit for battery
  life.
- Online play: server browser, joining and hosting work as on PC.

## Requirements

- Android 9 or newer, 64-bit ARM (arm64-v8a), Vulkan 1.1.
- A controller: built in (handhelds) or connected. Touch only drives the menu cursor.
- About 1.6 GB free during setup (about 800 MB once installed).

## Installing

1. Install the APK (`app-release.apk`) from the releases page.
2. Open it. The setup screen explains what it will download and asks where to install. With an SD card you can put
   maps, textures, sounds and music on the card; the program files (about 180 MB) always stay on internal storage.
3. Accept the Epic Games Terms of Service and install. It takes a minute or two on a fast connection.
4. Play. Start opens the menus.

To reinstall or move the game later: `adb shell am start -n com.lscardinali.unrealtournament/.MainActivity --ez setup true`.
Your saves are kept.

## Controls

In a match (defaults; change them in Options → Preferences → Controls, or in Mod → Handheld Settings for the gyro):

| Button | Action |
|---|---|
| Left stick | Move / strafe |
| Right stick | Look (hold fully sideways for a turn boost) |
| Gyro | Fine aim (turn the handheld) |
| R2 / L2 | Fire / alternate fire |
| A / B | Jump / crouch |
| X / Y | Translocator / throw weapon |
| R1 / L1 | Next / previous weapon |
| D-pad | Flak cannon, shock rifle, sniper rifle, rocket launcher |
| L3 / R3 | Walk / feign death |
| Select | Scores |
| Start | Menu |
| L3 + R3 | Show / hide the performance overlay |

In the menus:

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

The build runs on Windows with Git Bash, Android Studio (its JDK and the Android SDK/NDK) and Python 3.

1. Native libraries (each script clones its upstream source at a pinned version, see `native/SOURCES.md`):
   ```
   bash native/sdl/build-sdl-android.sh        # SDL3 + SDL3_ttf with the UTAndroid controller bridge
   bash native/openal/build-openal-android.sh  # OpenAL Soft (OpenSL ES backend)
   bash native/utloader/build.sh               # libutloader.so: ELF loader + glibc-to-bionic shim
   ```
   Copy the resulting `.so` files to `Android/app/src/main/jniLibs/arm64-v8a/` (committed builds are already there).
2. Runtime libraries (`libgcc_s.so.1`, `libstdc++.so.6`, from Debian): `python native/runtime/fetch_debs.py`, then
   copy them to `Android/app/src/main/assets/runtime/` (already committed).
3. PadUI (the controller UI, UnrealScript): `bash native/padui/build-padui.sh`. Needs a Windows install of UT with
   the 469 patch in `UnrealTournament/` (for `UCC.exe`). It writes `PadUI.u`/`PadUI.int` into the app's assets
   (already committed).
4. The app: `cd Android && ./gradlew assembleRelease`. Release signing reads `Android/keystore.properties`
   (`storeFile`, `storePassword`, `keyAlias`, `keyPassword`); without it the release build is signed with the debug
   key.

Maintenance: `./gradlew assembleRelease -PdebuggableRelease` builds the same release but debuggable, so
`adb shell run-as` can reach an installed copy (for example to back up or restore saves). Installing the normal
build over it keeps the data.

PC unit tests (the CD image reader and map decompressor against real data, when present):
`./gradlew :app:testDebugUnitTest`.

## How it works

- `native/utloader`: an ELF loader that maps UT's glibc-linked binaries into the Android process and resolves their
  libc calls to bionic through a shim, including TLS, signals, locale and dlopen. The game runs on its own thread.
  SDL3 and OpenAL calls go to the app's Android builds; Vulkan goes to the system driver.
- `native/sdl/utandroid`: a virtual controller in SDL that shapes the real pad (dead zones, look curve, frame-rate
  independent look, gyro axes) and turns it into menu keys.
- `native/padui`: an UnrealScript root window that gives every UWindow dialog a D-pad focus box, the pad keyboard
  and the Handheld Settings window.
- `Android/app`: the setup (download, CD image reader, `.uz` map decompressor, installer), the game activity,
  overlays and ini defaults.
- `research/`: design notes and measurements (performance, controller, setup, small-screen UI, aim assist).

## Legal and licences

- **Unreal Tournament** is © Epic Games. This project ships none of its files. The setup downloads the GOTY CD
  image and Bonus Pack 4 from OldUnreal's mirrors (archive.org as a fallback). The Epic Games Terms of Service
  (https://legal.epicgames.com/en-US/epicgames/tos) apply to the use and distribution of the game, and the setup
  asks you to accept them, as OldUnreal's installers do.
- **OldUnreal 469 patch** (downloaded from https://github.com/OldUnreal/UnrealTournamentPatches): see its
  `LICENSE.md`, installed with the game.
- Shipped in the APK, with their licences:
  - SDL3 and SDL3_ttf: zlib licence. SDL3_ttf is built with FreeType (FreeType Licence) and HarfBuzz (MIT).
  - OpenAL Soft: LGPL-2.1, built unmodified from https://github.com/kcat/openal-soft (1.24.3) as a separate shared
    library (`native/openal/build-openal-android.sh` rebuilds it).
  - libgcc_s and libstdc++ (Debian): GPL-3.0 with the GCC Runtime Library Exception.
  - Apache Commons Compress and Commons IO/Lang: Apache-2.0. XZ for Java: 0BSD. AndroidX, Jetpack Compose and
    Kotlin: Apache-2.0.
- This project's own code (loader, shim, SDL bridge, PadUI, app): see `LICENSE`.

Unreal and Unreal Tournament are trademarks of Epic Games, Inc. This is an unofficial fan project, not affiliated
with or endorsed by Epic Games or OldUnreal.
