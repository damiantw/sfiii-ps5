// menu::Host on top of the saturn-recomp runtime (sfiii-native's patched build): save states
// through the runtime's native states, pause while the overlay is open, the speed for the pacer.
// The runtime calls go through RuntimeApi, so the logic is tested without a game
// (tests/runtime_host_test.cpp); runtime_bind.cpp fills it in from the real runtime.
// MIT, see LICENSE.
#pragma once
#include "menu/menu.h"
#include <atomic>
#include <functional>
#include <string>

namespace sf3::runtime {

struct RuntimeApi {
    // state.cpp: these throw std::runtime_error with a message the player can read
    std::function<void(const std::string&)> request_save;
    std::function<void(const std::string&)> request_load;   // checks the file first
    std::function<bool()> save_busy, load_busy;
    std::function<int()> write_result;   // 1 written, -1 failed, 0 nothing new (each result read once)
    std::function<int()> load_result;    // 1 loaded, -1 no safe moment in time, 0 nothing new
    std::function<void(const char*)> stop;   // state_stop: the run ends (quit to the launcher)
    std::function<void(int)> paused;         // g_host_paused: >0 = no VBlank is not a stall
};

// Boots a disc, quits to the launcher: process-level, so the platform layer provides them
// (a fresh runtime run per disc). Disc identity for SF3_STATE_ID comes from the image path.
struct Launcher {
    std::function<void(const std::string& image)> launch;
    std::function<void()> quit;
};

class RuntimeHost : public menu::Host {
public:
    // states_dir: where slot files live (one folder per disc identity, made on demand)
    RuntimeHost(RuntimeApi api, Launcher launcher, std::string states_dir);

    void launch(const std::string& image) override;
    void resume() override {}
    void quit_to_launcher() override;
    bool save_state(int slot) override;
    bool load_state(int slot) override;
    std::string state_info(int slot) override;   // file time, local, "YYYY-MM-DD HH:MM"
    std::string last_error() override { return error_; }
    void settings_changed(const menu::Settings& s) override;

    // Once per host frame, before the menu's draw(): pause while the overlay is up in game,
    // the speed the pacer reads, and the outcome of a queued save/load into the menu.
    void frame(menu::Menu& m, bool fast_forward_held);
    int speed() const { return speed_.load(); }   // emulated frames per host frame (host_pace thread)
    bool paused() const { return paused_; }

    std::string slot_path(int slot) const;
    const std::string& disc_id() const { return disc_id_; }
    void set_disc_id(std::string id) { disc_id_ = std::move(id); }   // also set by launch()

private:
    RuntimeApi api_;
    Launcher launcher_;
    std::string states_dir_, disc_id_, error_;
    std::atomic<int> speed_{1};
    bool paused_ = false;
    enum class Pending { None, Save, Load } pending_ = Pending::None;
    int pending_slot_ = -1;
};

// The real runtime's calls (runtime_bind.cpp; only in a build against the runtime).
RuntimeApi saturn_runtime_api();

// A file-name-safe identity for an image path: its file name minus the extension, plus a short
// hash of the whole path (two images with the same name stay apart).
std::string disc_identity(const std::string& image);

}  // namespace sf3::runtime
