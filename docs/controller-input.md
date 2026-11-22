# Controller input for UT99 469f on Android (Retroid Pocket 6)

Scope: how OldUnreal 469 handles joysticks and gamepads on Windows (WinDrv) and Linux (SDLDrv over SDL3), what
SDLDrv actually sends to the engine, a recommended `User.ini` binding set for the RP6, and ways to make UWindow
menus usable with a pad.

Evidence comes from:
- the 469f release notes (`linux-arm64/ReleaseNotes.md`, `UnrealTournament/ReleaseNotes.md`)
- the shipped inis
- strings (ASCII and UTF-32) from `SDLDrv.so`, `Engine.so` and `WinDrv.dll`
- disassembly of `SDLDrv.so` (`USDLClient::Init`, `USDLViewport::UpdateInput`) and of `Engine.so` (`UInput::Exec`, `UInput::ReadInput`), done with NDK `llvm-objdump`
- the UnrealScript source text embedded in `Engine.u`, `UWindow.u` and `UTMenu.u`
- the SDL3 Android joystick sources on GitHub

Each claim is marked with how it was established: **[disasm]**, **[strings]**, **[uscript]**, **[ini]**, **[web]**,
or **[inferred]**.

---

## 1. Controller support in OldUnreal 469 (469a–469f)

### 1.1 Release notes

Searching both 469 release notes for joystick, gamepad, controller, XInput and SDL finds **no controller or joystick
feature work** anywhere in 469a–469f. The only input changes concern the mouse:
- Raw Input and DirectInput mouse modes
- the XP mouse acceleration curve
- IOKit raw mouse on macOS
- `MouseInputMode=SDL3_INPUT_Standard|SDL3_INPUT_RawInput`

469e/f also note that "SDL3 has landed", meaning the SDL backend was ported from SDL2 to SDL3. Joystick support is
therefore the classic UE1 design: one joystick, digital buttons mapped to `Joy1..Joy16`, and raw axes mapped to
`JoyX/Y/Z/R/U/V`. There is no gamepad abstraction, no rumble and no hot-plug.

### 1.2 Engine key names (`Engine.u` EInputKey) [strings]

| Code | Keys |
|---|---|
| `0xC8–0xD7` | `IK_Joy1` … `IK_Joy16` (only 16 buttons exist) |
| `0xE0–0xE3` | `IK_JoyX`, `IK_JoyY`, `IK_JoyZ`, `IK_JoyR` |
| `0xE4/0xE5` | `IK_MouseX`, `IK_MouseY` |
| `0xE8/0xE9` | `IK_JoyU`, `IK_JoyV` (EA/EB are unknown or unused) |
| `0xF0–0xF3` | `IK_JoyPovUp`, `IK_JoyPovDown`, `IK_JoyPovLeft`, `IK_JoyPovRight` |

There are no `JoySlider` or trigger keys.

### 1.3 Windows: WinDrv.dll [strings][ini]

- **Joystick API: legacy WinMM** (`joyGetNumDevs`, `joyGetDevCapsA`, `joyGetPosEx`) through
  `UWindowsViewport::JoystickInputEvent`. It does **not use XInput**, and DirectInput (`dinput8.dll`) is used only
  for the **mouse** (`UseDirectInput`, `UseRawHIDInput`).
- Settings in `[WinDrv.WindowsClient]`:
  - `UseJoystick` (default False)
  - `DeadZoneXYZ=True`, `DeadZoneRUV=False` (booleans)
  - `ScaleXYZ=1000`, `ScaleRUV=2000`
  - `InvertVertical`
- Under Wine/GameNative, WinMM joystick goes through Wine's dinput/winebus. XInput pads under WinMM have the classic
  quirks: triggers are merged on one axis and the right stick is on R/U.

### 1.4 Linux and macOS: SDLDrv.so (SDL3)

