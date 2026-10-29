// The in-app menu (Dear ImGui): a launcher with the four disc slots, disc status and the
// patch/download pages, and an in-game overlay with save states, speed and 4:3/16:9.
// Renderer- and input-agnostic: the platform layer owns the ImGui context and backends and
// calls draw() between ImGui::NewFrame() and ImGui::Render(). MIT, see LICENSE.
#pragma once
#include "jobs.h"
#include "pad.h"
#include "settings.h"
#include <memory>
#include <string>

namespace sf3::menu {

constexpr int STATE_SLOTS = 10;

// Implemented by the emulator/runtime.
class Host {
public:
    virtual ~Host() = default;
    virtual void launch(const std::string& image) = 0;          // boot a disc
    virtual void resume() {}                                     // close the overlay
    virtual void quit_to_launcher() = 0;
    virtual bool save_state(int slot) = 0;
    virtual bool load_state(int slot) = 0;
    virtual std::string state_info(int slot) { (void)slot; return {}; }   // "" = empty, else a timestamp
    virtual void settings_changed(const Settings&) {}
    virtual std::shared_ptr<fetch::Transport> transport() { return nullptr; }   // nullptr: downloads unavailable
};

enum class Page { Launcher, Patch, Download, Settings, InGame };

class Menu {
public:
    Menu(Host& host, Settings& settings, Jobs& jobs, std::string settings_path);
    void open(Page p);                 // Launcher at boot; InGame from the pause button
    void close();
    bool visible() const { return visible_; }
    Page page() const { return page_; }
    bool in_game() const { return in_game_; }
    void set_in_game(bool g) { in_game_ = g; }
    void draw();                       // inside an ImGui frame
    void handle(const PadEvents& e);   // before draw(): toggle, L1/R1 tabs

    void set_reproducible(bool r) { reproducible_ = r; }   // tests: deterministic patch output

    // Helpers for the runtime.
    float aspect() const;                               // 4/3 or 16/9
    int speed(bool fast_forward_held) const;            // emulated frames per host frame
private:
    void launcher();
    void patch_page();
    void download_page();
    void settings_page();
    void in_game_page();
    void job_status();
    void changed();
    std::string package_path() const;
    std::string addons_dir() const;
    Host& host_;
    Settings& s_;
    Jobs& jobs_;
    std::string settings_path_;
    uint64_t handled_job_ = 0;
    int select_ = 2;                 // frames left to force page_'s tab selected
    bool reproducible_ = false;
    bool visible_ = true, in_game_ = false, scanned_ = false;
    Page page_ = Page::Launcher;
    int patch_target_ = -1;                     // index into the scan's candidates
    PatchList list_;
    std::string list_error_;
    std::vector<bool> ticks_;
    int state_slot_ = 0;
    std::string toast_;
};

}  // namespace sf3::menu
