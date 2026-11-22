# Docs

Design notes and measurements written while building the port, mostly as working notes: they record what was
tried, measured and decided, and why. Dates are when each was written.

## Current

| Note | What it covers |
|---|---|
| [native-port-plan.md](native-port-plan.md) | How the game runs inside the app: the ELF loader, the glibc-to-bionic shim, TLS, signals, the milestones (M0-M5) and what each one fixed. Start here. |
| [performance-overview.md](performance-overview.md) | Frame rate, CPU, GPU and power on the RP6; what the optimisation pass changed; SD card loading. |
| [optimization.md](optimization.md) | Optimisation candidates and their status. |
| [setup.md](setup.md) | The first-run setup: OldUnreal sources, the CD image reader, `.uz` map decompression, SD card layout. |
| [small-screen-ui.md](small-screen-ui.md) | Making UWindow usable with a pad on a 6" screen: GUI scale, PadUI (focus box, lists, tabs, key binding, pad keyboard, server browser), the touchpad cursor. |
| [controller-input.md](controller-input.md) | How UT 469 reads joysticks through SDLDrv, and the virtual pad layout. |
| [controller-defaults.md](controller-defaults.md) | The default bindings and handheld-friendly settings, checked against the game's scripts. |
| [aim-assist.md](aim-assist.md) | UT's built-in auto-aim, gyro aiming (implemented, tuning), stick response. |
| [device-findings.md](device-findings.md) | Retroid Pocket 6 hardware and software facts relevant to the port. |
| [unreal-227-spike.md](unreal-227-spike.md) | The Unreal Gold app: Unreal 227 on the same glue, XOpenGL on GLES, the shim additions, PadUI for 227, setup, device tests. |
| [symbol-imports/](symbol-imports/) | Every glibc, C++ and SDL symbol the game's binaries import, per module: what the shim has to provide. |

## History

Notes from earlier approaches, kept for reference. They describe the first working version, which ran the game as a
separate glibc process behind an X server (Termux:X11). That build has since been removed; the native in-process
build replaced it.

| Note | What it covers |
|---|---|
| [history/PLAN.md](history/PLAN.md) | The original architecture plan. |
| [history/architecture-options.md](history/architecture-options.md) | The options weighed at the start (container, X server, native). |
| [history/x11-integration.md](history/x11-integration.md) | The Termux:X11 integration of the first build. |
| [history/pocket-foundry.md](history/pocket-foundry.md) | What was learnt from Pocket Foundry (glibc programs and Turnip on Android). |
| [history/winlator-gamenative.md](history/winlator-gamenative.md) | What could be reused from Winlator and GameNative. |
| [history/wowforeverywhere.md](history/wowforeverywhere.md) | What was learnt from WoWForeverywhere. |