**It uses the SDL_Joystick API, not SDL_Gamepad.** [disasm][strings]
- `SDL_InitSubSystem(0x220)` = `SDL_INIT_VIDEO | SDL_INIT_JOYSTICK`. The gamepad subsystem is not initialised.
- Imports: `SDL_GetJoysticks`, `SDL_OpenJoystick`, `SDL_GetNumJoystick{Buttons,Axes,Hats,Balls}`,
  `SDL_UpdateJoysticks`, `SDL_GetJoystickButton`, `SDL_GetJoystickAxis`, `SDL_GetJoystickNameForID`,
  `SDL_CloseJoystick`.
- It does **not** import `SDL_GetJoystickHat`. Hats are handled through `SDL_EVENT_JOYSTICK_HAT_MOTION` (0x602) in
  the `SDL_PollEvent` loop, and trackballs through `SDL_EVENT_JOYSTICK_BALL_MOTION` (0x601).

**Config keys in `[SDLDrv.SDLClient]`** (from the UTF-32 property strings) [strings]:

| Key | Meaning |
|---|---|
| `UseJoystick` | Master switch. **Init writes it back to False if no joystick can be opened** [disasm], and that value then gets saved. UT2004's SDL3 client shows the same behaviour [web: OldUnreal forum]. The launcher should force `UseJoystick=True` on every start. |
| `JoystickNumber` | Index into `SDL_GetJoysticks()`. Reset to 0 if out of range ("JoystickNumber exceeds the number of detected joysticks"). |
| `JoystickHatNumber` | Which hat feeds the d-pad keys. Reset to 0 if out of range. |
| `IgnoreHat` | Ignores hat events. It also lifts the 12-button clamp (see below). |
| `ScaleJBX`, `ScaleJBY` | Trackball scaling. Ball motion is turned into **mouse** X/Y. This is irrelevant on Android, where balls are always 0. |

The `DeadZoneXYZ`, `DeadZoneRUV`, `ScaleXYZ`, `ScaleRUV` and `InvertVertical` lines in the `[SDLDrv.SDLClient]`
section of `Default.ini` are **leftovers: SDLDrv never reads them.** SDLDrv has **no dead zone at all**.

**Init behaviour (`USDLClient::Init`)** [disasm]:
- Logs "Detected %d joysticks" and "Joystick [%i] : %s", then opens `JoystickNumber`.
- The joystick is opened **once, at init**. There is no hot-plug handling.
- Logs the button, hat, ball and axis counts.
- **Buttons are clamped to 16.** If buttons > 12, the device has at least one hat and `IgnoreHat=False`, buttons are
  **clamped to 12**. The log says "Too many joystick buttons; clamped to %d. (Disable hat switches with
  "IgnoreHat=True" to raise this.)". This matches the earlier on-device test under the Linux evdev backend, where the
  pad showed "Xbox Wireless Controller", 25 buttons, 1 hat and 6 axes.
- **Axes are clamped to 8** ("Too many joystick axes; clamped to 8"), but the axis key table has only **6** valid
  entries. See the bug below.

**Per-frame polling (`USDLViewport::UpdateInput`)** [disasm]:
- **Buttons:** `SDL_GetJoystickButton(i)` maps to key `0xC8+i`, so SDL button *i* becomes **`Joy(i+1)`**. A press or
  release is only generated when the state changes.
- **Axes:** `SDL_GetJoystickAxis(i)` is converted to `float = raw / 65536`, so the range is **-0.5 … +0.5**. It is
  sent **every frame** as `IST_Axis` to the key from this table:

  | SDL axis | UT key |
  |---|---|
  | 0 | `JoyX` |
  | 1 | `JoyY` |
  | 2 | `JoyZ` |
  | 3 | `JoyR` |
  | 4 | `JoyU` |
  | 5 | `JoyV` |

  - **Bug:** the table at `.rodata 0xE8F8` has only 6 entries. Axes 6 and 7 read the bytes that follow, which are
    part of the string "12USDLViewport" (`0x53553231`, `0x69564C44`). They are then passed as a bogus key code to
    `Engine->InputEvent` without any range check. **A device that reports 7 or 8 axes causes undefined input**: a
    garbage key index, and possibly out-of-bounds reads in UInput. Many Android pads report 8 axes:
    X, Y, Z, RZ, LTRIGGER, RTRIGGER, BRAKE, GAS.
