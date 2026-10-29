// The menu end to end on the box: a frame from VkScene (lavapipe), the menu drawn over it
// by MenuRenderer (imgui_impl_vulkan), driven by an SDL2 virtual game controller through
// SdlPad -> PadNav -> Menu. Writes the last frame to argv[1] (PPM) if given.
#include "../src/vulkan/menu_vk.h"
#include "../src/vulkan/present.h"
#include "menu/menu.h"
#include "menu/pad_sdl2.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <initializer_list>
#include <iterator>
namespace {
#include "vk_scene_test_frag.inc"
}
using namespace sf3::menu;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s %s\n", __LINE__, #c, why.c_str()); failures++; } } while (0)

struct FakeHost : Host {
    int resumes = 0, saved = -1;
    void launch(const std::string&) override {}
    void resume() override { resumes++; }
    void quit_to_launcher() override {}
    bool save_state(int s) override { saved = s; return true; }
    bool load_state(int) override { return true; }
};

int main(int argc, char** argv) {
    std::string why;
    // Pad: an SDL2 virtual controller with the standard button/axis layout.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_VirtualJoystickDesc vd{};
    vd.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    vd.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    vd.naxes = SDL_CONTROLLER_AXIS_MAX, vd.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    vd.name = "virtual DualSense";
    int vi = SDL_JoystickAttachVirtualEx(&vd);
    CHECK(vi >= 0);
    SDL_Joystick* vj = SDL_JoystickOpen(vi);
    CHECK(vj && SDL_IsGameController(vi));
    SdlPad pad;
    PadNav nav;

    // Vulkan: a game frame to draw the menu over.
    sf3ps5::VkScene s;
    CHECK(s.init(why));
    printf("device: %s\n", s.device_name());
    CHECK(s.load_fragment(std::vector<uint32_t>(std::begin(kTestFrag), std::end(kTestFrag)), why));
    const int w = 64, h = 32, W = 1920, H = 1080;
    std::vector<uint32_t> scene(1 + w * h, 0x204060), tex{0};
    scene[0] = w;
    CHECK(s.upload(scene, tex, why));
    CHECK(s.draw(w, h, 0, false, why));
    sf3ps5::Rect pr = sf3ps5::place(W, H, w, h, sf3ps5::Aspect::Classic4x3);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    setup_imgui(io);
    io.DisplaySize = ImVec2(W, H);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    sf3ps5::MenuRenderer mr;
    CHECK(mr.init(s, sf3ps5::VkScene::kOffscreenFormat, 2, why));

    FakeHost host;
    Settings st;
    st.data_dir = "/nonexistent-sf3";
    st.ui_scale = 1.0f;
    Jobs jobs;
    Menu m(host, st, jobs, "");
    m.set_in_game(true);
    m.open(Page::InGame);

    std::vector<uint8_t> px;
    PadEvents last;
    float font_px = 0;
    auto frame = [&](bool render = false) {
        SDL_GameControllerUpdate();
        PadState ps = pad.read();
        last = nav.feed(ps, m.visible(), &io);
        m.handle(last);
        io.DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        font_px = ImGui::GetFontSize();
        m.draw();
        ImGui::Render();
        if (render) CHECK(s.present_offscreen(W, H, pr, true, px, why, mr.overlay()));
    };
    auto tap = [&](SDL_GameControllerButton b) {
        SDL_JoystickSetVirtualButton(vj, b, 1); frame(); frame();
        SDL_JoystickSetVirtualButton(vj, b, 0); frame(); frame();
    };
    // SDL2's virtual controller has no touchpad in its mapping (a real DualSense does), so the
    // touchpad goes straight into PadNav.
    auto touchpad = [&]() {
        for (bool down : {true, false, false}) {
            PadState ps = pad.read();
            ps.touchpad = down;
            last = nav.feed(ps, m.visible(), &io);
            m.handle(last);
            ImGui::NewFrame(); m.draw(); ImGui::Render();
        }
    };
    auto nav_label = [&]() -> std::string {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        return g.NavId ? (g.NavWindow ? std::string(g.NavWindow->Name) : "") : "";
    };
    (void)nav_label;

    for (int i = 0; i < 5; i++) frame(i == 4);
    CHECK(pad.read().connected);
    CHECK(io.BackendFlags & ImGuiBackendFlags_HasGamepad);
    std::vector<uint8_t> with_menu = px;
    // Same frame without the overlay: the menu must change the window area and leave the edges.
    std::vector<uint8_t> plain;
    CHECK(s.present_offscreen(W, H, pr, true, plain, why));
    size_t diff = 0;
    for (size_t i = 0; i < plain.size(); i += 4) diff += plain[i] != with_menu[i] || plain[i + 1] != with_menu[i + 1];
    auto at = [&](const std::vector<uint8_t>& v, int x, int y) { return &v[(size_t(y) * W + x) * 4]; };
    printf("menu changed %zu of %d pixels\n", diff, W * H);
    CHECK(diff > size_t(W * H) / 4);
    CHECK(at(with_menu, 5, 5)[0] == at(plain, 5, 5)[0]);   // outside the 80% window: untouched

    // L1/R1 switch tabs, wrapping.
    CHECK(m.page() == Page::InGame);
    tap(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(m.page() == Page::Launcher);
    tap(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(m.page() == Page::InGame);
    tap(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(m.page() == Page::Settings);
    tap(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(m.page() == Page::InGame);

    // Cross: Resume has the default focus on the Game page. Then D-pad down moves to the next item.
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        ImGuiID after_down = g.NavId;
        tap(SDL_CONTROLLER_BUTTON_DPAD_UP);
        ImGuiID after_up = g.NavId;
        printf("nav ids: down %08x up %08x\n", after_down, after_up);
        CHECK(after_down && after_up && after_down != after_up);
    }
    tap(SDL_CONTROLLER_BUTTON_A);
    printf("after cross: resumes=%d visible=%d\n", host.resumes, m.visible());
    CHECK(host.resumes == 1 && !m.visible());

    // Menu closed: R2 is fast-forward, and ImGui sees no buttons.
    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    frame();
    CHECK(last.fast_forward && m.speed(last.fast_forward) == 3);
    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);   // released
    frame();
    CHECK(!last.fast_forward && m.speed(last.fast_forward) == 1);

    // Touchpad toggles the overlay; on the launcher (not in game) it stays open.
    touchpad();
    CHECK(m.visible() && m.page() == Page::InGame);
    touchpad();
    CHECK(!m.visible());
    tap(SDL_CONTROLLER_BUTTON_START);
    CHECK(m.visible());
    m.set_in_game(false);
    m.open(Page::Launcher);
    touchpad();
    CHECK(m.visible());

    // Menu size 2x still renders (font atlas rebuilt at a larger scale is the backend's job).
    float base = font_px;
    st.ui_scale = 2.0f;
    for (int i = 0; i < 3; i++) frame(i == 2);
    printf("font size %.1f -> %.1f at menu size 2\n", base, font_px);
    CHECK(font_px > base * 1.9f);
    if (argc > 1) {
        FILE* f = fopen(argv[1], "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (size_t i = 0; i < px.size(); i += 4) fwrite(&px[i], 1, 3, f);
        fclose(f);
    }
    jobs.join();
    mr.shutdown();
    ImGui::DestroyContext();
    SDL_JoystickClose(vj);
    SDL_JoystickDetachVirtual(vi);
    SDL_Quit();
    printf(failures ? "vk_menu_test: %d FAILURES\n" : "vk_menu_test: ok\n", failures);
    return failures ? 1 : 0;
}
