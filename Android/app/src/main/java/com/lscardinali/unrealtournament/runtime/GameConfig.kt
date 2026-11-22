package com.lscardinali.unrealtournament.runtime

import android.content.Context
import java.io.File

/**
 * UT's ini settings, applied before every launch ([applyAll]):
 *  - [system]: technical essentials, forced every launch (renderer, joystick on, resolution, first-run done).
 *  - [userDefaults] / [systemDefaults]: controller bindings and handheld-friendly preferences, applied ONCE per
 *    [DEFAULTS_VERSION] so changes made in UT's own menus stick. Bumping the version re-applies the whole set.
 *    Every key was checked against the 469f game files (research/controller-defaults.md, small-screen-ui.md,
 *    aim-assist.md).
 *
 * UT469 on Linux keeps them in $HOME/.utpg/System/{UnrealTournament,User}.ini; on first run it creates them from
 * System/Default.ini and DefUser.ini, so when they don't exist yet we seed them from those first.
 *
 * Joystick layout (our SDL's virtual pad, see SDL_utandroid.c / InputBridge.kt):
 *   Joy1 A, Joy2 B, Joy3 X, Joy4 Y, Joy5 L1, Joy6 R1, Joy7 L2, Joy8 R2, Joy9 Select, Joy10 Start,
 *   Joy11 L3, Joy12 R3, Joy13 DUp, Joy14 DDown, Joy15 DLeft, Joy16 DRight
 *   JoyX/JoyY left stick, JoyZ/JoyR right stick (SDLDrv delivers -0.5..+0.5).
 */
object GameConfig {
    fun system(width: Int, height: Int): Map<String, Map<String, String>> = mapOf(
        // 469 = first-run setup done: skips the "choose a 3D video device" dialog.
        "FirstRun" to mapOf("FirstRun" to "469"),
        "Engine.Engine" to mapOf(
            "GameRenderDevice" to "VulkanDrv.VulkanRenderDevice",
            "WindowedRenderDevice" to "VulkanDrv.VulkanRenderDevice",
            "RenderDevice" to "VulkanDrv.VulkanRenderDevice",
            "AudioDevice" to "ALAudio.ALAudioSubsystem",
            "ViewportManager" to "SDLDrv.SDLClient",
        ),
        "SDLDrv.SDLClient" to mapOf(
            // SDLDrv writes UseJoystick=False when it can't open a pad at startup: force it back on every launch.
            "UseJoystick" to "True",
            "JoystickNumber" to "0",
            "IgnoreHat" to "True",
            "StartupFullscreen" to "True",
            "FullscreenViewportX" to "$width",
            "FullscreenViewportY" to "$height",
            "WindowedViewportX" to "$width",
            "WindowedViewportY" to "$height",
        ),
        // UT's Options > Input "Joystick" checkbox reads this section, not SDLDrv's: keep it in step so it shows on.
        "WinDrv.WindowsClient" to mapOf("UseJoystick" to "True"),
        // PadUI (native/padui): controller focus box + D-pad navigation for UWindow dialogs. Its GUI scale lives in
        // [PadUI.PadRootWindow] (defaults 2.0 from the class).
        "UTMenu.UTConsole" to mapOf("RootWindow" to "PadUI.PadRootWindow"),
        "VulkanDrv.VulkanRenderDevice" to mapOf(
            "UseVSync" to "True", // the stick speeds below assume ~60 fps
        ),
    )

    /** Bump to re-apply [userDefaults] and [systemDefaults] on existing installs (overwrites the user's changes). */
    const val DEFAULTS_VERSION = 5 // 2: AutoAim floor; 3: view bob back; 4: GUI scale 3.0; 5: gyro axes

    val systemDefaults: Map<String, Map<String, String>> = mapOf(
        // UWindow menus at 3x, the slider's maximum (640x360 virtual desktop on 1080p). Auto scaling reads ~96 DPI on
        // Android and stays 1x. PadUI's root window keeps its own copy of the setting.
        "UMenu.UMenuRootWindow" to mapOf("AutoGUIScale" to "False", "ConfiguredGUIScale" to "3.000000"),
        "PadUI.PadRootWindow" to mapOf("AutoGUIScale" to "False", "ConfiguredGUIScale" to "3.000000"),
        // The game's auto-aim floor: at startup MyAutoAim = FMax(Level.Game.AutoAim, MyAutoAim), so 1.0 here (as the
        // shipped ini had) turns the player's auto-aim off and saves it back as 1.0. 0.93 allows up to UT's "strong".
        "Engine.GameInfo" to mapOf("AutoAim" to "0.930000"),
    )

