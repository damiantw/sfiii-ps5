// User settings shown in the menu, stored as key=value text. MIT, see LICENSE.
#pragma once
#include <string>

namespace sf3::menu {

enum class Aspect { Ratio4x3, Ratio16x9 };

struct Settings {
    int speed = 1;                 // 1x, 2x, 3x
    bool fast_forward_hold = true; // R2 held = fast forward
    Aspect aspect = Aspect::Ratio4x3;
    static constexpr int kMaxRenderScale = 6;  // 6x (1920x1440) is the largest tested; 9x would be the last step inside 2160
    int render_scale = 1;          // 3D (VDP1 polygons) drawn at 1x..9x on the GPU (menu offers 1-4 and 6)
    float ui_scale = 2.0f;         // 1.5 - 3.0 on a TV
    bool sharp_pixels = true;      // nearest-neighbour vs linear
    bool low_latency = false;      // two swapchain images instead of three (applies at next start)
    bool allow_download = false;   // opt-in: fetch the translation package from the project's server
    bool include_addons = true;    // apply the V25+ add-ons when patching
    bool patch_in_place = false;   // false: write a new <name>-v25_1 image beside the original
    std::string data_dir = "/data/shiningforceiii";

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace sf3::menu
