// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/cdimage.py.
#include "cdimage.h"
#include "sha.h"

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sf3::disc {

// ---------------------------------------------------------------- small helpers

std::string ascii_replace(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < 0x80) s += char(p[i]);
        else s += "\xEF\xBF\xBD";
    }
    return s;
}
std::string to_lower_ascii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}
std::string to_upper_ascii(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = char(c - 32);
    return s;
}
std::string path_join(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") return name;
    if (!name.empty() && name[0] == '/') return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}
std::string path_dir(const std::string& p) {
    auto k = p.find_last_of('/');
    if (k == std::string::npos) return ".";
    return k == 0 ? "/" : p.substr(0, k);
}
std::string path_name(const std::string& p) {
    auto k = p.find_last_of('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}
std::string path_suffix_lower(const std::string& p) {
    std::string n = path_name(p);
    auto k = n.find_last_of('.');
    if (k == std::string::npos || k == 0) return "";
    return to_lower_ascii(n.substr(k));
}
#if SF3_PS5_CONSOLE
int sf3_last_getdents = 0, sf3_last_getdents_errno = 0;
extern "C" int getdents(int fd, char* buf, int nbytes);
// The kernel's own directory read: open + getdents, no libc opendir. Returns false
// if the folder cannot be opened (errno set).
static bool list_dir_kernel(const std::string& dir, std::vector<std::string>& out) {
    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    // A whole directory block: /data reports 64 KiB directories, and a smaller
    // buffer reads nothing there.
    std::vector<char> buf(65536);
    int n;
    while ((n = getdents(fd, buf.data(), int(buf.size()))) > 0) {
        for (int o = 0; o < n;) {
            auto* e = reinterpret_cast<dirent*>(buf.data() + o);
            if (e->d_reclen == 0) break;
            if (e->d_fileno != 0 && strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) out.push_back(e->d_name);
            o += e->d_reclen;
        }
    }
    int e = errno;
    close(fd);
    sf3_last_getdents = n; sf3_last_getdents_errno = n < 0 ? e : 0;
    return true;
}
#endif
std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
#if SF3_PS5_CONSOLE
    if (list_dir_kernel(dir, out)) return out;  // libc opendir is refused in a title
#endif
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        out.push_back(e->d_name);
    }
    closedir(d);
    return out;
}
bool is_dir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool file_exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

// ---------------------------------------------------------------- files

namespace {
class FileSource : public ByteSource {
public:
    FileSource(int fd, uint64_t size, std::string path) : fd_(fd), size_(size), path_(std::move(path)) {}
    ~FileSource() override { close(fd_); }
    uint64_t size() const override { return size_; }
    size_t read(uint64_t off, void* buf, size_t n) override {
        size_t got = 0;
        while (got < n) {
            ssize_t r = pread(fd_, static_cast<char*>(buf) + got, n - got, off_t(off + got));
            if (r <= 0) break;
            got += size_t(r);
        }
        return got;
    }
    std::string name() const override { return path_; }
private:
    int fd_;
    uint64_t size_;
    std::string path_;
};
}  // namespace

std::shared_ptr<ByteSource> open_file(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw DiscError("cannot open " + path);
    struct stat st;
    fstat(fd, &st);
    return std::make_shared<FileSource>(fd, uint64_t(st.st_size), path);
}

// ---------------------------------------------------------------- tracks

int Track::sector_size() const {
    return mode.size() >= 5 && mode.compare(mode.size() - 5, 5, "/2048") == 0 ? USER : RAW;
}

