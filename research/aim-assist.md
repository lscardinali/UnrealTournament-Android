# Aim assist for gamepad play — findings and plan

Date: 2026-11-22. Scope: UT99 469 (OldUnreal) Linux arm64 binaries in `linux-arm64/System`, input via
`native/sdl/utandroid/SDL_utandroid.c`, bindings in `GameConfig.kt`. Nothing in the project was modified.

Evidence tags: **[uscript]** = read from the script text embedded in the shipped `.u` packages (grep -a, line numbers
are line offsets in the binary file), **[ini]** = shipped ini, **[strings]** = binary string scan, **[web]** = web search,
**[inference]** = reasoning, not yet tested on device.

---

## TL;DR

1. UT99 already has auto-aim ("bullet magnetism", shot direction only, the crosshair does not move). It is a
   per-player setting, `MyAutoAim` in `[Engine.PlayerPawn]`, with a menu checkbox, and the engine **only applies it in
   standalone (offline) games**, so it can't work as an online cheat. It has two gotchas that matter for us:
   it is disabled when `bAlwaysMouseLook=True`, and when `Level.Game.Difficulty > 2`.
2. No UT99 aim-assist mutator turned up (UnrealArchive, ModDB, general web). The nearest is foxMod for **UT3**
   (gamepad-only subtle auto-aim), which doesn't run on UT99. **[web]**
3. Recommendation, in order:
   - **Phase 1 (config only, no code):** enable the built-in auto-aim offline with a moderate cone, and set
     `bAlwaysMouseLook=False`. Expose it as an app setting ("Aim assist: off / light / strong").
   - **Phase 2 (input layer):** add **gyro aiming** (the RP6 has a gyroscope) on two extra virtual joystick axes, plus
     stick polish (response curve, a short turn-acceleration ramp, optional look slow-down while firing).
   - **Phase 3 (optional):** a small offline-only UnrealScript mutator for "aim friction / slow-down" near targets, if
     the built-in magnetism plus gyro still feels too hard. It destroys itself on anything other than `NM_Standalone`.
   - Don't build the combined mutator-to-SDL channel (approach c). It's impractical and isn't needed.

---

## 1. What UT99 already has

### 1.1 Properties and functions (verified in the 469 packages)

| Identifier | Where | Meaning | Evidence |
|---|---|---|---|
| `MyAutoAim` | `Engine.PlayerPawn`, `var travel globalconfig float` | Player's auto-aim threshold, the cosine of the cone half-angle. `1.0` turns it off; lower values give more assist. | [uscript] Engine.u:5561 |
| `AutoAim` | `Engine.GameInfo`, `var() globalconfig float` | Game-wide floor. The comment says "How much autoaiming to do (1 = none, 0 = always)". The player value is clamped to `FMax(MyAutoAim, Level.Game.AutoAim)`, so the game can only *reduce* assist. | [uscript] Engine.u:16769, 9230, 8297 |
| `exec SetAutoAim(float F)` → `ChangeAutoAim(F)` | PlayerPawn | Console command. Sets `MyAutoAim = FMax(Level.Game.AutoAim, F)`. | [uscript] Engine.u:8289-8297 |
| `AdjustAim(projSpeed, projStart, aimerror, bLeadTarget, bWarnTarget)` | Pawn / PlayerPawn (Bot overrides it for AI aiming) | Weapons call it to get the actual fire rotation. | [uscript] Engine.u:9309, Botpack.u:139153 |
| `PickTarget` / `PickAnyTarget` | Pawn, `native(531)` / `native(534)` | Native cone search for the best pawn or actor along `FireDir`. | [uscript] Engine.u:11756-11757 |
| `bWarnTarget` | `Engine.Weapon`, `var(WeaponAI) bool` | **Not aim assist.** When a projectile is fired, it tells the target pawn so bots can dodge. | [uscript] Engine.u:1620 |
| `AimError` | Weapon | Bot inaccuracy input. PlayerPawn's `AdjustAim` ignores it ("Aimerror not used in this version"). | [uscript] Engine.u:9305 |
| UMenu "Auto Aim" checkbox | UMenu game options window | Checked means `ChangeAutoAim(0.93)`, unchecked means `ChangeAutoAim(1.0)`. It shows as checked when `MyAutoAim < 1.0`. | [uscript] UMenu.u:3749, 3970-3976 |

