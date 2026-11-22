package com.lscardinali.unrealtournament

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.lscardinali.unrealtournament.runtime.GlibcProgram
import com.lscardinali.unrealtournament.setup.AppAssets
import com.lscardinali.unrealtournament.setup.GameInstall
import java.io.File
import kotlin.concurrent.thread

/**
 * Dev harness: runs a UT binary through the glibc loader and shows its output.
 * Intent extras (adb shell am start -n ... --es bin ucc-bin-arm64 --es args "help"):
 *   bin  — binary in game/System (default ucc-bin-arm64)
 *   args — space separated args (default "help")
 */
class MainActivity : ComponentActivity() {
    private val output = mutableStateOf("starting…")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            Text(
                text = output.value,
                color = Color.Green,
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black)
                    .padding(8.dp)
                    .verticalScroll(rememberScrollState()),
            )
        }

        val bin = intent.getStringExtra("bin")
        // No game yet (or "--ez setup true"): the setup screen downloads and installs it.
        if (bin == null && (intent.getBooleanExtra("setup", false) || !GameInstall.isInstalled(this))) {
            startActivity(android.content.Intent(this, SetupActivity::class.java))
            finish()
            return
        }
        if (bin == null) {
            AppAssets.sync(this) // libgcc/libstdc++ and PadUI from this APK
            // Normal launch: straight into the native (in-process) build. Extra "x11": the X server build
            // (glibc process + Termux:X11), kept as a fallback.
            val target = if (intent.getBooleanExtra("x11", false)) GameActivity::class.java else NativeGameActivity::class.java
            // All launch extras pass through (args, env, sound, vkdebug, ...).
            startActivity(android.content.Intent(this, target).putExtras(intent))
            finish()
            return
        }
        val args = (intent.getStringExtra("args") ?: "help").split(' ').filter { it.isNotBlank() }
        thread(name = "ut-runner") {
            val runtime = GlibcProgram(this)
            val log = File(runtime.files, "logs/run.log").apply { parentFile?.mkdirs() }
            val text = try {
                val p = runtime.startGame(bin, args, log)
                val code = p.waitFor()
                "exit=$code\n" + log.readText()
            } catch (t: Throwable) {
                "failed: $t"
            }
            Log.i(TAG, text)
            runOnUiThread { output.value = text }
        }
    }

    companion object {
        const val TAG = "UTAndroid"
    }
}
