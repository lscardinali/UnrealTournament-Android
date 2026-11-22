package com.lscardinali.unrealtournament

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Color
import android.graphics.Typeface
import android.os.BatteryManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.util.Locale
import kotlin.math.abs

/**
 * Performance HUD drawn over the game (toggle: L3 + R3).
 *
 *   FPS        frames the game presented (counted by utshim at Mesa's xcb_put_image / present) + frame time
 *   PWR        battery power draw (current × voltage); "chg" while charging
 *   CPU        % of one core used by the game and by the X server (100% = one core busy), cluster clocks
 *   GPU        Adreno busy %, clock, temperature (kgsl sysfs)
 *   MEM/TEMP   game RSS, battery temperature
 *
 * Sampled every [PERIOD_MS] on a background thread; text set on the UI thread.
 */
class PerfOverlay(
    private val activity: android.app.Activity,
    private val framesFile: File,
    /** Native port: the game runs inside the app process (no ut-bin or X server processes). */
    private val inProcess: Boolean = false,
) {
    private val text = TextView(activity).apply {
        setTextColor(Color.WHITE)
        setShadowLayer(3f, 1f, 1f, Color.BLACK)
        setBackgroundColor(0x88000000.toInt())
        typeface = Typeface.MONOSPACE
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        setPadding(20, 14, 20, 14)
    }
    private val worker = HandlerThread("ut-perf").apply { start() }
    private val handler = Handler(worker.looper)
    private val ui = Handler(Looper.getMainLooper())
    @Volatile private var running = false
    private val cores = Runtime.getRuntime().availableProcessors()
    private val clockTicks = 100.0 // USER_HZ on Android/arm64

    // previous samples
    private var lastFrames = -1L
    private var lastTime = 0L
    private val lastCpu = HashMap<Int, Long>()
    private var frames: java.nio.ByteBuffer? = null

    val visible get() = running

    fun toggle() = if (running) hide() else show()

    fun show() {
        if (running) return
        running = true
        if (text.parent == null) {
            activity.addContentView(text, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START))
        }
        text.visibility = android.view.View.VISIBLE
        text.bringToFront()
        handler.post(sampler)
    }

    fun hide() {
        running = false
        handler.removeCallbacks(sampler)
        text.visibility = android.view.View.GONE
    }

    fun release() {
        hide()
        worker.quitSafely()
    }

    private val sampler = object : Runnable {
        override fun run() {
            if (!running) return
            val s = try { sample() } catch (t: Throwable) { "perf: ${t.javaClass.simpleName}: ${t.message}" }
            ui.post { text.text = s }
            handler.postDelayed(this, PERIOD_MS)
        }
    }

    private fun sample(): String {
        val now = SystemClock.elapsedRealtime()
        val dt = if (lastTime == 0L) 0.0 else (now - lastTime) / 1000.0
        lastTime = now
        val sb = StringBuilder()

        // FPS
        val f = readFrames()
        if (f != null && lastFrames >= 0 && dt > 0) {
            val d = (f - lastFrames + (1L shl 32)) % (1L shl 32)
            val fps = d / dt
            sb.append(fmt("FPS  %5.1f  %5.1f ms\n", fps, if (fps > 0) 1000.0 / fps else 0.0))
        } else {
            sb.append("FPS    --\n")
        }
        if (f != null) lastFrames = f

        // Power
        val bm = activity.getSystemService(Context.BATTERY_SERVICE) as BatteryManager
        val battery = activity.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val microAmps = bm.getLongProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW)
        val milliVolts = battery?.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0) ?: 0
        val charging = (battery?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0
        // Some kernels report mA instead of µA: anything below 20 A-as-µA is taken as mA.
        val amps = if (abs(microAmps) in 1..20_000) abs(microAmps) / 1000.0 else abs(microAmps) / 1_000_000.0
        val watts = amps * milliVolts / 1000.0
        val pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        sb.append(fmt("PWR  %5.2f W  %d%%%s\n", watts, pct, if (charging) " chg" else ""))

        // CPU: game, X server, app
        val game = if (inProcess) android.os.Process.myPid() else findPid("ut-bin-arm64")
        if (inProcess) {
            val cpu = cpuPercent(android.os.Process.myPid(), dt)
            sb.append(fmt("CPU  game (in-process) %3.0f%%\n", cpu ?: 0.0))
        } else {
            val xserver = findPid("CmdEntryPoint")
            val gameCpu = game?.let { cpuPercent(it, dt) }
            val xCpu = xserver?.let { cpuPercent(it, dt) }
            val appCpu = cpuPercent(android.os.Process.myPid(), dt)
            sb.append(fmt("CPU  game %3.0f%%  X %3.0f%%  app %3.0f%%\n", gameCpu ?: 0.0, xCpu ?: 0.0, appCpu ?: 0.0))
        }
        sb.append("     ").append(clusterClocks()).append('\n')

        // GPU
        val busy = readText("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage")?.trim()?.removeSuffix("%")?.trim()
        val gpuHz = readLong("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq") ?: readLong("/sys/class/kgsl/kgsl-3d0/gpuclk")
        val gpuTemp = readLong("/sys/class/kgsl/kgsl-3d0/temp")
        sb.append(fmt("GPU  %s%%  %s MHz  %s\n", busy ?: "?", gpuHz?.let { (it / 1_000_000).toString() } ?: "?",
                      gpuTemp?.let { fmt("%.0f°C", it / 1000.0) } ?: ""))

        // Memory / temperature
        val rss = game?.let { rssMb(it) }
        val batTemp = (battery?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0) / 10.0
        sb.append(fmt("MEM  game %s MB   BAT %.1f°C", rss?.toString() ?: "?", batTemp))
        return sb.toString()
    }

    private fun readFrames(): Long? {
        val buf = frames ?: run {
            if (!framesFile.exists() || framesFile.length() < 8) return null
            RandomAccessFile(framesFile, "r").use { raf ->
                raf.channel.map(FileChannel.MapMode.READ_ONLY, 0, 8).order(ByteOrder.LITTLE_ENDIAN)
            }.also { frames = it }
        }
        return buf.getInt(4).toLong() and 0xFFFFFFFFL
    }

    /** Our own child processes (same uid), found by a needle in their command line. */
    private fun findPid(needle: String): Int? = File("/proc").listFiles()?.firstOrNull { dir ->
        dir.name.all(Char::isDigit) && runCatching { File(dir, "cmdline").readText().contains(needle) }.getOrDefault(false)
    }?.name?.toInt()

    /** CPU use since the last sample, in % of one core. */
    private fun cpuPercent(pid: Int, dt: Double): Double? {
        val stat = readText("/proc/$pid/stat") ?: return null
        val fields = stat.substringAfterLast(") ").split(' ')
        val ticks = (fields.getOrNull(11)?.toLongOrNull() ?: 0) + (fields.getOrNull(12)?.toLongOrNull() ?: 0)
        val prev = lastCpu.put(pid, ticks) ?: return null
        if (dt <= 0) return null
        return (ticks - prev) / clockTicks / dt * 100.0
    }

    /** Current clock of each CPU cluster (first core of each distinct policy). */
    private fun clusterClocks(): String {
        val seen = LinkedHashMap<String, Long>()
        for (i in 0 until cores) {
            val policy = runCatching { File("/sys/devices/system/cpu/cpu$i/cpufreq").canonicalFile.name }.getOrNull() ?: continue
            if (policy in seen) continue
            seen[policy] = readLong("/sys/devices/system/cpu/cpu$i/cpufreq/scaling_cur_freq") ?: continue
        }
        return seen.entries.joinToString("  ") { (k, khz) -> "${k.removePrefix("policy")}:${khz / 1000}" } + " MHz"
    }

    private fun rssMb(pid: Int): Long? = readText("/proc/$pid/status")?.lineSequence()
        ?.firstOrNull { it.startsWith("VmRSS:") }?.split(Regex("\\s+"))?.getOrNull(1)?.toLongOrNull()?.div(1024)

    private fun readText(path: String): String? = runCatching { File(path).readText() }.getOrNull()
    private fun readLong(path: String): Long? = readText(path)?.trim()?.toLongOrNull()
    private fun fmt(f: String, vararg a: Any) = String.format(Locale.ROOT, f, *a)

    companion object {
        const val PERIOD_MS = 500L
    }
}
