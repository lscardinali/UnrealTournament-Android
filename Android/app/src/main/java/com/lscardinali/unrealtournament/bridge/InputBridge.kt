package com.lscardinali.unrealtournament.bridge

import android.content.Context
import android.net.LocalSocket
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import java.io.DataInputStream
import java.io.File
import java.io.IOException
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * The gamepad, shaped for UT469's SDLDrv and sent to our SDL's virtual joystick (SDL_utandroid.c).
 *
 * Wire: 4-byte messages: kind, index, little-endian int16 value.
 *   to game:   'B' button 0..15 (1/0), 'A' axis 0..3 (-32767..32767)
 *   from game: 'R' rumble strength, 'M' 1 = menu cursor mode / 0 = game mode
 *
 * Buttons (-> UT Joy1..Joy16): A B X Y L1 R1 L2 R2 Select Start L3 R3 DUp DDown DLeft DRight.
 * Axes (-> UT JoyX JoyY JoyZ JoyR): left X, left Y, right X, right Y; radial dead zone applied here.
 */
object InputBridge {
    private const val TAG = "UTAndroid"
    private const val AXIS_MAX = 32767
    private const val STICK_DEADZONE = 0.18f
    private const val TRIGGER_ON = 0.45f
    private const val TRIGGER_OFF = 0.30f
    private const val RUMBLE_MS = 2500L

    const val BTN_A = 0; const val BTN_B = 1; const val BTN_X = 2; const val BTN_Y = 3
    const val BTN_L1 = 4; const val BTN_R1 = 5; const val BTN_L2 = 6; const val BTN_R2 = 7
    const val BTN_SELECT = 8; const val BTN_START = 9; const val BTN_L3 = 10; const val BTN_R3 = 11
    const val BTN_DUP = 12; const val BTN_DDOWN = 13; const val BTN_DLEFT = 14; const val BTN_DRIGHT = 15

    private val KEYMAP = mapOf(
        KeyEvent.KEYCODE_BUTTON_A to BTN_A, KeyEvent.KEYCODE_BUTTON_B to BTN_B,
        KeyEvent.KEYCODE_BUTTON_X to BTN_X, KeyEvent.KEYCODE_BUTTON_Y to BTN_Y,
        KeyEvent.KEYCODE_BUTTON_L1 to BTN_L1, KeyEvent.KEYCODE_BUTTON_R1 to BTN_R1,
        KeyEvent.KEYCODE_BUTTON_L2 to BTN_L2, KeyEvent.KEYCODE_BUTTON_R2 to BTN_R2,
        KeyEvent.KEYCODE_BUTTON_SELECT to BTN_SELECT, KeyEvent.KEYCODE_BACK to BTN_SELECT,
        KeyEvent.KEYCODE_BUTTON_START to BTN_START,
        KeyEvent.KEYCODE_BUTTON_THUMBL to BTN_L3, KeyEvent.KEYCODE_BUTTON_THUMBR to BTN_R3,
        KeyEvent.KEYCODE_DPAD_UP to BTN_DUP, KeyEvent.KEYCODE_DPAD_DOWN to BTN_DDOWN,
        KeyEvent.KEYCODE_DPAD_LEFT to BTN_DLEFT, KeyEvent.KEYCODE_DPAD_RIGHT to BTN_DRIGHT,
    )

    @Volatile private var client: LocalSocket? = null
    @Volatile var menuMode = false
        private set

    /** Called on the main thread when L3 and R3 are pressed together (toggles the performance overlay). */
    @Volatile var onStickClickCombo: (() -> Unit)? = null
    private val out = ByteArray(4)

    // Last sent state, to send only changes (main thread).
    private val buttons = BooleanArray(16)
    private val axes = IntArray(4)
    private var hatButtons = 0     // DPad pressed through HAT axes, as a bitmask over BTN_DUP..BTN_DRIGHT
    private var keyDpadButtons = 0 // DPad pressed through key events
    // L2/R2 come as keys (BUTTON_L2/R2), as analog axes, or both: each source keeps its own state and the
    // button is down while either says so — otherwise a stick motion event (trigger axis at 0) would release a
    // trigger held through its key.
    private var triggerL = false     // from the axis
    private var triggerR = false
    private var keyTriggerL = false  // from the key
    private var keyTriggerR = false

    fun start(context: Context, socket: File) = UnixServer.serve(socket) { accepted ->
        val vibrator = context.getSystemService(Vibrator::class.java)
        client = accepted
        Log.i(TAG, "Game connected to the gamepad")
        resendAll()
        val msg = ByteArray(4)
        try {
            val input = DataInputStream(accepted.inputStream)
            while (true) {
                input.readFully(msg)
                val value = (msg[2].toInt() and 0xFF) or ((msg[3].toInt() and 0xFF) shl 8)
                when (msg[0].toInt().toChar()) {
                    'R' -> if (value == 0) vibrator?.cancel()
                           else vibrator?.vibrate(VibrationEffect.createOneShot(RUMBLE_MS, (value * 255 / 32767).coerceIn(1, 255)))
                    'M' -> { menuMode = value != 0; Log.i(TAG, "Menu cursor mode: $menuMode") }
                }
            }
        } catch (e: IOException) {
            // Game closed its joystick.
        } finally {
            vibrator?.cancel()
            client = null
            Log.i(TAG, "Game disconnected from the gamepad")
        }
    }

