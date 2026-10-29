// The runtime's host_* calls on PlatformHost, and the app's main (instead of the runtime's
// main.cpp): the launcher menu without a game, or one disc per process (relaunch.h).
// Compiled against a patched runtime checkout (SF3_PS5_RUNTIME_DIR); the console build links it
// with the runtime and the generated code. MIT, see LICENSE.
#include "platform_host.h"
#include "relaunch.h"
#include "menu/jobs.h"
#include "menu/pad_sdl2.h"
#include "../vulkan/display.h"
#include "imgui.h"
#include "saturn.h"
#include "host/host.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

#ifndef SF3_PS5_BUILD_ID
#define SF3_PS5_BUILD_ID "dev"
#endif

using namespace sf3;

namespace {
host::PlatformHost* g_host = nullptr;
host::AudioOut* g_audio = nullptr;
bool g_have_audio = false;
host::AppArgs g_next;           // what to start after this run
bool g_relaunch = false;
std::string g_launch_cue;       // chosen in the launcher (no game running yet)

struct Call {                   // the stall report's "in host call ..."
    explicit Call(const char* n) { g_host_call = n; }
    ~Call() { g_host_call = ""; }
};
}

bool host_launch(SaturnConfig&) { return false; }   // the app's main runs the launcher itself
bool host_open() { return g_host != nullptr; }

void host_present(const Frame& f) {
    Call c("present");
    g_host->present(f.px.data(), f.w, f.h, f.origin_x, f.native_w);
}
void host_pace(uint64_t now) { Call c("pace"); g_host->pace(now); }
bool host_wants_frame() {
    if (!g_host) return false;
    bool want = g_host->wants_frame(sat_vblanks());
    if (!want) g_host->poll();
    return want;
}
uint16_t host_pad(int slot) { return g_host ? g_host->pad(slot) : 0; }
bool host_pad_connected(int slot) { return g_host && g_host->pad_connected(slot); }
bool host_audio_open() { return g_have_audio; }
void host_audio_push(const int16_t* lr, int frames) { if (g_host) g_host->audio(lr, frames); }
void host_audio_report() {
    if (!g_audio) return;
    auto s = g_audio->stats();
    std::fprintf(stderr, "audio: 48 kHz, %d underruns, %d chunks dropped\n", s.underruns, s.dropped);
}

int main(int argc, char** argv) {
    host::AppArgs args = host::AppArgs::parse(argc, argv);
    std::string why;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }

    menu::Settings settings;
    const char* dd = std::getenv("SF3_PS5_DATA");
    if (dd) settings.data_dir = dd;
    std::string settings_path = settings.data_dir + "/settings.txt";
    settings.load(settings_path);

    sf3ps5::VkScene scene;
    bool offscreen = std::getenv("SF3_PS5_OFFSCREEN") != nullptr;   // PC checks without a display
    if (!scene.init(why, !offscreen)) { std::fprintf(stderr, "vulkan: %s\n", why.c_str()); return 1; }
    sf3ps5::Display display;
    std::unique_ptr<host::Screen> screen;
    if (offscreen) screen = std::make_unique<host::OffscreenScreen>(scene, 1920, 1080);
    else {
        if (!display.open(scene, why)) { std::fprintf(stderr, "display: %s\n", why.c_str()); return 1; }
        screen = std::make_unique<host::DisplayScreen>(display);
    }

    host::AudioOut audio;
    g_have_audio = audio.open(why);
    if (!g_have_audio) std::fprintf(stderr, "audio: %s (no sound)\n", why.c_str());
    g_audio = g_have_audio ? &audio : nullptr;

    runtime::Launcher launcher;
    bool in_game = !args.cue.empty();
    runtime::RuntimeApi api = runtime::saturn_runtime_api();
    launcher.launch = [&](const std::string& image) {
        if (!in_game) { g_launch_cue = image; return; }
        g_next.cue = image, g_relaunch = true;   // another disc: end this run, then relaunch
        api.stop("Another disc was chosen in the menu");
        if (g_host) g_host->stop();
    };
    launcher.quit = [&] { g_next = {}, g_relaunch = true; if (g_host) g_host->stop(); };
    runtime::RuntimeHost rt(api, launcher, settings.data_dir + "/states");
    rt.settings_changed(settings);

    ImGui::CreateContext();
    menu::setup_imgui(ImGui::GetIO());
    menu::Jobs jobs;
    menu::Menu m(rt, settings, jobs, settings_path);
    menu::SdlPad pad;
    host::PlatformHost ph(scene, *screen, m, settings, rt,
                          [&] { SDL_GameControllerUpdate(); return pad.read(); }, g_audio);
    if (!ph.init(why)) { std::fprintf(stderr, "menu: %s\n", why.c_str()); return 1; }
    g_host = &ph;

    if (!in_game) {   // the launcher: until a disc is chosen
        m.set_in_game(false);
        m.open(menu::Page::Launcher);
        while (g_launch_cue.empty())
            if (!ph.ui_frame()) { std::fprintf(stderr, "screen: %s\n", ph.error().c_str()); return 1; }
        args.cue = g_launch_cue;
    }
    rt.set_disc_id(runtime::disc_identity(args.cue));
    setenv("SF3_STATE_ID", (std::string(SF3_PS5_BUILD_ID) + ":" + rt.disc_id()).c_str(), 1);
    m.set_in_game(true);
    m.close();

    std::error_code ec;
    std::filesystem::create_directories(settings.data_dir + "/saves", ec);
    std::filesystem::create_directories(settings.data_dir + "/logs", ec);
    SaturnConfig cfg;
    cfg.cue = args.cue;
    cfg.save = settings.data_dir + "/saves/backup.bin";   // one backup memory, shared by the scenarios
    cfg.settings = "-";
    cfg.out = settings.data_dir + "/logs";
    int code = saturn_main(cfg);
    g_host = nullptr;
    if (g_relaunch && !host::relaunch(g_next, why))
        std::fprintf(stderr, "relaunch: %s; exiting (start the app again from the home screen)\n", why.c_str());
    return code;
}