Track::Hashes Track::hashes() const {
    Sha1 h1;
    Sha256 h2;
    std::vector<uint8_t> buf(size_t(1) << 22);
    uint64_t silence = uint64_t(pregap) * sector_size();
    std::fill(buf.begin(), buf.end(), 0);
    for (uint64_t left = silence; left;) {
        size_t k = size_t(std::min<uint64_t>(left, buf.size()));
        h1.update(buf.data(), k);
        h2.update(buf.data(), k);
        left -= k;
    }
    uint64_t pos = start, done = 0;
    while (pos < end) {
        size_t k = size_t(std::min<uint64_t>(end - pos, buf.size()));
        size_t got = src->read(pos, buf.data(), k);
        if (!got) break;
        h1.update(buf.data(), got);
        h2.update(buf.data(), got);
        pos += got;
        done += got;
    }
    return {h1.hex(), h2.hex(), silence + done};
}

std::vector<uint8_t> Track::audio_from_index1() const {
    uint64_t a = index1();
    std::vector<uint8_t> out(end > a ? size_t(end - a) : 0);
    out.resize(src->read(a, out.data(), out.size()));
    return out;
}

namespace {
int msf(const std::string& t) {
    int mm, ss, ff;
    char tail;
    // re.fullmatch(r"(\d+):(\d+):(\d+)")
    auto digits = [](const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    auto a = t.find(':'), b = a == std::string::npos ? a : t.find(':', a + 1);
    if (b == std::string::npos || !digits(t.substr(0, a)) || !digits(t.substr(a + 1, b - a - 1)) || !digits(t.substr(b + 1)) ||
        sscanf(t.c_str(), "%d:%d:%d%c", &mm, &ss, &ff, &tail) != 3)
        throw DiscError("bad MSF time '" + t + "' in CUE sheet");
    return (mm * 60 + ss) * 75 + ff;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

// shlex.split(line, posix=True) (no comments); false on an unclosed quote or a trailing escape.
bool shlex_split(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    bool have = false;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (is_ws(c)) {
            if (have) { out.push_back(cur); cur.clear(); have = false; }
            i++;
        } else if (c == '\\') {
            if (i + 1 >= s.size()) return false;
            cur += s[i + 1]; have = true; i += 2;
        } else if (c == '\'') {
            auto k = s.find('\'', i + 1);
            if (k == std::string::npos) return false;
            cur += s.substr(i + 1, k - i - 1); have = true; i = k + 1;
        } else if (c == '"') {
            i++;
            have = true;
            for (;;) {
                if (i >= s.size()) return false;
                if (s[i] == '"') { i++; break; }
                if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\\' || s[i + 1] == '"')) { cur += s[i + 1]; i += 2; continue; }
                if (s[i] == '\\' && i + 1 >= s.size()) return false;
                cur += s[i++];
            }
        } else {
            cur += c; have = true; i++;
        }
    }
    if (have) out.push_back(cur);
    return true;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (is_ws(c)) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Python's str.strip() and splitlines() on the ASCII characters that matter in a CUE sheet.
std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    auto ws = [](unsigned char c) { return is_ws(char(c)) || (c >= 0x1c && c <= 0x1f); };
    while (a < b && ws(s[a])) a++;
    while (b > a && ws(s[b - 1])) b--;
    return s.substr(a, b - a);
}
std::vector<std::string> splitlines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == '\n' || c == '\r' || c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1e)) {
            out.push_back(cur); cur.clear();
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') i++;
        } else {
            cur += char(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int to_int(const std::string& s) {
    try { size_t k; int v = std::stoi(s, &k); if (k == s.size()) return v; } catch (...) {}
    throw DiscError("bad number '" + s + "' in CUE sheet");
}
}  // namespace

std::vector<Track> parse_cue_text(const std::string& raw, const std::string& dir, const std::string& what,
                                  std::shared_ptr<ByteSource> (*opener)(const std::string&)) {
    std::string text = raw;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text = text.substr(3);
    std::vector<Track> tracks;
    std::string current;
    std::shared_ptr<ByteSource> current_src;
    std::map<std::string, std::shared_ptr<ByteSource>> opened;
    for (auto line : splitlines(text)) {
        line = strip(line);
        if (line.empty() || to_upper_ascii(line.substr(0, 3)) == "REM") continue;
        std::vector<std::string> words;
        if (!shlex_split(line, words)) words = split_ws(line);
        if (words.empty()) continue;
        std::string key = to_upper_ascii(words[0]);
        if (key == "FILE") {
            if (words.size() < 3) throw DiscError("bad FILE line in " + what);
            if (to_upper_ascii(words.back()) != "BINARY")
                throw DiscError("unsupported CUE file type " + words.back() + " (only BINARY)");
            std::string name;
            for (size_t i = 1; i + 1 < words.size(); i++) name += (i > 1 ? " " : "") + words[i];
            current = path_join(dir, name);
            if (!opened.count(current) && opener == open_file && !file_exists(current)) {
                std::vector<std::string> m;
                for (auto& n : list_dir(dir)) if (to_lower_ascii(n) == to_lower_ascii(name)) m.push_back(n);
                if (m.size() != 1) throw DiscError("the CUE sheet's file " + current + " does not exist");
                current = path_join(dir, m[0]);
            }
            auto& s = opened[current];
            if (!s) s = opener(current);
            current_src = s;
        } else if (key == "TRACK") {
            if (!current_src) throw DiscError("TRACK before FILE in " + what);
            if (words.size() < 3) throw DiscError("bad TRACK line in " + what);
            Track t;
            t.number = to_int(words[1]);
            t.mode = to_upper_ascii(words[2]);
            t.src = current_src;
            t.path = current;
            tracks.push_back(std::move(t));
        } else if (key == "INDEX") {
            if (tracks.empty() || words.size() < 3) throw DiscError("bad INDEX line in " + what);
            tracks.back().indexes[to_int(words[1])] = msf(words[2]);
        } else if (key == "PREGAP") {
            if (tracks.empty() || words.size() < 2) throw DiscError("bad PREGAP line in " + what);
            tracks.back().pregap = msf(words[1]);
        }
    }
    if (tracks.empty()) throw DiscError("no tracks in " + what);
    for (size_t i = 0; i < tracks.size(); i++) {
        Track& t = tracks[i];
        if (!t.indexes.count(1)) throw DiscError("track " + std::to_string(t.number) + " has no INDEX 01");
        int first = INT32_MAX;
        for (auto& [k, v] : t.indexes) first = std::min(first, v);
        t.start = uint64_t(first) * t.sector_size();
        if (i + 1 < tracks.size() && tracks[i + 1].path == t.path) {
            int nf = INT32_MAX;
            for (auto& [k, v] : tracks[i + 1].indexes) nf = std::min(nf, v);
            t.end = uint64_t(nf) * tracks[i + 1].sector_size();
        } else {
            t.end = t.src->size();
        }
        if (t.end < t.index1()) throw DiscError("track " + std::to_string(t.number) + " ends before its INDEX 01");
    }
    return tracks;
}

std::vector<Track> parse_cue(const std::string& cue_path) {
    auto src = open_file(cue_path);
    std::string text(size_t(src->size()), '\0');
    src->read(0, text.data(), text.size());
    // decode("utf-8-sig", errors="replace") only matters for non-ASCII names, which stay as bytes
    // here and are looked up as bytes; ASCII CUE syntax is unchanged.
    return parse_cue_text(text, path_dir(cue_path), cue_path);
}

// ---------------------------------------------------------------- CHD

namespace {
struct ChdTrack {
    int number;
    std::string type, mode;
    int frames, pregap;
    bool pregap_stored;
    int sector;        // bytes per frame in the extracted BIN
    uint64_t vstart;   // offset in the virtual BIN
    uint32_t vframe;   // frames before it in the virtual BIN (what chdman's CUE counts)
    uint32_t chd_frame;// first physical frame in the CHD
};

class ChdSource : public ByteSource {
public:
    ChdSource(chd_file* f, std::string path, std::vector<ChdTrack> t)
        : f_(f), path_(std::move(path)), tracks_(std::move(t)) {
        const chd_header* h = chd_get_header(f_);
        hunk_bytes_ = h->hunkbytes;
        frames_per_hunk_ = h->hunkbytes / CD_FRAME_SIZE;
        hunk_.resize(hunk_bytes_);
        size_ = tracks_.empty() ? 0 : tracks_.back().vstart + uint64_t(tracks_.back().frames) * tracks_.back().sector;
    }
    ~ChdSource() override { chd_close(f_); }
    uint64_t size() const override { return size_; }
    std::string name() const override { return path_; }
    size_t read(uint64_t off, void* out, size_t n) override {
        size_t done = 0;
        auto* o = static_cast<uint8_t*>(out);
        while (done < n && off < size_) {
            const ChdTrack* t = &tracks_[0];
            for (auto& x : tracks_) if (off >= x.vstart) t = &x;
            uint64_t rel = off - t->vstart;
            uint32_t frame = uint32_t(rel / t->sector), within = uint32_t(rel % t->sector);
            if (frame >= uint32_t(t->frames)) break;
            const uint8_t* fr = load(t->chd_frame + frame);
            if (!fr) throw DiscError("CHD read error in " + path_);
            uint8_t tmp[RAW];
            memcpy(tmp, fr, t->sector);
            if (t->type == "AUDIO")
                for (int i = 0; i + 1 < t->sector; i += 2) std::swap(tmp[i], tmp[i + 1]);
            size_t k = std::min<size_t>(n - done, size_t(t->sector - within));
            memcpy(o + done, tmp + within, k);
            done += k;
            off += k;
        }
        return done;
    }
private:
    const uint8_t* load(uint32_t frame) {
        uint32_t hunk = frame / frames_per_hunk_;
        if (hunk != cur_) {
            if (chd_read(f_, hunk, hunk_.data()) != CHDERR_NONE) return nullptr;
            cur_ = hunk;
        }
        return hunk_.data() + size_t(frame % frames_per_hunk_) * CD_FRAME_SIZE;
    }
    chd_file* f_;
    std::string path_;
    std::vector<ChdTrack> tracks_;
    uint32_t hunk_bytes_ = 0, frames_per_hunk_ = 1, cur_ = UINT32_MAX;
    std::vector<uint8_t> hunk_;
    uint64_t size_ = 0;
};

std::string msf_text(uint32_t f) {
    char b[16];
    snprintf(b, sizeof b, "%02u:%02u:%02u", f / (75 * 60), (f / 75) % 60, f % 75);
    return b;
}
}  // namespace

ChdImage open_chd(const std::string& path) {
    chd_file* f = nullptr;
    chd_error e = chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &f);
    if (e != CHDERR_NONE) throw DiscError("cannot open CHD " + path + ": " + chd_error_string(e));
    const chd_header* h = chd_get_header(f);
    if (h->hunkbytes % CD_FRAME_SIZE) { chd_close(f); throw DiscError(path + " is not a CD CHD"); }
    std::vector<ChdTrack> tracks;
    uint64_t vpos = 0;
    uint32_t phys = 0, vframes = 0;
    for (uint32_t i = 0;; i++) {
        char meta[512] = {};
        uint32_t len = 0;
        char type[64] = {}, sub[64] = {}, pgtype[64] = {}, pgsub[64] = {};
        int num = 0, frames = 0, pregap = 0, postgap = 0;
        if (chd_get_metadata(f, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof meta - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
            if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &num, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) != 8) {
                chd_close(f);
                throw DiscError("bad track metadata in " + path);
            }
        } else if (chd_get_metadata(f, CDROM_TRACK_METADATA_TAG, i, meta, sizeof meta - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
            if (sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &num, type, sub, &frames) != 4) {
                chd_close(f);
                throw DiscError("bad track metadata in " + path);
            }
        } else {
            break;
        }
        ChdTrack t;
        t.number = num;
        t.type = type;
        t.frames = frames;
        t.pregap = pregap;
        t.pregap_stored = pgtype[0] == 'V';
        if (t.type == "MODE1_RAW") { t.mode = "MODE1/2352"; t.sector = RAW; }
        else if (t.type == "MODE1") { t.mode = "MODE1/2048"; t.sector = USER; }
        else if (t.type == "AUDIO") { t.mode = "AUDIO"; t.sector = RAW; }
        else if (t.type == "MODE2_RAW") { t.mode = "MODE2/2352"; t.sector = RAW; }
        else { chd_close(f); throw DiscError("unsupported CHD track type " + t.type + " in " + path); }
        t.vstart = vpos;
        t.vframe = vframes;
        vframes += uint32_t(frames);
        t.chd_frame = phys;
        vpos += uint64_t(frames) * t.sector;
        phys += uint32_t((frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING * CD_TRACK_PADDING);
        tracks.push_back(t);
    }
    if (tracks.empty()) { chd_close(f); throw DiscError("no CD tracks in " + path); }
    // The CUE chdman extractcd writes for one BIN.
    std::string cue = "FILE \"disc.bin\" BINARY\r\n";
    for (auto& t : tracks) {
        char b[64];
        snprintf(b, sizeof b, "  TRACK %02d %s\r\n", t.number, t.mode.c_str());
        cue += b;
        uint32_t f0 = t.vframe;
        if (t.pregap > 0 && t.pregap_stored) {
            cue += "    INDEX 00 " + msf_text(f0) + "\r\n";
            cue += "    INDEX 01 " + msf_text(f0 + uint32_t(t.pregap)) + "\r\n";
        } else {
            if (t.pregap > 0) cue += "    PREGAP " + msf_text(uint32_t(t.pregap)) + "\r\n";
            cue += "    INDEX 01 " + msf_text(f0) + "\r\n";
        }
    }
    // A track's frame offset in the virtual BIN must be whole frames of that track's size;
    // mixing 2048- and 2352-byte tracks in one BIN is what chdman writes too.
    return {cue, std::make_shared<ChdSource>(f, path, std::move(tracks))};
}

