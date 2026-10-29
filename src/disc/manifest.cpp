// See manifest.h. MIT, see LICENSE.
#include "manifest.h"
#include "sha.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace sf3::disc {
namespace {

// json.dumps of a str (ensure_ascii): the names here are ISO 9660 d-characters, but escape anyway
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c < 0x20 || c >= 0x7f) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}
// IP.BIN header text: latin-1, trailing whitespace stripped (Python rstrip)
std::string field(const std::vector<uint8_t>& ip, size_t a, size_t b) {
    std::string s(ip.begin() + a, ip.begin() + b);
    while (!s.empty() && (s.back() == ' ' || (s.back() >= '\t' && s.back() <= '\r') || s.back() == '\x1c' ||
                          s.back() == '\x1d' || s.back() == '\x1e' || s.back() == '\x1f' || s.back() == '\x85' || s.back() == '\xa0'))
        s.pop_back();
    return s;
}
std::string sha1_of(const std::vector<uint8_t>& v) { Sha1 h; h.update(v.data(), v.size()); return h.hex(); }
bool is_bin(const std::string& n) {
    return n.size() > 4 && n.compare(n.size() - 4, 4, ".BIN") == 0;
}

}  // namespace

Probe probe(const Disc& d, const std::string& extract_dir, const std::function<void(uint64_t, uint64_t)>& progress) {
    uint64_t total = 0, done = 0;
    for (auto& r : d.files) if (!r.is_cdda()) total += r.size;
    for (auto* t : d.audio_tracks()) total += t->end > t->index1() ? t->end - t->index1() : 0;
    std::map<std::string, std::string> files;   // path -> {"sha1":..,"size":..} (sorted keys)
    for (auto& r : d.files) {
        if (r.is_dir || r.is_cdda()) continue;
        auto data = d.read_file(r);
        files["/" + r.name] = "{\"sha1\":" + jstr(sha1_of(data)) + ",\"size\":" + std::to_string(data.size()) + "}";
        if (!extract_dir.empty() && is_bin(r.name)) {
            std::ofstream f(path_join(extract_dir, r.name), std::ios::binary);
            f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            if (!f) throw DiscError("cannot write " + path_join(extract_dir, r.name));
        }
        done += r.size;
        if (progress) progress(done, total);
    }
    std::map<std::string, std::string> audio;
    for (auto* t : d.audio_tracks()) {
        uint64_t a = t->index1(), n = t->end > a ? t->end - a : 0;
        uint64_t frames = n / RAW, want = frames * RAW;
        Sha1 h;
        std::vector<uint8_t> buf(1 << 20);
        for (uint64_t at = 0; at < want;) {
            size_t k = size_t(std::min<uint64_t>(buf.size(), want - at));
            size_t got = t->src->read(a + at, buf.data(), k);
            h.update(buf.data(), got);
            if (got < k) break;
            at += k;
            done += k;
            if (progress) progress(done, total);
        }
        char key[8];
        snprintf(key, sizeof key, "%02d", t->number);
        audio[key] = "{\"frames\":" + std::to_string(frames) + ",\"sha1\":" + jstr(h.hex()) + "}";
    }
    const auto& ip = d.ip_raw;
    uint32_t ip_size = uint32_t(ip[0xE0]) << 24 | uint32_t(ip[0xE1]) << 16 | uint32_t(ip[0xE2]) << 8 | ip[0xE3];
    std::vector<uint8_t> ipb(ip.begin(), ip.begin() + (ip_size && ip_size < ip.size() ? ip_size : ip.size()));
    std::string areas;
    for (char c : field(ip, 0x40, 0x50)) if (c != ' ') areas += c;
    std::string j = "{\"areas\":" + jstr(areas) + ",\"audio\":{";
    bool first = true;
    for (auto& [k, v] : audio) { j += (first ? "" : ",") + jstr(k) + ":" + v; first = false; }
    j += "},\"files\":{";
    first = true;
    for (auto& [k, v] : files) { j += (first ? "" : ",") + jstr(k) + ":" + v; first = false; }
    j += "},\"ip_sha1\":" + jstr(sha1_of(ipb)) + ",\"product\":" + jstr(field(ip, 0x20, 0x2A)) +
         ",\"version\":" + jstr(field(ip, 0x2A, 0x30)) + "}";
    return {j, sha256_hex(j.data(), j.size())};
}

std::string match_profile(const std::string& dir, const std::string& content_id) {
    static const std::regex key("\"content_sha256\"\\s*:\\s*\"([0-9a-f]{64})\"");
    std::vector<std::string> names = list_dir(dir);
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
        if (path_suffix_lower(n) != ".json") continue;
        std::ifstream f(path_join(dir, n), std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        std::string text = s.str();
        std::smatch m;
        if (std::regex_search(text, m, key) && m[1] == content_id) return path_join(dir, n);
    }
    return {};
}

}  // namespace sf3::disc
