package com.lscardinali.unrealtournament

import android.app.Activity
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.view.Surface
import kotlin.math.abs

/**
 * Gyro aiming: reads the device gyroscope while the game is in the foreground and hands the angle turned (yaw and
 * pitch, in screen space, micro-radians) to the SDL bridge, which turns it into the virtual pad's gyro axes once per
 * game frame (JoyU = turn, JoyV = look up; see SDL_utandroid.c and research/aim-assist.md).
 *
 * Turning the device left turns left; tilting its top edge towards you looks up. Stick and gyro add together.
 * Very slow rotation (hand tremor) is attenuated ("tightening"), so the view holds still when you do.
 */
class GyroAim(private val activity: Activity, private val send: (yawMicroRad: Int, pitchMicroRad: Int) -> Unit) :
    SensorEventListener {
    private val sensors = activity.getSystemService(SensorManager::class.java)
    private val gyro: Sensor? = sensors?.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
    private var lastNs = 0L
    private var restYaw = 0.0
    private var restPitch = 0.0

    /** Multiplier on the real-world angle (1 = the view turns as far as the device does). */
    var sensitivity = DEFAULT_SENSITIVITY

    fun start() {
        val g = gyro ?: return
        lastNs = 0L
        sensors?.registerListener(this, g, SAMPLING_PERIOD_US)
    }

    fun stop() {
        sensors?.unregisterListener(this)
    }

    override fun onSensorChanged(event: SensorEvent) {
        val ns = event.timestamp
        if (lastNs == 0L) {
            lastNs = ns
            return
        }
        val dt = ((ns - lastNs) / 1e9).coerceIn(0.0, 0.05)
        lastNs = ns
        // Device axes -> screen axes (the activity is landscape either way up).
        val gx = event.values[0].toDouble()
        val gy = event.values[1].toDouble()
        val (sx, sy) = when (activity.display?.rotation ?: Surface.ROTATION_0) {
            Surface.ROTATION_90 -> Pair(-gy, gx)
            Surface.ROTATION_180 -> Pair(-gx, -gy)
            Surface.ROTATION_270 -> Pair(gy, -gx)
            else -> Pair(gx, gy)
        }
        // About the screen's vertical axis = yaw (left is positive), about its horizontal axis = pitch (up positive).
        val yaw = -tighten(sy) * dt * sensitivity * 1e6 + restYaw   // positive = turn right
        val pitch = tighten(sx) * dt * sensitivity * 1e6 + restPitch // positive = look up
        val iy = yaw.toInt()
        val ip = pitch.toInt()
        restYaw = yaw - iy
        restPitch = pitch - ip
        if (iy != 0 || ip != 0) send(iy, ip)
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    /** Below [TIGHTEN_RAD_S] the rate fades out quadratically: removes jitter, keeps slow deliberate aiming. */
    private fun tighten(rate: Double): Double {
        val a = abs(rate)
        return if (a >= TIGHTEN_RAD_S) rate else rate * (a / TIGHTEN_RAD_S)
    }

    companion object {
        const val DEFAULT_SENSITIVITY = 2.0
        private const val SAMPLING_PERIOD_US = 5000          // 200 Hz (higher needs HIGH_SAMPLING_RATE_SENSORS)
        private const val TIGHTEN_RAD_S = 0.75 * Math.PI / 180 // 0.75 deg/s
    }
}
