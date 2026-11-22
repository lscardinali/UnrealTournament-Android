package com.lscardinali.unrealtournament.x11

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import kotlin.concurrent.thread

/**
 * Termux:X11's X server (third_party/termux-x11, module :lorie), run as its own process the way pocket-foundry's
 * Session.startXServer does: `app_process` loads com.termux.x11.CmdEntryPoint from our own APK (CLASSPATH), which
 * loads libXlorie.so (XLORIE_LIB, patch 0001) and starts Xorg's dix on display :[DISPLAY_NUMBER].
 *
 * The server listens on the unix socket [socket] (`$TMPDIR/.X11-unix/X0`; Android has no /tmp). Once it is up it
 * announces itself to the app with a broadcast (CmdEntryPoint.ACTION_START, received by LorieApp.Receiver), and
 * Termux:X11's display activity ([XDisplay.show]) connects to it and shows the screen.
 *
 * A glibc X client reaches it with `DISPLAY=<socket path>` ([clientEnvironment]): libxcb takes a DISPLAY that starts
 * with '/' as the socket itself, so the client needs no /tmp either.
 *
 * Blocking: [start] waits for the socket, so call it off the main thread.
 */
object XServer {
    private const val TAG = "UTX11"
    /** Termux:X11's display :0. */
    const val DISPLAY_NUMBER = 0
    /** Android's ART without its boot image can take several seconds to reach the server's main. */
    private const val START_TIMEOUT_MS = 30_000L
    private const val POLL_MS = 50L

    @Volatile private var process: Process? = null

    /**
     * The server's TMPDIR, which holds its socket and lock. Must be the same directory the game gets as TMPDIR
     * (runtime/GlibcProgram.tmpDir, filesDir/tmp), so both agree on the socket.
     */
    fun tmpDir(context: Context) = File(context.filesDir, "tmp")

    /** The X socket: what DISPLAY names for a glibc client. */
    fun socket(context: Context) = File(tmpDir(context), ".X11-unix/X$DISPLAY_NUMBER")
    private fun lock(context: Context) = File(tmpDir(context), ".X$DISPLAY_NUMBER-lock")

    /**
     * xkeyboard-config data the server compiles its keymap from (XKB_CONFIG_ROOT). Not in the APK yet: ship it with
     * the game data (pocket-foundry puts share/X11/xkb in its runtime). The server also finds it on its own at
     * dirname(TMPDIR)/usr/share/X11/xkb, which is this path.
     */
    fun xkbRoot(context: Context) = File(context.filesDir, "usr/share/X11/xkb")

    /** Extracted by the installer: the app packages native libraries uncompressed-legacy (useLegacyPackaging). */
    fun xlorieLib(context: Context) = File(context.applicationInfo.nativeLibraryDir, "libXlorie.so")

    val isRunning get() = process?.isAlive == true

    /** What a glibc X client (the game, a window manager) needs to reach this server. */
    fun clientEnvironment(context: Context): Map<String, String> = mapOf(
        "DISPLAY" to socket(context).path,
        "TMPDIR" to tmpDir(context).path,
        "XDG_RUNTIME_DIR" to tmpDir(context).path,
    )

    /**
     * Starts the server unless it runs already, and returns once its socket exists. [log] gets the server's output
     * (stdout and stderr: patch 0006 sends the X server's log there). [extraArgs] go to Xorg after ":0 -ac", e.g.
     * "-disable-dri3" or "-legacy-drawing".
     */
    @Synchronized
    fun start(context: Context, log: File? = null, extraArgs: List<String> = emptyList(),
              extraEnv: Map<String, String> = emptyMap()): Process {
        process?.takeIf { it.isAlive }?.let { return it }
        val tmp = tmpDir(context).apply { mkdirs() }
        // Leftovers from a server that died with the app: Xorg refuses to start over a live-looking lock.
        socket(context).delete()
        lock(context).delete()

        // -ac: no access control. Only our own uid can reach the socket in app-private storage anyway.
        val pb = ProcessBuilder(listOf("/system/bin/app_process", "-Xnoimage-dex2oat", "/",
                                       "com.termux.x11.CmdEntryPoint", ":$DISPLAY_NUMBER", "-ac") + extraArgs)
        pb.environment().apply {
            remove("LD_PRELOAD")
            put("CLASSPATH", context.applicationInfo.sourceDir)
            put("TMPDIR", tmp.path)
            put("XLORIE_LIB", xlorieLib(context).path)
            xkbRoot(context).takeIf { it.isDirectory }?.let { put("XKB_CONFIG_ROOT", it.path) }
            putAll(extraEnv)
        }
        val server = pb.redirectErrorStream(true).start()
        process = server
        keepLog(server, log)

        val socket = socket(context)
        val deadline = System.currentTimeMillis() + START_TIMEOUT_MS
        while (!socket.exists()) {
            if (!server.isAlive) {
                process = null
                throw IOException("The X server exited (${server.exitValue()})" + (log?.let { "; see ${it.path}" } ?: ""))
            }
            if (System.currentTimeMillis() > deadline) {
                server.destroyForcibly()
                process = null
                throw IOException("The X server didn't start in ${START_TIMEOUT_MS / 1000} s")
            }
            Thread.sleep(POLL_MS)
        }
        Log.i(TAG, "X server up on ${socket.path}")
        return server
    }

    @Synchronized
    fun stop() {
        process?.destroy()
        process = null
    }

    /** The output must be drained or the server blocks once the pipe fills. */
    private fun keepLog(server: Process, log: File?) = thread(name = "ut-x11-log", isDaemon = true) {
        try {
            val out = log?.apply { parentFile?.mkdirs() }?.printWriter()
            out.use {
                server.inputStream.bufferedReader().forEachLine { line ->
                    if (out != null) {
                        out.println(line)
                        out.flush()
                    } else {
                        Log.i(TAG, line)
                    }
                }
            }
        } catch (e: IOException) {
            // The server was destroyed while this read its output.
        }
    }
}
