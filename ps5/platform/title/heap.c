// MIT, see LICENSE.
// The title's C heap, in direct memory. The system malloc in a title stops giving
// medium blocks after a few hundred MB, with most of the flexible memory still free:
// a 30 MB disc read and then RADV's first shader build aborted in it. Every
// malloc-family call in the executable is routed here with the linker's --wrap
// (tools/ps5/saturn_ps5.cmake), so RADV, libc++ and our code share this heap.
//
// Layout: chunks of direct memory of at least 64 MiB, mapped as the heap fills
// (a title may not reserve a virtual range up front). Blocks are cut from them in size classes
// (32-byte steps to 512 bytes, then four classes for each power of two) and go
// back to a free list for their class; memory is not returned to the system.
// Each block starts with a 32-byte header holding its class size, so every block is
// 32-byte aligned: the compiler for this target assumes operator new gives that
// (it stores with vmovdqa ymm into a new object). Pointers that
// are not in a chunk (from the system libc, such as strdup's) go to the
// system functions. One lock guards the heap.
#include <errno.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int sceKernelAllocateDirectMemory(long start, long end, size_t len, size_t align, int type, long* phys);
int sceKernelMapDirectMemory(void** addr, size_t len, int prot, int flags, long phys, size_t align);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelDebugOutText(int channel, const char* text);

void* __real_malloc(size_t);
void __real_free(void*);
void* __real_realloc(void*, size_t);

enum { kHdr = 32, kStepMin = 64u << 20, kPage = 2u << 20 };
static const uint64_t kMagic = 0x5346334845415031ull, kAligned = 0x5346334845415032ull;

typedef struct { uint64_t size; uint64_t tag; uint64_t pad[2]; } Hdr;   // 32 bytes: blocks are 32-aligned   // size: the block's class size (header included)
typedef struct Free { struct Free* next; } Free;

// Chunks of direct memory, in address order. The heap cuts blocks from the last one
// and opens another when a block does not fit (the rest of the old one is left).
enum { kChunks = 1024 };
static struct { char* lo; char* hi; } g_chunk[kChunks];
static int g_nchunks;
static char *g_cur, *g_end;     // what is left of the newest chunk
static size_t g_used, g_committed;
static int g_state;             // 0 untried, 1 working, -1 no direct memory
static Free* g_free[256];
static volatile char g_lock;

static void lock(void) { while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)) sched_yield(); }
static void unlock(void) { __atomic_clear(&g_lock, __ATOMIC_RELEASE); }

static void say(const char* t) { sceKernelDebugOutText(0, t); }

// class index and size for a block of n bytes (header included)
static unsigned class_of(size_t n, size_t* size) {
    if (n <= 512) { unsigned c = (unsigned)((n + 31) / 32); *size = (size_t)c * 32; return c; }   // 1..16
    unsigned b = 63 - (unsigned)__builtin_clzll((unsigned long long)(n - 1));   // 2^b < n <= 2^(b+1), b >= 9
    size_t q = (size_t)1 << (b - 2);
    size_t k = (n - ((size_t)1 << b) + q - 1) / q;   // 1..4
    *size = ((size_t)1 << b) + k * q;
    return 16 + (b - 9) * 4 + (unsigned)k;
}

static int init(void) { return g_state >= 0; }

static int add_chunk(size_t need) {   // a new chunk holding at least need bytes
    if (g_nchunks == kChunks) return 0;
    size_t step = need < kStepMin ? kStepMin : (need + kPage - 1) & ~(size_t)(kPage - 1);
    long phys = 0;
    int r = sceKernelAllocateDirectMemory(0, (long)sceKernelGetDirectMemorySize(), step, kPage, 0 /* WB_ONION */, &phys);
    void* at = 0;
    if (r == 0) r = sceKernelMapDirectMemory(&at, step, 3 /* CPU read|write */, 0, phys, kPage);
    if (r != 0 || !at) {
        if (g_state == 0) { g_state = -1; say("[sf3 heap] no direct memory: using the system heap\n"); }
        return 0;
    }
    g_state = 1;
    int i = g_nchunks++;
    while (i > 0 && g_chunk[i - 1].lo > (char*)at) { g_chunk[i] = g_chunk[i - 1]; i--; }
    g_chunk[i].lo = at, g_chunk[i].hi = (char*)at + step;
    g_cur = at, g_end = (char*)at + step;
    g_committed += step;
    return 1;
}

