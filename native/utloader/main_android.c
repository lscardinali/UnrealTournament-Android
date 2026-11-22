/*
 * libutloader.so: the native (in-process) entry point. SDLActivity calls SDL_main on its SDL thread with
 *   argv[1] = the app's files dir (layout of runtime/GlibcProgram.kt: glibc/, game/System/, home/, logs/)
 *   argv[2..] = ut-bin arguments
 * We run UT's own ut-bin-arm64 _start on a utl thread (TLS block carved), with Android SDL3 standing in for
 * the game's glibc libSDL3.so.0, and Qualcomm's libvulkan.so for libvulkan.so.1.
 */
#define _GNU_SOURCE
#include "utl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <android/log.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>

#include <fcntl.h>
#include <stdarg.h>
#include <jni.h>
#include <sys/mman.h>

int utl_run_exe(utl_module *exe, int argc, char **argv);
extern volatile uint32_t *utl_frames_shared, *utl_frames_file;

/* Shared with SDL_utandroid.c (native-pad mode); the layout must match UTANDROID_Shared there. */
static struct {
    volatile uint32_t frames;
    volatile int32_t menu;
    volatile float cursor_x, cursor_y;
    volatile int32_t window_w, window_h;
    volatile uint32_t inject_buttons; /* debug: pad buttons injected by the app (adb input gamepad), bit i = button i */
    volatile uint32_t inject_pressed; /* presses not yet taken by the SDL bridge (a tap shorter than a poll) */
    volatile int32_t touch_dx, touch_dy; /* touchpad cursor: movement not yet applied, in window pixels */
    volatile uint32_t touch_taps;        /* touchpad taps (left clicks) not yet applied */
    volatile int32_t gyro_yaw, gyro_pitch; /* gyro angle turned and not yet applied, micro-radians */
    volatile int32_t perf_overlay;       /* handheld settings (files/handheld.cfg): performance overlay shown */
    volatile float stick_curve;          /* look stick response exponent (1 = linear) */
    volatile float turn_boost;           /* extra turn speed at full yaw deflection (0 = off, 1 = up to 2x) */
    volatile int32_t gyro_on;            /* gyro aiming enabled (the app stops the sensor otherwise) */
    volatile int32_t fps_limit;          /* display refresh to ask for: 0 = the panel's maximum, else e.g. 60 / 90 */
} shared_state = { .stick_curve = 1.5f, .turn_boost = 1.0f, .gyro_on = 1 };

/*
 * Handheld settings: set from PadUI's Handheld Settings window through the clipboard (shim.c intercepts
 * "utandroid:" texts) and from the app (L3+R3), kept in files/handheld.cfg.
 *   "get"                         -> "perf=1 curve=1.50 boost=1.00"
 *   "set perf=0|1|toggle curve=F boost=F"
 */
static char handheld_path[512];

static void handheld_apply(const char *args) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", args);
    for (char *save = NULL, *tok = strtok_r(buf, " \n", &save); tok; tok = strtok_r(NULL, " \n", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(tok, "perf")) {
            shared_state.perf_overlay = !strcmp(v, "toggle") ? !shared_state.perf_overlay : atoi(v) != 0;
        } else if (!strcmp(tok, "curve")) {
            float f = strtof(v, NULL);
            if (f >= 1.0f && f <= 3.0f) shared_state.stick_curve = f;
        } else if (!strcmp(tok, "boost")) {
            float f = strtof(v, NULL);
            if (f >= 0.0f && f <= 2.0f) shared_state.turn_boost = f;
        } else if (!strcmp(tok, "gyro")) {
            shared_state.gyro_on = atoi(v) != 0;
        } else if (!strcmp(tok, "fps")) {
            int n = atoi(v);
            if (n == 0 || (n >= 30 && n <= 240)) shared_state.fps_limit = n;
        }
    }
}

static void handheld_save(void) {
    FILE *f = fopen(handheld_path, "w");
    if (!f) return;
    fprintf(f, "perf=%d curve=%.2f boost=%.2f gyro=%d fps=%d\n", (int)shared_state.perf_overlay,
            shared_state.stick_curve, shared_state.turn_boost, (int)shared_state.gyro_on, (int)shared_state.fps_limit);
    fclose(f);
}

