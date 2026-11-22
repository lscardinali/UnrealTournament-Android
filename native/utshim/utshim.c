/*
 * utshim — tiny glibc LD_PRELOAD shim for running UT469 Linux arm64 on Android.
 *
 * 1. /proc/self/exe: on Android the glibc loader (ld-linux-aarch64.so.1) must be exec'd from the APK's
 *    nativeLibraryDir (W^X), so /proc/self/exe points at the loader, not at System/ut-bin-arm64.
 *    Core.so derives the base directory from readlink("/proc/self/exe"), so we redirect that to $UT_EXE_PATH.
 *
 * 2. Frame counter for the app's performance overlay: Mesa's software X11 present path (MESA_VK_WSI_DEBUG=sw)
 *    sends each frame with xcb_put_image, in row chunks starting at dst_y == 0. We count those first chunks into
 *    a shared file ($UTANDROID_FRAMES): "UTF1" then a little-endian uint32 frame count that wraps.
 *    xcb_present_pixmap (a future zero-copy path) counts one per call.
 *
 * Built freestanding (no libc headers) with the NDK clang:
 *   clang --target=aarch64-unknown-linux-gnu -shared -fPIC -nostdlib -O2 -fuse-ld=lld \
 *         -Wl,-soname,libutshim.so -o libutshim.so utshim.c
 */
typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

#define RTLD_NEXT ((void *)-1l)
#define O_RDWR 02
#define O_CREAT 0100
#define O_CLOEXEC 02000000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_FAILED ((void *)-1)

extern void *dlsym(void *handle, const char *symbol);
extern void *dlopen(const char *file, int mode);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
extern char *getenv(const char *name);
extern int open(const char *path, int flags, ...);
extern int ftruncate(int fd, off_t length);
extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
extern int close(int fd);
struct timespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int clk, struct timespec *ts);
#define CLOCK_MONOTONIC 1

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ul + (uint64_t)ts.tv_nsec;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ------------------------------------------------------------------------------------------ readlink */

typedef ssize_t (*readlink_fn)(const char *, char *, size_t);

__attribute__((visibility("default")))
ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    static readlink_fn real_readlink;
    if (path && str_eq(path, "/proc/self/exe")) {
        const char *exe = getenv("UT_EXE_PATH");
        if (exe && *exe) {
            size_t n = str_len(exe);
            if (n > bufsiz) n = bufsiz; /* readlink semantics: truncate, no NUL */
            for (size_t i = 0; i < n; i++) buf[i] = exe[i];
            return (ssize_t)n;
        }
    }
    if (!real_readlink) real_readlink = (readlink_fn)dlsym(RTLD_NEXT, "readlink");
    return real_readlink(path, buf, bufsiz);
}

/* ------------------------------------------------------------------------------------------ frame counter */

static volatile uint32_t *frame_counter; /* points into the shared file, after the magic */
static int frame_counter_state;          /* 0 = not tried, 1 = mapped, -1 = unavailable */

static uint32_t frames_presented; /* also kept here so utshim_frame_count works without the shared file */

/* Presented frames so far: our SDL reads it (dlsym) to scale the look axes by real frame time. */
__attribute__((visibility("default")))
uint32_t utshim_frame_count(void) {
    return __atomic_load_n(&frames_presented, __ATOMIC_RELAXED);
}

/*
 * Frame timing for diagnostics, over the window since the last utshim_stats() call:
 * frames, sum and max of frame-to-frame intervals, and time spent inside xcb_put_image (the copy into X).
 */
static uint64_t st_frames, st_interval_sum, st_interval_max, st_put_ns, st_last_frame_ns;

__attribute__((visibility("default")))
void utshim_stats(uint64_t out[4]) {
    out[0] = st_frames;
    out[1] = st_frames ? st_interval_sum / st_frames : 0; /* avg frame interval, ns */
    out[2] = st_interval_max;                             /* worst frame interval, ns */
    out[3] = st_frames ? st_put_ns / st_frames : 0;       /* avg xcb_put_image time per frame, ns */
    st_frames = st_interval_sum = st_interval_max = st_put_ns = 0;
}

