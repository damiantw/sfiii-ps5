// libScePad -> PadState (the console). MIT, see LICENSE.
#pragma once
#include "pad.h"
#include <cstdint>

namespace sf3::menu {

// The initial user's controller, opened on first use and reopened after a failed read.
class ScePad {
public:
    PadState read();
    ~ScePad();
private:
    int32_t handle_ = -1;
    bool init_ = false;
};

// The decode on its own (tests): the leading ScePadData fields and the "connected" flag.
PadState decode_sce_pad(uint32_t buttons, const uint8_t sticks[4], uint8_t l2, uint8_t r2);

}  // namespace sf3::menu