- **Hats (event-driven):** hat values map to buttons, **not** to the `JoyPov*` keys.

  | Hat direction | UT key |
  |---|---|
  | UP | `Joy13` |
  | DOWN | `Joy14` |
  | LEFT | `Joy15` |
  | RIGHT | `Joy16` |
  | Diagonals | Release (no key) |

  This is why buttons are clamped to 12 when a hat is present. **SDLDrv never generates `JoyPovUp/Down/Left/Right`**,
  so the `JoyPov*` lines in `DefUser.ini` are dead on SDL. They only work with WinDrv.

### 1.5 What the engine does with axis values [disasm of `UInput::Exec` / `ReadInput`, uscript]

A binding has the form `Axis <var> Speed=<S> SpeedBase=<B>`. `Speed` defaults to 1 and `SpeedBase` to 0. There are
**no `DeadZone=` or `Invert=` parameters** (those exist only in UE2).

The binding changes the axis variable like this:

| Condition | Effect |
|---|---|
| `SpeedBase > 0` | `axis += Speed * SpeedBase * Delta` |
| else, `IST_Hold` (keys held) | `axis += Speed * Delta`, where `Delta` = frame time |
| else, `IST_Axis` (mouse and joystick) | `axis += 0.01 * Speed * Delta` |

After processing, `UInput::ReadInput` multiplies every axis variable by **`20 / DeltaSeconds`**.
`PlayerPawn.PlayerInput` and `PlayerWalking.PlayerMove` [uscript] then apply:
- `aLookUp *= FOVScale`, `aTurn *= FOVScale`
- `aForward += aBaseY`
- `aForward *= 0.4`, `aStrafe *= 0.4`, `aLookUp *= 0.24`, `aTurn *= 0.24`
- `Yaw += 32 * DeltaTime * aTurn`, `Pitch += 32 * DeltaTime * aLookUp`

Consequences:
1. **The joystick is frame-rate dependent.** The stick value is re-sent every frame and is not multiplied by the
   frame time, so turn and look speed scale with FPS. On SDLDrv, at FOV 90:

   `turn deg/s ≈ 0.00422 × Speed × FPS × stick` (stick from 0 to 1)

   Movement saturates at AccelRate anyway, so it is less sensitive to this. **Cap the frame rate** (VSync, or the
   display refresh) and tune for that cap.
2. **The stock joystick bindings are useless on SDLDrv.** With `JoyU=Axis aturn speed=5.9` at 60 FPS, full
   deflection gives about 1.5°/s. Speeds need to be roughly 100–200× larger, around 500–1000. WinDrv evidently
   pre-scales with ScaleXYZ/RUV; SDLDrv does not. [inferred from disasm; verify in game]
3. There is **no dead zone anywhere in the chain**:
   - SDL raw joystick: Android reports `MotionRange.getFlat()` but SDL ignores it
   - SDLDrv: none
   - UInput: none

   The dodge detector compares `aBaseY > 0` and `aStrafe > 0` each frame. Stick noise around zero flips those signs,
   which can trigger double-tap dodges within `DodgeClickTime` (0.25 s). Either add a dead zone before the engine
   (recommended), or set `DodgeClickTime=0.0` in `[Engine.PlayerPawn]`/User.ini, since `if (DodgeClickTime > 0)`
   disables dodging.
