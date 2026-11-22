# UT99 (OldUnreal 469f, linux-arm64) on Android: Architecture Options

Target: Retroid Pocket 6 (Snapdragon 8 Gen 2 / Adreno 740, Android 13 / API 33, 12 GB RAM).
Input binaries: `linux-arm64/System` (closed-source glibc aarch64 ELF).
Date: 2026-11-21.

**Recommendation: Option B, in-process loading.** Write a custom ELF loader that runs inside a normal
SDL3-for-Android app (bionic). It resolves glibc imports to a bionic shim layer, resolves `libSDL3.so.0` to the
real Android SDL3, and resolves `libopenal.so.1` and the codec libraries to NDK builds. GNU libstdc++/libgcc_s
are still required (see 3.4). Before starting, spend about a day running the binaries unmodified under Termux +
glibc + Turnip + Termux:X11 (Option C) to confirm the game itself works on Adreno/aarch64.

Raw symbol data is in `docs/symbol-imports/`:
`game_glibc.txt`, `game_cxx.txt`, `game_sdl.txt`, `all_glibc.txt`, and `per-module/*.und`.

---

## 1. Binary analysis (measured with NDK r28 llvm-readelf / llvm-nm / llvm-objdump)

### 1.1 General properties

| Property | Finding | Consequence |
|---|---|---|
| `ut-bin-arm64` type | `ET_DYN`, `FLAGS_1 = PIE`, `RUNPATH $ORIGIN`, entry `0x23840` | Can be mapped like a shared library. `main` is **not** exported, so capture it through a `__libc_start_main` shim: jump to `e_entry` with a synthetic argc/argv stack, and `_start` passes `main` to the shim. |
| NEEDED (ut-bin) | SDLDrv.so Engine.so Core.so libSDL3_ttf.so.0 libSDL3.so.0 libm.so.6 libstdc++.so.6 libgcc_s.so.1 libc.so.6 | No libpthread/libdl (glibc ≥ 2.34 style; everything is in libc.so.6). |
| Toolchain | `GCC: (Ubuntu 12.3.0-1ubuntu1~22.04.3) 12.3.0` | Built on Ubuntu 22.04 (glibc 2.35, libstdc++ GLIBCXX_3.4.30). Ubuntu 22.04 arm64 `libstdc++6`/`libgcc-s1` are the exact-match runtime. |
| Max symbol versions | GLIBC_2.34 (game), GLIBCXX_3.4.29, CXXABI_1.3.7 | |
| Relocation types | ABS64, GLOB_DAT, JUMP_SLOT, RELATIVE, TLSDESC, TLS_TPREL64 | **No** COPY relocs, **no** IRELATIVE/IFUNC, no TEXTREL. A minimal relocator covers everything. |
| LOAD alignment | 0x10000 (64 KB) | Fine on 4 KB and 16 KB page kernels. |
| GNU_PROPERTY (BTI/PAC) | none | No BTI landing-pad constraints. |
| OpenAL | OpenAL Soft 1.24.3 (glibc build, needs libatomic.so.1, which is **not bundled**) | Replace it with an NDK build of OpenAL Soft. |
| Vulkan | VulkanDrv `dlopen`s `libvulkan.so.1` / `libvulkan.so` itself and resolves `vkGetInstanceProcAddr`. From SDL it uses only `SDL_Vulkan_LoadLibrary`, `SDL_Vulkan_GetInstanceExtensions`, `SDL_Vulkan_CreateSurface` and the window size calls. | In-process, this maps directly onto Android's system `libvulkan.so` and `VK_KHR_android_surface` from SDL. |
| Cluster.so | NEEDS `libfmod.so.13` (not shipped) | Ignore; it is not needed. |

### 1.2 Import counts per module (`llvm-nm -D --undefined-only`)

