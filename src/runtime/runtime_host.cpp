// MIT, see LICENSE.
#include "runtime_host.h"
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <sys/stat.h>

namespace sf3::runtime {

static void make_dirs(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);   // EEXIST is fine
}

std::string disc_identity(const std::string& image) {
    size_t sl = image.find_last_of('/');
    std::string name = sl == std::string::npos ? image : image.substr(sl + 1);
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    for (char& c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) c = '_';
    uint32_t h = 2166136261u;   // FNV-1a of the full path
    for (unsigned char c : image) h = (h ^ c) * 16777619u;
    char tag[16];
    std::snprintf(tag, sizeof tag, "-%08x", h);
    return name + tag;
}

RuntimeHost::RuntimeHost(RuntimeApi api, Launcher launcher, std::string states_dir)
    : api_(std::move(api)), launcher_(std::move(launcher)), states_dir_(std::move(states_dir)) {}

std::string RuntimeHost::slot_path(int slot) const {
    return states_dir_ + "/" + (disc_id_.empty() ? std::string("unknown") : disc_id_) + "/slot" + std::to_string(slot) + ".sf3state";
}

void RuntimeHost::launch(const std::string& image) {
    disc_id_ = disc_identity(image);
    pending_ = Pending::None;
    if (launcher_.launch) launcher_.launch(image);
}

void RuntimeHost::quit_to_launcher() {
    if (api_.stop) api_.stop("Quit was chosen in the menu");
    if (launcher_.quit) launcher_.quit();
    pending_ = Pending::None;
}

bool RuntimeHost::save_state(int slot) {
    error_.clear();
    if (pending_ != Pending::None || (api_.save_busy && api_.save_busy())) { error_ = "a state is still being written"; return false; }
    std::string p = slot_path(slot);
    make_dirs(p.substr(0, p.find_last_of('/')));
    try { api_.request_save(p); }
    catch (const std::exception& e) { error_ = e.what(); return false; }
    pending_ = Pending::Save, pending_slot_ = slot;
    return true;
}

bool RuntimeHost::load_state(int slot) {
    error_.clear();
    if (pending_ != Pending::None || (api_.load_busy && api_.load_busy())) { error_ = "a state is already waiting to load"; return false; }
    try { api_.request_load(slot_path(slot)); }
    catch (const std::exception& e) { error_ = e.what(); return false; }
    pending_ = Pending::Load, pending_slot_ = slot;
    return true;
}

std::string RuntimeHost::state_info(int slot) {
    struct stat st;
    if (stat(slot_path(slot).c_str(), &st) != 0) return {};
    time_t t = st.st_mtime;
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void RuntimeHost::settings_changed(const menu::Settings& s) { speed_ = s.speed; }

void RuntimeHost::frame(menu::Menu& m, bool ff) {
    bool want = m.visible() && m.in_game();
    if (want != paused_) {
        paused_ = want;
        if (api_.paused) api_.paused(want ? 1 : -1);
    }
    speed_ = want ? 1 : m.speed(ff);
    if (pending_ == Pending::Save) {
        if (int r = api_.write_result ? api_.write_result() : 0) {
            m.notify(r > 0 ? "slot " + std::to_string(pending_slot_) + " saved" : "save failed; the slot keeps its old state");
            pending_ = Pending::None;
        }
    } else if (pending_ == Pending::Load) {
        if (int r = api_.load_result ? api_.load_result() : 0) {
            m.notify(r > 0 ? "slot " + std::to_string(pending_slot_) + " loaded" : "load failed: no safe moment came; try again");
            pending_ = Pending::None;
        }
    }
}

}  // namespace sf3::runtime
