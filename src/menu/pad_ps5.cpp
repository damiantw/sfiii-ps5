// MIT, see LICENSE.
#include "pad_ps5.h"
#include "ps5/sce.h"
#include <cstdio>

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
// Every logged-in user's controller and remote-control port: a Remote Play session's virtual
// pad is not the initial user's standard pad, so all of them are read and merged.
void ScePad::open_all() {
    int32_t users[4] = {-1, -1, -1, -1}, init_user = -1, fg = -1;
    int lr = sceUserServiceGetLoginUserIdList(users);
    sceUserServiceGetInitialUser(&init_user);
    sceUserServiceGetForegroundUser(&fg);
    std::vector<int32_t> list;
    auto add = [&](int32_t u) { if (u >= 0) { for (int32_t x : list) if (x == u) return; list.push_back(u); } };
    add(init_user), add(fg);
    for (int32_t u : users) add(u);
    std::string d = "pad: us " + std::to_string(us_rc_) + " login " + std::to_string(lr);
    char m[64];
    for (int32_t u : list)
        for (int32_t t : {ps5::kPadStandard, ps5::kPadRemote}) {
            int h = scePadOpen(u, t, 0, nullptr);
            if (h < 0) h = scePadGetHandle(u, t, 0);
            std::snprintf(m, sizeof m, "; u%d t%d %#x", u, t, unsigned(h));
            d += m;
            if (h >= 0) ports_.push_back({u, t, h});
        }
    diag_ = d;
}

PadState ScePad::read() {
    if (!init_) { us_rc_ = sceUserServiceInitialize(nullptr); pad_rc_ = scePadInit(); init_ = true; }
    if (ports_.empty()) {
        if (retry_++ % 60) return {};   // about once a second until a pad opens
        open_all();
        if (ports_.empty()) return {};
    }
    uint32_t b = 0;
    uint8_t st[4] = {128, 128, 128, 128}, l2 = 0, r2 = 0;
    bool any = false;
    for (size_t i = 0; i < ports_.size();) {
        alignas(16) uint8_t data[512] = {};
        if (scePadReadState(ports_[i].handle, data) != 0) { ++i; continue; }
        any = true;
        auto* h = reinterpret_cast<const ps5::PadHead*>(data);
        b |= h->buttons;
        if (h->buttons || h->l2 || h->r2 || h->lx < 100 || h->lx > 156 || h->ly < 100 || h->ly > 156)
            st[0] = h->lx, st[1] = h->ly, st[2] = h->rx, st[3] = h->ry;
        l2 = l2 > h->l2 ? l2 : h->l2, r2 = r2 > h->r2 ? r2 : h->r2;
        ++i;
    }
    if (!any) { for (auto& p : ports_) scePadClose(p.handle); ports_.clear(); retry_ = 0; return {}; }
    if (b != last_buttons_) {
        char m[48];
        std::snprintf(m, sizeof m, "pad: %zu ports, buttons %#x", ports_.size(), b);
        diag_ = m, last_buttons_ = b;
    }
    return decode_sce_pad(b, st, l2, r2);
}

ScePad::~ScePad() { for (auto& p : ports_) scePadClose(p.handle); }
#endif

}  // namespace sf3::menu
