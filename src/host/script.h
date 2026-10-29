// PC test driver for the host (SF3_PS5_SCRIPT=file): pad presses, menu actions and screenshots
// at given host ticks, so a full run with the game can be checked without a person. One line each:
//   GEN TICK press BUTTON[+BUTTON...] [TICKS]   held for TICKS (default 2)
//   GEN TICK save|load SLOT | quit | launch IMAGE | menu | shot FILE.ppm | exit
// GEN is the process generation (0, then +1 at each relaunch; SF3_PS5_GEN). TICK counts host
// ticks (input polls: shown fields, skipped fields, menu frames) in that process. Buttons:
// cross circle square triangle options touchpad l1 r1 l2 r2 r3 up down left right.
// Not part of the console build. MIT, see LICENSE.
#pragma once
#include "menu/pad.h"
#include <string>
#include <vector>

namespace sf3::host {

struct ScriptStep { int gen = 0; long tick = 0; std::string op, arg; int hold = 2; };

class Script {
public:
    bool load(const std::string& path, int gen, std::string& why);
    // Next tick: adds held buttons to p; returns the actions due now (press is applied here).
    std::vector<ScriptStep> tick(menu::PadState& p);
    long now() const { return tick_; }
    bool done() const { return next_ >= steps_.size() && held_.empty(); }
private:
    struct Held { std::string buttons; long until; };
    std::vector<ScriptStep> steps_;
    std::vector<Held> held_;
    size_t next_ = 0;
    long tick_ = -1;
};

bool press(menu::PadState& p, const std::string& buttons);   // false: an unknown name

}  // namespace sf3::host