    val userDefaults: Map<String, Map<String, String>> = mapOf(
        "Engine.Input" to mapOf(
            "Joy1" to "Jump",                          // A
            "Joy2" to "Duck",                          // B
            "Joy3" to "GetWeapon BotPack.Translocator", // X
            "Joy4" to "ThrowWeapon",                   // Y
            "Joy5" to "PrevWeapon",                    // L1
            "Joy6" to "NextWeapon",                    // R1
            "Joy7" to "AltFire",                       // L2
            "Joy8" to "Fire",                          // R2
            "Joy9" to "ShowScores",                    // Select
            "Joy10" to "",                             // Start: sent as the Escape key by our SDL (menu / skip intro)
            "Joy11" to "Walking",                      // L3
            "Joy12" to "FeignDeath",                   // R3
            "Joy13" to "SwitchWeapon 8",               // DUp    Flak
            "Joy14" to "SwitchWeapon 4",               // DDown  Shock
            "Joy15" to "SwitchWeapon 10",              // DLeft  Sniper
            "Joy16" to "SwitchWeapon 9",               // DRight Rocket
            "JoyX" to "Axis aStrafe Speed=1500",
            "JoyY" to "Axis aBaseY Speed=-1500",
            "JoyZ" to "Axis aTurn Speed=800",
            "JoyR" to "Axis aLookUp Speed=-550",
            // Gyro aiming (GyroAim.kt -> SDL_utandroid.c axes 4/5): the axes carry an angle per frame, so Speed must
            // match GYRO_AXIS_SPEED there.
            "JoyU" to "Axis aTurn Speed=1000",
            "JoyV" to "Axis aLookUp Speed=1000",
            "JoyPovUp" to "", "JoyPovDown" to "", "JoyPovLeft" to "", "JoyPovRight" to "",
        ),
        "Engine.PlayerPawn" to mapOf(
            // No double-tap dodge (stick flicks through the dead zone triggered it); same value the Game menu writes.
            "DodgeClickTime" to "-1.000000",
            // UT's own auto-aim (offline games only, below Skilled bots): needs bAlwaysMouseLook off, and no view
            // re-centring. 0.98 = light (~11 degree cone); the menu checkbox writes 0.93 (strong), 1.0 = off.
            "bAlwaysMouseLook" to "False",
            "bSnapToLevel" to "False",
            "bLookUpStairs" to "False",
            "MyAutoAim" to "0.980000",
            "bNeverAutoSwitch" to "False",
            // 469 widescreen FOV: 90 at 4:3 -> ~106 at 16:9, weapon model scaled to match.
            "FOVFix" to "2",
            "DesiredNarrowFOV" to "90.000000",
            "WeaponFOVScale" to "0.670000",
            "Bob" to "0.016000", // UT's default view bob (halving it was tried and reverted)
        ),
        "Engine.Pawn" to mapOf("bNeverSwitchOnPickup" to "False"),
    )

    /** Forced settings every launch, the defaults once per [DEFAULTS_VERSION]. [iniDir]: $HOME/.utpg/System. */
    fun applyAll(context: Context, iniDir: File, systemDir: File, width: Int, height: Int) {
        val utIni = File(iniDir, "UnrealTournament.ini")
        val userIni = File(iniDir, "User.ini")
        val defaultIni = File(systemDir, "Default.ini")
        val defUserIni = File(systemDir, "DefUser.ini")
        apply(utIni, defaultIni, system(width, height))
        apply(userIni, defUserIni, emptyMap()) // seeds User.ini on first run
        val prefs = context.getSharedPreferences("utandroid", Context.MODE_PRIVATE)
        if (prefs.getInt("iniDefaultsVersion", 0) < DEFAULTS_VERSION) {
            apply(utIni, defaultIni, systemDefaults)
            apply(userIni, defUserIni, userDefaults)
            prefs.edit().putInt("iniDefaultsVersion", DEFAULTS_VERSION).apply()
            android.util.Log.i("UTAndroid", "UT ini defaults v$DEFAULTS_VERSION applied")
        }
    }

    /** Seeds [ini] from [default] if missing, then sets every key of [settings]. */
    fun apply(ini: File, default: File, settings: Map<String, Map<String, String>>) {
        if (!ini.exists() && default.exists()) {
            ini.parentFile?.mkdirs()
            default.copyTo(ini)
        }
        val text = if (ini.exists()) ini.readText() else ""
        ini.parentFile?.mkdirs()
        ini.writeText(set(text, settings))
    }

    /** Minimal ini editor: keeps everything it doesn't touch, line endings included. */
    internal fun set(text: String, settings: Map<String, Map<String, String>>): String {
        val eol = if (text.contains("\r\n")) "\r\n" else "\n"
        val lines = text.split(Regex("\r?\n")).toMutableList()
        if (lines.isNotEmpty() && lines.last().isEmpty()) lines.removeAt(lines.size - 1)
        for ((section, keys) in settings) {
            var start = lines.indexOfFirst { it.trim().equals("[$section]", ignoreCase = true) }
            if (start < 0) {
                lines += ""
                lines += "[$section]"
                start = lines.size - 1
            }
            var end = start + 1
            while (end < lines.size && !lines[end].trimStart().startsWith("[")) end++
            for ((key, value) in keys) {
                val idx = (start + 1 until end).firstOrNull {
                    lines[it].substringBefore('=').trim().equals(key, ignoreCase = true)
                }
                if (idx != null) {
                    lines[idx] = "$key=$value"
                } else {
                    // insert after the section's last non-blank line
                    var at = end
                    while (at > start + 1 && lines[at - 1].isBlank()) at--
                    lines.add(at, "$key=$value")
                    end++
                }
            }
        }
        return lines.joinToString(eol) + eol
    }
}
