package com.lscardinali.unrealtournament

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.os.SystemClock
import android.view.Choreographer
import android.view.View

/**
 * The menu cursor for the native port. UT's menus use the system mouse cursor, which Android only draws for a
 * real pointer device. The cursor is a touchpad fallback (NativeGameActivity.dispatchTouchEvent): this view draws an
 * arrow at its position while UT is in menu mode and the touchpad was used recently ([showFor]), polled once per
 * display frame from the loader
 * ([read] = NativeGameActivity.nativeCursorState: menu, x, y, window w, window h).
 */
class CursorOverlay(context: Context, private val read: (FloatArray) -> Unit) : View(context), Choreographer.FrameCallback {
    private val state = FloatArray(8)
    private val scale = resources.displayMetrics.density
    private val arrow = Path().apply {
        // A classic pointer, tip at (0,0), about 24 dp tall.
        moveTo(0f, 0f); lineTo(0f, 17f); lineTo(4.5f, 13f); lineTo(7.5f, 20f); lineTo(10.5f, 18.6f)
        lineTo(7.6f, 11.8f); lineTo(13.5f, 11.8f); close()
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; style = Paint.Style.FILL }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK; style = Paint.Style.STROKE; strokeWidth = 1.2f; strokeJoin = Paint.Join.ROUND
    }
    private var visibleNow = false
    private var x = 0f
    private var y = 0f
    private var running = false
    private var shownUntil = 0L

    /** The cursor is hidden unless the touchpad was used in the last [ms] milliseconds. */
    fun showFor(ms: Long) {
        shownUntil = SystemClock.uptimeMillis() + ms
    }

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
        try {
            read(state)
        } catch (e: UnsatisfiedLinkError) {
            // libutloader not loaded yet
        }
        val menu = state[0] > 0.5f && state[3] > 0f && state[4] > 0f && SystemClock.uptimeMillis() < shownUntil
        val nx = if (menu) state[1] * width / state[3] else x
        val ny = if (menu) state[2] * height / state[4] else y
        if (menu != visibleNow || nx != x || ny != y) {
            visibleNow = menu
            x = nx
            y = ny
            invalidate()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onDraw(canvas: Canvas) {
        if (!visibleNow) return
        canvas.save()
        canvas.translate(x, y)
        canvas.scale(scale * 1.4f, scale * 1.4f)
        canvas.drawPath(arrow, fill)
        canvas.drawPath(arrow, outline)
        canvas.restore()
    }
}
