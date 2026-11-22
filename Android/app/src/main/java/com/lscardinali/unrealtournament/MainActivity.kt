package com.lscardinali.unrealtournament

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import com.lscardinali.unrealtournament.setup.AppAssets
import com.lscardinali.unrealtournament.setup.GameInstall

/**
 * Launcher: the setup screen when no game is installed (or with "--ez setup true"), otherwise straight into the
 * game. Launch extras pass through to the game (args, nosound, vkdebug; see NativeGameActivity).
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (intent.getBooleanExtra("setup", false) || !GameInstall.isInstalled(this)) {
            startActivity(Intent(this, SetupActivity::class.java))
        } else {
            AppAssets.sync(this) // libgcc/libstdc++ and PadUI from this APK
            startActivity(Intent(this, NativeGameActivity::class.java).putExtras(intent))
        }
        finish()
    }
}
