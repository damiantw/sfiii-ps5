// MIT, see LICENSE.
#include "audio_sdl2.h"
#include <algorithm>
#include <vector>

namespace sf3::host {

bool AudioOut::open(std::string& why, const char* device) {
    SDL_AudioSpec want{}, have{};
    want.freq = kOutRate, want.format = AUDIO_S16SYS, want.channels = 2, want.samples = 1024;
    dev_ = SDL_OpenAudioDevice(device, 0, &want, &have, 0);
    if (!dev_) return why = std::string("SDL_OpenAudioDevice: ") + SDL_GetError(), false;
    if (!make_stream()) return why = std::string("SDL_NewAudioStream: ") + SDL_GetError(), false;
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

bool AudioOut::make_stream() {
    if (stream_) SDL_FreeAudioStream(stream_);
    stream_ = SDL_NewAudioStream(AUDIO_S16SYS, 2, kInRate * speed_, AUDIO_S16SYS, 2, kOutRate);
    return stream_ != nullptr;
}

void AudioOut::set_speed(int s) {
    s = std::clamp(s, 1, 3);
    if (s == speed_ || !dev_) { speed_ = s; return; }
    speed_ = s;
    make_stream();
}

void AudioOut::pause(bool p) { if (dev_) SDL_PauseAudioDevice(dev_, p ? 1 : 0); }

int AudioOut::queued_ms() const {
    return dev_ ? int(SDL_GetQueuedAudioSize(dev_) / 4 * 1000 / kOutRate) : 0;
}

void AudioOut::push(const int16_t* lr, int frames) {
    if (!dev_ || !stream_ || frames <= 0) return;
    if (SDL_GetQueuedAudioSize(dev_) == 0) {
        ++stats_.underruns;
        std::vector<int16_t> silence(size_t(kOutRate) * kLeadMs / 1000 * 2, 0);
        SDL_QueueAudio(dev_, silence.data(), Uint32(silence.size() * 2));
    }
    stats_.count(lr, frames);
    std::vector<int16_t> in(lr, lr + size_t(frames) * 2);
    if (volume_ < 1.0f) for (auto& v : in) v = int16_t(v * volume_);
    SDL_AudioStreamPut(stream_, in.data(), int(in.size() * 2));
    int avail = SDL_AudioStreamAvailable(stream_);
    if (avail <= 0) return;
    std::vector<int16_t> out(size_t(avail) / 2);
    int got = SDL_AudioStreamGet(stream_, out.data(), avail);
    if (got <= 0) return;
    if (queued_ms() + got / 4 * 1000 / kOutRate > kMaxMs) { ++stats_.dropped; return; }
    SDL_QueueAudio(dev_, out.data(), Uint32(got));
}

AudioOut::~AudioOut() {
    if (stream_) SDL_FreeAudioStream(stream_);
    if (dev_) SDL_CloseAudioDevice(dev_);
}

}  // namespace sf3::host
