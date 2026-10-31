// MIT, see LICENSE.
#include <cstdio>
#include <algorithm>
#include "platform_host.h"
#include "../vulkan/geometry_frag.inc"
#include <cstdio>
#include <iterator>
#include "saturn_pad.h"
#include "../vulkan/display.h"
#include "imgui.h"
#include <thread>

namespace sf3::host {

bool DisplayScreen::show(sf3ps5::Rect r, bool nearest, const sf3ps5::Overlay& o, std::string& why) { return d_.frame(r, nearest, why, o); }
int DisplayScreen::width() const { return d_.width(); }
int DisplayScreen::height() const { return d_.height(); }
VkFormat DisplayScreen::format() const { return d_.format(); }
uint32_t DisplayScreen::images() const { return d_.image_count(); }

PlatformHost::PlatformHost(sf3ps5::VkScene& scene, Screen& screen, menu::Menu& menu, menu::Settings& settings,
                           runtime::RuntimeHost& rt, PadReader pad, AudioOut* audio)
    : scene_(scene), screen_(screen), menu_(menu), settings_(settings), rt_(rt), read_(std::move(pad)), audio_(audio) {
    sleep = [](Pacer::Clock::duration d) { std::this_thread::sleep_for(d); };
}

bool PlatformHost::init(std::string& why) {
    if (!renderer_.init(scene_, screen_.format(), screen_.images() < 2 ? 2 : screen_.images(), why)) return false;
    // Until the game shows a field (the launcher), the picture is one black pixel.
    uint32_t black = 0;
    if (!scene_.upload_picture(&black, 1, 1, why)) return false;
    std::string gwhy;
    geometry_ok_ = scene_.load_fragment(std::vector<uint32_t>(std::begin(kGeometryFrag), std::end(kGeometryFrag)), gwhy);
    if (!geometry_ok_) std::fprintf(stderr, "geometry shader: %s; 3D stays 1x\n", gwhy.c_str());
    src_w_ = src_h_ = 0;
    return true;
}

sf3ps5::Rect PlatformHost::rect() const {
    if (src_w_ <= 0) return {};   // no field yet: black screen, the menu on top
    auto a = settings_.aspect == menu::Aspect::Ratio16x9 ? sf3ps5::Aspect::Wide16x9 : sf3ps5::Aspect::Classic4x3;
    auto r = sf3ps5::place(screen_.width(), screen_.height(), src_w_ - 2 * extra_, src_h_, a, extra_);
    r.src_x0 *= scale_, r.src_x1 *= scale_;   // place() counts field columns; a GPU pass has scale_ per column
    return r;
}

void PlatformHost::input() {
    last_pad_ = read_ ? read_() : menu::PadState{};
    last_read_ = last_vblank_read_ = Pacer::Clock::now();
    ImGuiIO& io = ImGui::GetIO();
    bool in_game = menu_.in_game();
    // In game, Options is START and only the touchpad opens the menu; once open, either closes it.
    menu::PadEvents ev = nav_.feed(last_pad_, menu_.visible(), &io, !in_game || menu_.visible());
    menu_.handle(ev);
    rt_.frame(menu_, ev.fast_forward && settings_.fast_forward_hold);
    auto now = Pacer::Clock::now();
    pacer_.set_speed(rt_.speed(), now);
    if (audio_) audio_->set_speed(rt_.speed()), audio_->pause(rt_.paused());
    if (was_visible_ && !menu_.visible()) pacer_.restart();   // back from the menu: no catching up
    was_visible_ = menu_.visible();
    if (menu_.status_text() != last_toast_) {   // a save/load result: show it briefly in game too
        last_toast_ = menu_.status_text();
        if (!last_toast_.empty()) toast_until_ = now + std::chrono::seconds(3);
    }
}

bool PlatformHost::ui_frame() {
    input();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(screen_.width()), float(screen_.height()));
    auto now = Pacer::Clock::now();
    float dt = last_ui_.time_since_epoch().count() ? std::chrono::duration<float>(now - last_ui_).count() : 1.0f / 60;
    io.DeltaTime = dt > 0 && dt < 1 ? dt : 1.0f / 60;
    last_ui_ = now;
    bool toast = !menu_.visible() && now < toast_until_;
    ImGui::NewFrame();
    menu_.draw();
    if (toast) {
        ImGui::GetStyle().FontScaleMain = std::max(1.0f, settings_.ui_scale);   // same size as the menu text
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.03f, io.DisplaySize.y * 0.92f), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextUnformatted(last_toast_.c_str());
        ImGui::End();
    }
    ImGui::Render();
    bool overlay = menu_.visible() || toast;
    if (!screen_.show(rect(), settings_.sharp_pixels, overlay ? renderer_.overlay() : sf3ps5::Overlay(), error_)) return false;
    ++shown_;
    return true;
}