static void handheld_load(const char *files) {
    snprintf(handheld_path, sizeof handheld_path, "%s/handheld.cfg", files);
    FILE *f = fopen(handheld_path, "r");
    if (!f) return;
    char line[256];
    if (fgets(line, sizeof line, f)) handheld_apply(line);
    fclose(f);
}

/* Returns the reply for a "utandroid:" command (static buffer). */
const char *utl_handheld_command(const char *cmd) {
    static char reply[128];
    if (!strncmp(cmd, "set ", 4)) {
        handheld_apply(cmd + 4);
        handheld_save();
    }
    snprintf(reply, sizeof reply, "perf=%d curve=%.2f boost=%.2f gyro=%d fps=%d", (int)shared_state.perf_overlay,
             shared_state.stick_curve, shared_state.turn_boost, (int)shared_state.gyro_on, (int)shared_state.fps_limit);
    return reply;
}

/* App side (L3+R3 overlay toggle). */
JNIEXPORT void JNICALL Java_com_lscardinali_unrealtournament_NativeGameActivity_nativeHandheld(JNIEnv *env, jclass c,
                                                                                               jstring cmd) {
    const char *s = (*env)->GetStringUTFChars(env, cmd, NULL);
    if (s) {
        utl_handheld_command(s);
        (*env)->ReleaseStringUTFChars(env, cmd, s);
    }
}

/* Gyro aiming (GyroAim.kt): angle turned since the last call, micro-radians (yaw right +, pitch up +). */
JNIEXPORT void JNICALL Java_com_lscardinali_unrealtournament_NativeGameActivity_nativeGyro(JNIEnv *env, jclass c,
                                                                                           jint yaw, jint pitch) {
    if (yaw) __atomic_add_fetch(&shared_state.gyro_yaw, yaw, __ATOMIC_SEQ_CST);
    if (pitch) __atomic_add_fetch(&shared_state.gyro_pitch, pitch, __ATOMIC_SEQ_CST);
}

/* The touchscreen as a touchpad for the menu cursor (NativeGameActivity): relative moves and tap clicks. */
JNIEXPORT void JNICALL Java_com_lscardinali_unrealtournament_NativeGameActivity_nativeTouch(JNIEnv *env, jclass c,
                                                                                            jint dx, jint dy,
                                                                                            jint taps) {
    if (dx) __atomic_add_fetch(&shared_state.touch_dx, dx, __ATOMIC_SEQ_CST);
    if (dy) __atomic_add_fetch(&shared_state.touch_dy, dy, __ATOMIC_SEQ_CST);
    if (taps) __atomic_add_fetch(&shared_state.touch_taps, (uint32_t)taps, __ATOMIC_SEQ_CST);
}

/* Debug builds: NativeGameActivity forwards pad presses from virtual input devices (adb shell input gamepad),
   which SDL doesn't see as a gamepad, so the native build can be driven over adb like a real pad. */
JNIEXPORT void JNICALL Java_com_lscardinali_unrealtournament_NativeGameActivity_nativeInjectButton(JNIEnv *env, jclass c,
                                                                                                   jint button,
                                                                                                   jboolean down) {
    const uint32_t bit = 1u << (button & 31);
    if (down) {
        __atomic_or_fetch(&shared_state.inject_buttons, bit, __ATOMIC_SEQ_CST);
        __atomic_or_fetch(&shared_state.inject_pressed, bit, __ATOMIC_SEQ_CST);
    } else {
        __atomic_and_fetch(&shared_state.inject_buttons, ~bit, __ATOMIC_SEQ_CST);
    }
}

/* NativeGameActivity reads the menu cursor (drawn as an Android overlay: no pointer on Android). */
JNIEXPORT void JNICALL Java_com_lscardinali_unrealtournament_NativeGameActivity_nativeCursorState(JNIEnv *env, jclass c,
                                                                                                   jfloatArray out) {
    jfloat v[8] = { (jfloat)shared_state.menu, shared_state.cursor_x, shared_state.cursor_y,
                    (jfloat)shared_state.window_w, (jfloat)shared_state.window_h, (jfloat)shared_state.perf_overlay,
                    (jfloat)shared_state.fps_limit, (jfloat)shared_state.gyro_on };
    (*env)->SetFloatArrayRegion(env, out, 0, 8, v);
}