4. **Triggers as axes on Android rest at -0.5, not 0.** The SDL Android Java layer normalises every axis as
   `(v - min) / range * 2 - 1`, so a 0..1 trigger becomes -1..1 [web: SDLControllerManager.java]. With the default
   `DefUser.ini` (`JoyU=Axis aturn`, `JoyV=Axis aLookUp`), a stock Android pad whose axes 4 and 5 are triggers turns
   and looks permanently. **Clear `JoyU`/`JoyV`, or never expose triggers as axes.** An `Axis` binding cannot fire a
   weapon anyway: `Fire` needs a button press and release.

### 1.6 How SDL3 on Android numbers the RP6 pad (stock SDL3, no interposer) [web: SDL source]

`SDL_sysjoystick.c` (android) and `SDLControllerManager.java`:

**Buttons.** Android keycodes become SDL button indices in SDL_Gamepad order. `nbuttons` = highest set bit of the
button mask + 1. Combined with SDLDrv's `Joy = index + 1`:

| SDL index | Pad control | UT key |
|---|---|---|
| 0 | A (South) | Joy1 |
| 1 | B (East) | Joy2 |
| 2 | X (West) | Joy3 |
| 3 | Y (North) | Joy4 |
| 4 | Select/Back | Joy5 |
| 5 | Guide/Home | Joy6 |
| 6 | Start | Joy7 |
| 7 | L3 | Joy8 |
| 8 | R3 | Joy9 |
| 9 | L1 | Joy10 |
| 10 | R1 | Joy11 |
| 11 | D-pad Up | Joy12 |
| 12 | D-pad Down | Joy13 |
| 13 | D-pad Left | Joy14 |
| 14 | D-pad Right | Joy15 |
| 15 | L2 digital key (`KEYCODE_BUTTON_L2` → MISC1) | Joy16 |
| 16 | R2 digital key | **dropped** (SDLDrv clamps to 16) |

- Home on the RP6 is normally consumed by the system.
- **The hat becomes D-pad buttons:** `Android_AddJoystick` sets `nhats = 0`. So on Android the 12-button clamp does
  not apply, and `IgnoreHat` is irrelevant.
- The RP6 has an A/B layout swap setting (Xbox vs Nintendo). `KEYCODE_BUTTON_A` is always SDL South, so check which
  physical button sends it.

**Axes.** Sorted with SDL's RangeComparator: X, Y, Z, RZ, (RX, RY), LTRIGGER, RTRIGGER, BRAKE, GAS. A typical pad
gives:

| SDL axis | Source | UT key |
|---|---|---|
| 0 | left X | `JoyX` |
| 1 | left Y | `JoyY` |
| 2 | right X | `JoyZ` |
| 3 | right Y | `JoyR` |
| 4 | L trigger | `JoyU` |
| 5 | R trigger | `JoyV` |
| 6, 7 | (if present) | **garbage key bug** |

Stick Y is negative for up. The SDL3 Android backend does not add the accelerometer as a joystick; it becomes a
sensor on the opened joystick.

**So the stock path has three problems on the RP6:**
1. R2 is unusable.
2. Triggers rest at -0.5 as axes.
3. 7–8 axes may trigger the garbage-key bug.

On top of that there is no dead zone and the speed is frame-rate dependent. **Verify the real layout** from the UT
log lines ("Joystick has N buttons/axes/hats") and with a binding test (§2.4).

### 1.7 Recommended fix: a "UT layout" SDL_Joystick interposer

`architecture-options.md` Option B already resolves SDLDrv's SDL imports through our own table. Use that to
**interpose the joystick calls** and back them with `SDL_Gamepad`, which gives the stable Android/Xbox mapping and
real trigger axes from 0 to 32767:

