// The PS5 host behind the runtime's host_* calls (host_runtime.cpp), without the runtime itself,
// so the box tests it with synthetic frames on lavapipe:
//  - present: the game field goes to the screen (VkScene::upload_picture + present_to, placed
//    with place() and the aspect setting), the menu drawn over it while open, and the field kept
//    for redraws. With the menu open in game, present runs the menu until it closes (the machine
//    thread waits there; RuntimeHost pauses the stall watch meanwhile).
//  - pace / wants_frame: the speed setting (and R2 held) on the host clock; at 2x/3x only every
//    2nd/3rd field is shown, and the others still poll the pad and the menu.
//  - pad: the DualSense as a Saturn pad (saturn_pad.h); nothing reaches the game with the menu up.
//  - audio: AudioOut (48 kHz), at the same speed.
// Runs on the machine thread (host_present, host_pace, host_pad and host_audio_push all come from
// it), or on the main thread in the launcher. MIT, see LICENSE.
#pragma once
#include "audio_sdl2.h"
#include "pacer.h"
#include "menu/menu.h"
#include "menu/pad.h"
#include "runtime/runtime_host.h"
#include "../vulkan/menu_vk.h"
#include "../vulkan/present.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sf3ps5 { class Display; }

namespace sf3::host {

// Where frames go: the TV (Display) or memory (tests).
class Screen {
public:
    virtual ~Screen() = default;
    virtual bool show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& overlay, std::string& why) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual VkFormat format() const = 0;
    virtual uint32_t images() const = 0;
};

class DisplayScreen : public Screen {
public:
    explicit DisplayScreen(sf3ps5::Display& d) : d_(d) {}
    bool show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& o, std::string& why) override;
    int width() const override;
    int height() const override;
    VkFormat format() const override;
    uint32_t images() const override;
private:
    sf3ps5::Display& d_;
};

class OffscreenScreen : public Screen {
public:
    OffscreenScreen(sf3ps5::VkScene& s, int w, int h) : s_(s), w_(w), h_(h) {}
    bool show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& o, std::string& why) override {
        last_rect = r;
        return s_.present_offscreen(w_, h_, r, nearest, rgba, why, o);
    }
    int width() const override { return w_; }
    int height() const override { return h_; }
    VkFormat format() const override { return sf3ps5::VkScene::kOffscreenFormat; }
    uint32_t images() const override { return 2; }
    std::vector<uint8_t> rgba;   // the last frame, RGBA, top row first
    sf3ps5::Rect last_rect;
private:
    sf3ps5::VkScene& s_;
    int w_, h_;
};

class PlatformHost {
public:
    using PadReader = std::function<menu::PadState()>;
    // ImGui's context must exist (setup_imgui done). audio may be null (no sound).
    PlatformHost(sf3ps5::VkScene& scene, Screen& screen, menu::Menu& menu, menu::Settings& settings,
                 runtime::RuntimeHost& rt, PadReader pad, AudioOut* audio);
    bool init(std::string& why);
    void shutdown() { renderer_.shutdown(); }   // before ImGui::DestroyContext

    // host_present: one game field, 0x00RRGGBB, top row first. origin_x/native_w: the 4:3 window
    // inside a wider field (16:9), as the runtime's Frame has them.
    void present(const uint32_t* px, int w, int h, int origin_x, int native_w);
    void pace(uint64_t now_ns);                  // host_pace
    bool wants_frame(uint64_t vblanks) const;    // host_wants_frame
    void poll();                                 // a field that is not shown: pad and menu only
    uint16_t pad(int slot);                      // host_pad
    bool pad_connected(int slot) const { return slot == 0 && last_pad_.connected; }
    void audio(const int16_t* lr, int frames);   // host_audio_push

    // One menu frame (the launcher, or the paused game): input, menu, draw over the last field.
    bool ui_frame();
    // Stop running the menu loop (a launch or quit is under way).
    void stop() { stopping_ = true; }
    bool stopping() const { return stopping_; }

    const std::string& error() const { return error_; }
    int frames_shown() const { return shown_; }
    int speed() const { return pacer_.speed(); }
    std::function<void()> on_menu_frame;   // tests: drive the pad inside the in-game menu loop
    std::function<void(Pacer::Clock::duration)> sleep;   // default std::this_thread::sleep_for

private:
    void input();
    sf3ps5::Rect rect() const;
    sf3ps5::VkScene& scene_;
    Screen& screen_;
    menu::Menu& menu_;
    menu::Settings& settings_;
    runtime::RuntimeHost& rt_;
    PadReader read_;
    AudioOut* audio_;
    sf3ps5::MenuRenderer renderer_;
    menu::PadNav nav_;
    menu::PadState last_pad_;
    Pacer pacer_;
    int src_w_ = 0, src_h_ = 0, extra_ = 0;
    bool stopping_ = false, was_visible_ = false;
    int shown_ = 0;
    std::string error_, last_toast_;
    Pacer::Clock::time_point toast_until_{}, last_ui_{};
};

}  // namespace sf3::host
