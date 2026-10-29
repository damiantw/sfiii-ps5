// Background work for the menu: scanning for discs, patching one, downloading the package.
// One job at a time; the menu polls status() each frame. MIT, see LICENSE.
#pragma once
#include "disc/scan.h"
#include "patch/fetch.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf3::menu {

struct PatchRequest {
    std::string source;        // .cue or .chd
    std::string package;       // SF3Patch.exe or its folder
    std::string addons_dir;    // empty: base package only
    std::string output;        // new .bin; empty with in_place
    bool in_place = false;     // only for .cue sources; the originals are kept as <file>.bak
    bool reproducible = false;  // tests: same stamps as `sf3patch --reproducible`
    std::vector<std::string> enable, disable;   // patch names toggled away from the wizard defaults
};

struct PatchEntry { std::string name, label, category; int priority = 0; bool ticked = false; };
struct PatchList {
    std::string serial, version, package_version;
    std::vector<PatchEntry> entries;
    std::vector<std::string> notes, issues;   // add-on layering notes; conflict warnings/errors
};

// What the wizard would offer for this disc (fast: no track hashing).
PatchList list_patches(const PatchRequest& r);

enum class JobKind { None, Scan, Patch, Fetch };

struct JobStatus {
    uint64_t id = 0;
    JobKind kind = JobKind::None;
    bool running = false, done = false, failed = false;
    std::string title, message;
    std::vector<std::string> log;
    double progress = 0;       // 0..1, or < 0 when unknown
};

class Jobs {
public:
    ~Jobs();
    bool busy() const { return running_; }
    JobStatus status() const;
    JobStatus last(JobKind k) const;   // the most recent finished job of a kind
    void start_scan(const std::string& dir);
    void start_patch(const PatchRequest& r);
    void start_fetch(const std::string& dest, std::shared_ptr<fetch::Transport> t);
    disc::ScanResult scan_result() const;
    void join();
private:
    void run(JobKind kind, const std::string& title, std::function<void()> body);
    void log(const std::string& m);
    void progress(double p);
    mutable std::mutex mu_;
    JobStatus st_, last_[4];
    disc::ScanResult scan_;
    std::atomic<bool> running_{false};
    uint64_t next_id_ = 0;
    std::thread th_;
};

// Runs a patch synchronously (also used by the job): returns the track 1 SHA-1.
std::string patch_disc(const PatchRequest& r, const std::function<void(const std::string&)>& log,
                       const std::function<void(double)>& progress);

}  // namespace sf3::menu
