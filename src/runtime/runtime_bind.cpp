// RuntimeApi from the patched saturn-recomp runtime (sfiii-native's tools/runtime-diagnostics.patch).
// Compiled only against a runtime checkout (SF3_PS5_RUNTIME_DIR); the console build links it with the
// runtime and the generated code. MIT, see LICENSE.
#include "runtime_host.h"
#include "saturn.h"
#include "host/host.h"

namespace sf3::runtime {

RuntimeApi saturn_runtime_api() {
    RuntimeApi a;
    a.request_save = [](const std::string& p) { state_request_save(p); };
    a.request_load = [](const std::string& p) { state_request_load(p); };
    a.save_busy = [] { return state_save_busy(); };
    a.load_busy = [] { return state_load_busy(); };
    a.write_result = [] { return state_write_result(); };
    a.load_result = [] { return state_load_result(); };
    a.stop = [](const char* why) { state_stop(why); };
    a.paused = [](int d) { g_host_paused += d; };
    return a;
}

}  // namespace sf3::runtime
