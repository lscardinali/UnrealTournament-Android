package com.lscardinali.unrealtournament.setup

import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import kotlin.concurrent.thread

/** The running setup, outliving the activity (one per process). */
object InstallTask {
    enum class Step { DownloadGame, DownloadBonus, DownloadPatch, Unpack, Patch, Finish }

    /** The steps this game's setup shows (Unreal has no bonus pack). */
    fun steps(p: Product = Product.current): List<Step> = Step.entries.filter { it != Step.DownloadBonus || p.bonus != null }

    fun title(s: Step, p: Product = Product.current): String = when (s) {
        Step.DownloadGame -> "Download ${p.iso.name}"
        Step.DownloadBonus -> "Download ${p.bonus?.name}"
        Step.DownloadPatch -> "Download the OldUnreal ${p.knownPatch.name}"
        Step.Unpack -> "Unpack the game"
        Step.Patch -> if (p.bonus != null) "Apply ${p.bonus.name} and the patch" else "Apply the patch"
        Step.Finish -> "Finish setup"
    }

    data class State(
        val running: Boolean = false,
        val step: Int = -1,
        val fraction: Float = 0f,
        val detail: String = "",
        val error: String? = null,
        val done: Boolean = false,
    )

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state
    @Volatile private var cancel = false

    /** The SD card folder of the last start (null = internal), for Retry. */
    var lastSdCard: File? = null
        private set

    fun start(ctx: Context, sdCard: File?) {
        if (_state.value.running) return
        cancel = false
        lastSdCard = sdCard
        _state.value = State(running = true, step = 0)
        val app = ctx.applicationContext
        thread(name = "ut-setup") {
            val layout = GameInstall.Layout(app, sdCard)
            try {
                run(app, layout)
                _state.value = State(done = true, step = Step.entries.size)
            } catch (e: CancelledException) {
                // The user stopped it: leave nothing behind (the installed game, if any, is untouched).
                GameInstall.deleteTree(layout.downloads)
                layout.clearStage()
                _state.value = State()
            } catch (e: Throwable) {
                Log.e("UTAndroid", "setup failed", e)
                _state.value = _state.value.copy(running = false, error = e.message ?: e.toString())
            }
        }
    }

    fun cancel() {
        cancel = true
    }

    private fun run(ctx: Context, layout: GameInstall.Layout) {
        val ua = "UTAndroid-Setup/1.0"
        val dl = Downloader(ua) { cancel }
        layout.downloads.mkdirs()
        val product = Product.current
        val iso = File(layout.downloads, product.isoFile)
        val bonus = product.bonusFile?.let { File(layout.downloads, it) }

        step(Step.DownloadGame)
        dl.fetch(product.iso, iso, ::bytes) { detail("Verifying the download…") }
        if (product.bonus != null && bonus != null) {
            step(Step.DownloadBonus)
            dl.fetch(product.bonus, bonus, ::bytes) { detail("Verifying the download…") }
        }
        step(Step.DownloadPatch)
        detail("Looking up the latest release…")
        val patchSource = GameInstall.latestPatch(ua)
        val patch = File(layout.downloads, patchSource.urls.first().substringAfterLast('/'))
        dl.fetch(patchSource, patch, ::bytes) { detail("Verifying the download…") }

        layout.clearStage()
        val installer = GameInstaller(layout.systemStage, layout.dataStage, { cancel }, product.installer) { f, d ->
            _state.value = _state.value.copy(fraction = f, detail = d)
        }
        step(Step.Unpack)
        installer.extractIso(iso)
        iso.delete() // 650 MB back before the rest
        step(Step.Patch)
        if (bonus != null) installer.extractBonusPack(bonus)
        installer.extractPatch(patch)
        step(Step.Finish)
        detail("Putting the game in place…")
        installer.finish()
        if (cancel) throw CancelledException()
        layout.commit()
        AppAssets.sync(ctx, force = true)
    }

    private var lastBytes = 0L
    private var lastNs = 0L
    private var speed = 0.0

    private fun step(s: Step) {
        lastNs = 0
        speed = 0.0
        _state.value = _state.value.copy(step = s.ordinal, fraction = 0f, detail = "")
    }

    private fun detail(d: String) {
        _state.value = _state.value.copy(detail = d)
    }

    private fun bytes(done: Long, total: Long) {
        val now = System.nanoTime()
        if (lastNs != 0L && now > lastNs) {
            val inst = (done - lastBytes) / ((now - lastNs) / 1e9)
            speed = if (speed == 0.0) inst else speed * 0.8 + inst * 0.2
        }
        lastNs = now
        lastBytes = done
        val mb = 1_000_000.0
        val text = buildString {
            append("%.0f".format(done / mb))
            if (total > 0) append(" / %.0f MB".format(total / mb)) else append(" MB")
            if (speed > 0) append("  ·  %.1f MB/s".format(speed / mb))
        }
        _state.value = _state.value.copy(fraction = if (total > 0) done.toFloat() / total else 0f, detail = text)
    }
}
