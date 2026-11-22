/*
 * Static TLS for custom-loaded glibc modules in a bionic process ("stack carve").
 *
 * glibc-compiled code reaches initial-exec TLS as TP + (constant from a GOT slot), and TLSDESC with a static
 * resolver does the same. bionic can't grow static TLS after start-up, but on bionic every pthread_create'd
 * thread has its stack top a constant distance D below TP (measured: 848 bytes on the RP6 in a plain
 * executable). So we reserve a BLOCK at the top of every engine thread's stack, TOP_MARGIN below the stack top,
 * and hand every module a constant TP-relative offset into it:
 *
 *   [ ... stack grows down ... | BLOCK (module TLS) | TOP_MARGIN (thread entry frames) | D | TP ]
 *
 * Engine code must only run on threads made by utl_thread_create (our game thread and the engine's own
 * pthread_create via the shim); the game modules import no callbacks that bionic threads could call.
 */
#define _GNU_SOURCE
#include "utl.h"

#include <alloca.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK (256 * 1024)
#define TOP_MARGIN (32 * 1024)
#define MIN_STACK (1024 * 1024)

static intptr_t stack_gap;        /* D = TP - stack top, for pthread_create'd threads */
static intptr_t block_tp_offset;  /* block start - TP (negative) */
static size_t block_used;
static int tls_ready;

static __thread int is_utl_thread;

/* Live utl threads' blocks, to initialise TLS of modules loaded after they started. */
#define MAX_THREADS 128
static uint8_t *live_blocks[MAX_THREADS];
static pthread_mutex_t live_lock = PTHREAD_MUTEX_INITIALIZER;

__asm__(
    ".text\n"
    ".global utl_tlsdesc_return\n"
    ".type utl_tlsdesc_return, %function\n"
    "utl_tlsdesc_return:\n"
    "    ldr x0, [x0, #8]\n" /* the descriptor's argument is already the TP offset */
    "    ret\n");

static uintptr_t thread_pointer(void) { return (uintptr_t)__builtin_thread_pointer(); }

static void *probe(void *arg) {
    pthread_attr_t a;
    void *lo;
    size_t size;
    pthread_getattr_np(pthread_self(), &a);
    pthread_attr_getstack(&a, &lo, &size);
    pthread_attr_destroy(&a);
    *(intptr_t *)arg = (intptr_t)thread_pointer() - (intptr_t)((uintptr_t)lo + size);
    return NULL;
}

int utl_tls_init(void) {
    pthread_t t;
    intptr_t d1 = 0, d2 = 0;
    pthread_attr_t a;
    pthread_create(&t, NULL, probe, &d1);
    pthread_join(t, NULL);
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 4 * 1024 * 1024);
    pthread_create(&t, &a, probe, &d2);
    pthread_join(t, NULL);
    pthread_attr_destroy(&a);
    if (d1 != d2 || d1 < 0 || d1 > 64 * 1024) {
        utl_log("TLS: thread layout not constant (TP-stacktop %ld vs %ld): unsupported", (long)d1, (long)d2);
        return -1;
    }
    stack_gap = d1;
    /* Page-aligned offset so every module's alignment holds (TP itself is page-aligned on bionic). */
    block_tp_offset = -(intptr_t)(((size_t)stack_gap + TOP_MARGIN + BLOCK + 4095) & ~(size_t)4095);
    tls_ready = 1;
    utl_log("TLS: TP - stack top = %ld; block of %d KB at TP%ld", (long)stack_gap, BLOCK / 1024, (long)block_tp_offset);
    return 0;
}

intptr_t utl_tls_tp_offset(const utl_module *m) { return block_tp_offset + m->tls_block_offset; }

static void init_module_tls(uint8_t *block, const utl_module *m) {
    uint8_t *dst = block + m->tls_block_offset;
    memcpy(dst, m->tls_image, m->tls_filesz);
    memset(dst + m->tls_filesz, 0, m->tls_memsz - m->tls_filesz);
}

void utl_tls_add_module(utl_module *m) {
    size_t off = (block_used + m->tls_align - 1) & ~(m->tls_align - 1);
    if (off + m->tls_memsz > BLOCK) {
        utl_log("TLS: %s needs %zu bytes, block full (%zu used)", m->name, m->tls_memsz, block_used);
        abort();
    }
    m->tls_block_offset = (intptr_t)off;
    block_used = off + m->tls_memsz;
    utl_log("TLS: %s gets %zu bytes at block+%zu (TP%ld)", m->name, m->tls_memsz, off, (long)utl_tls_tp_offset(m));
    pthread_mutex_lock(&live_lock);
    for (int i = 0; i < MAX_THREADS; i++)
        if (live_blocks[i]) init_module_tls(live_blocks[i], m);
    pthread_mutex_unlock(&live_lock);
}

void *utl_tls_block(void) { return is_utl_thread ? (void *)(thread_pointer() + block_tp_offset) : NULL; }

typedef struct {
    void *(*fn)(void *);
    void *arg;
} start_info;

static void *trampoline(void *p) {
    start_info s = *(start_info *)p;
    free(p);
    uint8_t *block = (uint8_t *)(thread_pointer() + block_tp_offset);
    uint8_t *frame = (uint8_t *)__builtin_frame_address(0);
    if (frame < block + BLOCK) {
        utl_log("TLS: thread entry frame %p overlaps the block [%p,%p): raise TOP_MARGIN", frame, block, block + BLOCK);
        abort();
    }
    /* Claim everything from here down to the block's start, so normal stack use stays below it. */
    volatile uint8_t *claim = alloca((size_t)(frame - block) + 256);
    if ((uint8_t *)claim > block) {
        utl_log("TLS: carve failed (%p > %p)", claim, block);
        abort();
    }
    int slot = -1;
    pthread_mutex_lock(&live_lock);
    for (utl_module *m = utl_modules(); m; m = m->next)
        if (m->has_tls) init_module_tls(block, m);
    for (int i = 0; i < MAX_THREADS; i++)
        if (!live_blocks[i]) {
            live_blocks[i] = block;
            slot = i;
            break;
        }
    pthread_mutex_unlock(&live_lock);
    is_utl_thread = 1;

    void *ret = s.fn(s.arg);

    pthread_mutex_lock(&live_lock);
    if (slot >= 0) live_blocks[slot] = NULL;
    pthread_mutex_unlock(&live_lock);
    claim[0] = 0; /* keep the claim alive until the thread function returned */
    return ret;
}

int utl_thread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    pthread_attr_t a;
    size_t stack = 0;
    if (!tls_ready) return -1;
    if (attr) {
        memcpy(&a, attr, sizeof a); /* bionic attr (the shim passes bionic-initialised attrs through) */
        pthread_attr_getstacksize(&a, &stack);
    } else {
        pthread_attr_init(&a);
    }
    if (stack < MIN_STACK) stack = MIN_STACK;
    pthread_attr_setstacksize(&a, stack + BLOCK + TOP_MARGIN + 64 * 1024);
    start_info *s = malloc(sizeof *s);
    s->fn = fn;
    s->arg = arg;
    int r = pthread_create(t, &a, trampoline, s);
    if (!attr) pthread_attr_destroy(&a);
    if (r) free(s);
    return r;
}
