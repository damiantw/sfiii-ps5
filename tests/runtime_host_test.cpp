// RuntimeHost against a fake runtime: queued saves/loads report back through the menu, pause
// follows the overlay in game, speed and fast-forward reach the pacer, errors read through.
#include "runtime/runtime_host.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace sf3;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct Fake {
    std::string saving, loading, launched, stopped;
    int write_result = 0, load_result = 0, paused = 0, quits = 0;
    bool states = true;
};

int main() {
    char tmpl[] = "/tmp/rthostXXXXXX";
    std::string dir = mkdtemp(tmpl);
    Fake f;
    runtime::RuntimeApi api;
    api.request_save = [&](const std::string& p) {
        if (!f.states) throw std::runtime_error("This build has no native state continuations");
        f.saving = p;
    };
    api.request_load = [&](const std::string& p) {
        struct stat st;
        if (stat(p.c_str(), &st) != 0) throw std::runtime_error("Missing or invalid native state");
        f.loading = p;
    };
    api.save_busy = [&] { return !f.saving.empty() && f.write_result == 0; };
    api.load_busy = [&] { return !f.loading.empty() && f.load_result == 0; };
    api.write_result = [&] { int r = f.write_result; f.write_result = 0; if (r) f.saving.clear(); return r; };
    api.load_result = [&] { int r = f.load_result; f.load_result = 0; if (r) f.loading.clear(); return r; };
    api.stop = [&](const char* why) { f.stopped = why; };
    api.paused = [&](int d) { f.paused += d; };
    runtime::Launcher l{[&](const std::string& img) { f.launched = img; }, [&] { f.quits++; }};
    runtime::RuntimeHost host(api, l, dir + "/states");

    menu::Settings s;
    menu::Jobs jobs;
    menu::Menu m(host, s, jobs, "");

    host.launch("/data/shiningforceiii/Shining Force III (USA).cue");
    CHECK(f.launched == "/data/shiningforceiii/Shining Force III (USA).cue");
    CHECK(host.disc_id().rfind("Shining_Force_III__USA_-", 0) == 0);
    CHECK(runtime::disc_identity("/a/x.cue") != runtime::disc_identity("/b/x.cue"));
    m.set_in_game(true);
    m.close();

    // closed: speed from the settings, R2 held = 3x, not paused
    s.speed = 2;
    host.frame(m, false); CHECK(host.speed() == 2); CHECK(!host.paused()); CHECK(f.paused == 0);
    host.frame(m, true);  CHECK(host.speed() == 3);
    // overlay open in game: paused at 1x, unpaused once when it closes
    m.open(menu::Page::InGame);
    host.frame(m, true); CHECK(host.paused()); CHECK(f.paused == 1); CHECK(host.speed() == 1);
    host.frame(m, false); CHECK(f.paused == 1);
    m.close();
    host.frame(m, false); CHECK(!host.paused()); CHECK(f.paused == 0);

    // save: queued, then the result reaches the menu; a second save while busy is refused
    CHECK(host.state_info(3).empty());
    CHECK(host.save_state(3));
    CHECK(f.saving == host.slot_path(3));
    struct stat st;
    CHECK(stat((dir + "/states/" + host.disc_id()).c_str(), &st) == 0);   // folder made
    CHECK(!host.save_state(4)); CHECK(host.last_error() == "a state is still being written");
    host.frame(m, false); CHECK(m.status_text().empty());   // nothing yet
    if (FILE* fp = std::fopen(host.slot_path(3).c_str(), "wb")) { std::fputs("state", fp); std::fclose(fp); }
    f.write_result = 1;
    host.frame(m, false); CHECK(m.status_text() == "slot 3 saved");
    CHECK(host.state_info(3) == "saved just now");

    // a failed write
    CHECK(host.save_state(5)); f.write_result = -1;
    host.frame(m, false); CHECK(m.status_text() == "save failed; the slot keeps its old state");

    // load: a missing slot fails at once with the runtime's reason; a present one queues
    CHECK(!host.load_state(7)); CHECK(host.last_error() == "Missing or invalid native state");
    CHECK(host.load_state(3)); CHECK(f.loading == host.slot_path(3));
    f.load_result = -1; host.frame(m, false); CHECK(m.status_text() == "load failed: no safe moment came; try again");
    CHECK(host.load_state(3)); f.load_result = 1; host.frame(m, false); CHECK(m.status_text() == "slot 3 loaded");

    // a build without native states: the reason reaches the menu
    f.states = false;
    CHECK(!host.save_state(1)); CHECK(host.last_error() == "This build has no native state continuations");

    host.quit_to_launcher();
    CHECK(f.stopped == "Quit was chosen in the menu"); CHECK(f.quits == 1);

    std::system(("rm -rf '" + dir + "'").c_str());
    std::printf("runtime_host_test: %s\n", fails ? "FAILURES" : "ok");
    return fails ? 1 : 0;
}
