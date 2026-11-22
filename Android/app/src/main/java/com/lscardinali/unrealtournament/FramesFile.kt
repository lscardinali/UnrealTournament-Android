package com.lscardinali.unrealtournament

import android.content.Context
import java.io.File
import java.io.RandomAccessFile

/**
 * The frames file shared with the loader: "UTF1", then a uint32 count of presented frames at offset 4 (libutloader
 * increments it in its vkQueuePresentKHR wrapper). The performance overlay reads it for FPS.
 */
object FramesFile {
    const val SIZE = 32

    fun path(context: Context) = File(context.filesDir, "tmp/ut-frames")

    /** A fresh file before the game starts (the loader maps it at start-up). */
    fun create(context: Context) {
        val file = path(context)
        file.parentFile?.mkdirs()
        file.delete()
        RandomAccessFile(file, "rw").use { raf ->
            raf.setLength(SIZE.toLong())
            raf.write(byteArrayOf('U'.code.toByte(), 'T'.code.toByte(), 'F'.code.toByte(), '1'.code.toByte()))
        }
    }
}
