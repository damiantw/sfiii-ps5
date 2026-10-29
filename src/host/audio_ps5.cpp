// MIT, see LICENSE.
#include "audio_ps5.h"
#include <algorithm>
#include <vector>
#if defined(SF3_PS5_CONSOLE)
#include "ps5/sce.h"
#endif

namespace sf3::host {

void Resampler::run(const int16_t* lr, int frames, float volume, std::deque<int16_t>& out) {
    // pos_ is in input frames, relative to the chunk; -1..0 interpolates from the last chunk's end
    for (;;) {
        int i = int(pos_ < 0 ? -1 : pos_);
        if (i + 1 >= frames) break;
        double f = pos_ - i;
        for (int c = 0; c < 2; c++) {
            double a = i < 0 ? last_[c] : lr[i * 2 + c], b = lr[(i + 1) * 2 + c];
            out.push_back(int16_t(std::clamp((a + (b - a) * f) * volume, -32768.0, 32767.0)));
        }
        pos_ += step_;
    }
    if (frames > 0) { last_[0] = lr[(frames - 1) * 2], last_[1] = lr[(frames - 1) * 2 + 1]; pos_ -= frames; }
}

void AudioOut::set_speed(int s) {
    std::lock_guard<std::mutex> l(mu_);
    speed_ = std::clamp(s, 1, 3);
    rs_.set_ratio(double(kInRate) * speed_ / kOutRate);
}

AudioOut::Stats AudioOut::stats() const {
    std::lock_guard<std::mutex> l(mu_);
    return stats_;
}

int AudioOut::queued_ms() const {
    std::lock_guard<std::mutex> l(mu_);
    return int(queue_.size() / 2 * 1000 / kOutRate);
}

void AudioOut::push(const int16_t* lr, int frames) {
    if (!run_ || frames <= 0) return;
    std::deque<int16_t> out;
    std::lock_guard<std::mutex> l(mu_);
    stats_.count(lr, frames);
    rs_.run(lr, frames, volume_, out);
    if (queue_.empty()) { ++stats_.underruns; queue_.insert(queue_.end(), size_t(kOutRate) * kLeadMs / 1000 * 2, 0); }
    if ((queue_.size() + out.size()) / 2 * 1000 / kOutRate > size_t(kMaxMs)) { ++stats_.dropped; return; }
    queue_.insert(queue_.end(), out.begin(), out.end());
}

#if defined(SF3_PS5_CONSOLE)
bool AudioOut::open(std::string& why, const char*) {
    sceAudioOutInit();   // fails harmlessly when already done
    port_ = sceAudioOutOpen(ps5::kUserSystem, ps5::kAudioMain, 0, kGrain, kOutRate, ps5::kAudioS16Stereo);
    if (port_ < 0) return why = "sceAudioOutOpen failed: " + std::to_string(port_), false;
    set_speed(1);
    run_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void AudioOut::loop() {
    alignas(64) int16_t grain[kGrain * 2];
    while (run_) {
        {
            std::lock_guard<std::mutex> l(mu_);
            size_t n = paused_ ? 0 : std::min(queue_.size(), size_t(kGrain) * 2);
            std::copy_n(queue_.begin(), n, grain);
            queue_.erase(queue_.begin(), queue_.begin() + n);
            std::fill(grain + n, grain + kGrain * 2, 0);
        }
        sceAudioOutOutput(port_, grain);
    }
}

AudioOut::~AudioOut() {
    run_ = false;
    if (thread_.joinable()) thread_.join();
    if (port_ >= 0) { sceAudioOutOutput(port_, nullptr); sceAudioOutClose(port_); }
}
#else
// PC: the class builds for the resampler test; there is no device.
bool AudioOut::open(std::string& why, const char*) { return why = "no libSceAudioOut", false; }
void AudioOut::loop() {}
AudioOut::~AudioOut() = default;
#endif

}  // namespace sf3::host
