package com.lscardinali.unrealtournament

import android.view.Choreographer
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel

/**
 * Publishes display vsync timing to the game's frame pacer (utshim pace_frame), through the shared frames file:
 *   offset 8  uint64 latest vsync time (Choreographer frameTimeNanos = System.nanoTime = CLOCK_MONOTONIC)
 *   offset 16 uint64 refresh period, ns
 * Runs on the main thread, one cheap write per vsync while started.
 */
class VsyncWriter(private val file: File, private val refreshHz: () -> Float) : Choreographer.FrameCallback {
    private var buffer: ByteBuffer? = null
    private var running = false

    fun start() {
        if (running) return
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    fun stop() {
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        val buf = buffer ?: map()
        if (buf != null) {
            val hz = refreshHz().takeIf { it > 1f } ?: 60f
            buf.putLong(16, (1_000_000_000.0 / hz).toLong())
            buf.putLong(8, frameTimeNanos)
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    private fun map(): ByteBuffer? {
        if (!file.exists() || file.length() < SIZE) return null
        return RandomAccessFile(file, "rw").use { raf ->
            raf.channel.map(FileChannel.MapMode.READ_WRITE, 0, SIZE.toLong()).order(ByteOrder.LITTLE_ENDIAN)
        }.also { buffer = it }
    }

    companion object {
        const val SIZE = 32

        /** A fresh frames file ("UTF1", zeros) before the game starts. */
        fun create(file: File) {
            file.parentFile?.mkdirs()
            file.delete()
            RandomAccessFile(file, "rw").use { raf ->
                raf.setLength(SIZE.toLong())
                raf.write(byteArrayOf('U'.code.toByte(), 'T'.code.toByte(), 'F'.code.toByte(), '1'.code.toByte()))
            }
        }
    }
}
