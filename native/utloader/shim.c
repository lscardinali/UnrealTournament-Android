/*
 * glibc -> bionic shim for custom-loaded modules (UT469 Linux arm64, GNU libstdc++/libgcc_s).
 *
 * Everything that has the same name and arm64 ABI in both libcs passes straight through to bionic
 * (utl_bionic_lookup). This file covers the rest: different struct layouts or constants, glibc-only names,
 * GNU-vs-POSIX variants, loader services (dl*, dl_iterate_phdr, _dl_find_object, pthread_create with TLS,
 * __libc_start_main) and the glibc locale internals libstdc++ ("gnu" model) reaches into.
 * Anything still missing binds to a trap stub that names itself when called.
 */
#define _GNU_SOURCE
#include "utl.h"

#include <alloca.h>
#include <ctype.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include <netdb.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <fenv.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ------------------------------------------------------------------------------------------ trap stubs */

#ifndef SDLCALL
#define SDLCALL
#endif

#include "trap_stubs.h"

static const char *trap_names[UTL_NTRAPS];
static int trap_count;
static pthread_mutex_t trap_lock = PTHREAD_MUTEX_INITIALIZER;

static void utl_trap_hit(int i) {
    utl_log("FATAL: unimplemented glibc function called: %s", trap_names[i] ? trap_names[i] : "?");
    abort();
}

void *utl_trap_stub(const char *name) {
    void *r = NULL;
    pthread_mutex_lock(&trap_lock);
    for (int i = 0; i < trap_count; i++)
        if (strcmp(trap_names[i], name) == 0) {
            r = (void *)utl_traps[i];
            break;
        }
    if (!r && trap_count < UTL_NTRAPS) {
        trap_names[trap_count] = strdup(name);
        r = (void *)utl_traps[trap_count++];
    }
    pthread_mutex_unlock(&trap_lock);
    return r ? r : (void *)utl_traps[UTL_NTRAPS - 1];
}

/* ------------------------------------------------------------------------------------------ bionic */

void *utl_bionic_lookup(const char *name) {
    static void *libc, *libm, *libdl;
    void *p;
    if (!libc) {
        libc = dlopen("libc.so", RTLD_NOW);
        libm = dlopen("libm.so", RTLD_NOW);
        libdl = dlopen("libdl.so", RTLD_NOW);
    }
    if (!(p = dlsym(libc, name)) && !(p = dlsym(libm, name))) p = dlsym(libdl, name);
    (void)dlerror(); /* a miss in libc before the hit in libm must not leave a stale error for the game's dlerror() */
    return p;
}

/* ------------------------------------------------------------------------------------------ errno, ctype */

static int *g_errno_location(void) { return &errno; }
static int *g_h_errno_location(void) { return &h_errno; }

/* glibc ctype tables: 384 entries for -128..255, the returned pointer at index 0. Bit layout (little endian):
   upper 0x100 lower 0x200 alpha 0x400 digit 0x800 xdigit 0x1000 space 0x2000 print 0x4000 graph 0x8000
   blank 0x1 cntrl 0x2 punct 0x4 alnum 0x8. */
static unsigned short g_ctype_b[384];
static int32_t g_ctype_tolower[384], g_ctype_toupper[384];
static const unsigned short *g_ctype_b_ptr = g_ctype_b + 128;
static const int32_t *g_ctype_tolower_ptr = g_ctype_tolower + 128;
static const int32_t *g_ctype_toupper_ptr = g_ctype_toupper + 128;

