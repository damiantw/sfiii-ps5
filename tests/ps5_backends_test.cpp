// The console's pad decode and audio resampler, on PC (src/menu/pad_ps5.cpp, src/host/audio_ps5.cpp). MIT.
#include "host/audio_ps5.h"
#include "menu/pad_ps5.h"
#include "ps5/sce.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    using namespace sf3;
    const uint8_t centre[4] = {128, 128, 128, 128}, corner[4] = {0, 255, 255, 0};
    auto p = menu::decode_sce_pad(ps5::kCross | ps5::kUp | ps5::kTouchPad | ps5::kR1, centre, 0, 255);
    CHECK(p.connected && p.cross && p.up && p.touchpad && p.r1 && !p.circle && !p.options);
    CHECK(std::fabs(p.lx) < 0.01f && p.r2 == 1.0f && p.l2 == 0.0f);
    p = menu::decode_sce_pad(ps5::kOptions | ps5::kL3, corner, 128, 0);
    CHECK(p.options && p.l3 && p.lx == -1.0f && p.ly == 1.0f && p.rx == 1.0f && p.ry == -1.0f);

    // 1 s of a 441 Hz tone at 44.1 kHz, in chunks of 735 frames: 48000 +- 1 frames out, no clicks
    host::Resampler rs;
    std::deque<int16_t> out;
    std::vector<int16_t> chunk(735 * 2);
    int n = 0;
    for (int c = 0; c < 60; c++) {
        for (int i = 0; i < 735; i++, n++) chunk[i * 2] = chunk[i * 2 + 1] = int16_t(10000 * std::sin(2 * M_PI * 441 * n / 44100.0));
        rs.run(chunk.data(), 735, 1.0f, out);
    }
    CHECK(std::abs(int(out.size() / 2) - 48000) <= 1);
    int maxstep = 0;
    for (size_t i = 2; i < out.size(); i += 2) maxstep = std::max(maxstep, std::abs(out[i] - out[i - 2]));
    CHECK(maxstep < 700);   // 441 Hz at 10000 peak: ~577 per 48 kHz sample at most
    // 2x: half the output frames
    host::Resampler r2;
    r2.set_ratio(88200.0 / 48000);
    std::deque<int16_t> o2;
    for (int c = 0; c < 60; c++) r2.run(chunk.data(), 735, 1.0f, o2);
    CHECK(std::abs(int(o2.size() / 2) - 24000) <= 1);
    std::printf("%s\n", fails ? "ps5_backends_test: FAIL" : "ps5_backends_test: ok");
    return fails != 0;
}
