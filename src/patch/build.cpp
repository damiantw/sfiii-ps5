// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/{patcher,conflicts,isobuild,edcecc,output}.py.
#include "build.h"
#include "dfr.h"
#include "ipbin.h"
#include "disc/sha.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>

namespace sf3::build {
using namespace dotnet;
using vfs::Node;

constexpr uint64_t SECTOR = 2048;
constexpr uint64_t POSTGAP = 150;

static U32 u(const std::string& s) { return utf8_decode(s); }
static std::string a(const U32& s) { return utf8_encode(s); }
static std::string hexr(uint64_t lo, uint64_t hi) {
    char b[64];
    snprintf(b, sizeof b, "0x%llX-0x%llX", (unsigned long long)lo, (unsigned long long)hi);
    return b;
}

// ================================================================ conflicts
static bool node_file(const Node* n) { return n && !n->dir; }

std::vector<Issue> check_conflicts(const package::Mods& patches, const std::vector<std::string>& disc_names,
                                   const std::string& serial, const std::string& version) {
    std::vector<Issue> issues;
    struct Hist { std::string label, kind; std::shared_ptr<dfr::Patch> data; };
    std::map<std::string, std::vector<Hist>> touched;
    std::map<U32, std::pair<std::string, const Node*>> new_files;
    std::map<U32, std::string> upper_names;
    for (auto& n : disc_names) upper_names[upper(u(n))] = n;   // the last one wins, like a dict
    std::set<std::string> disc_set(disc_names.begin(), disc_names.end());
    for (auto& p : patches) {
        const Node* idx = p->dir;
        for (auto& name : disc_names) {
            std::vector<const Node*> rep;
            for (const Node* f : {idx->get({serial, name}), idx->get({serial, version, name})})
                if (node_file(f)) rep.push_back(f);
            const Node* d = idx->get({serial, version, name + ".DFR"});
            if (!node_file(d)) d = idx->get({serial, name + ".DFR"});
            if (d && d->dir) d = nullptr;
            if (rep.empty() && !d) continue;
            auto& hist = touched[name];
            if (!rep.empty()) {
                if (!hist.empty()) {
                    std::set<std::string> labels;
                    for (auto& h : hist) labels.insert(h.label);
                    std::string l;
                    for (auto& x : labels) l += (l.empty() ? "" : ", ") + x;
                    issues.push_back({"warning", p->label() + " replaces " + name + " after " + l + " changed it; its file wins"});
                }
                hist = {{p->label(), "replace", nullptr}};
            }
            if (d) {
                auto parsed = std::make_shared<dfr::Patch>(dfr::parse(d->read(), d->name));
                for (auto& h : hist) {
                    if (h.kind == "replace" && h.label != p->label())
                        issues.push_back({"warning", p->label() + " patches " + name + " on top of " + h.label + "'s replacement file"});
                    if (h.kind != "dfr") continue;
                    for (auto& aop : h.data->ops) {
                        if (aop.kind != dfr::Op::Replace) continue;
                        int64_t a0 = aop.offset, a1 = aop.offset + int64_t(aop.new_.size());
                        for (auto& bop : parsed->ops) {
                            if (bop.kind != dfr::Op::Replace) continue;
                            int64_t b0 = bop.offset, b1 = bop.offset + int64_t(bop.new_.size());
                            int64_t lo = std::max(a0, b0), hi = std::min(a1, b1);
                            if (lo >= hi) continue;
                            // Python slices clamp at the end of the shorter list
                            auto sl = [](const std::vector<uint8_t>& v, int64_t s, int64_t e) {
                                s = std::min<int64_t>(s, int64_t(v.size())); e = std::min<int64_t>(e, int64_t(v.size()));
                                return std::vector<uint8_t>(v.begin() + s, v.begin() + std::max(s, e));
                            };
                            bool same = sl(bop.old_, lo - b0, hi - b0) == sl(aop.new_, lo - a0, hi - a0);
                            issues.push_back({same ? "warning" : "error",
                                              h.label + " and " + p->label() + " both patch " + name + " at " + hexr(uint64_t(lo), uint64_t(hi - 1)) +
                                                  (same ? " (stacked on purpose)" : "; they conflict")});
                        }
                    }
                }
                hist.push_back({p->label(), "dfr", parsed});
            }
        }
        for (auto sub : {std::vector<std::string>{serial, version}, std::vector<std::string>{serial}}) {
            auto fl = idx->files(sub);
            if (!fl) continue;
            for (const Node* f : *fl) {
                const std::string& fn = f->name;
                if ((fn.size() >= 4 && fn.compare(fn.size() - 4, 4, ".DFR") == 0) || disc_set.count(fn)) continue;
                U32 up = upper(u(fn));
                auto hit = upper_names.find(up);
                if (hit != upper_names.end()) {
                    issues.push_back({"error", p->label() + " ships " + fn + ", which differs only in case from the disc's " + hit->second +
                                                   "; the wizard fails with 'File already exists'"});
                    continue;
                }
                auto prev = new_files.find(up);
                if (prev != new_files.end() && prev->second.first != p->label())
                    issues.push_back({"warning", prev->second.first + " and " + p->label() + " both add " + a(up) + "; only the first one found is used"});
                else if (prev == new_files.end())
                    new_files[up] = {p->label(), f};
            }
        }
    }
    return issues;
}

// ================================================================ isobuild names
static bool valid_file_name(const U32& s) {
    for (char32_t c : s) if (!((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || c == U'_' || c == U'.' || c == U';')) return false;
    return true;
}
static bool valid_dchar(char32_t c) { return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || c == U'_' || c == U'.'; }
static bool valid_achar(char32_t c) {
    return (c >= U' ' && c <= U'"') || (c >= U'%' && c <= U'/') || (c >= U':' && c <= U'?') || (c >= U'0' && c <= U'9') ||
           (c >= U'A' && c <= U'Z') || c == U'_';
}

static std::vector<U32> split_file_name(const U32& name) {
    std::vector<U32> p = {name, U"", U"1"};
    size_t dot = name.find(U'.');
    if (dot != U32::npos) {
        p[0] = name.substr(0, dot);
        if (name.find(U';') != U32::npos) {
            size_t semi = name.find(U';', dot + 1);
            if (semi == U32::npos) throw BuildError("bad file name '" + a(name) + "'");
            p[1] = name.substr(dot + 1, semi - dot - 1);
            p[2] = name.substr(semi + 1);
        } else p[1] = name.substr(dot + 1);
    } else if (name.find(U';') != U32::npos) {
        size_t semi = name.find(U';');
        p[0] = name.substr(0, semi);
        p[2] = name.substr(semi + 1);
    }
    U32 t = python_strip(p[2]);
    long long v = 0;
    bool digits = !t.empty();
    for (char32_t c : t) if (c < U'0' || c > U'9') digits = false;
    if (digits) {
        v = 0;
        for (char32_t c : t) { v = v * 10 + (c - U'0'); if (v > 100000) { v = 100000; } }
    }
    if (v < 1 || v > 32767) v = 1;
    p[2] = u(std::to_string(v));
    return p;
}
U32 normalize_file_name(const U32& name) {
    auto p = split_file_name(name);
    return p[0] + U"." + p[1] + U";" + p[2];
}
U32 short_file_name(const U32& name) {
    if (valid_file_name(name)) return name;
    U32 chars;
    for (char32_t c : upper(name)) chars += (valid_dchar(c) || c == U'.' || c == U';') ? c : U'_';
    auto p = split_file_name(chars);
    if (p[0].size() + p[1].size() > 30) p[1] = p[1].substr(0, 3);
    if (p[0].size() + p[1].size() > 30) p[0] = p[0].substr(0, 30 - p[1].size());
    return p[0] + U"." + p[1] + U";" + p[2];
}
static U32 identifier_text(const U32& s) { return (s.size() & 1) ? s + U";1" : s + U";1" + U32(1, U'\0'); }
static bool ends_with(const U32& s, const U32& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }
static size_t record_size(const U32& s) {
    size_t n;
    if (s.size() == 1 && s[0] <= 1) n = 1;
    else {
        n = s.size() + 2;
        if (s.rfind(U"CD", 0) == 0 || ends_with(s, U".MPG")) n += 14;
    }
    return 33 + n + (n % 2 == 0 ? 1 : 0);
}
static int compare_part(const U32& x, const U32& y, char32_t pad) {
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        char32_t p = i < x.size() ? x[i] : pad, q = i < y.size() ? y[i] : pad;
        if (p != q) return int(p) - int(q);
    }
    return 0;
}
static int compare_part_backwards(const U32& x, const U32& y, char32_t pad) {
    size_t n = std::max(x.size(), y.size()), ox = n - x.size(), oy = n - y.size();
    for (size_t i = 0; i < n; i++) {
        char32_t p = i >= ox ? x[i - ox] : pad, q = i >= oy ? y[i - oy] : pad;
        if (p != q) return int(q) - int(p);
    }
    return 0;
}
static std::vector<U32> split_name(const U32& s) {
    std::vector<U32> out;
    U32 cur;
    for (char32_t c : s) {
        if (c == U'.' || c == U';') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
int member_compare(const U32& x, const U32& y) {
    auto A = split_name(x), B = split_name(y);
    for (size_t i = 0; i < 2; i++) {
        int r = compare_part(A.size() > i ? A[i] : U"", B.size() > i ? B[i] : U"", U' ');
        if (r) return r;
    }
    return compare_part_backwards(A.size() > 2 ? A[2] : U"", B.size() > 2 ? B[2] : U"", U'0');
}

// ================================================================ isobuild records
static void both32(std::vector<uint8_t>& r, size_t o, uint32_t v) {
    for (int k = 0; k < 4; k++) { r[o + size_t(k)] = uint8_t(v >> (8 * k)); r[o + 7 - size_t(k)] = uint8_t(v >> (8 * k)); }
}
static void both16(std::vector<uint8_t>& r, size_t o, uint16_t v) {
    r[o] = uint8_t(v); r[o + 1] = uint8_t(v >> 8); r[o + 2] = uint8_t(v >> 8); r[o + 3] = uint8_t(v);
}
static void dir_time(std::vector<uint8_t>& r, size_t o, Time t) {
    if (!t) { for (int k = 0; k < 7; k++) r[o + size_t(k)] = 0; return; }
    int y, mo, d, h, mi, s;
    vfs::epoch_to_civil(*t, y, mo, d, h, mi, s);
    if (y < 1900) throw BuildError("Year is out of range");
    uint8_t b[7] = {uint8_t(y - 1900), uint8_t(mo), uint8_t(d), uint8_t(h), uint8_t(mi), uint8_t(s), 0};
    memcpy(&r[o], b, 7);
}
static std::vector<uint8_t> record_standard(const U32& ident, uint32_t lba, uint32_t size, Time t, uint8_t flags) {
    size_t n = record_size(ident);
    std::vector<uint8_t> r(n);
    r[0] = uint8_t(n);
    both32(r, 2, lba);
    both32(r, 10, size);
    dir_time(r, 18, t);
    r[25] = flags;
    both16(r, 28, 1);
    if (ident.size() == 1 && ident[0] <= 1) { r[32] = 1; r[33] = uint8_t(ident[0]); }
    else {
        r[32] = uint8_t(ident.size());
        for (size_t k = 0; k < ident.size(); k++) r[33 + k] = ident[k] < 0x80 ? uint8_t(ident[k]) : uint8_t('?');
    }
    return r;
}
static std::vector<uint8_t> record_member(const U32& sh, uint32_t lba, uint32_t size, Time t, bool xa) {
    U32 text = identifier_text(sh);
    std::vector<uint8_t> r(33, 0);
    both32(r, 2, lba);
    both32(r, 10, size);
    dir_time(r, 18, t);
    both16(r, 28, 1);
    for (char32_t c : text) r.push_back(uint8_t(c & 0xFF));
    size_t tl = text.size();
    while (tl > 0 && text[tl - 1] == 0) tl--;
    r[32] = uint8_t(tl);
    if (xa) {
        for (int k = 0; k < 4; k++) r.push_back(0);
        if (sh.rfind(U"CD", 0) == 0) r.push_back(0x41);
        else if (ends_with(sh, U".MPG")) r.push_back(0x31);
        for (uint8_t b : {0x11, 0x58, 0x41}) r.push_back(b);
        for (int k = 0; k < 6; k++) r.push_back(0);
    }
    r[0] = uint8_t(r.size() & 0xFF);
    return r;
}

Layout::Layout(std::vector<uint8_t> ip, std::vector<BuildFile> files, Time root_time,
               const std::vector<uint64_t>& audio_bytes, Identifiers ids)
    : ip_(std::move(ip)), files_(std::move(files)), root_time_(root_time), ids_(std::move(ids)) {
    if (ip_.size() != 16 * SECTOR) throw BuildError("IP.BIN area must be 16 sectors");
    std::map<U32, size_t> names;
    for (size_t i = 0; i < files_.size(); i++) {
        auto& f = files_[i];
        if (f.name.find('\\') != std::string::npos || f.name.find('/') != std::string::npos)
            throw BuildError("subdirectories are not supported (" + f.name + ")");
        if (ends_with(u(f.name), U".MPG")) throw BuildError(f.name + ": MPEG (Mode 2) files are not supported");
        U32 key = normalize_file_name(u(f.name));
        if (names.count(key)) throw BuildError("File already exists: " + f.name + " (the wizard fails here too)");
        names[key] = i;
        members_.emplace_back(key, i);
    }
    introsort(members_, [](const std::pair<U32, size_t>& x, const std::pair<U32, size_t>& y) { return member_compare(x.first, y.first); });
    uint64_t n = 68;
    for (auto& [key, i] : members_) {
        size_t rs = record_size(short_file_name(u(files_[i].name)));
        if (n % SECTOR + rs > SECTOR) n += SECTOR - n % SECTOR;
        n += rs;
    }
    root_size = (n + SECTOR - 1) / SECTOR * SECTOR;
    uint64_t pos = 40960 + root_size;
    for (size_t i = 0; i < files_.size(); i++) {
        auto& f = files_[i];
        if (f.is_cd()) continue;
        lba_[f.name] = uint32_t(pos / SECTOR);
        if (f.size) { order_.push_back(i); pos += (f.size + SECTOR - 1) / SECTOR * SECTOR; }
    }
    data_sectors = pos / SECTOR + POSTGAP;
    int64_t audio = 0;
    for (auto b : audio_bytes) audio += int64_t(b / 2352 * 2048);
    bool any_cd = false;
    for (auto& f : files_) any_cd |= f.is_cd();
    int64_t num7 = int64_t(pos);
    if (any_cd) num7 += 307200;
    int64_t sub = 0;
    if (!any_cd) sub = audio > 0 ? -614400 : -307200;
    for (auto& f : files_) {
        if (!f.is_cd()) continue;
        num7 += 307200;
        lba_[f.name] = uint32_t((num7 + int64_t(SECTOR) - 1) / int64_t(SECTOR));
        int64_t size = int64_t(f.size / 2352 * 2048);
        num7 += (size + int64_t(SECTOR) - 1) / int64_t(SECTOR) * int64_t(SECTOR);
        sub += (size + int64_t(SECTOR) - 1) / int64_t(SECTOR) * int64_t(SECTOR);
    }
    num7 += audio - sub;
    volume_space = uint64_t(num7 / int64_t(SECTOR));
    root_ = root_directory();
}

std::vector<uint8_t> Layout::root_directory() const {
    std::vector<uint8_t> buf(root_size);
    auto here = record_standard(U32(1, U'\0'), 20, uint32_t(root_size), root_time_, 2);
    auto up = record_standard(U32(1, U'\x01'), 20, uint32_t(root_size), root_time_, 2);
    memcpy(&buf[0], here.data(), 34);
    memcpy(&buf[34], up.data(), 34);
    size_t n = 68;
    for (auto& [key, i] : members_) {
        auto& f = files_[i];
        U32 sh = short_file_name(u(f.name));
        uint64_t size = f.is_cd() ? f.size / 2352 * 2048 : f.size;
        auto rec = record_member(sh, lba_.at(f.name), uint32_t(size), f.time, f.is_cd());
        size_t room = SECTOR - n % SECTOR;
        if (rec.size() >= room) rec.insert(rec.begin(), room, 0);
        if (n + rec.size() > buf.size())
            throw BuildError("the root directory overflows the space the wizard's builder reserves for it; the wizard would silently drop " + f.name);
        memcpy(&buf[n], rec.data(), rec.size());
        n += rec.size();
    }
    return buf;
}

static void write_string(std::vector<uint8_t>& buf, size_t off, size_t width, const std::string& s8, bool truncate,
                         bool (*check)(char32_t)) {
    U32 s = u(s8);
    if (check) for (char32_t c : s) if (!check(c)) throw BuildError("Attempt to write string with invalid characters: '" + s8 + "'");
    U32 text = s + U32(width, U' ');
    if (!truncate && s.size() > width) throw BuildError("Failed to write entire string '" + s8 + "'");
    for (size_t k = 0; k < width && k < text.size(); k++) buf[off + k] = uint8_t(text[k] & 0xFF);
}

std::vector<uint8_t> Layout::system_area() const {
    std::vector<uint8_t> out(40960, 0);
    memcpy(out.data(), ip_.data(), 32768);
    std::vector<uint8_t> pvd(SECTOR, 0);
    pvd[0] = 1;
    memcpy(&pvd[1], "CD001", 5);
    pvd[6] = 1;
    write_string(pvd, 8, 32, ids_.system, false, valid_achar);
    write_string(pvd, 40, 32, ids_.volume, true, nullptr);
    both32(pvd, 80, uint32_t(volume_space));
    both16(pvd, 120, 1);
    both16(pvd, 124, 1);
    both16(pvd, 128, 2048);
    both32(pvd, 132, 10);
    pvd[140] = 18;
    pvd[151] = 19;
    auto rr = record_standard(U32(1, U'\0'), 20, uint32_t(root_size), root_time_, 2);
    memcpy(&pvd[156], rr.data(), 34);
    auto up = [](std::string s) { for (auto& c : s) c = char(toupper((unsigned char)c)); return s; };
    write_string(pvd, 190, 129, ids_.volume_set, false, valid_dchar);
    write_string(pvd, 318, 129, ids_.publisher, false, valid_achar);
    write_string(pvd, 446, 129, ids_.preparer, false, valid_achar);
    write_string(pvd, 574, 129, ids_.application, false, valid_achar);
    write_string(pvd, 702, 37, up(ids_.copyright), false, valid_dchar);
    write_string(pvd, 739, 37, up(ids_.abstract), false, valid_dchar);
    write_string(pvd, 776, 37, up(ids_.bibliographic), false, valid_dchar);
    for (size_t off : {813, 830, 847, 864}) { memset(&pvd[off], '0', 16); pvd[off + 16] = 0; }
    pvd[881] = 1;
    memcpy(&out[32768], pvd.data(), SECTOR);
    out[34816] = 255;
    memcpy(&out[34817], "CD001", 5);
    out[34822] = 1;
    uint8_t lpt[10] = {1, 0, 20, 0, 0, 0, 1, 0, 0, 0};
    uint8_t mpt[10] = {1, 0, 0, 0, 0, 20, 0, 1, 0, 0};
    memcpy(&out[36864], lpt, 10);
    memcpy(&out[38912], mpt, 10);
    out.insert(out.end(), root_.begin(), root_.end());
    return out;
}

void Layout::user_data(const std::function<void(const uint8_t*, size_t)>& sink) const {
    auto sys = system_area();
    sink(sys.data(), sys.size());
    for (size_t i : order_) {
        auto& f = files_[i];
        auto data = f.read();
        if (data.size() != f.size) throw BuildError(f.name + " changed size while building");
        data.resize((data.size() + SECTOR - 1) / SECTOR * SECTOR, 0);
        sink(data.data(), data.size());
    }
    std::vector<uint8_t> gap(POSTGAP * SECTOR, 0);
    sink(gap.data(), gap.size());
}

// ================================================================ edcecc
namespace {
struct Tables {
    uint8_t F[256], B[256];
    uint32_t E[256];
    Tables() {
        for (int i = 0; i < 256; i++) {
            int j = (i << 1) ^ ((i & 0x80) ? 0x11D : 0);
            F[i] = uint8_t(j & 0xFF);
            B[i ^ (j & 0xFF)] = uint8_t(i);
            uint32_t e = uint32_t(i);
            for (int k = 0; k < 8; k++) e = (e >> 1) ^ ((e & 1) ? 0xD8018001u : 0);
            E[i] = e;
        }
    }
};
const Tables T;
uint8_t bcd(int n) { return uint8_t(((n / 10) << 4) | (n % 10)); }
void parity(uint8_t* s, int major_count, int minor_count, int major_mult, int minor_inc, int dest) {
    int size = major_count * minor_count;
    for (int major = 0; major < major_count; major++) {
        int index = (major >> 1) * major_mult + (major & 1);
        uint8_t x = 0, y = 0;
        for (int m = 0; m < minor_count; m++) {
            uint8_t t = s[12 + index];
            index += minor_inc;
            if (index >= size) index -= size;
            x ^= t;
            y ^= t;
            x = T.F[x];
        }
        x = T.B[T.F[x] ^ y];
        s[dest + major] = x;
        s[dest + major + major_count] = x ^ y;
    }
}
}  // namespace

void mode1(const uint8_t* user, uint32_t lba, uint8_t s[2352]) {
    memset(s, 0, 2352);
    memset(s + 1, 0xFF, 10);
    uint32_t a = lba + 150;
    s[12] = bcd(int(a / 4500));
    s[13] = bcd(int(a / 75 % 60));
    s[14] = bcd(int(a % 75));
    s[15] = 1;
    memcpy(s + 16, user, 2048);
    uint32_t e = 0;
    for (int i = 0; i < 2064; i++) e = (e >> 8) ^ T.E[(e ^ s[i]) & 0xFF];
    for (int k = 0; k < 4; k++) s[2064 + k] = uint8_t(e >> (8 * k));
    parity(s, 86, 24, 2, 86, 2076);
    parity(s, 52, 43, 86, 88, 2248);
}

// ================================================================ output
static std::string cue_address(uint64_t n) {
    char b[32];
    snprintf(b, sizeof b, "%02llu:%02llu:%02llu", (unsigned long long)(n / 4500), (unsigned long long)(n / 75 % 60),
             (unsigned long long)(n % 75));
    return b;
}
std::string cue_text(const std::string& bin_name, uint64_t data_sectors, size_t audio_tracks) {
    std::string s = "FILE \"" + bin_name + "\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n";
    if (audio_tracks)
        s += "  TRACK 02 AUDIO\r\n    INDEX 00 " + cue_address(data_sectors - 150) + "\r\n    INDEX 01 " + cue_address(data_sectors) + "\r\n";
    return s;
}

Written write_image(const std::string& bin_path, const Layout& layout,
                    const std::vector<std::function<void(const std::function<void(const uint8_t*, size_t)>&)>>& audio,
                    const std::function<void(uint64_t, uint64_t)>& progress) {
    FILE* out = fopen(bin_path.c_str(), "wb");
    if (!out) throw BuildError("cannot write " + bin_path);
    disc::Sha1 whole, track1;
    uint32_t lba = 0;
    std::vector<uint8_t> raw;
    auto put = [&](const uint8_t* p, size_t n) {
        if (fwrite(p, 1, n, out) != n) throw BuildError("write failed: " + bin_path);
        whole.update(p, n);
    };
    try {
        layout.user_data([&](const uint8_t* p, size_t n) {
            size_t k = n / SECTOR;
            raw.resize(k * 2352);
            for (size_t i = 0; i < k; i++) mode1(p + i * SECTOR, lba + uint32_t(i), &raw[i * 2352]);
            put(raw.data(), raw.size());
            track1.update(raw.data(), raw.size());
            lba += uint32_t(k);
            if (progress) progress(lba, layout.data_sectors);
        });
        if (lba != layout.data_sectors) throw BuildError("wrote a different number of data sectors than the layout says");
        for (auto& track : audio) track(put);
    } catch (...) { fclose(out); throw; }
    if (fclose(out) != 0) throw BuildError("write failed: " + bin_path);
    Written w;
    w.sectors = lba;
    w.track1_sha1 = track1.hex();
    w.whole_sha1 = whole.hex();
    std::string name = disc::path_name(bin_path);
    w.cue_path = bin_path.substr(0, bin_path.size() - 4) + ".cue";
    std::string cue = cue_text(name, lba, audio.size());
    FILE* c = fopen(w.cue_path.c_str(), "wb");
    if (!c || fwrite(cue.data(), 1, cue.size(), c) != cue.size()) { if (c) fclose(c); throw BuildError("cannot write " + w.cue_path); }
    fclose(c);
    return w;
}

// ================================================================ patcher
std::string stem(const std::string& name) {
    size_t k = name.rfind('.');
    if (k == std::string::npos || k == 0 || k + 1 == name.size()) return name;
    return name.substr(0, k);
}

static Time utc(const uint8_t t[7], const std::string& mode) {
    int64_t v;
    if (disc::iso_time_to_utc(t, mode, v)) return v;
    return std::nullopt;
}

Build::Build(const disc::Disc& d, const package::Package& pkg, const package::Mods& patches, Options o)
    : disc_(d), pkg_(pkg), opts_(std::move(o)) {
    auto h = ipbin::parse(d.ip_raw);
    serial = a(h.product);
    version = a(h.version);
    int64_t now = opts_.now ? *opts_.now : int64_t(time(nullptr));
    auto order = pkg.drive_order(serial);
    std::map<std::string, uint64_t> pos;
    for (size_t i = 0; i < order.size(); i++) pos.emplace(order[i], i);
    std::vector<const disc::Record*> recs;
    for (auto& r : d.files) recs.push_back(&r);
    std::stable_sort(recs.begin(), recs.end(), [&](const disc::Record* x, const disc::Record* y) {
        auto px = pos.find(x->name), py = pos.find(y->name);
        uint64_t kx = px == pos.end() ? (1ULL << 32) : px->second, ky = py == pos.end() ? (1ULL << 32) : py->second;
        return kx < ky;
    });
    Time root_utc = opts_.root_time ? opts_.root_time : utc(d.root.time, opts_.cdfs_time);
    // Python: `root_utc + offset if root_utc else None` (a datetime is always truthy)
    if (root_utc) root_time_local = *root_utc + opts_.host_utc_offset;
    std::set<U32> names_upper;
    auto add = [&](BuildFile bf) {
        U32 key = normalize_file_name(u(bf.name));
        if (names_upper.count(key))
            throw PatchError("File already exists: " + bf.name + " (two files whose names differ only in case end up as one; the wizard fails the same way)");
        names_upper.insert(key);
        files.push_back(std::move(bf));
    };
    for (const disc::Record* rec : recs) {
        FilePlan plan{rec->name, {}, 0, rec->size};
        if (rec->name.rfind("CD", 0) == 0) {
            plans.push_back(plan);
            add({rec->name, 0, root_utc, [] { return std::vector<uint8_t>(); }});
            continue;
        }
        std::shared_ptr<std::vector<uint8_t>> data;
        Time t = utc(rec->time, opts_.cdfs_time);
        bool changed = false;
        for (auto& p : patches) {
            const Node* idx = p->dir;
            for (const Node* f : {idx->get({serial, rec->name}), idx->get({serial, version, rec->name})}) {
                if (f && !f->dir) {
                    data = std::make_shared<std::vector<uint8_t>>(f->read());
                    t = vfs::python_mtime_seconds(f->mtime);
                    changed = true;
                    plan.steps.emplace_back(p->label(), "replace");
                }
            }
            const Node* df = idx->get({serial, version, rec->name + ".DFR"});
            if (!df || df->dir) df = idx->get({serial, rec->name + ".DFR"});
            if (df && !df->dir) {
                if (!data) data = std::make_shared<std::vector<uint8_t>>(d.read_file(*rec));
                auto patch = dfr::parse(df->read(), p->label() + ":" + df->name);
                try {
                    *data = dfr::apply(patch, std::move(*data), stem(rec->name));
                } catch (const dfr::DFRError& e) {
                    throw PatchError(std::string(e.what()) + "\n(patch " + p->label() + ", file " + df->relative_to(idx) +
                                     "; the original bytes do not match, so this patch does not fit this disc or an earlier patch changed the same bytes)");
                }
                t = now;
                changed = true;
                plan.steps.emplace_back(p->label(), "dfr");
            }
        }
        if (changed) {
            plan.size = data->size();
            plans.push_back(plan);
            add({rec->name, data->size(), t, [data] { return *data; }});
        } else {
            plan.size = rec->size;
            plans.push_back(plan);
            const disc::Disc* dp = &d;
            disc::Record r = *rec;
            add({rec->name, rec->size, t, [dp, r] { return dp->read_file(r); }});
        }
    }
    std::set<std::string> disc_names;
    for (auto* r : recs) disc_names.insert(r->name);
    std::vector<std::pair<std::shared_ptr<package::PatchDir>, const Node*>> candidates;
    auto is_dfr = [](const std::string& n) { return n.size() >= 4 && n.compare(n.size() - 4, 4, ".DFR") == 0; };
    for (auto& p : patches)
        if (auto fl = p->dir->files({serial, version}))
            for (const Node* f : *fl) if (!is_dfr(f->name)) candidates.emplace_back(p, f);
    for (auto& p : patches) {
        auto fl = p->dir->files({serial});
        if (!fl) throw PatchError("Could not find a part of the path '" + p->dir->path() + "/" + serial + "'");
        for (const Node* f : *fl) if (!is_dfr(f->name)) candidates.emplace_back(p, f);
    }
    std::set<std::string> seen;
    for (auto& [p, f] : candidates) {
        if (disc_names.count(f->name) || seen.count(f->name)) continue;
        seen.insert(f->name);
        std::string name = a(upper(u(f->name)));
        plans.push_back({name, {{p->label(), "new"}}, f->size, 0});
        auto data = std::make_shared<std::vector<uint8_t>>(f->read());
        add({name, data->size(), vfs::python_mtime_seconds(f->mtime), [data] { return *data; }});
    }
}

Layout Build::layout() const {
    std::string label = disc_.volume_id;
    while (!label.empty() && (label.back() == ' ' || label.back() == '\0')) label.pop_back();
    label = label.substr(0, label.find(' '));
    auto audio = disc_.audio_tracks();
    if (audio.size() > 1)
        throw PatchError("discs with more than one audio track are not supported (the wizard writes only the first one)");
    std::vector<uint64_t> sizes;
    for (auto* t : audio) sizes.push_back(150 * 2352 + (t->end - t->index1()));
    Identifiers ids;
    ids.volume = ids.volume_set = label;
    return Layout(ipbin::wizard_rewrite(disc_.ip_raw, pkg_.version, opts_.release_date), files, root_time_local, sizes, ids);
}

std::vector<std::function<void(const std::function<void(const uint8_t*, size_t)>&)>> Build::audio_data() const {
    std::vector<std::function<void(const std::function<void(const uint8_t*, size_t)>&)>> out;
    for (auto* t : disc_.audio_tracks())
        out.push_back([t](const std::function<void(const uint8_t*, size_t)>& sink) {
            std::vector<uint8_t> silence(150 * 2352, 0);
            sink(silence.data(), silence.size());
            auto a = t->audio_from_index1();
            sink(a.data(), a.size());
        });
    return out;
}

}  // namespace sf3::build
