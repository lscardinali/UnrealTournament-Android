package com.lscardinali.unrealtournament.setup

import java.io.Closeable
import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Minimal ISO 9660 reader with Joliet (long, mixed-case names), enough for the UT GOTY CD image.
 * Paths use '/' and keep the CD's case ("Maps/CTF-Face.unr.uz").
 */
class IsoReader(file: File) : Closeable {
    data class Entry(val path: String, val lba: Long, val size: Long, val isDir: Boolean)

    private val raf = RandomAccessFile(file, "r")
    private val blockSize: Int
    private val rootLba: Long
    private val rootSize: Long
    private val joliet: Boolean

    init {
        var found: ByteArray? = null
        var primary: ByteArray? = null
        var sector = 16L
        while (true) {
            val d = readBytes(sector * SECTOR, SECTOR.toInt())
            if (String(d, 1, 5, Charsets.US_ASCII) != "CD001") error("not an ISO 9660 image")
            val type = d[0].toInt() and 0xff
            if (type == 255) break
            if (type == 1 && primary == null) primary = d
            if (type == 2 && d[88] == '%'.code.toByte() && d[89] == '/'.code.toByte() &&
                d[90].toInt().toChar() in "@CE") found = d
            sector++
        }
        val vd = found ?: primary ?: error("no volume descriptor")
        joliet = found != null
        val b = ByteBuffer.wrap(vd).order(ByteOrder.LITTLE_ENDIAN)
        blockSize = b.getShort(128).toInt() and 0xffff
        rootLba = b.getInt(156 + 2).toLong() and 0xffffffffL
        rootSize = b.getInt(156 + 10).toLong() and 0xffffffffL
    }

    /** Every file and directory, depth first. */
    fun entries(): List<Entry> {
        val out = ArrayList<Entry>()
        walk("", rootLba, rootSize, out, 0)
        return out
    }

    private fun walk(prefix: String, lba: Long, size: Long, out: MutableList<Entry>, depth: Int) {
        if (depth > 32) return
        val dir = readBytes(lba * blockSize, size.toInt())
        var pos = 0
        while (pos < dir.size) {
            val len = dir[pos].toInt() and 0xff
            if (len == 0) { // records never cross a sector: skip to the next one
                pos = (pos / blockSize + 1) * blockSize
                continue
            }
            val b = ByteBuffer.wrap(dir, pos, len).order(ByteOrder.LITTLE_ENDIAN)
            val extent = b.getInt(pos + 2).toLong() and 0xffffffffL
            val length = b.getInt(pos + 10).toLong() and 0xffffffffL
            val flags = dir[pos + 25].toInt()
            val nameLen = dir[pos + 32].toInt() and 0xff
            val raw = dir.copyOfRange(pos + 33, pos + 33 + nameLen)
            pos += len
            if (nameLen == 1 && (raw[0].toInt() == 0 || raw[0].toInt() == 1)) continue // "." and ".."
            var name = if (joliet) String(raw, Charsets.UTF_16BE) else String(raw, Charsets.ISO_8859_1)
            name = name.substringBefore(';')
            if (!joliet && name.endsWith('.')) name = name.dropLast(1)
            val path = if (prefix.isEmpty()) name else "$prefix/$name"
            val isDir = flags and 2 != 0
            out += Entry(path, extent, length, isDir)
            if (isDir) walk(path, extent, length, out, depth + 1)
        }
    }

    /** Copies a file's bytes to [out], reporting bytes copied to [progress]. */
    fun copy(entry: Entry, out: OutputStream, progress: (Long) -> Unit = {}) {
        val buf = ByteArray(1 shl 20)
        var offset = entry.lba * blockSize
        var left = entry.size
        while (left > 0) {
            val n = minOf(left, buf.size.toLong()).toInt()
            raf.seek(offset)
            raf.readFully(buf, 0, n)
            out.write(buf, 0, n)
            offset += n
            left -= n
            progress(n.toLong())
        }
    }

    fun read(entry: Entry): ByteArray = readBytes(entry.lba * blockSize, entry.size.toInt())

    private fun readBytes(offset: Long, size: Int): ByteArray {
        val b = ByteArray(size)
        raf.seek(offset)
        raf.readFully(b)
        return b
    }

    override fun close() = raf.close()

    private companion object {
        const val SECTOR = 2048L
    }
}
