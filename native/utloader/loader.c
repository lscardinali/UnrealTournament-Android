/*
 * utloader: ELF loader for glibc-linked aarch64 shared objects / PIE executables inside a bionic process.
 *
 * Supports what UT469's Linux build and GNU libstdc++/libgcc_s use: PT_LOAD/PT_DYNAMIC/PT_TLS/PT_GNU_EH_FRAME,
 * DT_GNU_HASH/DT_HASH, RELA (RELATIVE, ABS64, GLOB_DAT, JUMP_SLOT, TLS_TPREL64, TLSDESC) and DT_RELR.
 * Symbols: our modules first (load order), then the glibc shim, then bionic, then a trap stub (reports the
 * name when called). glibc's own libraries (libc.so.6, libm.so.6, ...) are never loaded: the shim provides them.
 */
#define _GNU_SOURCE
#include "utl.h"

#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef R_AARCH64_TLSDESC
#define R_AARCH64_TLSDESC 1031
#endif
#ifndef DT_RELR
#define DT_RELR 36
#define DT_RELRSZ 35
#endif

static utl_module *modules, *modules_tail;
static int load_count;
static char search_dirs[8][512];
static int nsearch;

/* glibc's own libraries: their symbols come from the shim/bionic, never loaded. */
static const char *const provided[] = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "libutil.so.1",
    "ld-linux-aarch64.so.1", NULL,
};

void utl_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "utl: %s\n", buf);
    __android_log_write(ANDROID_LOG_INFO, "utloader", buf);
}

void utl_add_search_dir(const char *dir) {
    if (nsearch < 8) snprintf(search_dirs[nsearch++], sizeof search_dirs[0], "%s", dir);
}

utl_module *utl_modules(void) { return modules; }
int utl_load_count(void) { return load_count; }

