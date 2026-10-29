// MIT, see LICENSE.
#include "pad_ps5.h"
#include "ps5/sce.h"

namespace sf3::menu {

PadState decode_sce_pad(uint32_t b, const uint8_t st[4], uint8_t l2, uint8_t r2) {
    using namespace sf3::ps5;
    PadState s;
    s.connected = true;
    s.up = b & kUp, s.down = b & kDown, s.left = b & kLeft, s.right = b & kRight;
    s.cross = b & kCross, s.circle = b & kCircle, s.square = b & kSquare, s.triangle = b & kTriangle;
    s.l1 = b & kL1, s.r1 = b & kR1, s.l3 = b & kL3, s.r3 = b & kR3;
    s.options = b & kOptions, s.touchpad = b & kTouchPad;
    auto ax = [](uint8_t v) { float f = (float(v) - 128.0f) / 127.0f; return f < -1 ? -1.0f : f > 1 ? 1.0f : f; };
    s.lx = ax(st[0]), s.ly = ax(st[1]), s.rx = ax(st[2]), s.ry = ax(st[3]);
    s.l2 = l2 / 255.0f, s.r2 = r2 / 255.0f;
    return s;
}

#if defined(SF3_PS5_CONSOLE)
PadState ScePad::read() {
    if (!init_) { sceUserServiceInitialize(nullptr); scePadInit(); init_ = true; }
    if (handle_ < 0) {
        int32_t user = -1;
        if (sceUserServiceGetInitialUser(&user) != 0) return {};
        handle_ = scePadOpen(user, ps5::kPadStandard, 0, nullptr);
        if (handle_ < 0) handle_ = scePadGetHandle(user, ps5::kPadStandard, 0);
        if (handle_ < 0) return {};
    }
    alignas(16) uint8_t data[512] = {};
    if (scePadReadState(handle_, data) != 0) { handle_ = -1; return {}; }
    auto* h = reinterpret_cast<const ps5::PadHead*>(data);
    const uint8_t st[4] = {h->lx, h->ly, h->rx, h->ry};
    return decode_sce_pad(h->buttons, st, h->l2, h->r2);
}

ScePad::~ScePad() { if (handle_ >= 0) scePadClose(handle_); }
#endif

}  // namespace sf3::menu
