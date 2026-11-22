# Gamepad / handheld default settings for UT99 469f

All findings below were checked against the UnrealScript source embedded in the shipped 469f packages
(`linux-arm64/System/Engine.u`, `Botpack.u`, `UMenu.u`; identical to the Windows copies) and against the
shipped ini files (`linux-arm64/System/Default.ini`, `DefUser.ini`, `UnrealTournament/System/User.ini`,
`UnrealTournament.ini`). Script source was pulled out with `strings -n 3 Engine.u` etc.; "Engine.u:Lnnnn" below
refers to line numbers in that dump.

Where things live: `PlayerPawn`, `Pawn`, `HUD`, `ChallengeHUD`, `TournamentPlayer` and `ChallengeBotInfo` are
`config(user)` → **User.ini**. `GameInfo`, `DeathMatchPlus` and `Player` (net speed) → **UnrealTournament.ini**.
`globalconfig` vars are saved under the class that *declares* them (e.g. `DodgeClickTime` is under
`[Engine.PlayerPawn]` even though the player is a `Botpack.TournamentPlayer`).

---

## 1. Disable double-tap dodge — `[Engine.PlayerPawn] DodgeClickTime=-1.0` (User.ini)

**There is no `bEnableDodging`, `bDisableDodge`, `bNoDodge` or other 469-specific switch** (searched Engine.u,
Botpack.u, UnrealShare.u, UMenu.u, UTMenu.u, OldUnreal469c.u: zero hits). The only control is `DodgeClickTime`:

```
var(Movement) globalconfig float DodgeClickTime;           // Engine.u, class PlayerPawn
...
if (DodgeClickTime > 0.0)        // PlayerWalking.PlayerMove: whole dodge detector is skipped when <= 0
{ ... if (bEdgeForward && bWasForward) DodgeDir = DODGE_Forward; ... }
...
DodgeClickTime = FMin(0.3, DodgeClickTime);   // PostBeginPlay/TravelPostAccept: clamped to max 0.3, no lower bound
```

The stock menu (Options > Preferences > Game > "Dodging" checkbox, `UMenuGameOptionsClientWindow`) does exactly this:
`SetDodgeClickTime(0.25)` when checked, `SetDodgeClickTime(-1.0)` when unchecked, and shows the box as checked
when `DodgeClickTime > 0`. So **use -1.0**, which keeps the menu in sync. Console equivalent: `SetDodgeClickTime -1`.

Why sticks trigger it: the edge detector is `bEdgeForward = (bWasForward ^^ (aBaseY > 0))` and
`bEdgeLeft = (bWasLeft ^^ (aStrafe > 0))` — any non-zero analog value counts as "pressed". Releasing the stick into
the dead zone and pushing it again within 0.25 s (a flick, or a stick bouncing off center) is a double tap. Our 0.18
radial dead zone in InputBridge.kt doesn't prevent that.

Caveats:
- Works online too: dodge detection runs client-side in `PlayerMove`; the server only receives the resulting
  `DodgeMove` through `ServerMove`. The server cannot force it on.
- Stock UT has no "dodge" exec/button command, so with this off the player cannot dodge at all. (A dedicated dodge
  button would need a mutator/our own input trick: e.g. inject two quick direction pulses. Not in scope.)
