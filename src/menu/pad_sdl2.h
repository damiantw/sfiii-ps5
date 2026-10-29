// SDL2 GameController -> PadState (PC, and the PS5's SDL2 port). MIT, see LICENSE.
#pragma once
#include "pad.h"
#include <SDL.h>

namespace sf3::menu {

// The first game controller, opened on first use and reopened after a disconnect.
class SdlPad {
public:
    PadState read();   // call after SDL_PumpEvents / SDL_GameControllerUpdate
    ~SdlPad();
private:
    SDL_GameController* gc_ = nullptr;
};

PadState read_pad(SDL_GameController* gc);

}  // namespace sf3::menu
