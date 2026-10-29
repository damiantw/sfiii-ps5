// MIT, see LICENSE.
#include "window_screen.h"

namespace sf3::host {

bool WindowScreen::open(std::string& why) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return why = SDL_GetError(), false;
    win_ = SDL_CreateWindow("Shining Force III (sfiii-ps5 host, PC)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w_, h_, SDL_WINDOW_RESIZABLE);
    if (!win_) return why = SDL_GetError(), false;
    ren_ = SDL_CreateRenderer(win_, -1, 0);
    if (!ren_) return why = SDL_GetError(), false;
    SDL_RenderSetLogicalSize(ren_, w_, h_);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w_, h_);
    return tex_ ? true : (why = SDL_GetError(), false);
}

bool WindowScreen::show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& o, std::string& why) {
    if (!off_.show(r, nearest, o, why)) return false;
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) closed = true;
    SDL_PumpEvents();
    SDL_UpdateTexture(tex_, nullptr, off_.rgba.data(), w_ * 4);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    SDL_RenderPresent(ren_);
    return true;
}

WindowScreen::~WindowScreen() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
}

}  // namespace sf3::host
