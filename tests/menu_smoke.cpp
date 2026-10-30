// Headless menu test: drives the real menu through ImGui's null backend, finding widgets by
// label through the test-engine item hooks and clicking them with the simulated mouse.
// Usage: menu_smoke DISC_DIR PACKAGE ADDONS_DIR SCRATCH  (DISC_DIR holds one original .cue disc)
#include "menu/menu.h"
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_internal.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <thread>

namespace fs = std::filesystem;
using namespace sf3::menu;
namespace fetch = sf3::fetch;

// Widget registry, rebuilt each frame.
static std::map<ImGuiID, ImRect> g_bb;
static std::map<std::string, ImGuiID> g_label;
static std::map<std::string, bool> g_disabled;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* d) {
    g_bb[id] = bb;
    (void)d;
    // Combos report no label; match them by ID.
    for (const char* c : {"Speed", "Display", "Disc"})
        if (ctx->CurrentWindow && ctx->CurrentWindow->GetID(c) == id) { g_label[c] = id; g_disabled[c] = false; }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    std::string l = label;
    auto p = l.find("##");
    if (p != std::string::npos) l = l.substr(0, p);
    g_label[l] = id;
    g_disabled[l] = (ctx->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

static int failures = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct FakeHost : Host {
    std::string launched;
    int saved = -1, loaded = -1, quits = 0, resumes = 0;
    std::string info[STATE_SLOTS];
    std::shared_ptr<fetch::Transport> net;
    void launch(const std::string& p) override { launched = p; }
    void resume() override { resumes++; }
    void quit_to_launcher() override { quits++; }
    bool save_state(int s) override { saved = s; info[s] = "2026-10-29 12:00"; return true; }
    bool load_state(int s) override { loaded = s; return true; }
    std::string state_info(int s) override { return info[s]; }
    std::shared_ptr<fetch::Transport> transport() override { return net; }
};

struct Offline : fetch::Transport {
    std::string get_text(const std::string&) override { throw fetch::FetchError("offline"); }
    bool get_file(const std::string&, const std::function<bool(uint64_t)>&, const std::string&,
                  const std::function<void(uint64_t, uint64_t)>&) override { throw fetch::FetchError("offline"); }
};

struct Harness {
    Menu& m;
    ImVec2 mouse{-1, -1};
    bool down = false;
    void frame() {
        g_bb.clear(); g_label.clear(); g_disabled.clear();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        m.draw();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }
    void frames(int n) { for (int i = 0; i < n; i++) frame(); }
    bool has(const std::string& l) { return g_label.count(l) > 0; }
    bool enabled(const std::string& l) { return has(l) && !g_disabled[l]; }
    bool click(const std::string& l) {
        frames(3);   // a requested tab switch shows a frame later
        if (!has(l)) {
            fprintf(stderr, "no widget \"%s\"; have:", l.c_str());
            for (auto& [k, v] : g_label) fprintf(stderr, " [%s]", k.c_str());
            fprintf(stderr, "\n");
            return false;
        }
        ImRect r = g_bb[g_label[l]];
        mouse = r.GetCenter();
        frame();
        down = true; frame();
        down = false; frame();
        mouse = ImVec2(-1, -1);
        frame();
        return true;
    }
    void wait(Jobs& j) {
        for (int i = 0; i < 20000; i++) {
            frame();
            if (!j.busy()) { frame(); frame(); if (!j.busy()) return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        fprintf(stderr, "job timeout\n");
        failures++;
    }
};

static std::string last_sha(const JobStatus& st) {
    for (auto it = st.log.rbegin(); it != st.log.rend(); ++it)
        if (it->rfind("track 1 sha1 ", 0) == 0) return it->substr(13);
    return {};
}

static bool same_file(const fs::path& a, const fs::path& b) {
    FILE *x = fopen(a.c_str(), "rb"), *y = fopen(b.c_str(), "rb");
    bool same = x && y;
    static char p[1 << 16], q[1 << 16];
    while (same) {
        size_t n = fread(p, 1, sizeof p, x), m = fread(q, 1, sizeof q, y);
        same = n == m && memcmp(p, q, n) == 0;
        if (n == 0) break;
    }
    if (x) fclose(x);
    if (y) fclose(y);
    printf("%s vs %s: %s\n", a.filename().c_str(), b.c_str(), same ? "identical" : "DIFFERENT");
    return same;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: menu_smoke DISC_DIR PACKAGE ADDONS_DIR SCRATCH\n"); return 2; }
    fs::path discs = argv[1], pkg = argv[2], addons = argv[3], scratch = argv[4];
    fs::remove_all(scratch);
    fs::create_directories(scratch / "data" / "translation");
    fs::create_directories(scratch / "inplace" / "translation");
    for (auto& e : fs::directory_iterator(discs)) {
        fs::create_symlink(fs::absolute(e.path()), scratch / "data" / e.path().filename());
        fs::copy_file(e.path(), scratch / "inplace" / e.path().filename());
    }
    for (auto d : {"data", "inplace"}) {
        fs::create_symlink(fs::absolute(pkg), scratch / d / "translation" / "SF3Patch.exe");
        fs::create_symlink(fs::absolute(addons), scratch / d / "translation" / "V25+");
    }
    const char* ref = getenv("EXPECT_BIN");   // reference .bin of the reproducible add-ons build

    ImGui::CreateContext();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    ImGui::GetIO().DisplaySize = ImVec2(3840, 2160);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();

    {
        FakeHost host;
        Settings s;
        s.data_dir = (scratch / "data").string();
        s.ui_scale = 1.0f;
        std::string sp = (scratch / "settings.txt").string();
        Jobs jobs;
        Menu m(host, s, jobs, sp);
        m.set_reproducible(true);
        Harness h{m};
        h.frame();
        h.wait(jobs);
        auto r = jobs.scan_result();
        CHECK(r.slot[4] >= 0);
        for (int i = 1; i <= 3; i++) CHECK(r.slot[i] < 0);
        CHECK(h.has("Play") && h.has("Translate..."));
        {   // scan cache: the second scan reuses every unchanged image; Rescan (refresh) reads them again
            sf3::disc::ScanOptions o;
            o.cache_path = (scratch / "scan-cache.txt").string();
            int n1 = -1, n2 = -1, n3 = -1;
            o.reused = &n1;
            auto a = sf3::disc::scan(s.data_dir, o);
            o.reused = &n2;
            auto b = sf3::disc::scan(s.data_dir, o);
            o.refresh = true, o.reused = &n3;
            sf3::disc::scan(s.data_dir, o);
            CHECK(n1 == 0 && n2 == int(a.all.size()) && n2 > 0 && n3 == 0);
            CHECK(b.slot[4] == a.slot[4] && b.all[size_t(b.slot[4])].id.verified() == a.all[size_t(a.slot[4])].id.verified());
            CHECK(b.all[size_t(b.slot[4])].cls.state == a.all[size_t(a.slot[4])].cls.state);
        }

        // Play -> launch, overlay closes.
        CHECK(h.click("Play"));
        CHECK(host.launched == r.all[size_t(r.slot[4])].path);
        CHECK(!m.visible() && m.in_game());

        // In-game: save state 0, then load it (closes the menu and resumes).
        m.open(Page::InGame);
        h.frame();
        CHECK(!h.enabled("Load"));
        CHECK(h.click("Save"));
        CHECK(host.saved == 0);
        CHECK(h.enabled("Load"));
        CHECK(h.click("Load"));
        CHECK(host.loaded == 0 && !m.visible() && host.resumes == 1);

        // Speed / fast-forward / aspect through the widgets; persisted.
        m.open(Page::InGame);
        CHECK(m.speed(false) == 1 && m.speed(true) == 3);
        CHECK(h.click("Hold R2 to fast-forward"));
        CHECK(m.speed(true) == 1);
        CHECK(h.click("Display") && h.click("16:9 (where the game draws it)"));
        CHECK(m.aspect() > 1.7f);
        CHECK(h.click("Speed") && h.click("2x"));
        CHECK(m.speed(false) == 2);
        Settings back;
        CHECK(back.load(sp) && back.speed == 2 && back.aspect == Aspect::Ratio16x9 && !back.fast_forward_hold);

        CHECK(h.click("Quit to launcher"));
        CHECK(host.quits == 1 && !m.in_game() && m.page() == Page::Launcher);

        // Download: disabled until opted in; with no network the job fails cleanly.
        m.open(Page::Download);
        h.frame();
        CHECK(!h.has("Download latest release and add-ons"));   // no transport in this build
        host.net = std::make_shared<Offline>();
        h.frame();
        CHECK(!h.enabled("Download latest release and add-ons"));
        CHECK(h.click("Allow downloading the translation package"));
        CHECK(h.enabled("Download latest release and add-ons"));
        CHECK(h.click("Download latest release and add-ons"));
        h.wait(jobs);
        CHECK(jobs.status().failed);

        // Translate the disc to a new image (add-ons on, wizard defaults).
        m.open(Page::Launcher);
        CHECK(h.click("Translate..."));
        CHECK(m.page() == Page::Patch);
        h.frames(2);
        CHECK(h.has("Patch"));
        CHECK(h.click("Patch"));
        h.wait(jobs);
        auto st = jobs.last(JobKind::Patch);
        for (auto& l : st.log) printf("  %s\n", l.c_str());
        CHECK(!st.failed);
        std::string sha = last_sha(st);
        printf("new image track 1 sha1 %s\n", sha.c_str());
        if (ref) CHECK(same_file(scratch / "data" / (discs.filename().string() + " (Translated).bin"), ref));
        h.wait(jobs);   // the rescan after the patch
        r = jobs.scan_result();
        CHECK(r.slot[4] >= 0 && r.all[size_t(r.slot[4])].cls.state == sf3::disc::State::Translated);
    }
    {
        // In place, through the confirmation; originals end up as .bak.
        FakeHost host;
        Settings s;
        s.data_dir = (scratch / "inplace").string();
        s.ui_scale = 1.0f;
        Jobs jobs;
        Menu m(host, s, jobs, "");
        m.set_reproducible(true);
        Harness h{m};
        h.frame();
        h.wait(jobs);
        m.open(Page::Launcher);
        CHECK(h.click("Translate..."));
        CHECK(h.click("Replace the original"));
        CHECK(s.patch_in_place);
        CHECK(h.click("Patch"));
        CHECK(!jobs.busy());       // nothing until confirmed
        CHECK(h.click("Replace"));
        h.wait(jobs);
        auto st = jobs.last(JobKind::Patch);
        for (auto& l : st.log) printf("  %s\n", l.c_str());
        CHECK(!st.failed);
        std::string sha = last_sha(st);
        printf("in place track 1 sha1 %s\n", sha.c_str());
        if (ref) CHECK(same_file(scratch / "inplace" / (discs.filename().string() + ".bin"), ref));
        int baks = 0, cues = 0;
        for (auto& e : fs::directory_iterator(scratch / "inplace")) {
            baks += e.path().extension() == ".bak";
            cues += e.path().extension() == ".cue";
        }
        printf("in place: %d .bak files, %d cue\n", baks, cues);
        CHECK(baks == 3 && cues == 1);
        h.wait(jobs);
        auto r = jobs.scan_result();
        CHECK(r.slot[4] >= 0 && r.all[size_t(r.slot[4])].cls.state == sf3::disc::State::Translated);
    }
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(failures ? "menu_smoke: %d FAILURES\n" : "menu_smoke: ok\n", failures);
    return failures ? 1 : 0;
}
