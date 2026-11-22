# Unreal Gold for Android

Unreal (1998) and Return to Na Pali on Android handhelds, running the [OldUnreal](https://www.oldunreal.com/) 227
community patch's own **Linux arm64** game binaries natively inside the app, with the same native glue as
[Unreal Tournament for Android](README.md). No emulation, no X server, no Linux container. Built and tuned on the
Retroid Pocket 6 (Snapdragon 8 Gen 2), where it runs at 120 fps at 1920×1080.

> **This port exists because of [OldUnreal](https://www.oldunreal.com/).** The 227 patch, its native Linux arm64
> build, the XOpenGL renderer, the installers and the download mirrors are their work, done for free over many
> years. This project is only a thin layer that brings their build to Android handhelds. If you enjoy it, please
> **[donate to OldUnreal](https://www.oldunreal.com/phpBB3/donate)**.

**The app contains no game files.** On first launch it downloads Unreal Gold from OldUnreal's servers and the latest
227 patch from OldUnreal's GitHub, the same way OldUnreal's official Linux installer does.

## Features

- Native: Unreal 227's Linux arm64 build loaded in-process (the UT port's ELF loader and glibc-to-bionic shim),
  Android SDL3 for input, window and audio, OpenAL Soft.
- Renderer: 227's XOpenGL in OpenGL ES mode on the device's own GLES 3.2 driver. The loader adapts XOpenGL's GLES
  shaders for Adreno on the fly. (227 has no Vulkan renderer.)
- First-run setup in an Unreal Gold styled screen: downloads the Unreal Gold CD image (680 MB) and the 227 patch
  (130 MB), checks their SHA-256, installs to internal storage or the SD card.
- Controller first: the same PadUI as UT, built for 227. Every menu, dialog, list, tab, text field and key binding
  works with the D-pad; on-screen pad keyboard; the touchscreen as an optional touchpad cursor.
- Gyro aiming, look stick curve and turn boost, performance overlay and frame-rate limit (Mod → Handheld Settings).

## Requirements

Android 9 or newer, arm64-v8a, OpenGL ES 3.2; a controller; about 1.7 GB free during setup (about 850 MB once
installed). Tested only on the Retroid Pocket 6 so far: reports from other devices are very welcome.

## Installing

1. Install `app-unreal-release.apk`. It's a separate app (`com.lscardinali.unreal`) and can sit next to the UT one.
2. Open it, pick internal storage or the SD card, accept the Epic Games Terms of Service, install.
3. Play. Start opens the menus.

Reinstall or move the game: `adb shell am start -n com.lscardinali.unreal/com.lscardinali.unrealtournament.MainActivity --ez setup true`
(saves are kept).

## Controls

In the game (defaults; change them in Options → Preferences → Controls with the pad, or Mod → Handheld Settings):

| Button | Action |
|---|---|
| Left stick / right stick | Move / look (turn boost at full sideways) |
| Gyro | Fine aim |
| R2 / L2 | Fire / alternate fire |
| A / B | Jump / crouch |
| X | Use the selected item |
| Y | Best weapon |
| R1 / L1 | Next / previous weapon |
| D-pad left / right | Previous / next item |
| D-pad up / down | Quick save / throw weapon |
| Select | Translator |
| L3 / R3 | Walk / center view |
| Start | Menu |

Menus: as in UT (D-pad moves the yellow focus box, A selects, B goes back, R1/L1 switch tabs).

## Building

Same tree and scripts as UT (see [README.md](README.md)), plus:

- PadUI for 227: `bash native/padui/build-padui-227.sh`, using the `UCC.exe` of a Windows Unreal 227 install
  (`UNREAL=path`, default `Unreal/` in the repo root). The output goes to `Android/app/src/unreal/assets/padui-227/`
  (committed).
- The app: `cd Android && ./gradlew assembleUnrealRelease`.

## How it differs from the UT app

- `setup/Product.kt` (`Product.UNREAL`): Unreal Gold ISO (uppercase CD folders, no compressed maps), the
  `OldUnreal/Unreal-testing` patch (one Linux tarball; its x86, x86-64 and arm64 builds; only arm64 is installed).
- `runtime/UnrealConfig.kt`: 227 keeps its ini files next to the executable (`SystemARM64/UnrealLinux.ini`,
  `User.ini`); XOpenGL ES settings, vsync on, PadUI root window, pad bindings, GUI scale.
- Loader/shim additions for 227: the executable path via `AT_EXECFN`/`realpath`, glibc's `FE_DFL_ENV`, one Android
  window shared by SDLDrv and XOpenGL, the GLSL rewrite, a frozen EGL framebuffer config (so the surface recreated
  after the app returns from the background matches the GL context), frame counting on `SDL_GL_SwapWindow`, and
  227's own clipboard (the Handheld Settings channel).

Details and measurements: [docs/unreal-227-spike.md](docs/unreal-227-spike.md).

## Legal

Unreal and Unreal Gold are © Epic Games. This project ships none of their files; the Epic Games Terms of Service
(https://legal.epicgames.com/en-US/epicgames/tos) apply to the game, and the setup asks you to accept them. The
OldUnreal 227 patch is downloaded from https://github.com/OldUnreal/Unreal-testing (see its licence, installed with
the game). Shipped libraries and this project's licence: as in [README.md](README.md). Unofficial fan project, not
affiliated with or endorsed by Epic Games or OldUnreal.
