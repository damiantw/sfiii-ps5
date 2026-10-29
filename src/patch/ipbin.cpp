// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/ipbin.py.
#include "ipbin.h"

namespace sf3::ipbin {
using namespace dotnet;

namespace {
struct Field { U32 Header::*m; int width; };
const Field kFields[] = {
    {&Header::hardware, 16}, {&Header::maker, 16}, {&Header::product, 10}, {&Header::version, 6},
    {&Header::date, 8}, {&Header::device, 8}, {&Header::areas, 10}, {nullptr, 6},
    {&Header::peripherals, 16}, {&Header::title, 112}, {nullptr, 16}, {&Header::ip_size, 4},
    {nullptr, 4}, {&Header::stack_m, 4}, {&Header::stack_s, 4}, {&Header::read_addr, 4},
    {&Header::read_size, 4}, {nullptr, 4}, {nullptr, 4}};

const char* kSecurity[][2] = {
    {"J", "For JAPAN.                  "}, {"T", "For TAIWAN and PHILIPINES.  "},
    {"U", "For USA and CANADA.         "}, {"B", "For BRAZIL.                 "},
    {"A", "For ASIA PAL area.          "}, {"K", "For KOREA.                  "},
    {"E", "For EUROPE.                 "}, {"L", "For LATIN AMERICA.          "}};

void put_le32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; i++) o.push_back(uint8_t(v >> (8 * i)));
}
void append(std::vector<uint8_t>& o, const std::string& s) { o.insert(o.end(), s.begin(), s.end()); }
std::string field(const U32& v, size_t width, size_t maxlen, char32_t pad) {
    return utf8_encode(pad_right(utf16_sub(v, std::min(utf16_len(v), maxlen)), width, pad));
}
U32 u(const char* s) { return utf8_decode(std::string(s)); }
}  // namespace

Header parse(const std::vector<uint8_t>& raw) {
    Header h;
    size_t pos = 0;
    for (auto& f : kFields) {
        if (f.m) {
            size_t a = std::min(pos, raw.size()), b = std::min(pos + size_t(f.width), raw.size());
            h.*f.m = trim(utf8_decode(raw.data() + a, b - a));
        }
        pos += size_t(f.width);
    }
    return h;
}

std::vector<uint8_t> serialize(const std::vector<uint8_t>& raw, const Header& h) {
    std::vector<uint8_t> buf(raw), out;
    append(out, field(h.hardware, 16, 16, U' '));
    append(out, field(h.maker, 16, 16, U' '));
    append(out, field(h.product, 10, 10, U' '));
    append(out, field(h.version, 6, 6, U' '));
    append(out, field(h.date, 8, 9, U' '));
    append(out, field(h.device, 8, 8, U' '));
    append(out, field(h.areas, 10, 10, U' '));
    out.insert(out.end(), 6, ' ');
    append(out, field(h.peripherals, 16, 16, U' '));
    append(out, field(h.title, 112, 112, U' '));
    out.insert(out.end(), 16, 0);
    append(out, field(h.ip_size, 4, 4, U'\0'));
    out.insert(out.end(), 4, 0);
    append(out, field(h.stack_m, 4, 4, U'\0'));
    append(out, field(h.stack_s, 4, 4, U'\0'));
    append(out, field(h.read_addr, 4, 4, U'\0'));
    append(out, field(h.read_size, 4, 4, U'\0'));
    out.insert(out.end(), 8, 0);
    if (out.size() > buf.size()) buf.resize(out.size());
    std::copy(out.begin(), out.end(), buf.begin());
    std::vector<uint8_t> area;
    for (auto& s : kSecurity)
        if (h.areas.find(char32_t(s[0][0])) != U32::npos) { put_le32(area, 150998688u); append(area, s[1]); }
    put_le32(area, 725615056u);
    put_le32(area, 150997248u);
    put_le32(area, 4194310u);
    size_t end = 0xE00 + area.size();
    if (end > buf.size()) buf.resize(end);
    std::copy(area.begin(), area.end(), buf.begin() + 0xE00);
    return buf;
}

Header wizard_header(const Header& h, const U32& version, const U32& release_date) {
    Header o = h;
    o.title = utf16_sub(pad_right(h.title, 32), 32) + pad_right(u("TRANSLATION"), 16) +
              pad_right(u("VERSION ") + version, 16) + u("SF3TRANS.SHININGFORCECENTRAL.COM");
    o.product = u("SF3TRANS");
    o.version = u("V") + version;
    o.date = release_date;
    o.areas = u("JTUBKAEL");
    o.peripherals = u("AJE");
    return o;
}

std::vector<uint8_t> wizard_rewrite(const std::vector<uint8_t>& raw, const std::string& version, const std::string& release_date) {
    return serialize(raw, wizard_header(parse(raw), utf8_decode(version), utf8_decode(release_date)));
}

}  // namespace sf3::ipbin