/* The perf overlay's frames file ("UTF1", uint32 count at +4), created by the activity. */
static void map_frames_file(const char *files) {
    char path[512];
    snprintf(path, sizeof path, "%s/tmp/ut-frames", files);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return;
    uint8_t *p = mmap(NULL, 32, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p != MAP_FAILED) utl_frames_file = (volatile uint32_t *)(p + 4);
}

/* Crash logger: this firmware writes no tombstones to logcat, so log fault, pc/lr as module+offset (our glibc
   modules or bionic libraries) and a frame-pointer backtrace, then let the default action happen. Installed
   through the process's sigaction (libsigchain in an app): ART still sees its own faults first. */
static void describe(char *out, size_t n, uintptr_t a) {
    utl_module *m = utl_module_for_addr(a);
    Dl_info di;
    if (m) snprintf(out, n, "%#lx %s+%#lx", (unsigned long)a, m->name, (unsigned long)(a - m->bias));
    else if (a && dladdr((void *)a, &di) && di.dli_fname)
        snprintf(out, n, "%#lx %s+%#lx (%s)", (unsigned long)a, strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname,
                 (unsigned long)(a - (uintptr_t)di.dli_fbase), di.dli_sname ? di.dli_sname : "?");
    else snprintf(out, n, "%#lx ?", (unsigned long)a);
}

static int crash_fd = -1;

/* logcat + files/logs/crash.txt (readable without adb; the app can offer it later). */
static void crash_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void crash_line(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    __android_log_write(ANDROID_LOG_FATAL, "utloader", buf);
    if (crash_fd >= 0) {
        buf[n] = '\n';
        write(crash_fd, buf, (size_t)n + 1);
    }
}

static void crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx;
    char pc[256], lr[256];
    crash_line("CRASH raw: sig %d addr %p tid %ld pc %#llx lr %#llx sp %#llx",
                        sig, si->si_addr, (long)syscall(SYS_gettid), (unsigned long long)uc->uc_mcontext.pc,
                        (unsigned long long)uc->uc_mcontext.regs[30], (unsigned long long)uc->uc_mcontext.sp);
    describe(pc, sizeof pc, uc->uc_mcontext.pc);
    describe(lr, sizeof lr, uc->uc_mcontext.regs[30]);
    crash_line("CRASH signal %d code %d addr %p tid %ld", sig, si->si_code,
                        si->si_addr, (long)syscall(SYS_gettid));
    crash_line("  pc %s", pc);
    crash_line("  lr %s", lr);
    uintptr_t *fp = (uintptr_t *)uc->uc_mcontext.regs[29];
    for (int i = 0; i < 16 && fp && ((uintptr_t)fp & 7) == 0; i++) {
        char f[256];
        describe(f, sizeof f, fp[1]);
        crash_line("  #%02d %s", i, f);
        uintptr_t *next = (uintptr_t *)fp[0];
        if (next <= fp) break;
        fp = next;
    }
    /* UT's log goes through (bionic) stdio: flush it so its last lines survive. Not async-signal-safe, but we
       are dying anyway. */
    static volatile int flushing;
    if (!flushing) {
        flushing = 1;
        fflush(NULL);
    }
    signal(sig, SIG_DFL);
}

static void install_crash_logger(void) {
    int (*sys_sigaction)(int, const struct sigaction *, struct sigaction *) =
        (int (*)(int, const struct sigaction *, struct sigaction *))dlsym(RTLD_DEFAULT, "sigaction");
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    int r1 = sys_sigaction(SIGSEGV, &sa, NULL);
    int r2 = sys_sigaction(SIGBUS, &sa, NULL);
    sys_sigaction(SIGABRT, &sa, NULL);
    sys_sigaction(SIGILL, &sa, NULL);
    sys_sigaction(SIGFPE, &sa, NULL);
    sys_sigaction(SIGTRAP, &sa, NULL);
    Dl_info di;
    dladdr((void *)sys_sigaction, &di);
    utl_log("crash logger installed via %s (%d, %d)", di.dli_fname ? di.dli_fname : "?", r1, r2);
}

