package com.lscardinali.unrealtournament.bridge

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import java.io.DataInputStream
import java.io.EOFException
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The game's sound from our SDL (utandroid audio driver): a 12-byte header ("UTA1", rate, channels as LE int32),
 * then interleaved float32 samples until the game closes its audio device.
 */
object AudioBridge {
    private const val TAG = "UTAndroid"
    private const val MAGIC = "UTA1"
    private const val LAG_FRAMES = 2048
    private const val READ_FRAMES = 256

    fun start(socket: File) = UnixServer.serve(socket) { client ->
        try {
            play(DataInputStream(client.inputStream))
        } catch (e: IOException) {
            Log.w(TAG, "Audio stream ended: $e")
        }
    }

    private fun play(input: DataInputStream) {
        val header = ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN)
        input.readFully(header.array())
        if (String(header.array(), 0, 4, Charsets.US_ASCII) != MAGIC) throw IOException("not a UTA1 stream")
        val freq = header.getInt(4)
        val channels = header.getInt(8)
        val track = newTrack(freq, channels)
        Log.i(TAG, "Audio: $freq Hz, $channels ch, buffer ${track.bufferSizeInFrames} frames")
        val bytes = ByteArray(4 * channels * READ_FRAMES)
        val samples = FloatArray(bytes.size / 4)
        val view = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
        track.play()
        try {
            while (true) {
                input.readFully(bytes)
                view.rewind()
                view.get(samples)
                track.write(samples, 0, samples.size, AudioTrack.WRITE_BLOCKING)
            }
        } catch (e: EOFException) {
            // The game closed its audio device.
        } finally {
            track.stop()
            track.release()
        }
    }

    private fun newTrack(freq: Int, channels: Int): AudioTrack {
        val mask = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val minimum = AudioTrack.getMinBufferSize(freq, mask, AudioFormat.ENCODING_PCM_FLOAT)
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build())
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                .setSampleRate(freq)
                .setChannelMask(mask)
                .build())
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .setBufferSizeInBytes(maxOf(minimum, 4 * channels * LAG_FRAMES))
            .build()
        track.bufferSizeInFrames = LAG_FRAMES
        return track
    }
}
