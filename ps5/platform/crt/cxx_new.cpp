// SPDX-License-Identifier: MIT
// operator new/delete on the C heap (C++17 [new.delete]). No exceptions: an
// allocation failure in the throwing forms aborts.
#include <cstddef>
#include <cstdlib>
#include <new>

static void* sf3_alloc(std::size_t n, std::size_t align) {
    if (n == 0) n = 1;
    void* p = nullptr;
    if (align <= alignof(std::max_align_t)) p = std::malloc(n);
    else if (posix_memalign(&p, align, n) != 0) p = nullptr;
    return p;
}
static void* sf3_alloc_or_die(std::size_t n, std::size_t a) { void* p = sf3_alloc(n, a); if (!p) std::abort(); return p; }

void* operator new(std::size_t n) { return sf3_alloc_or_die(n, 0); }
void* operator new[](std::size_t n) { return sf3_alloc_or_die(n, 0); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return sf3_alloc(n, 0); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return sf3_alloc(n, 0); }
void* operator new(std::size_t n, std::align_val_t a) { return sf3_alloc_or_die(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a) { return sf3_alloc_or_die(n, (std::size_t)a); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return sf3_alloc(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return sf3_alloc(n, (std::size_t)a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
