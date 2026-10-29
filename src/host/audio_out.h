// The app's sound output: SDL2 on PC (audio_sdl2.h), libSceAudioOut on the console (audio_ps5.h).
// Both take the runtime's 16-bit stereo at 44 100 Hz and play it at 48 kHz. MIT, see LICENSE.
#pragma once
#if defined(SF3_PS5_CONSOLE)
#include "audio_ps5.h"
#else
#include "audio_sdl2.h"
#endif