Neither `MyAutoAim` nor `GameInfo.AutoAim` appears in the shipped `Default.ini`/`DefUser.ini`, so both use the
compiled defaults until the player toggles the checkbox, which writes `MyAutoAim` to User.ini. **[ini]**

### 1.2 How `PlayerPawn.AdjustAim` behaves (paraphrased, Engine.u:9309-9346)

1. It traces 4000 units along `ViewRotation`. If that trace hits a `bProjTarget` actor, it returns `ViewRotation`
   unchanged. A precise shot is never altered, so headshots still work.
2. Otherwise it calls `PickTarget` with a cone of `FMin(0.93, MyAutoAim)` to find a pawn.
3. It **returns the unmodified `ViewRotation`** (no assist) if any of these is true:
   - `Level.NetMode != NM_Standalone`, so **there is no auto-aim online**, on either a listen server or a client.
   - `Level.Game.Difficulty > 2`
   - `bAlwaysMouseLook` is true
   - the best target lies outside the `MyAutoAim` cone
   - `MyAutoAim >= 1`
4. Otherwise it aims at the target. If no pawn was found, `PickAnyTarget` gets a second chance at the `MyAutoAim` cone.
   The aim point is at the target's distance along the fire direction, with Z set to `Location.Z + 0.3*CollisionHeight`
   (the chest). The weapon fires along that rotation, and the view and crosshair do not move.

All UT weapons route through it. That covers hitscan through `TraceFire` (projSpeed 1000000), projectiles through
`ProjectileFire`, and the alt-fires (Botpack.u:104801, 105415, 134058, 135216 …). `TournamentPlayer` does not override
it, so the PlayerPawn version is the one players get. **[uscript]**

