// SPDX-License-Identifier: MIT
// Console swapchain smoke test: VkScene on the display (VK_KHR_display, FIFO, 4K), the test shader's
// 64x32 pattern 4:3-integer-scaled and moving left/right for ~10 s. Logs to stdout and to
// SF3_TEST_LOG (default /data/sfiii-ps5-test/swapchain.txt).
#include "../../src/vulkan/display.h"
#include "../../src/vulkan/present.h"
#include "../../src/vulkan/vk_scene.h"
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <time.h>
namespace {
#include "../../tests/vk_scene_test_frag.inc"
}
using namespace sf3ps5;
static FILE* g_log;
static void say(const char* fmt, const std::string& s = {}) {
    std::printf(fmt, s.c_str()); std::printf("\n"); std::fflush(stdout);
    if (g_log) { std::fprintf(g_log, fmt, s.c_str()); std::fprintf(g_log, "\n"); std::fflush(g_log); }
}
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main() {
    const char* lp = std::getenv("SF3_TEST_LOG");
    g_log = std::fopen(lp ? lp : "/data/sfiii-ps5-test/swapchain.txt", "w");
    std::string why;
    VkScene s;
    if (!s.init(why, true)) return say("FAIL init: %s", why), 1;
    say("device: %s", s.device_name());
    if (!s.load_fragment(std::vector<uint32_t>(std::begin(kTestFrag), std::end(kTestFrag)), why)) return say("FAIL shader: %s", why), 1;
    const int w = 64, h = 32;
    std::vector<uint32_t> scene(1 + w * h), tex{0x000101};
    scene[0] = w;
    for (int i = 0; i < w * h; ++i) scene[1 + i] = uint32_t(i * 2654435761u) & 0xFFFFFF;
    if (!s.upload(scene, tex, why) || !s.draw(w, h, 0, false, why)) return say("FAIL draw: %s", why), 1;
    Display d;
    if (!d.open(s, why)) return say("FAIL display: %s", why), 1;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%dx%d", d.width(), d.height());
    say("swapchain %s", buf);
    Rect r = place(d.width(), d.height(), w, h, Aspect::Classic4x3);
    const double t0 = now();
    int frames = 0;
    while (now() - t0 < 10.0) {
        Rect m = r;
        m.x = r.x + int((frames % 240 < 120 ? frames % 120 : 120 - frames % 120) * 2);
        if (!d.frame(m, true, why)) return say("FAIL frame: %s", why), 1;
        ++frames;
    }
    std::snprintf(buf, sizeof buf, "%d frames, %.2f fps", frames, frames / (now() - t0));
    say("PASS %s", buf);
    return 0;
}
