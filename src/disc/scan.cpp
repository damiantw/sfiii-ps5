// MIT, see LICENSE.
#include "scan.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

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

// The image's stamp: size and mtime of every file in its folder (a .cue's tracks sit beside it).
static std::string stamp(const std::string& image) {
    std::string dir = image.substr(0, image.find_last_of('/'));
    auto names = list_dir(dir);
    std::sort(names.begin(), names.end());
    std::string s;
    for (auto& n : names) {
        struct stat st;
        if (n.empty() || n[0] == '.' || stat(path_join(dir, n).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
        char b[96];
        std::snprintf(b, sizeof b, "%llu:%lld,", (unsigned long long)st.st_size, (long long)st.st_mtime);
        s += n + "=" + b;
    }
    return s;
}

static std::string clean(std::string s) {
    for (auto& ch : s) if (ch == '\t' || ch == '\n' || ch == '\r' || ch == '\x1f') ch = ' ';
    return s;
}

static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> v;
    size_t a = 0;
    for (size_t b; (b = s.find(d, a)) != std::string::npos; a = b + 1) v.push_back(s.substr(a, b - a));
    v.push_back(s.substr(a));
    return v;
}

static const int kCacheFields = 18;
static const char* kCacheHeader = "sf3scan 1";

static std::map<std::string, std::pair<std::string, Candidate>> load_cache(const std::string& path) {
    std::map<std::string, std::pair<std::string, Candidate>> m;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return m;
    std::string line;
    char buf[8192];
    bool first = true;
    while (std::fgets(buf, sizeof buf, f)) {
        line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (first) { first = false; if (line != kCacheHeader) break; continue; }
        auto v = split(line, '\t');
        if (int(v.size()) != kCacheFields) continue;
        Candidate c;
        c.path = v[0];
        c.ok = v[2] == "1", c.error = v[3];
        Identity& i = c.id;
        i.serial = v[4], i.version = v[5], i.areas = v[6], i.title = v[7], i.hashed = v[8] == "1";
        i.track1_sha1 = v[9], i.track1_sha256 = v[10], i.track2_sha1 = v[11], i.track2_sha256 = v[12];
        i.track1_size = std::strtoull(v[13].c_str(), nullptr, 10), i.track2_size = std::strtoull(v[14].c_str(), nullptr, 10);
        i.has_track1 = v[15] == "1", i.has_track2 = v[16] == "1";
        if (!v[17].empty()) i.warnings = split(v[17], '\x1f');
        i.known = i.has_track1 ? by_track1(i.track1_sha1) : nullptr;
        if (c.ok) c.cls = classify(i);
        m[c.path] = {v[1], c};
    }
    std::fclose(f);
    return m;
}

static void save_cache(const std::string& path, const std::vector<std::pair<std::string, const Candidate*>>& all) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%s\n", kCacheHeader);
    for (auto& [st, c] : all) {
        const Identity& i = c->id;
        std::string w;
        for (size_t k = 0; k < i.warnings.size(); k++) w += (k ? "\x1f" : "") + clean(i.warnings[k]);
        std::fprintf(f, "%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%llu\t%llu\t%d\t%d\t%s\n",
                     clean(c->path).c_str(), clean(st).c_str(), c->ok, clean(c->error).c_str(), clean(i.serial).c_str(),
                     clean(i.version).c_str(), clean(i.areas).c_str(), clean(i.title).c_str(), i.hashed,
                     i.track1_sha1.c_str(), i.track1_sha256.c_str(), i.track2_sha1.c_str(), i.track2_sha256.c_str(),
                     (unsigned long long)i.track1_size, (unsigned long long)i.track2_size, i.has_track1, i.has_track2, w.c_str());
    }
    if (std::fclose(f) == 0) std::rename(tmp.c_str(), path.c_str());
    else std::remove(tmp.c_str());
}

ScanResult scan(const std::string& root, const ScanOptions& opt) {
    ScanResult r;
    std::map<std::string, std::pair<std::string, Candidate>> cache;
    if (!opt.cache_path.empty() && !opt.refresh) cache = load_cache(opt.cache_path);
    std::vector<std::string> stamps;
    int reused = 0;
    for (auto& p : find_images(root, opt.max_depth)) {
        std::string st = opt.cache_path.empty() ? std::string() : stamp(p);
        stamps.push_back(st);
        auto it = cache.find(p);
        if (it != cache.end() && it->second.first == st && (it->second.second.id.hashed || !opt.hash)) {
            r.all.push_back(it->second.second);
            reused++;
            continue;
        }
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
    if (opt.reused) *opt.reused = reused;
    if (!opt.cache_path.empty()) {
        std::vector<std::pair<std::string, const Candidate*>> out;
        for (size_t i = 0; i < r.all.size(); i++) out.push_back({stamps[i], &r.all[i]});
        save_cache(opt.cache_path, out);
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
