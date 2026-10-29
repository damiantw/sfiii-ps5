// The runtime's host_* calls on PlatformHost, and the app's main (instead of the runtime's
// main.cpp): the launcher menu without a game, or one disc per process (relaunch.h).
// Compiled against a patched runtime checkout (SF3_PS5_RUNTIME_DIR); the console build links it
// with the runtime and the generated code. MIT, see LICENSE.
#include "platform_host.h"
#include "relaunch.h"
#if !defined(SF3_PS5_CONSOLE)
#include "script.h"
#include "window_screen.h"
#endif
#include "menu/jobs.h"
#include "prepare/prepare.h"
#include "patch/fetch_sce.h"
#if defined(SF3_PS5_CONSOLE)
#include "menu/pad_ps5.h"
#else
#include "menu/pad_sdl2.h"
#include <SDL.h>
#endif
#include "../vulkan/display.h"
#include "imgui.h"
#include "saturn.h"
#include "host/host.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#ifndef SF3_PS5_PROFILES_DIR   // the shipped revision profiles (sfiii-native profiles/*.json)
#if defined(SF3_PS5_CONSOLE)
#define SF3_PS5_PROFILES_DIR "/app0/profiles"
#else
#define SF3_PS5_PROFILES_DIR "profiles"
#endif
#endif

#ifndef SF3_PS5_BUILD_ID
#define SF3_PS5_BUILD_ID "dev"
#endif

using namespace sf3;

// The game build's SH-2 interpreter fallback (sfiii-native native/recomp/interp_fallback.cpp):
// code the recompiler missed runs interpreted instead of stopping the game. Weak: a tree built
// without it keeps the runtime's fatal stop.
void sf3_interp_fallback_install() __attribute__((weak));
// A generic game build's module set loader (sfiii-native native/recomp/gen/x64_set.cpp): installs the
// code caches the generator made from the user's disc. Absent in a build with a generated tree.
bool sf3_x64_install_set(const std::string& dir, std::string& why) __attribute__((weak));

namespace {
// the PC window has no vsync wait; the console's swapchain present blocks on vsync
void pc_frame_wait() {
#if !defined(SF3_PS5_CONSOLE)
    SDL_Delay(16);
#endif
}
host::PlatformHost* g_host = nullptr;
host::AudioOut* g_audio = nullptr;
bool g_have_audio = false;
host::AppArgs g_next;           // what to start after this run
bool g_relaunch = false;
// saturn_main does not return: once a run stops, the runtime writes the backup memory and states,
// calls this, then ends the process with _Exit. So a relaunch happens here, with the saves on disk.
void run_over(int code) {
    std::fprintf(stderr, "run ended (%d)%s%s\n", code, g_relaunch ? "; restarting " : "",
                 g_relaunch ? (g_next.cue.empty() ? "into the launcher" : g_next.cue.c_str()) : "");
    std::fflush(nullptr);
    if (!g_relaunch) return;
    const char* g = std::getenv("SF3_PS5_GEN");   // the test script's generation (script.h)
    setenv("SF3_PS5_GEN", std::to_string((g ? std::atoi(g) : 0) + 1).c_str(), 1);
    std::string why;
    if (!host::relaunch(g_next, why))
        std::fprintf(stderr, "relaunch: %s; exiting (start the app again from the home screen)\n", why.c_str());
}
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
#if !defined(SF3_PS5_CONSOLE)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#endif

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
    const std::vector<uint8_t>* pixels = nullptr;   // the last frame, for the script's shots (PC)
    if (offscreen) {
        auto o = std::make_unique<host::OffscreenScreen>(scene, 1280, 720);
        pixels = &o->rgba;
        screen = std::move(o);
    } else {
#if defined(SF3_PS5_CONSOLE)
        if (!display.open(scene, why)) { std::fprintf(stderr, "display: %s\n", why.c_str()); return 1; }
        screen = std::make_unique<host::DisplayScreen>(display);
#else
        auto w = std::make_unique<host::WindowScreen>(scene, 1280, 720);
        if (!w->open(why)) { std::fprintf(stderr, "window: %s\n", why.c_str()); return 1; }
        pixels = &w->rgba();
        screen = std::move(w);
#endif
    }
    (void)pixels;

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
#if defined(SF3_PS5_CONSOLE)
    rt.set_transport(fetch::sce_http_transport());   // used only after the player opts in
#else
    rt.set_transport(fetch::curl_transport());
#endif