| Module | total | GLIBC | GLIBCXX/CXXABI | GCC (libgcc_s) | SDL3/TTF | other (engine/3rd-party) |
|---|---|---|---|---|---|---|
| ut-bin-arm64 | 211 | 68 | 17 | 1 | 16 | 109 |
| Core.so | 185 | 146 | 22 | 12 | 1 | 4 |
| Engine.so | 427 | 51 | 20 | 1 | 0 | 355 |
| Render.so | 173 | 12 | 18 | 1 | 0 | 142 |
| SDLDrv.so | 235 | 10 | 15 | 1 | 79 | 130 |
| VulkanDrv.so | 247 | 65 | 103 | 1 | 6 | 72 |
| ALAudio.so | 243 | 34 | 15 | 1 | 0 | 193 (56 al*/alc*, 11 mpg123, 13 xmp, 3 sf_) |
| IpDrv.so | 253 | 37 | 16 | 1 | 0 | 199 |
| Fire / UWeb / udemo | 95 / 75 / 250 | 5 / 6 / 9 | ~15 | 1 | 0 | rest |
| libSDL3-3.0.so.0 | 245 | 242 | 0 | 0 | – | – |
| libopenal.so.1 | 278 | 121 | 147 | 7 | 0 | – |
| libsndfile/mpg123/xmp/lame | 158/46/64/39 | 95/43/61/36 | 0 | 0 | 0 | – |

**Union across the game modules we need** (ut-bin, Core, Engine, Render, SDLDrv, VulkanDrv, ALAudio, IpDrv,
Fire, UWeb, udemo):

- **242 unique glibc symbols**: 217 @GLIBC_2.17, 3 @2.27, 5 @2.29, 1 @2.32, 1 @2.33, 15 @2.34.
- **106 unique libstdc++ symbols** (GLIBCXX/CXXABI).
- **12 libgcc_s symbols**: `_Unwind_Resume` plus 128-bit long-double soft-float helpers (`__addtf3 __multf3 __subtf3 __eqtf2 __netf2 __unordtf2 __extenddftf2 __fixtfsi __fixunstfsi __floatsitf __floatunsitf`).
- **92 SDL symbols** (83 SDL3 + 9 SDL3_ttf).

The 568-symbol union over *all* shipped `.so` files includes the bundled third-party libraries. We would
replace those libraries with NDK builds, so their imports do not need shimming.

Notably **absent** from the game modules: there are no `__*_chk` fortify functions, no `__isoc99_*scanf`, and no
`*64` LFS aliases. These appear only in the third-party libraries (libopenal, libsndfile, and so on), which we
replace. That keeps the shim surface small.

### 1.3 glibc imports that are NOT a trivial pass-through to bionic

Most of the 242 symbols are name-identical with identical ABI on arm64, such as `memcpy`, `strlen`, `fopen`,
`read`, `socket` and the math functions. arm64 glibc and bionic both use the kernel asm-generic `struct stat`,
`struct dirent`, `timespec`, `utsname`, `rlimit`, `fd_set` and `tm`. `long double` is IEEE binary128 on both, and
`va_list` is the AAPCS64 struct on both, so `vsnprintf`/`FString::Printf(std::__va_list)` pass straight through.
The ones that need work:

| Symbol(s) | Issue | Shim |
|---|---|---|
| `__libc_start_main@2.34` | glibc entry protocol | Implement it; call `main(argc, argv, environ)` on the game thread. |
| `stdin/stdout/stderr`, `environ`, `__libc_single_threaded` | data imports (GLOB_DAT, no COPY) | Export variables pointing to bionic `stdin`/… and set `__libc_single_threaded = 0`. |
| `__errno_location`, `__h_errno_location`, `__getauxval` | glibc-only names | Alias to bionic `__errno`, `__get_h_errno`, `getauxval`. |
| `__ctype_b_loc` (+ `__ctype_tolower/toupper_loc` in libstdc++) | glibc table format (16-bit masks, index −128..255) | Static glibc-format "C" tables. |
| `sigaction`, `signal`, `sigemptyset`, `sigaltstack`, `pthread_sigmask` | **struct sigaction layout differs**: glibc `{handler, sa_mask[128 B], int flags, restorer}`; bionic arm64 `{int flags, handler, sa_mask[8 B], restorer}`. glibc `sigset_t` is 128 B and bionic's is 8 B. | Translate the struct. Refuse or chain SIGSEGV/SIGBUS so ART's fault handlers (libsigchain) survive. |
| `getaddrinfo` / `freeaddrinfo` | **`struct addrinfo` field order differs** (`ai_addr` and `ai_canonname` are swapped) | Deep-copy translation; free our copy. |
| `pthread_mutex_*` | glibc mutex is 48 B, bionic 40 B; zero init means a normal mutex in both | Pass through (bionic fits in glibc storage). Map the mutexattr type values (identical: NORMAL 0, RECURSIVE 1, ERRORCHECK 2). |
| `pthread_attr_*` | glibc 64 B, bionic 56 B | Pass through. |
| `pthread_cond_*`, `pthread_rwlock_*`, `pthread_once`, `sem_*` | same or smaller on bionic | Pass through. |
| `pthread_create@2.34` | Must set up TLS for loaded modules (see 1.4) | Wrapper trampoline. |
| `_setjmp`, `longjmp`, (`__sigsetjmp` in libs) | glibc `jmp_buf` is 312 B, bionic 256 B | Pass through (bionic needs less). Never mix with a glibc `longjmp`. |
| `sysconf` | **`_SC_*` constants differ** (for example `_SC_NPROCESSORS_ONLN` glibc 84 vs bionic 0x61; `_SC_PAGESIZE` 30 vs 0x27) | Mapping table. |
| `dlopen/dlsym/dlclose/dlerror@2.34` | Engine loads `VulkanDrv.so`, `ALAudio.so`, and so on, and VulkanDrv loads `libvulkan.so.1` | Route to the custom loader first. Map `libvulkan.so.1` to bionic `dlopen("libvulkan.so")`. |
| `dl_iterate_phdr` (+ `_dl_find_object` in GCC 12 libgcc_s) | C++ exceptions need FDE lookup for custom-loaded modules | Return custom modules (plus bionic's). |
| `__cxa_atexit`, `__cxa_finalize`, `__cxa_thread_atexit` | Fine in bionic, but libstdc++ wants `__cxa_thread_atexit_impl` | Alias. |
| `stat@2.33` (and `fstat/lstat@2.33` in libs) | Same layout on arm64 | Pass through. |
| `iconv*` | bionic has it (API 28+). Engine uses `UTF-8`, `UTF8`, `UTF-16`, `UTF-16LE`, `WCHAR_T` | Normalize `"UTF8"` → `"UTF-8"`. |
| `fork`, `execv*`, `system`, `waitpid` | Not meaningful in an app | Stub with `ENOSYS` (used for the crash handler and URL opening). |
| `malloc_trim`, `backtrace*`, `getpwent` & co | `malloc_trim` is missing in bionic; backtrace exists from API 33 | Stub or pass through. |
| GCC soft-float `__*tf*` | Not exported by bionic libc | Link compiler-rt builtins statically into the shim, or load libgcc_s. |

Rough estimate: about 200 pass-through entries, about 25–30 real wrappers, and a few data symbols. This matches the
complexity of existing reverse-direction shims (libhybris `hooks.c`, minecraft-linux `libc-shim`), which can be
used as references for the table.

### 1.4 The one hard finding: initial-exec TLS across modules

Every game module has an `R_AARCH64_TLS_TPREL64` relocation against `_ZN15UnGuardBlockTLS4DataE`, a
`thread_local` defined in **Core.so**: `st_size = 0x10008` (≈ 64 KB). Core's PT_TLS is 0x10019 bytes. The
`guard/unguard` macros read it through the **initial-exec** model, `mrs xN, TPIDR_EL0` + GOT offset. The counts
are large: 62 sites in ut-bin, 484 in Core, 890 in Engine, 37 in SDLDrv and 27 in VulkanDrv.

Core, VulkanDrv and libopenal also use TLSDESC, including against libstdc++'s `std::__once_callable` and
`std::__once_call`.

Initial-exec needs a **constant offset from TPIDR_EL0 that is valid on every thread**. In a bionic process the
static TLS layout is fixed at zygote start with no surplus, so a loader cannot add 64 KB of static TLS. It has
three workable options:

1. **Stack-carve (recommended).** In bionic (Android 10+) a thread's mapping is `[guard][stack][static TLS/TCB][pthread_internal][bionic_tls]`,
   so the stack top sits a small, constant distance below `tp`. Give every engine thread (created through our
   `pthread_create` wrapper, plus the dedicated game thread) a large stack. Its trampoline immediately
   `alloca`s a reserved region (for example 256 KB) at the top of the stack. Then pick a static TLS block at
   `tp - K` (for example `K = 0x30000`) that falls inside that reserved region. Lay out every loaded module's
   TLS (Core 64 KB, VulkanDrv, libstdc++, plus surplus for later `dlopen`s) in that block.
   Resolve TPREL64 to `-K + module_offset`. Resolve TLSDESC to a static resolver returning the same constant
   (this is glibc's `_dl_tlsdesc_return`). Copy TLS init images into each thread's block in the trampoline, and
   into all live threads when a TLS module is loaded later.
   Constraint: engine code must never run on a thread we did not create. In practice that holds: SDL events are
   polled on the game thread, and OpenAL/Vulkan driver threads never call into engine code.
2. **Binary patching.** Rewrite about 1,500 `mrs xN, TPIDR_EL0` sites into branches to stubs. This is automatable
   but fragile.
3. **Fixed shadow mapping at `tp + K`.** Use `MAP_FIXED_NOREPLACE` at a constant offset. This is fragile because of
   VA collisions in a 39-bit address space.

This is the main technical risk of Option B. It can be solved in roughly 200 lines of loader code, and it should
be prototyped first.

### 1.5 C++ runtime: GNU libstdc++ is mandatory

- No exported Core/Engine function takes a `std::` type (0 `basic_string` exports in Core's 2,462 symbols), so
  the engine's own cross-module ABI is plain C++ with UE1 `FString`/`TArray`.
- However, every module has **inlined libstdc++ templates**: `std::__cxx11::basic_string`, `_Prime_rehash_policy`
  (`unordered_map`), `_Rb_tree`, iostreams/`stringstream`, `std::call_once` (`__once_proxy` + TLS), and the
  libstdc++ exception ABI (`__cxa_throw` etc., with `__gxx_personality_v0`). The mangled names (`std::__cxx11::`)
  and object layouts are libstdc++-specific. LLVM libc++ (`std::__ndk1::`) can **not** substitute for them.
- So libstdc++ must be GNU. There are two ways to get it:
  - **(a) Load Ubuntu 22.04's arm64 `libstdc++.so.6.0.30` + `libgcc_s.so.1` through the same custom loader.**
    Their glibc imports go through the same shim. The extra work is the "gnu" locale model: libstdc++ reaches
    into glibc's `__locale_struct` (`__ctype_b`, `__ctype_toupper`, …) and calls `__newlocale`, `__uselocale`,
    `__nl_langinfo_l`, `__strcoll_l`, `__strftime_l`, `__wcsftime_l`, `__towupper_l` and so on. Supporting only
    the "C" locale with a fabricated glibc-layout locale object is enough for UT. Also add `_dl_find_object` and
    `__cxa_thread_atexit_impl`. Expect roughly 150–250 extra imports, mostly pass-through.
  - **(b) Build GCC 12's libstdc++ for `aarch64-linux-android` (bionic).** GCC supports the target, and Termux
    community GCC builds exist. It is then a normal bionic `.so`. Register each custom module's `.eh_frame` with
    `__register_frame_info` so the libgcc unwinder finds its FDEs.
  - Recommendation: start with (a), because it is byte-identical to what the game was built against.
    Switch to (b) if locale shimming gets ugly.
- The engine uses C++ exceptions (`appErrorf` → throw, guard blocks catch), so unwinding through custom-loaded
  modules must work from day one.

### 1.6 SDL usage (92 imports)

- Video: `SDL_CreateWindow(WithProperties)`, fullscreen and display-mode queries, `SDL_GetWindowSurface` /
  `SDL_UpdateWindowSurface` (SoftDrv path), a small `SDL_Renderer` use (splash), `SDL_Vulkan_*`, cursor,
  clipboard, message boxes, and relative mouse.
- **Input: the raw `SDL_Joystick` API, not `SDL_Gamepad`** (`SDL_GetJoysticks`, `OpenJoystick`, `GetJoystickAxis/Button`,
  `GetNumJoystickAxes/Buttons/Hats/Balls`, `UpdateJoysticks`), plus keyboard and mouse events through `SDL_PollEvent`.
- TTF: SDL3_ttf glyph rendering for fonts.
- All of these are stable SDL3 ABI (`SDL3_0.0.0` version node). An NDK-built SDL3 ≥ 3.2 + SDL3_ttf satisfies them.

---

## 2. Option A: separate glibc process plus a custom IPC SDL3

**Concept.** Ship aarch64 glibc (ld-linux, libc, libm), libstdc++ and libgcc_s, all in `nativeLibraryDir` as
`lib*.so` names so they are executable. The Android app runs
`exec(nativeLibDir/libld-linux.so --library-path … ut-bin-arm64)`. A custom glibc build of `libSDL3.so.0`
forwards video, input and audio to the Java/NDK app over a Unix socket and shared memory.

### 2.1 Feasibility issues

- **Exec / W^X.** With targetSdk ≥ 29, apps cannot `execve` files in app-data, but they can exec from
  `nativeLibraryDir` (`extractNativeLibs=true`), and they can `mmap(PROT_EXEC)` app-data files. Running
  `ld-linux` from the native lib dir and letting it map the rest works. Termux and Winlator sidestep this with
  targetSdk 28, which is fine for sideloading.
- **Seccomp.** App processes inherit Android's seccomp filter, which traps (SIGSYS) on unlisted syscalls. Stock
  glibc 2.34+ calls `clone3` in `pthread_create`, registers `rseq` (you can disable it with
  `GLIBC_TUNABLES=glibc.pthread.rseq=0`), and calls `set_robust_list`, which reports say is blocked on Android 15.
  Users report "Bad system call" crashes on Android 10/11 from `clone3`/`rseq`. You therefore need **a patched
  glibc**: Termux-pacman's `glibc-packages` (with grun) is Android-patched but has hardcoded Termux prefixes, or
  your own build. This works but is an ongoing maintenance burden.
- **GPU: the Android system Vulkan driver is unusable from glibc.** `vulkan.adreno.so` is a bionic library that
  depends on libnativewindow, gralloc and libcutils. A glibc process could only load it through **libhybris**
  (bionic-in-glibc), and its WSI needs an `ANativeWindow`. An `ANativeWindow` is a Binder `IGraphicBufferProducer`
  and cannot be handed to a process that does not speak Binder. You can pass `AHardwareBuffer`s across processes
  (`AHardwareBuffer_sendHandleToUnixSocket`), but not a window.
- **Realistic GPU path: Mesa Turnip (KGSL backend) built for glibc.** It talks directly to `/dev/kgsl-3d0`, which
  is app-accessible. Adreno 740 (a7xx) is supported by recent Mesa. Presentation options:
  1. `VK_EXT_headless_surface` (Mesa common WSI) + `vkCmdCopyImageToBuffer` into a memfd that the app process
     shares → the app does `ANativeWindow_lock` + memcpy, or a GLES texture upload. Cost at 1080p: about 8 MB per
     frame, roughly 1–2 ms. This is simple and robust, and UT99 is light enough that this is acceptable.
  2. Zero-copy: the app allocates linear `AHardwareBuffer`s with CPU and GPU usage, sends their dma-buf fds over
     `SCM_RIGHTS`, Turnip imports them (`VK_EXT_external_memory_dma_buf`; KGSL import landed upstream together
     with dma_heap support), and the app presents with `ASurfaceTransaction_setBuffer` (API 29+) and sync_file
     fences. This is exactly what Termux:X11 DRI3 + Turnip does, and community reports show a large speedup over
     the copy path. The cost is a custom Vulkan WSI layer (intercept `vkCreateSwapchainKHR` / `vkQueuePresentKHR`)
     and UBWC/linear-layout care. Community threads show export/DRI3 bugs in KGSL Turnip, so stability is
     uneven.
- **Audio.** glibc OpenAL Soft has no Android backend. Use OpenAL Soft's SDL3 backend → custom SDL3 audio driver →
  shared-memory ring → AAudio in the app. Extra latency is about 5–20 ms.
- **Input.** The app side (Java `InputDevice`/`KeyEvent`/`MotionEvent`, or a bionic SDL3 inside the app) serializes
  joystick, key, touch and mouse events over the socket. The custom glibc SDL3 exposes them as an
  `SDL_VirtualJoystick` plus keyboard and mouse events. Rumble, hot-plug, lifecycle (pause/resume, surface
  destroyed) and IME all need explicit protocol messages.

### 2.2 Assessment

| | |
|---|---|
| Feasibility | Proven pattern (Winlator, Termux+X11+Turnip), but it needs a patched glibc, Turnip instead of the vendor driver, and a custom SDL3 backend with 3 subsystems over IPC. |
| Performance | CPU is native. GPU uses Turnip (good on a7xx, sometimes behind the blob). Presentation costs a copy or needs complex zero-copy. Total overhead is small for UT99 (UT99 is CPU/draw-call light). |
| Controller | Indirect: Android InputDevice → app → socket → virtual SDL joystick. |
| Effort | High: glibc packaging and patching, glibc Turnip build, WSI bridge layer, SDL3 IPC video, audio and input backends, process lifecycle (Android may kill a background child), and two processes to debug. Roughly 8–14 weeks. |
| Risks | Seccomp changes on future Android versions (Android 15 is already stricter), Turnip regressions on specific Adreno firmware, IPC lifecycle bugs, and no access to Android Vulkan blob features or adrenotools. Distribution size grows by about 30 MB. |
| Upside | No ABI shimming at all; the glibc binaries run unmodified with their own libc. |

---

## 3. Option B: in-process loader (recommended)

**Concept.** The APK is a standard SDL3 Android app (`SDLActivity`, NDK-built `libSDL3.so`, `libSDL3_ttf.so`,
`libopenal.so` (OpenAL Soft 1.24 with AAudio/OpenSL backends), `libsndfile`, `libmpg123`, `libxmp`, `libmp3lame`,
all rebuilt with the NDK). `libmain.so`'s `SDL_main` starts the **glibc loader**.

The loader maps `ut-bin-arm64` (PIE) and its glibc DT_NEEDED chain: Core, Engine, SDLDrv, then libstdc++/libgcc_s
if you choose 1.5(a). Load the game binaries from `nativeLibraryDir`, renamed `lib*.so`, or from app storage with
mmap PROT_EXEC.

It resolves each import in this order:
1. the glibc→bionic shim table (versioned name match),
2. other custom-loaded modules,
3. bionic `dlsym(RTLD_DEFAULT)`, which covers SDL3, OpenAL, sndfile, xmp and mpg123 because their C APIs match.
   SDL symbol version `SDL3_0.0.0` matches when the version is ignored.

It then applies relocations, sets up TLS (1.4), runs `DT_INIT`/`INIT_ARRAY`, and jumps to `_start` on a dedicated
game thread with `argv = {"ut-bin", "-ini=…", …}`. The shim's `dlopen("VulkanDrv.so")` re-enters the loader.

### 3.1 Feasibility details

- The ELF features used are minimal. There are no IFUNC, COPY, TEXTREL or BTI; only 6 relocation types and
  standard `DT_GNU_HASH`/versym. A loader of this kind is about 1–2 k lines. References include min-dl,
  Vita/Switch `so_loader`s (the opposite direction, same technique), libhybris's linker, mcpelauncher-linker,
  and droidports.
- Shim surface: 242 game glibc symbols (about 30 real wrappers, 1.3) plus libstdc++'s imports if loaded
  (locale shims). Box64's `wrappedlibc` (which has an Android/bionic host build) and libhybris `hooks.c` are
  catalogs of exactly these struct differences.
- TLS: solvable with the stack-carve scheme (1.4). This is the most novel piece, so prototype it in week 1.
- Exceptions: `dl_iterate_phdr` / `_dl_find_object` shims that know the custom modules. If you choose libstdc++
  option (b), use `__register_frame_info`.
- Signals: route `sigaction` through bionic (which goes through ART's libsigchain). Never let the engine replace
  SIGSEGV/SIGBUS, because ART uses them for implicit null checks and stack overflow detection.
- Linker namespaces and the API 24+ private-library restrictions do not apply to libraries we map ourselves.
  The only system library we `dlopen` is the public `libvulkan.so`.

### 3.2 Graphics

VulkanDrv → `SDL_Vulkan_LoadLibrary` + `SDL_Vulkan_CreateSurface` (Android SDL uses `VK_KHR_android_surface` on the
`SurfaceView`) → **Qualcomm's system Vulkan driver, directly and with zero copy**. If the blob misbehaves,
for example over BCn support or a driver bug, you can swap in Turnip **for Android** via libadrenotools, which
works only in-process. XOpenGLDrv / OpenGLDrv would need GLES and are not a goal.

### 3.3 Controller input

Android `InputDevice`/`MotionEvent` → SDL3's Android joystick backend (`SDLControllerManager` + HIDAPI) →
`SDL_Joystick` → SDLDrv's JoyX/JoyY/Joy1..N bindings. The Retroid's built-in pad appears as a standard Android
gamepad InputDevice.

Because SDL is in-process and resolved through our table, you can **interpose** the `SDL_*Joystick*` calls. For
example, back them with `SDL_Gamepad` so button and axis indices follow a stable Xbox-style layout, and ship a
matching `User.ini` binding set. Add an on-screen touch overlay or touch-as-mouse in Java/SDL without touching
the engine. Rumble, hot-plug and lifecycle events come from stock SDL3 Android.

### 3.4 Assessment

| | |
|---|---|
| Feasibility | High. Every import has been enumerated, and none is unshimmable. Two engineering puzzles remain: IE TLS (1.4) and libstdc++ locale internals (1.5a). |
| Performance | Fully native: one process, no IPC, the vendor Vulkan driver, AAudio-backed OpenAL Soft, and zero-copy presentation by SDL. It is the best possible. |
| Controller | Stock SDL3 Android joystick/gamepad, which can be interposed for remapping. Touch overlay is easy. |
| Effort | Medium. Loader about 2 weeks, shim plus libstdc++ about 2–3 weeks, Android app/SDL/OpenAL/codecs about 1 week, then debugging and polish. Roughly 6–9 weeks for one experienced engineer. |
| Risks | (1) TLS scheme edge cases, for example an engine thread created by a bionic library that bypasses our wrapper. (2) Hidden struct-ABI mismatches that surface as rare crashes, so audit each wrapper and run under a debug build with guard checks. (3) Future OldUnreal patches may add new imports; re-run the nm diff (scripted) on every update. (4) The libstdc++ "gnu" locale model if not built for bionic. (5) Debuggability: lldb won't know the custom modules unless you also publish them via `r_debug`/a GDB JIT interface. Add a module-map log and use `addr2line` on offsets. |

---

## 4. Option C: existing projects

| Project | What it is | Relevance |
|---|---|---|
| **Termux + termux-pacman `glibc-packages` (grun) + Termux:X11 + Turnip (KGSL)** | Android-patched glibc userland running glibc binaries natively. X11 server app with DRI3 via AHardwareBuffer, and Mesa Turnip/Zink. | **Fastest smoke test**: run `ut-bin-arm64` today with SDL3's x11 backend and VulkanDrv on Turnip, with no porting. It validates game logic, Vulkan renderer behavior on Adreno, and the content. It is not a shippable product. Community notes: DRI3 is a large win over the copy path; KGSL dma-buf export still has rough edges. |
| **Winlator / Mobox / Box64Droid** | targetSdk-28 apps that exec a glibc rootfs, run Box64 (x86-64 → arm64) and Turnip, with an in-app X server. | Productized Option A. Box64's wrapped-lib tables (glibc API ↔ host, including a bionic host build) are a reference for the B shim. Too heavy to reuse directly. |
| **Pocket Foundry (Factorio)** | Third-party APK listings describe it as running Factorio's Linux arm64 build "via Pocket Foundry" with touch controls. | **Unverified**: no source code or architecture write-up found, only APK-mirror descriptions. It is likely a Termux-style glibc runtime or a loader, but this could not be confirmed. Hackaday's "Factorio on mobile" was postmarketOS + Box64, not Android. |
| **"glibc-bridge"** | No project by this name was found. | – |
| **libhybris** | Loads *bionic* libraries into a *glibc* process (Sailfish, Ubuntu Touch). | The reverse of B. Its `hooks.c` is the best existing glibc↔bionic struct-translation table (pthread, stat, FILE, …). |
| **minecraft-linux `libc-shim` + `mcpelauncher-linker`** | Loads Android (bionic) `.so` into glibc Linux. | The reverse of B; a reference for shim structure. |
| **Vita/Switch `so_loader`s, droidports** | Load Android `.so` into non-Android OSes with import wrappers. | The same loader and import-table technique as B. |
| **patchelf / proot-less glibc** | Rewrite PT_INTERP/RPATH to a bundled ld-linux. | Part of Option A's packaging. Android 15 seccomp blocks `set_robust_list` and Android 10/11 block `clone3`/`rseq` per reports, so a patched glibc is still needed. |

---

## 5. Comparison summary

| | A: glibc process + IPC SDL3 | B: in-process loader + real Android SDL3 | C: Termux stack |
|---|---|---|---|
| Native-ness | Native CPU; GPU via Turnip; IPC for I/O | **Fully native**, vendor Vulkan, one process | Native CPU, X11 indirection |
| Frame path | Copy (1–2 ms) or complex zero-copy | Zero-copy (SDL/ANativeWindow) | DRI3/AHB |
| Input | Serialized → virtual joystick | **Stock SDL3 Android** (interposable) | X11 + Termux:X11 gamepad hacks |
| Audio | SDL3 audio over shm → AAudio | OpenAL Soft (NDK) → AAudio | PulseAudio over TCP |
| Effort | High (8–14 wk) | **Medium (6–9 wk)** | ~1 day (test only) |
| Main risk | Seccomp/glibc patching, Turnip WSI, IPC lifecycle | IE TLS across modules, struct-ABI mistakes | Not shippable |
| Shippable APK | Yes | **Yes** | No |

## 6. Recommended plan (Option B)

1. **Day 1 smoke test (Option C):** Termux + glibc-runner + Termux:X11 + Turnip; run `ut-bin-arm64` with
   VulkanDrv to confirm the game runs on this SoC.
2. **Week 1 spike:** a minimal loader that maps Core.so + libstdc++/libgcc_s (Ubuntu 22.04 arm64), with a shim
   for those imports. Prove the **TLS stack-carve** scheme with `UnGuardBlockTLS::Data` and a
   **throw/catch across Core** before anything else.
3. Bring up `ut-bin` via the `__libc_start_main` shim on a dedicated 8 MB-stack game thread. Use NullRender /
   SoftDrv over `SDL_GetWindowSurface` first, then VulkanDrv on the system driver.
4. Build OpenAL Soft 1.24.x, libsndfile, mpg123, libxmp and lame with the NDK, and resolve ALAudio to them.
5. Input: an `SDL_Joystick`→`SDL_Gamepad` interposer, a default `User.ini` mapping for the Retroid pad, and a
   touch overlay.
6. Keep the import analysis scripted (`llvm-nm -D --undefined-only` diff against the shim table) so a future
   OldUnreal patch fails loudly at load time with a list of missing symbols, rather than crashing.

## Sources

- Bionic status / glibc differences: https://android.googlesource.com/platform/bionic/+/master/docs/status.md
- Android linker changes for NDK developers: https://android.googlesource.com/platform//bionic/+show/HEAD/android-changes-for-ndk-developers.md
- glibc vs musl/bionic mutex sizes (aarch64 48 vs 40 bytes): https://openwall.com/lists/musl/2023/08/23/3
- Running glibc binaries on Android 15 without proot (seccomp `set_robust_list`): https://dev.to/opassoca/running-native-glibc-debian-binaries-on-android-15-without-proot-1hmo
- Seccomp blocking clone3/rseq on Android (opencode-termux notes): https://github.com/HanSoBored/opencode-termux
- termux-pacman glibc-packages / grun: https://github.com/termux-pacman/glibc-packages/wiki/About-glibc-runner-%28grun%29
- Turnip KGSL external memory / dma_heap: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/14928
- Turnip AHardwareBuffer issue: https://gitlab.freedesktop.org/mesa/mesa/-/issues/9691
- Turnip X11 on KGSL, copy-based WSI: https://gitlab.freedesktop.org/mesa/mesa/-/issues/9311
- Zink/kopper on Termux:X11: https://gitlab.freedesktop.org/mesa/mesa/-/issues/9903 , https://gitlab.freedesktop.org/mesa/mesa/-/issues/8460
- DRI3 vs non-DRI3 Turnip benchmarks (community): https://red.bismuth.it/r/termux/comments/16dfdsw/proot_only_dri3_patch_freedreno_turnip_driver
- Termux KGSL dma-buf export discussion: https://github.imc.re/termux/termux-packages/discussions/22451
- Mesa on Android (Collabora): https://www.collabora.com/news-and-blog/news-and-events/mesa-on-android-how-we-brought-turnip,-skiavk,-and-cuttlefish-to-ci.html
- Pocket Foundry listing (unverified): https://apkvision.org/games/simulation/factorio-135421/
- Factorio on a phone (postmarketOS, not Android): https://hackaday.com/2025/10/15/factorio-running-on-mobile
- minecraft-linux libc-shim (reverse-direction shim): https://minecraft-linux.github.io/print.html
- droidports: https://github.com/johnnyonflame/droidports ; min-dl: https://github.com/jserv/min-dl
