package com.lscardinali.unrealtournament.x11

import android.content.Context
import android.content.Intent
import com.termux.x11.MainActivity

/**
 * The game screen: Termux:X11's own display activity (com.termux.x11.MainActivity, from :lorie), used as-is like
 * pocket-foundry does. It shows the X screen in a LorieView (a SurfaceView the server renders into), turns touch,
 * mouse, keyboard and gamepad events into X input, and connects to the server when CmdEntryPoint announces it.
 * AndroidManifest.xml hides its launcher entry and keeps it in landscape.
 *
 * Termux:X11 reads its settings from the app's default SharedPreferences; [configure] writes the ones a fullscreen
 * landscape game wants.
 */
object XDisplay {
    /** How large the X screen is. NATIVE: the panel's size (1920x1080 on a Retroid Pocket 6). */
    sealed interface Resolution {
        data object Native : Resolution
        /** The panel's size × 100 / [percent] (Termux:X11's "scaled" mode, 30..300). */
        data class Scaled(val percent: Int) : Resolution
        /** A fixed size, letterboxed. Must be one of Termux:X11's displayResolution entries (e.g. "1280x720"). */
        data class Exact(val size: String) : Resolution
    }

    fun configure(context: Context, resolution: Resolution = Resolution.Native) {
        val prefs = context.getSharedPreferences("${context.packageName}_preferences", Context.MODE_PRIVATE)
        prefs.edit().apply {
            putString("forceOrientation", "landscape")  // patch 0005: either way up
            putBoolean("fullscreen", true)
            putBoolean("hideCutout", true)
            putBoolean("showAdditionalKbd", false)
            putBoolean("additionalKbdVisible", false)
            putString("touchMode", "3")                  // Direct touch: a tap clicks where it lands
            putString("screenIdleTimeout", "never")
            // Hardware keys straight to X as held keys (WASD), not through an input method.
            putBoolean("hardwareKbdScancodesWorkaround", false)
            putBoolean("enforceCharBasedInput", false)
            when (resolution) {
                Resolution.Native -> putString("displayResolutionMode", "native")
                is Resolution.Scaled -> putString("displayResolutionMode", "scaled").putInt("displayScale", resolution.percent)
                is Resolution.Exact -> putString("displayResolutionMode", "exact")
                    .putString("displayResolutionExact", resolution.size)
            }
        }.apply()
    }

    /** Opens the X screen. Start [XServer] first (or right after: the activity shows "Not connected" until then). */
    fun show(context: Context) {
        val intent = Intent(context, MainActivity::class.java)
        if (context !is android.app.Activity) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }
}
