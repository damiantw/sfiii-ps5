// sfiii-ps5 dev tools: a network debug server (dev builds only).
//
// It speaks the saturn-recomp --agent protocol (runtime/src/machine/agent.cpp),
// one text command a line, so tools/sf3-story.py and sf3test drive a console run
// the way they drive a desktop run. Added for the console: ping, auth, info,
// pause/run (attach to a free-running game), load, log, detach. See docs/devtools.md.
//
// Release builds do not compile this file (CMake SF3_PS5_DEVTOOLS=OFF), and even
// in a dev build nothing listens unless /data/sfiii/devtools.enable exists.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct DevHooks {                       // what the server needs from the game (the runtime fills it in)
    uint64_t (*vblanks)();
    uint8_t (*ld8)(uint32_t addr);
    void (*st8)(uint32_t addr, uint8_t v);
    bool (*press)(const std::string& spec, std::string& error);         // smpc_press
    bool (*frame)(uint32_t& w, uint32_t& h, std::string& rgb);          // composed field, RGB
    bool (*save_state)(const std::string& path, std::string& error);    // at the next safe point
    bool (*load_state)(const std::string& path, std::string& error);
    std::string (*save_status)();                                       // "busy|done RESULT"
    std::string (*load_status)();                                       // "busy|done RESULT" (1 loaded, -1 timed out)
    std::string (*info)();                                              // disc, build, title id
};

struct DevConfig {
    bool enabled = false;
    int port = 9200;   // a title may bind 9200 (9300 and 9301 give EACCES), as Shipwright's devtools do
    std::string token;                  // empty: no auth
    std::string bind = "0.0.0.0";
};

DevConfig devtools_read_config(const char* enable_path);   // key=value lines; missing file: disabled
bool devtools_start(const DevConfig&, const DevHooks&);    // opens the listener (non-blocking)
void devtools_poll();                                      // call once per VBlank-IN on the emu thread
void devtools_log(const char* line);                       // ring buffer for "log N" (thread-safe)
void devtools_stop();