static void init_ctype(void) {
    for (int c = -128; c < 256; c++) {
        unsigned short m = 0;
        const int u = c & 0xff; /* signed char values alias 128..255 in the "C" locale: no class */
        if (c >= 0 && c < 128) {
            if (isupper(u)) m |= 0x100;
            if (islower(u)) m |= 0x200;
            if (isalpha(u)) m |= 0x400;
            if (isdigit(u)) m |= 0x800;
            if (isxdigit(u)) m |= 0x1000;
            if (isspace(u)) m |= 0x2000;
            if (isprint(u)) m |= 0x4000;
            if (isgraph(u)) m |= 0x8000;
            if (u == ' ' || u == '\t') m |= 0x1;
            if (iscntrl(u)) m |= 0x2;
            if (ispunct(u)) m |= 0x4;
            if (isalnum(u)) m |= 0x8;
        }
        g_ctype_b[c + 128] = m;
        g_ctype_tolower[c + 128] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        g_ctype_toupper[c + 128] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
}

static const unsigned short **g_ctype_b_loc(void) { return &g_ctype_b_ptr; }
static const int32_t **g_ctype_tolower_loc(void) { return &g_ctype_tolower_ptr; }
static const int32_t **g_ctype_toupper_loc(void) { return &g_ctype_toupper_ptr; }

/* ------------------------------------------------------------------------------------------ locale (glibc layout) */

/* glibc's struct __locale_struct: libstdc++'s "gnu" locale model reads __ctype_b/__ctype_tolower/__ctype_toupper
   straight from it. One "C" instance is all UT needs. */
typedef struct {
    void *locales[13];
    const unsigned short *ctype_b;
    const int32_t *ctype_tolower;
    const int32_t *ctype_toupper;
    const char *names[13];
} glibc_locale;

static glibc_locale g_c_locale;

static void init_locale(void) {
    g_c_locale.ctype_b = g_ctype_b_ptr;
    g_c_locale.ctype_tolower = g_ctype_tolower_ptr;
    g_c_locale.ctype_toupper = g_ctype_toupper_ptr;
    for (int i = 0; i < 13; i++) g_c_locale.names[i] = "C";
}

static void *g_newlocale(int mask, const char *name, void *base) { return &g_c_locale; }
static void *g_duplocale(void *l) { return &g_c_locale; }
static void g_freelocale(void *l) {}
static void *g_uselocale(void *l) { return &g_c_locale; }

/* glibc nl_item = (category << 16) | index; LC_CTYPE 0, LC_NUMERIC 1, LC_TIME 2, LC_COLLATE 3, LC_MONETARY 4. */
static char *g_nl_langinfo_l(int item, void *loc) {
    switch (item) {
    case 14: return "ANSI_X3.4-1968";          /* CODESET */
    case 0x10000: return ".";                  /* RADIXCHAR / DECIMAL_POINT */
    case 0x10001: return "";                   /* THOUSEP */
    case 0x10002: return "";                   /* GROUPING */
    case 0x10003: return (char *)(uintptr_t)L'.'; /* _NL_NUMERIC_DECIMAL_POINT_WC: libstdc++ reads the pointer as a wchar_t */
    case 0x10004: return (char *)(uintptr_t)0; /* _NL_NUMERIC_THOUSANDS_SEP_WC */
    default: return "";
    }
}
static char *g_nl_langinfo(int item) { return g_nl_langinfo_l(item, NULL); }

static int g_strcoll_l(const char *a, const char *b, void *l) { return strcoll(a, b); }
static size_t g_strxfrm_l(char *d, const char *s, size_t n, void *l) { return strxfrm(d, s, n); }
static int g_wcscoll_l(const wchar_t *a, const wchar_t *b, void *l) { return wcscoll(a, b); }
static size_t g_wcsxfrm_l(wchar_t *d, const wchar_t *s, size_t n, void *l) { return wcsxfrm(d, s, n); }
static wint_t g_towlower_l(wint_t c, void *l) { return towlower(c); }
static wint_t g_towupper_l(wint_t c, void *l) { return towupper(c); }
static wctype_t g_wctype_l(const char *n, void *l) { return wctype(n); }
static int g_iswctype_l(wint_t c, wctype_t t, void *l) { return iswctype(c, t); }
static size_t g_strftime_l(char *s, size_t n, const char *f, const struct tm *t, void *l) { return strftime(s, n, f, t); }
static size_t g_wcsftime_l(wchar_t *s, size_t n, const wchar_t *f, const struct tm *t, void *l) { return wcsftime(s, n, f, t); }
static double g_strtod_l(const char *s, char **e, void *l) { return strtod(s, e); }
static float g_strtof_l(const char *s, char **e, void *l) { return strtof(s, e); }
static long double g_strtold_l(const char *s, char **e, void *l) { return strtold(s, e); }
static char *g_setlocale(int cat, const char *name) { return "C"; }

/* ------------------------------------------------------------------------------------------ misc glibc names */

static void g_assert_fail(const char *expr, const char *file, unsigned line, const char *func) {
    utl_log("assertion failed: %s (%s:%u %s)", expr, file, line, func ? func : "");
    abort();
}

static const char *g_gnu_get_libc_version(void) { return "2.35"; }
static char *g_secure_getenv(const char *n) { return getenv(n); }
static char exe_path[512];
/* AT_EXECFN is the app process (app_process64): answer with the loaded program, like readlink("/proc/self/exe").
   Unreal 227's appBaseDir uses it. */
static unsigned long g_getauxval(unsigned long t) {
    if (t == 31 /* AT_EXECFN */ && exe_path[0]) return (unsigned long)exe_path;
    return getauxval(t);
}
static int g_register_atfork(void (*p)(void), void (*pa)(void), void (*c)(void), void *dso) { return pthread_atfork(p, pa, c); }
static int g_sched_cpucount(size_t setsize, const unsigned long *set) {
    int n = 0;
    for (size_t i = 0; i < setsize / sizeof(unsigned long); i++) n += __builtin_popcountl(set[i]);
    return n;
}
static int g_malloc_trim(size_t pad) { return 0; }
/* With _GNU_SOURCE bionic's strerror_r is the GNU variant (returns the message), like glibc's strerror_r. */
static char *g_strerror_r(int e, char *buf, size_t n) { return strerror_r(e, buf, n); }
static int g_xpg_strerror_r(int e, char *buf, size_t n) { /* POSIX variant: always fills buf */
    const char *m = strerror_r(e, buf, n);
    if (m != buf && n) snprintf(buf, n, "%s", m);
    return 0;
}
static char *g_basename(const char *p) {
    const char *s = strrchr(p, '/');
    return (char *)(s ? s + 1 : p);
}
static int g_fcntl64(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}
static long g_isoc_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
static unsigned long g_isoc_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
static long long g_isoc_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
static unsigned long long g_isoc_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
static int g_isoc_vsscanf(const char *s, const char *f, va_list ap) { return vsscanf(s, f, ap); }
static int g_isoc_sscanf(const char *s, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsscanf(s, f, ap);
    va_end(ap);
    return r;
}
static int g_isoc_vfscanf(FILE *fp, const char *f, va_list ap) { return vfscanf(fp, f, ap); }
static int g_isoc_fscanf(FILE *fp, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vfscanf(fp, f, ap);
    va_end(ap);
    return r;
}
static long g_isoc_wcstol(const wchar_t *s, wchar_t **e, int b) { return wcstol(s, e, b); }
static unsigned long g_isoc_wcstoul(const wchar_t *s, wchar_t **e, int b) { return wcstoul(s, e, b); }

/* No message catalogs: gettext returns the untranslated text (libstdc++ uses it for exception messages). */
static char *g_gettext(const char *m) { return (char *)m; }
static char *g_dgettext(const char *d, const char *m) { return (char *)m; }
static char *g_dcgettext(const char *d, const char *m, int c) { return (char *)m; }
static char *g_bindtextdomain(const char *d, const char *dir) { return (char *)(dir ? dir : "/"); }
static char *g_bind_textdomain_codeset(const char *d, const char *cs) { return (char *)cs; }
static char *g_textdomain(const char *d) { return (char *)(d ? d : "messages"); }
static int g_pthread_setcanceltype(int t, int *old) { if (old) *old = 0; return 0; } /* bionic: no cancellation */
static int g_pthread_setcancelstate(int s, int *old) { if (old) *old = 0; return 0; }
static int g_posix_spawn_addchdir(void *fa, const char *p) { return ENOSYS; }

/* Fortified fprintf (libsndfile): bionic doesn't export glibc's name. */
static int g_fprintf_chk(FILE *fp, int flag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

char g_single_threaded = 0;    /* __libc_single_threaded: never claim single-threaded */
static char *g_progname = "ut-bin";

/* ------------------------------------------------------------------------------------------ iconv */

/* bionic's iconv has no WINDOWS-1252, which UT uses for ANSI <-> wide text (WCHAR_T and UTF-16LE, with
   //IGNORE). Those pairs are converted here; everything else goes to bionic. */
enum { ENC_CP1252, ENC_WCHAR, ENC_UTF16LE, ENC_OTHER };

static const uint16_t cp1252_high[32] = { /* 0x80..0x9F; undefined bytes map to themselves */
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D,
    0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
    0x0153, 0x009D, 0x017E, 0x0178,
};

typedef struct {
    uint32_t magic;
    int from, to, ignore;
} our_iconv;
#define OUR_ICONV_MAGIC 0x31323532u

static int enc_of(const char *name, int *ignore) {
    char n[64];
    size_t i = 0;
    for (; name[i] && name[i] != '/' && i < sizeof n - 1; i++) n[i] = (char)toupper((unsigned char)name[i]);
    n[i] = 0;
    if (ignore && strstr(name, "//IGNORE")) *ignore = 1;
    if (!strcmp(n, "WINDOWS-1252") || !strcmp(n, "CP1252") || !strcmp(n, "WINDOWS1252")) return ENC_CP1252;
    if (!strcmp(n, "WCHAR_T") || !strcmp(n, "UCS-4LE") || !strcmp(n, "UTF-32LE")) return ENC_WCHAR;
    if (!strcmp(n, "UTF-16LE")) return ENC_UTF16LE;
    return ENC_OTHER;
}

static our_iconv *our_handles[64];

static iconv_t g_iconv_open(const char *to, const char *from) {
    int ignore = 0;
    const int t = enc_of(to, &ignore), f = enc_of(from, NULL);
    if ((t == ENC_CP1252 || f == ENC_CP1252) && t != ENC_OTHER && f != ENC_OTHER) {
        our_iconv *c = calloc(1, sizeof *c);
        c->magic = OUR_ICONV_MAGIC;
        c->from = f;
        c->to = t;
        c->ignore = ignore;
        for (int i = 0; i < 64; i++)
            if (!our_handles[i]) { our_handles[i] = c; break; }
        return (iconv_t)c;
    }
    /* bionic spells it "UTF-8"; UT sometimes asks for "UTF8". */
    if (!strncasecmp(to, "UTF8", 4)) to = "UTF-8";
    if (!strncasecmp(from, "UTF8", 4)) from = "UTF-8";
    return iconv_open(to, from);
}

static our_iconv *ours(iconv_t cd) {
    for (int i = 0; i < 64; i++)
        if (our_handles[i] && (iconv_t)our_handles[i] == cd) return our_handles[i];
    return NULL;
}

static size_t g_iconv(iconv_t cd, char **in, size_t *inleft, char **out, size_t *outleft) {
    our_iconv *c = ours(cd);
    if (!c) return iconv(cd, in, inleft, out, outleft);
    if (!in || !*in) return 0; /* reset: stateless */
    size_t irreversible = 0;
    while (*inleft) {
        uint32_t cp;
        size_t used;
        const uint8_t *s = (const uint8_t *)*in;
        switch (c->from) {
        case ENC_CP1252: cp = s[0] >= 0x80 && s[0] < 0xA0 ? cp1252_high[s[0] - 0x80] : s[0]; used = 1; break;
        case ENC_WCHAR:
            if (*inleft < 4) { errno = EINVAL; return (size_t)-1; }
            memcpy(&cp, s, 4); used = 4; break;
        default: /* UTF-16LE */
            if (*inleft < 2) { errno = EINVAL; return (size_t)-1; }
            cp = (uint32_t)(s[0] | (s[1] << 8)); used = 2;
            if (cp >= 0xD800 && cp < 0xDC00) {
                if (*inleft < 4) { errno = EINVAL; return (size_t)-1; }
                cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)(s[2] | (s[3] << 8)) - 0xDC00);
                used = 4;
            }
        }
        uint8_t buf[4];
        size_t n;
        switch (c->to) {
        case ENC_CP1252: {
            int b = -1;
            if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) b = (int)cp;
            else for (int i = 0; i < 32; i++) if (cp1252_high[i] == cp) { b = 0x80 + i; break; }
            if (b < 0) {
                if (!c->ignore) { errno = EILSEQ; return (size_t)-1; }
                irreversible++;
                *in += used; *inleft -= used;
                continue;
            }
            buf[0] = (uint8_t)b; n = 1; break;
        }
        case ENC_WCHAR: memcpy(buf, &cp, 4); n = 4; break;
        default: /* UTF-16LE */
            if (cp >= 0x10000) {
                const uint32_t v = cp - 0x10000, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
                buf[0] = hi & 0xff; buf[1] = hi >> 8; buf[2] = lo & 0xff; buf[3] = lo >> 8; n = 4;
            } else {
                buf[0] = cp & 0xff; buf[1] = (cp >> 8) & 0xff; n = 2;
            }
        }
        if (*outleft < n) { errno = E2BIG; return (size_t)-1; }
        memcpy(*out, buf, n);
        *out += n; *outleft -= n;
        *in += used; *inleft -= used;
    }
    return irreversible;
}

static int g_iconv_close(iconv_t cd) {
    our_iconv *c = ours(cd);
    if (!c) return iconv_close(cd);
    for (int i = 0; i < 64; i++)
        if (our_handles[i] == c) our_handles[i] = NULL;
    free(c);
    return 0;
}

static int g_bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

/* Core derives its base directory from readlink("/proc/self/exe"): answer with the loaded program's path. */
static ssize_t g_readlink(const char *path, char *buf, size_t n) {
    if (path && exe_path[0] && strcmp(path, "/proc/self/exe") == 0) {
        size_t l = strlen(exe_path);
        if (l > n) l = n;
        memcpy(buf, exe_path, l);
        return (ssize_t)l;
    }
    return readlink(path, buf, n);
}

/* glibc's FE_DFL_ENV / FE_NOMASK_ENV are the pointers -1 / -2; bionic's FE_DFL_ENV is a real object
   (Unreal 227's PhysX calls fesetenv(FE_DFL_ENV)). */
static const fenv_t *g_fe_env(const fenv_t *e) {
    return ((intptr_t)e == -1 || (intptr_t)e == -2) ? FE_DFL_ENV : e;
}
static int g_fesetenv(const fenv_t *e) { return fesetenv(g_fe_env(e)); }
static int g_feupdateenv(const fenv_t *e) { return feupdateenv(g_fe_env(e)); }

