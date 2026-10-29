// Gamepad input for the menu, backend-neutral: the platform fills PadState (SDL2's
// GameController on PC and on the PS5's SDL2), PadNav turns it into ImGui gamepad
// navigation plus the menu's own buttons. Shipwright-style: the touchpad (or Options) toggles
// the menu, L1/R1 switch tabs, Cross accepts, Circle backs out. MIT, see LICENSE.
#pragma once

struct ImGuiIO;

namespace sf3::menu {

struct PadState {
    bool up = false, down = false, left = false, right = false;
    bool cross = false, circle = false, square = false, triangle = false;
    bool l1 = false, r1 = false, l3 = false, r3 = false;
    bool options = false, create = false, touchpad = false, ps = false;
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1, +y down
    float l2 = 0, r2 = 0;                   // 0..1
    bool connected = false;
};

struct PadEvents {
    bool toggle_menu = false;   // touchpad or Options pressed
    int tab = 0;                // -1 L1, +1 R1 (pressed this frame)
    bool fast_forward = false;  // R2 held (only while the menu is closed)
};

class PadNav {
public:
    // Feeds ImGui (io may be null) while the menu is visible; returns the menu-level events.
    PadEvents feed(const PadState& s, bool menu_visible, ImGuiIO* io);
private:
    PadState prev_;
};

// Shipwright-like look and ImGui flags (gamepad navigation, amber focus, no .ini file).
void setup_imgui(ImGuiIO& io);

}  // namespace sf3::menu
