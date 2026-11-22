# Small-screen and gamepad UI for UT99 469f on the Retroid Pocket 6

Scope: how to make UT's UWindow menus (UMenu/UTMenu) comfortable on a 6-inch 1920x1080 screen with only the built-in
pad. Today the port has Start = Escape, a stick-driven cursor overlay (A = left click, X = right click, B = Escape).

Evidence sources:
- the 469f release notes: `linux-arm64/ReleaseNotes.md`
- the shipped `linux-arm64/System/Default.ini` and `DefUser.ini`
- the UnrealScript source text embedded in `UWindow.u`, `UMenu.u`, `UTMenu.u` and `Botpack.u`, extracted with a
  Python printable-run scan (469 packages keep their `.uc` text)
- the Windows install's live ini, `UnrealTournament/System/UnrealTournament.ini`
- web searches

Marks: **[uscript]**, **[ini]**, **[relnotes]**, **[web]**, **[inferred]**.

---

## 1. What 469 already offers

### 1.1 GUI scale (the big, free win)

`UWindowRootWindow` (`UWindow.u`) **[uscript]**:

```
var float        GUIScale;             // effective scale
var config float ConfiguredGUIScale;
var config bool  AutoGUIScale;
var config string LookAndFeelClass;

function CalculateEffectiveGUIScale()
  if (AutoGUIScale) { DPI = class'Canvas'.static.GetDesktopDPI(); Scale = DPI*100/96;
                      round down to a multiple of 25, clamp 100..300;
                      Log("Calculated Effective GUI Scale:"@GUIScale); }
  else              { GUIScale = ConfiguredGUIScale; Log("Using Configured GUI Scale:"@GUIScale); }

function SetScale(float NewScale)
  // WinWidth = RealWidth / GUIScale; WinHeight = RealHeight / GUIScale; SetupFonts(); Resized();
```

- **How it works.** The whole UWindow desktop is laid out in virtual pixels of `RealSize / GUIScale`, and every draw
  call multiplies by `Root.GUIScale`.
- **Fonts.** `SetupFonts()` picks pre-rendered Tahoma sizes from `Textures/UWindowFonts.utx` for each scale step
  (1.0, 1.25, …, 3.0). For example, 2.0 uses `Tahoma20`/`TahomaB20`/`Tahoma30`/`TahomaB30`. Text stays sharp; it is
  not bitmap-stretched.
  - Steps of 0.25 are the only ones that get their own fonts. A value in between falls through to the default
    branch, which uses the 1.0 fonts.
- **Ini section.** The live root window is `UMenu.UMenuRootWindow` (`[UMenu.UnrealConsole] RootWindow=UMenu.UMenuRootWindow`
  in Default.ini, inherited by `UTMenu.UTConsole`). The settings are saved under that class's section. This is
  confirmed in the Windows install's `UnrealTournament.ini`:
  ```ini
  [UMenu.UMenuRootWindow]
  ConfiguredGUIScale=1.500000
  AutoGUIScale=False
  LookAndFeelClass=UMenu.UMenuBlueLookAndFeel
  ```
  The Lutris UT GOTY+469 installer writes the same keys **[web]**.
- **Why the device log shows 1.0.** Our device log line "Calculated Effective GUI Scale: 1.000000" means
  `AutoGUIScale` is True (the compiled default, since it is not in Default.ini). It also means `GetDesktopDPI()`
  returns about 96 on SDL/Android. So the automatic scale does nothing, and the UI is laid out on a 1920x1080 virtual
  desktop, which is why it is tiny.
- **In-game control.** Options > Preferences > Video (`UMenuVideoClientWindow` **[uscript]**) has:
  - `AutoGUIScalingCheck`, labelled "override GUI scaling"; it is checked when `AutoGUIScale` is off
  - `GUIScalingSlider`, with `SetRange(100, 300, 25)`

  `ScaleChanged()` calls `Root.SetScale(slider/100)` and then `Root.SaveConfig()`. There is no console command for
  it; use the ini, or `set UMenu.UMenuRootWindow ConfiguredGUIScale 2` (takes effect after the next `SetScale` or a
  restart) **[inferred]**.
