// Linux test: a fake machine (64 KiB RAM, a counter as the VBlank clock) behind the debug server.
#include "debug_server.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
static uint64_t vb = 0;
static uint8_t ram[0x10000];
static std::string pads;
int main(int argc, char** argv) {
    DevConfig c;
    c.enabled = true, c.port = argc > 1 ? std::atoi(argv[1]) : 9300, c.bind = "127.0.0.1";
    DevHooks h{};
    h.vblanks = [] { return vb; };
    h.ld8 = [](uint32_t a) { return ram[a & 0xFFFF]; };
    h.st8 = [](uint32_t a, uint8_t v) { ram[a & 0xFFFF] = v; };
    h.press = [](const std::string& s, std::string&) { pads = s; return true; };
    h.frame = [](uint32_t& w, uint32_t& hh, std::string& rgb) { w = 4, hh = 2; rgb.assign(24, char(vb)); return true; };
    h.save_state = [](const std::string&, std::string&) { return true; };
    h.load_state = [](const std::string&, std::string& e) { e = "no such state"; return false; };
    h.save_status = [] { return std::string("done 0"); };
    h.info = [] { return std::string("fake PPSA93303"); };
    if (!devtools_start(c, h)) return 1;
    for (;; ++vb) {
        ram[0] = uint8_t(vb);              // "game" writes a byte every frame
        devtools_poll();
        usleep(1000);
        if (vb > 200000) return 0;
    }
}
