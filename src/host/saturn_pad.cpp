// MIT, see LICENSE.
#include "saturn_pad.h"

namespace sf3::host {

uint16_t saturn_buttons(const menu::PadState& p, bool ff) {
    if (!p.connected) return 0;
    uint16_t b = 0;
    if (p.up || p.ly < -0.5f) b |= S_UP;
    if (p.down || p.ly > 0.5f) b |= S_DOWN;
    if (p.left || p.lx < -0.5f) b |= S_LEFT;
    if (p.right || p.lx > 0.5f) b |= S_RIGHT;
    if ((b & (S_LEFT | S_RIGHT)) == (S_LEFT | S_RIGHT)) b &= ~(S_LEFT | S_RIGHT);   // as controllers.cpp: no opposite pair
    if ((b & (S_UP | S_DOWN)) == (S_UP | S_DOWN)) b &= ~(S_UP | S_DOWN);
    if (p.options) b |= S_START;
    if (p.square) b |= S_A;
    if (p.cross) b |= S_B;
    if (p.circle) b |= S_C;
    if (p.triangle) b |= S_X;
    if (p.l2 > 0.5f) b |= S_Y;
    if (ff ? p.r3 : p.r2 > 0.5f) b |= S_Z;
    if (p.l1) b |= S_L;
    if (p.r1) b |= S_R;
    return b;
}

}  // namespace sf3::host