typedef struct {
    int argc;
    char **argv;
    int status;
} game_args;

/* Which game: UT by default; UTL_EXE / UTL_SYSTEM_DIR / UTL_HOME (set by the activity) run another one, e.g.
   Unreal 227's SystemARM64/unreal-bin-arm64 (docs/unreal-227-spike.md). */
static const char *game_exe = "ut-bin-arm64";

static void *game_thread(void *p) {
    game_args *g = p;
    utl_module *gcc = utl_load("libgcc_s.so.1");
    utl_module *cxx = utl_load("libstdc++.so.6");
    utl_module *exe = utl_load(game_exe);
    if (!gcc || !cxx || !exe) {
        utl_log("could not load the game (libgcc_s %p, libstdc++ %p, ut-bin %p)", gcc, cxx, exe);
        g->status = 127;
        return NULL;
    }
    for (utl_module *m = utl_modules(); m; m = m->next) utl_run_inits(m);
    g->status = utl_run_exe(exe, g->argc, g->argv);
    utl_log("ut-bin finished with %d", g->status);
    return NULL;
}

__attribute__((visibility("default"))) int SDL_main(int argc, char *argv[]) {
    if (argc < 2) {
        utl_log("SDL_main: missing files dir argument");
        return 2;
    }
    const char *files = argv[1];
    char path[512], home[512], system_dir[512], glibc_dir[512];

    snprintf(path, sizeof path, "%s/logs", files);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/logs/native-ut.log", files);
    freopen(path, "w", stdout);
    freopen(path, "a", stderr);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    snprintf(home, sizeof home, "%s/home", files);
    snprintf(system_dir, sizeof system_dir, "%s/game/System", files);
    if (getenv("UTL_HOME")) snprintf(home, sizeof home, "%s", getenv("UTL_HOME"));
    if (getenv("UTL_SYSTEM_DIR")) snprintf(system_dir, sizeof system_dir, "%s", getenv("UTL_SYSTEM_DIR"));
    if (getenv("UTL_EXE")) game_exe = getenv("UTL_EXE");
    snprintf(glibc_dir, sizeof glibc_dir, "%s/glibc", files);
    mkdir(home, 0700);
    setenv("HOME", home, 1);
    setenv("TMPDIR", files, 1);
    chdir(system_dir);

    snprintf(path, sizeof path, "%s/logs/crash.txt", files);
    crash_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    install_crash_logger();
    /* Controller: SDL's virtual pad reads the real pad itself (no socket); shared state for frames + cursor. */
    char shared_addr[32];
    snprintf(shared_addr, sizeof shared_addr, "%p", (void *)&shared_state);
    setenv("UTANDROID_NATIVE_PAD", "1", 1);
    setenv("UTANDROID_SHARED", shared_addr, 1);
    utl_frames_shared = &shared_state.frames;
    map_frames_file(files);
    handheld_load(files);
    if (utl_tls_init() != 0) return 1;
    utl_add_search_dir(system_dir);
    utl_add_search_dir(glibc_dir);

    char *game_argv[32] = { (char *)game_exe };
    int n = 1;
    for (int i = 2; i < argc && n < 31; i++) game_argv[n++] = argv[i];
    game_argv[n] = NULL;
    game_args g = { n, game_argv, 0 };

    utl_log("native UT: files=%s, %d arg(s)", files, n - 1);
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024); /* the game's main() runs on this thread's own stack */
    utl_thread_create(&t, &attr, game_thread, &g);
    pthread_attr_destroy(&attr);
    pthread_join(t, NULL);
    utl_log("game exited (%d): ending the process", g.status);
    fflush(NULL);
    /* The loaded game modules keep global state (and our TLS block, signal setup, SDL hooks): a second SDL_main
       in this process would run UT's main() over a used engine. End the process so the next launch is fresh. */
    _exit(g.status);
}
