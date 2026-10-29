// MIT, see LICENSE.
#include "vfs.h"
#include "disc/cdimage.h"
#include <miniz.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

namespace sf3::vfs {
using namespace dotnet;

// ------------------------------------------------------------------ time
int64_t civil_to_epoch(int y, int m, int d, int h, int mi, int s) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * unsigned(m > 2 ? m - 3 : m + 9) + 2) / 5 + unsigned(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + int64_t(doe) - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}
void epoch_to_civil(int64_t t, int& y, int& mo, int& d, int& h, int& mi, int& s) {
    int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    int64_t rem = t - days * 86400;
    h = int(rem / 3600); mi = int(rem / 60 % 60); s = int(rem % 60);
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = unsigned(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    mo = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(yy + (mo <= 2));
}
int64_t local_to_epoch(int y, int mo, int d, int h, int mi, int s) {
    struct tm tm = {};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
    return int64_t(mktime(&tm));
}

int64_t python_mtime_seconds(FTime t) {
    // st_mtime = sec + nsec * 1e-9 (a double), then fromtimestamp rounds half-even to µs.
    double v = double(t.sec) + double(t.nsec) * 1e-9;
    double ip;
    double fp = std::modf(v, &ip);
    fp *= 1e6;
    fp = std::nearbyint(fp);
    if (fp >= 1e6) ip += 1.0;
    else if (fp < 0) ip -= 1.0;   // negative fractions borrow a second (microsecond >= 0)
    return int64_t(ip);
}

// ------------------------------------------------------------------ zip
static const char16_t CP437_HI[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0};

class ZipArchive {
public:
    struct Entry {
        std::string name;        // decoded, UTF-8
        uint16_t flags = 0, method = 0, dos_time = 0, dos_date = 0;
        uint32_t crc = 0;
        uint64_t csize = 0, usize = 0, local_off = 0;
        bool has_ntfs = false;
        uint64_t ntfs_mtime = 0;
    };
    std::shared_ptr<disc::ByteSource> src;
    std::string label;
    std::vector<Entry> entries;

    explicit ZipArchive(const std::string& path) : src(disc::open_file(path)), label(disc::path_name(path)) {
        uint64_t size = src->size();
        size_t tail = size_t(std::min<uint64_t>(size, 65557));
        std::vector<uint8_t> t(tail);
        if (src->read(size - tail, t.data(), tail) != tail) bad("short read");
        long pos = -1;
        for (long k = long(tail) - 22; k >= 0; k--)
            if (t[size_t(k)] == 'P' && t[size_t(k) + 1] == 'K' && t[size_t(k) + 2] == 5 && t[size_t(k) + 3] == 6) { pos = k; break; }
        if (pos < 0) bad("not a zip file");
        const uint8_t* e = &t[size_t(pos)];
        uint64_t n = u16(e + 10), cd_size = u32(e + 12), cd_off = u32(e + 16);
        if (n == 0xFFFF || cd_size == 0xFFFFFFFF || cd_off == 0xFFFFFFFF) bad("zip64 archives are not supported");
        uint64_t end_pos = size - tail + uint64_t(pos);
        int64_t concat = int64_t(end_pos) - int64_t(cd_size) - int64_t(cd_off);
        std::vector<uint8_t> cd(cd_size);
        if (src->read(uint64_t(int64_t(cd_off) + concat), cd.data(), cd_size) != cd_size) bad("bad central directory");
        size_t p = 0;
        for (uint64_t k = 0; k < n; k++) {
            if (p + 46 > cd.size() || u32(&cd[p]) != 0x02014b50) bad("bad central directory");
            const uint8_t* h = &cd[p];
            Entry en;
            en.flags = u16(h + 8); en.method = u16(h + 10); en.dos_time = u16(h + 12); en.dos_date = u16(h + 14);
            en.crc = u32(h + 16); en.csize = u32(h + 20); en.usize = u32(h + 24);
            size_t nl = u16(h + 28), xl = u16(h + 30), cl = u16(h + 32);
            en.local_off = uint64_t(int64_t(u32(h + 42)) + concat);
            if (p + 46 + nl + xl + cl > cd.size()) bad("bad central directory");
            const uint8_t* nm = h + 46;
            if (en.flags & 0x800) en.name = utf8_encode(utf8_decode(nm, nl));
            else {
                U32 s;
                for (size_t q = 0; q < nl; q++) s += nm[q] < 0x80 ? char32_t(nm[q]) : char32_t(CP437_HI[nm[q] - 0x80]);
                en.name = utf8_encode(s);
            }
            parse_extra(h + 46 + nl, xl, en);
            entries.push_back(std::move(en));
            p += 46 + nl + xl + cl;
        }
    }
    std::vector<uint8_t> read(size_t i) const {
        const Entry& en = entries[i];
        uint8_t lh[30];
        if (src->read(en.local_off, lh, 30) != 30 || u32(lh) != 0x04034b50) bad("bad local header for " + en.name);
        uint64_t data = en.local_off + 30 + u16(lh + 26) + u16(lh + 28);
        if (en.flags & 1) bad(en.name + " is encrypted");
        std::vector<uint8_t> c(en.csize);
        if (src->read(data, c.data(), c.size()) != c.size()) bad("truncated data for " + en.name);
        std::vector<uint8_t> out;
        if (en.method == 0) out = std::move(c);
        else if (en.method == 8) {
            out.resize(en.usize);
            size_t got = tinfl_decompress_mem_to_mem(out.data(), out.size(), c.data(), c.size(), 0);
            if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || got != en.usize) bad("cannot inflate " + en.name);
        } else bad(en.name + ": compression method " + std::to_string(en.method) + " is not supported");
        if (out.size() != en.usize || uint32_t(mz_crc32(MZ_CRC32_INIT, out.data(), out.size())) != en.crc)
            bad("bad CRC-32 for " + en.name);
        return out;
    }
private:
    [[noreturn]] void bad(const std::string& m) const { throw VfsError(label + ": " + m); }
    static uint32_t u16(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8); }
    static uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
    static uint64_t u64(const uint8_t* p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }
    static void parse_extra(const uint8_t* x, size_t n, Entry& en) {
        size_t p = 0;
        while (p + 4 <= n) {
            uint32_t id = u16(x + p), len = u16(x + p + 2);
            if (p + 4 + len > n) break;
            if (id == 0x000A && len >= 4) {
                size_t q = p + 8, end = p + 4 + len;
                while (q + 4 <= end) {
                    uint32_t tag = u16(x + q), sz = u16(x + q + 2);
                    if (q + 4 + sz > end) break;
                    if (tag == 1 && sz >= 24) { en.has_ntfs = true; en.ntfs_mtime = u64(x + q + 4); }
                    q += 4 + sz;
                }
            }
            p += 4 + len;
        }
    }
};