    ImGui::CreateContext();
    menu::setup_imgui(ImGui::GetIO());
    menu::Jobs jobs;
    menu::Menu m(rt, settings, jobs, settings_path);
#if defined(SF3_PS5_CONSOLE)
    menu::ScePad pad;
    std::function<menu::PadState()> read = [&] { return pad.read(); };
#else
    menu::SdlPad pad;
    std::function<menu::PadState()> read = [&] { SDL_GameControllerUpdate(); return pad.read(); };
#endif
    bool preparing = false;   // a disc's first launch: the script's clock waits for the game
#if !defined(SF3_PS5_CONSOLE)
    // PC test driver (script.h)
    const char* gen_env = std::getenv("SF3_PS5_GEN");
    int gen = gen_env ? std::atoi(gen_env) : 0;
    host::Script script;
    bool scripted = false;
    std::string last_status;
    if (const char* sp = std::getenv("SF3_PS5_SCRIPT")) {
        if (!script.load(sp, gen, why)) { std::fprintf(stderr, "script: %s\n", why.c_str()); return 1; }
        scripted = true;
    }
    if (scripted) read = [&, base = read] {
        menu::PadState p = base();
        p.connected = true;
        if (preparing) return p;
        for (auto& st : script.tick(p)) {
            std::fprintf(stderr, "script %d/%ld: %s %s\n", gen, script.now(), st.op.c_str(), st.arg.c_str());
            if (st.op == "save" || st.op == "load") {
                bool ok = st.op == "save" ? rt.save_state(std::atoi(st.arg.c_str())) : rt.load_state(std::atoi(st.arg.c_str()));
                std::fprintf(stderr, "script: %s %s: %s\n", st.op.c_str(), ok ? "queued" : "refused", rt.last_error().c_str());
            } else if (st.op == "quit") {
                if (in_game) rt.quit_to_launcher(); else std::exit(0);
            } else if (st.op == "launch") {
                rt.launch(st.arg);
            } else if (st.op == "menu") {
                m.open(menu::Page::InGame);
            } else if (st.op == "shot" && pixels && !pixels->empty()) {
                if (FILE* f = std::fopen(st.arg.c_str(), "wb")) {
                    int W = screen->width(), H = screen->height();
                    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
                    for (size_t i = 0; i < size_t(W) * H; i++) std::fwrite(&(*pixels)[i * 4], 1, 3, f);
                    std::fclose(f);
                }
            } else if (st.op == "exit") {
                g_relaunch = false;
                        if (in_game) api.stop("the script ended the run"); else std::exit(0);
            }
        }
        if (m.status_text() != last_status) {
            last_status = m.status_text();
            std::fprintf(stderr, "menu status (tick %ld): %s\n", script.now(), last_status.c_str());
        }
        return p;
    };
#endif
    host::PlatformHost ph(scene, *screen, m, settings, rt, read, g_audio);
#if !defined(SF3_PS5_CONSOLE)
    ph.menu_frame_floor = std::chrono::milliseconds(offscreen ? 0 : 16);
#endif
    if (!ph.init(why)) { std::fprintf(stderr, "menu: %s\n", why.c_str()); return 1; }
    g_host = &ph;

    if (!in_game) {   // the launcher: until a disc is chosen
        m.set_in_game(false);
        m.open(menu::Page::Launcher);
        while (g_launch_cue.empty()) {
            if (!ph.ui_frame()) { std::fprintf(stderr, "screen: %s\n", ph.error().c_str()); return 1; }
            if (offscreen) continue;
#if !defined(SF3_PS5_CONSOLE)
            pc_frame_wait();
#endif
        }
        args.cue = g_launch_cue;
    }
    rt.set_disc_id(runtime::disc_identity(args.cue));
    setenv("SF3_STATE_ID", (std::string(SF3_PS5_BUILD_ID) + ":" + rt.disc_id()).c_str(), 1);
    m.set_in_game(true);
    in_game = true;
    m.close();

    std::error_code ec;
    std::filesystem::create_directories(settings.data_dir + "/saves", ec);
    std::filesystem::create_directories(settings.data_dir + "/logs", ec);
    SaturnConfig cfg;
    cfg.cue = args.cue;
    cfg.save = settings.data_dir + "/saves/backup.bin";   // one backup memory, shared by the scenarios
    cfg.settings = "-";
    cfg.out = settings.data_dir + "/logs";
    if (sf3_x64_install_set) {   // a generic build: the game's code comes from this disc (src/prepare)
        std::string set_dir;
        if (const char* set = std::getenv("SF3_X64_SET")) {
            set_dir = set;   // a set made elsewhere (dev path; see docs/pc-run.md)
        } else {
            const char* pd = std::getenv("SF3_PROFILES");
            std::string profiles = pd ? pd : SF3_PS5_PROFILES_DIR;
            prepare::Result pr;
            jobs.start_prepare("Preparing " + disc::path_name(args.cue), [&](const auto& progress) {
                pr = prepare::prepare_disc(args.cue, settings.data_dir, profiles, progress);
                if (!pr.ok) throw std::runtime_error(pr.why);
            });
            m.open(menu::Page::Prepare);
            preparing = true;
            while (jobs.busy()) {
                if (!ph.ui_frame()) { std::fprintf(stderr, "screen: %s\n", ph.error().c_str()); return 1; }
#if !defined(SF3_PS5_CONSOLE)
                if (!offscreen) pc_frame_wait();
#endif
            }
            jobs.join();
            preparing = false;
            if (!pr.ok) {   // leave the reason on screen until the player goes back
                std::fprintf(stderr, "prepare: %s\n", pr.why.c_str());
                for (int i = 0; i < (offscreen ? 1 : 600) && ph.ui_frame(); i++) {
#if !defined(SF3_PS5_CONSOLE)
                    if (!offscreen) pc_frame_wait();
#endif
                }
                if (!offscreen) { g_next = {}, g_relaunch = true; run_over(1); }   // back to the launcher
                return 1;
            }
            std::fprintf(stderr, "prepare: %s%s\n", pr.cached ? "cached " : "", pr.set_dir.c_str());
            set_dir = pr.set_dir;
            m.close();
        }
        std::string why;
        if (!sf3_x64_install_set(set_dir, why)) { std::fprintf(stderr, "module set: %s\n", why.c_str()); return 1; }
    } else if (std::getenv("SF3_X64_SET")) {
        std::fprintf(stderr, "SF3_X64_SET: this build has no module set loader\n");
        return 1;
    }
    sat_set_run_over_hook(run_over);
    if (sf3_interp_fallback_install) sf3_interp_fallback_install();
    int code = saturn_main(cfg);
    g_host = nullptr;
    run_over(code);   // only if saturn_main ever returns
    return code;
}
