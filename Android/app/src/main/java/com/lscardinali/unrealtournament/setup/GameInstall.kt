package com.lscardinali.unrealtournament.setup

import android.content.Context
import android.os.Environment
import android.system.Os
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.LinkOption

/**
 * Where the game lives on the device and how a fresh install replaces the old one.
 *
 *   filesDir/game/            the install the launcher runs: System/, SystemLocalized/, Help/, Web/, Save/ ...
 *   filesDir/game/Maps etc.   real folders (internal install) or symlinks to <sd>/game/Maps etc. (SD card install;
 *                             program files can't run from the SD card, which is mounted noexec)
 *   filesDir/game/.utandroid-install.json   {"version":1,"dataDir":"..."} written by the setup
 *
 * A setup builds into game.new (and <sd>/game.new) and only swaps it in when everything succeeded, keeping Save/.
 */
object GameInstall {
    private const val MARKER = ".utandroid-install.json"

    fun gameDir(ctx: Context) = File(ctx.filesDir, "game")

    /** True when a game is installed: by the setup (marker) or by the dev deploy script (System + Maps present). */
    fun isInstalled(ctx: Context): Boolean {
        val g = gameDir(ctx)
        return File(g, MARKER).exists() ||
            (File(g, "System/ut-bin-arm64").isFile && File(g, "Maps").isDirectory)
    }

    /** The SD card's app folder, when a removable card is mounted. */
    fun sdCardDir(ctx: Context): File? = ctx.getExternalFilesDirs(null).drop(1).firstOrNull {
        it != null && Environment.getExternalStorageState(it) == Environment.MEDIA_MOUNTED &&
            Environment.isExternalStorageRemovable(it)
    }

    class Layout(val ctx: Context, val sdCard: File?) {
        val systemStage = File(ctx.filesDir, "game.new")
        val dataStage = if (sdCard != null) File(sdCard, "game.new") else systemStage
        val downloads = File(sdCard ?: ctx.filesDir, "downloads")

        /** Bytes free where the big files go (downloads + data) and on internal storage. */
        fun freeData() = (sdCard ?: ctx.filesDir).usableSpace
        fun freeInternal() = ctx.filesDir.usableSpace

        fun clearStage() {
            deleteTree(systemStage)
            if (dataStage != systemStage) deleteTree(dataStage)
        }

        /** Swaps the staged install in. */
        fun commit() {
            val game = gameDir(ctx)
            val old = readMarker(game)
            // Keep the player's saves.
            val save = File(game, "Save")
            if (save.isDirectory) {
                deleteTree(File(systemStage, "Save"))
                save.renameTo(File(systemStage, "Save"))
            }
            deleteTree(game)
            old?.optString("dataDir")?.takeIf { it.isNotEmpty() }?.let { deleteTree(File(it, "game")) }
            if (sdCard != null) {
                val sdGame = File(sdCard, "game")
                deleteTree(sdGame)
                check(dataStage.renameTo(sdGame)) { "could not move the game data into place on the SD card" }
                for (d in GameInstaller.DATA_DIRS) {
                    val target = File(sdGame, d)
                    if (target.isDirectory) Os.symlink(target.path, File(systemStage, d).path)
                }
            }
            File(systemStage, "Save").mkdirs()
            File(systemStage, MARKER).writeText(
                JSONObject().put("version", 1).put("dataDir", sdCard?.path ?: "").toString())
            check(systemStage.renameTo(game)) { "could not move the game into place" }
            deleteTree(downloads)
        }
    }

    private fun readMarker(game: File): JSONObject? =
        runCatching { JSONObject(File(game, MARKER).readText()) }.getOrNull()

    /** Deletes a folder without following symlinks (an SD card install links its data folders). */
    fun deleteTree(f: File) {
        val p = f.toPath()
        if (Files.isSymbolicLink(p)) {
            f.delete()
            return
        }
        if (Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS)) f.listFiles()?.forEach { deleteTree(it) }
        f.delete()
    }

    // ------------------------------------------------------------------ download sources (OldUnreal's installer)

    private val mirrors = listOf("https://files.oldunreal.net", "https://files2.oldunreal.net", "https://files3.oldunreal.net")

    val gameIso = Download(
        "Unreal Tournament GOTY",
        mirrors.shuffled().map { "$it/UT_GOTY_CD1.ISO" } + "https://archive.org/download/ut-goty/UT_GOTY_CD1.iso",
        649633792L, "e184984ca88f001c5ddd52035d76cd64e266e26c74975161b5ed72366c74704f")

    val bonusPack4 = Download(
        "Bonus Pack 4", mirrors.shuffled().map { "$it/utbonuspack4-zip.7z" },
        11268844L, "5b7a1080724a122a596c226c50d4dc7c2d7636ceaf067e9c12112014a170ffba")

    private val knownPatch = Download(
        "469 patch",
        listOf("https://github.com/OldUnreal/UnrealTournamentPatches/releases/download/v469f-rc6/OldUnreal-UTPatch469f-Linux-arm64.tar.bz2"),
        null, "56c09d3f7f1889f01acf21638c71952c5e2dfbd737b075ffaea3b94bfe52ef46")

    /** The latest 469 Linux arm64 patch from GitHub (the known v469f-rc6 asset when GitHub can't be reached). */
    fun latestPatch(userAgent: String): Download = runCatching {
        val c = URL("https://api.github.com/repos/OldUnreal/UnrealTournamentPatches/releases/latest")
            .openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 30_000
        c.setRequestProperty("User-Agent", userAgent)
        c.setRequestProperty("Accept", "application/vnd.github+json")
        val json = JSONObject(c.inputStream.bufferedReader().use { it.readText() })
        val assets = json.getJSONArray("assets")
        for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            val url = a.getString("browser_download_url")
            if (!url.lowercase().contains("-linux-arm64")) continue
            val digest = a.optString("digest").takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")
            return@runCatching Download("469 patch (${json.optString("tag_name")})", listOf(url),
                                        a.optLong("size").takeIf { it > 0 }, digest)
        }
        null
    }.getOrNull() ?: knownPatch
}
