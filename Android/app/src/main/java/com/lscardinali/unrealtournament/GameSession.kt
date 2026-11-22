package com.lscardinali.unrealtournament

import android.content.Context
import android.util.Log
import com.lscardinali.unrealtournament.bridge.AudioBridge
import com.lscardinali.unrealtournament.bridge.InputBridge
import com.lscardinali.unrealtournament.runtime.GameConfig
import com.lscardinali.unrealtournament.runtime.GlibcProgram
import com.lscardinali.unrealtournament.x11.XServer
import java.io.File

/**
 * One run of the game: X server → window manager → bridges → ini → ut-bin-arm64.
 * Logs go to filesDir/logs (x11.log, wm.log, ut.log) and UT's own log to home/.utpg/System/UnrealTournament.log.
 */
class GameSession(private val context: Context) {
    private val rt = GlibcProgram(context)
    private val logs = File(rt.files, "logs").apply { mkdirs() }
    @Volatile private var game: Process? = null
    @Volatile private var wm: Process? = null

    /** Blocking; returns the game's exit code. Call off the main thread. */
    /**
     * [envOverrides]: dev knobs from the launch intent ("env" extra, "K=V,K2=V2"); an empty value removes a default,
     * e.g. "TU_DEBUG=" to A/B Turnip's sysmem rendering.
     */
    fun run(width: Int, height: Int, args: List<String> = emptyList(), envOverrides: Map<String, String> = emptyMap(),
            refreshHz: Int = 60): Int {
        check(rt.loader.exists()) { "missing loader ${rt.loader}" }
        check(File(rt.systemDir, "ut-bin-arm64").exists()) { "game not installed in ${rt.gameDir}" }

        XServer.start(context, File(logs, "x11.log"))
        val x11 = XServer.clientEnvironment(context)

        File(rt.binDir, "utandroid-wm").takeIf { it.exists() }?.let {
            wm = rt.start(it, emptyList(), File(logs, "wm.log"), extraEnv = x11)
        }

        val inputSocket = File(rt.tmpDir, "ut-input")
        val audioSocket = File(rt.tmpDir, "ut-audio")
        InputBridge.start(context, inputSocket)
        AudioBridge.start(audioSocket)

        // UT keeps Running.ini while it runs and, finding it at startup, asks about "safe mode". Android kills the game
        // routinely (swiped away, low memory), so that question would come up after most sessions.
        for (marker in listOf("Running.ini", "UnrealTournament.crash")) File(rt.utIniDir, marker).delete()
        File(rt.gameDir, "Save").mkdirs() // UT's SavePath=../Save (the install doesn't ship the folder)

        GameConfig.applyAll(context, rt.utIniDir, rt.systemDir, width, height)

        val env = x11 + mapOf(
            "SDL_VIDEO_DRIVER" to "x11",
            "SDL_AUDIO_DRIVER" to "utandroid",
            "UTANDROID_FRAMES" to framesFile(context).path,
            // Frame pacing (utshim): one frame per vsync of the panel, sent LEAD before it (VsyncWriter feeds vsync).
            "UTANDROID_PACE_FPS" to "$refreshHz",
            // Send each frame 5.5 ms before its vsync. Measured on screen (SurfaceFlinger, native/tools/sf_latency.py):
            // 4 ms -> 11% repeated frames, 5.5 ms -> 0%, 7 ms -> 0.4%, pacing off -> 2.6%.
            "UTANDROID_PACE_LEAD_US" to "5500",
            "SDL_LOGGING" to "input=info",
            "UTANDROID_INPUT" to inputSocket.path,
            "UTANDROID_AUDIO" to audioSocket.path,
            "ALSOFT_DRIVERS" to "sdl3",
            "VK_DRIVER_FILES" to File(rt.glibcDir, "freedreno_icd.json").path,
            // No TU_DEBUG=sysmem: Turnip renders in Adreno's on-chip tile memory (GMEM). Measured on the RP6:
            // 112-117 -> 118-121 fps, worst frame 10-13 -> 10-11 ms, no visible artifacts. Re-enable per launch with
            // the "env" extra (TU_DEBUG=sysmem) if a map shows corruption.
            // Turnip presents to Termux:X11 by copying (MIT-SHM): lorie only takes AHB-backed DRI3 buffers.
            "MESA_VK_WSI_DEBUG" to "sw",
            "MESA_SHADER_CACHE_DIR" to File(rt.files, "cache/mesa").path,
        )
        Log.i(TAG, "Starting UT ${width}x$height ${args.joinToString(" ")}")
        val finalEnv = (env + envOverrides).filterValues { it.isNotEmpty() }
        if (envOverrides.isNotEmpty()) Log.i(TAG, "Env overrides: $envOverrides")
        val p = rt.startGame("ut-bin-arm64", args, File(logs, "ut.log"), finalEnv)
        game = p
        val code = p.waitFor()
        Log.i(TAG, "UT exited: $code")
        wm?.destroy()
        return code
    }

    fun stop() {
        game?.destroy()
        wm?.destroy()
    }

    companion object {
        const val TAG = "UTAndroid"

        /** Frame counter written by utshim (presented frames), read by PerfOverlay. */
        fun framesFile(context: Context) = File(context.filesDir, "tmp/ut-frames")
    }
}
