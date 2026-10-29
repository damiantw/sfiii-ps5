// libScePad -> PadState (the console). MIT, see LICENSE.
#pragma once
#include "pad.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sf3::menu {

// The initial user's controller, opened on first use and reopened after a failed read.
class ScePad {
public:
    PadState read();
    ~ScePad();
    // The last open/read result or button change, for the launcher log and the debug server.
    const std::string& diag() const { return diag_; }
private:
    void open_all();
    struct Port { int32_t user, type, handle; };
    std::vector<Port> ports_;   // every logged-in user's standard pad and remote-control port
    bool init_ = false;
    int us_rc_ = 0, pad_rc_ = 0, retry_ = 0;
    uint32_t last_buttons_ = ~0u;
    std::string diag_;
};

// The decode on its own (tests): the leading ScePadData fields and the "connected" flag.
PadState decode_sce_pad(uint32_t buttons, const uint8_t sticks[4], uint8_t l2, uint8_t r2);

}  // namespace sf3::menu
