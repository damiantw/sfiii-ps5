// The runtime's host_* calls on PlatformHost, and the app's main (instead of the runtime's
// main.cpp): the launcher menu without a game, or one disc per process (relaunch.h).
// Compiled against a patched runtime checkout (SF3_PS5_RUNTIME_DIR); the console build links it
// with the runtime and the generated code. MIT, see LICENSE.
#include "disc/cdimage.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
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
#if SF3_PS5_DEVTOOLS
#include "devtools/debug_server.h"
#endif
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <cctype>
#include <cerrno>
#include <cstring>

#if SF3_PS5_DEVTOOLS
// The debug server in the launcher (dev builds, /data/sfiii/devtools.enable): "pad [N.]BUTTON+..."
// holds menu buttons for N frames (default 6), so automated runs do not depend on a controller.
namespace {
uint64_t g_dev_frames = 0, g_dev_until = 0;
sf3::menu::PadState g_dev_pad;
sf3::menu::Menu* g_dev_menu = nullptr;
std::string g_dev_status, g_dev_launch;
uint64_t dev_vblanks() { return g_dev_frames; }
bool dev_press(const std::string& spec, std::string& err) {
    sf3::menu::PadState p;
    std::string rest = spec;
    uint64_t n = 6;
    if (size_t dot = rest.find('.'); dot != std::string::npos) { n = std::strtoull(rest.c_str(), nullptr, 10); rest = rest.substr(dot + 1); }
    size_t i = 0;
    while (i <= rest.size()) {
        size_t j = rest.find('+', i);
        std::string b = rest.substr(i, j == std::string::npos ? std::string::npos : j - i);
        for (auto& c : b) c = char(std::toupper((unsigned char)c));
        if (b == "UP") p.up = true; else if (b == "DOWN") p.down = true;
        else if (b == "LEFT") p.left = true; else if (b == "RIGHT") p.right = true;
        else if (b == "CROSS" || b == "A") p.cross = true; else if (b == "CIRCLE" || b == "B") p.circle = true;
        else if (b == "SQUARE") p.square = true; else if (b == "TRIANGLE") p.triangle = true;
        else if (b == "L1") p.l1 = true; else if (b == "R1") p.r1 = true;
        else if (b == "OPTIONS" || b == "START") p.options = true; else if (b == "TOUCHPAD") p.touchpad = true;
        else if (b != "NONE") return err = "unknown button " + b, false;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    g_dev_pad = p, g_dev_until = g_dev_frames + n;
    return true;
}
std::string dev_info() { return "sfiii-ps5 launcher" + (g_dev_menu ? " " + g_dev_menu->status_text() : std::string()); }
sf3::menu::PadState launcher_dev_pad(sf3::menu::PadState p) {
    ++g_dev_frames;
    if (g_dev_frames > g_dev_until) return p;
    const auto& d = g_dev_pad;
    p.up |= d.up, p.down |= d.down, p.left |= d.left, p.right |= d.right;
    p.cross |= d.cross, p.circle |= d.circle, p.square |= d.square, p.triangle |= d.triangle;
    p.l1 |= d.l1, p.r1 |= d.r1, p.options |= d.options, p.touchpad |= d.touchpad;
    p.connected = true;
    return p;
}
template <class Rt> void launcher_dev_start(sf3::menu::Menu& m, Rt&) {
    g_dev_menu = &m;
    errno = 0;
    DevConfig c = devtools_read_config("/data/sfiii/devtools.enable");
    int read_errno = errno;
    DevHooks h{};
    h.vblanks = dev_vblanks, h.press = dev_press, h.info = dev_info;
    h.ld8 = [](uint32_t) -> uint8_t { return 0; };
    h.st8 = [](uint32_t, uint8_t) {};
    h.frame = [](uint32_t&, uint32_t&, std::string&) { return false; };
    h.save_state = h.load_state = [](const std::string&, std::string& e) { return e = "not in game", false; };
    h.save_status = h.load_status = [] { return std::string("done 0"); };
    errno = 0;
    bool ok = devtools_start(c, h);
    char b[96];
    if (!c.enabled) std::snprintf(b, sizeof b, " | devtools off (enable file errno %d)", read_errno);
    else if (ok) std::snprintf(b, sizeof b, " | devtools on :%d", c.port);
    else std::snprintf(b, sizeof b, " | devtools :%d failed, errno %d", c.port, errno);
    g_dev_status = b;
    // launch=CUE: start that disc without input (automated runs when no listener is allowed)
    if (FILE* f = std::fopen("/data/sfiii/devtools.enable", "r")) {
        char l[1024];
        while (std::fgets(l, sizeof l, f))
            if (!std::strncmp(l, "launch=", 7)) {
                std::string v(l + 7);
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
                g_dev_launch = v;
            }
        std::fclose(f);
    }
}
}  // namespace
#endif

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

#if defined(SF3_PS5_CONSOLE)
extern "C" volatile int sf3_perf_wait;                                    // fault_klog.c (profiler)
extern "C" void sf3_perf_frame(unsigned long work_ns, unsigned long frame_ns);
extern "C" void sf3_perf_present(unsigned long ns);
#else
static void sf3_perf_present(unsigned long) {}
static volatile int sf3_perf_wait;
static void sf3_perf_frame(unsigned long, unsigned long) {}
#endif
namespace {
using PerfClock = std::chrono::steady_clock;
PerfClock::time_point g_perf_last_pace, g_perf_work_from;   // the frame's start; when the machine resumed
}
void host_present(const Frame& f) {
    Call c("present");
    sf3_perf_wait = 1;   // the swapchain present blocks on the display
    auto p0 = PerfClock::now();
    g_host->present(f.px.data(), f.w, f.h, f.origin_x, f.native_w);
    sf3_perf_wait = 0;
    g_perf_work_from = PerfClock::now();
    sf3_perf_present((unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(g_perf_work_from - p0).count());
}
void host_pace(uint64_t now) {
    Call c("pace");
    auto t = PerfClock::now();
    if (g_perf_last_pace.time_since_epoch().count() && g_perf_work_from.time_since_epoch().count())
        sf3_perf_frame((unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(t - g_perf_work_from).count(),
                       (unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(t - g_perf_last_pace).count());
    if (g_perf_last_pace.time_since_epoch().count() && t - g_perf_last_pace > std::chrono::milliseconds(50))
        std::fprintf(stderr, "hitch: %.1f ms frame at %.3f s\n",
                     std::chrono::duration<double, std::milli>(t - g_perf_last_pace).count(),
                     std::chrono::duration<double>(t.time_since_epoch()).count());
    g_perf_last_pace = t;
    sf3_perf_wait = 1;
    g_host->pace(now);
    sf3_perf_wait = 0;
    g_perf_work_from = PerfClock::now();
}
bool host_wants_frame() {
    if (!g_host) return false;
    bool want = g_host->wants_frame(sat_vblanks());
    if (!want) g_host->poll();
    return want;
}
uint16_t host_pad(int slot) { return g_host ? g_host->pad(slot) : 0; }
bool host_pad_connected(int slot) { return g_host && g_host->pad_connected(slot); }
bool host_audio_open() { return g_have_audio; }
#if defined(SF3_PS5_CONSOLE)
extern "C" int sceKernelDebugOutText(int channel, const char* text);
#endif
void host_audio_report();
void host_audio_push(const int16_t* lr, int frames) {
    if (g_host) g_host->audio(lr, frames);
    static auto next = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    if (auto now = std::chrono::steady_clock::now(); now >= next) next = now + std::chrono::seconds(30), host_audio_report();
}
// The summary goes to stderr, /data/sfiii/audio.txt (kept across a relaunch, which starts stderr
// again) and on the console the kernel log. "loud" counts frames with a channel above 64/32768.
void host_audio_report() {
    if (!g_audio) return;
    auto s = g_audio->stats();
    char m[200];
    double rms = s.frames ? std::sqrt(s.sumsq / double(2 * s.frames)) : 0;
    std::snprintf(m, sizeof m, "audio: 48 kHz, %d underruns, %d chunks dropped, %lld frames in, %lld loud, peak %d, rms %.1f\n",
                  s.underruns, s.dropped, s.frames, s.loud, s.peak, rms);
    std::fputs(m, stderr);
#if defined(SF3_PS5_CONSOLE)
    sceKernelDebugOutText(0, m);
    // A /data write can block for tens of ms on the console: off the machine's thread.
    std::thread([line = std::string(m)] {
        if (FILE* f = std::fopen("/data/sfiii/audio.txt", "a")) { std::fputs(line.c_str(), f); std::fclose(f); }
    }).detach();
#endif
}

#if defined(SF3_PS5_CONSOLE)
extern "C" void sf3_fault_install(void);
extern "C" void sf3_sample_start(void);
extern "C" int sf3_stderr_fd;   // stderr_fast.c
#endif
int main(int argc, char** argv) {
#if defined(SF3_PS5_CONSOLE)
    sf3_fault_install();
    mkdir("/data/sfiii", 0777);   // a title has no console: keep stderr in a file the player (or FTP) can read
    // stderr: a pipe that a thread drains into the file (stderr_fast.c writes to it). A write
    // through the system's stderr takes 40-100 ms, and RADV logs from its submit path.
    if (std::freopen("/data/sfiii/stderr.txt", "w", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
    int log_fd = open("/data/sfiii/stderr.txt", O_WRONLY | O_APPEND);
    int pp[2];
    if (log_fd >= 0 && pipe(pp) == 0) {
        sf3_stderr_fd = pp[1];
        std::thread([r = pp[0], log_fd] {
            char b[4096];
            for (ssize_t n; (n = read(r, b, sizeof b)) > 0;) (void)!write(log_fd, b, size_t(n));
        }).detach();
    } else if (log_fd >= 0) {
        close(log_fd);
    }
#endif
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
        if (!display.open(scene, why, settings.low_latency)) { std::fprintf(stderr, "display: %s\n", why.c_str()); return 1; }
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
    std::function<menu::PadState()> read = [&] {
        menu::PadState p = pad.read();
        if (!in_game) {
#if SF3_PS5_DEVTOOLS
            m.set_diag(pad.diag() + g_dev_status);
#else
            m.set_diag(pad.diag());
#endif
        }
#if SF3_PS5_DEVTOOLS
        if (!in_game) p = launcher_dev_pad(p);
#endif
        return p;
    };
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
#if defined(SF3_PS5_CONSOLE)
    // FIFO paces the TV frames (Pacer::follow); /data/sfiii/pace-timer brings back the timer for A/B.
    struct stat pst;
    ph.vsync_paced = !offscreen && stat("/data/sfiii/pace-timer", &pst) != 0;
    std::fprintf(stderr, "pacing: %s\n", ph.vsync_paced ? "vsync" : "timer");
#endif

    if (!in_game) {   // the launcher: until a disc is chosen
        m.set_in_game(false);
        m.open(menu::Page::Launcher);
#if SF3_PS5_DEVTOOLS
        launcher_dev_start(m, rt);
        if (!g_dev_launch.empty()) g_launch_cue = g_dev_launch;
#endif
        while (g_launch_cue.empty()) {
#if SF3_PS5_DEVTOOLS
            devtools_poll();
#endif
            if (!ph.ui_frame()) { std::fprintf(stderr, "screen: %s\n", ph.error().c_str()); return 1; }
            if (offscreen) continue;
#if !defined(SF3_PS5_CONSOLE)
            pc_frame_wait();
#endif
        }
        args.cue = g_launch_cue;
#if SF3_PS5_DEVTOOLS
        devtools_stop();   // the game's runtime_agent.cpp opens the server again
#endif
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
    // The runtime reads CUE sheets only: a CHD is written out once, on the console, as the BIN/CUE
    // chdman extractcd would make (cache/chd-<disc id>/), and the run reads that.
    if (sf3::disc::path_suffix_lower(args.cue) == ".chd") {
        try {
            std::string dir = settings.data_dir + "/cache/chd-" + rt.disc_id();
            std::fprintf(stderr, "chd: unpacking %s to %s\n", args.cue.c_str(), dir.c_str());
            cfg.cue = sf3::disc::extract_chd(args.cue, dir);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "chd: %s\n", e.what());
            return 2;
        }
    }
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
#if defined(SF3_PS5_CONSOLE)
    sf3_sample_start();   // dev runs: /data/sfiii/sample.enable
#endif
    if (sf3_interp_fallback_install) sf3_interp_fallback_install();
    int code = saturn_main(cfg);
    g_host = nullptr;
    run_over(code);   // only if saturn_main ever returns
    return code;
}