// ---------------------------------------------------------------- ISO 9660

namespace {
thread_local std::shared_ptr<ByteSource> g_chd_bin;
std::shared_ptr<ByteSource> chd_opener(const std::string&) { return g_chd_bin; }
}  // namespace

Disc::Disc(const std::string& path) {
    std::string suf = path_suffix_lower(path);
    if (suf == ".chd") {
        ChdImage img = open_chd(path);
        g_chd_bin = img.bin;
        try {
            tracks = parse_cue_text(img.cue_text, path_dir(path), path, chd_opener);
        } catch (...) { g_chd_bin.reset(); throw; }
        g_chd_bin.reset();
        for (auto& t : tracks) t.path = path;
    } else if (suf == ".cue") {
        tracks = parse_cue(path);
    } else {
        throw DiscError("give the disc as a .cue sheet or a .chd");
    }
    cue = path;
    init();
}

void Disc::init() {
    if (data().is_audio()) throw DiscError("track 1 is not a data track");
    if (data().mode != "MODE1/2352" && data().mode != "MODE1/2048")
        throw DiscError("track 1 is " + data().mode + "; Saturn discs are MODE1");
    ip_raw = read_sectors(0, 16);
    pvd = read_sectors(16, 1);
    if (pvd[0] != 1 || memcmp(&pvd[1], "CD001", 5) != 0)
        throw DiscError("no ISO 9660 primary volume descriptor at sector 16");
    volume_id = ascii_replace(&pvd[40], 32);
    root = record(pvd.data(), pvd.size(), 156);
    files = list_root();
}

