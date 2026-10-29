// MIT, see LICENSE.
#include "pad.h"
#include "imgui.h"

namespace sf3::menu {

namespace {
float dead(float v) { const float z = 0.25f; return v > z ? (v - z) / (1 - z) : v < -z ? (v + z) / (1 - z) : 0; }
}

PadEvents PadNav::feed(const PadState& s, bool visible, ImGuiIO* io, bool options_toggles) {
    PadEvents e;
    auto pressed = [&](bool now, bool before) { return now && !before; };
    e.toggle_menu = pressed(s.touchpad, prev_.touchpad) || (options_toggles && pressed(s.options, prev_.options));
    if (visible) {
        if (pressed(s.l1, prev_.l1)) e.tab = -1;
        if (pressed(s.r1, prev_.r1)) e.tab = +1;
    }
    e.fast_forward = !visible && s.r2 > 0.5f;
    if (io) {
        io->BackendFlags |= s.connected ? ImGuiBackendFlags_HasGamepad : 0;
        if (!s.connected) io->BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        // While the menu is closed the game owns the pad: send ImGui all-released.
        PadState z;
        const PadState& p = visible ? s : z;
        io->AddKeyEvent(ImGuiKey_GamepadDpadUp, p.up);
        io->AddKeyEvent(ImGuiKey_GamepadDpadDown, p.down);
        io->AddKeyEvent(ImGuiKey_GamepadDpadLeft, p.left);
        io->AddKeyEvent(ImGuiKey_GamepadDpadRight, p.right);
        io->AddKeyEvent(ImGuiKey_GamepadFaceDown, p.cross);       // activate
        io->AddKeyEvent(ImGuiKey_GamepadFaceRight, p.circle);     // cancel / close popup
        io->AddKeyEvent(ImGuiKey_GamepadFaceLeft, p.square);      // tweak (sliders)
        io->AddKeyEvent(ImGuiKey_GamepadFaceUp, p.triangle);      // text input / menu
        float lx = dead(p.lx), ly = dead(p.ly);
        io->AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, lx < 0, lx < 0 ? -lx : 0);
        io->AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, lx > 0, lx > 0 ? lx : 0);
        io->AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ly < 0, ly < 0 ? -ly : 0);
        io->AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ly > 0, ly > 0 ? ly : 0);
        // L1/R1 are the menu's tab keys, not ImGui's window focus keys.
    }
    prev_ = s;
    return e;
}

void setup_imgui(ImGuiIO& io) {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    // Remote Play and network jitter stretch a short press past ImGui's 0.275 s repeat delay,
    // so one tap moved the cursor two rows. Repeat only on a deliberate hold.
    io.KeyRepeatDelay = 0.6f;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6, st.FrameRounding = 4, st.GrabRounding = 4, st.TabRounding = 4;
    st.FramePadding = ImVec2(10, 6), st.ItemSpacing = ImVec2(10, 8);
    const ImVec4 amber(1.00f, 0.69f, 0.00f, 1.00f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_NavCursor] = amber;
    c[ImGuiCol_CheckMark] = amber;
    c[ImGuiCol_SliderGrab] = amber;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.55f, 0.38f, 0.02f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.75f, 0.52f, 0.05f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.94f);
}

}  // namespace sf3::menu