| SDLDrv call | Interposer returns |
|---|---|
| `SDL_GetJoysticks` | Exactly one ID, the first gamepad |
| `SDL_GetNumJoystickButtons` | 16 |
| `SDL_GetNumJoystickAxes` | 4 |
| `SDL_GetNumJoystickHats` | 0 |
| `SDL_GetNumJoystickBalls` | 0 |
| `SDL_GetJoystickAxis(i)` | Gamepad LEFTX, LEFTY, RIGHTX, RIGHTY after a **radial dead zone** (about 0.15–0.20, rescaled to full range). An optional response curve helps here. |
| `SDL_GetJoystickButton(i)` | The table below, with **L2 and R2 as thresholded digital buttons** (press > 0.35, release < 0.25) |
| `SDL_UpdateJoysticks` | Call the real one, then run the menu-mouse emulation (§3.2). SDLDrv calls this exactly once per frame. |

The UT layout keeps SDL_Gamepad order and only repurposes index 5, which is Home (taken by the OS anyway):

| SDL index | UT key | RP6 control |
|---|---|---|
| 0 | Joy1 | A |
| 1 | Joy2 | B |
| 2 | Joy3 | X |
| 3 | Joy4 | Y |
| 4 | Joy5 | Select |
| 5 | Joy6 | **R2** (trigger to digital) |
| 6 | Joy7 | Start |
| 7 | Joy8 | L3 |
| 8 | Joy9 | R3 |
| 9 | Joy10 | L1 |
| 10 | Joy11 | R1 |
| 11 | Joy12 | D-pad Up |
| 12 | Joy13 | D-pad Down |
| 13 | Joy14 | D-pad Left |
| 14 | Joy15 | D-pad Right |
| 15 | Joy16 | **L2** (trigger to digital) |

This keeps the bindings identical to the stock Android layout for every button except R2. If the interposer is not
built yet, the block in §2 still mostly works on stock SDL: R2 is dead, L2 works only if the RP6 sends
`KEYCODE_BUTTON_L2`, and `JoyU`/`JoyV` must stay empty.

If you use Option A (separate glibc process), the same layout should be exposed through `SDL_AttachVirtualJoystick`
(16 buttons, 4 axes, 0 hats).

---

## 2. Recommended bindings for the RP6

### 2.1 Driver section (System ini, e.g. `UnrealTournament.ini`)

```ini
[Engine.Engine]
ViewportManager=SDLDrv.SDLClient

[SDLDrv.SDLClient]
UseJoystick=True          ; force on every launch (SDLDrv resets it to False if it can't open a pad)
JoystickNumber=0
JoystickHatNumber=0
IgnoreHat=True            ; harmless on Android (0 hats); on desktop Linux it lifts the 12-button clamp
StartupFullscreen=True
```

Cap the frame rate. For example, set `UseVSync=On` (XOpenGLDrv) or the VulkanDrv VSync option, and keep the RP6
panel at 60 Hz. The speeds below assume **60 FPS**; scale them by 60/FPS for other caps.

### 2.2 `User.ini` `[Engine.Input]` block (UT layout, §1.7)

```ini
[Engine.Input]
; --- face buttons ---
Joy1=Jump                                   ; A
Joy2=Duck                                   ; B
Joy3=GetWeapon BotPack.Translocator         ; X
Joy4=ThrowWeapon                            ; Y   (alt: Taunt wave / FeignDeath)
; --- system ---
Joy5=ShowScores                             ; Select
Joy7=ShowMenu                               ; Start
; --- shoulders / triggers ---
Joy6=Fire                                   ; R2 (interposer)   -- stock SDL: R2 is lost, use R1
Joy16=AltFire                               ; L2
Joy10=PrevWeapon                            ; L1
Joy11=NextWeapon                            ; R1
; --- stick clicks ---
Joy8=Walking                                ; L3 (hold = walk)
Joy9=FeignDeath                             ; R3
; --- d-pad: direct weapon picks ---
Joy12=SwitchWeapon 8                        ; Up    = Flak
Joy13=SwitchWeapon 4                        ; Down  = Shock
Joy14=SwitchWeapon 10                       ; Left  = Sniper ("0" group)
Joy15=SwitchWeapon 9                        ; Right = Rocket
; --- sticks (SDLDrv delivers -0.5..+0.5; Speed tuned for 60 fps) ---
JoyX=Axis aStrafe Speed=1500                ; left X  (+ = right)
JoyY=Axis aBaseY Speed=-1500                ; left Y  (SDL up = negative)
JoyZ=Axis aTurn Speed=800                   ; right X ~200 deg/s full tilt @60fps
JoyR=Axis aLookUp Speed=-550                ; right Y ~140 deg/s; use +550 for inverted look
JoyU=                                       ; MUST be empty on stock Android SDL (trigger rests at -0.5)
JoyV=
; SDLDrv never sends these:
JoyPovUp=
JoyPovDown=
JoyPovLeft=
JoyPovRight=
```

