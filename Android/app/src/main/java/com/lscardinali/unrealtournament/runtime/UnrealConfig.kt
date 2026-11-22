package com.lscardinali.unrealtournament.runtime

import android.content.Context
import java.io.File

/**
 * Unreal 227's ini settings (the Unreal counterpart of [GameConfig]). 227 keeps its ini files next to the arm64
 * executable ("Not using preference directory"): files/unreal/SystemARM64/UnrealLinux.ini and User.ini, created from
 * DefaultLinux.ini / DefUser.ini, which 227 looks for in that same folder but which ship in System/.
 *
 *  - [system]: forced every launch (renderer = XOpenGL on GLES with the Adreno settings, SDLDrv fullscreen, PadUI).
 *  - [systemDefaults] / [userDefaults]: applied once per [DEFAULTS_VERSION] (menu changes stick).
 * See docs/unreal-227-spike.md.
 */
object UnrealConfig {
    const val DEFAULTS_VERSION = 2 // 2: 227k's GUI scale keys

    fun system(width: Int, height: Int): Map<String, Map<String, String>> = mapOf(
        "FirstRun" to mapOf("FirstRun" to "227"),
        "Engine.Engine" to mapOf(
            "GameRenderDevice" to "XOpenGLDrv.XOpenGLRenderDevice",
            "WindowedRenderDevice" to "XOpenGLDrv.XOpenGLRenderDevice",
            "AudioDevice" to "ALAudio.ALAudioSubsystem",
            "ViewportManager" to "SDLDrv.SDLClient",
            // UWindow menus (UMenu) with PadUI's controller navigation (the classic menu is UBrowser.UBrowserConsole).
            "Console" to "UMenu.UnrealConsole",
        ),
        "UMenu.UnrealConsole" to mapOf("RootWindow" to "PadUI.PadRootWindow"),
        // XOpenGL on OpenGL ES 3.2 (Adreno): the desktop-only features off. The shim rewrites the rest of the GLSL.
        "XOpenGLDrv.XOpenGLRenderDevice" to mapOf(
            "OpenGLVersion" to "ES",
            "UseShaderDrawParameters" to "False",
            "UseHWClipping" to "False",
            "UseBindlessTextures" to "False",
            // "Adaptive" (the default) fails on Android and XOpenGL falls back to no vsync (~280 fps in menus).
            "UseVSync" to "On",
        ),
        "SDLDrv.SDLClient" to mapOf(
            "UseJoystick" to "True",
            "JoystickNumber" to "0",
            "StartupFullscreen" to "True",
            "FullscreenViewportX" to "$width",
            "FullscreenViewportY" to "$height",
            "WindowedViewportX" to "$width",
            "WindowedViewportY" to "$height",
        ),
    )

    private val guiScale = mapOf("AutoGUIScale" to "False", "ConfiguredGUIScale" to "3.000000", "GUIScale" to "3.000000")

    val systemDefaults: Map<String, Map<String, String>> = mapOf(
        // GUI scale 3x (640x360 on 1080p). 227k (what the setup installs) has UT 469's AutoGUIScale/ConfiguredGUIScale;
        // 227j kept it in GUIScale (1x/2x only). Both written: each version reads its own keys.
        "UMenu.UMenuRootWindow" to guiScale,
        "PadUI.PadRootWindow" to guiScale,
        // 4x MSAA cost ~20 fps at 1080p in the spike (70 -> 89 fps); off by default, the menu can turn it back on.
        "XOpenGLDrv.XOpenGLRenderDevice" to mapOf("UseAA" to "False"),
    )

