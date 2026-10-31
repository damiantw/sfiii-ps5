// MIT, see LICENSE.
#include "menu.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <sys/stat.h>
#include <vector>

namespace sf3::menu {

namespace {

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

const char* slot_label(int i) {
    static const char* n[] = {"", "Scenario 1", "Scenario 2", "Scenario 3", "Premium Disc"};
    return n[i];
}

}  // namespace

Menu::Menu(Host& host, Settings& settings, Jobs& jobs, std::string settings_path)
    : host_(host), s_(settings), jobs_(jobs), settings_path_(std::move(settings_path)) {}

void Menu::handle(const PadEvents& e) {
    if (e.toggle_menu) {
        if (visible_) { if (in_game_) close(); }        // the launcher cannot be closed
        else open(in_game_ ? Page::InGame : Page::Launcher);
        return;
    }
    if (!visible_ || !e.tab || ImGui::GetTopMostPopupModal()) return;
    std::vector<Page> tabs;
    if (in_game_) tabs.push_back(Page::InGame);
    for (Page p : {Page::Launcher, Page::Patch, Page::Download, Page::Settings}) tabs.push_back(p);
    size_t i = 0;
    while (i < tabs.size() && tabs[i] != page_) i++;
    i = (i + tabs.size() + size_t(e.tab + 1) - 1) % tabs.size();   // e.tab is -1 or +1
    open(tabs[i]);
}

void Menu::open(Page p) { page_ = p; visible_ = true; select_ = 2; }
void Menu::close() { visible_ = false; if (in_game_) host_.resume(); }
float Menu::aspect() const { return s_.aspect == Aspect::Ratio16x9 ? 16.0f / 9.0f : 4.0f / 3.0f; }
int Menu::speed(bool ff) const { return ff && s_.fast_forward_hold ? 3 : s_.speed; }

void Menu::changed() {
    if (!settings_path_.empty()) s_.save(settings_path_);
    host_.settings_changed(s_);
}

std::string Menu::package_path() const {
    for (auto p : {"/translation/SF3Patch.exe", "/SF3Patch.exe"})
        if (exists(s_.data_dir + p)) return s_.data_dir + p;
    return {};
}

std::string Menu::addons_dir() const {
    std::string d = s_.data_dir + "/translation/V25+";
    return s_.include_addons && exists(d) ? d : std::string();
}

void Menu::draw() {
    if (page_ == Page::Prepare) scanned_ = true;   // the game's process: no launcher to fill
    if (!scanned_ && !jobs_.busy()) { jobs_.start_scan(s_.data_dir); scanned_ = true; }
    {   // After a patch or download finishes, refresh what is on disk.
        auto st = jobs_.status();
        if (st.done && st.id != handled_job_) {
            handled_job_ = st.id;
            if (!st.failed && st.kind != JobKind::Scan && st.kind != JobKind::Prepare) { list_ = {}; list_error_.clear(); patch_target_ = -1; jobs_.start_scan(s_.data_dir); }
        }
    }
    if (!visible_) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontScaleMain = s_.ui_scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f), ImGuiCond_Always);
    ImGui::Begin("Shining Force III", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (page_ == Page::Prepare) {   // a disc's first launch: only the progress, nothing to choose
        ImGui::TextWrapped("Preparing this disc. This happens once per disc: the game's code is read from it "
                           "and translated for this machine, then kept in %s/cache.", s_.data_dir.c_str());
        job_status();
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("pages")) {
        auto tab = [&](const char* name, Page p, void (Menu::*fn)()) {
            ImGuiTabItemFlags f = select_ > 0 && page_ == p ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(name, nullptr, f)) { if (select_ == 0) page_ = p; (this->*fn)(); ImGui::EndTabItem(); }
        };
        if (in_game_) tab("Game", Page::InGame, &Menu::in_game_page);
        tab("Discs", Page::Launcher, &Menu::launcher);
        tab("Translation", Page::Patch, &Menu::patch_page);
        tab("Download", Page::Download, &Menu::download_page);
        tab("Settings", Page::Settings, &Menu::settings_page);
        ImGui::EndTabBar();
        if (select_ > 0) select_--;
    }
    job_status();
    ImGui::End();
}

