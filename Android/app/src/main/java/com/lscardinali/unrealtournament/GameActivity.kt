package com.lscardinali.unrealtournament

import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import com.lscardinali.unrealtournament.bridge.InputBridge
import com.lscardinali.unrealtournament.x11.XDisplay
import kotlin.concurrent.thread

/**
 * The game screen: Termux:X11's display activity (shows the X server), with the gamepad taken off it and sent to
 * the game's SDL as a virtual joystick (InputBridge) instead of being turned into X keys.
 *
 * Extras: "args" — extra ut-bin arguments, e.g. "CTF-Face?Game=Botpack.CTFGame".
 */
class GameActivity : com.termux.x11.MainActivity() {
    private var session: GameSession? = null
    private var perf: PerfOverlay? = null
    private var vsync: VsyncWriter? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        XDisplay.configure(this)
        super.onCreate(savedInstanceState)
        if (session != null) return

        val args = intent.getStringExtra("args")?.split(' ')?.filter { it.isNotBlank() }.orEmpty()
        // The whole panel (the X screen is native size), not the app window minus system bars.
        val bounds = windowManager.maximumWindowMetrics.bounds
        val width = maxOf(bounds.width(), bounds.height())
        val height = minOf(bounds.width(), bounds.height())
        // Fresh frames file before anything maps it (overlay, vsync writer, the game): a mapping of an older file
        // would keep pointing at the deleted copy.
        VsyncWriter.create(GameSession.framesFile(this))
        val prefs = getSharedPreferences("utandroid", MODE_PRIVATE)
        val overlay = PerfOverlay(this, GameSession.framesFile(this))
        perf = overlay
        if (prefs.getBoolean("perfOverlay", true)) overlay.show()
        InputBridge.onStickClickCombo = {
            runOnUiThread {
                overlay.toggle()
                prefs.edit().putBoolean("perfOverlay", overlay.visible).apply()
            }
        }

        val refresh = { display?.refreshRate ?: 60f }
        vsync = VsyncWriter(GameSession.framesFile(this), refresh).also { it.start() }

        val s = GameSession(applicationContext)
        session = s
        thread(name = "ut-session") {
            val code = try {
                val envOverrides = intent.getStringExtra("env").orEmpty().split(',')
                    .filter { '=' in it }.associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }
                s.run(width, height, args, envOverrides, Math.round(refresh()))
            } catch (t: Throwable) {
                Log.e(GameSession.TAG, "Game session failed", t)
                -1
            }
            runOnUiThread { finish() }
            Log.i(GameSession.TAG, "Session ended ($code)")
        }
    }

    // Termux:X11's LorieView takes keys in dispatchKeyEventPreIme — before Activity.dispatchKeyEvent — and hands
    // them to handleKey, which turns them into X keys. Gamepad keys go to the game's virtual joystick instead.
    override fun handleKey(e: KeyEvent): Boolean =
        InputBridge.onKey(e) || super.handleKey(e)

    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        InputBridge.onKey(event) || super.dispatchKeyEvent(event)

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean =
        InputBridge.onMotion(event) || super.dispatchGenericMotionEvent(event)

    override fun onDestroy() {
        InputBridge.onStickClickCombo = null
        vsync?.stop()
        perf?.release()
        if (isFinishing) session?.stop()
        super.onDestroy()
    }
}
