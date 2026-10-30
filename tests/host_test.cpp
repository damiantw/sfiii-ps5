// The PS5 host on the box: the pacer, the Saturn pad layout, 48 kHz sound (SDL2's disk driver,
// a sine's frequency measured), PlatformHost with synthetic fields on lavapipe (placement, the
// menu over the game, pause, speed, 16:9) and relaunching (execv). MIT, see LICENSE.
#include "host/pacer.h"
#include "host/platform_host.h"
#include "host/relaunch.h"
#include "host/saturn_pad.h"
#include "menu/jobs.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace sf3;
using namespace std::chrono;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void pacer_test() {
    host::Pacer p;
    auto t0 = host::Pacer::Clock::now();
    const uint64_t field = 16'683'333;   // ns, NTSC
    CHECK(p.wait(0, t0).count() == 0);
    auto w = p.wait(field, t0);
    CHECK(w > milliseconds(16) && w < milliseconds(17));
    p.set_speed(2, t0);
    w = p.wait(2 * field, t0);            // one field later at 2x: half the wait
    CHECK(w > milliseconds(8) && w < milliseconds(9));
    p.set_speed(3, t0);
    w = p.wait(3 * field, t0 + seconds(1));   // far behind: no catching up
    CHECK(w.count() == 0);
    w = p.wait(4 * field, t0 + seconds(1));
    CHECK(w > milliseconds(5) && w < milliseconds(6));
    p.restart();
    CHECK(p.wait(100 * field, t0 + seconds(5)).count() == 0);
}

static void pad_test() {
    menu::PadState s;
    s.connected = true;
    s.cross = s.square = s.options = true;
    CHECK(host::saturn_buttons(s, true) == (host::S_B | host::S_A | host::S_START));
    s = {}; s.connected = true; s.r2 = 1; s.l2 = 1;
    CHECK(host::saturn_buttons(s, false) == (host::S_Z | host::S_Y));
    CHECK(host::saturn_buttons(s, true) == host::S_Y);   // R2 is fast-forward: Z on R3
    s.r3 = true;
    CHECK(host::saturn_buttons(s, true) == (host::S_Y | host::S_Z));
    s = {}; s.connected = true; s.left = s.right = true; s.up = true; s.lx = 0.2f;
    CHECK(host::saturn_buttons(s, false) == host::S_UP);   // an opposite pair cancels
    s = {}; s.connected = true; s.lx = 0.9f; s.ly = -0.9f; s.l1 = s.r1 = s.circle = s.triangle = true;
    CHECK(host::saturn_buttons(s, false) == (host::S_RIGHT | host::S_UP | host::S_L | host::S_R | host::S_C | host::S_X));
    s.connected = false;
    CHECK(host::saturn_buttons(s, false) == 0);
}

// Zero crossings per second of the left channel.
static double frequency(const std::vector<int16_t>& lr, int rate) {
    int cross = 0, first = -1, last = -1;
    for (size_t i = 1; i < lr.size() / 2; i++)
        if (lr[2 * (i - 1)] < 0 && lr[2 * i] >= 0) { cross++; if (first < 0) first = int(i); last = int(i); }
    return cross > 1 ? double(cross - 1) * rate / (last - first) : 0;
}

