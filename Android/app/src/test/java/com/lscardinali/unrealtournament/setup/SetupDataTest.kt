package com.lscardinali.unrealtournament.setup

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/**
 * Checks the setup's readers against real data on the dev PC (skipped when it isn't there):
 *   downloads/UT_GOTY_CD1.iso (OldUnreal's GOTY image) and UnrealTournament/Maps (a retail install, maps already
 *   decompressed by the original installer). Tests run from Android/app.
 */
class SetupDataTest {
    // The repo root (tests run from Android/app).
    private val root = File("../..").canonicalFile
    private val iso = File(root, "downloads/UT_GOTY_CD1.iso")
    private val retailMaps = File(root, "UnrealTournament/Maps")

    @Test
    fun isoListsTheGame() {
        assumeTrue(iso.exists())
        IsoReader(iso).use { r ->
            val paths = r.entries().map { it.path }.toSet()
            assertTrue("System/Engine.u" in paths)
            assertTrue(paths.any { it.startsWith("Maps/") && it.endsWith(".unr.uz") })
            assertTrue("Textures/LadderFonts.utx" in paths)
        }
    }

    @Test
    fun fullInstallWithSplitDataFolder() {
        val bp4 = File(root, "downloads/utbonuspack4-zip.7z")
        val patch = File(root, "OldUnreal-UTPatch469f-Linux-arm64.tar.bz2")
        assumeTrue(iso.exists() && bp4.exists() && patch.exists())
        val tmp = java.nio.file.Files.createTempDirectory("ut-setup-test").toFile()
            .also { d -> Runtime.getRuntime().addShutdownHook(Thread { d.deleteRecursively() }) }
        val sys = File(tmp, "internal")
        val data = File(tmp, "sdcard")
        val inst = GameInstaller(sys, data, { false }) { _, _ -> }
        inst.extractIso(iso)
        inst.extractBonusPack(bp4)
        inst.extractPatch(patch)
        inst.finish()
        for (rel in listOf("System/ut-bin-arm64", "System/Engine.u", "System/libSDL3.so.0", "System/Default.ini",
                           "SystemLocalized/int/Engine.int", "Help/Splash2.bmp")) {
            assertTrue(rel, File(sys, rel).isFile)
        }
        for (rel in listOf("Maps/DM-Deck16][.unr", "Maps/CTF-Beatitude.unr", "Maps/DOM-Cybrosis][.unr",
                           "Textures/SGTech1.utx", "Sounds/Announcer.uax", "Music/Foregone.umx")) {
            assertTrue(rel, File(data, rel).isFile)
        }
        assertTrue(File(sys, "Maps").exists().not())
        assertTrue(File(data, "Maps").listFiles()!!.none { it.name.endsWith(".uz") })
        assertTrue(File(sys, "System").listFiles()!!.none { it.extension in setOf("exe", "dll", "int") && it.name != "skeletalchars.int" })
        // Same maps as the dev install (retail + BP4).
        val ours = File(data, "Maps").list()!!.map { it.lowercase() }.toSet()
        val dev = retailMaps.list()?.map { it.lowercase() }?.toSet().orEmpty()
        println("maps: ours ${ours.size}, dev ${dev.size}, missing ${dev - ours}, extra ${ours - dev}")
        println("internal MB ${sys.walkTopDown().filter { it.isFile }.sumOf { it.length() } shr 20}, " +
                "data MB ${data.walkTopDown().filter { it.isFile }.sumOf { it.length() } shr 20}")
    }

    @Test
    fun unrealGoldWith227() {
        val ug = File(root, "downloads/UNREAL_GOLD.iso")
        val patch = File(root, "downloads/OldUnreal-UnrealPatch227k-Linux.tar.bz2")
        assumeTrue(ug.exists() && patch.exists())
        val tmp = java.nio.file.Files.createTempDirectory("unreal-setup-test").toFile()
            .also { d -> Runtime.getRuntime().addShutdownHook(Thread { d.deleteRecursively() }) }
        val sys = File(tmp, "internal")
        val data = File(tmp, "sdcard")
        val inst = GameInstaller(sys, data, { false }, GameInstaller.Game.UNREAL) { _, _ -> }
        inst.extractIso(ug)
        inst.extractPatch(patch)
        inst.finish()
        for (rel in listOf("SystemARM64/unreal-bin-arm64", "SystemARM64/XOpenGLDrv.so", "SystemARM64/libSDL3.so.0",
                           "SystemARM64/DefaultLinux.ini", "SystemARM64/DefUser.ini", "System/UMenu.u",
                           "System/UnrealShare.u", "System/DefaultLinux.ini", "Help/Unreal.ico")) {
            assertTrue(rel, File(sys, rel).isFile)
        }
        for (rel in listOf("Maps/Vortex2.unr", "Maps/Entry.unr", "Textures/Ancient.utx", "Music/Vortex.umx")) {
            assertTrue(rel, File(data, rel).isFile)
        }
        // Exact names (the PC's file system ignores case).
        assertEquals(listOf("Help", "System", "SystemARM64", "SystemLocalized", "WebServer"), sys.list()!!.sorted())
        assertEquals(listOf("Maps", "Music", "Sounds", "Textures"), data.list()!!.sorted())
        val system = File(sys, "System").list()!!.toList()
        assertTrue(system.none { it.lowercase().endsWith(".exe") || it.lowercase().endsWith(".dll") })
        assertTrue(system.none { it.contains(".so") || it.endsWith("-x86") }) // 227's x86 build
        println("unreal: maps ${File(data, "Maps").list()!!.size}, " +
                "internal MB ${sys.walkTopDown().filter { it.isFile }.sumOf { it.length() } shr 20}, " +
                "data MB ${data.walkTopDown().filter { it.isFile }.sumOf { it.length() } shr 20}")
    }

    @Test
    fun uzMapsMatchTheRetailInstall() {
        assumeTrue(iso.exists() && retailMaps.exists())
        var checked = 0
        IsoReader(iso).use { r ->
            for (e in r.entries().filter { it.path.startsWith("Maps/") && it.path.endsWith(".uz") }) {
                val result = UzDecoder.decode(r.read(e))
                val expected = File(retailMaps, result.fileName)
                if (!expected.exists()) continue
                assertArrayEquals(e.path, expected.readBytes(), result.data)
                checked++
            }
        }
        println("uz maps identical to retail: $checked")
        assertTrue(checked > 50)
    }
}
