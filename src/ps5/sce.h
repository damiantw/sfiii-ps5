// The few PS5 system-library calls the console app makes (libScePad, libSceAudioOut,
// libSceUserService), declared from the public API shape only. MIT, see LICENSE.
#pragma once
#include <cstdint>

extern "C" {
// UserService
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetInitialUser(int32_t* user);
int sceUserServiceGetForegroundUser(int32_t* user);

// Pad. Only the leading fields of the state are read; the buffer is larger than the system's.
int scePadInit(void);
int scePadOpen(int32_t user, int32_t type, int32_t index, const void* param);
int scePadGetHandle(int32_t user, int32_t type, int32_t index);
int scePadReadState(int32_t handle, void* data);
int scePadClose(int32_t handle);

// AudioOut
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t freq, uint32_t param);
int sceAudioOutOutput(int32_t handle, const void* buf);   // blocks until the previous grain has played
int sceAudioOutClose(int32_t handle);
}

namespace sf3::ps5 {
constexpr int32_t kUserSystem = 0xFF;      // the system user, for audio
constexpr int32_t kPadStandard = 0;
constexpr int32_t kAudioMain = 0;
constexpr uint32_t kAudioS16Stereo = 1;

// ScePadData's leading fields
struct PadHead {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry;
    uint8_t l2, r2;
};
enum PadButton : uint32_t {
    kL3 = 0x2, kR3 = 0x4, kOptions = 0x8, kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80,
    kL2 = 0x100, kR2 = 0x200, kL1 = 0x400, kR1 = 0x800, kTriangle = 0x1000, kCircle = 0x2000,
    kCross = 0x4000, kSquare = 0x8000, kTouchPad = 0x100000,
};
}  // namespace sf3::ps5