- 469 fixed the dodge-timer-vs-ping bug (#805) — irrelevant once disabled.

## 2. Auto-aim — exists, but only offline, and **disabled by `bAlwaysMouseLook=True`**

Settings that exist:
| Key | File | Meaning |
|---|---|---|
| `[Engine.PlayerPawn] MyAutoAim` | User.ini | player's auto-aim cone, a cosine. `1.0` = off. `0.93` (default) ≈ 21.6° half-angle. Larger = weaker (0.97 ≈ 14°, 0.99 ≈ 8°). |
| `[Engine.GameInfo] AutoAim` | UnrealTournament.ini | game-side floor, comment: "How much autoaiming to do (1 = none, 0 = always)". Default 0.93. `MyAutoAim = FMax(MyAutoAim, Level.Game.AutoAim)` so the player can't be stronger than this. |

There is **no `bAutoAim`**. Menu: Options > Preferences > Input > "Auto Aim" checkbox → `ChangeAutoAim(0.93)` / `ChangeAutoAim(1.0)`.
Console: `SetAutoAim 0.93`.

The decisive code (`PlayerPawn.AdjustAim`, used by every Botpack weapon via `PawnOwner.AdjustAim(...)`,
TournamentPlayer does not override it):

```
bestAim = FMin(0.93, MyAutoAim);
BestTarget = PickTarget(bestAim, bestDist, FireDir, projStart);
if ( (Level.NetMode != NM_Standalone) || (Level.Game.Difficulty > 2)
    || bAlwaysMouseLook || ((BestTarget != None) && (bestAim < MyAutoAim)) || (MyAutoAim >= 1) )
    return ViewRotation;                       // no assist
...
AimSpot = projStart + FireDir * bestDist;
AimSpot.Z = BestTarget.Location.Z + 0.3 * BestTarget.CollisionHeight;   // only the HEIGHT is corrected
return rotator(AimSpot - projStart);
```

So in practice:
- **Never online** (`NetMode != NM_Standalone`; also listen servers). Practice/botmatch/ladder are standalone → OK.
- **Never with `bAlwaysMouseLook=True`** — which is UT's default and what GameConfig currently forces. That's why
  auto-aim "does nothing" in UT even though the checkbox is on.
- Only when `Level.Game.Difficulty <= 2`. DeathMatchPlus maps bot skill 0..7 to Difficulty with
  `if (Difficulty >= 4) Difficulty -= 4`, so it works at Novice/Average/Experienced and Adept/Masterful/Inhuman, but
  **not at Skilled (3) or Godlike (7)** — a quirk, not a design.
- It is **vertical-only** assist: yaw is untouched, the pitch is snapped to 30% above the target's center. That's
  exactly the hard axis on a gamepad, so it is genuinely useful vs bots.

To get it, the view-centering behaviour that `bAlwaysMouseLook=False` would normally bring has to be switched off:
`ChangeSnapView`/lookspring code only re-centers pitch when `bSnapToLevel && !bAlwaysMouseLook` (or bLookUpStairs).
With `bSnapToLevel=False` and `bLookUpStairs=False`, right-stick pitch (`Axis aLookUp`) behaves exactly as now
(the only other effect of `bAlwaysMouseLook=False` is that *mouse* Y moves forward/back; joystick axes are unaffected).

Recommended (offline aim assist on):
```
[Engine.PlayerPawn]
bAlwaysMouseLook=False
bSnapToLevel=False
bLookUpStairs=False
MyAutoAim=0.930000
```
If you prefer pure PC behaviour, keep `bAlwaysMouseLook=True` (assist silently off). Verify on device that the
pitch doesn't drift back to level while walking — by the code it shouldn't.

## 3. Other handheld-friendly defaults

### Weapon auto-switch on pickup → ON (User.ini)
```
[Engine.PlayerPawn] bNeverAutoSwitch=False
[Engine.Pawn]       bNeverSwitchOnPickup=False
```
Both exist (Engine.u: `var globalconfig bool bNeverAutoSwitch` in PlayerPawn, `bNeverSwitchOnPickup` in Pawn; the
Windows User.ini has both set to True — that copy came from a PC player). With only 4 D-pad slots + L1/R1 cycling,
switching to a newly picked-up better weapon (by `WeaponPriority[]`) saves a lot of fumbling. Console:
`NeverSwitchOnPickup False` (sets both + tells the server). Online: `Possess()` on a client calls
`NeverSwitchOnPickup(bNeverAutoSwitch)` → `ServerNeverSwitchOnPickup`, so the client pref is honoured by servers.

### FOV: 469 Hor+ widescreen scaling (User.ini)
```
[Engine.PlayerPawn]
FOVFix=2
WeaponFOVScale=0.670000
DesiredNarrowFOV=90.000000
```
469 adds `FOVFix` / `DesiredNarrowFOV` / `WeaponFOVScale` (Engine.u comment "OldUnreal wide-screen support";
ReleaseNotes.md, 469e: "widescreen-friendly Hor+ Field of View (FOV) scaling"). `DesiredNarrowFOV` is the 4:3-equivalent angle;
`PreRender` recomputes `DefaultFOV/DesiredFOV = atan(tan(N/2)·0.75·W/H)·2` whenever the canvas size changes, so
there's no need to write `DefaultFOV`. 90 at 16:9 → 106.3° horizontal.
`FOVFix=2, WeaponFOVScale=0.67` is exactly what the in-game Video menu sets when you pick Hor+.
On a ~6" screen a wide FOV makes distant enemies tiny; 85 (→101° at 16:9) is a reasonable alternative.
Caveat: online the narrow FOV is clamped to the server's `[Botpack.DeathMatchPlus] MinFOV/MaxFOV` (default 80..130)
unless `bAllowFOV`; 85–90 is always allowed. Console: `SetNarrowFOV 90`.

### View bob → halve (User.ini)
`[Engine.PlayerPawn] Bob=0.008000` (default 0.016; `UpdateBob` clamps to 0..0.032; menu slider Game > View Bob).
Less screen motion on a small screen held close; purely cosmetic, client-side. `0` is fine too.

### Things that need NO change
- **Always run**: UT always runs; `Walking` = `Button bRun` is a hold, so L3 must be *held* to walk. No `bAlwaysRun` exists.
- **Mouse smoothing** (`bNoMouseSmoothing`, `bMaxMouseSmoothing`, `MouseSmoothThreshold`): only touch `aMouseX/Y`;
  joystick axes bypass it. Defaults (smoothing off) are fine.
- **HUD scale**: `[Botpack.ChallengeHUD] HudScale=1.0` (DefUser default) — HUD already scales with resolution
  (`Scale = HUDScale*ClipX/1280`), so 1.0 is the PC-equivalent size; don't shrink it on a phone-sized screen
  (the Windows User.ini has 0.6 — don't copy that).
- **Crosshair**: `bAutoCrosshairScale=True` gives `clamp(int(0.1+ClipX/640),1,2)` = 2× at 1920 wide — readable.
  If you want it bigger: `bAutoCrosshairScale=False`, `CrosshairScale=2.5`. `[Engine.HUD] Crosshair=n` picks the shape.
- **Bot skill**: `[Botpack.ChallengeBotInfo] Difficulty=1` (Average, User.ini) is already the default. Optional:
  `bAdjustSkill=True` lets bots adapt to the player. Note the auto-aim quirk at Skilled/Godlike above.
- **Net speed**: `[Engine.Player] ConfiguredInternetSpeed=20000` / `ConfiguredLanSpeed=20000` in UnrealTournament.ini
  (469 defaults; servers cap it anyway). Fine for Wi-Fi.
- **Behind view / FeignDeath / bInstantRocket**: personal taste; `[Botpack.TournamentPlayer] bInstantRocket=True`
  makes a trigger tap fire one rocket instead of starting a load — optional.

## 4. Implementation note for GameConfig

`GameConfig.apply` rewrites every key on **every launch**, so anything put in `user` can never be changed from the
in-game menu (it snaps back next start). Control-critical keys (Joy bindings, `UseJoystick`) should stay forced;
the preference-type defaults above are better written **once** (e.g. a `defaultsVersion` flag in SharedPreferences:
apply a `userDefaults` map only when the stored version is older, then bump it). "Only if key absent" won't work
reliably because UT's first `SaveConfig` writes all PlayerPawn keys.

```kotlin
/** Preference defaults: apply once per defaultsVersion, not every launch (so the in-game menus still work). */
val userDefaults: Map<String, Map<String, String>> = mapOf(
    "Engine.PlayerPawn" to mapOf(
        "DodgeClickTime" to "-1.000000",    // dodge off (same value the Game menu's Dodging checkbox writes)
        "bAlwaysMouseLook" to "False",      // required for UT's (offline-only, vertical) auto-aim
        "bSnapToLevel" to "False",          //  ...and these two keep pitch from re-centering
        "bLookUpStairs" to "False",
        "MyAutoAim" to "0.930000",          // 1.0 = off; can't go below [Engine.GameInfo] AutoAim
        "bNeverAutoSwitch" to "False",      // switch to better weapon on pickup
        "FOVFix" to "2",                    // 469 Hor+ widescreen FOV
        "WeaponFOVScale" to "0.670000",
        "DesiredNarrowFOV" to "90.000000",  // 4:3-equivalent; ~106 deg at 16:9
        "Bob" to "0.008000",
    ),
    "Engine.Pawn" to mapOf("bNeverSwitchOnPickup" to "False"),
)
```
(If you keep the current "force every launch" model, drop `"bAlwaysMouseLook" to "True"` from `user`, or it will
override the line above.)


## Applied (2026-11-22, GameConfig DEFAULTS_VERSION 2)

`GameConfig.applyAll`: technical essentials forced every launch; bindings + these defaults applied once per
`DEFAULTS_VERSION` (SharedPreferences `iniDefaultsVersion`), so in-game changes stick.
User.ini: DodgeClickTime=-1, bAlwaysMouseLook/bSnapToLevel/bLookUpStairs=False, MyAutoAim=0.98 (light),
bNeverAutoSwitch=False, FOVFix=2, DesiredNarrowFOV=90, WeaponFOVScale=0.67, Bob=0.016 (UT default; 0.008 tried and reverted in v3), bNeverSwitchOnPickup=False.
UnrealTournament.ini: [UMenu.UMenuRootWindow] AutoGUIScale=False, ConfiguredGUIScale=2.0;
**[Engine.GameInfo] AutoAim=0.93** — found on device: the shipped value was 1.0, and at startup the engine does
`MyAutoAim = FMax(Level.Game.AutoAim, MyAutoAim)` and saves it, which silently turned the player's 0.98 back to 1.0.
Verified on device: values present after UT started (MyAutoAim stays 0.98).