/* realpath("/proc/self/exe") resolves to app_process64 too: same answer as readlink. */
static char *g_realpath(const char *path, char *resolved) {
    if (path && exe_path[0] && strcmp(path, "/proc/self/exe") == 0) {
        if (!resolved) return strdup(exe_path);
        snprintf(resolved, PATH_MAX, "%s", exe_path);
        return resolved;
    }
    return realpath(path, resolved);
}

/* ------------------------------------------------------------------------------------------ sysconf */

static long g_sysconf(int name) {
    static const struct { int glibc, bionic; } map[] = {
        { 0, _SC_ARG_MAX }, { 1, _SC_CHILD_MAX }, { 2, _SC_CLK_TCK }, { 3, _SC_NGROUPS_MAX }, { 4, _SC_OPEN_MAX },
        { 30, _SC_PAGESIZE }, { 43, _SC_LINE_MAX }, { 60, _SC_IOV_MAX }, { 69, _SC_GETGR_R_SIZE_MAX },
        { 70, _SC_GETPW_R_SIZE_MAX }, { 75, _SC_THREAD_STACK_MIN }, { 83, _SC_NPROCESSORS_CONF },
        { 84, _SC_NPROCESSORS_ONLN }, { 85, _SC_PHYS_PAGES }, { 86, _SC_AVPHYS_PAGES },
        { 190, _SC_LEVEL1_DCACHE_LINESIZE },
    };
    if (name == 249) return MINSIGSTKSZ; /* _SC_MINSIGSTKSZ (glibc 2.34+) */
    if (name == 250) return SIGSTKSZ;    /* _SC_SIGSTKSZ */
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].glibc == name) return sysconf(map[i].bionic);
    utl_log("sysconf: unmapped glibc constant %d", name);
    errno = EINVAL;
    return -1;
}

/* ------------------------------------------------------------------------------------------ signals */

/* glibc arm64 struct sigaction: handler, 128-byte sigset, int flags, restorer. */
typedef struct {
    void *handler;
    unsigned long mask[16];
    int flags;
    void (*restorer)(void);
} glibc_sigaction;

/* Fatal signals stay with the runtime: ART uses SIGSEGV/SIGBUS itself, and the engine's handlers would run on
   threads without our TLS block (they read UnGuardBlockTLS). */
static int runtime_signal(int sig) {
    return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE || sig == SIGABRT || sig == SIGTRAP ||
           sig == SIGSYS;
}

/* In an app, ART owns SIGSEGV/SIGBUS (implicit null checks, stack overflow) through libsigchain, which
   interposes sigaction: call the process's default sigaction (sigchain's in an app, libc's otherwise), and never
   let the engine take those two. */
static int g_sigaction(int sig, const glibc_sigaction *act, glibc_sigaction *old) {
    static int (*sys_sigaction)(int, const struct sigaction *, struct sigaction *);
    struct sigaction b, bo;
    if (!sys_sigaction) sys_sigaction = (int (*)(int, const struct sigaction *, struct sigaction *))dlsym(RTLD_DEFAULT, "sigaction");
    if (act && runtime_signal(sig) && act->handler != (void *)SIG_IGN && act->handler != (void *)SIG_DFL) {
        utl_log("sigaction(%d): kept for the runtime (ART); the engine's handler is not installed", sig);
        if (old) memset(old, 0, sizeof *old);
        return 0;
    }
    memset(&b, 0, sizeof b);
    if (act) {
        b.sa_sigaction = (void (*)(int, siginfo_t *, void *))act->handler;
        memcpy(&b.sa_mask, act->mask, sizeof b.sa_mask < sizeof act->mask ? sizeof b.sa_mask : sizeof act->mask);
        b.sa_flags = act->flags;
    }
    int r = sys_sigaction(sig, act ? &b : NULL, old ? &bo : NULL);
    if (r == 0 && old) {
        memset(old, 0, sizeof *old);
        old->handler = (void *)bo.sa_sigaction;
        memcpy(old->mask, &bo.sa_mask, sizeof bo.sa_mask);
        old->flags = bo.sa_flags;
    }
    return r;
}

/* The engine installs its own (small) alternate signal stack; ART's handler (and ours) run on it first and
   overflow it, killing the process silently. bionic already gives every thread a suitable one: keep that. */
static int g_sigaltstack(const stack_t *ss, stack_t *old) {
    if (old) return sigaltstack(NULL, old);
    (void)ss;
    return 0;
}

/* signal() must take the same path: straight to libc it would bypass both the filter and libsigchain. */
typedef void (*sighandler_fn)(int);
static sighandler_fn g_signal(int sig, sighandler_fn h) {
    glibc_sigaction act, old;
    memset(&act, 0, sizeof act);
    act.handler = (void *)h;
    act.flags = SA_RESTART;
    if (g_sigaction(sig, &act, &old) != 0) return SIG_ERR;
    return (sighandler_fn)old.handler;
}

/* ------------------------------------------------------------------------------------------ getaddrinfo */

/* glibc: ..., ai_addrlen, ai_addr, ai_canonname, ai_next. bionic: ..., ai_addrlen, ai_canonname, ai_addr, ai_next. */
typedef struct glibc_addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct glibc_addrinfo *ai_next;
} glibc_addrinfo;

static int g_getaddrinfo(const char *node, const char *service, const glibc_addrinfo *hints, glibc_addrinfo **res) {
    struct addrinfo h, *r = NULL;
    memset(&h, 0, sizeof h);
    if (hints) {
        h.ai_flags = hints->ai_flags;
        h.ai_family = hints->ai_family;
        h.ai_socktype = hints->ai_socktype;
        h.ai_protocol = hints->ai_protocol;
    }
    int e = getaddrinfo(node, service, hints ? &h : NULL, &r);
    if (e) return e;
    glibc_addrinfo *head = NULL, **tail = &head;
    for (struct addrinfo *p = r; p; p = p->ai_next) {
        glibc_addrinfo *g = calloc(1, sizeof *g + p->ai_addrlen);
        g->ai_flags = p->ai_flags;
        g->ai_family = p->ai_family;
        g->ai_socktype = p->ai_socktype;
        g->ai_protocol = p->ai_protocol;
        g->ai_addrlen = p->ai_addrlen;
        g->ai_addr = (struct sockaddr *)(g + 1);
        memcpy(g->ai_addr, p->ai_addr, p->ai_addrlen);
        g->ai_canonname = p->ai_canonname ? strdup(p->ai_canonname) : NULL;
        *tail = g;
        tail = &g->ai_next;
    }
    freeaddrinfo(r);
    *res = head;
    return 0;
}

static void g_freeaddrinfo(glibc_addrinfo *g) {
    while (g) {
        glibc_addrinfo *n = g->ai_next;
        free(g->ai_canonname);
        free(g);
        g = n;
    }
}

/* ------------------------------------------------------------------------------------------ threads */

static int g_pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    return utl_thread_create(t, attr, fn, arg);
}

/* __cxa_thread_atexit_impl: destructors of C++ thread_local objects. bionic's looks the dso handle up in its own
   linker (aborts for our modules), so keep a per-thread list and run it when the thread exits. */
typedef struct tl_dtor {
    void (*fn)(void *);
    void *obj;
    struct tl_dtor *next;
} tl_dtor;
static pthread_key_t tl_key;
static pthread_once_t tl_once = PTHREAD_ONCE_INIT;

static void tl_run(void *head) {
    for (tl_dtor *d = head; d;) {
        tl_dtor *n = d->next;
        d->fn(d->obj);
        free(d);
        d = n;
    }
}
static void tl_make_key(void) { pthread_key_create(&tl_key, tl_run); }

static int g_cxa_thread_atexit_impl(void (*fn)(void *), void *obj, void *dso) {
    pthread_once(&tl_once, tl_make_key);
    tl_dtor *d = malloc(sizeof *d);
    d->fn = fn;
    d->obj = obj;
    d->next = pthread_getspecific(tl_key); /* run in reverse order of registration */
    pthread_setspecific(tl_key, d);
    return 0;
}

/* ------------------------------------------------------------------------------------------ Vulkan interposer */

/* (also preTransform: see w_create_swapchain) */
/* VulkanDrv dlopens libvulkan.so.1 and fetches everything through vkGetInstanceProcAddr / vkGetDeviceProcAddr,
   so we hand it wrapped versions of those two and fix desktop assumptions Qualcomm's Android driver doesn't share
   (Turnip on X11 tolerated them; found with the Khronos validation layer):
   - compositeAlpha: VulkanDrv asks for OPAQUE; Android surfaces support INHERIT only.
   - command buffers are re-begun without a reset from pools lacking RESET_COMMAND_BUFFER: Turnip resets
     implicitly, Adreno fails the begin and then crashes recording into it. Give every pool the reset flag
     (then vkBeginCommandBuffer resets implicitly, which is valid Vulkan). */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

static void *vulkan_handle;
static VkInstance vk_instance;
static VkPhysicalDevice vk_physical;
static PFN_vkGetInstanceProcAddr real_gipa;
static PFN_vkGetDeviceProcAddr real_gdpa;
static PFN_vkCreateDevice real_create_device;
static PFN_vkCreateSwapchainKHR real_create_swapchain;
static PFN_vkCreateCommandPool real_create_pool;