/*
 * Shared file layout ($UTANDROID_FRAMES, 32 bytes, little-endian):
 *   0  "UTF1"
 *   4  uint32 frames presented (we write; the app's overlay reads)
 *   8  uint64 time of the latest display vsync, CLOCK_MONOTONIC ns (the app writes, from Choreographer)
 *   16 uint64 display refresh period, ns (the app writes)
 */
#define FRAMES_FILE_SIZE 32
static volatile uint64_t *vsync_time, *vsync_period;

static void map_frames_file(void) {
    const char *path = getenv("UTANDROID_FRAMES");
    int fd = path && *path ? open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600) : -1;
    frame_counter_state = -1;
    if (fd < 0) return;
    if (ftruncate(fd, FRAMES_FILE_SIZE) == 0) { /* the app creates it at this size: no change then */
        uint8_t *p = (uint8_t *)mmap(0, FRAMES_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) {
            p[0] = 'U'; p[1] = 'T'; p[2] = 'F'; p[3] = '1';
            frame_counter = (volatile uint32_t *)(p + 4);
            vsync_time = (volatile uint64_t *)(p + 8);
            vsync_period = (volatile uint64_t *)(p + 16);
            frame_counter_state = 1;
        }
    }
    close(fd);
}

/* ------------------------------------------------------------------------------------------ frame pacing */

/*
 * Termux:X11 shows a frame as soon as it arrives (eglSwapInterval 0) and the compositor takes the newest one at
 * each vsync, so frames sent at uneven times are shown unevenly (judder). We hold each frame until LEAD before a
 * vsync slot, so it reaches the X server, gets drawn by lorie and is latched by the next refresh, one frame per
 * slot. Target rate: $UTANDROID_PACE_FPS (0 = off; e.g. 120 = every vsync of a 120 Hz panel, 60 = every other).
 * Lead: $UTANDROID_PACE_LEAD_US (default 4000: xcb_put_image ~2.2 ms + lorie draw).
 * A frame that is already late is sent at once and the cadence re-syncs from there.
 */
extern int clock_nanosleep(int clk, int flags, const struct timespec *req, struct timespec *rem);
#define TIMER_ABSTIME 1

static int pace_fps = -1;        /* -1 = not read yet */
static uint64_t pace_lead_ns;
static uint64_t pace_last_slot;  /* vsync time the previous frame was aimed at */

static uint64_t env_u64(const char *name, uint64_t fallback) {
    const char *s = getenv(name);
    uint64_t v = 0;
    if (!s || !*s) return fallback;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (uint64_t)(*s - '0');
    return v;
}

static void pace_frame(void) {
    uint64_t period, vsync, interval, now, earliest, slot, k;
    if (pace_fps < 0) {
        pace_fps = (int)env_u64("UTANDROID_PACE_FPS", 0);
        pace_lead_ns = env_u64("UTANDROID_PACE_LEAD_US", 4000) * 1000ul;
    }
    if (pace_fps <= 0) return;

    period = vsync_period ? *vsync_period : 0;
    vsync = vsync_time ? *vsync_time : 0;
    if (!period) period = 1000000000ul / (uint64_t)pace_fps; /* no display timing yet: a steady clock */
    k = (1000000000ul + period * (uint64_t)pace_fps / 2) / (period * (uint64_t)pace_fps); /* vsyncs per frame */
    interval = (k ? k : 1) * period;

    now = now_ns();
    if (!vsync) vsync = pace_last_slot ? pace_last_slot : now;
    /* Earliest slot: one interval after the last one, and not before we could make it (now + lead). */
    earliest = pace_last_slot ? pace_last_slot + interval - period / 2 : 0;
    if (earliest < now + pace_lead_ns) earliest = now + pace_lead_ns;
    /* Snap onto the vsync grid. */
    if (earliest > vsync) slot = vsync + ((earliest - vsync + period - 1) / period) * period;
    else slot = vsync - ((vsync - earliest) / period) * period;
    pace_last_slot = slot;

    if (slot > now + pace_lead_ns && slot - now < 4 * interval) {
        const uint64_t wake = slot - pace_lead_ns;
        struct timespec ts = { (long)(wake / 1000000000ul), (long)(wake % 1000000000ul) };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, 0);
    }
}

