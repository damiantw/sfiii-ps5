// PC only: the host's frames in an SDL2 window (rendered offscreen with VkScene, then copied
// to an SDL texture). The console uses DisplayScreen. MIT, see LICENSE.
#pragma once
#include "platform_host.h"
#include <SDL.h>

namespace sf3::host {

class WindowScreen : public Screen {
public:
    WindowScreen(sf3ps5::VkScene& s, int w, int h) : off_(s, w, h), w_(w), h_(h) {}
    bool open(std::string& why);
    bool show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& o, std::string& why) override;
    int width() const override { return w_; }
    int height() const override { return h_; }
    VkFormat format() const override { return off_.format(); }
    uint32_t images() const override { return 2; }
    const std::vector<uint8_t>& rgba() const { return off_.rgba; }
    bool closed = false;   // the window's close button
    ~WindowScreen();
private:
    OffscreenScreen off_;
    int w_, h_;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
};

}  // namespace sf3::host
