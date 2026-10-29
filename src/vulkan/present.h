// sfiii-ps5: where the game picture goes on the 4K screen (MIT).
//
// 4:3 (the default): the picture is scaled to the screen height and pillarboxed. The scale is an
// integer when one fits, so pixels stay square and sharp. 16:9: renderer-only widening. The
// renderer draws the extra columns that the game already composed outside the 4:3 window
// (Frame::origin_x / native_w), and nothing writes game memory. Without extra columns it falls back to 4:3.
#pragma once
#include <algorithm>
#include <cstdint>

namespace sf3ps5 {

enum class Aspect { Classic4x3, Wide16x9 };

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

// src_w x src_h: the game field (e.g. 320x224, 352x240, 704x480), shown as 4:3 on a CRT.
// extra_w: game-composed columns available on each side for 16:9 (0 if none).
inline Rect place(int screen_w, int screen_h, int src_w, int src_h, Aspect a, int extra_w = 0) {
    Rect r;
    if (screen_w <= 0 || screen_h <= 0 || src_w <= 0 || src_h <= 0) return r;
    // the 4:3 box at full height
    int box_h = screen_h, box_w = screen_h * 4 / 3;
    if (box_w > screen_w) box_w = screen_w, box_h = screen_w * 3 / 4;
    // prefer an integer vertical scale when it fills at least 90 % of the height
    int k = box_h / src_h;
    if (k >= 1 && k * src_h * 10 >= box_h * 9) box_h = k * src_h, box_w = box_h * 4 / 3;
    r.w = box_w, r.h = box_h;
    if (a == Aspect::Wide16x9 && extra_w > 0) {
        double px = double(box_w) / src_w;                    // screen pixels per source column
        int wide = int(box_w + 2 * extra_w * px + 0.5);
        r.w = std::min(wide, std::min(screen_w, box_h * 16 / 9));
    }
    r.x = (screen_w - r.w) / 2, r.y = (screen_h - r.h) / 2;
    return r;
}

}  // namespace sf3ps5
