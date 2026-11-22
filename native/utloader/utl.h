/*
 * utloader — loads glibc-linked aarch64 ELF modules (UT469 Linux build, GNU libstdc++/libgcc_s) into a bionic
 * (Android) process. See docs/native-port-plan.md.
 */
#ifndef UTL_H
#define UTL_H

#include <elf.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>

#define UTL_MAX_NEEDED 48

typedef struct utl_module {
    char name[128];             /* soname used to find it (e.g. "Core.so") */
    char path[512];
    uintptr_t bias;             /* load bias: runtime address = bias + vaddr */
    uintptr_t map_start, map_end;
    const ElfW(Phdr) *phdr;     /* in memory */
    int phnum;
    const ElfW(Dyn) *dynamic;

    const ElfW(Sym) *dynsym;
    const char *dynstr;
    const uint16_t *versym;

    /* DT_GNU_HASH */
    uint32_t gnu_nbucket, gnu_symoffset, gnu_bloom_size, gnu_bloom_shift;
    const uint64_t *gnu_bloom;
    const uint32_t *gnu_buckets, *gnu_chain;
    /* DT_HASH (fallback) */
    const uint32_t *sysv_hash;

    const ElfW(Rela) *rela; size_t rela_count;
    const ElfW(Rela) *jmprel; size_t jmprel_count;
    const ElfW(Addr) *relr; size_t relr_count;

    void (*init)(void);
    void (**init_array)(int, char **, char **); size_t init_array_count;
    void (**fini_array)(void); size_t fini_array_count;

    const void *eh_frame_hdr;   /* PT_GNU_EH_FRAME, for the unwinder */

    /* TLS (PT_TLS): laid out in our static block (tls.c) */
    int has_tls;
    const void *tls_image; size_t tls_filesz, tls_memsz, tls_align;
    intptr_t tls_block_offset;  /* offset inside the block */

    struct utl_module *needed[UTL_MAX_NEEDED];
    int nneeded;
    int relocated, initialized;
    struct utl_module *next;
} utl_module;

/* loader.c */
void utl_add_search_dir(const char *dir);
utl_module *utl_load(const char *name);         /* name or path; loads DT_NEEDED recursively, relocates */
void utl_run_inits(utl_module *m);              /* constructors, dependencies first (call on a utl thread) */
void *utl_sym(utl_module *m, const char *name); /* defined symbol address in m (NULL if m==NULL: all modules) */
utl_module *utl_modules(void);
utl_module *utl_module_for_addr(uintptr_t addr);
int utl_load_count(void);
void *utl_external_lookup(const char *name);     /* Android builds standing in for glibc libs (SDL3, ...) */
void utl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* tls.c */
int utl_tls_init(void);                         /* measure the bionic thread layout; call once, first */
intptr_t utl_tls_tp_offset(const utl_module *m);/* TP-relative offset of m's TLS block */
void utl_tls_add_module(utl_module *m);         /* assign m a slot in the block (at load) */
int utl_thread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
void *utl_tls_block(void);                      /* current thread's block (NULL if not a utl thread) */
extern void utl_tlsdesc_return(void);           /* TLSDESC resolver for static offsets */

/* shim.c */
void *utl_shim_lookup(const char *name);        /* our glibc -> bionic replacement, or NULL */
void *utl_bionic_lookup(const char *name);      /* pass-through to bionic libc/libm/libdl, or NULL */
void *utl_trap_stub(const char *name);          /* a function that reports `name` and aborts when called */
void utl_shim_set_args(int argc, char **argv);

#endif
