// Spike test library, built for glibc/libstdc++ exactly like UT's modules (clang --target=aarch64-linux-gnu).
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
// Core.so's 64 KB thread_local UnGuardBlockTLS::Data, reached with the initial-exec model like the engine does
// (one R_AARCH64_TLS_TPREL64 against Core's symbol).
extern __thread char _ZN15UnGuardBlockTLS4DataE[] __attribute__((tls_model("initial-exec")));

void *tt_tls_addr(void) { return _ZN15UnGuardBlockTLS4DataE; }
void tt_tls_write(int v, int at) { std::memcpy(_ZN15UnGuardBlockTLS4DataE + at, &v, sizeof v); }
int tt_tls_read(int at) {
    int v;
    std::memcpy(&v, _ZN15UnGuardBlockTLS4DataE + at, sizeof v);
    return v;
}

// Our own thread_local: TLSDESC (general-dynamic in a shared library on aarch64).
static thread_local int tt_counter = 100;
int tt_tlsdesc_bump(void) { return ++tt_counter; }

// An exception thrown inside libstdc++ (std::string::at -> __throw_out_of_range_fmt), unwound through
// libstdc++'s frames and caught here: two custom-loaded modules, GNU personality and unwinder.
int tt_throw(void) {
    try {
        std::string s("abc");
        return s.at(10);
    } catch (const std::out_of_range &e) {
        return std::strstr(e.what(), "basic_string") ? 42 : 41;
    } catch (...) {
        return 2;
    }
}

// Our own throw of a class type across a non-inlined frame.
struct TTError : std::runtime_error {
    explicit TTError(const char *m) : std::runtime_error(m) {}
};
__attribute__((noinline)) static void tt_deep(int n) {
    if (n == 0) throw TTError("deep");
    tt_deep(n - 1);
}
int tt_throw_deep(void) {
    try {
        tt_deep(20);
    } catch (const TTError &e) {
        return std::strcmp(e.what(), "deep") == 0 ? 43 : 3;
    }
    return 0;
}
}