std::vector<uint8_t> Disc::read_sectors(uint32_t lba, uint32_t count) const {
    const Track& t = data();
    int ss = t.sector_size();
    std::vector<uint8_t> raw(size_t(count) * ss);
    size_t got = t.src->read(t.index1() + uint64_t(lba) * ss, raw.data(), raw.size());
    if (got < raw.size())
        throw DiscError("read past the end of the data track (sector " + std::to_string(lba + count) + ")");
    if (ss == USER) return raw;
    std::vector<uint8_t> out(size_t(count) * USER);
    for (uint32_t k = 0; k < count; k++) memcpy(&out[size_t(k) * USER], &raw[size_t(k) * RAW + 16], USER);
    return out;
}

std::vector<uint8_t> Disc::read_file(const Record& r) const {
    if (r.size == 0) return {};
    auto d = read_sectors(r.lba, (r.size + USER - 1) / USER);
    d.resize(r.size);
    return d;
}

Record Disc::record(const uint8_t* b, size_t len, size_t off) {
    auto at = [&](size_t i) -> uint8_t { return i < len ? b[i] : 0; };
    auto le32 = [&](size_t i) { return uint32_t(at(i)) | uint32_t(at(i + 1)) << 8 | uint32_t(at(i + 2)) << 16 | uint32_t(at(i + 3)) << 24; };
    Record r;
    size_t n = at(off);
    r.lba = le32(off + 2);
    r.size = le32(off + 10);
    for (int i = 0; i < 7; i++) r.time[i] = at(off + 18 + i);
    r.flags = at(off + 25);
    size_t nl = at(off + 32);
    std::vector<uint8_t> ident;
    for (size_t i = 0; i < nl && off + 33 + i < len; i++) ident.push_back(b[off + 33 + i]);
    size_t su = off + 33 + nl + ((nl & 1) ? 0 : 1);
    if (off + n >= su + 14 && at(su + 6) == 'X' && at(su + 7) == 'A') r.xa_attr = at(su + 4) << 8 | at(su + 5);
    if (ident.size() == 1 && (ident[0] == 0 || ident[0] == 1)) {
        r.name = std::string(1, char(ident[0]));
    } else {
        r.name = ascii_replace(ident.data(), ident.size());
        auto k = r.name.find(';');
        if (k != std::string::npos) r.name = r.name.substr(0, k);
        if (!r.name.empty() && r.name.back() == '.' && !(r.flags & 2)) r.name.pop_back();
    }
    r.is_dir = r.flags & 2;
    return r;
}

