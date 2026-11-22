package com.lscardinali.unrealtournament

import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewGroup
import android.widget.FrameLayout
import com.lscardinali.unrealtournament.runtime.GameConfig
import org.libsdl.app.SDLActivity
import java.io.File

/**
 * The native (in-process) port: stock SDL3 for Android hosts UT's own Linux arm64 binaries, loaded into this
 * process by libutloader.so (native/utloader). No glibc process, no X server: Android SDL3 for window/input,
 * Qualcomm's Vulkan driver straight to this activity's surface. See research/native-port-plan.md.
 *
 * Controller: SDL's virtual "UTAndroid controller" (SDL_utandroid.c, native-pad mode) reads the real pad and
 * shapes it exactly like the X11 build; the menu cursor is drawn here ([CursorOverlay]); L3+R3 toggles the
 * performance overlay.
 *
 * Extras: "args" — extra ut-bin arguments; "nosound" — true to run without audio;
 * "vkdebug" — true to run VulkanDrv with the Khronos validation layer (debug builds).
 */
class NativeGameActivity : SDLActivity() {
    private var perf: PerfOverlay? = null
    private var cursor: CursorOverlay? = null
    private var gyro: GyroAim? = null
    private var l3 = false
    private var r3 = false

    // Touchpad cursor (menus only): relative finger moves, a short still touch is a left click.
    private var touchX = 0f
    private var touchY = 0f
    private var touchDownAt = 0L
    private var touchTravel = 0f
    private var restX = 0f
    private var restY = 0f

    private val gameDir get() = File(filesDir, "game")
    private val systemDir get() = File(gameDir, "System")

    override fun onCreate(savedInstanceState: Bundle?) {
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            android.util.Log.e("utloader", "uncaught Java exception on ${t.name}", e)
            previous?.uncaughtException(t, e)
        }
        val bounds = windowManager.maximumWindowMetrics.bounds
        val width = maxOf(bounds.width(), bounds.height())
        val height = minOf(bounds.width(), bounds.height())
        // Spike: "--es game unreal" runs Unreal 227 from files/unreal (SystemARM64/unreal-bin-arm64, own home).
        if (intent.getStringExtra("game") == "unreal") {
            val unreal = File(filesDir, "unreal")
            android.system.Os.setenv("UTL_SYSTEM_DIR", File(unreal, "SystemARM64").path, true)
            android.system.Os.setenv("UTL_EXE", "unreal-bin-arm64", true)
            android.system.Os.setenv("UTL_HOME", File(filesDir, "home-unreal").apply { mkdirs() }.path, true)
            FramesFile.create(this)
            super.onCreate(savedInstanceState)
            return
        }
        val ini = File(filesDir, "home/.utpg/System")
        for (marker in listOf("Running.ini", "UnrealTournament.crash")) File(ini, marker).delete()
        File(gameDir, "Save").mkdirs() // UT's SavePath=../Save (the install doesn't ship the folder)
        GameConfig.applyAll(this, ini, systemDir, width, height)
        // "vkdebug": VulkanDrv's own debug mode, which enables the Khronos validation layer (debug builds ship it in
        // src/debug/jniLibs; Android's Vulkan loader finds layers in a debuggable app's lib dir).
        val vkDebug = intent.getBooleanExtra("vkdebug", false)
        GameConfig.apply(File(ini, "UnrealTournament.ini"), File(systemDir, "Default.ini"),
                         mapOf("VulkanDrv.VulkanRenderDevice" to mapOf("VkDebug" to if (vkDebug) "True" else "False")))
        // Frames file for the FPS overlay: the loader counts presented frames into it.
        FramesFile.create(this)

        super.onCreate(savedInstanceState)

