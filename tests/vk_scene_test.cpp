// VkScene on any Vulkan device (lavapipe on the box): runs a test shader with geometry.hlsl's
// layout through both passes and checks every pixel. Optional argv[1]: the desktop's
// geometry.hlsl SPIR-V. If given, it only checks that this exact shader builds a pipeline (layout compatibility).
#include "../src/vulkan/vk_scene.h"
#include "../src/vulkan/present.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
namespace { 
#include "vk_scene_test_frag.inc"
}
using namespace sf3ps5;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s %s\n", __LINE__, #x, why.c_str()); return 1; } } while (0)
int main(int argc, char** argv) {
    std::string why;
    // presentation math (4K)
    Rect r = place(3840, 2160, 320, 224, Aspect::Classic4x3);
    CHECK(r.h == 2016 && r.w == 2688 && r.x == 576 && r.y == 72);         // integer 9x, square pixels
    r = place(3840, 2160, 352, 240, Aspect::Classic4x3);
    CHECK(r.h == 2160 && r.w == 2880 && r.x == 480);                      // 9x = 2160
    r = place(3840, 2160, 320, 224, Aspect::Wide16x9, 0);
    CHECK(r.w == 2688);                                                    // no extra columns: stays 4:3
    r = place(3840, 2160, 320, 224, Aspect::Wide16x9, 64);
    CHECK(r.w <= 3584 && r.w > 2688 && r.x == (3840 - r.w) / 2);           // capped at 16:9 of the box height

    VkScene s;
    CHECK(s.init(why));
    std::printf("device: %s\n", s.device_name());
    CHECK(s.load_fragment(std::vector<uint32_t>(std::begin(kTestFrag), std::end(kTestFrag)), why));
    const int w = 64, h = 32;
    std::vector<uint32_t> scene(1 + w * h), tex{0x000101};
    scene[0] = w;
    for (int i = 0; i < w * h; ++i) scene[1 + i] = uint32_t(i * 2654435761u) & 0xFFFFFF;
    CHECK(s.upload(scene, tex, why));
    CHECK(s.draw(w, h, 0, false, why));
    std::vector<uint8_t> px;
    CHECK(s.read_rgba(px, why));
    for (int i = 0; i < w * h; ++i) {
        uint32_t v = scene[1 + i] ^ tex[0];
        if (px[i * 4] != (v >> 16 & 255) || px[i * 4 + 1] != (v >> 8 & 255) || px[i * 4 + 2] != (v & 255)) { why = "pixel " + std::to_string(i); CHECK(false); }
    }
    CHECK(s.draw(w, h, 1, true, why));                    // the effects pass samples the geometry output
    CHECK(s.read_rgba(px, why));
    int off = 0;
    for (int i = 0; i < w * h; ++i) { uint32_t v = scene[1 + i] ^ tex[0]; off += px[i * 4] != 255 - (v >> 16 & 255); }
    CHECK(off == 0);
    // present: 4K, 4:3 integer scale, nearest; the last pass (effects output) lands in the box, black elsewhere
    {
        CHECK(s.draw(w, h, 0, false, why));                // back to the geometry output (known pixels)
        const int W = 3840, H = 2160;
        Rect pr = place(W, H, w, h, Aspect::Classic4x3);
        std::vector<uint8_t> sc;
        CHECK(s.present_offscreen(W, H, pr, true, sc, why));
        CHECK(sc.size() == size_t(W) * H * 4);
        auto at = [&](int x, int y) { return &sc[(size_t(y) * W + x) * 4]; };
        CHECK(at(0, 0)[0] == 0 && at(0, 0)[3] == 255 && at(W - 1, H - 1)[0] == 0);     // pillarbox is black
        int bad = 0;
        for (int sy = 0; sy < h; ++sy) for (int sx = 0; sx < w; ++sx) {
            int x = pr.x + (2 * sx + 1) * pr.w / (2 * w), y = pr.y + (2 * sy + 1) * pr.h / (2 * h);   // texel centres
            uint32_t v = scene[1 + sy * w + sx] ^ tex[0];
            const uint8_t* q = at(x, y);
            bad += q[0] != (v >> 16 & 255) || q[1] != (v >> 8 & 255) || q[2] != (v & 255);
        }
        why = std::to_string(bad) + " texels off";
        CHECK(bad == 0);
        std::printf("present 4K: box %dx%d at %d,%d ok\n", pr.w, pr.h, pr.x, pr.y);
    }
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        CHECK(b.size() % 4 == 0 && !b.empty());
        std::vector<uint32_t> code(b.size() / 4);
        std::memcpy(code.data(), b.data(), b.size());
        CHECK(s.load_fragment(code, why));
        std::printf("geometry.hlsl SPIR-V: pipeline ok\n");
    }
    std::printf("vk_scene: all checks passed\n");
    return 0;
}