Cone sizes, from acos(MyAutoAim):
- 0.93 → about 21.6° half-angle (the menu's "on" value, very generous)
- 0.97 → about 14°
- 0.98 → about 11.5°
- 0.99 → about 8°
- 0.995 → about 5.7°

Any value between 0.93 and 1 works as a strength slider.

### 1.3 Gotcha: `Level.Game.Difficulty`

In Botpack practice and botmatch games, `Difficulty` comes from the bot skill. Skills 0-3 (Novice…Skilled) give
`Difficulty` 0-3 with novice mode. Skills 4-7 (Adept…Godlike) give `Difficulty = skill - 4`, so 0-3, without novice
mode (Botpack.u:92034-92048). Auto-aim is therefore **off at Skilled (3) and Godlike (7)**, and on for every other skill
level. The ladder works the same way through `TournamentDifficulty` (Botpack.u:91414). A mutator can't change this,
because it is a hard check inside `PlayerPawn.AdjustAim`. **[uscript]** We should state the limitation in the UI.

### 1.4 Gotcha: `bAlwaysMouseLook`

Auto-aim needs `bAlwaysMouseLook=False` (`[Engine.PlayerPawn]`, globalconfig, Engine.u:5569). What changes when it
is false (Engine.u:9001-9030, 9948-9955):
- Mouse **Y** moves the player forward and back instead of looking, unless `bLook` is held.
  - We don't use the mouse in-game. Our right-stick look goes through `aLookUp` (`JoyR` → `Axis aLookUp`), which still
    works.
- If `bSnapToLevel=True`, the view re-centres pitch while you walk without looking. A non-zero `aLookUp` sets
  `bKeyboardLook`, which suppresses this.
  - Leave `bSnapToLevel=False` and `bLookUpStairs=False`.
- `exec AlwaysMouseLook(B)` and `exec LookUpStairs(B)` toggle these flags from the console.

→ **This conflicts with gyro-as-mouse.** If gyro deltas were sent as SDL mouse motion, the pitch would turn into
forward walking. Feed gyro through **joystick axes** instead (section 3b), which keeps the built-in auto-aim usable.

### 1.5 Enabling it

User.ini, which `GameConfig.kt` would write:

```
[Engine.PlayerPawn]
MyAutoAim=0.97
bAlwaysMouseLook=False
bSnapToLevel=False
bLookUpStairs=False
```

Other ways to set it:
- Console: `SetAutoAim 0.97` and `AlwaysMouseLook False`
- The menu: Options > Game > Auto Aim. This only offers 0.93 or 1.0.

Because `MyAutoAim` is `travel globalconfig`, it persists. If the in-game checkbox is toggled, it overwrites our value
with 0.93 or 1.0, so we must re-apply our value on every launch, as we already do for other keys.

### 1.6 Single-player vs online

- The auto-aim code runs where the weapon fires, which is the server (the authority). The `NM_Standalone` check means it
  only works in purely offline games: practice sessions, botmatch, and the ladder.
- On a listen server you host, and on any online client, it is inert. Joining a server therefore can't give us an
  advantage, and there is nothing an anti-cheat could detect. `MyAutoAim` is a stock, menu-exposed setting.

---

## 2. Existing community work

- **UT99:** no aim-assist or console-style aim mutator was found on UnrealArchive, ModDB or the general web. The
  OldUnreal forum couldn't be searched directly, because results only returned redirects. **[web]**
- **UT3 foxMod:** "subtle auto-aim" that only activates when gamepad input has been used exclusively. This shows the
  idea is accepted, but the code isn't portable to UT99. **[web]**
- **Unreal 1 and UT99:** the stock `MyAutoAim`/`AutoAim` system above is the only known assist. It dates from Unreal 1.
- **Anti-cheat:** UT99 servers run ACE, and older servers run UTPure or NPLoader. They flag client-side packages and
  aim modification. ACE v1.2+ is needed on 469 Linux servers. **[web]** Implications for us:
  - Nothing we add should load on a client connected to a remote server. Any UnrealScript assist must check
    `Level.NetMode == NM_Standalone`, and it should only be added through the URL or mutator list for games we launch
    offline. It should never go in `ServerPackages`, or be auto-spawned through `ServerActors`, for online play.
  - Input-layer features such as response curves and gyro are pure input translation, equivalent to Steam Input or
    DS4Windows gyro-to-stick, and they are invisible to the game. They don't use game state, so they aren't cheats.
    ACE doesn't inspect them.

---

## 3. Approaches we could build

### (a) UnrealScript mutator (offline only)

What script can do (standalone, where server and client are the same process):
- **Find targets.** Iterate `Level.PawnList` (or use `ForEach VisibleCollidingActors`), filter by enemy team and
  `!bHidden` and `Health > 0`, then check `FastTrace`/`LineOfSightTo` and the angle to `vector(ViewRotation)`. That costs
  a handful of pawns per tick, which is cheap.
- **Aim friction / slow-down.** A `Mutator` (or spawned `Info`) `Tick` can't intercept `aTurn`/`aLookUp` before
  `PlayerInput` consumes them, and UT99 has no tick groups or Interactions. It can, however, post-correct `ViewRotation`:
  1. Remember last tick's `ViewRotation`.
  2. Compute the player's yaw and pitch delta.
  3. When a target is inside a small cone and the stick is moving, write back `Last + delta * k` with k ≈ 0.4-0.6.

  This gives one frame of latency at 60-120 fps and is generally imperceptible. Rotation still goes through
  `PlayerPawn`, and in standalone we can assign `ViewRotation` directly. **[inference, needs prototype]**
- **Rotational assist** (follow a strafing target slightly while the stick is held). Use the same hook: add a fraction of
  the target's angular velocity. This feels more "console" but is stronger, so keep it optional.
- **Magnetism.** This is already done by stock `AdjustAim` (section 1). A custom one would need a `PlayerPawn` subclass,
  meaning a different player class, which breaks skins and the menus. Not worth it.
- **Guard:** in `PostBeginPlay`, call `Destroy()` unless `Level.NetMode == NM_Standalone`, and only affect
  `PlayerPawn`s that are not `Spectator`s.
- **Delivery:** a `UTAAssist.u` package plus `.int` in System. Add it to the practice URL (`?Mutator=UTAAssist.AimFriction`)
  or pick it in the UT mutator menu. A mutator ignores the Difficulty and `bAlwaysMouseLook` restrictions of stock
  auto-aim, because it never touches `AdjustAim`.
- **Cost:** we need a UCC toolchain to compile the package. UCC from 469 can run on the PC (Windows build) or through our
  loader. That is the main effort.

### (b) Input layer (`SDL_utandroid.c` and the app)

This layer knows nothing about the world, so it can only make aiming easier to control.

1. **Gyro aiming (recommended).**
   - **Hardware:** the RP6 has an accelerometer and a gyroscope, according to the LineageOS device page and the Retro
     Catalog spec list. **[web]** Confirm on the device with `adb shell dumpsys sensorservice` (look for a
     `TYPE_GYROSCOPE`, ideally a non-wake-up 200-400 Hz sensor) before committing.
   - **Where to read the gyro:**
     - **Kotlin:** `SensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE)` inside our existing `InputBridge`. Integrate
       the angle between sends and push it over the existing input socket as new axis messages
       (`'A', 4/5, value`). This is the simplest option, and it is where the pad events already originate.
     - **Native:** SDL3's sensor API (`SDL_INIT_SENSOR`, `SDL_GetSensors`, `SDL_OpenSensor`, `SDL_GetSensorData`,
       type `SDL_SENSOR_GYRO`, rad/s; the Android backend wraps `ASensorManager`). Available since SDL 3.2.0. **[web]**
       This would work in native-pad mode, but it adds threading and lifecycle complexity in SDL for no gain.
     - Gamepad-embedded sensors (`SDL_GetGamepadSensor…`) don't apply here, because the RP6 pad is built in and the
       sensor is the device's own.
   - **Into UT:**
     1. Raise `UTA_NAXES` from 4 to 6. SDLDrv reads up to 8 axes (`"USDLClient::JoystickAxes <= 8"`). **[strings]**
     2. Bind `JoyU=Axis aTurn Speed=…` and `JoyV=Axis aLookUp Speed=…`. `JoyU` is currently unbound.
     3. Verify on the device that SDLDrv maps axes 4 and 5 to `IK_JoyU`/`IK_JoyV` (expected from the UT key order, not
        yet confirmed).

     Because UT adds joystick axis values once per frame like mouse deltas, the per-frame value must be the **angle
     turned since the last frame**. Either send the angular rate and multiply by the existing `look_scale`, which is
     already rate × dt / (1/60), or, better, accumulate the gyro angle and drain it on each poll. SDLDrv polls twice per
     frame, so drain it on the frame count from `utshim_frame_count` or the shared struct to avoid splitting it.
     - Check the resolution: Sint16 over ±0.5 is ~15 bits, which is fine with a sensible `Speed`.
     - Large flicks clip at the axis maximum, so pick `Speed` so that a full axis value is about 30°/frame.
   - Gyro must not go through mouse motion, because of the `bAlwaysMouseLook` conflict (section 1.4). Mouse motion also
     drives our menu cursor.
   - **Gyro UX** (current handheld best practice, as in Steam Input and JoyShockMapper):
     - Gyro is active only in-game. Our layer already zeroes axes in `menu_mode`.
     - Modes: always on, or a ratchet button that disables while held (for example L3), or enable-while-held.
     - Sensitivity can be real-world 1:1 (camera degrees = device degrees) or a multiplier.
     - A small deadzone or "tightening" around 0.5-1 °/s suppresses jitter, with optional light smoothing only at very
       low speeds.
     - Use yaw from the device's yaw axis, or "player space" yaw (yaw + roll) for handhelds held at an angle.
     - Pitch is optional, and inversion is configurable.
     - Handle `sensorLandscape` flips: read the display rotation and negate X and Y for reverse landscape.
     - Stick and gyro add together. The usual layout is stick for coarse turning and gyro for fine aim.
