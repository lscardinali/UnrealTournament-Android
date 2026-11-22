package com.lscardinali.unrealtournament.bridge

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.io.File
import kotlin.concurrent.thread

/** A filesystem Unix socket the game's SDL (utandroid driver) connects to, one client at a time. */
object UnixServer {
    private const val TAG = "UTAndroid"
    private val serving = mutableSetOf<String>()

    fun serve(file: File, handle: (LocalSocket) -> Unit) {
        synchronized(this) {
            if (!serving.add(file.path)) return
        }
        file.delete()
        // Keep the bound socket referenced: its cleanup on GC would close the server's descriptor.
        val bound = LocalSocket(LocalSocket.SOCKET_STREAM)
        bound.bind(LocalSocketAddress(file.path, LocalSocketAddress.Namespace.FILESYSTEM))
        val server = LocalServerSocket(bound.fileDescriptor)
        thread(name = file.name, isDaemon = true) {
            try {
                bound.use {
                    while (true) server.accept().use { client ->
                        try {
                            handle(client)
                        } catch (e: Exception) {
                            Log.e(TAG, "${file.name}: client failed", e)
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "${file.name}: stopped listening", e)
            } finally {
                synchronized(this) { serving -= file.path }
            }
        }
    }
}
