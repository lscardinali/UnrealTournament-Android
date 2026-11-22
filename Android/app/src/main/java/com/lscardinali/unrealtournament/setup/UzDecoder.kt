package com.lscardinali.unrealtournament.setup

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Decompresses Unreal's .uz files (what `ucc decompress` does): a header (signature 1234/5678 + the original file
 * name as an FString), then the engine's FCodecFull stream, which was encoded RLE -> BWT -> MTF -> RLE -> Huffman,
 * so it decodes Huffman -> RLE -> MTF -> BWT -> RLE. Ports of Core's FCodec* decoders.
 */
object UzDecoder {
    class Result(val fileName: String, val data: ByteArray)

    fun decode(input: ByteArray): Result {
        val b = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN)
        val signature = b.int
        require(signature == 1234 || signature == 5678) { "not a .uz file (signature $signature)" }
        val name = readFString(b)
        var data = input.copyOfRange(b.position(), input.size)
        data = huffman(data)
        data = rle(data)
        data = mtf(data)
        data = bwt(data)
        data = rle(data)
        return Result(name, data)
    }

    private fun readCompactIndex(b: ByteBuffer): Int {
        val b0 = b.get().toInt() and 0xff
        var value = b0 and 0x3f
        if (b0 and 0x40 != 0) {
            var shift = 6
            while (true) {
                val x = b.get().toInt() and 0xff
                value = value or ((x and 0x7f) shl shift)
                shift += 7
                if (x and 0x80 == 0 || shift > 27) break
            }
        }
        return if (b0 and 0x80 != 0) -value else value
    }

    private fun readFString(b: ByteBuffer): String {
        val len = readCompactIndex(b)
        val sb = StringBuilder()
        if (len > 0) repeat(len) { sb.append((b.get().toInt() and 0xff).toChar()) }
        else repeat(-len) { sb.append(b.short.toInt().toChar()) }
        return sb.toString().trimEnd('\u0000')
    }

    private class Out(capacity: Int) {
        var buf = ByteArray(maxOf(capacity, 16))
        var size = 0
        fun add(v: Int) {
            if (size == buf.size) buf = buf.copyOf(buf.size * 2)
            buf[size++] = v.toByte()
        }
        fun bytes(): ByteArray = buf.copyOf(size)
    }

    /** FCodecHuffman: total byte count, then a bit stream (LSB first): the tree, then the symbols. */
    private fun huffman(input: ByteArray): ByteArray {
        val total = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN).int
        var bit = 32L
        val bits = input.size.toLong() * 8
        fun readBit(): Int {
            check(bit < bits) { "huffman: out of data" }
            val v = (input[(bit ushr 3).toInt()].toInt() shr (bit and 7).toInt()) and 1
            bit++
            return v
        }
        // Tree as arrays: child0/child1 = node index, symbol = leaf byte (-1 for inner nodes).
        val child0 = IntArray(512)
        val child1 = IntArray(512)
        val symbol = IntArray(512) { -1 }
        var count = 0
        fun readTable(): Int {
            val node = count++
            if (readBit() != 0) {
                child0[node] = readTable()
                child1[node] = readTable()
            } else {
                var v = 0
                for (i in 0 until 8) v = v or (readBit() shl i)
                symbol[node] = v
            }
            return node
        }
        readTable()
        val out = Out(total)
        repeat(total) {
            var n = 0
            while (symbol[n] < 0) n = if (readBit() == 0) child0[n] else child1[n]
            out.add(symbol[n])
        }
        return out.bytes()
    }

    /** FCodecRLE: a run of RLE_LEAD equal bytes is followed by the run's full length. */
    private fun rle(input: ByteArray): ByteArray {
        val out = Out(input.size * 2)
        var count = 0
        var prev = 0
        var i = 0
        while (i < input.size) {
            val b = input[i++].toInt() and 0xff
            out.add(b)
            if (b != prev) {
                prev = b
                count = 1
            } else if (++count == RLE_LEAD) {
                var c = input[i++].toInt() and 0xff
                check(c >= 2) { "rle: bad run" }
                while (c-- > RLE_LEAD) out.add(b)
                count = 0
            }
        }
        return out.bytes()
    }

    /** FCodecMTF: move to front. */
    private fun mtf(input: ByteArray): ByteArray {
        val list = IntArray(256) { it }
        val out = ByteArray(input.size)
        for (k in input.indices) {
            val pos = input[k].toInt() and 0xff
            val c = list[pos]
            out[k] = c.toByte()
            System.arraycopy(list, 0, list, 1, pos)
            list[0] = c
        }
        return out
    }

    /** FCodecBWT: blocks of (length, first, last, length + 1 bytes). */
    private fun bwt(input: ByteArray): ByteArray {
        val b = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN)
        val out = Out(input.size)
        val temp = IntArray(MAX_BWT + 1)
        val count = IntArray(257)
        val running = IntArray(257)
        while (b.hasRemaining()) {
            val length = b.int + 1
            val first = b.int
            val last = b.int
            check(length <= MAX_BWT + 1 && length <= b.remaining()) { "bwt: bad block" }
            val start = b.position()
            b.position(start + length)
            java.util.Arrays.fill(count, 0)
            for (i in 0 until length) count[if (i != last) input[start + i].toInt() and 0xff else 256]++
            var sum = 0
            for (i in 0 until 257) {
                running[i] = sum
                sum += count[i]
                count[i] = 0
            }
            for (i in 0 until length) {
                val index = if (i != last) input[start + i].toInt() and 0xff else 256
                temp[running[index] + count[index]++] = i
            }
            var i = first
            for (j in 0 until length - 1) {
                out.add(input[start + i].toInt())
                i = temp[i]
            }
        }
        return out.bytes()
    }

    private const val RLE_LEAD = 5
    private const val MAX_BWT = 0x40000
}
