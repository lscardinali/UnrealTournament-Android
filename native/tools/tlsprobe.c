/*
 * tlsprobe — checks the bionic thread layout that the in-process loader's TLS "stack-carve" relies on:
 * for every thread created with pthread_create, is (thread pointer - stack top) the same constant?
 * If yes, a fixed TP-relative offset (what glibc-compiled initial-exec TLS code uses) can point into a
 * region reserved at the top of every engine thread's stack.
 *
 *   clang --target=aarch64-linux-android33 -O2 -o tlsprobe tlsprobe.c && adb push ... && adb shell ./tlsprobe
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void report(const char *who) {
    pthread_attr_t a;
    void *stack_lo;
    size_t stack_size;
    uintptr_t tp = (uintptr_t)__builtin_thread_pointer();
    pthread_getattr_np(pthread_self(), &a);
    pthread_attr_getstack(&a, &stack_lo, &stack_size);
    pthread_attr_destroy(&a);
    uintptr_t top = (uintptr_t)stack_lo + stack_size;
    printf("%-14s tp=%#lx stack=[%#lx,%#lx) size=%zu KB  tp-top=%ld\n", who, (unsigned long)tp,
           (unsigned long)(uintptr_t)stack_lo, (unsigned long)top, stack_size / 1024, (long)(tp - top));
}

static void *thread_fn(void *arg) {
    report((const char *)arg);
    /* Probe the carve: a 256 KB region right below the stack top must be usable stack. */
    volatile char carve[256 * 1024];
    memset((void *)carve, 0xA5, sizeof carve);
    return 0;
}

int main(void) {
    static const size_t sizes[] = { 0, 1 << 20, 8 << 20, 16 << 20 };
    char names[4][32];
    report("main");
    for (int i = 0; i < 4; i++) {
        pthread_t t;
        pthread_attr_t a;
        pthread_attr_init(&a);
        if (sizes[i]) pthread_attr_setstacksize(&a, sizes[i]);
        snprintf(names[i], sizeof names[i], "thread %zuMB", sizes[i] >> 20);
        pthread_create(&t, &a, thread_fn, names[i]);
        pthread_join(t, 0);
        pthread_attr_destroy(&a);
    }
    return 0;
}
