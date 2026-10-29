// MIT, see LICENSE.
#include "script.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace sf3::host {

bool press(menu::PadState& p, const std::string& buttons) {
    std::stringstream ss(buttons);
    std::string b;
    bool ok = true;
    while (std::getline(ss, b, '+')) {
        if (b == "cross") p.cross = true; else if (b == "circle") p.circle = true;
        else if (b == "square") p.square = true; else if (b == "triangle") p.triangle = true;
        else if (b == "options") p.options = true; else if (b == "touchpad") p.touchpad = true;
        else if (b == "l1") p.l1 = true; else if (b == "r1") p.r1 = true;
        else if (b == "l2") p.l2 = 1; else if (b == "r2") p.r2 = 1; else if (b == "r3") p.r3 = true;
        else if (b == "up") p.up = true; else if (b == "down") p.down = true;
        else if (b == "left") p.left = true; else if (b == "right") p.right = true;
        else ok = false;
    }
    return ok;
}

bool Script::load(const std::string& path, int gen, std::string& why) {
    std::ifstream f(path);
    if (!f) return why = "cannot read " + path, false;
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        ScriptStep s;
        if (!(ss >> s.gen >> s.tick >> s.op)) return why = path + ":" + std::to_string(n) + ": GEN TICK OP", false;
        std::getline(ss >> std::ws, s.arg);
        if (s.op == "press") {
            size_t sp = s.arg.find(' ');
            if (sp != std::string::npos) s.hold = std::stoi(s.arg.substr(sp + 1)), s.arg = s.arg.substr(0, sp);
            menu::PadState t;
            if (!press(t, s.arg)) return why = path + ":" + std::to_string(n) + ": unknown button in " + s.arg, false;
        }
        if (s.gen == gen) steps_.push_back(s);
    }
    std::stable_sort(steps_.begin(), steps_.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
    return true;
}

std::vector<ScriptStep> Script::tick(menu::PadState& p) {
    ++tick_;
    std::vector<ScriptStep> due;
    while (next_ < steps_.size() && steps_[next_].tick <= tick_) {
        auto& s = steps_[next_++];
        if (s.op == "press") held_.push_back({s.arg, tick_ + s.hold});
        else due.push_back(s);
    }
    held_.erase(std::remove_if(held_.begin(), held_.end(), [&](auto& h) { return h.until <= tick_; }), held_.end());
    for (auto& h : held_) press(p, h.buttons);
    return due;
}

}  // namespace sf3::host
