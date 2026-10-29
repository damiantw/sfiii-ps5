// A DualSense (PadState) as a Saturn pad, sfiii-native's default layout (native/controllers.h):
// Square A, Cross B, Circle C, Triangle X, L2 Y, R2 Z, L1 L, R1 R, Options START, d-pad or
// left stick. Two buttons are shared with the menu here:
//  - the touchpad opens the menu in game; Options is the game's START (in the launcher, where
//    there is no game, Options opens and closes the menu too);
//  - with hold-to-fast-forward on, R2 is fast-forward and Z moves to R3 (the right stick click).
// MIT, see LICENSE.
#pragma once
#include "menu/pad.h"
#include <cstdint>

namespace sf3::host {

enum : uint16_t {   // the runtime's bits (host.h), as SMPC reads them
    S_RIGHT = 0x8000, S_LEFT = 0x4000, S_DOWN = 0x2000, S_UP = 0x1000, S_START = 0x0800, S_A = 0x0400,
    S_C = 0x0200, S_B = 0x0100, S_R = 0x0080, S_X = 0x0040, S_Y = 0x0020, S_Z = 0x0010, S_L = 0x0008,
};

uint16_t saturn_buttons(const menu::PadState& p, bool r2_is_fast_forward);

}  // namespace sf3::host
