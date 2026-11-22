package com.lscardinali.unrealtournament

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import com.lscardinali.unrealtournament.setup.AppAssets
import com.lscardinali.unrealtournament.setup.GameInstall
import com.lscardinali.unrealtournament.setup.Product

/**
 * Launcher: the setup screen when no game is installed (or with "--ez setup true"), otherwise straight into the
 * game. Launch extras pass through to the game (args, nosound, vkdebug; see NativeGameActivity).
 * The game is the build flavor's (setup/Product.kt): UT, or Unreal Gold in the "unreal" flavor.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (intent.getBooleanExtra("setup", false) || !GameInstall.isInstalled(this)) {
            startActivity(Intent(this, SetupActivity::class.java))
        } else {
            AppAssets.sync(this) // libgcc/libstdc++ and PadUI from this APK
            val game = Intent(this, NativeGameActivity::class.java).putExtras(intent)
            if (Product.current === Product.UNREAL) game.putExtra("game", "unreal") // NativeGameActivity's Unreal path
            startActivity(game)
        }
        finish()
    }
}