How the speeds were derived (60 FPS, full deflection = 0.5):
- **Turn:** 0.00422 × 800 × 60 ≈ 203°/s. Keyboard turn is about 127°/s.
- **Forward and strafe:** with Speed=1000, `aForward` ≈ 2400, which equals keyboard MoveForward. 1500 gives headroom
  at lower FPS. Movement saturates at AccelRate, so a larger value only makes partial deflection more "digital".

**Without the interposer (stock SDL3 Android):**
- Move Fire to R1 (`Joy11=Fire`) and weapon-prev/next elsewhere, for example `Joy4=NextWeapon`.
- Keep `JoyU`/`JoyV` empty.
- Expect stick drift.
- Add `DodgeClickTime=0.0` under `[Engine.PlayerPawn]` in User.ini if phantom dodges occur.

**Also recommended in User.ini:**
- `[Engine.PlayerPawn] bAlwaysMouseLook=True`. Without it, a centred view (bSnapToLevel) fights right-stick look.
- `bNoMouseSmoothing=True` is already set.

### 2.3 Extra one-button actions worth binding

These are exec functions confirmed in `Engine.u`, `Botpack.u` and `UTMenu.u`:
- `ShowMenu`, `ShowScores`, `ShowServerInfo`, `ShowObjectives`
- `Talk`, `TeamTalk`, `Say`, `Speech`
- `QuickSave`, `QuickLoad` (single player / Unreal-style)
- `Pause`, `Suicide`, `BehindView 1`, `ThrowWeapon`, `FeignDeath`
- `Taunt <anim>`, `GetWeapon <class>`, `SwitchWeapon N`, `NextWeapon`, `PrevWeapon`
- `AddBots N`
- `MenuCmd <menu> <item>`: opens a specific menu-bar entry directly
- `Mutate <string>`
- `Open <map>?Game=...`
- `Exec <file>`

Bind several commands to one button with `|`. Use `OnRelease <cmd>` for release actions.

To tune speeds live from the console (no restart needed): `set input JoyZ Axis aTurn Speed=900`.

The UT voice/speech menu key is `[UTMenu.UTConsole] SpeechKey=<byte>`. It accepts a joystick key code, for example
`SpeechKey=201` for Joy2. The speech window itself is mouse-driven (see §3).

### 2.4 Verifying the mapping on the device

- Check the UT log for the "Joystick has N buttons / hats / axes" lines and any clamping message.
- Temporarily bind `Joy1=Say Joy1` … `Joy16=Say Joy16` (and the axes to `Say`-free tests) to identify each
  physical button.

---

## 3. Controller-friendly menus

### 3.1 What UT itself offers

**UWindow** (`UMenu`/`UTMenu`) is **mouse-only** [uscript, `UWindow.u` `WindowConsole` state `UWindow`]:
- In menu state, `KeyEvent` handles only:
  - `IK_LeftMouse`, `IK_RightMouse`, `IK_MiddleMouse` and the wheel
  - `IK_Escape` → `Root.CloseActiveWindow()`
  - the console key
  - `IK_MouseX`/`IK_MouseY` axis events, which move the cursor
- Everything else, including `Joy*`, becomes a `WM_KeyDown`/`WM_KeyUp` that menus ignore. Joystick axes are
  dropped.
