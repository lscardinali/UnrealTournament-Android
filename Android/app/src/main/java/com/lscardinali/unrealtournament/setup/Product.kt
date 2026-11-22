package com.lscardinali.unrealtournament.setup

import com.lscardinali.unrealtournament.BuildConfig

/**
 * The game this app installs and runs: UT 99 (OldUnreal 469) or the original Unreal (Unreal Gold + OldUnreal 227).
 * Picked at build time (BuildConfig.GAME). Download sources follow OldUnreal's FullGameInstallers/Linux scripts.
 */
class Product(
    /** Install folder under filesDir (and on the SD card). */
    val dir: String,
    /** The arm64 executable, relative to the install folder. */
    val exe: String,
    val installer: GameInstaller.Game,
    val iso: Download,
    val isoFile: String,
    val bonus: Download?,
    val bonusFile: String?,
    /** GitHub "latest release" API of the patch repo, and which asset is the Linux (arm64) build. */
    val patchApi: String,
    val patchAsset: (String) -> Boolean,
    val knownPatch: Download,
    // Setup screen text.
    val titleTop: String,
    val titleBottom: String,
    val edition: String,
    val patchLine: String,
    val downloadText: String,
    val installedText: String,
) {
    companion object {
        private val mirrors = listOf("https://files.oldunreal.net", "https://files2.oldunreal.net",
                                     "https://files3.oldunreal.net")

        val UT = Product(
            dir = "game",
            exe = "System/ut-bin-arm64",
            installer = GameInstaller.Game.UT,
            iso = Download("Unreal Tournament GOTY",
                           mirrors.shuffled().map { "$it/UT_GOTY_CD1.ISO" } + "https://archive.org/download/ut-goty/UT_GOTY_CD1.iso",
                           649633792L, "e184984ca88f001c5ddd52035d76cd64e266e26c74975161b5ed72366c74704f"),
            isoFile = "UT_GOTY_CD1.iso",
            bonus = Download("Bonus Pack 4", mirrors.shuffled().map { "$it/utbonuspack4-zip.7z" },
                             11268844L, "5b7a1080724a122a596c226c50d4dc7c2d7636ceaf067e9c12112014a170ffba"),
            bonusFile = "utbonuspack4-zip.7z",
            patchApi = "https://api.github.com/repos/OldUnreal/UnrealTournamentPatches/releases/latest",
            patchAsset = { it.lowercase().contains("-linux-arm64") },
            knownPatch = Download("469 patch",
                listOf("https://github.com/OldUnreal/UnrealTournamentPatches/releases/download/v469f-rc6/OldUnreal-UTPatch469f-Linux-arm64.tar.bz2"),
                null, "56c09d3f7f1889f01acf21638c71952c5e2dfbd737b075ffaea3b94bfe52ef46"),
            titleTop = "UNREAL", titleBottom = "TOURNAMENT",
            edition = "Game of the Year Edition", patchLine = "with the OldUnreal 469 community patch",
            downloadText = "Downloads the game (650 MB) and Bonus Pack 4 from OldUnreal, plus the latest 469 patch. " +
                "About 800 MB installed.",
            installedText = "Unreal Tournament is installed.",
        )

        val UNREAL = Product(
            dir = "unreal",
            exe = "SystemARM64/unreal-bin-arm64",
            installer = GameInstaller.Game.UNREAL,
            iso = Download("Unreal Gold",
                           mirrors.shuffled().map { "$it/UNREAL_GOLD.ISO" } + "https://archive.org/download/totallyunreal/UNREAL_GOLD.ISO",
                           676734976L, "7e360d0cc9e5533f38859819fd3cbfea7c475ecd428f9f433b5f8e1d5742cbca"),
            isoFile = "UNREAL_GOLD.iso",
            bonus = null, bonusFile = null,
            patchApi = "https://api.github.com/repos/OldUnreal/Unreal-testing/releases/latest",
            // One Linux tarball for all architectures (arm64 in SystemARM64/).
            patchAsset = { it.lowercase().contains("-linux") && !it.lowercase().contains("-sdk-") },
            knownPatch = Download("227 patch",
                listOf("https://github.com/OldUnreal/Unreal-testing/releases/download/v227k_15/OldUnreal-UnrealPatch227k-Linux.tar.bz2"),
                131414620L, "b04eba6d5c885c98bd9da8279840b15cd8fa06ed2b750235e7ad7aa26ef3f7a1"),
            titleTop = "UNREAL", titleBottom = "GOLD",
            edition = "Unreal and Return to Na Pali", patchLine = "with the OldUnreal 227 community patch",
            downloadText = "Downloads Unreal Gold (680 MB) from OldUnreal, plus the latest 227 patch (130 MB). " +
                "About 810 MB installed.",
            installedText = "Unreal is installed.",
        )

        val current: Product get() = if (BuildConfig.GAME == "unreal") UNREAL else UT
    }
}