utl_module *utl_module_for_addr(uintptr_t addr) {
    for (utl_module *m = modules; m; m = m->next)
        if (addr >= m->map_start && addr < m->map_end) return m;
    return NULL;
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* ------------------------------------------------------------------------------------------ symbols */

static uint32_t gnu_hash(const char *s) {
    uint32_t h = 5381;
    for (; *s; s++) h = h * 33 + (uint8_t)*s;
    return h;
}

static uint32_t sysv_hash(const char *s) {
    uint32_t h = 0, g;
    for (; *s; s++) {
        h = (h << 4) + (uint8_t)*s;
        g = h & 0xf0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static int sym_defined(const ElfW(Sym) *s) {
    int bind = ELF64_ST_BIND(s->st_info);
    return s->st_shndx != SHN_UNDEF && (bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE) &&
           (s->st_value != 0 || ELF64_ST_TYPE(s->st_info) == STT_TLS);
}

/* Defined symbol `name` in m; a hidden-version definition only if nothing better. */
static const ElfW(Sym) *module_find(const utl_module *m, const char *name, uint32_t gh, uint32_t sh) {
    const ElfW(Sym) *hidden = NULL;
    if (m->gnu_buckets) {
        const uint64_t word = m->gnu_bloom[(gh / 64) % m->gnu_bloom_size];
        const uint64_t mask = (1ull << (gh % 64)) | (1ull << ((gh >> m->gnu_bloom_shift) % 64));
        if ((word & mask) != mask) return NULL;
        uint32_t i = m->gnu_buckets[gh % m->gnu_nbucket];
        if (i < m->gnu_symoffset) return NULL;
        for (;; i++) {
            const uint32_t ch = m->gnu_chain[i - m->gnu_symoffset];
            if (((ch ^ gh) >> 1) == 0) {
                const ElfW(Sym) *s = &m->dynsym[i];
                if (sym_defined(s) && strcmp(m->dynstr + s->st_name, name) == 0) {
                    if (m->versym && (m->versym[i] & 0x8000)) hidden = s;
                    else return s;
                }
            }
            if (ch & 1) break;
        }
        return hidden;
    }
    if (m->sysv_hash) {
        const uint32_t nbucket = m->sysv_hash[0];
        const uint32_t *buckets = m->sysv_hash + 2, *chains = buckets + nbucket;
        for (uint32_t i = buckets[sh % nbucket]; i; i = chains[i]) {
            const ElfW(Sym) *s = &m->dynsym[i];
            if (sym_defined(s) && strcmp(m->dynstr + s->st_name, name) == 0) {
                if (m->versym && (m->versym[i] & 0x8000)) hidden = s;
                else return s;
            }
        }
    }
    return hidden;
}

void *utl_sym(utl_module *only, const char *name) {
    const uint32_t gh = gnu_hash(name), sh = sysv_hash(name);
    for (utl_module *m = only ? only : modules; m; m = only ? NULL : m->next) {
        const ElfW(Sym) *s = module_find(m, name, gh, sh);
        if (s) return (void *)(m->bias + s->st_value);
    }
    return NULL;
}

typedef struct {
    uintptr_t addr;
    const utl_module *module;   /* defining module, NULL if shim/bionic/trap */
    const ElfW(Sym) *sym;
} resolved;

static int resolve(const char *name, int weak, resolved *out) {
    const uint32_t gh = gnu_hash(name), sh = sysv_hash(name);
    void *p;
    /* A few game functions the shim replaces even though a loaded module defines them (utl_shim_preempt). */
    if ((p = utl_shim_preempt(name))) {
        out->module = NULL;
        out->sym = NULL;
        out->addr = (uintptr_t)p;
        return 1;
    }
    for (utl_module *m = modules; m; m = m->next) {
        const ElfW(Sym) *s = module_find(m, name, gh, sh);
        if (s) {
            out->module = m;
            out->sym = s;
            out->addr = ELF64_ST_TYPE(s->st_info) == STT_TLS ? s->st_value : m->bias + s->st_value;
            return 1;
        }
    }
    out->module = NULL;
    out->sym = NULL;
    if ((p = utl_shim_lookup(name)) || (p = utl_external_lookup(name)) || (p = utl_bionic_lookup(name))) {
        out->addr = (uintptr_t)p;
        return 1;
    }
    if (weak) {
        out->addr = 0;
        return 1;
    }
    out->addr = (uintptr_t)utl_trap_stub(name);
    return 0;
}

/* ------------------------------------------------------------------------------------------ relocation */

static int relocate_list(utl_module *m, const ElfW(Rela) *rel, size_t n) {
    int missing = 0;
    for (size_t i = 0; i < n; i++) {
        const ElfW(Rela) *r = &rel[i];
        const uint32_t type = ELF64_R_TYPE(r->r_info), si = ELF64_R_SYM(r->r_info);
        uintptr_t *where = (uintptr_t *)(m->bias + r->r_offset);
        resolved res = { 0, m, NULL };
        const ElfW(Sym) *s = si ? &m->dynsym[si] : NULL;
        const char *name = s ? m->dynstr + s->st_name : "";

        if (type == R_AARCH64_RELATIVE) {
            *where = m->bias + r->r_addend;
            continue;
        }
        if (type == R_AARCH64_NONE) continue;

        if (si) {
            const int weak = ELF64_ST_BIND(s->st_info) == STB_WEAK;
            if (!resolve(name, weak, &res)) {
                missing++;
                utl_log("%s: unresolved %s (trap)", m->name, name);
            }
        } else {
            res.module = m; /* module-local TLS */
        }

        switch (type) {
        case R_AARCH64_ABS64:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
            *where = res.addr + r->r_addend;
            break;
        case R_AARCH64_TLS_TPREL64:
            if (!res.module || !res.module->has_tls) {
                utl_log("%s: TPREL64 against %s without TLS module", m->name, name);
                *where = 0;
                break;
            }
            *where = (uintptr_t)(utl_tls_tp_offset(res.module) + (intptr_t)(res.sym ? res.sym->st_value : 0) +
                                 r->r_addend);
            break;
        case R_AARCH64_TLSDESC:
            if (!res.module || !res.module->has_tls) {
                utl_log("%s: TLSDESC against %s without TLS module", m->name, name);
                break;
            }
            where[0] = (uintptr_t)&utl_tlsdesc_return;
            where[1] = (uintptr_t)(utl_tls_tp_offset(res.module) + (intptr_t)(res.sym ? res.sym->st_value : 0) +
                                   r->r_addend);
            break;
        default:
            utl_log("%s: unsupported relocation type %u (sym %s)", m->name, type, name);
            missing++;
        }
    }
    return missing;
}

static void apply_relr(utl_module *m) {
    uintptr_t *where = NULL;
    for (size_t i = 0; i < m->relr_count; i++) {
        const ElfW(Addr) e = m->relr[i];
        if ((e & 1) == 0) {
            where = (uintptr_t *)(m->bias + e);
            *where++ += m->bias;
        } else {
            uintptr_t *p = where;
            for (ElfW(Addr) bits = e >> 1; bits; bits >>= 1, p++)
                if (bits & 1) *p += m->bias;
            where += 63;
        }
    }
}

/* ------------------------------------------------------------------------------------------ loading */

static uintptr_t page_down(uintptr_t x, size_t pg) { return x & ~(uintptr_t)(pg - 1); }
static uintptr_t page_up(uintptr_t x, size_t pg) { return (x + pg - 1) & ~(uintptr_t)(pg - 1); }

static int find_file(const char *name, char *out, size_t outsz) {
    if (strchr(name, '/')) {
        snprintf(out, outsz, "%s", name);
        return access(out, R_OK) == 0;
    }
    for (int i = 0; i < nsearch; i++) {
        snprintf(out, outsz, "%s/%s", search_dirs[i], name);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

static utl_module *find_loaded(const char *name) {
    const char *b = base_name(name);
    for (utl_module *m = modules; m; m = m->next)
        if (strcmp(m->name, b) == 0) return m;
    return NULL;
}

static int map_module(utl_module *m) {
    const size_t pg = (size_t)getpagesize();
    ElfW(Ehdr) eh;
    int fd = open(m->path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return utl_log("open %s: %s", m->path, strerror(errno)), -1;
    if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_machine != EM_AARCH64 || eh.e_type != ET_DYN) {
        close(fd);
        return utl_log("%s: not an aarch64 ET_DYN ELF", m->path), -1;
    }
    ElfW(Phdr) *ph = malloc(eh.e_phnum * sizeof *ph);
    if (pread(fd, ph, eh.e_phnum * sizeof *ph, (off_t)eh.e_phoff) != (ssize_t)(eh.e_phnum * sizeof *ph)) {
        free(ph);
        close(fd);
        return -1;
    }

    uintptr_t lo = UINTPTR_MAX, hi = 0;
    size_t align = pg;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (page_down(ph[i].p_vaddr, pg) < lo) lo = page_down(ph[i].p_vaddr, pg);
        if (page_up(ph[i].p_vaddr + ph[i].p_memsz, pg) > hi) hi = page_up(ph[i].p_vaddr + ph[i].p_memsz, pg);
        if (ph[i].p_align > align) align = ph[i].p_align;
    }
    const size_t span = hi - lo;
    uint8_t *reserve = mmap(NULL, span + align, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserve == MAP_FAILED) {
        free(ph);
        close(fd);
        return utl_log("%s: reserve %zu failed", m->name, span), -1;
    }
    const uintptr_t start = (uintptr_t)(reserve + align - 1) & ~(uintptr_t)(align - 1);
    if (start > (uintptr_t)reserve) munmap(reserve, start - (uintptr_t)reserve);
    if ((uintptr_t)reserve + span + align > start + span)
        munmap((void *)(start + span), (uintptr_t)reserve + span + align - (start + span));
    m->bias = start - lo;
    m->map_start = start;
    m->map_end = start + span;

    for (int i = 0; i < eh.e_phnum; i++) {
        const ElfW(Phdr) *p = &ph[i];
        if (p->p_type != PT_LOAD) continue;
        const int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) |
                         ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        const uintptr_t seg = m->bias + p->p_vaddr, seg_page = page_down(seg, pg);
        const uintptr_t file_end = seg + p->p_filesz;
        if (p->p_filesz) {
            void *r = mmap((void *)seg_page, file_end - seg_page, prot, MAP_PRIVATE | MAP_FIXED, fd,
                           (off_t)page_down(p->p_offset, pg));
            if (r == MAP_FAILED) {
                utl_log("%s: mmap segment %d: %s", m->name, i, strerror(errno));
                free(ph);
                close(fd);
                return -1;
            }
        }
        if (p->p_memsz > p->p_filesz) {
            const uintptr_t zero_page = page_up(file_end, pg), seg_end = page_up(seg + p->p_memsz, pg);
            if ((p->p_flags & PF_W) && p->p_filesz && zero_page > file_end) memset((void *)file_end, 0, zero_page - file_end);
            if (seg_end > zero_page)
                mmap((void *)zero_page, seg_end - zero_page, prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
        }
    }
    close(fd);

    for (int i = 0; i < eh.e_phnum; i++) {
        const ElfW(Phdr) *p = &ph[i];
        if (p->p_type == PT_DYNAMIC) m->dynamic = (const ElfW(Dyn) *)(m->bias + p->p_vaddr);
        else if (p->p_type == PT_GNU_EH_FRAME) m->eh_frame_hdr = (const void *)(m->bias + p->p_vaddr);
        else if (p->p_type == PT_PHDR) m->phdr = (const ElfW(Phdr) *)(m->bias + p->p_vaddr);
        else if (p->p_type == PT_TLS) {
            m->has_tls = 1;
            m->tls_image = (const void *)(m->bias + p->p_vaddr);
            m->tls_filesz = p->p_filesz;
            m->tls_memsz = p->p_memsz;
            m->tls_align = p->p_align ? p->p_align : 1;
        }
    }
    if (!m->phdr) /* the program headers sit inside the first segment */
        for (int i = 0; i < eh.e_phnum; i++)
            if (ph[i].p_type == PT_LOAD && eh.e_phoff >= ph[i].p_offset && eh.e_phoff < ph[i].p_offset + ph[i].p_filesz)
                m->phdr = (const ElfW(Phdr) *)(m->bias + ph[i].p_vaddr + (eh.e_phoff - ph[i].p_offset));
    m->phnum = eh.e_phnum;
    free(ph);
    return m->dynamic ? 0 : (utl_log("%s: no PT_DYNAMIC", m->name), -1);
}

static void parse_dynamic(utl_module *m, const char **needed, int *nneeded) {
    size_t strtab_needed_off[UTL_MAX_NEEDED];
    int n = 0;
    size_t relasz = 0, pltrelsz = 0, init_arraysz = 0, fini_arraysz = 0, relrsz = 0;
    for (const ElfW(Dyn) *d = m->dynamic; d->d_tag != DT_NULL; d++) {
        const uintptr_t v = d->d_un.d_ptr;
        switch (d->d_tag) {
        case DT_NEEDED: if (n < UTL_MAX_NEEDED) strtab_needed_off[n++] = d->d_un.d_val; break;
        case DT_STRTAB: m->dynstr = (const char *)(m->bias + v); break;
        case DT_SYMTAB: m->dynsym = (const ElfW(Sym) *)(m->bias + v); break;
        case DT_VERSYM: m->versym = (const uint16_t *)(m->bias + v); break;
        case DT_HASH: m->sysv_hash = (const uint32_t *)(m->bias + v); break;
        case DT_GNU_HASH: {
            const uint32_t *h = (const uint32_t *)(m->bias + v);
            m->gnu_nbucket = h[0];
            m->gnu_symoffset = h[1];
            m->gnu_bloom_size = h[2];
            m->gnu_bloom_shift = h[3];
            m->gnu_bloom = (const uint64_t *)(h + 4);
            m->gnu_buckets = (const uint32_t *)(m->gnu_bloom + m->gnu_bloom_size);
            m->gnu_chain = m->gnu_buckets + m->gnu_nbucket;
            break;
        }
        case DT_RELA: m->rela = (const ElfW(Rela) *)(m->bias + v); break;
        case DT_RELASZ: relasz = d->d_un.d_val; break;
        case DT_JMPREL: m->jmprel = (const ElfW(Rela) *)(m->bias + v); break;
        case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
        case DT_RELR: m->relr = (const ElfW(Addr) *)(m->bias + v); break;
        case DT_RELRSZ: relrsz = d->d_un.d_val; break;
        case DT_INIT: m->init = (void (*)(void))(m->bias + v); break;
        case DT_INIT_ARRAY: m->init_array = (void (**)(int, char **, char **))(m->bias + v); break;
        case DT_INIT_ARRAYSZ: init_arraysz = d->d_un.d_val; break;
        case DT_FINI_ARRAY: m->fini_array = (void (**)(void))(m->bias + v); break;
        case DT_FINI_ARRAYSZ: fini_arraysz = d->d_un.d_val; break;
        default: break;
        }
    }
    m->rela_count = relasz / sizeof(ElfW(Rela));
    m->jmprel_count = pltrelsz / sizeof(ElfW(Rela));
    m->relr_count = relrsz / sizeof(ElfW(Addr));
    m->init_array_count = init_arraysz / sizeof(void *);
    m->fini_array_count = fini_arraysz / sizeof(void *);
    for (int i = 0; i < n; i++) needed[i] = m->dynstr + strtab_needed_off[i];
    *nneeded = n;
}

/* glibc libraries the game links that have an Android (bionic) build in the app: when that build can be
   dlopen'ed, it answers the imports instead of loading the glibc file. */
static const char *const bionic_aliases[][2] = {
    { "libSDL3.so.0", "libSDL3.so" }, { "libSDL3_ttf.so.0", "libSDL3_ttf.so" }, { "libopenal.so.1", "libopenal.so" },
    { NULL, NULL },
};
static void *external[16];
static int nexternal;

static int try_bionic_alias(const char *name, int depth) {
    for (int i = 0; bionic_aliases[i][0]; i++) {
        if (strcmp(name, bionic_aliases[i][0]) != 0) continue;
        void *h = dlopen(bionic_aliases[i][1], RTLD_NOW | RTLD_GLOBAL);
        if (!h) return 0;
        for (int j = 0; j < nexternal; j++)
            if (external[j] == h) return 1;
        if (nexternal < 16) external[nexternal++] = h;
        utl_log("%*s%s -> Android %s", depth * 2, "", name, bionic_aliases[i][1]);
        return 1;
    }
    return 0;
}

void *utl_external_lookup(const char *name) {
    void *p = NULL;
    for (int i = 0; i < nexternal && !p; i++) p = dlsym(external[i], name);
    if (nexternal) (void)dlerror(); /* a miss in an earlier library must not leave a stale error for the game */
    return p;
}

static int is_provided(const char *name) {
    for (int i = 0; provided[i]; i++)
        if (strcmp(name, provided[i]) == 0) return 1;
    return 0;
}

static utl_module *load_one(const char *name, int depth);

static utl_module *load_one(const char *name, int depth) {
    utl_module *m = find_loaded(name);
    char path[512];
    const char *needed[UTL_MAX_NEEDED];
    int nneeded = 0;
    if (m) return m;
    if (!find_file(name, path, sizeof path)) {
        utl_log("%*snot found: %s", depth * 2, "", name);
        return NULL;
    }
    m = calloc(1, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", base_name(name));
    snprintf(m->path, sizeof m->path, "%s", path);
    if (map_module(m) != 0) {
        free(m);
        return NULL;
    }
    parse_dynamic(m, needed, &nneeded);
    /* Register before loading dependencies (cycles), append in load order. */
    if (modules_tail) modules_tail->next = m;
    else modules = m;
    modules_tail = m;
    load_count++;
    if (m->has_tls) utl_tls_add_module(m);
    utl_log("%*sloaded %s at %#lx (%zu KB)%s", depth * 2, "", m->name, (unsigned long)m->map_start,
            (size_t)((m->map_end - m->map_start) >> 10), m->has_tls ? " +TLS" : "");

    for (int i = 0; i < nneeded; i++) {
        if (is_provided(needed[i]) || try_bionic_alias(needed[i], depth + 1)) continue;
        utl_module *d = load_one(needed[i], depth + 1);
        if (d && m->nneeded < UTL_MAX_NEEDED) m->needed[m->nneeded++] = d;
        else if (!d) utl_log("%*s%s: dependency %s unavailable; its symbols resolve via shim/bionic/trap",
                             depth * 2 + 2, "", m->name, needed[i]);
    }
    return m;
}

static int relocate_all(void) {
    int missing = 0;
    for (utl_module *m = modules; m; m = m->next) {
        if (m->relocated) continue;
        m->relocated = 1;
        apply_relr(m);
        missing += relocate_list(m, m->rela, m->rela_count);
        missing += relocate_list(m, m->jmprel, m->jmprel_count);
    }
    return missing;
}

utl_module *utl_load(const char *name) {
    utl_module *m = load_one(name, 0);
    if (!m) return NULL;
    const int missing = relocate_all();
    if (missing) utl_log("%s: %d unresolved symbol reference(s) bound to traps", name, missing);
    return m;
}

void utl_run_inits(utl_module *m) {
    extern char **environ;
    static int argc = 1;
    static char *argv[] = { "ut", NULL };
    if (!m || m->initialized) return;
    m->initialized = 1;
    for (int i = 0; i < m->nneeded; i++) utl_run_inits(m->needed[i]);
    if (m->init) m->init();
    for (size_t i = 0; i < m->init_array_count; i++) {
        void (*f)(int, char **, char **) = m->init_array[i];
        if (f && (uintptr_t)f != (uintptr_t)-1) f(argc, argv, environ);
    }
    utl_log("initialized %s", m->name);
}