- When `Viewport.bWindowsMouseAvailable` is true (an OS cursor is in use), the cursor position comes **from the
  absolute OS/SDL mouse position** (`MouseX = Viewport.WindowsMouseX / GUIScale`). Moving the SDL mouse therefore
  moves the UWindow cursor.

**Other findings:**
- **No built-in joystick-as-mouse** in 469. The only joystick-to-mouse path in SDLDrv is trackball motion
  (`ScaleJBX`/`ScaleJBY`), and Android pads have no balls.
- **No 469 controller features** in the release notes.
- **No maintained UT99 "gamepad menu" mod** was found (searched OldUnreal forums, unrealarchive and GitHub). foxWSFix
  is widescreen only. Community practice (Steam Deck and similar) is external mouse emulation.
- The old Unreal-style keyboard menus (`bShowMenu`/`MainMenuType`) are not wired into UT's `ChallengeHUD`, so they
  are not a usable fallback.
- `MenuCmd <menu> <item>` (UTConsole) opens a specific menu-bar item programmatically. It is useful for "jump
  straight to Botmatch setup" buttons, but you still need a pointer inside the dialog.

### 3.2 Recommendations, in order

1. **Touchscreen works today.** SDL3 Android synthesises mouse events from touch by default
   (`SDL_HINT_TOUCH_MOUSE_EVENTS=1`). In menus SDLDrv uses the absolute cursor, so tapping UWindow controls should
   just work on the RP6's touchscreen. Verify the coordinate mapping when the internal resolution differs from the
   panel resolution.

2. **Gamepad-to-mouse in the interposer (best UX; no engine or UScript changes).** Inside the interposed
   `SDL_UpdateJoysticks`, which runs once per frame:
   - **Menu-mode detection.** Track SDLDrv's calls to `SDL_SetWindowRelativeMouseMode(window, on)`; you also
     interpose that. Relative mode on means in-game; off means a UWindow cursor is shown. As a fallback, compare
     `SDL_GetWindowRelativeMouseMode`.
   - **In menu mode:**
     - Integrate right stick and left stick (with dead zone and acceleration) into a cursor position. Apply it with
       `SDL_WarpMouseInWindow(window, x, y)`, which updates SDL's internal mouse state, so `SDL_GetMouseState`
       agrees. Verify it on Android; the fallback is `SDL_PushEvent(SDL_EVENT_MOUSE_MOTION)` with absolute x/y.
     - A → push `SDL_EVENT_MOUSE_BUTTON_DOWN`/`UP` (left).
     - X → right button.
     - B → push `SDL_EVENT_KEY_DOWN`/`UP` for `SDLK_ESCAPE`, which closes the active window.
     - D-pad → arrow keys, which work in list boxes and edit fields.
     - L1/R1 → mouse wheel.
     - Start → Escape (toggles the menu).
   - While in menu mode, report the pad as neutral to SDLDrv (all buttons up, axes 0), so nothing leaks into
     bindings when the menu closes.
   - For text fields (player name, chat, console), show the Android IME. SDLDrv does not call
     `SDL_StartTextInput`, so the launcher or interposer must trigger the soft keyboard. A Bluetooth keyboard also
     works.

   If Option A (separate process) is used, do the same on the Java side with
   `SDLActivity.onNativeMouse(button, action, x, y, relative)` and `onNativeKeyDown(KEYCODE_ESCAPE)`. Hook
   `SDLActivity.setRelativeMouseEnabled()` to know the mode.

