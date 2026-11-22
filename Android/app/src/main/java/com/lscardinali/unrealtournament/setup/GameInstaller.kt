package com.lscardinali.unrealtournament.setup

import org.apache.commons.compress.archivers.sevenz.SevenZFile
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

/**
 * Builds a UT install the way OldUnreal's Linux installer does (FullGameInstallers/Linux/install-ut99.sh):
 * GOTY CD image, then Bonus Pack 4, then the 469 Linux patch on top; .uz maps decompressed; DOM-Cybrosis][ copy;
 * System/ translations that SystemLocalized/ replaces removed.
 *
 * Files go under [systemRoot], except the big data folders ([DATA_DIRS]), which go under [dataRoot] (the SD card
 * when the user chose it; the same folder otherwise). Plain JVM code (tested on the PC: SetupDataTest).
 */
class GameInstaller(
    private val systemRoot: File,
    private val dataRoot: File,
    private val cancelled: () -> Boolean,
    private val progress: (fraction: Float, detail: String) -> Unit,
) {
    /** Where a game-relative path ("Maps/DM-Deck16][.unr") lives. */
    fun file(rel: String): File {
        val top = rel.substringBefore('/')
        return File(if (DATA_DIRS.any { it.equals(top, true) }) dataRoot else systemRoot, rel)
    }

    /** The CD image: the game folders only, minus the files OldUnreal skips; .uz maps decompressed on the way. */
    fun extractIso(iso: File) {
        IsoReader(iso).use { r ->
            val files = r.entries().filter { !it.isDir && isoWanted(it.path) }
            val total = files.sumOf { it.size }.coerceAtLeast(1)
            var done = 0L
            for (e in files) {
                check()
                progress(done.toFloat() / total, e.path)
                if (e.path.startsWith("Maps/", true) && e.path.endsWith(".uz", true)) {
                    val uz = UzDecoder.decode(r.read(e))
                    write(file("Maps/" + File(uz.fileName).name)) { it.write(uz.data) }
                } else {
                    write(file(e.path)) { out -> r.copy(e, out) }
                }
                done += e.size
            }
        }
    }

    /** Bonus Pack 4 (.7z, LZMA2). */
    fun extractBonusPack(archive: File) {
        @Suppress("DEPRECATION")
        SevenZFile(archive).use { z ->
            val entries = z.entries.filter { !it.isDirectory }
            val total = entries.sumOf { it.size }.coerceAtLeast(1)
            var done = 0L
            while (true) {
                val e = z.nextEntry ?: break
                if (e.isDirectory) continue
                check()
                val rel = e.name.replace('\\', '/')
                progress(done.toFloat() / total, rel)
                write(file(rel)) { out -> copyStream(z.getInputStream(e), out) }
                done += e.size
            }
        }
    }

    /** The 469 Linux patch (.tar.bz2), over everything else. Symlinks become copies of their targets. */
    fun extractPatch(archive: File) {
        val links = ArrayList<Pair<String, String>>()
        val total = archive.length().coerceAtLeast(1)
        val counted = CountingInputStream(BufferedInputStream(archive.inputStream(), 1 shl 16))
        TarArchiveInputStream(BZip2CompressorInputStream(counted)).use { tar ->
            while (true) {
                val e = tar.nextEntry ?: break
                check()
                val rel = e.name.removePrefix("./").trimEnd('/')
                if (rel.isEmpty() || e.isDirectory) continue
                progress(counted.count.toFloat() / total, rel)
                when {
                    e.isSymbolicLink -> links += rel to e.linkName
                    e.isFile -> write(file(rel)) { out -> copyStream(tar, out, close = false) }
                }
            }
        }
        for ((rel, target) in links) {
            val src = file(rel.substringBeforeLast('/', "") .let { if (it.isEmpty()) target else "$it/$target" })
            if (src.isFile) write(file(rel)) { out -> src.inputStream().use { copyStream(it, out) } }
        }
    }

    /** OldUnreal's fix-ups after the patch. */
    fun finish() {
        val dm = file("Maps/DM-Cybrosis][.unr")
        val dom = file("Maps/DOM-Cybrosis][.unr")
        if (dm.isFile && !dom.exists()) dm.copyTo(dom)
        // Translations: drop System/<file> when SystemLocalized/ (or SystemLocalized/<lang>/) has the same file.
        val localized = file("SystemLocalized")
        val system = file("System")
        if (localized.isDirectory) {
            for (f in system.listFiles().orEmpty()) {
                if (f.extension.lowercase() !in LANGUAGE_EXTS) continue
                if (File(localized, f.name).exists() || File(localized, "${f.extension}/${f.name}").exists()) f.delete()
            }
        }
    }

    private fun isoWanted(path: String): Boolean {
        val top = path.substringBefore('/')
        if (ISO_DIRS.none { it.equals(top, true) }) return false
        val name = path.substringAfterLast('/').lowercase()
        val ext = name.substringAfterLast('.', "")
        if (ext in LANGUAGE_EXTS) return false
        if (top.equals("System", true) && (name == "unrealtournament.ini" || name == "user.ini" ||
                ext == "bat" || ext == "dll" || ext == "exe")) return false
        return true
    }

    private fun check() {
        if (cancelled()) throw CancelledException()
    }

    private inline fun write(f: File, body: (FileOutputStream) -> Unit) {
        f.parentFile?.mkdirs()
        val tmp = File(f.path + ".tmp")
        FileOutputStream(tmp).use(body)
        if (f.exists()) f.delete()
        if (!tmp.renameTo(f)) error("could not write $f")
    }

    private fun copyStream(input: InputStream, out: FileOutputStream, close: Boolean = true) {
        try {
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
            }
        } finally {
            if (close) input.close()
        }
    }

    private class CountingInputStream(private val inner: InputStream) : InputStream() {
        var count = 0L
        override fun read(): Int = inner.read().also { if (it >= 0) count++ }
        override fun read(b: ByteArray, off: Int, len: Int): Int = inner.read(b, off, len).also { if (it > 0) count += it }
        override fun close() = inner.close()
    }

    companion object {
        /** Big read-only data: these can live on the SD card. */
        val DATA_DIRS = listOf("Maps", "Textures", "Sounds", "Music")
        private val ISO_DIRS = listOf("System", "Maps", "Textures", "Sounds", "Music", "Help", "Web")
        private val LANGUAGE_EXTS = setOf("ctt", "det", "elt", "est", "frt", "int", "itt", "kot", "nlt", "ptt",
                                          "rut", "smt", "tmt")
    }
}
