// MIT, see LICENSE.
#include "pad_sdl2.h"

namespace sf3::menu {

PadState read_pad(SDL_GameController* gc) {
    PadState s;
    if (!gc || !SDL_GameControllerGetAttached(gc)) return s;
    s.connected = true;
    auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(gc, x) != 0; };
    auto a = [&](SDL_GameControllerAxis x) { return float(SDL_GameControllerGetAxis(gc, x)) / 32767.0f; };
    s.up = b(SDL_CONTROLLER_BUTTON_DPAD_UP), s.down = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    s.left = b(SDL_CONTROLLER_BUTTON_DPAD_LEFT), s.right = b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    // SDL names the positions: A = bottom (Cross), B = right (Circle), X = left (Square), Y = top (Triangle).
    s.cross = b(SDL_CONTROLLER_BUTTON_A), s.circle = b(SDL_CONTROLLER_BUTTON_B);
    s.square = b(SDL_CONTROLLER_BUTTON_X), s.triangle = b(SDL_CONTROLLER_BUTTON_Y);
    s.l1 = b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), s.r1 = b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    s.l3 = b(SDL_CONTROLLER_BUTTON_LEFTSTICK), s.r3 = b(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    s.options = b(SDL_CONTROLLER_BUTTON_START), s.create = b(SDL_CONTROLLER_BUTTON_BACK);
    s.touchpad = b(SDL_CONTROLLER_BUTTON_TOUCHPAD), s.ps = b(SDL_CONTROLLER_BUTTON_GUIDE);
    s.lx = a(SDL_CONTROLLER_AXIS_LEFTX), s.ly = a(SDL_CONTROLLER_AXIS_LEFTY);
    s.rx = a(SDL_CONTROLLER_AXIS_RIGHTX), s.ry = a(SDL_CONTROLLER_AXIS_RIGHTY);
    s.l2 = a(SDL_CONTROLLER_AXIS_TRIGGERLEFT), s.r2 = a(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    return s;
}

PadState SdlPad::read() {
    if (gc_ && !SDL_GameControllerGetAttached(gc_)) { SDL_GameControllerClose(gc_); gc_ = nullptr; }
    if (!gc_)
        for (int i = 0; i < SDL_NumJoysticks() && !gc_; i++)
            if (SDL_IsGameController(i)) gc_ = SDL_GameControllerOpen(i);
    return read_pad(gc_);
}

SdlPad::~SdlPad() { if (gc_) SDL_GameControllerClose(gc_); }

}  // namespace sf3::menu