static int ours(const void* p) {   // called without the lock held; takes it (chunks move on insert)
    if (g_state != 1) return 0;
    lock();
    int lo = 0, hi = g_nchunks - 1, found = 0;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if ((const char*)p < g_chunk[m].lo) hi = m - 1;
        else if ((const char*)p >= g_chunk[m].hi) lo = m + 1;
        else { found = 1; break; }
    }
    unlock();
    return found;
}

static void* take(size_t n) {   // a block with n user bytes, or null
    if (n > ((size_t)1 << 36)) return 0;
    size_t size;
    unsigned c = class_of(n + kHdr, &size);
    Hdr* h;
    lock();
    if (c < 256 && g_free[c]) {
        h = (Hdr*)g_free[c];
        g_free[c] = g_free[c]->next;
    } else {
        if ((size_t)(g_end - g_cur) < size && !add_chunk(size)) { unlock(); return 0; }
        h = (Hdr*)g_cur;
        g_cur += size, g_used += size;
    }
    unlock();
    h->size = size, h->tag = kMagic;
    return (char*)h + kHdr;
}

static void give(void* p) {
    Hdr* h = (Hdr*)((char*)p - kHdr);
    if (h->tag == kAligned) { give((void*)(uintptr_t)h->size); return; }
    size_t size;
    unsigned c = class_of(h->size, &size);
    lock();
    Free* f = (Free*)h;
    f->next = g_free[c], g_free[c] = f;
    unlock();
}

static size_t usable(const void* p) {
    const Hdr* h = (const Hdr*)((const char*)p - kHdr);
    if (h->tag == kAligned) {
        const char* raw = (const char*)(uintptr_t)h->size;
        return ((const Hdr*)(raw - kHdr))->size - kHdr - (size_t)((const char*)p - raw);
    }
    return h->size - kHdr;
}

static void* take_aligned(size_t align, size_t n) {
    if (align <= kHdr) return take(n);
    char* raw = take(n + align + kHdr);
    if (!raw) return 0;
    char* p = (char*)(((uintptr_t)raw + kHdr + align - 1) & ~(uintptr_t)(align - 1));
    Hdr* h = (Hdr*)(p - kHdr);
    h->size = (uint64_t)(uintptr_t)raw, h->tag = kAligned;
    return p;
}

void* __wrap_malloc(size_t n) {
    if (!init()) return __real_malloc(n);
    void* p = take(n ? n : 1);
    if (!p && !init()) return __real_malloc(n);   // the first chunk was refused
    if (!p) errno = ENOMEM;
    return p;
}

void __wrap_free(void* p) {
    if (!p) return;
    if (ours(p)) give(p); else __real_free(p);
}

void* __wrap_calloc(size_t a, size_t b) {
    size_t n;
    if (__builtin_mul_overflow(a, b, &n)) { errno = ENOMEM; return 0; }
    void* p = __wrap_malloc(n);
    if (p) memset(p, 0, n);   // direct memory arrives holding what it last held
    return p;
}

void* __wrap_realloc(void* p, size_t n) {
    if (!p) return __wrap_malloc(n);
    if (!ours(p)) return __real_realloc(p, n);
    if (n == 0) { give(p); return 0; }
    size_t have = usable(p);
    if (n <= have && n >= have / 2) return p;
    void* q = __wrap_malloc(n);
    if (!q) return 0;
    memcpy(q, p, n < have ? n : have);
    give(p);
    return q;
}

int __wrap_posix_memalign(void** out, size_t align, size_t n) {
    if (align < sizeof(void*) || (align & (align - 1))) return EINVAL;
    if (!init()) {   // no range: the system heap, over-allocated by hand is not possible here
        void* p = __real_malloc(n);
        if (!p) return ENOMEM;
        if ((uintptr_t)p & (align - 1)) { __real_free(p); return ENOMEM; }
        *out = p;
        return 0;
    }
    void* p = take_aligned(align, n ? n : 1);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

void* __wrap_aligned_alloc(size_t align, size_t n) {
    void* p = 0;
    int e = __wrap_posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, n);
    if (e) { errno = e; return 0; }
    return p;
}

// What the heap holds, for the kernel log (fault_klog.c's memory lines).
size_t sf3_heap_committed(void) { return g_committed; }
size_t sf3_heap_used(void) { return g_used; }
