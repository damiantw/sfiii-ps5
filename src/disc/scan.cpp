// MIT, see LICENSE.
#include "scan.h"
#include <algorithm>

namespace sf3::disc {

static void walk(const std::string& dir, int depth, std::vector<std::string>& out) {
    auto names = list_dir(dir);
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
        if (n.empty() || n[0] == '.') continue;
        std::string p = path_join(dir, n);
        if (is_dir(p)) {
            if (depth > 0) walk(p, depth - 1, out);
            continue;
        }
        std::string s = path_suffix_lower(n);
        if (s == ".cue" || s == ".chd") out.push_back(p);
    }
}

std::vector<std::string> find_images(const std::string& root, int max_depth) {
    std::vector<std::string> out;
    walk(root, max_depth, out);
    return out;
}

static int rank(const Candidate& c) {
    if (!c.ok || c.cls.slot == Slot::None) return -1;
    if (c.cls.state == State::Translated) return 3;
    if (c.cls.state == State::Original && c.id.verified()) return 2;
    return 1;
}

ScanResult scan(const std::string& root, const ScanOptions& opt) {
    ScanResult r;
    for (auto& p : find_images(root, opt.max_depth)) {
        if (opt.progress) opt.progress(p);
        Candidate c;
        c.path = p;
        try {
            Disc d(p);
            c.id = identify(d, opt.hash);
            c.cls = classify(c.id);
            c.ok = true;
        } catch (const std::exception& e) {
            c.error = e.what();
        }
        r.all.push_back(std::move(c));
    }
    for (size_t i = 0; i < r.all.size(); i++) {
        int s = int(r.all[i].cls.slot);
        int k = rank(r.all[i]);
        if (k < 0) continue;
        if (r.slot[s] < 0 || k > rank(r.all[size_t(r.slot[s])])) r.slot[s] = int(i);
    }
    return r;
}

}  // namespace sf3::disc