void PlatformHost::present(const uint32_t* px, int w, int h, int origin_x, int native_w) {
    if (scene_.upload_picture(px, w, h, error_)) {
        src_w_ = w, src_h_ = h, scale_ = 1;
        extra_ = native_w > 0 && native_w < w ? origin_x : 0;
        last_data_.clear();
    }
    shown();
}

bool PlatformHost::present_scene(const std::vector<uint32_t>& data, const std::vector<uint32_t>& tex, int w, int h, int scale,
                                 int origin_x, int native_w) {
    // an unchanged field (30 Hz pictures, menus) keeps the last pass
    const bool same = scale_ == scale && src_w_ == w && src_h_ == h && data == last_data_ && tex == last_tex_;
    if (!same) {
        if (!scene_.upload(data, tex, error_) || !scene_.draw(w * scale, h * scale, 0, false, error_)) {
            last_data_.clear();
            return false;
        }
        last_data_ = data, last_tex_ = tex;
    }
    src_w_ = w, src_h_ = h, scale_ = scale;
    extra_ = native_w > 0 && native_w < w ? origin_x : 0;
    shown();
    return true;
}

bool PlatformHost::read_scene(std::vector<uint8_t>& rgba, int& w, int& h) {
    if (scale_ < 2 || !scene_.read_rgba(rgba, error_)) return false;
    w = src_w_ * scale_, h = src_h_ * scale_;
    return true;
}

void PlatformHost::shown() {
    presented_ = ui_frame();
    // The menu is open over the game: the machine waits here until it closes (or a launch/quit
    // begins). Display FIFO paces this loop at the TV's rate.
    while (menu_.visible() && menu_.in_game() && !stopping_) {
        if (on_menu_frame) on_menu_frame();
        if (!ui_frame()) break;
        if (menu_frame_floor.count() && sleep) sleep(menu_frame_floor);
    }
}

void PlatformHost::poll() {
    // Not drawn: keep the pad and the menu's toggle live so 3x stays responsive.
    input();
    if (menu_.visible() && menu_.in_game() && src_w_ > 0)
        while (menu_.visible() && menu_.in_game() && !stopping_) {
            if (on_menu_frame) on_menu_frame();
            if (!ui_frame()) break;
        }
}

void PlatformHost::pace(uint64_t now_ns) {
    if (vsync_paced && presented_) { presented_ = false; pacer_.follow(now_ns, Pacer::Clock::now()); return; }
    auto d = pacer_.wait(now_ns, Pacer::Clock::now());
    if (d.count() > 0 && sleep) sleep(d);
}

bool PlatformHost::wants_frame(uint64_t vblanks) const {
    int s = pacer_.speed();
    return s <= 1 || vblanks % uint64_t(s) == 0;
}

uint16_t PlatformHost::pad(int slot) {
    if (slot != 0 || menu_.visible()) return 0;
    // The game samples the pad mid-field (SMPC INTBACK); read it again here rather than use the
    // read from the last VBlank, up to a field old. Menu navigation still runs from input().
    auto now = Pacer::Clock::now();
    // How old the VBlank read is when the game samples it, and how old the read the game gets:
    // stderr "pad: ..." every 600 samples (the latency gained by the fresh read).
    double vb_age = std::chrono::duration<double, std::milli>(now - last_vblank_read_).count();
    if (!no_fresh_pad && read_ && now - last_read_ > std::chrono::milliseconds(2)) {
        menu::PadState p = read_();
        last_pad_ = p, last_read_ = now;
    }
    age_vb_ += vb_age, age_used_ += std::chrono::duration<double, std::milli>(now - last_read_).count();
    if (++age_n_ == 600) {
        std::fprintf(stderr, "pad: game sample age avg %.1f ms (VBlank read %.1f ms)%s\n", age_used_ / 600, age_vb_ / 600,
                     no_fresh_pad ? " [fresh read off]" : "");
        age_n_ = 0, age_vb_ = age_used_ = 0;
    }
    return saturn_buttons(last_pad_, settings_.fast_forward_hold);
}

void PlatformHost::audio(const int16_t* lr, int frames) {
    if (audio_) audio_->push(lr, frames);
}

}  // namespace sf3::host