// ------------------------------------------------------------------ nodes
std::vector<uint8_t> Node::read() const {
    if (dir) throw VfsError(path() + " is a folder");
    if (zip) return zip->read(entry);
    FILE* f = fopen(disk_path.c_str(), "rb");
    if (!f) throw VfsError("cannot open " + disk_path);
    std::vector<uint8_t> out;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return out;
}
std::string Node::path() const {
    if (!parent) return name;
    return parent->path() + "/" + name;
}
std::string Node::relative_to(const Node* a) const {
    std::string out;
    for (const Node* n = this; n && n != a; n = n->parent) out = out.empty() ? n->name : n->name + "/" + out;
    return out;
}
Node* Node::child_exact(const std::string& n) const {
    for (auto& k : kids) if (k->name == n) return k.get();
    return nullptr;
}
const std::vector<Node*>& Node::sorted() const {
    if (!cached_) {
        std::vector<std::pair<std::vector<uint32_t>, Node*>> v;
        for (auto& k : kids) v.emplace_back(ntfs_key(utf8_decode(k->name)), k.get());
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [key, n] : v) {
            sorted_.push_back(n);
            upper_.emplace(upper(utf8_decode(n->name)), n);   // setdefault
        }
        cached_ = true;
    }
    return sorted_;
}
const Node* Node::get(std::initializer_list<std::string> parts) const { return get(std::vector<std::string>(parts)); }
const Node* Node::get(const std::vector<std::string>& parts) const {
    const Node* cur = this;
    for (size_t i = 0; i < parts.size(); i++) {
        if (!cur->dir) return nullptr;
        cur->sorted();
        auto it = cur->upper_.find(upper(utf8_decode(parts[i])));
        if (it == cur->upper_.end()) return nullptr;
        if (i + 1 == parts.size()) return it->second;
        if (!it->second->dir) return nullptr;
        cur = it->second;
    }
    return this;
}
std::optional<std::vector<const Node*>> Node::files(const std::vector<std::string>& parts) const {
    const Node* d = parts.empty() ? this : get(parts);
    if (!d || !d->dir) return std::nullopt;
    std::vector<const Node*> out;
    for (Node* k : d->sorted()) if (!k->dir) out.push_back(k);
    return out;
}

