# Game setup (download + install)

First launch without a game (or `am start ... --ez setup true` to reinstall) opens `SetupActivity`, which does what
OldUnreal's Linux installer does (FullGameInstallers/Linux/install-ut99.sh):

1. `UT_GOTY_CD1.ISO` (649633792 B, sha256 e184984c…) from files/files2/files3.oldunreal.net (shuffled), then
   archive.org. Resumable (`.part` + Range), 3 attempts per source, SHA-256 checked.
2. `utbonuspack4-zip.7z` (sha256 5b7a1080…) from the same mirrors.
3. The latest `-linux-arm64` asset of OldUnreal/UnrealTournamentPatches (GitHub API, sha256 from the asset digest;
   falls back to v469f-rc6 `OldUnreal-UTPatch469f-Linux-arm64.tar.bz2`, sha256 56c09d3f…).
4. ISO (Joliet reader `IsoReader`): System, Maps, Textures, Sounds, Music, Help, Web, minus System/*.ini/*.bat/*.dll/
   *.exe and all translation files. The 81 `.uz` maps are decompressed in Kotlin (`UzDecoder`, a port of FCodecFull:
   Huffman -> RLE -> MTF -> BWT -> RLE), byte-identical to the retail maps (SetupDataTest).
5. Bonus Pack 4 (commons-compress SevenZFile, LZMA2 via xz), then the patch tar.bz2 over everything.
6. Fix-ups: DOM-Cybrosis][ copy; System/ translations removed when SystemLocalized/ has them.
7. Swap in: built in `files/game.new` (+ `<sd>/game.new`), Save/ carried over, old install deleted, marker
   `game/.utandroid-install.json` written, downloads deleted. Then `AppAssets` copies libgcc_s/libstdc++ (files/glibc)
   and PadUI.u/.int from the APK (also on every app update).

SD card: program files can't run from the card (exfat/FUSE, noexec), so System/ etc. stay internal (~175 MB) and
Maps/Textures/Sounds/Music (~670 MB) go to `/storage/<card>/Android/data/<pkg>/files/game`, linked from
`files/game` with symlinks. The ISO is also downloaded to the card then.

Measured on the RP6 (Wi-Fi): full SD install in ~80 s (ISO at ~31 MB/s). The game started from it (city intro,
~119 fps). PC test (`./gradlew :app:testDebugUnitTest --tests *SetupDataTest*`): full install into split folders,
96 maps, same as the dev install.

UI: UT99 look (gunmetal, bevelled steel plates with rivets, bronze/gold title and focus, red accent), regular type.
Pad: D-pad focus (the screen switches Compose to keyboard input mode so the main button is focused on open),
A = select, B = back.

Limits: the setup runs in the activity (no foreground service); leaving the app mid-install may stop it, and a retry
resumes the downloads. Reinstall/move to SD is only reachable with `--ez setup true` so far.
