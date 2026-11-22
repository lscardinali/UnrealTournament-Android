/*
 * utloader spike (M0/M1 of docs/native-port-plan.md), a plain bionic executable run over adb:
 *
 *   utl-spike <glibc-lib-dir> <game-System-dir> <test-lib-dir>              M0 checks
 *   utl-spike <glibc-lib-dir> <game-System-dir> <test-lib-dir> ucc [args]   + run ucc-bin-arm64 [args]
 *
 * M0: load GNU libgcc_s/libstdc++ and UT's Core.so into this bionic process; run their constructors; Core's own
 * initial-exec TLS (UnGuardBlockTLS::GetBackTrace) and a cross-module TPREL64 on two threads; TLSDESC; C++
 * exceptions thrown in libstdc++ and caught in another custom-loaded module.
 */
#define _GNU_SOURCE
#include "utl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

int utl_run_exe(utl_module *exe, int argc, char **argv);

static int failures;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (cond) utl_log("PASS: " __VA_ARGS__);                           \
        else { utl_log("FAIL: " __VA_ARGS__); failures++; }                \
    } while (0)

typedef void *(*addr_fn)(void);
typedef void (*write_fn)(int, int);
typedef int (*read_fn)(int);
typedef int (*int_fn)(void);
typedef void (*backtrace_fn)(wchar_t *, int);

static utl_module *core, *testlib;

typedef struct {
    int value;
    int seen;
    void *addr, *expected;
    int bt_ok;
} tls_job;

static void *tls_thread(void *p) {
    tls_job *j = p;
    addr_fn addr = (addr_fn)utl_sym(testlib, "tt_tls_addr");
    write_fn wr = (write_fn)utl_sym(testlib, "tt_tls_write");
    read_fn rd = (read_fn)utl_sym(testlib, "tt_tls_read");
    backtrace_fn bt = (backtrace_fn)utl_sym(core, "_ZN15UnGuardBlockTLS12GetBackTraceEPwi");

    /* Core's own code reading its TLS through its own GOT entry. */
    if (bt) {
        wchar_t buf[256] = { 0 };
        bt(buf, 256);
        j->bt_ok = 1;
    }
    j->addr = addr();
    j->expected = (char *)utl_tls_block() + core->tls_block_offset + (uintptr_t)utl_sym(core, "_ZN15UnGuardBlockTLS4DataE") -
                  core->bias; /* st_value of the TLS symbol (utl_sym adds the bias) */
    wr(j->value, 32 * 1024);
    usleep(50 * 1000); /* overlap with the other thread */
    j->seen = rd(32 * 1024);
    return NULL;
}

typedef struct {
    int argc;
    char **argv;
    const char *glibc, *game, *testdir;
} spike_args;

static void *game_thread(void *p) {
    spike_args *a = p;
    utl_module *gcc = utl_load("libgcc_s.so.1");
    utl_module *cxx = utl_load("libstdc++.so.6");
    core = utl_load("Core.so");
    testlib = utl_load("libthrowtest.so");
    CHECK(gcc && cxx && core && testlib, "load libgcc_s, libstdc++, Core.so, libthrowtest.so");
    if (!(gcc && cxx && core && testlib)) return NULL;
    utl_run_inits(gcc);
    utl_run_inits(cxx);
    utl_run_inits(core);
    utl_run_inits(testlib);
    CHECK(1, "constructors of libgcc_s, libstdc++, Core, testlib ran");

    /* TLS on two threads at once: each sees its own value, at the address our layout says. */
    tls_job j1 = { 0x11111111 }, j2 = { 0x22222222 };
    pthread_t t1, t2;
    utl_thread_create(&t1, NULL, tls_thread, &j1);
    utl_thread_create(&t2, NULL, tls_thread, &j2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(j1.bt_ok && j2.bt_ok, "Core's UnGuardBlockTLS::GetBackTrace (Core's own IE TLS) ran on two threads");
    CHECK(j1.addr == j1.expected && j2.addr == j2.expected && j1.addr != j2.addr,
          "cross-module TPREL64 resolves to each thread's carved block (%p / %p)", j1.addr, j2.addr);
    CHECK(j1.seen == j1.value && j2.seen == j2.value, "TLS isolation between threads (%#x, %#x)", j1.seen, j2.seen);

    int_fn bump = (int_fn)utl_sym(testlib, "tt_tlsdesc_bump");
    int b1 = bump(), b2 = bump();
    CHECK(b1 == 101 && b2 == 102, "TLSDESC thread_local with initial value (%d, %d)", b1, b2);

    int_fn thr = (int_fn)utl_sym(testlib, "tt_throw");
    int r = thr();
    CHECK(r == 42, "exception thrown in libstdc++, caught in testlib (%d)", r);
    int_fn deep = (int_fn)utl_sym(testlib, "tt_throw_deep");
    r = deep();
    CHECK(r == 43, "own exception unwound through 21 frames (%d)", r);

    utl_log("M0 result: %d failure(s)", failures);

    if (a->argc > 0 && strcmp(a->argv[0], "ucc") == 0) {
        utl_module *exe = utl_load("ucc-bin-arm64");
        CHECK(exe != NULL, "load ucc-bin-arm64 and its dependencies");
        if (exe) {
            for (utl_module *m = utl_modules(); m; m = m->next) utl_run_inits(m);
            char *argv[16] = { "ucc-bin-arm64" };
            int n = 1;
            for (int i = 1; i < a->argc && n < 15; i++) argv[n++] = a->argv[i];
            argv[n] = NULL;
            int status = utl_run_exe(exe, n, argv);
            utl_log("ucc exited with %d", status);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <glibc-lib-dir> <game-System-dir> <test-lib-dir> [ucc args...]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (utl_tls_init() != 0) return 1;
    utl_add_search_dir(argv[2]); /* game first: its own libs win */
    utl_add_search_dir(argv[1]);
    utl_add_search_dir(argv[3]);
    spike_args a = { argc - 4, argv + 4, argv[1], argv[2], argv[3] };
    pthread_t t;
    utl_thread_create(&t, NULL, game_thread, &a);
    pthread_join(t, NULL);
    utl_log("spike done: %d failure(s)", failures);
    fflush(NULL);
    /* Skip atexit/static destructors of the loaded glibc modules for now (shutdown order is a later item). */
    _exit(failures ? 1 : 0);
}
