package com.lscardinali.unrealtournament.setup

import android.content.Context
import java.io.File

/**
 * Files the APK carries for the installed game, refreshed whenever the app is updated:
 *   runtime/libgcc_s.so.1, libstdc++.so.6 -> filesDir/glibc (the native build's only glibc-side libraries)
 *   padui/PadUI.u -> game/System, padui/PadUI.int -> game/SystemLocalized/int (controller UI, gyro settings)
 */
object AppAssets {
    fun sync(ctx: Context, force: Boolean = false) {
        val prefs = ctx.getSharedPreferences("utandroid", Context.MODE_PRIVATE)
        val stamp = ctx.packageManager.getPackageInfo(ctx.packageName, 0).lastUpdateTime
        val game = GameInstall.gameDir(ctx)
        val targets = listOf(
            "runtime/libgcc_s.so.1" to File(ctx.filesDir, "glibc/libgcc_s.so.1"),
            "runtime/libstdc++.so.6" to File(ctx.filesDir, "glibc/libstdc++.so.6"),
            "padui/PadUI.u" to File(game, "System/PadUI.u"),
            "padui/PadUI.int" to File(game, "SystemLocalized/int/PadUI.int"),
        )
        if (!force && prefs.getLong("assetsStamp", 0) == stamp && targets.all { it.second.exists() }) return
        for ((asset, dest) in targets) {
            dest.parentFile?.mkdirs()
            val tmp = File(dest.path + ".tmp")
            ctx.assets.open(asset).use { input -> tmp.outputStream().use { input.copyTo(it) } }
            dest.delete()
            tmp.renameTo(dest)
        }
        prefs.edit().putLong("assetsStamp", stamp).apply()
    }
}
