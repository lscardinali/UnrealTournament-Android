package com.lscardinali.unrealtournament

import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.lscardinali.unrealtournament.setup.GameInstall
import com.lscardinali.unrealtournament.setup.InstallTask
import com.lscardinali.unrealtournament.setup.Product
import com.lscardinali.unrealtournament.setup.MetalButton
import com.lscardinali.unrealtournament.setup.SelectPlate
import com.lscardinali.unrealtournament.setup.SetupScaffold
import com.lscardinali.unrealtournament.setup.SteelPanel
import com.lscardinali.unrealtournament.setup.StepRow
import com.lscardinali.unrealtournament.setup.UT
import com.lscardinali.unrealtournament.setup.UtProgressBar

/**
 * First-run setup: downloads Unreal Tournament GOTY, Bonus Pack 4 and the 469 patch from OldUnreal (as their own
 * installer does) and installs them to internal storage or the SD card. Controller friendly: D-pad moves the focus,
 * A selects, B goes back.
 */
class SetupActivity : ComponentActivity() {
    /** Compose can only take the D-pad focus once the window has focus. */
    private val windowFocused = mutableStateOf(false)

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        windowFocused.value = hasFocus
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        val sd = GameInstall.sdCardDir(this)
        val reinstall = GameInstall.isInstalled(this)
        setContent {
            val state by InstallTask.state.collectAsState()
            when {
                state.done -> DoneScreen { play() }
                state.running || state.error != null -> ProgressScreen(state)
                else -> WelcomeScreen(sd, reinstall)
            }
        }
    }

    private fun play() {
        startActivity(Intent(this, MainActivity::class.java))
        finish()
    }

    /** Pad: A confirms like the D-pad centre key, B goes back (not during the install). */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            return super.dispatchKeyEvent(KeyEvent(event.downTime, event.eventTime, event.action,
                                                   KeyEvent.KEYCODE_DPAD_CENTER, event.repeatCount))
        }
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            if (event.action == KeyEvent.ACTION_UP && !InstallTask.state.value.running) finish()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /** Controller first: leave touch mode (where clickables can't hold focus) and focus the main button. */
    @OptIn(androidx.compose.ui.ExperimentalComposeUiApi::class)
    @Composable
    private fun FocusOnOpen(focus: FocusRequester, windowHasFocus: Boolean, key: Any = Unit) {
        val inputMode = androidx.compose.ui.platform.LocalInputModeManager.current
        LaunchedEffect(windowHasFocus, key) {
            if (!windowHasFocus) return@LaunchedEffect
            inputMode.requestInputMode(androidx.compose.ui.input.InputMode.Keyboard)
            // The previous screen's focused button was just removed and this screen may still be laying out: retry
            // for a moment until the button holds the focus.
            repeat(10) {
                kotlinx.coroutines.delay(80)
                if (runCatching { focus.requestFocus(); focus.captureFocus().also { if (it) focus.freeFocus() } }
                        .getOrDefault(false)) return@LaunchedEffect
            }
        }
    }

    @Composable
    private fun WelcomeScreen(sd: java.io.File?, reinstall: Boolean) {
        var useSd by remember { mutableStateOf(sd != null) }
        val focus = remember { FocusRequester() }
        val gb = 1024.0 * 1024 * 1024
        SetupScaffold {
            SteelPanel(Modifier.fillMaxWidth(), title = if (reinstall) "Reinstall the game" else "Install the game") {
                Text(Product.current.downloadText, color = UT.Text, fontSize = 13.sp)
                Spacer(Modifier.height(12.dp))
                Text("INSTALL TO", color = UT.TextDim, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
                Spacer(Modifier.height(6.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    SelectPlate("Internal", "%.1f GB free".format(filesDir.usableSpace / gb), !useSd,
                                Modifier.weight(1f)) { useSd = false }
                    SelectPlate("SD card", if (sd != null) "%.1f GB free".format(sd.usableSpace / gb) else "No card inserted",
                                useSd, Modifier.weight(1f), enabled = sd != null) { useSd = true }
                }
                Text(if (useSd) "Maps, textures, sounds and music go on the card; program files (130 MB) stay internal."
                     else "Everything goes on internal storage.",
                     color = UT.TextDim, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
                Spacer(Modifier.height(12.dp))
                Text("The Epic Games Terms of Service apply to the use and distribution of this game, and they " +
                     "supersede any other end user agreements that may accompany the game " +
                     "(legal.epicgames.com/en-US/epicgames/tos).",
                     color = UT.TextDim, fontSize = 11.sp)
                Spacer(Modifier.height(14.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    MetalButton("Accept & install", Modifier.focusRequester(focus), primary = true) {
                        InstallTask.start(this@SetupActivity, if (useSd) sd else null)
                    }
                    if (reinstall) MetalButton("Back") { finish() }
                }
            }
        }
        FocusOnOpen(focus, windowFocused.value)
    }

    @Composable
    private fun ProgressScreen(state: InstallTask.State) {
        val focus = remember { FocusRequester() }
        SetupScaffold {
            SteelPanel(Modifier.fillMaxWidth(), title = if (state.error != null) "Setup stopped" else "Installing") {
                for (s in InstallTask.steps()) {
                    StepRow(InstallTask.title(s), when {
                        s.ordinal < state.step -> 2
                        s.ordinal == state.step -> if (state.error != null) 3 else 1
                        else -> 0
                    })
                }
                Spacer(Modifier.height(16.dp))
                if (state.error == null) {
                    UtProgressBar(state.fraction, Modifier.fillMaxWidth())
                    Text(state.detail, color = UT.TextDim, fontSize = 13.sp, maxLines = 1,
                         modifier = Modifier.padding(top = 8.dp))
                    Spacer(Modifier.height(16.dp))
                    MetalButton("Cancel", Modifier.focusRequester(focus)) { InstallTask.cancel() }
                } else {
                    Text(state.error, color = androidx.compose.ui.graphics.Color(0xFFE57368), fontSize = 14.sp)
                    Text("Downloads already done are kept: retrying picks up where it stopped.", color = UT.TextDim,
                         fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
                    Spacer(Modifier.height(16.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        MetalButton("Retry", Modifier.focusRequester(focus), primary = true) {
                            InstallTask.start(this@SetupActivity, InstallTask.lastSdCard)
                        }
                        MetalButton("Back") { finish() }
                    }
                }
            }
        }
        FocusOnOpen(focus, windowFocused.value, state.error == null)
    }

    @Composable
    private fun DoneScreen(onPlay: () -> Unit) {
        val focus = remember { FocusRequester() }
        SetupScaffold {
            SteelPanel(Modifier.fillMaxWidth(), title = "Ready") {
                Text(Product.current.installedText, color = UT.Text, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                Text("Start opens the menus. Your controller works everywhere in them.", color = UT.TextDim,
                     fontSize = 14.sp, modifier = Modifier.padding(top = 6.dp))
                Spacer(Modifier.height(20.dp))
                Column { MetalButton("Play", Modifier.focusRequester(focus), primary = true, onClick = onPlay) }
            }
        }
        FocusOnOpen(focus, windowFocused.value)
    }
}
