// Launching a disc and quitting to the launcher restart the app: one game per process, so a
// disc always boots into a fresh runtime (and its own scenario's code), and quitting never has
// to unwind a running machine. The app's mode is in its arguments: none = the launcher,
// --cue <image> = that disc.
//
// PC: execv of the running binary. PS5 (SF3_PS5_CONSOLE builds): sceSystemServiceLaunchApp with
// our own title ID, then the process exits; this is unverified on the console. Until it is
// proven there, a failed relaunch falls back to exiting cleanly: the player comes back to the
// home screen and starts the app again (saves and settings are already on disk).
// See docs/host.md. MIT, see LICENSE.
#pragma once
#include <string>
#include <vector>

namespace sf3::host {

struct AppArgs {
    std::string cue;                 // empty: the launcher
    static AppArgs parse(int argc, char** argv);
    std::vector<std::string> argv() const;   // without the program name
};

// Does not return on success. On failure, why says what happened and the caller exits.
bool relaunch(const AppArgs& next, std::string& why);

}  // namespace sf3::host