2. **Stick response tuning.**
   - Exponent curve: currently radial deadzone 0.18 then linear. Offer an exponent of 1.5-2 for finer control near the
     centre.
   - Outer "turn boost": after about 0.25 s at full deflection, ramp the yaw speed up so 180° turns are faster.
   - Optional look slow-down while the fire trigger is held (×0.6-0.8). This is a cheap "steadiness" aid that needs no
     world knowledge.

   These are all a few lines each near the existing `look_scale` code.
3. Pure input-layer "aim assist" (stickiness without target knowledge) isn't possible. Anything that guesses targets
   from pixels would be a screen-reading aimbot. We won't do that.

### (c) Combined mutator → SDL channel

UnrealScript has no IPC or shared-memory access. The only ways out are:
- `Log()`, with our loader parsing stdout
- `SaveConfig()`, ini writes, which hit the disk every frame
- `TcpLink`/`UdpLink` from IpDrv to localhost

All of these are high-latency or hacky, and the UDP version also makes the "offline only" promise harder to audit. It
also buys nothing: a mutator already knows the target and can apply friction itself (approach a), while the input layer
can't do better with the same information one frame late. **Judgement: not worth building.**

---

## 4. Recommended plan

1. **Built-in auto-aim (about an hour, config only).**
   - In `GameConfig.kt`, write `[Engine.PlayerPawn] bAlwaysMouseLook=False`, `bSnapToLevel=False` and
     `bLookUpStairs=False` on every launch.
   - Add an app setting "Aim assist (offline)": Off = 1.0, Light = 0.98, Strong = 0.95. Avoid 0.93, which feels like
     an aimbot with ~22° of slack.
   - Test in a practice session at Average or Adept skill. Fire hitscan and projectiles slightly off-target and confirm
     the hits.
   - Note in the settings: offline only, and not at Skilled or Godlike bot skill.
