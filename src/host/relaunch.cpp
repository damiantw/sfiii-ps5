// MIT, see LICENSE.
#include <cstdio>
#include "relaunch.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#if defined(SF3_PS5_CONSOLE)
// PS5 SystemService/UserService (libSceSystemService, libSceUserService). Declarations from the
// public API shape only.
extern "C" {
struct SceSystemServiceLaunchAppParam { uint32_t size; int32_t user_id; int32_t app_opt; uint64_t crash_report; uint32_t check_flag; };
int sceSystemServiceLaunchApp(const char* title_id, const char** argv, SceSystemServiceLaunchAppParam* p);
int sceSystemServiceLoadExec(const char* path, const char* const* argv);
int sceUserServiceGetForegroundUser(int32_t* user);
}
#ifndef SF3_PS5_TITLE_ID
#error "SF3_PS5_TITLE_ID: the app's own title ID"
#endif
#endif

namespace sf3::host {

AppArgs AppArgs::parse(int argc, char** argv) {
    AppArgs a;
    for (int i = 0; i < argc; i++)   // from 0: a LoadExec'd process may get the arguments alone
        if (!std::strcmp(argv[i], "--cue") && i + 1 < argc) a.cue = argv[++i];
    return a;
}

std::vector<std::string> AppArgs::argv() const {
    std::vector<std::string> v;
    if (!cue.empty()) v = {"--cue", cue};
    return v;
}

bool relaunch(const AppArgs& next, std::string& why) {
    std::vector<std::string> args = next.argv();
#if defined(SF3_PS5_CONSOLE)
    // A running title may not LaunchApp itself (0x8094000F); LoadExec replaces this process
    // with a fresh run of our own eboot and the new arguments. LaunchApp stays as the fallback.
    std::vector<const char*> ex;
    for (auto& s : args) ex.push_back(s.c_str());
    ex.push_back(nullptr);
    int le = sceSystemServiceLoadExec("/app0/eboot.bin", ex.data());
    std::fprintf(stderr, "relaunch: sceSystemServiceLoadExec returned %d\n", le);
    std::vector<const char*> av{SF3_PS5_TITLE_ID};   // argv[0] is the title ID
    for (auto& s : args) av.push_back(s.c_str());
    av.push_back(nullptr);
    SceSystemServiceLaunchAppParam p{};
    p.size = sizeof p;
    if (sceUserServiceGetForegroundUser(&p.user_id) != 0) p.user_id = -1;
    int r = sceSystemServiceLaunchApp(SF3_PS5_TITLE_ID, av.data(), &p);
    if (r != 0) { why = "LoadExec " + std::to_string(le) + ", sceSystemServiceLaunchApp failed: " + std::to_string(r); return false; }
    std::_Exit(0);   // the system starts the new instance once this one is gone
#else
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) { why = "cannot find the running binary"; return false; }
    self[n] = 0;
    std::vector<char*> av{self};
    for (auto& s : args) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    execv(self, av.data());
    why = std::string("execv: ") + std::strerror(errno);
    return false;
#endif
}

}  // namespace sf3::host
