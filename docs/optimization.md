# Android optimization candidates (2026-11-21)

Measured with the in-game PerfOverlay (L3+R3) once installed. Ordered by expected payoff ÷ effort.

| # | Change | Why | Effort | Status |
|---|---|---|---|---|
| 1 | **Frame cap at 60** (optional battery saver) | The panel runs at 120 Hz and the game stays at ~120 fps, so this is not a missing limit, only a power option. Look speed is now frame-rate independent (SDL bridge scales the right stick by real frame time), so a cap no longer changes aiming. Find the limiter VulkanDrv honours (UMenu exposes `FrameRateLimit`; test on the device), else cap in utshim. | small | todo |
| 2 | **Try without `TU_DEBUG=sysmem`** | Forces Turnip to render in system memory instead of Adreno's on-chip tile memory (GMEM). Winlator sets it for the 740 because of D3D titles; UT's simple renderer may be fine on GMEM. | trivial | **done 2026-11-22**: 112–117 → 118–121 fps, avg 8.9 → 8.4 ms, worst 10–13 → 10–11 ms, no artifacts seen. Re-enable per launch with the `env` extra (`TU_DEBUG=sysmem`). |
| 3 | **Render below native** (e.g. 1280×720 or 1600×900, scaled by lorie), keeping Full HD as an option | Cuts the per-frame copy and GPU fill by 2.25× / 1.44×. The screen is 6": 720p–900p looks close to native. | small (X screen size + UT res in GameConfig) | todo |
| 4 | **Zero-copy present** (Mesa exports AHardwareBuffers → lorie DRI3 modifier 1255) | Removes CPU copies: Mesa readback → X socket (`xcb_put_image`, no MIT-SHM in this Turnip) → lorie buffer → GL composite. Likely the biggest CPU/power win. | large: needs a Linux build env (WSL/Docker) to build Mesa | blocked |
| 5 | **ADPF performance hints** (`PerformanceHintManager`, API 31+) | Report the game's frame time target so the CPU governor ramps the prime core for the render thread instead of guessing; also helps power. Needs the game thread ids (from /proc) and the frame time (from the frame counter). | medium | todo |
| 6 | **Remove debug logging** | SDL `input=debug` heartbeat, InputBridge per-key / per-second motion logs, `send B` logs. Small, but runs every frame/event. | trivial | todo |
| 7 | **Refresh rate / frame pacing** | Presentation isn't synced to the display: sw WSI pushes frames as fast as they come and lorie redraws on damage, so frames that miss a refresh show unevenly (felt as jitter). Pace presents to the panel's vsync (Choreographer timing → utshim), or cap to a steady rate. | medium | **done 2026-11-22**: VsyncWriter publishes Choreographer vsync + period in the frames file; utshim `pace_frame` holds each frame until 5.5 ms before its vsync slot. On screen (SurfaceFlinger present times, `native/tools/sf_latency.py`): lead 4 ms → 107 fps, 11% repeated frames; pacing off → 117 fps, 2.6%; **5.5 ms → 119.7 fps, 0%**; 7 ms → 0.4%. Knobs (`env` extra): `UTANDROID_PACE_FPS` (0 = off, 60, 120), `UTANDROID_PACE_LEAD_US`. |
| 8 | **CPU placement** | Check with the overlay whether the game's main thread lands on the prime core; if not, consider `sched_setaffinity` for the render thread via utshim. Usually the scheduler does fine; only if numbers say so. | small | measure first |
| 9 | **Retroid performance mode** | RP6 firmware has its own performance profiles (Settings → Handheld). Document which one we test under; numbers aren't comparable otherwise. | none | note |
| 10 | **Release build** (R8 minified app, stripped native) | The Kotlin side is idle most of the time; small effect, but no reason to ship debug. | trivial | todo |

Not worth it now: the in-process ELF loader (architecture-options.md option B): native Vulkan + zero-copy, but 6–9 weeks
and high risk; revisit only if #4 doesn't get frame cost low enough.

## Measured frame budget (2026-11-22, DM-Deck16][, 1920×1080, panel 120 Hz)

utshim logs once per second (SDL heartbeat, `UTAndroid frames:` lines in files/logs/ut.log): fps, average and worst
frame interval, and time spent inside xcb_put_image.

| Setting | fps | avg frame | worst frame | xcb_put_image |
|---|---|---|---|---|
| `TU_DEBUG=sysmem` | 112–117 | 8.7–9.0 ms | 10.3–13.1 ms | ~1.9 ms |
| GMEM (default now) | 117–121 | 8.3–8.5 ms | 9.6–11.4 ms | ~2.2 ms |

The budget for 120 Hz is 8.33 ms. CPU: game ~47% of a core, X server ~31%, app ~27%; GPU ~53% busy at 401 MHz.
The remaining jitter is frames that miss a refresh, shown unevenly because presentation isn't synced to the display.

Dev knob: `adb shell am start -n com.lscardinali.unrealtournament/.MainActivity --es args '<map/url>' --es env 'K=V,K2='`
(an empty value removes a default env var).