static VKAPI_ATTR VkResult VKAPI_CALL w_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
                                                      const VkAllocationCallbacks *a, VkDevice *out) {
    vk_physical = pd;
    return real_create_device(pd, ci, a, out);
}

/* Android destroys the activity's surface in the background and makes a new one on return; VulkanDrv's
   VkSurfaceKHR would keep pointing at the dead one (black screen). We keep VulkanDrv's handle as a stand-in:
   when SDL's ANativeWindow changes, a fresh real surface is created for it, acquire reports OUT_OF_DATE so
   VulkanDrv rebuilds its swapchain, and every call that takes the stand-in gets the current real surface. */
static void *sdl_window;                      /* the window VulkanDrv's surface belongs to */
static VkSurfaceKHR stand_in_surface, current_surface;
static void *current_anw;                     /* ANativeWindow the current surface was made from */
static int surface_replaced;                  /* set until the next swapchain is created */

static void *window_anw(void) {
    static uint32_t (*get_props)(void *);
    static void *(*get_ptr)(uint32_t, const char *, void *);
    if (!sdl_window) return NULL;
    if (!get_props) get_props = (uint32_t (*)(void *))utl_external_lookup("SDL_GetWindowProperties");
    if (!get_ptr) get_ptr = (void *(*)(uint32_t, const char *, void *))utl_external_lookup("SDL_GetPointerProperty");
    return get_props && get_ptr ? get_ptr(get_props(sdl_window), "SDL.window.android.window", NULL) : NULL;
}

static VkSurfaceKHR map_surface(VkSurfaceKHR s) { return (s && s == stand_in_surface) ? current_surface : s; }

/* Returns 1 if the window changed and a new surface was made (the swapchain must be rebuilt). */
static int check_surface(void) {
    void *anw = window_anw();
    if (!anw || anw == current_anw || !vk_instance) return 0;
    PFN_vkCreateAndroidSurfaceKHR create =
        (PFN_vkCreateAndroidSurfaceKHR)real_gipa(vk_instance, "vkCreateAndroidSurfaceKHR");
    VkAndroidSurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, NULL, 0, anw };
    VkSurfaceKHR fresh = VK_NULL_HANDLE;
    if (!create || create(vk_instance, &ci, NULL, &fresh) != VK_SUCCESS) return 0;
    /* The previous real surface is left alone: its swapchain may still exist until VulkanDrv replaces it. */
    utl_log("Android window changed (%p -> %p): new Vulkan surface, swapchain rebuild", current_anw, anw);
    current_surface = fresh;
    current_anw = anw;
    surface_replaced = 1;
    return 1;
}

static int g_SDL_Vulkan_CreateSurface(void *window, VkInstance inst, const VkAllocationCallbacks *a, VkSurfaceKHR *out) {
    static int (*real)(void *, VkInstance, const VkAllocationCallbacks *, VkSurfaceKHR *);
    if (!real) real = (int (*)(void *, VkInstance, const VkAllocationCallbacks *, VkSurfaceKHR *))
                          utl_external_lookup("SDL_Vulkan_CreateSurface");
    int ok = real && real(window, inst, a, out);
    if (ok) {
        sdl_window = window;
        vk_instance = inst;
        stand_in_surface = current_surface = *out;
        current_anw = window_anw();
    }
    return ok;
}

static PFN_vkAcquireNextImageKHR real_acquire;
static PFN_vkQueuePresentKHR real_present;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR real_surf_support;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR real_surf_caps;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR real_surf_formats;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR real_surf_modes;
static PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR real_surf_caps2;
static PFN_vkGetPhysicalDeviceSurfaceFormats2KHR real_surf_formats2;

static VKAPI_ATTR VkResult VKAPI_CALL w_acquire(VkDevice d, VkSwapchainKHR sc, uint64_t t, VkSemaphore s, VkFence f, uint32_t *i) {
    if (check_surface()) return VK_ERROR_OUT_OF_DATE_KHR;
    VkResult r = real_acquire(d, sc, t, s, f, i);
    return r == VK_ERROR_SURFACE_LOST_KHR ? VK_ERROR_OUT_OF_DATE_KHR : r;
}
/* Presented frames: for SDL's look-axis scaling (shared struct) and the app's FPS overlay (frames file). */
volatile uint32_t *utl_frames_shared, *utl_frames_file;

