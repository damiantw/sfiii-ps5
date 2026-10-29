// The runtime's sound (16-bit stereo at 44 100 Hz) on an SDL2 device at 48 kHz (the PS5's
// rate). SDL_AudioStream resamples; at 2x/3x the stream takes the samples as 88.2/132.3 kHz,
// so the sound plays faster and higher, in step with the picture (as the desktop host does).
// The queue is kept between `lead` and `max` ms: an empty queue gets `lead` ms of silence first
// (one click instead of many), a full one drops the chunk. MIT, see LICENSE.
#pragma once
#include "audio_stats.h"
#include <SDL.h>
#include <cstdint>
#include <string>

namespace sf3::host {

class AudioOut {
public:
    static constexpr int kInRate = 44100, kOutRate = 48000;
    bool open(std::string& why, const char* device = nullptr);   // after SDL_Init(SDL_INIT_AUDIO)
    void push(const int16_t* lr, int frames);
    void set_speed(int s);
    void set_volume(float v) { volume_ = v < 0 ? 0 : v > 1 ? 1 : v; }
    void pause(bool p);
    int queued_ms() const;
    using Stats = AudioStats;
    Stats stats() const { return stats_; }
    SDL_AudioDeviceID device() const { return dev_; }
    ~AudioOut();
private:
    bool make_stream();
    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioStream* stream_ = nullptr;
    int speed_ = 1;
    float volume_ = 1.0f;
    Stats stats_;
    static constexpr int kLeadMs = 60, kMaxMs = 200;
};

}  // namespace sf3::host