    fun isGamepad(device: InputDevice?): Boolean {
        val s = device?.sources ?: return false
        return s and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            s and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK ||
            s and InputDevice.SOURCE_DPAD == InputDevice.SOURCE_DPAD
    }

    /** From Activity.dispatchKeyEvent. Returns true when consumed. */
    fun onKey(event: KeyEvent): Boolean {
        val button = KEYMAP[event.keyCode] ?: return false
        val fromPad = event.isFromSource(InputDevice.SOURCE_GAMEPAD) || event.isFromSource(InputDevice.SOURCE_JOYSTICK) ||
            isGamepad(event.device)
        if (!fromPad && event.keyCode != KeyEvent.KEYCODE_BACK) return false
        if (event.repeatCount > 0) return true
        val down = event.action == KeyEvent.ACTION_DOWN
        if (button in BTN_DUP..BTN_DRIGHT) {
            val bit = 1 shl (button - BTN_DUP)
            keyDpadButtons = if (down) keyDpadButtons or bit else keyDpadButtons and bit.inv()
            setButton(button, (keyDpadButtons or hatButtons) and bit != 0)
        } else if (button == BTN_L2) {
            keyTriggerL = down
            setButton(BTN_L2, keyTriggerL || triggerL)
        } else if (button == BTN_R2) {
            keyTriggerR = down
            setButton(BTN_R2, keyTriggerR || triggerR)
        } else {
            setButton(button, down)
            if (down && (button == BTN_L3 || button == BTN_R3) && buttons[BTN_L3] && buttons[BTN_R3]) {
                onStickClickCombo?.invoke()
            }
        }
        return true
    }

    /** From Activity.dispatchGenericMotionEvent. Returns true when consumed. */
    fun onMotion(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        val dev = event.device

        stick(event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y), 0)
        stick(event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ), 2)

        val lt = maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE))
        val rt = maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS))
        if (dev?.getMotionRange(MotionEvent.AXIS_LTRIGGER) != null || dev?.getMotionRange(MotionEvent.AXIS_BRAKE) != null) {
            triggerL = if (triggerL) lt > TRIGGER_OFF else lt > TRIGGER_ON
            setButton(BTN_L2, triggerL || keyTriggerL)
        }
        if (dev?.getMotionRange(MotionEvent.AXIS_RTRIGGER) != null || dev?.getMotionRange(MotionEvent.AXIS_GAS) != null) {
            triggerR = if (triggerR) rt > TRIGGER_OFF else rt > TRIGGER_ON
            setButton(BTN_R2, triggerR || keyTriggerR)
        }

        val hx = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        var hat = 0
        if (hy < -0.5f) hat = hat or (1 shl (BTN_DUP - BTN_DUP))
        if (hy > 0.5f) hat = hat or (1 shl (BTN_DDOWN - BTN_DUP))
        if (hx < -0.5f) hat = hat or (1 shl (BTN_DLEFT - BTN_DUP))
        if (hx > 0.5f) hat = hat or (1 shl (BTN_DRIGHT - BTN_DUP))
        if (hat != hatButtons) {
            hatButtons = hat
            for (i in 0..3) setButton(BTN_DUP + i, (hatButtons or keyDpadButtons) and (1 shl i) != 0)
        }
        return true
    }

    /** Radial dead zone, rescaled so motion starts smoothly at the edge of the zone. */
    private fun stick(x: Float, y: Float, firstAxis: Int) {
        val mag = sqrt(x * x + y * y)
        var sx = 0f
        var sy = 0f
        if (mag > STICK_DEADZONE) {
            val scaled = ((mag - STICK_DEADZONE) / (1f - STICK_DEADZONE)).coerceAtMost(1f)
            sx = x / mag * scaled
            sy = y / mag * scaled
        }
        setAxis(firstAxis, (sx * AXIS_MAX).toInt())
        setAxis(firstAxis + 1, (sy * AXIS_MAX).toInt())
    }

    private fun setButton(index: Int, down: Boolean) {
        if (buttons[index] == down) return
        buttons[index] = down
        send('B', index, if (down) 1 else 0)
    }

    private fun setAxis(index: Int, value: Int) {
        val v = value.coerceIn(-AXIS_MAX, AXIS_MAX)
        if (axes[index] == v || (abs(axes[index] - v) < 64 && v != 0)) return
        axes[index] = v
        send('A', index, v)
    }

    private fun resendAll() {
        for (i in buttons.indices) send('B', i, if (buttons[i]) 1 else 0)
        for (i in axes.indices) send('A', i, axes[i])
    }

    private fun send(kind: Char, index: Int, value: Int) {
        val socket = client
        if (socket == null) return
        try {
            synchronized(out) {
                out[0] = kind.code.toByte()
                out[1] = index.toByte()
                out[2] = value.toByte()
                out[3] = (value shr 8).toByte()
                socket.outputStream.write(out)
            }
        } catch (e: IOException) {
            Log.w(TAG, "send $kind $index failed: $e") // the reader thread handles the disconnect
        }
    }
}