static void scan_disk(Node& n, const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (!d) throw VfsError("cannot read folder " + path);
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        std::string nm = e->d_name;
        if (nm != "." && nm != "..") names.push_back(nm);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (auto& nm : names) {
        std::string p = path + "/" + nm;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        auto k = std::make_unique<Node>();
        k->name = nm;
        k->parent = &n;
        if (S_ISDIR(st.st_mode)) { k->dir = true; scan_disk(*k, p); }
        else if (S_ISREG(st.st_mode)) {
            k->disk_path = p;
            k->size = uint64_t(st.st_size);
#ifdef __APPLE__
            k->mtime = {st.st_mtimespec.tv_sec, int32_t(st.st_mtimespec.tv_nsec)};
#else
            k->mtime = {st.st_mtim.tv_sec, int32_t(st.st_mtim.tv_nsec)};
#endif
        } else continue;
        n.kids.push_back(std::move(k));
    }
}

std::unique_ptr<Node> from_disk(const std::string& path) {
    auto root = std::make_unique<Node>();
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    root->name = disc::path_name(p);
    root->dir = true;
    root->disk_path = p;
    scan_disk(*root, p);
    return root;
}

static Node* ensure_dir(Node* cur, const std::string& part) {
    Node* k = cur->child_exact(part);
    if (k) {
        if (!k->dir) throw VfsError(k->path() + " is a file and a folder in the archive");
        return k;
    }
    auto n = std::make_unique<Node>();
    n->name = part;
    n->dir = true;
    n->parent = cur;
    cur->kids.push_back(std::move(n));
    return cur->kids.back().get();
}

std::unique_ptr<Node> from_zip(const std::string& path, bool py, Offset off) {
    auto z = std::make_shared<ZipArchive>(path);
    auto root = std::make_unique<Node>();
    std::string stem = disc::path_name(path);
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
    root->name = stem;
    root->dir = true;
    for (size_t i = 0; i < z->entries.size(); i++) {
        auto& en = z->entries[i];
        std::string nm = en.name;
        bool is_dir = !nm.empty() && nm.back() == '/';
        if (py) std::replace(nm.begin(), nm.end(), '\\', '/');
        std::vector<std::string> parts;
        size_t st = 0;
        for (;;) {
            size_t sl = nm.find('/', st);
            std::string part = nm.substr(st, sl == std::string::npos ? std::string::npos : sl - st);
            if (part == "..") throw VfsError(z->label + ": unsafe path '" + en.name + "' in archive");
            if (!part.empty() && part != ".") parts.push_back(part);
            if (sl == std::string::npos) break;
            st = sl + 1;
        }
        if (!nm.empty() && nm[0] == '/') throw VfsError(z->label + ": unsafe path '" + en.name + "' in archive");
        if (parts.empty()) continue;
        Node* cur = root.get();
        size_t last = is_dir ? parts.size() : parts.size() - 1;
        for (size_t k = 0; k < last; k++) cur = ensure_dir(cur, parts[k]);
        if (is_dir) continue;
        Node* f = cur->child_exact(parts.back());
        if (f && f->dir) throw VfsError(f->path() + " is a file and a folder in the archive");
        if (!f) {
            auto n = std::make_unique<Node>();
            n->name = parts.back();
            n->parent = cur;
            cur->kids.push_back(std::move(n));
            f = cur->kids.back().get();
        }
        f->zip = z;
        f->entry = i;
        f->size = en.usize;
        int y = (en.dos_date >> 9) + 1980, mo = (en.dos_date >> 5) & 15, d = en.dos_date & 31;
        int h = en.dos_time >> 11, mi = (en.dos_time >> 5) & 63, s = (en.dos_time & 31) * 2;
        if (!py && en.has_ntfs) {
            uint64_t ft = en.ntfs_mtime;
            f->mtime = {int64_t(ft / 10000000ULL) - 11644473600LL, int32_t(ft % 10000000ULL * 100)};
        } else if (py && off) {
            f->mtime = {civil_to_epoch(y, mo, d, h, mi, s) - *off, 0};
        } else {
            f->mtime = {local_to_epoch(y, mo, d, h, mi, s), 0};
        }
    }
    return root;
}

}  // namespace sf3::vfs
