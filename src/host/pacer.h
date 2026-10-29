// The game's pace on the host clock: field N is due at base + now/speed. More than 100 ms out
// either way (a stall, the menu, a load) moves base so the game goes on from here instead of
// racing to catch up. Speed changes keep the current field where it is. MIT, see LICENSE.
#pragma once
#include <chrono>
#include <cstdint>

namespace sf3::host {

class Pacer {
public:
    using Clock = std::chrono::steady_clock;
    // How long to wait at host time t for machine time now_ns.
    Clock::duration wait(uint64_t now_ns, Clock::time_point t) {
        last_now_ = now_ns;
        if (!started_) { base_ = t - scaled(now_ns); started_ = true; return {}; }
        const auto slack = std::chrono::milliseconds(100);
        Clock::time_point due = base_ + scaled(now_ns);
        if (t > due + slack || due > t + slack) { base_ += t - due; return {}; }
        return due > t ? due - t : Clock::duration{};
    }
    void set_speed(int s, Clock::time_point t) {
        if (s < 1) s = 1;
        if (s == speed_) return;
        speed_ = s;
        if (started_) base_ = t - scaled(last_now_);
    }
    int speed() const { return speed_; }
    void restart() { started_ = false; }   // after a pause: the next field is due at once
private:
    Clock::duration scaled(uint64_t ns) const {
        return std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns / uint64_t(speed_)));
    }
    Clock::time_point base_{};
    bool started_ = false;
    int speed_ = 1;
    uint64_t last_now_ = 0;
};

}  // namespace sf3::host