void Menu::job_status() {
    auto st = jobs_.status();
    if (!st.running && !st.done) return;
    ImGui::Separator();
    ImGui::Text("%s%s", st.title.c_str(), st.running ? "..." : st.failed ? ": failed" : ": done");
    if (st.running) ImGui::ProgressBar(st.progress < 0 ? -1.0f * float(ImGui::GetTime()) : float(st.progress));
    if (!st.message.empty()) ImGui::TextWrapped("%s", st.message.c_str());
}

void Menu::launcher() {
    auto r = jobs_.scan_result();
    auto st = jobs_.status();
    bool scanning = st.running && st.title.rfind("Scanning", 0) == 0;   // a slot is only "not found" once the scan ends
    ImGui::TextDisabled("Discs in %s", s_.data_dir.c_str());
    auto row = [&](int c) {
        auto& cand = r.all[size_t(c)];
        // Buttons first, in fixed columns, so pad up/down moves straight between rows.
        float x = ImGui::GetCursorPosX();
        ImGui::BeginDisabled(jobs_.busy());
        if (ImGui::Button("Play")) { host_.launch(cand.path); in_game_ = true; visible_ = false; }
        ImGui::SameLine(x + ImGui::CalcTextSize("Play").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x);
        float tx = ImGui::GetCursorPosX();
        float tw = ImGui::CalcTextSize("Translate...").x + ImGui::GetStyle().FramePadding.x * 2;
        if (cand.cls.state == disc::State::Original) {
            if (ImGui::Button("Translate...")) { patch_target_ = c; list_ = {}; list_error_.clear(); page_ = Page::Patch; select_ = 2; }
        } else if (cand.cls.state == disc::State::Translated && disc::path_suffix_lower(cand.path) == ".cue" &&
                   restore_precheck(cand.path).empty()) {
            // Patched in place: the originals are still here as .bak files.
            if (ImGui::Button("Restore...", ImVec2(tw, 0))) { restore_target_ = cand.path; ImGui::OpenPopup("Restore the original?"); }
        } else {
            ImGui::Dummy(ImVec2(tw, ImGui::GetFrameHeight()));
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Restore the original?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
            ImGui::TextUnformatted(disc::path_name(restore_target_).c_str());
            ImGui::TextWrapped("The .bak backups are checked against the recorded hashes of the untranslated disc first; "
                               "if any is missing or does not match, nothing is changed. Then they are put back and the "
                               "translated image is deleted (you can translate it again afterwards).");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Restore")) { jobs_.start_restore(restore_target_); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(tx + tw + ImGui::GetStyle().ItemSpacing.x);
        ImGui::Text("%s  [%s]", disc::path_name(cand.path).c_str(), disc::state_name(cand.cls.state));
    };
    for (int i = 1; i <= 4; i++) {
        ImGui::PushID(i);
        int c = r.slot[i];
        ImGui::Text("%-13s", slot_label(i));
        ImGui::SameLine();
        if (c < 0) { ImGui::TextDisabled(scanning ? "scanning..." : "not found"); ImGui::PopID(); continue; }
        row(c);
        ImGui::PopID();
    }
    // Other recognised images (a second revision, a translated copy beside its original).
    for (size_t i = 0; i < r.all.size(); i++) {
        auto& a = r.all[i];
        if (!a.ok || a.cls.slot == disc::Slot::None || r.slot[int(a.cls.slot)] == int(i)) continue;
        ImGui::PushID(100 + int(i));
        ImGui::TextDisabled("%-13s", slot_label(int(a.cls.slot)));
        ImGui::SameLine();
        row(int(i));
        ImGui::PopID();
    }
    size_t unknown = 0;
    for (auto& a : r.all) unknown += !a.ok || a.cls.slot == disc::Slot::None;
    if (unknown) ImGui::TextDisabled("%zu other images were not recognised", unknown);
    ImGui::BeginDisabled(jobs_.busy());
    if (ImGui::Button("Rescan")) jobs_.start_scan(s_.data_dir, true);
    if (!diag_.empty()) ImGui::TextDisabled("%s", diag_.c_str());
    ImGui::EndDisabled();
    auto lr = jobs_.last(JobKind::Restore);
    if (lr.done && !lr.log.empty()) ImGui::TextWrapped("Last restore: %s", lr.log.back().c_str());
    ImGui::TextDisabled("frame %d", ImGui::GetFrameCount());   // a moving number: the screen is live
}

void Menu::patch_page() {
    auto r = jobs_.scan_result();
    std::string pkg = package_path();
    if (pkg.empty()) {
        ImGui::TextWrapped("No translation package. Download it from the Download tab, or copy SF3Patch.exe into %s/translation.",
                           s_.data_dir.c_str());
        return;
    }
    // Choose a disc: originals only.
    std::string preview = patch_target_ >= 0 && size_t(patch_target_) < r.all.size() ? disc::path_name(r.all[size_t(patch_target_)].path)
                                                                                    : "choose a disc";
    if (ImGui::BeginCombo("Disc", preview.c_str())) {
        for (size_t i = 0; i < r.all.size(); i++)
            if (r.all[i].ok && r.all[i].cls.state == disc::State::Original &&
                ImGui::Selectable(disc::path_name(r.all[i].path).c_str(), int(i) == patch_target_)) {
                patch_target_ = int(i);
                list_ = {};
                list_error_.clear();
            }
        ImGui::EndCombo();
    }
    if (patch_target_ < 0 || size_t(patch_target_) >= r.all.size()) return;
    auto& cand = r.all[size_t(patch_target_)];
    PatchRequest req;
    req.source = cand.path;
    req.package = pkg;
    req.addons_dir = addons_dir();
    req.reproducible = reproducible_;
    if (ImGui::Checkbox("Include add-ons (V25+)", &s_.include_addons)) { changed(); list_ = {}; }
    if (list_.entries.empty() && list_error_.empty()) {
        try {
            list_ = list_patches(req);
            ticks_.clear();
            for (auto& e : list_.entries) ticks_.push_back(e.ticked);
        } catch (const std::exception& e) { list_error_ = e.what(); }
    }
    if (!list_error_.empty()) { ImGui::TextWrapped("%s", list_error_.c_str()); return; }
    ImGui::Text("%s %s, package V%s", list_.serial.c_str(), list_.version.c_str(), list_.package_version.c_str());
    if (ImGui::BeginChild("patches", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), ImGuiChildFlags_Borders)) {
        std::string cat;
        for (size_t i = 0; i < list_.entries.size(); i++) {
            auto& e = list_.entries[i];
            if (e.category != cat) { cat = e.category; ImGui::SeparatorText(cat.c_str()); }
            bool t = ticks_[i];
            if (ImGui::Checkbox(e.label.c_str(), &t)) ticks_[i] = t;
        }
    }
    ImGui::EndChild();
    for (auto& is : list_.issues) ImGui::TextWrapped("%s", is.c_str());
    for (size_t i = 0; i < list_.entries.size(); i++)
        if (ticks_[i] != list_.entries[i].ticked) (ticks_[i] ? req.enable : req.disable).push_back(list_.entries[i].name);
    bool cue = disc::path_suffix_lower(cand.path) == ".cue";
    // A checkbox, not two radio buttons: the pad could not reach the radio row on the console.
    bool in_place = s_.patch_in_place && cue;
    ImGui::BeginDisabled(!cue);
    ImGui::Checkbox("Replace the original", &in_place);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(in_place ? "(the original is kept as .bak)" : "(off: a new image beside the original)");
    if (in_place != s_.patch_in_place && cue) { s_.patch_in_place = in_place; changed(); }
    req.in_place = s_.patch_in_place && cue;
    if (!req.in_place) {
        std::string base = cand.path.substr(0, cand.path.find_last_of('.'));
        req.output = base + " (Translated).bin";
    }
    ImGui::BeginDisabled(jobs_.busy());
    if (ImGui::Button("Patch")) {
        if (req.in_place) ImGui::OpenPopup("Replace the original?");
        else jobs_.start_patch(req);
    }
    ImGui::EndDisabled();
    auto lp = jobs_.last(JobKind::Patch);
    if (lp.done && !lp.log.empty()) ImGui::TextWrapped("Last patch: %s", lp.failed ? lp.message.c_str() : lp.log.back().c_str());
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Replace the original?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
        ImGui::TextUnformatted("The original image will be renamed to .bak files and the translated one put in its place. "
                               "Keep the .bak files to undo it later with Restore... on the Discs page, or delete them "
                               "once the game works.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Replace")) { jobs_.start_patch(req); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Menu::download_page() {
    ImGui::TextWrapped("The Shining Force III translation project publishes its patch at %s. "
                       "Downloads are off unless you turn them on; the files stay on this console.", fetch::SITE);
    if (ImGui::Checkbox("Allow downloading the translation package", &s_.allow_download)) changed();
    auto t = host_.transport();
    if (!t) { ImGui::TextDisabled("Networking is not available in this build."); return; }
    ImGui::BeginDisabled(!s_.allow_download || jobs_.busy());
    if (ImGui::Button("Download latest release and add-ons")) jobs_.start_fetch(s_.data_dir + "/translation", t);
    ImGui::EndDisabled();
}

void Menu::settings_page() {
    int sp = s_.speed - 1;
    if (ImGui::Combo("Speed", &sp, "1x\0" "2x\0" "3x\0")) { s_.speed = sp + 1; changed(); }
    if (ImGui::Checkbox("Hold R2 to fast-forward", &s_.fast_forward_hold)) changed();
    int a = s_.aspect == Aspect::Ratio16x9;
    if (ImGui::Combo("Display", &a, "4:3\0" "16:9 (where the game draws it)\0")) { s_.aspect = a ? Aspect::Ratio16x9 : Aspect::Ratio4x3; changed(); }
    static const int kScales[] = {1, 2, 3, 4, 6};
    int rs = 0;
    for (int i = 0; i < 5; ++i) if (kScales[i] == s_.render_scale) rs = i;
    if (ImGui::Combo("3D resolution", &rs, "1x (original)\0" "2x\0" "3x\0" "4x\0" "6x\0")) { s_.render_scale = kScales[rs]; changed(); }
    if (ImGui::Checkbox("Sharp pixels", &s_.sharp_pixels)) changed();
    if (ImGui::Checkbox("Low latency (restart to apply)", &s_.low_latency)) changed();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One frame less input lag; may stutter if the system flips late.");
    if (ImGui::SliderFloat("Menu size", &s_.ui_scale, 1.0f, 3.0f, "%.1f")) changed();
}

void Menu::in_game_page() {
    bool resume = ImGui::Button("Resume");
    ImGui::SetItemDefaultFocus();
    if (resume) { close(); return; }
    ImGui::SeparatorText("Save states");
    // A list rather than a slider: a pad can pick from it without first activating the control.
    auto slot_label = [&](int i) { std::string in = host_.state_info(i); return "Slot " + std::to_string(i) + ": " + (in.empty() ? "empty" : in); };
    if (ImGui::BeginCombo("Slot", slot_label(state_slot_).c_str())) {
        for (int i = 0; i < STATE_SLOTS; ++i) {
            if (ImGui::Selectable(slot_label(i).c_str(), i == state_slot_)) state_slot_ = i;
            if (i == state_slot_) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    std::string info = host_.state_info(state_slot_);
    auto failed = [&](const char* what) { std::string e = host_.last_error(); return e.empty() ? std::string(what) : std::string(what) + ": " + e; };
    // The machine is paused while the menu is up, so the save runs at its next safe point after the
    // menu closes; the host shows "slot N saved" when it has really been written.
    bool saving = false;
    if (ImGui::Button("Save")) { saving = host_.save_state(state_slot_); toast_ = saving ? "" : failed("save failed"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(info.empty());
    bool loaded = false;
    if (ImGui::Button("Load")) { loaded = host_.load_state(state_slot_); toast_ = loaded ? "" : failed("load failed"); }
    ImGui::EndDisabled();
    if (loaded || saving) { close(); return; }
    if (!toast_.empty()) { ImGui::SameLine(); ImGui::TextUnformatted(toast_.c_str()); }
    ImGui::SeparatorText("Speed and display");
    settings_page();
    ImGui::Separator();
    if (ImGui::Button("Quit to launcher")) { in_game_ = false; page_ = Page::Launcher; select_ = 2; host_.quit_to_launcher(); }
}

}  // namespace sf3::menu
