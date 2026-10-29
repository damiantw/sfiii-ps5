// MIT, see LICENSE.
// What the game sent to the audio output, for the quit report: tells silence from sound.
#pragma once
#include <cstdint>

namespace sf3::host {

struct AudioStats {
    int underruns = 0, dropped = 0;
    long long frames = 0, loud = 0;   // loud: frames with a channel above 64/32768
    int peak = 0;
    double sumsq = 0;
    void count(const int16_t* lr, int n) {
        for (int i = 0; i < n; i++) {
            int a = lr[2 * i] < 0 ? -lr[2 * i] : lr[2 * i], b = lr[2 * i + 1] < 0 ? -lr[2 * i + 1] : lr[2 * i + 1];
            int m = a > b ? a : b;
            peak = m > peak ? m : peak, loud += m > 64;
            sumsq += double(lr[2 * i]) * lr[2 * i] + double(lr[2 * i + 1]) * lr[2 * i + 1];
        }
        frames += n;
    }
};

}  // namespace sf3::host