static void count_frame(void) {
    uint64_t t;
    if (frame_counter_state == 0) map_frames_file();
    pace_frame();
    t = now_ns();
    if (st_last_frame_ns) {
        const uint64_t d = t - st_last_frame_ns;
        st_interval_sum += d;
        if (d > st_interval_max) st_interval_max = d;
        st_frames++;
    }
    st_last_frame_ns = t;
    __atomic_fetch_add(&frames_presented, 1, __ATOMIC_RELAXED);
    if (frame_counter_state == 1) __atomic_fetch_add(frame_counter, 1, __ATOMIC_RELAXED);
}

/*
 * The real xcb function. RTLD_NEXT only searches the global scope, but libxcb usually arrives in a *local* scope
 * (the Vulkan loader dlopens Turnip, which pulls in libxcb), so fall back to the already-loaded library itself.
 */
static void *real_xcb(const char *lib, const char *name) {
    void *f = dlsym(RTLD_NEXT, name);
    if (!f) {
        void *h = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
        if (h) f = dlsym(h, name);
    }
    return f;
}

typedef struct { unsigned int sequence; } xcb_void_cookie_t;
typedef struct xcb_connection_t xcb_connection_t;

typedef xcb_void_cookie_t (*put_image_fn)(xcb_connection_t *, uint8_t, uint32_t, uint32_t, uint16_t, uint16_t,
                                          int16_t, int16_t, uint8_t, uint8_t, uint32_t, const uint8_t *);

__attribute__((visibility("default")))
xcb_void_cookie_t xcb_put_image(xcb_connection_t *c, uint8_t format, uint32_t drawable, uint32_t gc, uint16_t width,
                                uint16_t height, int16_t dst_x, int16_t dst_y, uint8_t left_pad, uint8_t depth,
                                uint32_t data_len, const uint8_t *data) {
    static put_image_fn real;
    if (!real) real = (put_image_fn)real_xcb("libxcb.so.1", "xcb_put_image");
    if (dst_y == 0) count_frame();
    const uint64_t t0 = now_ns();
    xcb_void_cookie_t r = real(c, format, drawable, gc, width, height, dst_x, dst_y, left_pad, depth, data_len, data);
    st_put_ns += now_ns() - t0;
    return r;
}

typedef xcb_void_cookie_t (*present_pixmap_fn)(xcb_connection_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                               int16_t, int16_t, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t,
                                               uint64_t, uint64_t, uint32_t, const void *);

__attribute__((visibility("default")))
xcb_void_cookie_t xcb_present_pixmap(xcb_connection_t *c, uint32_t window, uint32_t pixmap, uint32_t serial,
                                     uint32_t valid, uint32_t update, int16_t x_off, int16_t y_off,
                                     uint32_t target_crtc, uint32_t wait_fence, uint32_t idle_fence, uint32_t options,
                                     uint64_t target_msc, uint64_t divisor, uint64_t remainder, uint32_t notifies_len,
                                     const void *notifies) {
    static present_pixmap_fn real;
    if (!real) real = (present_pixmap_fn)real_xcb("libxcb-present.so.0", "xcb_present_pixmap");
    count_frame();
    return real(c, window, pixmap, serial, valid, update, x_off, y_off, target_crtc, wait_fence, idle_fence, options,
                target_msc, divisor, remainder, notifies_len, notifies);
}
