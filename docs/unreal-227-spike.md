# Spike: original Unreal (OldUnreal 227k) on Android with the UT port's native glue

Result (2026-11-22, RP6, Adreno 740): **Unreal 227k runs natively in-process and renders through XOpenGL on
OpenGL ES 3.2.** Menus and the first campaign map (Vortex2) render correctly (textures, lightmaps, HUD), with OpenAL
audio. No X11, no Termux, no Mesa. App: `com.lscardinali.unreal` ("Unreal (spike)"), side by side with UT.

## What it reuses unchanged

utloader (ELF loader, TLS, glibc->bionic shim, signals, crash log), Android SDL3 + the SDL_utandroid bridge
(controller, menu keys, touchpad cursor), OpenAL Soft (OpenSL ES), NativeGameActivity/SDLActivity, libgcc_s and
libstdc++ from the APK assets. 227k's Linux arm64 build uses the same SDL3 / SDL3_ttf / OpenAL / sndfile / mpg123 /
lame / xmp set as UT 469f.

## Game files (as OldUnreal's FullGameInstallers/Linux/install-unreal.sh)

- `UNREAL_GOLD.ISO`, 676734976 B, sha256 7e360d0cc9e5533f38859819fd3cbfea7c475ecd428f9f433b5f8e1d5742cbca, from
  files/files2/files3.oldunreal.net, backup archive.org/download/totallyunreal. Folders are UPPERCASE on the CD:
  copy HELP/MAPS/MUSIC/SOUNDS/SYSTEM/TEXTURES to Help/Maps/... minus System/Unreal.ini, User.ini, *.bat/*.dll/*.exe
  and translations. No .uz maps (no decompression step).
- Patch: GitHub OldUnreal/Unreal-testing latest (v227k_15) `OldUnreal-UnrealPatch227k-Linux.tar.bz2`, 131414620 B,
  sha256 b04eba6d5c885c98bd9da8279840b15cd8fa06ed2b750235e7ad7aa26ef3f7a1 (one Linux tarball for all architectures;
  arm64 is `SystemARM64/unreal-bin-arm64`). Symlinks: only libSDL3(.ttf).so.0.
- 227 looks for `DefaultLinux.ini` / `DefUser.ini` in the executable's folder (appChdirSystem -> SystemARM64), but
  they ship in System/: copy them into SystemARM64. It writes `UnrealLinux.ini`, `User.ini` and
  `unreal-bin-arm64.log` there too ("Not using preference directory").
- Installed size ~850 MB (without System64).

## Loader/shim changes (native/utloader/shim.c, main_android.c)

- `UTL_EXE` / `UTL_SYSTEM_DIR` / `UTL_HOME` env overrides (the activity sets them for `game=unreal`).
- `__getauxval(AT_EXECFN)` and `realpath("/proc/self/exe")` answer the game's path (227's base directory logic).
- `fesetenv`/`feupdateenv`: glibc's FE_DFL_ENV is the pointer -1 (PhysX calls it), bionic's is an object.
- `SDL_CreateWindow`: XOpenGLDrv creates its own window while SDLDrv's is open; Android SDL allows one, so the
  existing window is handed back and the matching destroy ignored.
- GLSL rewrite in `glShaderSource` (via `SDL_GL_GetProcAddress`): `#version 310 es` -> `320 es` (I/O blocks are
  core), drop `#extension GL_OES_shader_io_blocks`, drop the `out float gl_ClipDistance[N]` redeclaration, and
  redirect `gl_ClipDistance` writes to a plain array. The last one was the silent link failure: GLES only allows a
  variable index into an unsized gl_ClipDistance after a sized redeclaration, which Adreno rejects. Consequence:
  no hardware user clip planes (check mirrors / warp zones). `UTL_KEEP_CLIPDISTANCE` turns the redirect off.
- Debug: failed `glLinkProgram` logs the info log (Adreno's is empty); `--ez shaderdump true` dumps every shader to
  files/logs/shaders.
- Unresolved at load: only `pthread_setaffinity_np` (trap, never called so far).

## ini (SystemARM64/DefaultLinux.ini before first run)

`GameRenderDevice=XOpenGLDrv.XOpenGLRenderDevice` (also WindowedRenderDevice), `[XOpenGLDrv.XOpenGLRenderDevice]`
`OpenGLVersion=ES`, `UseShaderDrawParameters=False`, `UseHWClipping=False`, `UseBindlessTextures=False`;
`[SDLDrv.SDLClient]` 1920x1080 fullscreen, `UseJoystick=True`; `FirstRun=227`.

## Performance (Vortex2 start, 1920x1080, not tuned)

- 4x MSAA (default): 70 fps on screen, 30% of frames on time, 70% held 2 refreshes.
- MSAA off: 89 fps, GPU 11% busy at 401 MHz: CPU / frame pacing bound, not GPU. Needs the UT treatment (pacing,
  frame counter for GL swaps, perf overlay; the overlay isn't wired for the Unreal path yet).

## Still to do for a real Unreal app

- Own folder/app module in the repo (e.g. `unreal/`), sharing `native/`; package `com.lscardinali.unreal`.
- Setup screen: reuse IsoReader/Downloader/GameInstaller with the Unreal Gold ISO + 227k patch (no .uz; casing
  copy; DefaultLinux.ini into SystemARM64), SD card layout as UT.
- Controller defaults and PadUI for 227's UMenu (verify it compiles against 227's UWindow), gyro, ini defaults.
- Frame pacing; check clip-plane effects; test Return to Na Pali, saves, the intro flyby (UPack/Unreal.unr).

## Compatibility of the UT mods (desk check, 2026-11-22; nothing run on the device)

227's UnrealScript exported with the 227 Windows `UCC.exe` (Unreal/SYSTEM, user's local install) to
`downloads/src227` (UWindow, UMenu, UBrowser, UnrealShare, UnrealI; Engine/Core don't export).

| Mod | 227 status |
|---|---|
| SDL bridge (pad, menu keys, touchpad cursor, rumble) | Engine-agnostic: works as is (seen in the spike). |
| PadUI (`PadRootWindow`, focus box, lists, tabs, key binding, pad keyboard) | **Compiles against 227** with 2 source changes: `UBrowserTypesGrid` doesn't exist in 227's UBrowser (guard/drop the game-type grid), and 227's `InsertUnicode` takes an int (use `InsertText(string)`, present in both). Must be built per game (separate `PadUI.u` from 227's UCC); UT's binary won't load in 227. |
| GUI scale default | 227's root window has `var config float GUIScale` (no `ConfiguredGUIScale`/`AutoGUIScale`): write `GUIScale=3.0` instead. |
| Mod menu item (Handheld Settings / gyro) | `UMenuModMenuItem` + .int registration exist in 227: OK. |
| Gyro, look-speed scaling, FPS overlay | **Broken until fixed:** they key off the loader's frame counter, which only counts `vkQueuePresentKHR`. XOpenGL presents with `SDL_GL_SwapWindow` -> hook it to bump the same counter (gyro is drained once per counted frame, so without it the gyro does nothing and look speed is frame-rate dependent). |
| GameConfig ini defaults | UT-specific (UnrealTournament.ini/User.ini under ~/.utpg, Botpack, VulkanDrv). 227 needs its own set for `SystemARM64/UnrealLinux.ini` + `User.ini` (XOpenGL ES settings, SDLDrv, joystick bindings, PlayerPawn prefs: verify key names against 227's Engine). |
| Setup (IsoReader, Downloader, GameInstaller) | Reusable: Unreal Gold ISO is ISO 9660 (check Joliet vs uppercase names), no .uz, patch is one tar.bz2. |

## The old (classic) Unreal menu

227 still has the classic menu system (`UnrealShare.UnrealMenu` family: main, new game, load/save, options, video,
audio, keyboard, player, multiplayer, bots, mutators). Which UI Escape opens depends on the console class:

- `Console=UBrowser.UBrowserConsole` (the user's PC install): Escape -> `Super(Console).KeyEvent` -> classic menu;
  the server browser (UBrowser) is still a UWindow (`[UBrowser.UBrowserConsole] RootWindow=UBrowser.UBrowserRootWindow`,
  which extends UWindowRootWindow, not UMenuRootWindow).
- `Console=UMenu.UnrealConsole` (227's DefaultLinux.ini): Escape -> UWindow menu bar (what the spike showed).

Classic menus are driven only by `Engine.Menu.MenuProcessInput`: IK_Up/IK_Down move, IK_Left/IK_Right change a
value, IK_Enter selects, IK_Escape backs out (plus the Y/N letter). No mouse at all, so it maps 1:1 to D-pad/A/B.

Catch: our SDL bridge only turns the pad into arrows/Enter/Escape in "menu mode" (relative mouse off). The classic
menu is drawn by the console's `Menuing` state while the mouse stays relative, so the pad arrives as Joy buttons
(D-pad = Joy13-16 = weapon switches, A = Joy1) and the menu ignores them. Start already works (always Escape).
Cleanest fix, engine-side: a small console subclass (e.g. `PadUI.PadClassicConsole extends UBrowser.UBrowserConsole`)
whose `Menuing`/`EndMenuing` states remap Joy13-16 -> arrows, Joy1 -> Enter, Joy2 -> Escape before calling the parent;
`KeyMenuing` (key binding) already receives raw Joy keys, so binding pad buttons works natively. Text fields in
classic menus (player name, server address) use the `MenuTyping` state: needs a classic version of the pad keyboard.
The UBrowser server browser would need PadUI on a UBrowserRootWindow subclass.

Trade-off: classic menu = native controller navigation and the original look, but fewer options (the UWindow
menus expose 227's newer settings, e.g. renderer, mutators UI, browser). Both can coexist: classic on Escape, UWindow
reachable for advanced settings.

## Implemented for the Unreal app (2026-11-22, built and PC-tested only; device tests pending)

Decision: UWindow menus + PadUI (classic menu maybe later as an option).

1. **GL frame counter**: shim interposes `SDL_GL_SwapWindow` and bumps the same counters as `vkQueuePresentKHR`
   (look-axis scaling, gyro draining, FPS overlay).
2. **PadUI for 227**: same source; UT-only lines tagged `//#UT`, 227-only lines `//#227 `.
   `native/padui/build-padui-227.sh` builds with the 227 UCC (default `../../Unreal`) -> `assets/padui-227/`.
   0 errors, 0 warnings. `InsertUnicode` -> `InsertText` (both games).
3. **UnrealConfig.kt**: SystemARM64/UnrealLinux.ini + User.ini. Every launch: XOpenGL ES settings, SDLDrv fullscreen
   at panel res + joystick, `Console=UMenu.UnrealConsole`, `[UMenu.UnrealConsole] RootWindow=PadUI.PadRootWindow`,
   FirstRun=227. Once: GUIScale=3 (227 key), UseAA=False, pad bindings (A Jump, B Duck, X InventoryActivate,
   Y SwitchToBestWeapon, L1/R1 Prev/NextWeapon, L2/R2 AltFire/Fire, Select ActivateTranslator, L3 Walking,
   R3 CenterView, D-pad up QuickSave / down ThrowWeapon / left-right InventoryPrevious/Next, sticks as UT, gyro JoyU/V
   16000), PlayerPawn prefs (no dodge, MyAutoAim 0.98, no snap/look-up-stairs). Installs PadUI 227 from assets.
   NativeGameActivity's Unreal path now has the overlays, gyro and touchpad cursor too.
4. **Setup for Unreal**: `setup/Product.kt` (BuildConfig.GAME = "unreal" in this app) drives GameInstall (folder
   `unreal`, marker, SD layout), InstallTask (no bonus-pack step), the setup screen text ("UNREAL / GOLD") and
   downloads (Unreal Gold ISO from OldUnreal mirrors + archive.org; latest `-linux` asset of OldUnreal/Unreal-testing,
   fallback v227k_15). `GameInstaller.Game.UNREAL`: uppercase CD folders -> Title case, no .uz, skips System64 and the
   x86 binaries in the patch's System/, copies DefaultLinux.ini/DefUser.ini into SystemARM64.
   PC test (SetupDataTest.unrealGoldWith227): 59 maps, 132 MB internal + 678 MB data.

### Test plan (on device, step by step)
1. Launch the existing (hand-pushed) install: config applied? PadUI loaded (focus box), GUI scale 3, FPS overlay
   counts frames (GL hook), look speed, gyro.
2. Controller in UWindow menus: menu bar, dialogs, key binding, pad keyboard, Handheld Settings.
3. Bindings in game (inventory, translator, quick save).
4. Setup: `--ez setup true` reinstall to SD card from OldUnreal, then play.

## Device test results (2026-11-22)

1. Config applied (UnrealLinux.ini/User.ini), PadUI 227 loaded as root window, focus box on menu items.
   GUI scale: the installed patch is **227k** (WIP), which has UT 469's AutoGUIScale/ConfiguredGUIScale (Windows
   release 227j uses GUIScale with 1x/2x only): both written, 3x shows. GL frame counter works (frames file ticks).
2. Pad in UWindow: menu bar/pulldowns, Mod > Handheld Settings window opens and drives the app.
   Fix needed: 227 keeps its clipboard in Core (`appClipboardCopy`, wchar_t) instead of SDL's, so PadUI's
   "utandroid:" channel never reached the loader. The shim now preempts `appClipboardCopy` (new
   `utl_shim_preempt` hook in the loader's symbol resolution, ahead of the game modules). Performance overlay
   toggle works.
   Fix needed: XOpenGL's default `UseVSync=Adaptive` fails on Android and falls back to vsync off (~280 fps in menus,
   89 fps stuttering in game). `UseVSync=On`: Vortex2 **119.7 fps on screen, 100% of frames on time**, GPU 14%.
3. QuickSave (D-pad up) works ("Quick Saving", Save0.usa). Translator (Select): nothing visible at the Vortex2
   start (no message yet), inconclusive.
4. Fresh setup (`--ez setup true`, reinstall over the hand-pushed copy) to the SD card, driven by the pad:
   Unreal Gold ISO + latest 227 patch downloaded and installed in **~115 s**; Maps/Music/Sounds/Textures (~700 MB)
   on the card, symlinked; SystemARM64 has the default ini files; Save/ carried over; downloads removed.
   Play: the game starts from the new install (Return to Na Pali intro flyby), XOpenGL ES with VSync On,
   ~108 fps on screen during the flyby (89% of frames on time; Vortex2 earlier: 119.7 fps, 100%).

## After the rebase onto main (2026-11-22)

- One app module, two build flavors (`ut`, `unreal`): `com.lscardinali.unreal`, "Unreal Gold"; the 227 PadUI build
  lives in `src/unreal/assets/padui-227`.
- **Black screen after returning from the background**: SDL re-chooses the EGL config from the current GL
  attributes when Android recreates the surface; XOpenGL's own attributes (depth 24, buffer 32, sRGB off) arrived
  after SDLDrv's window existed, so the new surface's config no longer matched the context (`EGL_BAD_MATCH` every
  frame). The shim now ignores framebuffer-config attributes once a window exists. Verified: back from Home,
  Vortex2 renders at 120 fps, no EGL errors.
- **Fresh install had 1x GUI scale and stock (unusable) pad bindings**: the "defaults applied" version was kept in
  the app's preferences, which survive a game reinstall that replaces the ini files. It now lives in the ini
  (`[UTAndroid] DefaultsVersion`). Verified on device.
- **Key remapping did nothing**: 227's key-binding window cancels the wait on any key release, and the Enter that
  the bridge sends with A was released immediately. PadUI swallows key releases while a binding waits. Verified:
  Move Forward bound to X.
- Setup screen in Unreal Gold colours (bronze plates, brown background) in the unreal flavor; UT keeps gunmetal.
- Launch splash: black, without the app icon (both apps).