static VKAPI_ATTR VkResult VKAPI_CALL w_present(VkQueue q, const VkPresentInfoKHR *pi) {
    VkResult r = real_present(q, pi);
    if (utl_frames_shared) __atomic_fetch_add(utl_frames_shared, 1, __ATOMIC_RELAXED);
    if (utl_frames_file) __atomic_fetch_add(utl_frames_file, 1, __ATOMIC_RELAXED);
    return r == VK_ERROR_SURFACE_LOST_KHR ? VK_ERROR_OUT_OF_DATE_KHR : r;
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_support(VkPhysicalDevice p, uint32_t q, VkSurfaceKHR s, VkBool32 *o) {
    return real_surf_support(p, q, map_surface(s), o);
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_caps(VkPhysicalDevice p, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *o) {
    return real_surf_caps(p, map_surface(s), o);
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_formats(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *n, VkSurfaceFormatKHR *o) {
    return real_surf_formats(p, map_surface(s), n, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_modes(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *n, VkPresentModeKHR *o) {
    return real_surf_modes(p, map_surface(s), n, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_caps2(VkPhysicalDevice p, const VkPhysicalDeviceSurfaceInfo2KHR *in, VkSurfaceCapabilities2KHR *o) {
    VkPhysicalDeviceSurfaceInfo2KHR m = *in;
    m.surface = map_surface(in->surface);
    return real_surf_caps2(p, &m, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL w_surf_formats2(VkPhysicalDevice p, const VkPhysicalDeviceSurfaceInfo2KHR *in, uint32_t *n, VkSurfaceFormat2KHR *o) {
    VkPhysicalDeviceSurfaceInfo2KHR m = *in;
    m.surface = map_surface(in->surface);
    return real_surf_formats2(p, &m, n, o);
}

static VKAPI_ATTR VkResult VKAPI_CALL w_create_swapchain(VkDevice dev, const VkSwapchainCreateInfoKHR *ci,
                                                         const VkAllocationCallbacks *alloc, VkSwapchainKHR *out) {
    VkSwapchainCreateInfoKHR fixed = *ci;
    fixed.surface = map_surface(ci->surface);
    if (surface_replaced) { /* the old swapchain belongs to the dead surface: don't chain from it */
        fixed.oldSwapchain = VK_NULL_HANDLE;
        surface_replaced = 0;
    }
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR caps_fn = vk_instance ?
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)real_gipa(vk_instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") : NULL;
    VkSurfaceCapabilitiesKHR caps;
    if (caps_fn && vk_physical && caps_fn(vk_physical, fixed.surface, &caps) == VK_SUCCESS &&
        !(fixed.compositeAlpha & caps.supportedCompositeAlpha)) {
        static const VkCompositeAlphaFlagBitsKHR prefer[] = { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR };
        for (size_t i = 0; i < sizeof prefer / sizeof prefer[0]; i++)
            if (caps.supportedCompositeAlpha & prefer[i]) { fixed.compositeAlpha = prefer[i]; break; }
        utl_log("vkCreateSwapchainKHR: compositeAlpha %#x unsupported (surface: %#x) -> %#x", ci->compositeAlpha,
                caps.supportedCompositeAlpha, fixed.compositeAlpha);
    }
    /* Android reports the display's rotation as the surface's currentTransform (the RP6 panel is portrait).
       VulkanDrv passes it back as preTransform ("already rotated") without rotating: show it sideways. Let the
       compositor rotate instead. */
    if (fixed.preTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        utl_log("vkCreateSwapchainKHR: preTransform %#x -> IDENTITY (the compositor rotates)", fixed.preTransform);
        fixed.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    VkResult r = real_create_swapchain(dev, &fixed, alloc, out);
    utl_log("vkCreateSwapchainKHR %ux%u format %d present mode %d images %u -> %d", fixed.imageExtent.width,
            fixed.imageExtent.height, fixed.imageFormat, fixed.presentMode, fixed.minImageCount, r);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL w_create_pool(VkDevice dev, const VkCommandPoolCreateInfo *ci,
                                                    const VkAllocationCallbacks *a, VkCommandPool *out) {
    VkCommandPoolCreateInfo fixed = *ci;
    fixed.flags |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    return real_create_pool(dev, &fixed, a, out);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL w_gdpa(VkDevice dev, const char *name);

static PFN_vkVoidFunction wrap_vk(const char *name, PFN_vkVoidFunction real) {
    if (!real) return real;
#define WRAP(n, var, w) if (strcmp(name, n) == 0) { var = (void *)real; return (PFN_vkVoidFunction)w; }
    WRAP("vkCreateDevice", real_create_device, w_create_device)
    WRAP("vkCreateSwapchainKHR", real_create_swapchain, w_create_swapchain)
    WRAP("vkCreateCommandPool", real_create_pool, w_create_pool)
    WRAP("vkGetDeviceProcAddr", real_gdpa, w_gdpa)
    WRAP("vkAcquireNextImageKHR", real_acquire, w_acquire)
    WRAP("vkQueuePresentKHR", real_present, w_present)
    WRAP("vkGetPhysicalDeviceSurfaceSupportKHR", real_surf_support, w_surf_support)
    WRAP("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", real_surf_caps, w_surf_caps)
    WRAP("vkGetPhysicalDeviceSurfaceFormatsKHR", real_surf_formats, w_surf_formats)
    WRAP("vkGetPhysicalDeviceSurfacePresentModesKHR", real_surf_modes, w_surf_modes)
    WRAP("vkGetPhysicalDeviceSurfaceCapabilities2KHR", real_surf_caps2, w_surf_caps2)
    WRAP("vkGetPhysicalDeviceSurfaceFormats2KHR", real_surf_formats2, w_surf_formats2)
#undef WRAP
    return real;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL w_gipa(VkInstance inst, const char *name) {
    static int announced;
    if (inst) vk_instance = inst;
    if (!announced) {
        announced = 1;
        utl_log("Vulkan interposer active (VulkanDrv -> system libvulkan.so)");
    }
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)w_gipa;
    return wrap_vk(name, real_gipa(inst, name));
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL w_gdpa(VkDevice dev, const char *name) {
    return wrap_vk(name, real_gdpa(dev, name));
}

/* ------------------------------------------------------------------------------------------ dl* */

#define GLOBAL_HANDLE ((void *)0x7574) /* dlopen(NULL): the program's global scope */
static __thread char dl_err[512];
static __thread int dl_err_set;

static int is_our_module(void *h) {
    for (utl_module *m = utl_modules(); m; m = m->next)
        if (h == m) return 1;
    return 0;
}

static void *g_dlopen(const char *name, int flags) {
    if (!name) return GLOBAL_HANDLE;
    const char *b = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
    if (strcmp(b, "libvulkan.so.1") == 0 || strcmp(b, "libvulkan.so") == 0)
        return vulkan_handle = dlopen("libvulkan.so", RTLD_NOW);
    utl_module *m = utl_load(name);
    if (m) {
        utl_run_inits(m);
        return m;
    }
    if (strcmp(b, "libSDL3.so.0") == 0) b = "libSDL3.so";
    void *h = dlopen(b, RTLD_NOW);
    if (!h) {
        snprintf(dl_err, sizeof dl_err, "%s: cannot open (utloader)", name);
        dl_err_set = 1;
    }
    return h;
}

static void *g_dlsym(void *h, const char *name) {
    void *p = NULL;
    if (h == GLOBAL_HANDLE || h == RTLD_DEFAULT || h == RTLD_NEXT) {
        p = utl_sym(NULL, name);
        if (!p) p = utl_shim_lookup(name);
        if (!p) p = utl_bionic_lookup(name);
    } else if (is_our_module(h)) {
        /* glibc searches the module, then its dependencies (which include libc/libm: our shim and bionic). */
        p = utl_sym((utl_module *)h, name);
        for (int i = 0; !p && i < ((utl_module *)h)->nneeded; i++) p = utl_sym(((utl_module *)h)->needed[i], name);
        if (!p) p = utl_shim_lookup(name);
        if (!p) p = utl_bionic_lookup(name);
    } else {
        p = dlsym(h, name);
        if (p && h == vulkan_handle) p = (void *)wrap_vk(name, (PFN_vkVoidFunction)p);
        if (p && h == vulkan_handle && strcmp(name, "vkGetInstanceProcAddr") == 0) {
            real_gipa = (PFN_vkGetInstanceProcAddr)p;
            p = (void *)w_gipa;
        }
    }
    if (!p) {
        snprintf(dl_err, sizeof dl_err, "undefined symbol: %s", name);
        dl_err_set = 1;
    }
    return p;
}

static int g_dlclose(void *h) { return (h == GLOBAL_HANDLE || is_our_module(h)) ? 0 : dlclose(h); }

static char *g_dlerror(void) {
    if (dl_err_set) {
        dl_err_set = 0;
        return dl_err;
    }
    return dlerror();
}

static int g_dladdr(const void *addr, Dl_info *info) {
    utl_module *m = utl_module_for_addr((uintptr_t)addr);
    if (!m) return dladdr(addr, info);
    memset(info, 0, sizeof *info);
    info->dli_fname = m->path;
    info->dli_fbase = (void *)m->map_start;
    return 1;
}

/* glibc struct dl_phdr_info == bionic's on arm64 (addr, name, phdr, phnum, adds, subs, tls_modid, tls_data). */
static int g_dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *data) {
    for (utl_module *m = utl_modules(); m; m = m->next) {
        struct dl_phdr_info info;
        memset(&info, 0, sizeof info);
        info.dlpi_addr = m->bias;
        info.dlpi_name = m->path;
        info.dlpi_phdr = m->phdr;
        info.dlpi_phnum = (ElfW(Half))m->phnum;
        info.dlpi_adds = (unsigned long long)utl_load_count();
        info.dlpi_subs = 0;
        int r = cb(&info, sizeof info, data);
        if (r) return r;
    }
    return dl_iterate_phdr(cb, data);
}

/* glibc 2.35 struct dl_find_object (aarch64): flags, map_start, map_end, link_map, eh_frame, reserved[7]. */
typedef struct {
    unsigned long long dlfo_flags;
    void *dlfo_map_start, *dlfo_map_end;
    void *dlfo_link_map;
    void *dlfo_eh_frame;
    unsigned long long reserved[7];
} glibc_dl_find_object;

typedef struct {
    uintptr_t pc;
    glibc_dl_find_object *out;
    int found;
} find_ctx;

static int find_bionic_cb(struct dl_phdr_info *info, size_t size, void *data) {
    find_ctx *c = data;
    const void *eh = NULL;
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type == PT_LOAD) {
            if (info->dlpi_addr + p->p_vaddr < lo) lo = info->dlpi_addr + p->p_vaddr;
            if (info->dlpi_addr + p->p_vaddr + p->p_memsz > hi) hi = info->dlpi_addr + p->p_vaddr + p->p_memsz;
        } else if (p->p_type == PT_GNU_EH_FRAME) {
            eh = (const void *)(info->dlpi_addr + p->p_vaddr);
        }
    }
    if (c->pc >= lo && c->pc < hi && eh) {
        c->out->dlfo_map_start = (void *)lo;
        c->out->dlfo_map_end = (void *)hi;
        c->out->dlfo_eh_frame = (void *)eh;
        c->found = 1;
        return 1;
    }
    return 0;
}

static int g_dl_find_object(void *pc, glibc_dl_find_object *out) {
    utl_module *m = utl_module_for_addr((uintptr_t)pc);
    memset(out, 0, sizeof *out);
    if (m && m->eh_frame_hdr) {
        out->dlfo_map_start = (void *)m->map_start;
        out->dlfo_map_end = (void *)m->map_end;
        out->dlfo_link_map = m;
        out->dlfo_eh_frame = (void *)m->eh_frame_hdr;
        return 0;
    }
    find_ctx c = { (uintptr_t)pc, out, 0 };
    dl_iterate_phdr(find_bionic_cb, &c);
    return c.found ? 0 : -1;
}

/* ------------------------------------------------------------------------------------------ program entry */

/* __libc_start_main: called by a loaded executable's _start (utl_run_exe below). Runs main, then returns to
   the caller of utl_run_exe through a longjmp. */
static __thread jmp_buf *exe_return;
static __thread int exe_status;

static int g_libc_start_main(int (*main)(int, char **, char **), int argc, char **argv, void (*init)(void),
                             void (*fini)(void), void (*rtld_fini)(void), void *stack_end) {
    extern char **environ;
    utl_log("__libc_start_main: running main (argc %d, argv[0] %s)", argc, argv[0]);
    exe_status = main(argc, argv, environ);
    utl_log("main returned %d", exe_status);
    if (exe_return) longjmp(*exe_return, 1);
    exit(exe_status);
}

static void g_exit(int status) {
    utl_log("exit(%d)", status);
    fflush(NULL);
    if (exe_return) {
        exe_status = status;
        longjmp(*exe_return, 1);
    }
    exit(status);
}

__asm__(
    ".text\n"
    ".global utl_enter_start\n"
    ".type utl_enter_start, %function\n"
    /* void utl_enter_start(void *entry, void *sp): x0 = 0 (no rtld fini), jump to _start with its stack. */
    "utl_enter_start:\n"
    "    mov x16, x0\n"
    "    mov sp, x1\n"
    "    mov x0, xzr\n"
    "    mov x29, xzr\n"
    "    mov x30, xzr\n"
    "    br x16\n");
extern void utl_enter_start(void *entry, void *sp) __attribute__((noreturn));

int utl_run_exe(utl_module *exe, int argc, char **argv);

int utl_run_exe(utl_module *exe, int argc, char **argv) {
    extern char **environ;
    const ElfW(Ehdr) *eh = (const ElfW(Ehdr) *)exe->map_start;
    void *entry = (void *)(exe->bias + eh->e_entry);
    int nenv = 0;
    while (environ[nenv]) nenv++;
    /* _start's initial stack (argc, argv..., NULL, envp..., NULL, auxv AT_NULL), built on *this thread's own
       stack* below our frame: the program keeps running on the thread's real stack, which ART (once SDL attaches
       the thread for JNI) and bionic know about. A separate malloc'd stack confuses ART's fault handling. */
    const size_t frame = ((size_t)(argc + nenv + 8) * sizeof(uintptr_t) + 64 + 15) & ~(size_t)15;
    uint8_t *area = alloca(frame + 1024);
    uintptr_t *sp = (uintptr_t *)(((uintptr_t)area + 15) & ~(uintptr_t)15);
    uintptr_t *w = sp;
    *w++ = (uintptr_t)argc;
    for (int i = 0; i < argc; i++) *w++ = (uintptr_t)argv[i];
    *w++ = 0;
    for (int i = 0; i < nenv; i++) *w++ = (uintptr_t)environ[i];
    *w++ = 0;
    *w++ = AT_NULL;
    *w++ = 0;

    snprintf(exe_path, sizeof exe_path, "%s", exe->path);
    jmp_buf back;
    exe_return = &back;
    if (setjmp(back) == 0) utl_enter_start(entry, sp);
    exe_return = NULL;
    return exe_status;
}

/* ------------------------------------------------------------------------------------------ SDL interposer */

/* UT's Linux ut-bin pins SDL's video driver to "x11,wayland": on Android SDL the driver is "android". */
static int g_SDL_SetHint(const char *name, const char *value) {
    static int (*real)(const char *, const char *);
    if (!real) real = (int (*)(const char *, const char *))utl_external_lookup("SDL_SetHint");
    if (!real) return 0;
    if (name && value && strcmp(name, "SDL_VIDEO_DRIVER") == 0 && (strstr(value, "x11") || strstr(value, "wayland"))) {
        utl_log("SDL_SetHint(%s, %s) -> android", name, value);
        value = "android";
    }
    return real(name, value);
}

/* Android SDL opens *relative* paths from the APK's assets: make the game's relative paths absolute. */
static const char *abs_path(const char *p, char *buf, size_t n) {
    if (!p || p[0] == '/') return p;
    char cwd[512];
    if (!getcwd(cwd, sizeof cwd)) return p;
    snprintf(buf, n, "%s/%s", cwd, p);
    return buf;
}

/* Unreal 227's XOpenGLDrv (OpenGLVersion=ES) writes GLSL "#version 310 es" with GL_OES_shader_io_blocks and uses
   gl_ClipDistance (redeclared); Adreno's GLES 3.2 has I/O blocks in core 3.20 and clip distances via
   GL_EXT_clip_cull_distance, without the redeclaration.
   Rewrite each shader's header on its way to the driver. */
typedef unsigned int GLuint_;
typedef int GLsizei_;
typedef int GLint_;
static void (*real_glShaderSource)(GLuint_, GLsizei_, const char *const *, const GLint_ *);
static void g_glShaderSource(GLuint_ shader, GLsizei_ count, const char *const *strings, const GLint_ *lengths) {
    size_t total = 0;
    for (GLsizei_ i = 0; i < count; i++) total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    char *src = malloc(total + 256);
    if (!src) {
        real_glShaderSource(shader, count, strings, lengths);
        return;
    }
    size_t o = 0;
    for (GLsizei_ i = 0; i < count; i++) {
        size_t l = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(src + o, strings[i], l);
        o += l;
    }
    src[o] = 0;
    char *v = strstr(src, "#version 310 es");
    if (v) v[10] = '2'; /* "#version 310 es" -> 320 es */
    char *ext = strstr(src, "#extension GL_OES_shader_io_blocks");
    if (ext) {
        char *eol = strchr(ext, '\n');
        memset(ext, ' ', eol ? (size_t)(eol - ext) : strlen(ext));
        ext[0] = '/', ext[1] = '/';
    }
    /* Redeclaring the built-in ("out float gl_ClipDistance[N];") is desktop GLSL only: drop it. */
    for (char *r = strstr(src, "out float gl_ClipDistance"); r; r = strstr(r, "out float gl_ClipDistance")) {
        r[0] = '/';
        r[1] = '/';
        r += 2;
    }
    /* Spike: indexing gl_ClipDistance with a variable needs a sized redeclaration on GLES, which Adreno rejects;
       send the writes to a plain array instead (no user clip planes) to see whether that is the link failure. */
    if (strstr(src, "gl_ClipDistance") && strstr(src, "gl_Position") && !getenv("UTL_KEEP_CLIPDISTANCE")) {
        char *line = strstr(src, "#extension GL_EXT_clip_cull_distance");
        if (!line) line = strstr(src, "#version");
        char *eol = line ? strchr(line, '\n') : NULL;
        if (eol) {
            static const char add[] = "#define gl_ClipDistance utl_ClipDistance\nhighp float utl_ClipDistance[8];\n";
            size_t at = (size_t)(eol + 1 - src);
            memmove(src + at + sizeof add - 1, src + at, strlen(src + at) + 1);
            memcpy(src + at, add, sizeof add - 1);
        }
    }
    if (strstr(src, "gl_ClipDistance") && !strstr(src, "GL_EXT_clip_cull_distance")) {
        char *line = strstr(src, "#version");
        char *eol = line ? strchr(line, '\n') : NULL;
        if (eol) {
            static const char add[] = "#extension GL_EXT_clip_cull_distance : enable\n";
            size_t at = (size_t)(eol + 1 - src);
            memmove(src + at + sizeof add - 1, src + at, strlen(src + at) + 1);
            memcpy(src + at, add, sizeof add - 1);
        }
    }
    if (getenv("UTL_SHADER_DUMP")) { /* debug: the source the driver gets, by shader id */
        char path[600];
        snprintf(path, sizeof path, "%s/shader-%u.glsl", getenv("UTL_SHADER_DUMP"), shader);
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(src, f);
            fclose(f);
        }
    }
    const char *one = src;
    real_glShaderSource(shader, 1, &one, NULL);
    free(src);
}
/* Logs why a program failed to link (XOpenGL only says "Failed linking"). */
static void (*real_glLinkProgram)(GLuint_);
static void (*gl_GetProgramiv)(GLuint_, unsigned int, GLint_ *);
static void (*gl_GetProgramInfoLog)(GLuint_, GLsizei_, GLsizei_ *, char *);
static void g_glLinkProgram(GLuint_ prog) {
    real_glLinkProgram(prog);
    GLint_ ok = 1;
    if (gl_GetProgramiv) gl_GetProgramiv(prog, 0x8B82 /* GL_LINK_STATUS */, &ok);
    if (!ok && gl_GetProgramInfoLog) {
        char log[2048];
        GLsizei_ n = 0;
        gl_GetProgramInfoLog(prog, sizeof log - 1, &n, log);
        log[n > 0 ? n : 0] = 0;
        utl_log("glLinkProgram(%u) failed: %s", prog, log);
    }
}

static void *g_SDL_GL_GetProcAddress(const char *name) {
    static void *(*real)(const char *);
    if (!real) real = (void *(*)(const char *))utl_external_lookup("SDL_GL_GetProcAddress");
    void *f = real(name);
    if (f && name && strcmp(name, "glShaderSource") == 0) {
        real_glShaderSource = (void (*)(GLuint_, GLsizei_, const char *const *, const GLint_ *))f;
        return (void *)g_glShaderSource;
    }
    if (f && name && strcmp(name, "glLinkProgram") == 0) {
        real_glLinkProgram = (void (*)(GLuint_))f;
        gl_GetProgramiv = (void (*)(GLuint_, unsigned int, GLint_ *))real("glGetProgramiv");
        gl_GetProgramInfoLog = (void (*)(GLuint_, GLsizei_, GLsizei_ *, char *))real("glGetProgramInfoLog");
        return (void *)g_glLinkProgram;
    }
    return f;
}

/* SDL re-chooses the EGL config from the current GL attributes whenever Android recreates the window surface
   (onNativeSurfaceChanged after a resume). Unreal 227's XOpenGLDrv sets its own framebuffer attributes after
   SDLDrv's window (and the surface its context uses) exists, so the surface recreated on resume had a
   different config than the context: EGL_BAD_MATCH on every eglMakeCurrent, black screen. Once the GL window
   exists, the attributes that select the framebuffer config are kept as they were (context attributes still pass). */
static bool g_SDL_GL_SetAttribute(int attr, int value) {
    static bool (*real)(int, int);
    if (!real) real = (bool (*)(int, int))utl_external_lookup("SDL_GL_SetAttribute");
    const bool config_attr = (attr >= 0 && attr <= 15) || attr == 22 /* FRAMEBUFFER_SRGB_CAPABLE */ ||
                             attr == 26 /* FLOATBUFFERS */;
    /* "The window exists": SDLDrv creates it with SDL_CreateWindowWithProperties, so ask SDL. */
    static void **(*get_windows)(int *);
    static void (*sdl_free)(void *);
    if (!get_windows) get_windows = (void **(*)(int *))utl_external_lookup("SDL_GetWindows");
    if (!sdl_free) sdl_free = (void (*)(void *))utl_external_lookup("SDL_free");
    int n = 0;
    void **list = config_attr && get_windows ? get_windows(&n) : NULL;
    if (list && sdl_free) sdl_free(list);
    if (n > 0) {
        utl_log("SDL_GL_SetAttribute(%d, %d) ignored: the window's framebuffer config is fixed", attr, value);
        return true;
    }
    return real(attr, value);
}

/* Android SDL allows one window. Unreal 227's XOpenGLDrv creates its own while SDLDrv's is open: when Android
   refuses a second window, hand back the existing one, and ignore the matching destroy. */
static void *alias_window;
static int alias_count;
/* OpenGL (ES) renderers present with SDL_GL_SwapWindow: count those frames like vkQueuePresentKHR above (look-axis
   scaling, gyro draining once per frame, FPS overlay). Unreal 227's XOpenGLDrv. */
static bool g_SDL_GL_SwapWindow(void *win) {
    static bool (*real)(void *);
    if (!real) real = (bool (*)(void *))utl_external_lookup("SDL_GL_SwapWindow");
    bool ok = real(win);
    if (utl_frames_shared) __atomic_fetch_add(utl_frames_shared, 1, __ATOMIC_RELAXED);
    if (utl_frames_file) __atomic_fetch_add(utl_frames_file, 1, __ATOMIC_RELAXED);
    return ok;
}

static void *g_SDL_CreateWindow(const char *title, int w, int h, uint64_t flags) {
    static void *(*real)(const char *, int, int, uint64_t);
    static void **(*get_windows)(int *);
    static void (*sdl_free)(void *);
    if (!real) real = (void *(*)(const char *, int, int, uint64_t))utl_external_lookup("SDL_CreateWindow");
    void *win = real(title, w, h, flags);
    if (win) {
        return win;
    }
    if (!get_windows) get_windows = (void **(*)(int *))utl_external_lookup("SDL_GetWindows");
    if (!sdl_free) sdl_free = (void (*)(void *))utl_external_lookup("SDL_free");
    int n = 0;
    void **list = get_windows ? get_windows(&n) : NULL;
    if (list && n > 0) {
        win = list[0];
        alias_window = win;
        alias_count++;
        utl_log("SDL_CreateWindow(\"%s\", %dx%d, flags 0x%llx): Android has one window, reusing it", title ? title : "",
                w, h, (unsigned long long)flags);
    }
    if (list && sdl_free) sdl_free(list);
    return win;
}
static void g_SDL_DestroyWindow(void *win) {
    static void (*real)(void *);
    if (win && win == alias_window && alias_count > 0) {
        alias_count--;
        return;
    }
    if (!real) real = (void (*)(void *))utl_external_lookup("SDL_DestroyWindow");
    real(win);
}

/* Android SDL allows one window: ut-bin's splash window would block the game's. Skip the splash (refuse its
   image) and swallow the resulting "Splash... init error" message box. */
static void *g_SDL_LoadBMP(const char *file) {
    static void *(*real)(const char *);
    static int (*set_error)(const char *, ...);
    char buf[1024];
    if (file && strstr(file, "Splash")) {
        utl_log("splash skipped on Android (one window only): %s", file);
        if (!set_error) set_error = (int (*)(const char *, ...))utl_external_lookup("SDL_SetError");
        if (set_error) set_error("splash skipped");
        return NULL;
    }
    if (!real) real = (void *(*)(const char *))utl_external_lookup("SDL_LoadBMP");
    return real ? real(abs_path(file, buf, sizeof buf)) : NULL;
}

static void *g_TTF_OpenFont(const char *file, float ptsize) {
    static void *(*real)(const char *, float);
    char buf[1024];
    if (!real) real = (void *(*)(const char *, float))utl_external_lookup("TTF_OpenFont");
    return real ? real(abs_path(file, buf, sizeof buf), ptsize) : NULL;
}

static int g_SDL_ShowMessageBox(const void *data, int *buttonid) {
    static int (*real)(const void *, int *);
    const char *title = data ? ((const char *const *)data)[2] : NULL; /* flags, window, title, message, ... */
    if (title && strncmp(title, "Splash", 6) == 0) {
        utl_log("message box suppressed: %s", title);
        if (buttonid) *buttonid = -1;
        return 1;
    }
    if (!real) real = (int (*)(const void *, int *))utl_external_lookup("SDL_ShowMessageBox");
    return real ? real(data, buttonid) : 0;
}

static int g_SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, void *window) {
    static int (*real)(uint32_t, const char *, const char *, void *);
    if (title && strncmp(title, "Splash", 6) == 0) {
        utl_log("message box suppressed: %s: %s", title, message ? message : "");
        return 1;
    }
    if (!real) real = (int (*)(uint32_t, const char *, const char *, void *))utl_external_lookup("SDL_ShowSimpleMessageBox");
    return real ? real(flags, title, message, window) : 0;
}

/* ------------------------------------------------------------------------------------------ app lifecycle */

/* OpenAL plays through its own OpenSL ES device, which SDL's background pause doesn't touch: track the
   devices ALAudio opens and pause/resume them (ALC_SOFT_pause_device) on SDL's background/foreground events. */
static void *al_devices[8];

static void *g_alcOpenDevice(const char *name) {
    static void *(*real)(const char *);
    if (!real) real = (void *(*)(const char *))utl_external_lookup("alcOpenDevice");
    void *d = real ? real(name) : NULL;
    for (int i = 0; d && i < 8; i++)
        if (!al_devices[i]) { al_devices[i] = d; break; }
    return d;
}

static int g_alcCloseDevice(void *d) {
    static int (*real)(void *);
    if (!real) real = (int (*)(void *))utl_external_lookup("alcCloseDevice");
    for (int i = 0; i < 8; i++)
        if (al_devices[i] == d) al_devices[i] = NULL;
    return real ? real(d) : 0;
}

static void al_pause_all(int pause) {
    static void *(*get_proc)(void *, const char *);
    if (!get_proc) get_proc = (void *(*)(void *, const char *))utl_external_lookup("alcGetProcAddress");
    for (int i = 0; get_proc && i < 8; i++) {
        if (!al_devices[i]) continue;
        void (*fn)(void *) = (void (*)(void *))get_proc(al_devices[i], pause ? "alcDevicePauseSOFT" : "alcDeviceResumeSOFT");
        if (fn) fn(al_devices[i]);
    }
    utl_log("audio %s", pause ? "paused (background)" : "resumed (foreground)");
}

/* SDL3 lifecycle events (WILL/DID_ENTER_BACKGROUND 0x103/0x104, WILL/DID_ENTER_FOREGROUND 0x105/0x106) have to
   be caught with an event watch: SDL queues them and then blocks the game thread inside its next event pump,
   before SDL_PollEvent could return them. The watch runs synchronously as they are queued, and it is our own
   (bionic) code, so it is fine on any thread. */
static bool SDLCALL lifecycle_watch(void *userdata, void *ev) {
    const uint32_t type = *(const uint32_t *)ev;
    if (type == 0x103) al_pause_all(1);
    else if (type == 0x105) al_pause_all(0);
    return true;
}

/* PadUI's Handheld Settings talk to us through the clipboard: a copied "utandroid:<command>" never reaches the real
   clipboard; its reply is what the next paste returns. */
__attribute__((weak)) const char *utl_handheld_command(const char *cmd); /* main_android.c (not in utl-spike) */
static char *handheld_reply;

static int g_SDL_SetClipboardText(const char *text) {
    static int (*real)(const char *);
    if (text && utl_handheld_command && strncmp(text, "utandroid:", 10) == 0) {
        free(handheld_reply);
        handheld_reply = strdup(utl_handheld_command(text + 10));
        return 1;
    }
    if (!real) real = (int (*)(const char *))utl_external_lookup("SDL_SetClipboardText");
    return real ? real(text) : 0;
}

/* Unreal 227 keeps the clipboard in its own Core (appClipboardCopy/appClipboardPaste, wchar_t = UTF-32) instead of
   SDL's, so PadUI's "utandroid:" channel is caught here: commands are run, and a reply is stored with the real
   appClipboardCopy, so the game's next paste returns it. UT 469 goes through SDL_SetClipboardText above. */
static void g_appClipboardCopy(const wchar_t *w) {
    static void (*real)(const wchar_t *);
    if (!real) real = (void (*)(const wchar_t *))utl_sym(NULL, "_Z16appClipboardCopyPKw");
    if (w && utl_handheld_command) {
        static const wchar_t prefix[] = L"utandroid:";
        if (wcsncmp(w, prefix, 10) == 0) {
            char cmd[512];
            size_t n = 0;
            for (const wchar_t *p = w + 10; *p && n < sizeof cmd - 1; p++) cmd[n++] = *p < 128 ? (char)*p : '?';
            cmd[n] = 0;
            const char *reply = utl_handheld_command(cmd);
            if (reply && *reply && real) {
                wchar_t wr[512];
                size_t i = 0;
                for (; reply[i] && i < 511; i++) wr[i] = (unsigned char)reply[i];
                wr[i] = 0;
                real(wr);
            }
            return;
        }
    }
    if (real) real(w);
}

void *utl_shim_preempt(const char *name) {
    if (strcmp(name, "_Z16appClipboardCopyPKw") == 0) return (void *)g_appClipboardCopy;
    return NULL;
}

static char *g_SDL_GetClipboardText(void) {
    static char *(*real)(void);
    static char *(*sdl_strdup)(const char *);
    if (handheld_reply) {
        if (!sdl_strdup) sdl_strdup = (char *(*)(const char *))utl_external_lookup("SDL_strdup");
        char *r = sdl_strdup ? sdl_strdup(handheld_reply) : NULL; /* the engine frees it with SDL_free */
        free(handheld_reply);
        handheld_reply = NULL;
        if (r) return r;
    }
    if (!real) real = (char *(*)(void))utl_external_lookup("SDL_GetClipboardText");
    return real ? real() : NULL;
}

/* Package reads: the engine reads through stdio in small pieces, and bionic's FILE buffer is only 1 KB (glibc uses
   the file system block size). Every refill is a syscall, and on the SD card a round trip through Android's FUSE
   daemon. A 64 KB buffer for read-only files cuts that ~64x (bionic still seeks within the buffer). */
static FILE *g_fopen(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    if (f && mode && mode[0] == 'r' && !strchr(mode, '+')) setvbuf(f, NULL, _IOFBF, 64 * 1024);
    return f;
}

static int g_SDL_PollEvent(void *ev) {
    static int (*real)(void *);
    static int watching;
    if (!real) real = (int (*)(void *))utl_external_lookup("SDL_PollEvent");
    if (!watching) {
        bool (*add_watch)(bool (SDLCALL *)(void *, void *), void *) =
            (bool (*)(bool (SDLCALL *)(void *, void *), void *))utl_external_lookup("SDL_AddEventWatch");
        if (add_watch && add_watch(lifecycle_watch, NULL)) utl_log("lifecycle event watch installed");
        watching = 1;
    }
    return real ? real(ev) : 0;
}

/* Only our virtual pad (SDL_utandroid.c, native-pad mode) is shown to UT: the real pad stays hidden so SDLDrv
   reads the shaped layout, not Android SDL's raw one. Falls back to the full list if ours isn't there. */
static uint32_t *g_SDL_GetJoysticks(int *count) {
    static uint32_t *(*real)(int *);
    static const char *(*name_for)(uint32_t);
    int n = 0, kept = 0;
    if (!real) real = (uint32_t *(*)(int *))utl_external_lookup("SDL_GetJoysticks");
    if (!name_for) name_for = (const char *(*)(uint32_t))utl_external_lookup("SDL_GetJoystickNameForID");
    uint32_t *ids = real ? real(&n) : NULL;
    for (int i = 0; ids && name_for && i < n; i++) {
        const char *nm = name_for(ids[i]);
        if (nm && strcmp(nm, "UTAndroid controller") == 0) ids[kept++] = ids[i];
    }
    if (ids && kept) {
        ids[kept] = 0;
        n = kept;
    }
    if (count) *count = n;
    return ids;
}

/* ------------------------------------------------------------------------------------------ table */

typedef struct {
    const char *name;
    void *fn;
} shim_entry;

#define S(n, f) { n, (void *)(f) }
static const shim_entry table[] = {
    S("__errno_location", g_errno_location), S("__h_errno_location", g_h_errno_location),
    S("__ctype_b_loc", g_ctype_b_loc), S("__ctype_tolower_loc", g_ctype_tolower_loc),
    S("__ctype_toupper_loc", g_ctype_toupper_loc),
    S("__newlocale", g_newlocale), S("newlocale", g_newlocale), S("__duplocale", g_duplocale),
    S("duplocale", g_duplocale), S("__freelocale", g_freelocale), S("freelocale", g_freelocale),
    S("__uselocale", g_uselocale), S("uselocale", g_uselocale), S("setlocale", g_setlocale),
    S("__nl_langinfo_l", g_nl_langinfo_l), S("nl_langinfo_l", g_nl_langinfo_l), S("nl_langinfo", g_nl_langinfo),
    S("__strcoll_l", g_strcoll_l), S("__strxfrm_l", g_strxfrm_l), S("__wcscoll_l", g_wcscoll_l),
    S("__wcsxfrm_l", g_wcsxfrm_l), S("__towlower_l", g_towlower_l), S("__towupper_l", g_towupper_l),
    S("__wctype_l", g_wctype_l), S("__iswctype_l", g_iswctype_l), S("__strftime_l", g_strftime_l),
    S("__wcsftime_l", g_wcsftime_l), S("__strtod_l", g_strtod_l), S("__strtof_l", g_strtof_l),
    S("strtold_l", g_strtold_l), S("__strtold_l", g_strtold_l),
    S("__assert_fail", g_assert_fail), S("gnu_get_libc_version", g_gnu_get_libc_version),
    S("secure_getenv", g_secure_getenv), S("__secure_getenv", g_secure_getenv), S("__getauxval", g_getauxval), S("fesetenv", g_fesetenv), S("feupdateenv", g_feupdateenv), S("realpath", g_realpath),
    S("__register_atfork", g_register_atfork), S("__sched_cpucount", g_sched_cpucount),
    S("malloc_trim", g_malloc_trim), S("strerror_r", g_strerror_r), S("__xpg_strerror_r", g_xpg_strerror_r),
    S("basename", g_basename), S("__xpg_basename", basename), S("fcntl64", g_fcntl64),
    S("__isoc23_strtol", g_isoc_strtol), S("__isoc23_strtoul", g_isoc_strtoul),
    S("__isoc23_strtoll", g_isoc_strtoll), S("__isoc23_strtoull", g_isoc_strtoull),
    S("__isoc23_wcstol", g_isoc_wcstol), S("__isoc23_wcstoul", g_isoc_wcstoul),
    S("__isoc23_sscanf", g_isoc_sscanf), S("__isoc99_sscanf", g_isoc_sscanf),
    S("__isoc23_vsscanf", g_isoc_vsscanf), S("__isoc99_vsscanf", g_isoc_vsscanf),
    S("__isoc23_fscanf", g_isoc_fscanf), S("__isoc99_fscanf", g_isoc_fscanf),
    S("__isoc23_vfscanf", g_isoc_vfscanf), S("__isoc99_vfscanf", g_isoc_vfscanf),
    S("__libc_single_threaded", &g_single_threaded),
    S("program_invocation_name", &g_progname), S("program_invocation_short_name", &g_progname),
    S("sysconf", g_sysconf), S("__sysconf", g_sysconf),
    S("sigaction", g_sigaction), S("__sigaction", g_sigaction), S("signal", g_signal), S("sigaltstack", g_sigaltstack), S("bsd_signal", g_signal),
    S("sysv_signal", g_signal), S("__sysv_signal", g_signal),
    S("getaddrinfo", g_getaddrinfo), S("freeaddrinfo", g_freeaddrinfo),
    S("pthread_create", g_pthread_create),
    S("dlopen", g_dlopen), S("dlsym", g_dlsym), S("dlclose", g_dlclose), S("dlerror", g_dlerror),
    S("dladdr", g_dladdr), S("dl_iterate_phdr", g_dl_iterate_phdr), S("_dl_find_object", g_dl_find_object),
    S("__libc_start_main", g_libc_start_main), S("exit", g_exit), S("SDL_SetHint", g_SDL_SetHint), S("SDL_CreateWindow", g_SDL_CreateWindow), S("SDL_GL_SetAttribute", g_SDL_GL_SetAttribute), S("SDL_GL_SwapWindow", g_SDL_GL_SwapWindow), S("SDL_GL_GetProcAddress", g_SDL_GL_GetProcAddress), S("SDL_DestroyWindow", g_SDL_DestroyWindow), S("SDL_Vulkan_CreateSurface", g_SDL_Vulkan_CreateSurface),
    S("SDL_PollEvent", g_SDL_PollEvent), S("SDL_SetClipboardText", g_SDL_SetClipboardText),
    S("SDL_GetClipboardText", g_SDL_GetClipboardText), S("fopen", g_fopen), S("fopen64", g_fopen), S("SDL_GetJoysticks", g_SDL_GetJoysticks), S("alcOpenDevice", g_alcOpenDevice), S("alcCloseDevice", g_alcCloseDevice), S("SDL_LoadBMP", g_SDL_LoadBMP), S("SDL_ShowMessageBox", g_SDL_ShowMessageBox), S("SDL_ShowSimpleMessageBox", g_SDL_ShowSimpleMessageBox), S("TTF_OpenFont", g_TTF_OpenFont),
    S("iconv_open", g_iconv_open), S("iconv", g_iconv), S("iconv_close", g_iconv_close), S("bcmp", g_bcmp), S("__fprintf_chk", g_fprintf_chk),
    /* returns twice: must be bionic's sigsetjmp itself, not a wrapper (glibc's 312-byte jmp_buf > bionic's 256) */
    S("__sigsetjmp", sigsetjmp), S("__cxa_thread_atexit_impl", g_cxa_thread_atexit_impl), S("readlink", g_readlink),
    S("gettext", g_gettext), S("dgettext", g_dgettext), S("dcgettext", g_dcgettext),
    S("bindtextdomain", g_bindtextdomain), S("bind_textdomain_codeset", g_bind_textdomain_codeset),
    S("textdomain", g_textdomain), S("pthread_setcanceltype", g_pthread_setcanceltype),
    S("pthread_setcancelstate", g_pthread_setcancelstate),
    S("posix_spawn_file_actions_addchdir_np", g_posix_spawn_addchdir),
    { NULL, NULL },
};

void *utl_shim_lookup(const char *name) {
    static int ready;
    if (!ready) {
        ready = 1;
        init_ctype();
        init_locale();
    }
    for (const shim_entry *e = table; e->name; e++)
        if (strcmp(e->name, name) == 0) return e->fn;
    return NULL;
}

void utl_shim_set_args(int argc, char **argv) {
    if (argc > 0) g_progname = argv[0];
}
