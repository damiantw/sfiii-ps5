// MIT, see LICENSE.
#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sf3::menu {

bool Settings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        std::string k = line, v = eq + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        auto b = [&] { return v == "1" || v == "true"; };
        if (k == "speed") { int s = atoi(v.c_str()); if (s >= 1 && s <= 3) speed = s; }
        else if (k == "fast_forward_hold") fast_forward_hold = b();
        else if (k == "aspect") aspect = v == "16:9" ? Aspect::Ratio16x9 : Aspect::Ratio4x3;
        else if (k == "ui_scale") { float s = float(atof(v.c_str())); if (s >= 1.0f && s <= 3.0f) ui_scale = s; }
        else if (k == "sharp_pixels") sharp_pixels = b();
        else if (k == "allow_download") allow_download = b();
        else if (k == "include_addons") include_addons = b();
        else if (k == "patch_in_place") patch_in_place = b();
        else if (k == "data_dir" && !v.empty()) data_dir = v;
    }
    fclose(f);
    return true;
}

bool Settings::save(const std::string& path) const {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return false;
    fprintf(f, "speed=%d\nfast_forward_hold=%d\naspect=%s\nui_scale=%.2f\nsharp_pixels=%d\nallow_download=%d\n"
               "include_addons=%d\npatch_in_place=%d\ndata_dir=%s\n",
            speed, fast_forward_hold, aspect == Aspect::Ratio16x9 ? "16:9" : "4:3", double(ui_scale), sharp_pixels,
            allow_download, include_addons, patch_in_place, data_dir.c_str());
    bool ok = fclose(f) == 0;
    return ok && rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace sf3::menu
