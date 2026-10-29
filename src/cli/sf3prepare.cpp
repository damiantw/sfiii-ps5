// sf3prepare: the console's first-launch step on PC (src/prepare): IMAGE DATA_DIR PROFILES_DIR ->
// DATA_DIR/cache/<content_id>-abi<N>, the module set SF3_X64_SET takes. MIT, see LICENSE.
#include "prepare/prepare.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: sf3prepare IMAGE DATA_DIR PROFILES_DIR\n"); return 2; }
    int last = -1;
    auto r = sf3::prepare::prepare_disc(argv[1], argv[2], argv[3], [&](double p, const std::string& what) {
        int pc = int(p * 100);
        if (pc != last) { std::fprintf(stderr, "%3d%% %s\n", pc, what.c_str()); last = pc; }
    });
    if (!r.ok) { std::fprintf(stderr, "sf3prepare: %s\n", r.why.c_str()); return 1; }
    std::printf("%s\n", r.set_dir.c_str());
    std::fprintf(stderr, "%s%s\n", r.cached ? "cached " : "", r.content_id.c_str());
    return 0;
}