2. **Gyro aiming (1-2 days).**
   - Confirm the sensor with `dumpsys sensorservice`.
   - In Kotlin, read `TYPE_GYROSCOPE` at `SENSOR_DELAY_GAME`/`FASTEST`, integrate it, and send axes 4 and 5. In
     `SDL_utandroid.c`, raise `UTA_NAXES` to 6 and drain the accumulated angle once per UT frame.
   - Bind `JoyU`/`JoyV` to `aTurn`/`aLookUp`.
   - Add settings: off / always / ratchet button, sensitivity, invert Y, and a yaw-axis choice.
3. **Stick polish (half a day).** Add the response exponent, turn boost, and fire slow-down as settings.
4. **Optional friction mutator (2-3 days including the UCC setup).**
   - `UTAAssist.AimFriction`, standalone only. Small cone (about 6°), k = 0.5, visible enemies only.
   - Inject it into the offline game URL only when the setting is on.
5. **Skip approach (c).**

Throughout, nothing is added to `ServerPackages` or `ServerActors`, and online play stays stock apart from the
gyro and stick translation.

## Sources

- [LineageOS wiki — Retroid Pocket 6 (RP6)](https://wiki.lineageos.org/devices/RP6/) — lists the gyroscope and accelerometer
- [Retro Catalog — Retroid Pocket 6](https://retrocatalog.com/retro-handhelds/retroid-pocket-6)
- [SDL3 SDL_sensor.h](https://github.com/libsdl-org/SDL/blob/db6b6bd75ef5fb1335d192cb4edc11719967f52e/include/SDL3/SDL_sensor.h), [SDL3 CategorySensor](https://wiki.libsdl.org/SDL3/CategorySensor/raw)
- [foxMod (UT3) on UnrealArchive](https://unrealarchive.org/unreal-tournament-3/gametypes/F/foxmod/index.html) — gamepad-only subtle auto-aim, UT3
- [OldUnreal 469b patch notes on UnrealArchive](https://unrealarchive.org/patches-updates/unreal-tournament/patches/patch-469b/index.html) — ACE compatibility notes
