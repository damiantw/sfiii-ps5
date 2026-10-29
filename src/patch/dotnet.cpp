// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/dotnet.py.
#include "dotnet.h"

namespace sf3::dotnet {

U32 utf8_decode(const uint8_t* p, size_t n) {
    U32 out;
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x80) { out += char32_t(c); i++; continue; }
        int need;
        uint8_t lo = 0x80, hi = 0xBF;
        char32_t v;
        if (c >= 0xC2 && c <= 0xDF) { need = 1; v = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) {
            need = 2; v = c & 0x0F;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3; v = c & 0x07;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
        } else { out += U'\uFFFD'; i++; continue; }
        size_t j = i + 1;
        bool ok = true;
        for (int k = 0; k < need; k++, j++) {
            uint8_t lo_k = k == 0 ? lo : 0x80, hi_k = k == 0 ? hi : 0xBF;
            if (j >= n || p[j] < lo_k || p[j] > hi_k) { ok = false; break; }
            v = (v << 6) | (p[j] & 0x3F);
        }
        if (ok) { out += v; i = j; }
        else { out += U'\uFFFD'; i = j; }   // the maximal subpart (lead + valid continuations) is consumed
    }
    return out;
}

std::string utf8_encode(const U32& s) {
    std::string o;
    for (char32_t c : s) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
        else if (c >= 0xD800 && c <= 0xDFFF) o += '?';
        else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
        else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    }
    return o;
}

bool is_white_space(char32_t c) {
    switch (c) {
    case U'\t': case U'\n': case U'\v': case U'\f': case U'\r': case U' ': case 0x85: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: return true;
    default: return c >= 0x2000 && c <= 0x200A;
    }
}
static bool is_number_white(char32_t c) { return c == U'\t' || c == U'\n' || c == U'\v' || c == U'\f' || c == U'\r' || c == U' '; }
static bool is_python_space(char32_t c) { return is_white_space(c) || (c >= 0x1C && c <= 0x1F); }

template <class P> static U32 trim_by(const U32& s, P p) {
    size_t i = 0, j = s.size();
    while (i < j && p(s[i])) i++;
    while (j > i && p(s[j - 1])) j--;
    return s.substr(i, j - i);
}
U32 trim(const U32& s) { return trim_by(s, is_white_space); }
U32 trim_number(const U32& s) { return trim_by(s, is_number_white); }
U32 python_strip(const U32& s) { return trim_by(s, is_python_space); }

size_t utf16_len(const U32& s) {
    size_t n = 0;
    for (char32_t c : s) n += c > 0xFFFF ? 2 : 1;
    return n;
}
U32 utf16_sub(const U32& s, size_t length) {
    U32 out;
    size_t n = 0;
    for (char32_t c : s) {
        size_t w = c > 0xFFFF ? 2 : 1;
        if (n + w > length) {
            if (n < length) out += U'\uFFFD';
            break;
        }
        out += c;
        n += w;
    }
    return out;
}
U32 pad_right(const U32& s, size_t width, char32_t ch) {
    size_t l = utf16_len(s);
    return l >= width ? s : s + U32(width - l, ch);
}

static int hexval(char32_t c) {
    if (c >= U'0' && c <= U'9') return int(c - U'0');
    if (c >= U'a' && c <= U'f') return int(c - U'a' + 10);
    if (c >= U'A' && c <= U'F') return int(c - U'A' + 10);
    return -1;
}
static uint64_t parse_hex(const U32& text, uint64_t limit, const char* type) {
    U32 t = trim_number(text);
    if (t.empty()) throw FormatError("Input string was not in a correct format: '" + utf8_encode(text) + "'");
    uint64_t v = 0;
    bool big = false;
    for (char32_t c : t) {
        int h = hexval(c);
        if (h < 0) throw FormatError("Input string was not in a correct format: '" + utf8_encode(text) + "'");
        if (v > (UINT64_MAX >> 4)) big = true;
        v = (v << 4) | uint64_t(h);
    }
    if (big || v > limit)
        throw FormatError(std::string("Value was either too large or too small for ") + type + ": '" + utf8_encode(text) + "'");
    return v;
}
int32_t parse_hex_int(const U32& text) { return int32_t(uint32_t(parse_hex(text, 0xFFFFFFFFu, "an Int32"))); }
uint8_t parse_hex_byte(const U32& text) { return uint8_t(parse_hex(text, 0xFF, "a byte")); }

// ---------------------------------------------------------------- case tables