std::vector<Record> Disc::list_root() const {
    auto data = read_sectors(root.lba, (root.size + USER - 1) / USER);
    std::vector<Record> out;
    size_t off = 0;
    while (off < data.size()) {
        size_t n = data[off];
        if (n == 0) { off = (off / USER + 1) * USER; continue; }
        Record r = record(data.data(), data.size(), off);
        off += n;
        if (r.name == std::string(1, '\0') || r.name == "\x01") continue;
        if (r.is_dir)
            throw DiscError("the disc has a subdirectory (" + r.name + "); Shining Force III discs have none, "
                            "and this tool only rebuilds a flat root");
        if (r.flags & 0x04) throw DiscError("associated file " + r.name + " is not supported");
        out.push_back(r);
    }
    return out;
}

std::vector<const Track*> Disc::audio_tracks() const {
    std::vector<const Track*> out;
    for (size_t i = 1; i < tracks.size(); i++) if (tracks[i].is_audio()) out.push_back(&tracks[i]);
    return out;
}

namespace {
int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
bool valid_date(int y, int m, int d, int h, int mi, int s) {
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || h > 23 || mi > 59 || s > 59) return false;
    int lim = dm[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    return d <= lim;
}
}  // namespace

bool iso_time_to_utc(const uint8_t t[7], const std::string& mode, int64_t& out) {
    bool zero = true;
    for (int i = 0; i < 7; i++) zero &= t[i] == 0;
    if (zero) return false;
    int y = t[0] + 1900, mo = t[1], d = t[2], h = t[3], mi = t[4], s = t[5];
    int off = int8_t(t[6]);
    if (!valid_date(y, mo, d, h, mi, s)) return false;
    out = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    if (mode == "iso" && off >= -48 && off <= 52) out -= int64_t(15 * 60) * off;
    return true;
}

}  // namespace sf3::disc