    /** Same pad layout as UT (SDL_utandroid.c: Joy1 A ... Joy16 D-pad right), commands from 227's own defaults. */
    val userDefaults: Map<String, Map<String, String>> = mapOf(
        "Engine.Input" to mapOf(
            "Joy1" to "Jump",                // A
            "Joy2" to "Duck",                // B
            "Joy3" to "InventoryActivate",   // X   use the selected item
            "Joy4" to "SwitchToBestWeapon",  // Y
            "Joy5" to "PrevWeapon",          // L1
            "Joy6" to "NextWeapon",          // R1
            "Joy7" to "AltFire",             // L2
            "Joy8" to "Fire",                // R2
            "Joy9" to "ActivateTranslator",  // Select  the Universal Translator (story messages)
            "Joy10" to "",                   // Start: Escape (menus), sent by our SDL
            "Joy11" to "Walking",            // L3
            "Joy12" to "CenterView",         // R3
            "Joy13" to "QuickSave",          // D-pad up
            "Joy14" to "ThrowWeapon",        // D-pad down
            "Joy15" to "InventoryPrevious",  // D-pad left   item selection
            "Joy16" to "InventoryNext",      // D-pad right
            "JoyX" to "Axis aStrafe Speed=1500",
            "JoyY" to "Axis aBaseY Speed=-1500",
            "JoyZ" to "Axis aTurn Speed=1600",       // look stick at half range (turn boost headroom), as UT
            "JoyR" to "Axis aLookUp Speed=-1100",
            "JoyU" to "Axis aTurn Speed=16000",      // gyro: angle per frame, 16000 = the tuned 100% (PadGyroClient)
            "JoyV" to "Axis aLookUp Speed=16000",
            "JoyPovUp" to "", "JoyPovDown" to "", "JoyPovLeft" to "", "JoyPovRight" to "",
        ),
        "Engine.PlayerPawn" to mapOf(
            "DodgeClickTime" to "-1.000000",  // no double-tap dodge from stick flicks
            "bAlwaysMouseLook" to "False",
            "bSnapToLevel" to "False",
            "bLookUpStairs" to "False",
            "MyAutoAim" to "0.980000",
            "bNeverAutoSwitch" to "False",
        ),
    )

    /** [unreal]: files/unreal (System/, SystemARM64/, ...). */
    fun applyAll(context: Context, unreal: File, width: Int, height: Int) {
        val arm = File(unreal, "SystemARM64")
        val system = File(unreal, "System")
        for (name in listOf("DefaultLinux.ini", "DefUser.ini")) {
            val there = File(arm, name)
            if (!there.exists() && File(system, name).exists()) File(system, name).copyTo(there)
        }
        installPadUI(context, unreal)
        val ini = File(arm, "UnrealLinux.ini")
        val user = File(arm, "User.ini")
        GameConfig.apply(ini, File(arm, "DefaultLinux.ini"), system(width, height))
        GameConfig.apply(user, File(arm, "DefUser.ini"), emptyMap())
        // The version lives in the ini itself, not in the app's preferences: a reinstall of the game (setup) replaces
        // the ini files, and must get the defaults again.
        val have = Regex("""(?m)^DefaultsVersion=(\d+)""").find(ini.readText())?.groupValues?.get(1)?.toInt() ?: 0
        if (have < DEFAULTS_VERSION) {
            GameConfig.apply(ini, File(arm, "DefaultLinux.ini"), systemDefaults)
            GameConfig.apply(user, File(arm, "DefUser.ini"), userDefaults)
            GameConfig.apply(ini, File(arm, "DefaultLinux.ini"),
                             mapOf("UTAndroid" to mapOf("DefaultsVersion" to "$DEFAULTS_VERSION")))
            android.util.Log.i("UTAndroid", "Unreal ini defaults v$DEFAULTS_VERSION applied")
        }
    }

    /** PadUI built against 227 (native/padui/build-padui-227.sh -> assets/padui-227). */
    private fun installPadUI(context: Context, unreal: File) {
        for ((asset, dest) in listOf("padui-227/PadUI.u" to File(unreal, "System/PadUI.u"),
                                     "padui-227/PadUI.int" to File(unreal, "SystemLocalized/int/PadUI.int"))) {
            dest.parentFile?.mkdirs()
            val tmp = File(dest.path + ".tmp")
            context.assets.open(asset).use { input -> tmp.outputStream().use { input.copyTo(it) } }
            dest.delete()
            tmp.renameTo(dest)
        }
    }
}