3. **Pure UnrealScript alternative (portable, client-side).** A tiny package with
   `class PadConsole extends UTConsole`, which overrides `state UWindow`:
   - In `KeyEvent`, capture `IST_Axis` for `IK_JoyZ`/`IK_JoyR` (or `JoyX`/`JoyY`) into a velocity.
   - Map `IK_Joy1` press/release → `Root.WindowEvent(WM_LMouseDown/WM_LMouseUp, None, MouseX, MouseY, IK_LeftMouse)`.
   - Map `IK_Joy2` → `Root.CloseActiveWindow()`.
   - In `Tick`, advance `MouseX`/`MouseY` and clamp them to `Root.WinWidth`/`WinHeight`.
   - When `Viewport.bWindowsMouseAvailable` is true, UWindow overwrites `MouseX` from the OS cursor each frame.
     So also issue `Viewport.ConsoleCommand("SETMOUSE "$X$" "$Y)`: SDLDrv implements a `SETMOUSE` exec, which
     presumably warps the cursor. Verify this.
   - Enable it with `[Engine.Engine] Console=PadConsole.PadConsole` and build it with `ucc make`.
   - It is client-only, so it is safe online.
   - This is more work and more fragile than option 2. Use it only if the interposer route is not taken.

4. **Launcher-first design (skip most menus).** Use a native Android launcher (Compose UI, fully gamepad-navigable)
   that writes the ini values and then starts the game straight into a match:

   ```
   ut-bin "CTF-Face?Game=Botpack.CTFGame?Mutator=Botpack.InstaGibDM?Name=Player?Team=0"
   ut-bin "DM-Deck16][?Game=Botpack.DeathMatchPlus"
   ```

   - URL options parsed by the game code [strings]:
     - `Game`, `Mutator` (comma-separated), `Name`, `Team`, `Difficulty`, `MaxPlayers`
     - `FragLimit`, `TimeLimit`, `GoalTeamScore`, `GameSpeed`, `Password`
   - Bot count is **not** a URL option. Write `[Botpack.DeathMatchPlus] MinPlayers=N` (globalconfig, used for
     CTF/TDM subclasses too) before launch, or bind a button to `AddBots 4`.
   - Bot skill lives in `[Botpack.ChallengeBotInfo]` (`Difficulty`).
   - In-game, Start=`ShowMenu` is then needed only for occasional settings, which touch handles fine.
   - `Joy5=ShowScores` and a "quit to launcher" binding (`Joy…=Quit`, or a long-press in the interposer) round out a
     menu-free loop.

5. **GameNative/Wine (current setup).** Use GameNative's own controller-to-keyboard/mouse mapper. WinDrv's WinMM
   joystick under Wine is the only native alternative, and it inherits the same UE1 limitations.

### 3.3 Mods and links

- No UT99 gamepad-UI mod exists that is worth shipping. The relevant pieces are:
  - **foxWSFix-UT99** (widescreen FOV/HUD fix, useful on the 16:9 RP6): https://github.com/alexstrout/foxWSFix-UT99/
  - UWindow extension notes for writing the PadConsole: https://unrealarchive.org/wikis/unreal-wiki/Legacy:Useful_UWindow_Extensions.html
- OldUnreal releases and issues (469f): https://github.com/OldUnreal/UnrealTournamentPatches
- SDL3 Android joystick internals used above:
  - https://github.com/libsdl-org/SDL/blob/main/src/joystick/android/SDL_sysjoystick.c
  - https://github.com/libsdl-org/SDL/blob/main/android-project/app/src/main/java/org/libsdl/app/SDLControllerManager.java
- OldUnreal forum report that SDL3 `UseJoystick` is reset to False (UT2004 SDL3 client, same driver family):
  https://www.oldunreal.com/phpBB3/viewtopic.php?p=105010

---

## 4. Open items to verify on the device

1. The actual Android button and axis counts for the RP6 pad, from the UT log lines; and whether L2/R2 also send
   `KEYCODE_BUTTON_L2/R2`.
2. Whether 7+ axes really break input (the bogus key path). This is moot with the interposer.
3. Turn and look speeds at the chosen FPS cap. Tune with `set input`.
4. That SDLDrv switches relative mouse mode off in UWindow menus, and that `SDL_WarpMouseInWindow` moves the UWindow
   cursor on Android.
5. Touch-to-mouse coordinate correctness at non-native internal resolutions.