static bool lower_to_upper(char32_t c, char32_t& u) {
    if (c >= U'a' && c <= U'z') { u = c - 32; return true; }
    if (c < 0x80) return false;
    if (c == 0xB5) { u = 0x39C; return true; }
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) { u = c - 32; return true; }
    if (c == 0xFF) { u = 0x178; return true; }
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x131) { u = U'I'; return true; }
        if (c == 0x17F) { u = U'S'; return true; }
        bool odd_lower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (c == 0x138 || c == 0x149) return false;
        if (odd_lower ? (c & 1) : !(c & 1)) return false;
        u = c - 1;
        return true;
    }
    if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) { u = c - 32; return true; }
    if (c == 0x3C2) { u = 0x3A3; return true; }
    if (c >= 0x430 && c <= 0x44F) { u = c - 32; return true; }
    if (c >= 0x450 && c <= 0x45F) { u = c - 80; return true; }
    return false;
}
static bool upper_to_lower(char32_t c, char32_t& l) {
    if (c >= U'A' && c <= U'Z') { l = c + 32; return true; }
    if (c < 0x80) return false;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) { l = c + 32; return true; }
    if (c == 0x178) { l = 0xFF; return true; }
    if (c == 0x130) { l = U'i'; return true; }
    if (c >= 0x100 && c <= 0x17F) {
        bool odd_lower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (c == 0x138 || c == 0x149 || c == 0x131 || c == 0x17F) return false;
        if (odd_lower ? !(c & 1) : (c & 1)) return false;
        l = c + 1;
        return true;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) { l = c + 32; return true; }
    if (c >= 0x410 && c <= 0x42F) { l = c + 32; return true; }
    if (c >= 0x400 && c <= 0x40F) { l = c + 80; return true; }
    return false;
}
char32_t simple_upper(char32_t c) { char32_t u; return lower_to_upper(c, u) ? u : c; }
char32_t simple_lower(char32_t c) { char32_t l; return upper_to_lower(c, l) ? l : c; }
U32 upper(const U32& s) { U32 o; for (char32_t c : s) o += simple_upper(c); return o; }
U32 lower(const U32& s) { U32 o; for (char32_t c : s) o += simple_lower(c); return o; }
bool outside_tables(const U32& s) {
    for (char32_t c : s)
        if (c >= 0x180 && !(c >= 0x370 && c <= 0x45F) && !(c >= 0x300 && c <= 0x36F)) return true;
    return false;
}

// ---------------------------------------------------------------- culture compare

// NFD base letter for Latin-1 / Latin Extended-A letters with diacritics (0 = none).
static char32_t nfd_base(char32_t c) {
    static const char* l1 =  // U+00C0..U+00FF
        "AAAAAA" "\0" "CEEEEIIII" "\0" "NOOOOO" "\0\0" "UUUUY" "\0\0"
        "aaaaaa" "\0" "ceeeeiiii" "\0" "nooooo" "\0\0" "uuuuy" "\0" "y";
    static const char* xa =  // U+0100..U+017F
        "AaAaAaCcCcCcCcDd" "\0\0" "EeEeEeEeEeGgGgGgGgHh" "\0\0" "IiIiIiIiI" "\0\0\0" "JjKk" "\0" "LlLlLl"
        "\0\0\0\0" "NnNnNn" "\0\0\0" "OoOoOo" "\0\0" "RrRrRrSsSsSsSsTtTt" "\0\0" "UuUuUuUuUuUuWwYyYZzZzZz" "\0";
    if (c >= 0xC0 && c <= 0xFF) return char32_t(uint8_t(l1[c - 0xC0]));
    if (c >= 0x100 && c <= 0x17F) return char32_t(uint8_t(xa[c - 0x100]));
    return 0;
}

static const char32_t* kVariable = U" _-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$";

static void keys(const U32& s, std::vector<uint32_t>& prim, std::vector<uint8_t>& tert) {
    for (char32_t c0 : s) {
        if (c0 >= 0x300 && c0 <= 0x36F) continue;   // combining marks: secondary level only
        char32_t c = c0;
        if (char32_t b = nfd_base(c0)) c = b;
        uint32_t p = 0;
        bool found = false;
        for (int i = 0; kVariable[i]; i++) if (kVariable[i] == c) { p = uint32_t(i); found = true; break; }
        if (!found) {
            if (c >= U'0' && c <= U'9') p = 100 + (c - U'0');
            else if (c >= U'a' && c <= U'z') p = 200 + (c - U'a');
            else if (c >= U'A' && c <= U'Z') p = 200 + (c - U'A');
            else p = 1000 + uint32_t(simple_lower(c));
        }
        prim.push_back(p);
        tert.push_back(simple_lower(c) != c ? 1 : 0);
    }
}

int culture_compare(const U32& a, const U32& b) {
    std::vector<uint32_t> pa, pb;
    std::vector<uint8_t> ta, tb;
    keys(a, pa, ta);
    keys(b, pb, tb);
    if (pa != pb) return pa < pb ? -1 : 1;
    if (ta != tb) return ta < tb ? -1 : 1;
    return 0;
}

std::vector<uint32_t> ntfs_key(const U32& name) {
    std::vector<uint32_t> out;
    for (char32_t c : name) {
        uint32_t o = simple_upper(c);
        if (c == 0xDF || c == 0x149) o = c;   // full upper is two characters: unchanged
        if (o > 0xFFFF) {
            o -= 0x10000;
            out.push_back(0xD800 + (o >> 10));
            out.push_back(0xDC00 + (o & 0x3FF));
        } else {
            out.push_back(o);
        }
    }
    return out;
}

}  // namespace sf3::dotnet
