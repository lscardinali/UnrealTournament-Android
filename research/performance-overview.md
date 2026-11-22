# Performance overview (native build, RP6)

Retroid Pocket 6: Snapdragon 8 Gen 2 (Adreno 740), 1920×1080 at 120 Hz. The native build runs UT's own 469f Linux
arm64 binaries inside the app process (utloader), with Android SDL3 and Qualcomm's Vulkan driver straight to the
window. No X server, no glibc process, no copies of the frame.

## Measured (before this pass)

DM-Deck16][, bots fighting, 1920×1080, from `native/tools/load_compare.py` and the overlay:

| | Native build | X11 build (backup) |
|---|---|---|
| Frame rate on screen | 119.6–120 fps, 100% of refreshes on time | 120 fps with the 5.5 ms pacer |
| CPU, whole app | 60.5% of one core | 143.4% (game 51 + X server 62 + app 30) |
| CPU clocks (cluster0 / cluster3) | 1252 / 1248 MHz | 1317 / 1793 MHz |
| GPU busy at 401 MHz | 41% | 54% |
| GPU temperature | 41 °C | 46 °C |
| Power | baseline | about 1.4 W more |

The GPU sits at its lowest clock (401 MHz) at 41% busy, and the CPU's big cores stay near idle clocks. At 120 fps
the game is nowhere near a limit: the frame time is set by the panel's vsync.

Setup: a full SD card install took about 80 s on Wi-Fi (CD image downloaded at about 31 MB/s). The game started
from it at about 119 fps.

## What this pass changed (built, not yet measured on the device)

1. **Buffered package reads.** The engine reads its packages through stdio (`fopen`/`fread` in Core and Engine).
   Android's C library gives a FILE a 1 KB buffer (glibc uses the file system block size, usually 4 KB+), so every
   1 KB was a separate `read` syscall. On the SD card each one is also a round trip through Android's FUSE daemon.
   `shim.c` now wraps `fopen` and gives read-only files a 64 KB buffer: about 64× fewer syscalls when loading maps,
   textures and sounds. This affects load times only; once a map is loaded everything is in memory.
2. **Gyro sensor only when needed.** It ran at 200 Hz the whole time the game was open. Now it runs only in a
   match with gyro aiming on (stopped in menus and when switched off in Handheld Settings).
3. **Performance overlay hidden by default.** Its sampling (proc stats, battery, GPU every 500 ms) and drawing now
   only run when it is shown: Mod > Handheld Settings > Performance overlay, or L3 + R3.
4. **Frame rate limit.** Mod > Handheld Settings > Frame rate limit: Display maximum / 90 / 60 fps. It switches the
   panel's display mode, so the GPU, the CPU and the panel itself all do less. Expect the biggest battery gain of
   anything here at 60 fps (roughly half the GPU and game-thread work). Look speed and gyro are frame-rate
   independent, so aiming feels the same.

## SD card

- The card is exFAT behind Android's FUSE layer (`/storage/<id>`, mounted noexec), so program files must stay on
  internal storage; only Maps, Textures, Sounds and Music go to the card (about 670 MB), linked into `files/game`.
- FUSE costs per request rather than per byte, which is why the 64 KB read buffer matters most there.
- The card only affects loading a map. During play nothing is read from it (UT loads a level's packages up front),
  so frame rate and power in a match are the same as internal storage.
- To measure (when the device is free): `python -I native/tools/storage_bench.py`. It reports dd throughput with
  1 KB vs 64 KB reads on the installed Maps folder, and the time from launch to "level up for play".

## Where the remaining cost is

- **GPU:** ~41% busy at the lowest clock. The renderer (VulkanDrv) has headroom for higher quality settings
  (anisotropic filtering, MSAA) if wanted, or a 60 fps limit for battery.
- **CPU:** one game thread (~50% of a core at 120 fps) plus SDL, audio (OpenAL, OpenSL ES) and the app's small
  per-frame overlay poll. UT99's engine is single-threaded; nothing to gain from more cores.
- **Not worth doing:** the X11 build's pacer and copies are irrelevant to the native build, and Turnip (Mesa)
  measured worse than Qualcomm's driver here.

## To verify once the device is free

- Load times internal vs SD with `storage_bench.py`, before/after the read buffer (an older APK for "before").
- Power at 120 / 90 / 60 fps with `load_compare.py`-style sampling (the overlay off).
- Handheld Settings: each control, and that the overlay, curve, boost and frame rate survive a restart
  (files/handheld.cfg).
- Stick curve 1.5 and turn boost: feel. Turn speed at full deflection is unchanged (axis at half range, binding
  speed doubled), so only mid-stick aiming is slower.
