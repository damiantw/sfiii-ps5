// sfiii-native's code-cache loader (native/recomp/gen/loader.h) for the console, in place of
// loader_posix.cpp: anonymous memory mapped RW, the code copied and relocated, then
// sceKernelMprotect to RX (plain mprotect as a fallback). Asking for exec at map time is refused
// on the PS5; this sequence ran on fw 13.590.001 in a payload (STATUS.md), not yet in a title.
// 16 KiB pages. MIT, see LICENSE.
#include "loader.h"
#include <cstring>
#include <sys/mman.h>

extern "C" int sceKernelMprotect(const void* addr, size_t len, int prot);
extern "C" volatile uintptr_t sf3_fault_gen_base, sf3_fault_gen_size;   // fault_klog.c

namespace sf3::gen {

bool load_cache(const Cache& c, const ServiceTable& svc, LoadedCode& out, std::string& why) {
    constexpr size_t page = 16384;
    size_t size = (c.code.size() + page - 1) / page * page;
    if (!size) return why = "empty cache", false;
    void* m = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) return why = "mmap RW failed", false;
    auto* b = static_cast<uint8_t*>(m);
    std::memcpy(b, c.code.data(), c.code.size());
    for (auto& r : c.relocs) {
        void* t = svc[size_t(r.target)];
        if (!t) { munmap(m, size); return why = "missing service", false; }
        uint64_t v;
        std::memcpy(&v, b + r.code_off, 8);
        v += reinterpret_cast<uint64_t>(t);
        std::memcpy(b + r.code_off, &v, 8);
    }
    if (sceKernelMprotect(m, size, PROT_READ | PROT_EXEC) != 0 && mprotect(m, size, PROT_READ | PROT_EXEC) != 0) {
        munmap(m, size);
        return why = "mprotect RX failed", false;
    }
    out.base = m, out.size = size, out.funcs.clear();
    sf3_fault_gen_base = reinterpret_cast<uintptr_t>(m), sf3_fault_gen_size = size;
    for (auto& e : c.funcs) out.funcs.push_back({e.addr, reinterpret_cast<SH2Func>(b + e.code_off)});
    return true;
}

void unload(LoadedCode& l) {
    if (l.base) munmap(l.base, l.size);
    l = {};
}

}  // namespace sf3::gen
