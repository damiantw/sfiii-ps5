// The runtime's sound (16-bit stereo, 44 100 Hz x speed) on libSceAudioOut at 48 kHz (the
// console). A linear resampler feeds a queue; a thread hands the system 256-frame grains
// (sceAudioOutOutput blocks until the previous one has played), with silence when the queue runs
// dry. The queue limits match audio_sdl2.h: `lead` ms of silence first after a dry spell, and a
// chunk that would push it past `max` ms is dropped. MIT, see LICENSE.
#pragma once
#include "audio_stats.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace sf3::host {

// 44.1 kHz x speed -> 48 kHz, linear, keeping its phase between chunks (testable on PC).
class Resampler {
public:
    void set_ratio(double in_per_out) { step_ = in_per_out; }
    // appends to out (interleaved stereo)
    void run(const int16_t* lr, int frames, float volume, std::deque<int16_t>& out);
private:
    double step_ = 44100.0 / 48000.0, pos_ = 0;
    int16_t last_[2] = {0, 0};
};

class AudioOut {
public:
    static constexpr int kInRate = 44100, kOutRate = 48000, kGrain = 256;
    bool open(std::string& why, const char* device = nullptr);
    void push(const int16_t* lr, int frames);
    void set_speed(int s);
    void set_volume(float v) { volume_ = v < 0 ? 0 : v > 1 ? 1 : v; }
    void pause(bool p) { paused_ = p; }
    int queued_ms() const;
    using Stats = AudioStats;
    Stats stats() const;
    ~AudioOut();
private:
    void loop();
    int32_t port_ = -1;
    std::thread thread_;
    std::atomic<bool> run_{false}, paused_{false};
    mutable std::mutex mu_;
    std::deque<int16_t> queue_;
    Resampler rs_;
    int speed_ = 1;
    float volume_ = 1.0f;
    Stats stats_;
    static constexpr int kLeadMs = 60, kMaxMs = 200, kTargetMs = 90;
};

}  // namespace sf3::host
