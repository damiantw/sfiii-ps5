// sfiii-ps5: the runtime side of the dev tools (MIT).
//
// The saturn-recomp runtime calls agent_poll() at every VBlank-IN (machine.cpp). The PS5 build
// leaves out the runtime's machine/agent.cpp, which needs a socket fd passed down by a parent
// process (there is none on the console) and links this file instead:
//   * dev build (SF3_PS5_DEVTOOLS): the first call reads /data/sfiii/devtools.enable, starts the
//     debug server with hooks into the runtime, and every call then runs devtools_poll().
//   * release build: agent_poll() is empty, and the server code is not linked at all.
// The same presses at the same VBlanks still replay exactly as with --agent.
#include "saturn.h"
#include "video.h"
#if SF3_PS5_DEVTOOLS
#include "debug_server.h"
#include <cstdlib>

#ifndef SF3_PS5_DATA_DIR
#define SF3_PS5_DATA_DIR "/data/sfiii"
#endif

namespace {
bool g_started = false;

uint64_t hook_vblanks() { return sat_vblanks(); }
uint8_t hook_ld8(uint32_t a) { return uint8_t(ld8(a)); }
void hook_st8(uint32_t a, uint8_t v) { st8(a, v); }
bool hook_press(const std::string& s, std::string& e) { return smpc_press(s, e); }
bool hook_frame(uint32_t& w, uint32_t& h, std::string& rgb) {
    static Frame f;
    vdp2_compose(f);
    if (f.w <= 0 || f.h <= 0) return false;
    w = uint32_t(f.w), h = uint32_t(f.h);
    rgb.resize(f.px.size() * 3);
    for (size_t i = 0; i < f.px.size(); ++i) rgb[3 * i] = char(f.px[i] >> 16), rgb[3 * i + 1] = char(f.px[i] >> 8), rgb[3 * i + 2] = char(f.px[i]);
    return true;
}
bool hook_save(const std::string& path, std::string& e) {
    if (!std::getenv("SF3_STATE_ID")) return e = "save needs SF3_STATE_ID (the executable/disc identity)", false;
    try { state_request_save(path); return true; } catch (const std::exception& x) { e = x.what(); return false; }
}
bool hook_load(const std::string&, std::string& e) {
    // the runtime loads a native state only at start-up (--state-in). A live load at a safe point is a
    // fork-side change (state.cpp), planned; until then the client restarts the title with --state-in.
    e = "load not supported yet (restart with --state-in)";
    return false;
}
std::string hook_save_status() { return std::string(state_save_busy() ? "busy " : "done ") + std::to_string(state_write_result()); }
std::string hook_info() { return "sfiii-ps5 PPSA93303 devtools"; }

void start() {
    g_started = true;
    DevConfig c = devtools_read_config(SF3_PS5_DATA_DIR "/devtools.enable");
    if (!c.enabled) return;
    DevHooks h{};
    h.vblanks = hook_vblanks, h.ld8 = hook_ld8, h.st8 = hook_st8, h.press = hook_press, h.frame = hook_frame;
    h.save_state = hook_save, h.load_state = hook_load, h.save_status = hook_save_status, h.info = hook_info;
    devtools_start(c, h);
}
}  // namespace

void agent_poll() {
    if (!g_started) start();
    devtools_poll();
}
#else
void agent_poll() {}
#endif