        val full = FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        // The loader's shared state also carries the "performance overlay" handheld setting (PadUI's Handheld
        // Settings window, L3+R3): follow it every frame.
        cursor = CursorOverlay(this) {
            nativeCursorState(it)
            val want = it[5] > 0.5f
            perf?.let { p -> if (p.visible != want) p.toggle() }
            // Power: the gyro sensor only runs in a match with gyro aiming on (200 Hz otherwise wasted).
            gyroWanted = it[7] > 0.5f && it[0] < 0.5f
            if (resumed && gyroWanted) gyro?.start() else gyro?.stop()
            applyFrameRateLimit(it[6].toInt())
        }.also {
            addContentView(it, full)
            it.start()
        }
        gyro = GyroAim(this) { yaw, pitch -> nativeGyro(yaw, pitch) }
        perf = PerfOverlay(this, FramesFile.path(this), inProcess = true) // shown per the handheld setting
    }

    // libutloader.so is last: SDL calls its SDL_main.
    override fun getLibraries() = arrayOf("SDL3", "SDL3_ttf", "utloader")

    override fun getArguments(): Array<String> {
        val extra = intent.getStringExtra("args")?.split(' ')?.filter { it.isNotBlank() }.orEmpty()
        val sound = if (intent.getBooleanExtra("nosound", false)) listOf("-nosound") else emptyList()
        return (listOf(filesDir.path) + extra + sound).toTypedArray()
    }

    /**
     * The touchscreen is a touchpad for the menu cursor (a fallback: the pad drives the menus). Sliding moves the
     * cursor relative to where it is and shows it for a few seconds; a short tap clicks where it is. SDL never sees
     * the touches, so there are no absolute touch clicks.
     */
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                touchX = ev.x; touchY = ev.y
                touchDownAt = ev.eventTime
                touchTravel = 0f
                restX = 0f; restY = 0f
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = (ev.x - touchX) * TOUCH_SPEED + restX
                val dy = (ev.y - touchY) * TOUCH_SPEED + restY
                touchTravel += Math.abs(ev.x - touchX) + Math.abs(ev.y - touchY)
                touchX = ev.x; touchY = ev.y
                val ix = dx.toInt()
                val iy = dy.toInt()
                restX = dx - ix; restY = dy - iy
                if (ix != 0 || iy != 0) {
                    nativeTouch(ix, iy, 0)
                    cursor?.showFor(CURSOR_SHOW_MS)
                }
            }
            MotionEvent.ACTION_UP -> {
                val still = touchTravel < TAP_SLOP_DP * resources.displayMetrics.density
                if (still && ev.eventTime - touchDownAt < TAP_MAX_MS) {
                    nativeTouch(0, 0, 1)
                    cursor?.showFor(CURSOR_SHOW_MS)
                }
            }
        }
        return true
    }

    /** L3 + R3 together toggles the performance overlay; the keys still reach SDL. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val down = event.action == KeyEvent.ACTION_DOWN
        // Debug builds: pad keys from a virtual device (adb shell input gamepad keyevent ...) go to the SDL bridge
        // as if pressed on the real pad; SDL itself doesn't treat that device as a gamepad.
        if (BuildConfig.DEBUG && event.device?.isVirtual != false) {
            val button = INJECT_BUTTONS[event.keyCode]
            if (button != null) {
                if (event.repeatCount == 0) nativeInjectButton(button, down)
                return true
            }
        }
        when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_THUMBL -> l3 = down
            KeyEvent.KEYCODE_BUTTON_THUMBR -> r3 = down
        }
        if (down && event.repeatCount == 0 && l3 && r3 &&
            (event.keyCode == KeyEvent.KEYCODE_BUTTON_THUMBL || event.keyCode == KeyEvent.KEYCODE_BUTTON_THUMBR)) {
            nativeHandheld("set perf=toggle") // the overlay follows on the next frame
        }
        return super.dispatchKeyEvent(event)
    }

    private var resumed = false
    private var gyroWanted = false
    private var fpsApplied = -1

    override fun onResume() {
        super.onResume()
        resumed = true
        if (gyroWanted) gyro?.start()
    }

    override fun onPause() {
        resumed = false
        gyro?.stop()
        super.onPause()
    }

    /**
     * Handheld setting "frame rate": ask for the display mode nearest [fps] (0 = the panel's fastest). The game's
     * vsync then runs at that rate, so a lower limit saves GPU, CPU and panel power alike.
     */
    private fun applyFrameRateLimit(fps: Int) {
        if (fps == fpsApplied) return
        fpsApplied = fps
        val d = display ?: return
        val cur = d.mode
        val modes = d.supportedModes.filter { it.physicalWidth == cur.physicalWidth && it.physicalHeight == cur.physicalHeight }
        val pick = (if (fps <= 0) modes.maxByOrNull { it.refreshRate }
                    else modes.minByOrNull { kotlin.math.abs(it.refreshRate - fps) }) ?: return
        window.attributes = window.attributes.also { it.preferredDisplayModeId = pick.modeId }
        android.util.Log.i("UTAndroid", "frame rate limit $fps -> display mode ${pick.refreshRate} Hz")
    }

    override fun onDestroy() {
        cursor?.stop()
        perf?.release()
        super.onDestroy()
    }

    companion object {
        /** From libutloader: [menu (0/1), cursor x, cursor y, window w, window h] in SDL window coordinates. */
        @JvmStatic external fun nativeCursorState(out: FloatArray)

        /** Handheld settings command (see utl_handheld_command in libutloader), e.g. "set perf=toggle". */
        @JvmStatic external fun nativeHandheld(cmd: String)

        /** Gyro aiming: angle turned since the last call, micro-radians (yaw right +, pitch up +). */
        @JvmStatic external fun nativeGyro(yaw: Int, pitch: Int)

        /** Touchpad cursor: move by (dx, dy) window pixels, then click [taps] times. */
        @JvmStatic external fun nativeTouch(dx: Int, dy: Int, taps: Int)

        private const val TOUCH_SPEED = 1.5f    // cursor pixels per finger pixel
        private const val TAP_SLOP_DP = 12f     // a touch that travels less than this is a tap
        private const val TAP_MAX_MS = 300L
        private const val CURSOR_SHOW_MS = 3000L

        /** Debug: press/release virtual-pad button [button] (SDL_utandroid.c layout) as if on the real pad. */
        @JvmStatic external fun nativeInjectButton(button: Int, down: Boolean)

        /** Android key code -> virtual-pad button (Joy1..16 order). */
        private val INJECT_BUTTONS = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to 0, KeyEvent.KEYCODE_BUTTON_B to 1,
            KeyEvent.KEYCODE_BUTTON_X to 2, KeyEvent.KEYCODE_BUTTON_Y to 3,
            KeyEvent.KEYCODE_BUTTON_L1 to 4, KeyEvent.KEYCODE_BUTTON_R1 to 5,
            KeyEvent.KEYCODE_BUTTON_L2 to 6, KeyEvent.KEYCODE_BUTTON_R2 to 7,
            KeyEvent.KEYCODE_BUTTON_SELECT to 8, KeyEvent.KEYCODE_BUTTON_START to 9,
            KeyEvent.KEYCODE_BUTTON_THUMBL to 10, KeyEvent.KEYCODE_BUTTON_THUMBR to 11,
            KeyEvent.KEYCODE_DPAD_UP to 12, KeyEvent.KEYCODE_DPAD_DOWN to 13,
            KeyEvent.KEYCODE_DPAD_LEFT to 14, KeyEvent.KEYCODE_DPAD_RIGHT to 15,
        )
    }
}