- **Fitting the dialogs.** UMenu dialogs size themselves from `Root.WinWidth/WinHeight` with clamps, for example:
  - `SetSize(Min(480, Root.WinWidth-50), Min(360, Root.WinHeight-50))`
  - `if(Root.WinHeight < 400) …` fallbacks

  They are designed for at least 640x480 virtual. At 1920x1080:

| ConfiguredGUIScale | Virtual desktop | Verdict |
|---|---|---|
| 1.0 (now) | 1920x1080 | tiny |
| 1.5 | 1280x720 | still small on 6" |
| **2.0** | **960x540** | recommended default: everything fits, ~2x bigger |
| **2.25** | **853x480** | max "safe" (480 high = original design height) |
| 2.5 | 768x432 | dialogs hit their `<400` fallback; some get cramped |
| 3.0 | 640x360 | too short; Start Game / Player setup will clip |

**Do this first.** Add the following to `GameConfig.system()` and test 2.0 against 2.25 on the device:
```kotlin
"UMenu.UMenuRootWindow" to mapOf("AutoGUIScale" to "False", "ConfiguredGUIScale" to "2.0")
```

Release notes (469a-f) **[relnotes]**:
- "Added automatic scaling of the game menu…" (line ~1649)
- "UWindow now supports higher resolution textures for LookAndFeels…" (1651)
- "The in-game menu will now look better when scaled to non-default scales (#898)" (211)
- fixes for "override GUI scaling" in the voice menu and the start game menu (1149-1150)
- "You can now type into UWindowComboControls to filter the combo control list" (219)
- "mouse wheel or the Page Up/Down buttons to scroll in a pulldown menu" (#1581)

### 1.2 Cursor speed

`[UWindow.WindowConsole]` / `[UTMenu.UTConsole] MouseScale` (float, Options slider 40-500) scales only `IK_MouseX/Y`
axis deltas. Our cursor is the absolute SDL cursor (`MouseX = Viewport.WindowsMouseX / Root.GUIScale` when
`bWindowsMouseAvailable`), so `MouseScale` doesn't affect it.

**Note:** our overlay works in physical pixels. With GUIScale 2 every control is twice as many physical pixels, so
raise the overlay's cursor speed (or scale it by GUIScale) and draw the cursor at least 2x bigger.

### 1.3 In-game (HUD/scoreboard) font scaling

`Botpack.FontInfo` (`config(User)`) **[uscript]**:
```ini
[Botpack.FontInfo]          ; User.ini
bEnableInGameScaling=True    ; default False
InGameScalingFactor=1.5      ; slider 25..300 % in Options > Preferences > Video ("InGameFontScalingSlider")
```
- When enabled, HUD, scoreboard and message fonts are generated at runtime with `Canvas.CreateFont` from TTF.
  `Engine.Canvas DynamicFontUnicodeRange` in Default.ini points at this; `System/Fonts` and `libSDL3_ttf` ship in the
  package.
- This is **unverified on Android**. If it fails, the HUD falls back to the static `GetStatic*Font` picks, which are
  already resolution-based.

### 1.4 HUD scale **[ini]** (`DefUser.ini`, `[Botpack.ChallengeHUD]`)

| Key | Default |
|---|---|
| `HudScale` | 1.0 |
| `StatusScale` | 1.0 |
| `WeaponScale` | 0.8 |
| `CrosshairScale` | 1.0 |
| `bAutoCrosshairScale` | true |
| `Opacity` | 15 |

On a 6" 1080p screen, `HudScale=1.25..1.5` is worth trying. The stat overlay (`stat fps`) has its own settings in
`[Render.Render]`: `StatScaleFont=True` and `StatScaleFactor=-1`, which means automatic.

### 1.5 Keyboard navigation already inside UWindow [uscript]

The idea that UWindow ignores keys (an earlier note in `controller-input.md` §3.1) is too strong. In menu state,
`WindowConsole.KeyEvent` forwards every non-mouse key press as `WM_KeyDown` with the key code to `Root`. That message
reaches `HotKeyDown` and then the **key-focus window** (`Root.KeyFocusWindow`, set by `CheckKeyFocusWindow`, i.e. the
active window's focused control). The handlers that exist:

| Where | Keys |
|---|---|
| `UWindowMenuBar.HotKeyDown` | **Alt + underlined letter** opens Game/Multiplayer/Options/… (`&Game`) |
| `UWindowMenuBar.KeyDown` | Left/Right switch menus |
| `UWindowPulldownMenu.KeyDown` | Up/Down move the highlight, Right opens a submenu, Left goes back, **Enter executes**, PgUp/PgDn scroll |
| `UWindowDialogControl.KeyDown` | **Tab → `TabNext`**. The ring is built automatically by `UWindowDialogClientWindow.CreateControl`, so every dialog has one. There is no Shift+Tab. |
| `UWindowButton` / `UWindowCheckbox` | Space = click |
| `UWindowHSliderControl` | Left/Right step, Home/End |
| `UWindowListBox`, `UWindowGrid` | Up/Down (PgUp/PgDn) |
| `UWindowComboList` | Up/Down/Enter/Esc, typing filters (469) |
| `UWindowEditBox` | full editing keys; Enter commits |
| `UWindowMessageBox` | Enter/Space = default button |
| Escape | `Root.CloseActiveWindow()` |

**Limitations:**
- **There is no visible focus indicator.** `bHasKeyboardFocus` is only used to draw the edit-box caret, so Tab moves
  an invisible focus.
- Focus inside a dialog normally starts where you last clicked.
- Tab order is creation order, not spatial.
- Opening the menu bar needs Alt plus a letter, or the existing `MenuCmd <menu> <item>` exec.

**Takeaway:** mapping the D-pad to arrow keys (plus a "Tab" and an "Enter" button) gives fully keyboard-driven menu
bar and pulldown navigation, plus slider, list and combo control, with no engine changes. Dialog-level "move between
controls" stays awkward until a focus highlight exists (see §3b).

---

## 2. Existing mods and community work

Searches covered OldUnreal forums, unrealarchive, ModDB, GitHub and Steam/ProtonDB. They found **no UT99
gamepad/console-style UWindow replacement**, which matches the earlier search in `controller-input.md` §3.3.

| Item | Relevance | Notes |
|---|---|---|
| [foxWSFix-UT99](https://github.com/alexstrout/foxWSFix-UT99/) | widescreen FOV/HUD | client-side; useful at 16:9; not a menu fix |
| [Legacy:Useful UWindow Extensions](https://unrealarchive.org/wikis/unreal-wiki/Legacy:Useful_UWindow_Extensions.html) | EWindow helper package | modder toolkit (tooltips, fixes), no pad nav |
| [foxmod splitscreen](https://www.moddb.com/mods/foxmod-splitscreen) | gamepad splitscreen | **UT3 only** |
| [UT99DInput](https://github.com/dpjudas/UT99DInput) | DirectInput mouse | Windows only, irrelevant |
| [Lutris UT GOTY OldUnreal installer](https://lutris.net/games/install/23518/view) | sets `[UMenu.UMenuRootWindow] ConfiguredGUIScale=1.5` | confirms the ini key in the wild |
| [OldUnreal patches/issues](https://github.com/OldUnreal/UnrealTournamentPatches) | upstream | the place to request a focus ring or pad nav upstream |

- The Steam Deck and Linux handheld community uses Steam Input mouse emulation, which is the same thing we already
  do.
- Licences are moot because there is nothing to bundle. If EWindow-style code is reused, check its readme, since
  UnrealScript mods are usually freeware without an explicit licence.

---

## 3. Custom approaches

### (a) Android-native launcher (Compose, D-pad-focusable)

This is the highest-value item for the "pick up and play" loop.
- It starts matches with URL arguments, so UT's menus are needed only for rare settings. URL keys come from the
  existing research: `Game`, `Mutator` (comma list), `Name`, `Team`, `Difficulty`, `MaxPlayers`, `FragLimit`,
  `TimeLimit`, `GoalTeamScore`, `GameSpeed`.
  ```
  ut-bin "DM-Deck16][?Game=Botpack.DeathMatchPlus?Mutator=Botpack.InstaGibDM?Name=Player"
  ```
- Values that are not URL options are written by `GameConfig.apply()` before launch:
  - bots: `[Botpack.DeathMatchPlus] MinPlayers=N`
  - skill: `[Botpack.ChallengeBotInfo] Difficulty`
  - player: `[Engine.PlayerPawn]`/`DefaultPlayer` `Name`, `Class`, `Skin`, `Face`, `Team` in User.ini
  - GUI scale, HUD scale and font scaling (§1)
- Data the launcher can read:
  - the map list, by scanning `Maps/*.unr` by prefix (DM-, CTF-, DOM-, AS-)
  - game types, from `*.int` `Object=(…MetaClass=Botpack.TournamentGameInfo)`
  - mutators, from `*.int` `MetaClass=Engine.Mutator`
- Cost: medium. It is all Kotlin, and Compose gives D-pad focus for free (`focusable()`, the TV-style focus).
- Risk: low. It sits beside the existing launch path in `GameSession`.
- Gaps:
  - the single-player Ladder, which is UTMenu-only (keep "Open UT menus")
  - online server browsing (UBrowser); the launcher can offer "connect to IP" via `ut-bin 1.2.3.4:7777`

### (b) Custom UnrealScript package ("PadUI")

This is feasible and client-only.
- **Install.** Create `System/PadUI.u`, then set `[Engine.Engine] Console=PadUI.PadConsole` with
  `class PadConsole extends UTConsole`. The console is client-side and never sent to servers, so it is safe online.
- **Build.** Run `ucc make` with the source in `PadUI/Classes/*.uc` and `EditPackages=PadUI` added to
  `[Editor.EditorEngine]`. Two ways to run it:
  - with `UCC.exe` from the Windows 469f install in `UnrealTournament/System` (same 469f version, so the `.u` file is
    portable)
  - with `ucc-bin-arm64` on device
- **Two useful features:**
  1. **Focus ring + D-pad.** Override `state UWindow KeyEvent` (or add a root window subclass via
     `[UMenu.UnrealConsole] RootWindow=PadUI.PadRootWindow`).
     - The D-pad (Joy13-16) moves `KeyFocusWindow` spatially. Walk the active window's
       `UWindowDialogClientWindow` children; their `WinLeft`/`WinTop` and `TabNext` are all script-visible.
     - Draw a highlight rectangle in `PostRender`.
     - A sends `LMouseDown`/`LMouseUp` to the focused control. This mirrors what `UWindowButton.KeyDown(IK_Space)`
       already does.
     - L1/R1 switch tabs in `UWindowPageControl` dialogs.
     - This makes every stock and mod dialog pad-navigable without a cursor.
     - Caveat: when `Viewport.bWindowsMouseAvailable` is set, the console overwrites `MouseX/Y` from the OS cursor
       every frame. Hover-based visuals therefore follow the SDL cursor. The overlay cursor should hide or park when
       the D-pad is used.
  2. **"Quick Play" big-button window.** A `UMenuFramedWindow` with 4-6 large `UWindowSmallButton`s:
     - Instant Action DM / CTF, Practice, Settings, Quit
     - opened by `UMenuModMenuItem` (shows under the "Mod" menu, registered by an `.int` line
       `Object=(Name=PadUI.PadQuickPlayItem,Class=Class,MetaClass=UMenu.UMenuModMenuItem,Description="&Quick Play,…")`)
       or automatically from `PadConsole.LaunchUWindow`
     - It can also replace menu-bar defaults through `[UMenu.UMenuMenuBar] GameUMenuDefault=…`.
     - This overlaps (a) and is lower value once the launcher exists.
- Cost: medium-high. It needs UScript, a compile loop, and testing across dialogs; custom mod windows can break the
  spatial heuristics. It is the only way to get true in-engine pad navigation.

### (c) D-pad → keyboard in menu mode (SDL layer)

This is the cheapest. The layer already knows menu mode (relative mouse off) and already maps Start/B to Escape.

| Pad | Sends | Effect |
|---|---|---|
| D-pad | Up/Down/Left/Right | pulldowns, menu bar, sliders, lists, combos (§1.5) |
| A | left click (keep) | |
| Y | Enter | executes the highlighted pulldown item, commits edit boxes, confirms message boxes |
| R1 / L1 | Tab / wheel | Tab cycles dialog controls (invisible focus); wheel scrolls |
| Select (in menu) | Alt+G | opens the Game menu; then the D-pad walks the whole menu bar |
| B / Start | Escape (keep) | |

- Also show the Android IME when an edit box gets focus. That is hard to detect from SDL; offer a manual "keyboard"
  chord instead.
- Cost: very low. It is a few lines in the existing SDL interposer/InputBridge.
- Combined with GUI scale 2.0 it removes most of the fiddliness. The cursor stays for dialogs.

---

## 4. Recommended phased plan

1. **Phase 0, ini only (minutes).** Add to `GameConfig`:
   - `[UMenu.UMenuRootWindow] AutoGUIScale=False, ConfiguredGUIScale=2.0` (try 2.25)
   - `[Botpack.ChallengeHUD] HudScale≈1.25`
   - test `[Botpack.FontInfo] bEnableInGameScaling=True, InGameScalingFactor≈1.25` on device and keep it if the TTF
     path works

   Scale the overlay cursor size and speed by GUIScale. Verify every main dialog fits at 960x540: Start Practice
   Session, Player Setup, Options pages, Ladder.
2. **Phase 1, SDL key mapping (hours).** Menu-mode D-pad → arrows, Y → Enter, R1 → Tab, Select → Alt+G, L1/R1 or the
   right stick Y → wheel. Document the controls on a one-line hint in the overlay.
3. **Phase 2, native launcher (days).** A Compose, D-pad-focusable home screen:
   - Quick Match (game type, map with thumbnails, bots, skill, mutators, time/frag limits)
   - Player (name, skin)
   - Settings (GUI scale, HUD scale, FOV via foxWSFix optional, sensitivity, FPS)
   - "Open UT menus" for Ladder and server browser
   - "Connect to IP"

   Launch via URL plus `GameConfig.apply()`. Bind a "quit to launcher" action in game.
4. **Phase 3, optional PadUI UnrealScript package (1-2 weeks).** A focus ring with spatial D-pad navigation for all
   UWindow dialogs, built with `UCC.exe make` on the PC and shipped as `System/PadUI.u`, with
   `[Engine.Engine] Console=PadUI.PadConsole`. Do it only if Phases 0-2 still leave the in-game menus painful.
   Consider upstreaming a focus highlight to OldUnreal instead.

## 5. Open items to verify on device
- `GetDesktopDPI()` on Android. If it ever returns the real ~367 dpi, `AutoGUIScale` would clamp to 3.0, which is too
  big. Keep `AutoGUIScale=False`.
- That 2.0/2.25 fonts render crisply with VulkanDrv, and that the 469 hi-res LookAndFeel textures load.
- Whether `Canvas.CreateFont` (in-game font scaling) works with the shipped `libSDL3_ttf` and `System/Fonts` on
  Android.
- That the Ladder (UTMenu, its own 1024x768-relative layout, `Root.SetMousePos`) behaves at scale 2.


## Implemented (2026-11-22): Phase 0 + Phase 1

- Phase 0: `ConfiguredGUIScale=2.0`, `AutoGUIScale=False` (GameConfig defaults v1+); UT log: "Using Configured GUI
  Scale: 2.000000" (menu bar laid out at 2x; UWindow picks larger fonts per scale step).
- Phase 1, SDL_utandroid.c menu mode (both builds): D-pad = arrow keys with auto-repeat (400 ms, then 90 ms),
  Y = Enter, R1 = Tab, L1 = Space, Select = Alt+G; A/X/B stay left/right click/Escape, sticks the cursor.
  No pad button reaches the game while a menu is up; held keys are released when the menu closes.
  Tested on the X11 build (accepts adb-injected pad presses): Start -> menus, Select -> Game menu opens, D-pad
  Down x2 -> "Start Practice Session" highlighted (status bar text changes accordingly).

### Menu auto-focus (done)
UWindow has no keyboard focus when the menus open, so the D-pad did nothing until Select (Alt+G).
SDL_utandroid.c now sends Alt+G once, 250 ms after entering menu mode (`menu_focus_at`), opening the
"Game" pulldown so the D-pad works immediately. Only fires on the in-game → menu transition, not for later dialogs.
Verified (X11 build): Start, then Down x2 → "Start Practice Session" highlighted (research/menu2.png).

### PadUI spike (2026-11-22) — works
`native/padui/Classes/PadRootWindow.uc` (one class, ~400 lines) compiled with the PC install's `UCC.exe`
(`bash native/padui/build-padui.sh` → `native/padui/out/PadUI.u`, also copied to `linux-arm64/System/`).
Installed by GameConfig every launch: `[UTMenu.UTConsole] RootWindow=PadUI.PadRootWindow`; its GUI scale defaults
(2.0) come from the class, section `[PadUI.PadRootWindow]`. Log: "Creating root window: PadUI.PadRootWindow".

`PadRootWindow extends UMenuRootWindow` takes keys first via `HotKeyDown` while a dialog is the active window
(menu bar / pulldowns keep stock handling) and draws a yellow focus box in `DrawMouse`:
- D-pad: spatial move to the nearest dialog control (controls = children of a UWindowDialogClientWindow: buttons,
  checkboxes, sliders, combos, edit boxes); scrolls UWindowScrollingDialogClient panes to keep it visible.
- Left/Right on a slider = step, on a combo = previous/next value. A (Enter) = press button / toggle checkbox /
  open combo list (Up/Down/A pick, B closes only the list). R1/L1 (Tab/Space) = next/previous page tab.
- A mouse click moves the box to the clicked control. SDL menu map changed: A = Enter, Y = left click.

Verified on device (X11 build, injected pad): Options > Preferences opens with A, box starts on the first control,
Down x9 to Brightness, Right adjusts it, scrolling follows to the bottom of Video, Down reaches Close, R1/L1 switch
tabs, A toggles a checkbox, A opens the Output Rate list and B closes just the list (research/padA..H.png).
Not covered yet: list boxes (map lists, server browser, bot/skin lists), key-binding grid (Controls tab), edit boxes
need a text-entry path, Down from a scroll pane's last visible row can jump to the Close button before scrolling.
- Tab strip focus (added): Up past the top control focuses the page control's tab strip (box on the selected tab);
  Left/Right (or L1/R1) switch tabs, Down/A return to the page's first control. Verified (research/tabA..C.png).
  Left/Right follow the tab list order (Video, Audio, Game, ...), not the two-row visual layout.
- GUI scale default is now 3.0, the slider maximum (640x360 virtual desktop): `[PadUI.PadRootWindow]` and
  `[UMenu.UMenuRootWindow]` in GameConfig.systemDefaults (DEFAULTS_VERSION 4) plus the class default.
  Start Practice Session and Preferences fit and stay readable (research/lb1.png).
- List boxes (added): any UWindowListBox is focusable, including those in splitter frames (Map List, Mutators).
  Entering selects the first item; Up/Down walk items (scrolling) and leave the list at either end; A = the list's
  double-click action (moves a mutator/map to the other list). Verified on Mutators (research/lb4..lb11.png).
- Direction rule tightened: a candidate must start past the focused control's centre (a tall list's neighbour above
  no longer counts as "to the right").
- Key bindings grid (added). The grid buttons were already reachable; binding needed pad buttons to reach UWindow:
  SDL_utandroid.c no longer blocks the Joy buttons in menu mode (they arrive alongside the bridge's keys, Joy first).
  PadUI swallows Joy keys, except while a UMenuCustomizeClientWindow button is polling: the next Joy1-16 press is
  bound (pad-made arrows/Enter ignored, and 15 frames after the wait starts/ends so the A that started it, or the
  key half of the press that ended it, does nothing). Start (Escape only, never a Joy button) cancels without
  closing the dialog; every other pad button is bindable. The grid shows pad names (A, B, L2, D-Pad Up...) by
  rewriting the dialog's LocalizedKeyName[IK_Joy1..16]. Verified: Fire -> "X or Left Mouse", Start cancel
  (research/kb16..18.png). Limitation (stock): each row shows only its first two keys, so pad bindings behind two
  keyboard keys are bound but not displayed (e.g. Fire = R2 shows "Left Mouse or Alt").
- On-screen pad keyboard (added). A on a text field (UWindowEditControl, or an editable combo such as a server
  address) opens a 10x5 key grid drawn by PadRootWindow (preview line with caret on top). D-pad picks a key (wraps),
  A types it (bottom row: Shift / Space / Del / Done), B backspace, X space, Y shift (capitals + symbols),
  L1/R1 move the caret, Start or Done closes it. Numeric-only fields accept digits (and one '.' if float).
  Delayed-notify fields (player name) get their change notification on close. Mouse clicks are ignored while it is up.
  Verified on Player Setup > Name: B,X,A,B,X,A,Right,A,Y,A on "Player" -> "Playe  qwW" (research/tx19..20.png).
- Two input fixes found while testing it:
  - The engine repeats a held Joy button as a press every frame with no release in between: PadUI counts a Joy press
    only if that button reported nothing in the previous 2 frames (`NewJoyPress`).
  - A tap shorter than a frame could vanish between two engine samples of the virtual pad: SDL_utandroid.c now keeps
    every button down for at least 50 ms (`MIN_PRESS_NS`), in game too.
- Server browser (added, partly verified). PadFocus is now any UWindowWindow, so UBrowserServerGrid (server list)
  and UBrowserTypesGrid (game-type filter) are focusable: Up/Down pick a row (leaving the grid at either end),
  A = the grid's double-click (join server / toggle type), X = refresh the server list.
  Pages with nothing focusable (the browser's News tab) now put the focus on the dialog's tab strip, so the D-pad
  walks the browser tabs. Verified on device: Find Internet Games via pad, Check Player Name dialog (No), tabs
  News -> Favorites -> Populated Servers (research/sb1..sb8.png). The grid itself is NOT verified: the X11 build
  (the only one adb pad injection reaches) gets no servers, see below.
- X11 build bug found: no hostname resolves in the game ("The master server could not be resolved:
  medor.no-ip.org") although the device resolves it. glibc's resolver reads /etc/resolv.conf, which Android lacks.
  The native build is unaffected (the shim maps getaddrinfo to bionic). Fix idea: patch the single
  "/etc/resolv.conf" string in the runtime libc.so.6 to a same-length path the app writes from
  ConnectivityManager's DNS servers.
- Native build test path (debug builds): NativeGameActivity forwards pad keys from virtual devices
  (`adb shell input gamepad keyevent ...`) via JNI nativeInjectButton -> shared_state.inject_buttons/inject_pressed
  -> OR-ed into the real pad in SDL_utandroid.c's UTANDROID_PollRealPad. Verified: Start / Right / Down / A opened
  Find Internet Games on the native build (research/nb1.png). Testing is on the native build from now on.
- User report (native build): connected to an internet server from the browser, so DNS, master server list and
  join work there. The user picked the server with the D-pad and joined with A (grid navigation confirmed on a real pad).
- Touchpad cursor (native build; replaces the stick cursor there). NativeGameActivity.dispatchTouchEvent consumes all
  touches (SDL never sees them): finger movement x1.5 -> nativeTouch(dx, dy) -> shared_state.touch_dx/dy, a still
  touch under 300 ms -> a tap = left click held 50 ms (SDL_utandroid.c, menu mode only; drained outside menus).
  CursorOverlay only draws the cursor for 3 s after touchpad use. The sticks no longer move the cursor and Y/X no
  longer click on the native build; the X11 backup keeps its stick cursor. Verified: hidden by default, relative
  swipe moves (research/tc1..tc5.png); user confirmed it works on the device.
