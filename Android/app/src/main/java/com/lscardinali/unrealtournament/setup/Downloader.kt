package com.lscardinali.unrealtournament.setup

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** A file to download: mirrors in order of preference, the expected size and SHA-256 (either may be unknown). */
class Download(val name: String, val urls: List<String>, val size: Long?, val sha256: String?)

class CancelledException : IOException("cancelled")

/**
 * Resumable HTTP downloads with mirror fallback and SHA-256 verification, as OldUnreal's installer does
 * (3 attempts per source, then the next source). A partial file (<dest>.part) is resumed with a Range request.
 */
class Downloader(private val userAgent: String, private val cancelled: () -> Boolean) {
    /** [progress] gets (bytes done, total or -1); [verifying] is called before the hash check. */
    fun fetch(d: Download, dest: File, progress: (Long, Long) -> Unit, verifying: () -> Unit = {}) {
        if (dest.exists() && (d.size == null || dest.length() == d.size)) {
            verifying()
            if (d.sha256 == null || sha256(dest) == d.sha256) return
            dest.delete()
        }
        dest.parentFile?.mkdirs()
        val part = File(dest.path + ".part")
        var lastError: Exception? = null
        for (url in d.urls) {
            for (attempt in 1..3) {
                if (cancelled()) throw CancelledException()
                try {
                    get(url, part, d.size, progress)
                    if (d.size != null && part.length() != d.size) throw IOException("size ${part.length()} != ${d.size}")
                    verifying()
                    if (d.sha256 != null && sha256(part) != d.sha256) {
                        part.delete()
                        throw IOException("checksum mismatch from $url")
                    }
                    if (!part.renameTo(dest)) throw IOException("could not rename ${part.name}")
                    return
                } catch (e: CancelledException) {
                    throw e
                } catch (e: Exception) {
                    lastError = e
                    if (attempt < 3) Thread.sleep(5000L * attempt)
                }
            }
        }
        throw IOException("could not download ${d.name}: ${lastError?.message}", lastError)
    }

    private fun get(url: String, part: File, size: Long?, progress: (Long, Long) -> Unit) {
        var have = if (part.exists()) part.length() else 0L
        if (size != null && have > size) {
            part.delete()
            have = 0
        }
        if (size != null && have == size) return
        val c = URL(url).openConnection() as HttpURLConnection
        c.instanceFollowRedirects = true
        c.connectTimeout = 15_000
        c.readTimeout = 30_000
        c.setRequestProperty("User-Agent", userAgent)
        if (have > 0) c.setRequestProperty("Range", "bytes=$have-")
        try {
            val code = c.responseCode
            val append = code == HttpURLConnection.HTTP_PARTIAL
            if (code != HttpURLConnection.HTTP_OK && !append) throw IOException("HTTP $code from $url")
            if (!append) have = 0
            val total = size ?: (c.contentLengthLong.takeIf { it >= 0 }?.plus(have) ?: -1L)
            c.inputStream.use { input ->
                FileOutputStream(part, append).use { out ->
                    val buf = ByteArray(256 * 1024)
                    var done = have
                    var lastReport = 0L
                    while (true) {
                        if (cancelled()) throw CancelledException()
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        done += n
                        val now = System.nanoTime()
                        if (now - lastReport > 100_000_000L) {
                            progress(done, total)
                            lastReport = now
                        }
                    }
                    progress(done, total)
                }
            }
        } finally {
            c.disconnect()
        }
    }

    companion object {
        fun sha256(f: File): String {
            val md = MessageDigest.getInstance("SHA-256")
            f.inputStream().use { input ->
                val buf = ByteArray(1 shl 20)
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    md.update(buf, 0, n)
                }
            }
            return md.digest().joinToString("") { "%02x".format(it) }
        }
    }
}