static double audio_run(int speed, const char* file) {
    setenv("SDL_DISKAUDIOFILE", file, 1);
    host::AudioOut a;
    std::string why;
    if (!a.open(why)) { std::fprintf(stderr, "audio: %s\n", why.c_str()); failures++; return 0; }
    a.set_speed(speed);
    const int chunk = 441;   // 10 ms
    std::vector<int16_t> buf(chunk * 2);
    double ph = 0;
    for (int n = 0; n < 100; n++) {   // 1 s of game sound, a 1 kHz sine
        for (int i = 0; i < chunk; i++) {
            int16_t v = int16_t(12000 * std::sin(ph));
            ph += 2 * M_PI * 1000 / 44100;
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        a.push(buf.data(), chunk);
        std::this_thread::sleep_for(milliseconds(10 / speed));
        CHECK(a.queued_ms() <= 260);
    }
    std::this_thread::sleep_for(milliseconds(300));
    a.pause(true);
    std::printf("audio %dx: %d underruns, %d dropped\n", speed, a.stats().underruns, a.stats().dropped);
    FILE* f = std::fopen(file, "rb");
    std::vector<int16_t> out;
    if (f) { int16_t s2[2]; while (std::fread(s2, 2, 2, f) == 2) out.push_back(s2[0]), out.push_back(s2[1]); std::fclose(f); }
    // skip the lead-in silence
    size_t start = 0;
    while (start < out.size() && std::abs(out[start]) < 1000) start++;
    std::vector<int16_t> tone(out.begin() + std::min(out.size(), start), out.end());
    double hz = frequency(tone, host::AudioOut::kOutRate);
    std::printf("audio %dx: %zu frames at 48 kHz, tone %.1f Hz\n", speed, out.size() / 2, hz);
    return hz;
}

static void audio_test() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { std::fprintf(stderr, "SDL audio: %s\n", SDL_GetError()); failures++; return; }
    CHECK(std::string(SDL_GetCurrentAudioDriver()) == "disk");
    double hz1 = audio_run(1, "/tmp/sf3_host_audio1.raw");
    CHECK(std::fabs(hz1 - 1000) < 10);
    double hz2 = audio_run(2, "/tmp/sf3_host_audio2.raw");
    CHECK(std::fabs(hz2 - 2000) < 20);   // 2x: faster and higher, as the picture
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

struct Script {   // the pad, field by field
    menu::PadState s;
    menu::PadState read() { menu::PadState r = s; r.connected = true; return r; }
};

static void platform_test() {
    std::string why;
    sf3ps5::VkScene scene;
    if (!scene.init(why)) { std::fprintf(stderr, "vulkan: %s\n", why.c_str()); failures++; return; }
    std::printf("device: %s\n", scene.device_name());
    const int W = 1920, H = 1080;
    host::OffscreenScreen screen(scene, W, H);

    int paused = 0, stops = 0, launches = 0, quits = 0;
    runtime::RuntimeApi api;
    api.request_save = [](const std::string&) {};
    api.request_load = [](const std::string&) {};
    api.save_busy = api.load_busy = [] { return false; };
    api.write_result = api.load_result = [] { return 0; };
    api.stop = [&](const char*) { stops++; };
    api.paused = [&](int d) { paused += d; };
    runtime::Launcher launcher{[&](const std::string&) { launches++; }, [&] { quits++; }};
    runtime::RuntimeHost rt(api, launcher, "/tmp/sf3_host_states");

    menu::Settings settings;
    settings.data_dir = "/nonexistent-sf3";
    settings.ui_scale = 1.0f;
    ImGui::CreateContext();
    menu::setup_imgui(ImGui::GetIO());
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    menu::Jobs jobs;
    menu::Menu m(rt, settings, jobs, "");
    rt.settings_changed(settings);
    Script pad;
    host::PlatformHost ph(scene, screen, m, settings, rt, [&] { return pad.read(); }, nullptr);
    CHECK(ph.init(why));

    // The launcher (no game): black screen, the menu over it, which stays open.
    m.set_in_game(false);
    m.open(menu::Page::Launcher);
    CHECK(ph.ui_frame());
    CHECK(screen.last_rect.w == 0);
    pad.s.options = true; ph.ui_frame(); pad.s.options = false; ph.ui_frame();
    CHECK(m.visible());

    // In game: a red 320x224 field, placed 4:3 at an integer scale, no menu.
    m.set_in_game(true);
    m.close();
    std::vector<uint32_t> field(320 * 224, 0x00ff0000);
    for (int x = 0; x < 320; x++) field[x] = 0x000000ff;   // top row blue
    ph.present(field.data(), 320, 224, 0, 0);
    sf3ps5::Rect r = screen.last_rect;
    sf3ps5::Rect want = sf3ps5::place(W, H, 320, 224, sf3ps5::Aspect::Classic4x3);
    CHECK(r.x == want.x && r.w == want.w && r.h == want.h);
    auto at = [&](int x, int y) { return &screen.rgba[(size_t(y) * W + x) * 4]; };
    CHECK(at(W / 2, H / 2)[0] == 255 && at(W / 2, H / 2)[2] == 0);   // red in the middle
    CHECK(at(W / 2, r.y + 1)[2] == 255);                               // blue at the top: not upside down
    CHECK(at(5, H / 2)[0] == 0);                                       // pillarbox
    std::vector<uint8_t> plain = screen.rgba;

    // The pad reaches the game; Options is START and does not open the menu.
    pad.s.cross = true; pad.s.options = true;
    ph.poll();
    CHECK(ph.pad(0) == (host::S_B | host::S_START));
    CHECK(ph.pad(1) == 0);
    CHECK(!m.visible());
    pad.s = {};
    ph.poll();

    // The touchpad opens the menu: present holds the machine until it closes, paused meanwhile.
    int menu_frames = 0, paused_seen = 0;
    size_t diff = 0;
    ph.on_menu_frame = [&] {
        ++menu_frames;
        paused_seen = std::max(paused_seen, paused);
        CHECK(ph.pad(0) == 0);
        if (menu_frames == 3) {
            for (size_t i = 0; i < plain.size(); i += 4) diff += plain[i] != screen.rgba[i];
        }
        pad.s.touchpad = menu_frames == 5;   // press again: close
    };
    pad.s.touchpad = true;
    ph.present(field.data(), 320, 224, 0, 0);
    pad.s = {};
    std::printf("menu: %d frames inside present, %zu pixels changed\n", menu_frames, diff);
    CHECK(menu_frames >= 5);
    CHECK(!m.visible());
    CHECK(paused_seen == 1 && paused == 0);
    CHECK(diff > size_t(W * H) / 4);
    ph.on_menu_frame = nullptr;

    // Speed 2x: every 2nd field is shown.
    settings.speed = 2;
    rt.settings_changed(settings);
    ph.poll();
    CHECK(ph.speed() == 2);
    CHECK(!ph.wants_frame(1) && ph.wants_frame(2));
    settings.speed = 1;
    rt.settings_changed(settings);
    // R2 held: fast-forward (3x) while held, and R2 is not Z.
    pad.s.r2 = 1;
    ph.poll();
    std::printf("R2 held: speed %d\n", ph.speed());
    CHECK(ph.speed() > 1);
    CHECK(ph.pad(0) == 0);
    pad.s = {};
    ph.poll();
    CHECK(ph.speed() == 1);

    // 16:9: a 432-wide field with the 4:3 window at 56..376 fills more of the screen.
    settings.aspect = menu::Aspect::Ratio16x9;
    std::vector<uint32_t> wide(432 * 224, 0x0000ff00);
    ph.present(wide.data(), 432, 224, 56, 320);
    std::printf("16:9 rect %d x %d at %d (4:3 was %d wide)\n", screen.last_rect.w, screen.last_rect.h, screen.last_rect.x, want.w);
    CHECK(screen.last_rect.w > want.w && screen.last_rect.h == want.h);
    CHECK(at(screen.last_rect.x + 2, H / 2)[1] == 255);

    // Quit from the menu: RuntimeHost stops the run and hands over to the launcher.
    ph.stop();
    rt.quit_to_launcher();
    CHECK(stops == 1 && quits == 1 && ph.stopping());
    ph.shutdown();
    ImGui::DestroyContext();
}

static void relaunch_test(const char* self) {
    host::AppArgs a;
    char* v[] = {(char*)"x", (char*)"--cue", (char*)"/d/disc 1.cue"};
    a = host::AppArgs::parse(3, v);
    CHECK(a.cue == "/d/disc 1.cue");
    CHECK(a.argv().size() == 2 && host::AppArgs{}.argv().empty());
    {
        host::AppArgs q; q.user_quit = true;
        auto qv = q.argv();
        CHECK(qv.size() == 1 && qv[0] == "--menu");
        char* m[] = {(char*)"--menu"};
        CHECK(host::AppArgs::parse(1, m).user_quit && host::AppArgs::parse(1, m).cue.empty());
    }
    (void)self;
    pid_t pid = fork();
    if (pid == 0) {
        host::AppArgs next;
        next.cue = "/d/disc 2.cue";
        std::string why;
        host::relaunch(next, why);
        _exit(3);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 42);   // the relaunched binary saw --cue
}

int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--cue")) return std::strcmp(argv[2], "/d/disc 2.cue") ? 4 : 42;
    pacer_test();
    pad_test();
    platform_test();
    relaunch_test(argv[0]);
    audio_test();
    std::printf(failures ? "host_test: %d failures\n" : "host_test: ok\n", failures);
    return failures ? 1 : 0;
}
