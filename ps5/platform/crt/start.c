// SPDX-License-Identifier: MIT
// Title entry point for sfiii-ps5, written from scratch.
//
// What it relies on (see docs/platform.md, "Startup"):
//  - The system loader enters _start with %rdi pointing at the argument
//    block: a 64-bit argc followed by the argv pointers (the FreeBSD/PS4
//    convention for the kernel-provided argument area).
//  - The system libc (libSceLibcInternal, via sce_module/libc.prx) needs
//    _init_env() before any other libc call; it sets up environ.
//  - Constructors are not run by the loader for the main executable, so
//    the startup code walks .preinit_array/.init_array itself; lld defines
//    the __*_array_start/end bounds.
#include <stddef.h>
#include <stdint.h>

extern void _init_env(void);
extern int atexit(void (*)(void));
extern void exit(int) __attribute__((noreturn));
extern int main(int, char**, char**);
extern char** environ;
void sf3_fp_ieee(void);

typedef void (*sf3_fn)(void);
extern sf3_fn __preinit_array_start[] __attribute__((weak, visibility("hidden")));
extern sf3_fn __preinit_array_end[] __attribute__((weak, visibility("hidden")));
extern sf3_fn __init_array_start[] __attribute__((weak, visibility("hidden")));
extern sf3_fn __init_array_end[] __attribute__((weak, visibility("hidden")));
extern sf3_fn __fini_array_start[] __attribute__((weak, visibility("hidden")));
extern sf3_fn __fini_array_end[] __attribute__((weak, visibility("hidden")));

void _init(void) {}
void _fini(void) {}

static void sf3_run_fini(void) {
    // Destructors run in reverse order of their constructors.
    for (sf3_fn* f = __fini_array_end; f > __fini_array_start;) {
        --f;
        if (*f && *f != (sf3_fn)-1) (*f)();
    }
}

static void sf3_run(sf3_fn* begin, sf3_fn* end) {
    for (sf3_fn* f = begin; f < end; ++f)
        if (*f && *f != (sf3_fn)-1) (*f)();
}

__attribute__((noreturn, used)) static void sf3_start_c(uint64_t* args) {
    int argc = 0;
    char** argv = NULL;
    if (args) {
        argc = (int)args[0];
        argv = (char**)(args + 1);
    }
    sf3_fp_ieee();
    _init_env();
    sf3_run(__preinit_array_start, __preinit_array_end);
    sf3_run(__init_array_start, __init_array_end);
    atexit(sf3_run_fini);
    exit(main(argc, argv, environ));
}

// The ABI wants %rsp+8 16-byte aligned at a call's first instruction; the
// loader's alignment is not documented, so realign before calling C.
__asm__(
    ".globl _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "  xorl %ebp, %ebp\n"
    "  andq $-16, %rsp\n"
    "  call sf3_start_c\n"
    "  ud2\n");
