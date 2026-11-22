package com.lscardinali.unrealtournament.runtime

import android.content.Context
import java.io.File
import java.util.Locale
import java.util.TimeZone

/**
 * Runs glibc (Linux aarch64) programs through the glibc loader shipped in nativeLibraryDir.
 *
 * Android forbids exec of files in app data (W^X), but the loader lives in nativeLibraryDir and only
 * *maps* the program and its libraries, which is allowed.
 *
 * On-device layout (filesDir):
 *   glibc/      glibc + userland libs (native/runtime/fetch_debs.py), Turnip + its ICD json, gconv/
 *   override/   our libSDL3.so.0 and libopenal.so.1 — searched before game/System (beats its RUNPATH=$ORIGIN)
 *   bin/        our glibc helper programs (utandroid-wm)
 *   usr/share/X11/{xkb,locale}   X data
 *   game/       the UT install: System/ (469f Linux arm64), Maps/, Textures/, Sounds/, Music/ …
 *   home/       $HOME (UT keeps its ini/logs/saves in home/.utpg/System)
 *   tmp/        $TMPDIR: X socket, bridge sockets
 */
class GlibcProgram(context: Context) {
    val files: File = context.filesDir
    private val nativeDir = File(context.applicationInfo.nativeLibraryDir)

    val loader = File(nativeDir, "libld-linux.so")
    val shim = File(nativeDir, "libutshim.so")
    val glibcDir = File(files, "glibc")
    val gconvDir = File(glibcDir, "gconv")
    val overrideDir = File(files, "override")
    val binDir = File(files, "bin")
    val x11Share = File(files, "usr/share/X11")
    val gameDir = File(files, "game")
    val systemDir = File(gameDir, "System")
    val homeDir = File(files, "home")
    val tmpDir = File(files, "tmp")
    val utIniDir = File(homeDir, ".utpg/System")

    /** Runs a UT binary from game/System (ut-bin-arm64, ucc-bin-arm64). */
    fun startGame(binary: String, args: List<String>, log: File, extraEnv: Map<String, String> = emptyMap()): Process {
        val exe = File(systemDir, binary)
        return start(
            exe, args, log,
            libraryPath = listOf(overrideDir, systemDir, glibcDir),
            workDir = systemDir,
            // Core.so derives its base dir from readlink("/proc/self/exe"), which points at the loader.
            extraEnv = mapOf("UT_EXE_PATH" to exe.path, "LD_PRELOAD" to shim.path) + extraEnv,
        )
    }

    fun start(
        exe: File,
        args: List<String>,
        log: File,
        libraryPath: List<File> = listOf(glibcDir),
        workDir: File = files,
        extraEnv: Map<String, String> = emptyMap(),
    ): Process {
        homeDir.mkdirs(); tmpDir.mkdirs()
        val pb = ProcessBuilder(listOf(loader.path, "--library-path", libraryPath.joinToString(":") { it.path }, exe.path) + args)
            .directory(workDir)
            .redirectErrorStream(true)
            .redirectOutput(log)
        pb.environment().apply {
            clear()
            put("HOME", homeDir.path)
            put("TMPDIR", tmpDir.path)
            put("XDG_RUNTIME_DIR", tmpDir.path)
            put("PATH", "/system/bin")
            put("LANG", "C.UTF-8")
            put("GCONV_PATH", gconvDir.path)
            put("XLOCALEDIR", File(x11Share, "locale").path)
            put("TZ", posixTimeZone())
            putAll(extraEnv)
        }
        return pb.start()
    }

    private fun posixTimeZone(): String {
        val minutes = TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60_000
        val sign = if (minutes >= 0) "-" else "+" // POSIX offsets are west-positive
        val abs = Math.abs(minutes)
        return "UTC%s%02d:%02d".format(Locale.ROOT, sign, abs / 60, abs % 60)
    }
}
